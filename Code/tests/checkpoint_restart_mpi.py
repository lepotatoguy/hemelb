#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Exercise checkpoint restarts with a changed MPI process count."""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET


RESOURCE_DIR = Path(__file__).resolve().parent / "resources"


def make_config(path, checkpoint=None, offsets=None):
    tree = ET.parse(RESOURCE_DIR / "large_cylinder.xml")
    root = tree.getroot()
    root.find("simulation/steps").set("value", "4")
    initial = root.find("initialconditions")
    if checkpoint is not None:
        initial.remove(initial.find("pressure"))
        ET.SubElement(initial, "checkpoint", file=str(checkpoint), offsets=str(offsets))
    properties = ET.Element("properties")
    ET.SubElement(properties, "checkpoint", file="checkpoint_%d.xtr", period="2")
    root.append(properties)
    tree.write(path, encoding="unicode", xml_declaration=True)


def run(launcher, executable, ranks, config, output):
    command = [launcher, "-np", str(ranks), executable, "-in", str(config), "-out", str(output)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} failed:\n{result.stdout}")


def checkpoint_at(output, timestep):
    matches = []
    for path in (output / "Extracted").glob("checkpoint_*.xtr"):
        data = path.read_bytes()
        if struct.unpack_from(">Q", data, 92)[0] == timestep:
            matches.append(path)
    if len(matches) != 1:
        raise AssertionError(f"Expected one checkpoint at timestep {timestep}, got {matches}")
    expected_name = f"checkpoint_{timestep:03d}.xtr"
    if matches[0].name != expected_name:
        raise AssertionError(f"Expected {expected_name}, got {matches[0].name}")
    return matches[0]


def saved_sites(path):
    data = path.read_bytes()
    count = struct.unpack_from(">Q", data, 44)[0]
    vectors = struct.unpack_from(">I", data, 80)[0]
    record_size = 12 + 8 * vectors
    if len(data) != 100 + count * record_size:
        raise AssertionError("Unexpected checkpoint record length")
    sites = {}
    for offset in range(100, len(data), record_size):
        coordinate = struct.unpack_from(">III", data, offset)
        values = struct.unpack_from(f">{vectors}d", data, offset + 12)
        if coordinate in sites:
            raise AssertionError(f"Duplicate site {coordinate}")
        sites[coordinate] = values
    return sites


def check_direction(work, launcher, executable, writers, readers):
    label = f"{writers}-to-{readers}"
    fresh = work / f"{label}-fresh.xml"
    make_config(fresh)
    saved = work / f"{label}-saved"
    run(launcher, executable, writers, fresh, saved)

    restart = work / f"{label}-restart.xml"
    source_checkpoint = checkpoint_at(saved, 2)
    make_config(restart, source_checkpoint, saved / "Extracted/checkpoint_.off")
    resumed = work / f"{label}-resumed"
    run(launcher, executable, readers, restart, resumed)

    expected = saved_sites(checkpoint_at(saved, 4))
    actual = saved_sites(checkpoint_at(resumed, 4))
    if expected != actual:
        raise AssertionError(f"{label}: checkpoint values differ")
    print(f"{label}: {len(actual)} sites and {sum(map(len, actual.values()))} values match")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hemelb", type=Path, required=True)
    parser.add_argument("--mpirun", default="mpirun")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hemelb-restart-") as directory:
        work = Path(directory)
        shutil.copyfile(RESOURCE_DIR / "large_cylinder.gmy", work / "large_cylinder.gmy")
        check_direction(work, args.mpirun, str(args.hemelb.resolve()), 2, 1)
        check_direction(work, args.mpirun, str(args.hemelb.resolve()), 1, 2)
        check_direction(work, args.mpirun, str(args.hemelb.resolve()), 2, 4)
        check_direction(work, args.mpirun, str(args.hemelb.resolve()), 1, 1)


if __name__ == "__main__":
    main()
