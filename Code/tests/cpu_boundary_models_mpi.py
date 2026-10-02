#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Exercise optional CPU models, including rank-changing checkpoint continuation."""
import argparse
import math
import struct
from pathlib import Path
import tempfile
import threading
import time
import xml.etree.ElementTree as ET
from checkpoint_restart_mpi import run, checkpoint_at, saved_sites

EXAMPLE = Path(__file__).resolve().parents[2] / "examples"


def config(path, model):
    tree = ET.parse(EXAMPLE / "first-run.xml")
    root = tree.getroot()
    root.find("geometry/datafile").set("path", str(EXAMPLE / "first-run.gmy"))
    root.find("simulation/steps").set(
        "value", "1200" if model in ("yang", "yang-corners") else "100"
    )
    for node in list(root.find("properties")):
        root.find("properties").remove(node)
    checkpoint = root.find("simulation/checkpoint")
    if checkpoint is None:
        checkpoint = ET.SubElement(root.find("simulation"), "checkpoint")
    checkpoint.set("period", "600" if model in ("yang", "yang-corners") else "50")
    ET.SubElement(root, "decomposition", method="octree")
    if model in ["sponge", "trt-sponge", "les-sponge"]:
        sponge = ET.SubElement(root.find("initialconditions"), "sponge_layer")
        for tag, value, unit in [
            ("viscosity_ratio", 4, "dimensionless"),
            ("width", 0.0008, "m"),
            ("lifetime", 200, "lattice"),
        ]:
            ET.SubElement(sponge, tag, value=str(value), units=unit)
        ET.SubElement(
            root.find("simulation"),
            "smagorinsky_constant",
            value="0.12",
            units="dimensionless",
        )
    if model == "elastic":
        ET.SubElement(
            root.find("simulation"),
            "elastic_wall_stiffness",
            value="0.01",
            units="lattice",
        )
    if model in [
        "womersleyElastic",
        "readWrite",
        "weightedReadWrite",
        "periodicVelocity",
    ]:
        condition = root.find("inlets/inlet/condition")
        condition.clear()
        condition.attrib.update(
            type="velocity",
            subtype=(
                "readWrite"
                if model == "weightedReadWrite"
                else "file"
                if model == "periodicVelocity"
                else model
            ),
        )
        fields = [("radius", 0.0005, "m")]
        if model == "womersleyElastic":
            fields += [
                ("pressure_gradient_amplitude", 100, "Pa/m"),
                ("period", 0.01, "s"),
                ("womersley_number", 2, "dimensionless"),
                ("poisson_ratio", 0.3, "dimensionless"),
                ("youngs_modulus", 10000, "Pa"),
                ("axial_position", 0.003, "m"),
            ]
        elif model == "periodicVelocity":
            condition.set("timing", "periodic")
            profile = path.parent / "velocity.txt"
            profile.write_text("0 0.001\n0.0007 0.002\n0.0014 0.001\n")
            ET.SubElement(condition, "path", value=str(profile))
        else:
            if model == "weightedReadWrite":
                weights = path.parent / "weights.txt"
                # Iolet coordinates of the bundled example: plane z=1, centre (5.5,5.5).
                rows = []
                for x in range(12):
                    for y in range(12):
                        radius2 = (x - 5.5) ** 2 + (y - 5.5) ** 2
                        if radius2 < 25:
                            rows.append(f"{x} {y} 1 {1 - radius2 / 25:.17g}\n")
                weights.write_text("".join(rows))
                ET.SubElement(condition, "weightsFilePath", value=str(weights))
            condition.set("timeout_s", "10")
            fields += [
                ("area", math.pi * 0.0005**2, "m^2"),
                ("frequency", 7, "lattice"),
                ("flowRateConversionFactor", 1, "dimensionless"),
                ("pressureConversionFactor", 1, "dimensionless"),
                ("smoothingFactor", 0.5, "dimensionless"),
            ]
            for tag, filename in [
                ("flowRateFilePath", "flow.txt"),
                ("pressureFilePath", "pressure.txt"),
            ]:
                ET.SubElement(condition, tag, value=str(path.parent / filename))
        for tag, value, unit in fields:
            ET.SubElement(condition, tag, value=str(value), units=unit)
    if model in ("yang", "yang-corners"):
        root.find("simulation/step_length").set("value", "0.00025")
    output = ET.SubElement(
        root.find("properties"),
        "propertyoutput",
        file="wall.xtr",
        period="1",
        start="50",
        stop="50",
    )
    ET.SubElement(output, "geometry", type="surface")
    for field in [
        "pressure",
        "traction",
        "tangentialprojectiontraction",
        "normalprojectiontraction",
        "wallextension",
    ]:
        ET.SubElement(output, "field", type=field, datatype="double")
    tree.write(path)


