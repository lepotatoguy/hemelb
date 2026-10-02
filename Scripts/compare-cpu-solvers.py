#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Run two matched CPU configurations and compare extracted pressure and velocity.

Install python-tools first. Configure both inputs to write one whole.xtr frame
at the same physical state. Some legacy solvers label the initial state step 1,
while HemeLB labels it step 0. Output selection must account for that difference.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import numpy as np
from hlb.parsers.extraction import ExtractedProperty


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compare(reference, candidate, args):
    readers = [
        ExtractedProperty(str(p / "Extracted/whole.xtr"))
        for p in [reference, candidate]
    ]
    assert all(
        len(reader.times) == 1 for reader in readers
    ), "Expected one matched output frame"
    a, b = [reader.GetByIndex(0) for reader in readers]
    order_a = np.lexsort(a.grid.T[::-1])
    order_b = np.lexsort(b.grid.T[::-1])
    a, b = a[order_a], b[order_b]
    assert np.array_equal(a.grid, b.grid), "Geometry site sets differ"
    result = {
        "reference_step": int(readers[0].times[0]),
        "candidate_step": int(readers[1].times[0]),
        "sites": len(a),
    }
    scales = {"Pa": 1, "mmHg": 133.3223874}
    for name, atol in [
        ("pressure", args.pressure_atol),
        ("velocity", args.velocity_atol),
    ]:
        x, y = np.asarray(a[name], dtype=float), np.asarray(b[name], dtype=float)
        if name == "pressure":
            x *= scales[args.reference_pressure_unit]
            y *= scales[args.candidate_pressure_unit]
        error = np.abs(x - y)
        result[name] = {
            "maximum_absolute_error": float(error.max()),
            "rms_error": float(np.sqrt(np.mean(error**2))),
            "atol": atol,
            "rtol": args.rtol,
            "passed": bool(np.allclose(x, y, rtol=args.rtol, atol=atol)),
        }
    if not all(result[name]["passed"] for name in ["pressure", "velocity"]):
        raise AssertionError(result)
    return result


