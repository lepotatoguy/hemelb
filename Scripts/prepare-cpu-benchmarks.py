#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Prepare matched branching-vessel CPU inputs for HemeLB and HemePure."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sixbranch-steps", type=int, default=600)
    parser.add_argument("--lores-steps", type=int, default=300)
    parser.add_argument("--hires-steps", type=int, default=60)
    args = parser.parse_args()
    if min(args.sixbranch_steps, args.lores_steps, args.hires_steps) < 1:
        parser.error("Update counts must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    cases = [
        ("sixbranch", "FiveExit/input.xml", args.sixbranch_steps),
        ("bifurcation-lores", "bifurcation/bifurcation_lores/input.xml", args.lores_steps),
        ("bifurcation-hires", "bifurcation/bifurcation_hires/input.xml", args.hires_steps),
    ]
    manifest = []
    for name, relative, updates in cases:
        source = root / "cases/hemepure" / relative
        base = ET.parse(source).getroot()
        geometry = (source.parent / base.find("geometry/datafile").get("path")).resolve()
        with geometry.open("rb") as stream:
            preamble = struct.unpack(">8I", stream.read(32))
            blocks = preamble[3] * preamble[4] * preamble[5]
            sites = sum(record[0] for record in struct.iter_unpack(">3I", stream.read(blocks * 12)))
        for child in list(base):
            if child.tag not in {"simulation", "geometry", "inlets", "outlets", "initialconditions"}:
                base.remove(child)
        sim = base.find("simulation")
        for child in list(sim):
            if child.tag not in {"step_length", "steps", "stresstype", "voxel_size", "origin"}:
                sim.remove(child)
        sim.find("steps").set("value", str(updates))
        base.find("geometry/datafile").set("path", str(geometry))
        for group in ("inlets", "outlets"):
            for iolet in base.findall(group + "/*"):
                mean = iolet.find("condition/mean")
                value = mean.get("value") if mean is not None else "0.0"
                for child in list(iolet):
                    if child.tag not in {"normal", "position"}:
                        iolet.remove(child)
                condition = ET.SubElement(iolet, "condition", type="pressure", subtype="cosine")
                for tag, unit, val in [("amplitude", "mmHg", "0.0"), ("mean", "mmHg", value),
                                       ("phase", "rad", "0.0"), ("period", "s", "1")]:
                    ET.SubElement(condition, tag, units=unit, value=val)
        ET.SubElement(ET.SubElement(base, "monitoring"), "incompressibility")
        dt = float(sim.find("step_length").get("value"))
        dx = float(sim.find("voxel_size").get("value"))
        tau = 0.5 + (dt * 0.004 / 1000.0) / ((1.0 / 3.0) * dx * dx)
        for kernel in ("LBGK", "TRT", "MRT"):
            folder = args.output / name / kernel
            folder.mkdir(parents=True)
            paths = {}
            for kind in ("reference", "candidate"):
                config = ET.fromstring(ET.tostring(base))
                if kind == "reference" and kernel != "LBGK":
                    parameter = 3.0 / 16.0 if kernel == "TRT" else 1.0 / tau
                    ET.SubElement(config.find("simulation"), "relaxation_parameter",
                                  units="lattice", value=str(parameter))
                if kind == "candidate":
                    ET.SubElement(config, "decomposition", method="octree")
                frame = updates if kind == "reference" else updates - 1
                properties = ET.SubElement(config, "properties")
                output = ET.SubElement(properties, "propertyoutput", file="whole.xtr",
                                       period="1", start=str(frame), stop=str(frame))
                ET.SubElement(output, "geometry", type="whole")
                ET.SubElement(output, "field", type="pressure")
                ET.SubElement(output, "field", type="velocity")
                path = folder / (kind + ".xml")
                ET.ElementTree(config).write(path, encoding="utf-8", xml_declaration=True)
                paths[kind] = str(path.resolve())
            manifest.append(dict(name=name, kernel=kernel, sites=sites, updates=updates,
                                 source=str(source), geometry=str(geometry),
                                 geometry_sha256=hashlib.sha256(geometry.read_bytes()).hexdigest(),
                                 dt_seconds=dt, dx_metres=dx, tau=tau, inputs=paths))
    (args.output / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
