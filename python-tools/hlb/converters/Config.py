# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

"""Convert legacy simulation XML to the current version 6 schema."""

import argparse
import math
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

# This is the pressure conversion constant used by the legacy solver.
MMHG_TO_PA = 133.3223874


class UnsupportedFeature(ValueError):
    pass


def number(value):
    result = float.fromhex(value) if "0x" in value.lower() else float(value)
    if not math.isfinite(result):
        raise ValueError(f"Non-finite numeric value: {value}")
    return result


def vector(value):
    values = [number(v.strip()) for v in value.strip("() ").split(",")]
    if len(values) != 3:
        raise ValueError(f"Expected three coordinates: {value}")
    return values


def convert(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination:
        raise ValueError("Input and output must be different files")
    if destination.exists():
        raise ValueError(f"Output already exists: {destination}")
    tree = ET.parse(source)
    root = tree.getroot()
    version = root.get("version")
    if root.tag != "hemelbsettings" or version not in ("3", "5"):
        raise ValueError("Expected hemelbsettings XML version 3 or 5")
    for tag in (
        "particles",
        "bodyForces",
        "boundaryConditions",
        "redbloodcells",
    ):
        if root.find(tag) is not None:
            raise UnsupportedFeature(f"Unsupported feature: {tag}")
    colloids = root.find("colloids")
    if colloids is not None:
        forces = colloids.find("bodyForces")
        if forces is not None and len(forces) and colloids.get("mode") != "tracer":
            raise UnsupportedFeature(
                "Force declarations require explicit colloids mode=tracer"
            )
    simulation = root.find("simulation")
    if simulation is None:
        raise ValueError("Missing simulation element")
    allowed_conditions = {
        ("pressure", "cosine"),
        ("pressure", "file"),
        ("velocity", "parabolic"),
        ("velocity", "womersley"),
        ("velocity", "file"),
        ("velocity", "womersleyElastic"),
        ("velocity", "readWrite"),
        *(
            (kind, subtype)
            for kind in ("pressure", "yangpressure")
            for subtype in ("cosine", "file", "WK2", "WK3", "fileWK")
        ),
    }
    for condition in root.findall(".//condition"):
        key = condition.get("type"), condition.get("subtype")
        if key not in allowed_conditions:
            raise UnsupportedFeature(f"Unsupported boundary feature: {key[0]}/{key[1]}")
        # These quantities are ignored by the legacy cosine-pressure reader.
        if key[0] in ("pressure", "yangpressure") and key[1] == "cosine":
            for tag in ("radius", "area"):
                for child in condition.findall(tag):
                    condition.remove(child)
        if key == ("velocity", "file"):
            for child in condition.findall("area"):
                condition.remove(child)
    initial = root.find("initialconditions")
    if initial is not None:
        pressure = initial.find("pressure")
        nested_checkpoint = initial.find("pressure/checkpoint")
        if nested_checkpoint is not None:
            if (
                pressure.find("uniform") is not None
                or initial.find("checkpoint") is not None
            ):
                raise ValueError("Conflicting legacy initial conditions")
            pressure.remove(nested_checkpoint)
            if len(pressure) or pressure.attrib:
                raise ValueError("Unrecognised legacy pressure initial condition")
            initial.remove(pressure)
            initial.append(nested_checkpoint)
    dx = number(simulation.find("voxel_size").get("value"))
    origin = vector(simulation.find("origin").get("value"))
    if dx <= 0:
        raise ValueError("voxel_size must be positive")
    # HemePure uses lattice coordinates without applying the world origin.
    for element in root.findall("./inlets/inlet/position") + root.findall(
        "./outlets/outlet/position"
    ):
        if element.get("units") == "lattice":
            coords = [v * dx + o for v, o in zip(vector(element.get("value")), origin)]
            element.set(
                "value", "(" + ",".join(format(v, ".17g") for v in coords) + ")"
            )
            element.set("units", "m")
    # Normalize only pressure quantities, keeping stress and elastic modulus in Pa.
    pressure_paths = (
        "./simulation/reference_pressure",
        "./initialconditions/pressure/uniform",
        ".//condition/mean",
        ".//condition/amplitude",
        ".//condition/pressure",
        ".//condition/pressure_gradient_amplitude",
    )
    for path in pressure_paths:
        for element in root.findall(path):
            unit = element.get("units")
            if unit in ("Pa", "Pa/m"):
                element.set(
                    "value", format(number(element.get("value")) / MMHG_TO_PA, ".17g")
                )
                element.set("units", "mmHg" if unit == "Pa" else "mmHg/m")
    stress = simulation.find("stresstype")
    if stress is not None:
        simulation.remove(stress)
    # Both legacy readers in this workspace default to zero reference pressure.
    if simulation.find("reference_pressure") is None:
        ET.SubElement(simulation, "reference_pressure", units="mmHg", value="0")
    properties = root.find("properties")
    if properties is not None:
        checkpoints = properties.findall("checkpoint")
        if len(checkpoints) > 1:
            raise ValueError("More than one checkpoint configuration")
        for checkpoint in checkpoints:
            if not checkpoint.get("file") or not checkpoint.get("period"):
                raise ValueError("Legacy checkpoint output requires file and period")
            output = ET.SubElement(
                properties,
                "propertyoutput",
                file=checkpoint.get("file"),
                period=checkpoint.get("period"),
                timestep_mode="single",
            )
            ET.SubElement(output, "geometry", type="whole")
            ET.SubElement(
                output,
                "field",
                type="distributions",
                name="distributions",
                datatype="double",
            )
            properties.remove(checkpoint)
    for selector in root.findall("./properties/propertyoutput/geometry"):
        if selector.get("type") not in (
            "inlet",
            "outlet",
            "whole",
            "plane",
            "line",
            "surface",
            "surfacepoint",
            "sphere",
            "surfaceWithinSphere",
        ):
            raise UnsupportedFeature(
                f"Unsupported extraction geometry: {selector.get('type')}"
            )
        if selector.get("type") in ("inlet", "outlet", "surfacepoint"):
            for child in selector.findall("normal"):
                selector.remove(child)
    pending_files = []
    for condition in root.findall(".//condition"):
        if condition.get("subtype") == "readWrite":
            condition.set("pressure_units", "mmHg")
            for name in ("flowRateFilePath", "pressureFilePath"):
                element = condition.find(name)
                target = (source.parent / element.get("value")).resolve()
                element.set("value", os.path.relpath(target, destination.parent))
        path = condition.find("path")
        if path is None:
            continue
        input_path = (source.parent / path.get("value")).resolve()
        if (
            condition.get("type") in ("pressure", "yangpressure")
            and condition.get("subtype") == "file"
        ):
            converted_path = destination.with_name(
                destination.stem + f".pressure-{len(pending_files)}.txt"
            )
            if converted_path.exists():
                raise ValueError(f"Output already exists: {converted_path}")
            lines = []
            for index, line in enumerate(input_path.read_text().splitlines(), 1):
                if not line.strip():
                    continue
                fields = line.split()
                if len(fields) != 2:
                    raise ValueError(f"Invalid pressure record at {input_path}:{index}")
                t, p = map(number, fields)
                lines.append(f"{t:.17g} {p:.17g}\n")
            pending_files.append((converted_path, "".join(lines)))
            path.set("value", os.path.relpath(converted_path, destination.parent))
            condition.set("units", "mmHg")
        else:
            path.set("value", os.path.relpath(input_path, destination.parent))
    for element in root.findall("./geometry/datafile"):
        path = (source.parent / element.get("path")).resolve()
        element.set("path", os.path.relpath(path, destination.parent))
    for element in root.findall("./initialconditions/checkpoint"):
        for attribute in ("file", "offsets"):
            if attribute in element.attrib:
                path = (source.parent / element.get(attribute)).resolve()
                element.set(attribute, os.path.relpath(path, destination.parent))
    root.set("version", "6")
    if hasattr(ET, "indent"):
        ET.indent(tree, space="  ")
    destination.parent.mkdir(parents=True, exist_ok=True)
    for path, data in pending_files:
        path.write_text(data)
    tree.write(destination, encoding="utf-8", xml_declaration=True)
    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = convert(args.input, args.output)
    except (ValueError, OSError, ET.ParseError, AttributeError, TypeError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