def run(args, label, executable, config, ranks, repeat):
    destination = args.output / f"{label}-{ranks}-{repeat}"
    command = [
        args.launcher,
        *shlex.split(os.environ.get("MPIRUN_FLAGS", "")),
        "-np",
        str(ranks + (args.reference_rank_offset if label == "reference" else 0)),
        str(executable),
        "-in",
        str(config),
        "-out",
        str(destination),
    ]
    if args.peak_rss:
        command = command[: command.index(str(executable))] + [
            sys.executable,
            str(args.output / "measure_rank.py"),
            str(args.output / f"{label}-{ranks}-{repeat}-rss"),
            *command[command.index(str(executable)) :],
        ]
    start = time.perf_counter()
    with (args.output / f"{label}-{ranks}-{repeat}.log").open("w") as log:
        subprocess.run(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=args.timeout,
        )
    elapsed = time.perf_counter() - start
    report = ET.parse(destination / "report.xml")
    phases = {
        node.findtext("name"): float(node.findtext("max"))
        for node in report.findall(".//timer")
    }
    rank_rss = (
        [
            json.loads(path.read_text())
            for path in sorted(args.output.glob(f"{label}-{ranks}-{repeat}-rss-*.json"))
        ]
        if args.peak_rss
        else None
    )
    if args.peak_rss:
        assert len(rank_rss) == ranks + (
            args.reference_rank_offset if label == "reference" else 0
        )
    active_ranks = sum(
        int(domain.findtext("sites")) > 0
        for domain in report.findall(".//geometry/domain")
    )
    performance = report.find(".//performance")
    return destination, {
        "solver": label,
        "ranks": ranks,
        "allocated_ranks": ranks
        + (args.reference_rank_offset if label == "reference" else 0),
        "active_ranks": active_ranks,
        "repeat": repeat,
        "wall_seconds": elapsed,
        "per_rank_peak_rss_bytes": rank_rss,
        "phase_max_seconds": phases,
        "performance": (
            None
            if performance is None
            else {child.tag: child.text for child in performance if len(child) == 0}
        ),
        "command": command,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in [
        "reference",
        "candidate",
        "reference_config",
        "candidate_config",
        "output",
    ]:
        p.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    p.add_argument(
        "--peak-rss",
        action="store_true",
        help="measure solver child-process peak RSS per rank using a Python wrapper",
    )
    p.add_argument("--launcher", default="mpirun")
    p.add_argument("--ranks", type=int, nargs="+", default=[1, 2, 4])
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument(
        "--reference-rank-offset",
        type=int,
        default=0,
        help="extra reference ranks, e.g. 1 when it reserves an I/O rank",
    )
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--reference-pressure-unit", choices=["Pa", "mmHg"], required=True)
    p.add_argument("--candidate-pressure-unit", choices=["Pa", "mmHg"], default="Pa")
    p.add_argument("--pressure-atol", type=float, default=5e-5)
    p.add_argument("--velocity-atol", type=float, default=1e-8)
    p.add_argument("--rtol", type=float, default=1e-5)
    args = p.parse_args()
    if (
        args.reference_rank_offset < 0
        or args.repeats < 1
        or any(n < 1 for n in args.ranks)
    ):
        p.error("Rank counts and repeats must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    if args.peak_rss:
        (args.output / "measure_rank.py").write_text(
            """import json, os, platform, resource, subprocess, sys
from pathlib import Path
rank = os.environ.get('OMPI_COMM_WORLD_RANK', os.environ.get('PMI_RANK', os.environ.get('PMIX_RANK')))
if rank is None:
    raise RuntimeError('MPI launcher did not provide a rank identifier')
status = subprocess.run(sys.argv[2:]).returncode
rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
Path(sys.argv[1] + '-' + rank + '.json').write_text(json.dumps({'rank': int(rank), 'peak_rss_bytes': int(rss * (1 if platform.system() == 'Darwin' else 1024))}))
sys.exit(status)
"""
        )
    result = {
        "measurement": {
            "reference_rank_offset": args.reference_rank_offset,
            "rss_wrapper": args.peak_rss,
        },
        "machine": platform.platform(),
        "processor": platform.processor(),
        "python": platform.python_version(),
        "mpi": subprocess.check_output([args.launcher, "--version"], text=True),
        "inputs": {},
        "runs": [],
        "comparisons": [],
    }
    for label in ["reference", "candidate"]:
        executable = getattr(args, label).resolve()
        config = getattr(args, label + "_config").resolve()
        geometry = (
            config.parent / ET.parse(config).find("geometry/datafile").get("path")
        ).resolve()
        result["inputs"][label] = {
            "executable": str(executable),
            "executable_sha256": digest(executable),
            "xml": str(config),
            "xml_sha256": digest(config),
            "geometry_sha256": digest(geometry),
        }
    for ranks in args.ranks:
        for repeat in range(args.repeats):
            outputs = {}
            # Alternate order to reduce systematic effects from machine warmup.
            for label in (
                ["reference", "candidate"]
                if repeat % 2 == 0
                else ["candidate", "reference"]
            ):
                outputs[label], record = run(
                    args,
                    label,
                    getattr(args, label).resolve(),
                    getattr(args, label + "_config").resolve(),
                    ranks,
                    repeat,
                )
                result["runs"].append(record)
            result["comparisons"].append(
                {
                    "ranks": ranks,
                    "repeat": repeat,
                    **compare(outputs["reference"], outputs["candidate"], args),
                }
            )
            (args.output / "comparison.json").write_text(
                json.dumps(result, indent=2) + "\n"
            )
    result["median_wall_seconds"] = {
        label: {
            str(ranks): statistics.median(
                r["wall_seconds"]
                for r in result["runs"]
                if r["solver"] == label and r["ranks"] == ranks
            )
            for ranks in args.ranks
        }
        for label in ["reference", "candidate"]
    }
    (args.output / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    print(args.output / "comparison.json")


if __name__ == "__main__":
    main()
