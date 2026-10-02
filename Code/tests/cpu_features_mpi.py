#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Check passive tracers, periodic pressure and restricted readers through restart."""
import argparse
from pathlib import Path
import struct
import tempfile
import xml.etree.ElementTree as ET
from checkpoint_restart_mpi import run, checkpoint_at, saved_sites, RESOURCE_DIR


def config(path, tracers=True, gmy=None, method="octree"):
    tree = ET.parse(RESOURCE_DIR / "large_cylinder.xml")
    root = tree.getroot()
    root.find("geometry/datafile").set(
        "path", str(gmy or RESOURCE_DIR / "large_cylinder.gmy")
    )
    root.find("simulation/steps").set("value", "40")
    ET.SubElement(root.find("simulation"), "checkpoint", period="20")
    ET.SubElement(
        root, "decomposition", method=method, reader_count="1", reader_spacing="2"
    )
    profile = path.with_suffix(".pressure")
    profile.write_text("0 0\n4.4e-6 0.01\n8.8e-6 0\n")
    condition = root.find("inlets/inlet/condition")
    condition.clear()
    condition.attrib.update(
        type="pressure", subtype="file", timing="periodic", units="Pa"
    )
    ET.SubElement(condition, "path", value=str(profile))
    output = ET.SubElement(
        ET.SubElement(root, "properties"),
        "propertyoutput",
        file="one.xtr",
        period="1",
        start="20",
        stop="20",
    )
    ET.SubElement(output, "geometry", type="whole")
    ET.SubElement(output, "field", type="velocity", datatype="double")
    if tracers:
        particles = ET.SubElement(
            ET.SubElement(root, "tracers", output_period="10", seed="1"), "particles"
        )
        particle = ET.SubElement(
            particles, "subgridParticle", units="lattice", ParticleId="0", Radius="0.01"
        )
        ET.SubElement(
            particle, "initialPosition", units="lattice", x="7.5", y="7.5", z="18"
        )
        ET.SubElement(particles, "sphereRadius", units="lattice", value="0.1")
        ET.SubElement(
            particles, "sphereCentre", units="lattice", x="7.5", y="7.5", z="18"
        )
        ET.SubElement(particles, "emissionCount", units="dimensionless", value="2")
        ET.SubElement(particles, "emissionItrvl", units="dimensionless", value="7")
    tree.write(path)


def state(output):
    return ET.parse(checkpoint_at(output, 40).parent / "restart.xml").find("tracers")


def equivalent_states(a, b):
    assert a.attrib == b.attrib
    left, right = list(a.iter()), list(b.iter())
    assert len(left) == len(right)
    for x, y in zip(left, right):
        assert x.tag == y.tag and x.attrib.keys() == y.attrib.keys()
        for key in x.attrib:
            try:
                p, q = float.fromhex(x.attrib[key]), float.fromhex(y.attrib[key])
            except ValueError:
                assert x.attrib[key] == y.attrib[key]
            else:
                assert abs(p - q) < 1e-12, (key, p, q)


def make_gmy_plus(path):
    data = (RESOURCE_DIR / "large_cylinder.gmy").read_bytes()
    preamble = list(struct.unpack_from(">8I", data))
    blocks = preamble[3] * preamble[4] * preamble[5]
    header = bytearray()
    for i in range(blocks):
        sites, compressed, expanded = struct.unpack_from(">3I", data, 32 + 12 * i)
        cost = sites * (9 if i % 3 == 0 else 1)
        header.extend(struct.pack(">4I", sites, cost, compressed, expanded))
    path.write_bytes(struct.pack(">8I", *preamble) + header + data[32 + 12 * blocks :])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--hemelb", required=True)
    p.add_argument("--launcher", default="mpirun")
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix="hemelb-cpu-") as directory:
        work = Path(directory)
        source = work / "full.xml"
        config(source)
        full = work / "full"
        run(args.launcher, args.hemelb, 2, source, full)
        restart = checkpoint_at(full, 20).parent / "restart.xml"
        # Reader spacing is valid even at one rank when reader_count is one.
        resumed = work / "resumed"
        run(args.launcher, args.hemelb, 1, restart, resumed)
        equivalent_states(state(full), state(resumed))
        a, b = saved_sites(checkpoint_at(full, 40)), saved_sites(
            checkpoint_at(resumed, 40)
        )
        assert a.keys() == b.keys()
        assert max(abs(x - y) for site in a for x, y in zip(a[site], b[site])) < 1e-12
        plain_xml, plain = work / "plain.xml", work / "plain"
        config(plain_xml, tracers=False)
        run(args.launcher, args.hemelb, 2, plain_xml, plain)
        assert a == saved_sites(
            checkpoint_at(plain, 40)
        ), "Passive tracers changed fluid distributions"
        positions = [
            node.attrib
            for node in state(full).findall("particles/subgridParticle/initialPosition")
        ]
        assert len(positions) > 1
        assert any(float.fromhex(node["z"]) != 18 for node in positions)
        assert (full / "tracers.csv").is_file()
        extracted = (full / "Extracted/one.xtr").read_bytes()
        sites = struct.unpack_from(">Q", extracted, 68)[0]
        # v6 single velocity field header and exactly one timestep.
        header_length = 84 + struct.unpack_from(">I", extracted, 80)[0]
        assert len(extracted) == header_length + 8 + sites * (12 + 24)
        assert struct.unpack_from(">Q", extracted, header_length)[0] == 20
        legacy_xml = work / "legacy-tracers.xml"
        config(legacy_xml)
        tree = ET.parse(legacy_xml)
        legacy = tree.find("tracers")
        legacy.tag = "colloids"
        legacy.set("mode", "tracer")
        legacy.find("particles/emissionItrvl").set("value", "0")
        # A legacy prototype is removed; zero interval disables emission even
        # with a nonzero count. Explicit tracer mode permits ignored force tags.
        forces = ET.SubElement(legacy, "bodyForces")
        ET.SubElement(forces, "magnetic", forceName="ignored-in-tracer-mode")
        tree.write(legacy_xml)
        legacy_out = work / "legacy-tracers"
        run(args.launcher, args.hemelb, 2, legacy_xml, legacy_out)
        legacy_state = state(legacy_out)
        assert legacy_state.find("particles/emissionCount").get("value") == "0"
        assert not legacy_state.findall("particles/subgridParticle")
        plus = work / "costs.gmy+"
        make_gmy_plus(plus)
        for method in ["octree", "parmetis"]:
            cost_xml, cost_output = work / (method + ".xml"), work / method
            config(cost_xml, tracers=False, gmy=plus, method=method)
            run(args.launcher, args.hemelb, 2, cost_xml, cost_output)
            c = saved_sites(checkpoint_at(cost_output, 40))
            assert c.keys() == a.keys()
            assert (
                max(abs(x - y) for site in a for x, y in zip(a[site], c[site])) < 1e-12
            )
        print(
            "Tracers preserve fluid state; rank-changing restart, periodic waveform, output window, GMY+ costs and restricted readers passed"
        )


if __name__ == "__main__":
    main()
