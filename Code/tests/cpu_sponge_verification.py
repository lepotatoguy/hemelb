#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Measure incident and returned acoustic pulses on the bundled cylinder.

Windows are fixed before running: incident steps 30 through 65 and returned
steps 80 through 130 at the centre plane. The pressure/velocity characteristic
split is a diagnostic for this viscous, finite-radius case, not an exact
reflection coefficient for arbitrary geometries or frequencies.
"""
import argparse
import json
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET
import numpy as np
from hlb.parsers.extraction import ExtractedProperty
from checkpoint_restart_mpi import run

EXAMPLE = Path(__file__).resolve().parents[2] / "examples"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--sponge", required=True)
    parser.add_argument("--launcher", default="mpirun")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = {}
    with tempfile.TemporaryDirectory(prefix="hemelb-sponge-pulse-") as directory:
        work = Path(directory)
        profile = work / "pressure.txt"
        profile.write_text("0 0\n0.0015 0\n0.002 1\n0.0025 0\n0.02 0\n")
        for label in ["baseline", "sponge"]:
            tree = ET.parse(EXAMPLE / "first-run.xml")
            root = tree.getroot()
            root.find("geometry/datafile").set("path", str(EXAMPLE / "first-run.gmy"))
            root.find("simulation").remove(root.find("simulation/checkpoint"))
            condition = root.find("inlets/inlet/condition")
            condition.clear()
            condition.attrib.update(
                type="pressure", subtype="file", units="Pa", timing="periodic"
            )
            ET.SubElement(condition, "path", value=str(profile))
            root.find("properties/propertyoutput").set("period", "1")
            ET.SubElement(root, "decomposition", method="octree")
            if label == "sponge":
                sponge = ET.SubElement(root.find("initialconditions"), "sponge_layer")
                for name, value, units in [
                    ("viscosity_ratio", 4, "dimensionless"),
                    ("width", 0.0008, "m"),
                    ("lifetime", 1000, "lattice"),
                ]:
                    ET.SubElement(sponge, name, value=str(value), units=units)
            source = work / (label + ".xml")
            tree.write(source)
            output = work / label
            run(args.launcher, getattr(args, label), 2, source, output)
            reader = ExtractedProperty(str(output / "Extracted/whole.xtr"))
            plus, minus = [], []
            for index in range(len(reader.times)):
                frame = reader.GetByIndex(index)
                mask = np.abs(frame.position[:, 2]) <= 0.51e-4
                pressure = np.mean(frame.pressure[mask])
                velocity = np.mean(frame.velocity[mask, 2])
                impedance = 1000 / np.sqrt(3)
                plus.append(0.5 * (pressure + impedance * velocity))
                minus.append(0.5 * (pressure - impedance * velocity))
            incident = max(
                abs(plus[i]) for i, step in enumerate(reader.times) if 30 <= step <= 65
            )
            returned = max(
                abs(minus[i])
                for i, step in enumerate(reader.times)
                if 80 <= step <= 130
            )
            assert np.isfinite(incident) and np.isfinite(returned) and incident > 0
            results[label] = {
                "incident_peak_Pa": float(incident),
                "returned_peak_Pa": float(returned),
                "returned_to_incident": float(returned / incident),
            }
    args.output.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
