#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Compare Windkessel continuation across decompositions and MPI rank counts."""
import argparse
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET
from checkpoint_restart_mpi import run, saved_sites, checkpoint_at, RESOURCE_DIR


def config(path, model, version=6):
    tree = ET.parse(RESOURCE_DIR / "large_cylinder.xml")
    root = tree.getroot()
    root.set("version", str(version))
    root.find("geometry/datafile").set("path", str(RESOURCE_DIR / "large_cylinder.gmy"))
    root.find("simulation/steps").set("value", "20")
    ET.SubElement(root.find("simulation"), "checkpoint", period="10")
    ET.SubElement(root, "decomposition", method="octree")
    root.append(ET.Element("properties"))
    condition = root.find("outlets/outlet/condition")
    condition.clear()
    condition.attrib.update(type="pressure", subtype=model)
    pairs = (
        [("Rc", 1e8, "kg/m^4*s"), ("Rp", 2e8, "kg/m^4*s"), ("Cp", 1e-8, "m^4*s^2/kg")]
        if model == "WK3"
        else [("R", 2e8, "kg/m^4*s"), ("C", 1e-8, "m^4*s^2/kg")]
    )
    pairs += [("area", 3e-10, "m^2")]
    if model == "fileWK":
        weights = path.with_suffix(".weights")
        weights.write_text("0 0 0 1\n")
        ET.SubElement(condition, "path", value=str(weights))
    else:
        pairs += [("radius", 1e-5, "m")]
    for tag, value, units in pairs:
        ET.SubElement(condition, tag, value=str(value), units=units)
    tree.write(path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--hemelb", required=True)
    p.add_argument("--launcher", default="mpirun")
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix="hemelb-wk-") as directory:
        work = Path(directory)
        for model in ["WK2", "WK3", "fileWK"]:
            source = work / (model + ".xml")
            config(source, model)
            full = work / (model + "-full")
            run(args.launcher, args.hemelb, 2, source, full)
            restart = checkpoint_at(full, 10).parent / "restart.xml"
            resumed = work / (model + "-resumed")
            run(args.launcher, args.hemelb, 1, restart, resumed)
            original = saved_sites(checkpoint_at(full, 20))
            restored = saved_sites(checkpoint_at(resumed, 20))
            assert original.keys() == restored.keys()
            error = max(
                abs(a - b)
                for site in original
                for a, b in zip(original[site], restored[site])
            )
            assert error < 1e-12, (model, error)
            states = [
                ET.parse(checkpoint_at(out, 20).parent / "restart.xml")
                .find("outlets/outlet/condition/state")
                .attrib
                for out in [full, resumed]
            ]
            for key in states[0]:
                assert (
                    abs(float.fromhex(states[0][key]) - float.fromhex(states[1][key]))
                    < 1e-12
                )
            legacy = work / (model + "-legacy.xml")
            config(legacy, model, 3)
            run(args.launcher, args.hemelb, 1, legacy, work / (model + "-legacy"))
            print(model + ": 2-to-1 checkpoint continuation and legacy XML3 passed")


if __name__ == "__main__":
    main()
