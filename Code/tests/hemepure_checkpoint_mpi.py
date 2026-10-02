#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Load a checkpoint written by a real D3Q19 HemePure executable into HemeLB."""
import argparse
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET
import numpy as np
from hlb.parsers.extraction import ExtractedProperty
from checkpoint_restart_mpi import run, checkpoint_at, saved_sites
from legacy_compatibility_mpi import legacy_xml

EXAMPLE = Path(__file__).resolve().parents[2] / "examples"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--hemepure", required=True)
    p.add_argument("--hemelb", required=True)
    p.add_argument("--launcher", default="mpirun")
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix="hemelb-real-checkpoint-") as directory:
        work = Path(directory)
        tree = legacy_xml(ET.parse(EXAMPLE / "first-run.xml"), 3)
        root = tree.getroot()
        root.remove(root.find("visualisation"))
        root.find("geometry/datafile").set("path", str(EXAMPLE / "first-run.gmy"))
        root.find("simulation").remove(root.find("simulation/checkpoint"))
        root.find("simulation/steps").set("value", "20")
        for condition in root.findall(".//condition"):
            ET.SubElement(condition, "radius", value="0.0005", units="m")
            ET.SubElement(condition, "area", value="7.853981633974483e-7", units="m^2")
        properties = root.find("properties")
        properties.clear()
        output = ET.SubElement(
            properties,
            "propertyoutput",
            file="saved.xtr",
            period="1",
            start="20",
            stop="20",
        )
        ET.SubElement(output, "geometry", type="whole")
        ET.SubElement(output, "field", type="distributions")
        source = work / "reference.xml"
        tree.write(source)
        reference = work / "reference"
        run(args.launcher, args.hemepure, 2, source, reference)
        checkpoint = reference / "Extracted/saved.xtr"
        reader = ExtractedProperty(str(checkpoint))
        assert reader.version == 4
        frame = reader.GetByIndex(0)
        expected = {
            tuple(int(x) for x in row.grid): tuple(float(x) for x in row.distributions)
            for row in frame
        }
        assert len(expected) == 2400
        results = []
        for ranks in [1, 3]:
            tree = ET.parse(EXAMPLE / "first-run.xml")
            root = tree.getroot()
            root.find("geometry/datafile").set("path", str(EXAMPLE / "first-run.gmy"))
            root.find("simulation/steps").set("value", "24")
            root.find("simulation/checkpoint").set("period", "2")
            root.find("properties").clear()
            initial = root.find("initialconditions")
            initial.clear()
            ET.SubElement(initial, "time", units="lattice", value="20")
            ET.SubElement(
                initial,
                "checkpoint",
                file=str(checkpoint),
                offsets=str(checkpoint.with_suffix(".off")),
            )
            ET.SubElement(root, "decomposition", method="octree")
            source = work / f"candidate-{ranks}.xml"
            tree.write(source)
            output = work / f"candidate-{ranks}"
            run(args.launcher, args.hemelb, ranks, source, output)
            assert (
                saved_sites(checkpoint_at(output, 20)) == expected
            ), "Float checkpoint did not load exactly"
            results.append(saved_sites(checkpoint_at(output, 24)))
        assert results[0].keys() == results[1].keys()
        error = max(
            abs(a - b)
            for grid in results[0]
            for a, b in zip(results[0][grid], results[1][grid])
        )
        assert np.isfinite(error) and error < 1e-12
        print(
            f"Actual HemePure v4 checkpoint: exact float promotion, 2-to-1 and 2-to-3 continuation passed; difference {error:.17g}"
        )


if __name__ == "__main__":
    main()
