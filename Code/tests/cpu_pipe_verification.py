#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Check a steady cylinder against Poiseuille flow at two spatial resolutions.

Requires python-tools, PyYAML and the geometry-tool CLI. The normalised RMS
velocity tolerance is fixed at 5 percent before running. This checks one rigid
straight cylinder, not compliant walls or arbitrary vascular geometries.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import numpy as np
import yaml
from hlb.parsers.extraction import ExtractedProperty
from checkpoint_restart_mpi import run

EXAMPLE = Path(__file__).resolve().parents[2] / "examples"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--hemelb", required=True)
    p.add_argument("--geometry-tool", default="hlb-gmy-cli")
    p.add_argument("--launcher", default="mpirun")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix="hemelb-pipe-verification-") as directory:
        for refinement in [1, 2]:
            work = Path(directory) / str(refinement)
            work.mkdir()
            shutil.copy(EXAMPLE / "first-run.stl", work)
            profile = yaml.safe_load((EXAMPLE / "first-run.pr2").read_text())
            profile["VoxelSize"] = 0.1 / refinement
            dx = profile["VoxelSize"] * 1e-3
            dt = 2.5e-4 / refinement**2
            steps = 1200 * refinement**2
            profile["TimeStepSeconds"] = dt
            profile["DurationSeconds"] = dt * steps
            (work / "case.pr2").write_text(yaml.safe_dump(profile))
            with (work / "geometry.log").open("w") as log:
                subprocess.run(
                    [
                        args.geometry_tool,
                        "case.pr2",
                        "--geometry",
                        "case.gmy",
                        "--xml",
                        "case.xml",
                    ],
                    cwd=work,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=True,
                )
            tree = ET.parse(work / "case.xml")
            root = tree.getroot()
            root.find("geometry/datafile").set("path", str(work / "case.gmy"))
            root.find("simulation/steps").set("value", str(steps))
            properties = root.find("properties")
            if properties is None:
                properties = ET.SubElement(root, "properties")
            output = ET.SubElement(
                properties,
                "propertyoutput",
                file="whole.xtr",
                period="1",
                start=str(steps - 1),
                stop=str(steps - 1),
            )
            ET.SubElement(output, "geometry", type="whole")
            ET.SubElement(output, "field", type="velocity")
            ET.SubElement(root, "decomposition", method="octree")
            tree.write(work / "case.xml")
            output_dir = work / "results"
            run(args.launcher, args.hemelb, 2, work / "case.xml", output_dir)
            reader = ExtractedProperty(str(output_dir / "Extracted/whole.xtr"))
            assert len(reader.times) == 1
            frame = reader.GetByIndex(0)
            middle = np.abs(frame.position[:, 2]) <= 0.51 * dx
            positions, velocity = frame.position[middle], frame.velocity[middle]
            radius = 0.0005
            gradient = 1.333223874 / 0.003
            exact = (
                gradient
                / (4 * 0.004)
                * (radius**2 - np.sum(positions[:, :2] ** 2, axis=1))
            )
            assert len(exact) > 0 and np.all(exact > 0)
            error = np.sqrt(np.mean((velocity[:, 2] - exact) ** 2)) / np.sqrt(
                np.mean(exact**2)
            )
            results.append(
                {
                    "refinement": refinement,
                    "voxel_m": dx,
                    "step_s": dt,
                    "updates": steps,
                    "sites": len(frame),
                    "sample_sites": len(exact),
                    "normalised_rms_error": float(error),
                    "maximum_velocity_error_ms": float(
                        np.max(np.abs(velocity[:, 2] - exact))
                    ),
                    "passed": bool(np.isfinite(error) and error < 0.05),
                }
            )
            args.output.write_text(json.dumps(results, indent=2) + "\n")
    assert all(result["passed"] for result in results), results
    assert (
        results[1]["normalised_rms_error"] < results[0]["normalised_rms_error"]
    ), results
    print(json.dumps(results))


if __name__ == "__main__":
    main()