def check_wall_fields(path, elastic):
    data = path.read_bytes()
    dx, dt, dm = struct.unpack_from(">3d", data, 12)
    reference = struct.unpack_from(">d", data, 60)[0]
    count, fields, header_bytes = struct.unpack_from(">QII", data, 68)
    offset, spec = 84, []
    for _ in range(fields):
        length = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        name = data[offset : offset + length].decode()
        offset += (length + 3) // 4 * 4
        components, datatype, noffsets = struct.unpack_from(">3I", data, offset)
        offset += 12
        assert datatype == 1, "Expected double output"
        values = struct.unpack_from(f">{noffsets + 1}d", data, offset)
        offset += 8 * (noffsets + 1)
        expected = dx if name == "wallextension" else dm / (dt * dt * dx)
        assert math.isclose(values[-1], expected, rel_tol=1e-14)
        spec.append((name, components, values))
    assert offset == 84 + header_bytes
    assert struct.unpack_from(">Q", data, offset)[0] == 50
    offset += 8
    extensions = []
    for _ in range(count):
        offset += 12
        row = {}
        for name, components, values in spec:
            raw = struct.unpack_from(f">{components}d", data, offset)
            offset += components * 8
            row[name] = [
                (
                    value + (values[i] if len(values) > 2 else values[0])
                    if len(values) > 1
                    else value
                )
                * values[-1]
                for i, value in enumerate(raw)
            ]
        for full, normal, tangent in zip(
            row["traction"],
            row["normalprojectiontraction"],
            row["tangentialprojectiontraction"],
        ):
            assert math.isclose(full, normal + tangent, rel_tol=1e-12, abs_tol=1e-12)
        extension = row["wallextension"][0]
        expected = (
            ((row["pressure"][0] - reference) / (dm / (dt * dt * dx)) / 0.01 * dx)
            if elastic
            else 0
        )
        assert math.isclose(extension, expected, rel_tol=1e-6, abs_tol=1e-14)
        extensions.append(extension)
    assert offset == len(data) and count > 0
    if elastic:
        assert max(map(abs, extensions)) > 0


def coupled_run(launcher, exe, ranks, source, output, work):
    tree = ET.parse(source)
    condition = tree.find("inlets/inlet/condition")
    dt = (
        float.fromhex(tree.find("simulation/step_length").get("value"))
        if "0x" in tree.find("simulation/step_length").get("value")
        else float(tree.find("simulation/step_length").get("value"))
    )
    state = condition.find("state")
    next_exchange = 2 if state is None else int(state.get("next_exchange"))
    flow, pressure = work / "flow.txt", work / "pressure.txt"
    pressure.unlink(missing_ok=True)

    def publish(timestamp):
        temporary = work / "flow.tmp"
        value = (
            math.pi
            * 0.0005**2
            * 0.001
            * (1 + 0.1 * math.sin(timestamp / 0.01 * 2 * math.pi))
        )
        temporary.write_text(f"{timestamp:.17g} {value:.17g}\n")
        temporary.replace(flow)

    publish((next_exchange - 2) * dt)
    finished = threading.Event()

    def peer():
        last = None
        while not finished.wait(0.001):
            try:
                timestamp, value = map(float, pressure.read_text().split())
            except (OSError, ValueError):
                continue
            if timestamp != last:
                publish(timestamp)
                last = timestamp

    thread = threading.Thread(target=peer)
    thread.start()
    try:
        run(launcher, exe, ranks, source, output)
    finally:
        finished.set()
        thread.join()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hemelb", required=True)
    parser.add_argument("--launcher", default="mpirun")
    parser.add_argument(
        "--model",
        required=True,
        choices=[
            "yang",
            "yang-corners",
            "elastic",
            "sponge",
            "trt-sponge",
            "les-sponge",
            "womersleyElastic",
            "readWrite",
            "weightedReadWrite",
            "periodicVelocity",
        ],
    )
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hemelb-model-") as directory:
        work = Path(directory)
        source = work / "input.xml"
        config(source, args.model)
        if args.model in ("yang", "yang-corners"):
            invalid = work / "invalid-tau.xml"
            tree = ET.parse(source)
            tree.find("simulation/step_length").set("value", "0.0001")
            tree.write(invalid)
            try:
                run(args.launcher, args.hemelb, 1, invalid, work / "invalid-out")
            except RuntimeError as error:
                assert "Yang pressure requires tau >= 0.8" in str(error), error
            else:
                raise AssertionError("Unsupported Yang relaxation time was accepted")
        execute = lambda ranks, src, out: (
            coupled_run(args.launcher, args.hemelb, ranks, src, out, work)
            if args.model in ["readWrite", "weightedReadWrite"]
            else run(args.launcher, args.hemelb, ranks, src, out)
        )
        steps, half = (
            (1200, 600) if args.model in ("yang", "yang-corners") else (100, 50)
        )
        full, resumed = work / "full", work / "resumed"
        execute(2, source, full)
        check_wall_fields(full / "Extracted/wall.xtr", args.model == "elastic")
        restart = checkpoint_at(full, half).parent / "restart.xml"
        saved = ET.parse(restart)
        fields = [
            node.get("type")
            for node in saved.findall("properties/propertyoutput/field")
        ]
        assert "normalprojectiontraction" in fields and "wallextension" in fields
        execute(1, restart, resumed)
        a, b = saved_sites(checkpoint_at(full, steps)), saved_sites(
            checkpoint_at(resumed, steps)
        )
        assert a.keys() == b.keys()
        error = max(abs(x - y) for site in a for x, y in zip(a[site], b[site]))
        assert error < 1e-12, (args.model, error)
        assert all(
            math.isfinite(value) and value > 0
            for values in a.values()
            for value in values
        )
        assert (
            max(map(sum, a.values())) - min(map(sum, a.values())) > 1e-7
        ), "No pressure response detected"
        if args.model in ("yang", "yang-corners"):
            legacy = work / "legacy-yang.xml"
            tree = ET.parse(source)
            tree.getroot().set("version", "3")
            for index, condition in enumerate(tree.findall(".//condition")):
                mean = float(condition.find("mean").get("value"))
                profile = work / f"legacy-pressure-{index}.txt"
                profile.write_text(
                    f"0 {mean / 133.3223874:.17g}\n1 {mean / 133.3223874:.17g}\n"
                )
                condition.clear()
                condition.attrib.update(
                    type="yangpressure" if args.model == "yang" else "pressure",
                    subtype="file",
                )
                ET.SubElement(condition, "path", value=str(profile))
            tree.write(legacy)
            legacy_output = work / "legacy-yang-output"
            execute(1, legacy, legacy_output)
            values = saved_sites(checkpoint_at(legacy_output, steps))
            assert a.keys() == values.keys()
            assert (
                max(abs(x - y) for site in a for x, y in zip(a[site], values[site]))
                < 1e-12
            )
            for condition in ET.parse(
                checkpoint_at(legacy_output, steps).parent / "restart.xml"
            ).findall(".//condition"):
                assert condition.get("units") == "mmHg"
        print(
            f"{args.model}: positive finite distributions and 2-to-1 restart passed; maximum difference {error:.17g}"
        )


if __name__ == "__main__":
    main()
