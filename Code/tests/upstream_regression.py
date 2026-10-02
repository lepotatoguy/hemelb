#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Run upstream legacy CPU fixtures against current extraction output."""

import argparse
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import numpy as np

from hlb.parsers.extraction import ExtractedProperty
from hlb.parsers.offset import OffsetFile
from checkpoint_restart_mpi import run

MMHG_TO_PA = 133.3223874


def prepare(source):
    tree = ET.parse(source)
    assert tree.getroot().get("version") in ("3", "5")
    initial = tree.find("initialconditions")
    # Upstream fixtures count iterations from 1 and include the end iteration.
    # Set their clock explicitly without changing XML version or pressure units.
    if initial.find("pressure") is not None and initial.find("time") is None:
        ET.SubElement(initial, "time", units="lattice", value="1")
    steps = tree.find("simulation/steps")
    steps.set("value", str(int(steps.get("value")) + 1))
    destination = source.with_name(source.stem + ".ci.xml")
    tree.write(destination)
    return destination


def ordered(reader, time):
    data = reader.GetByTimeStep(time)
    return data[np.lexsort(data.grid.T[::-1])]


def compare(reference, actual, pressure_fields=()):
    old, new = ExtractedProperty(str(reference)), ExtractedProperty(str(actual))
    assert old.siteCount > 0 and old.siteCount == new.siteCount
    assert old.fieldCount == new.fieldCount
    assert old.voxelSizeMetres == new.voxelSizeMetres
    np.testing.assert_array_equal(old.originMetres, new.originMetres)
    np.testing.assert_array_equal(old.times, new.times)
    assert len(old.times) > 0
    old_spec, new_spec = list(old.GetFieldSpec()), list(new.GetFieldSpec())
    assert [(f[0], f[3]) for f in old_spec] == [(f[0], f[3]) for f in new_spec]
    for time in old.times:
        gold, data = ordered(old, time), ordered(new, time)
        np.testing.assert_array_equal(gold.grid, data.grid)
        np.testing.assert_array_equal(gold.position, data.position)
        for spec in old_spec:
            field = spec[0]
            if field == "grid":
                continue
            expected, computed = gold[field].astype(np.float64), data[field].astype(
                np.float64
            )
            if old.version < 6 and field in pressure_fields:
                expected *= MMHG_TO_PA
            finite = np.isfinite(expected)
            np.testing.assert_array_equal(finite, np.isfinite(computed))
            # The reference stores physical float32; XML6 stores lattice
            # float32 and a scale. Compare in SI with a float32 rounding bound.
            # Double checkpoint distributions retain the original 1e-12 bound.
            tolerance = (
                16 * np.finfo(np.float32).eps * np.max(np.abs(expected[finite]))
                if np.dtype(spec[2]).itemsize == 4 and np.any(finite)
                else 1e-12
            )
            np.testing.assert_allclose(
                computed[finite],
                expected[finite],
                rtol=0,
                atol=tolerance,
                err_msg=f"{reference.name}: {field} at timestep {time}",
            )
    return old, new


def diff_test(executable, fixture):
    source = fixture / "config.xml"
    output = fixture / "results"
    if output.exists():
        shutil.rmtree(output)
    run("mpirun", executable, 2, prepare(source), output)
    reference_dir, actual_dir = fixture / "CleanExtracted", output / "Extracted"
    assert {p.name for p in reference_dir.iterdir()} == {
        p.name for p in actual_dir.iterdir()
    }
    pressure_fields = [
        el.get("name", "pressure")
        for el in ET.parse(source).findall(".//field")
        if el.get("type") == "pressure"
    ]
    for path in sorted(reference_dir.glob("*.xtr")):
        old, new = compare(path, actual_dir / path.name, pressure_fields)
        old_offsets = OffsetFile(path.with_suffix(".off"))
        new_offsets = OffsetFile((actual_dir / path.name).with_suffix(".off"))
        # Decomposition may redistribute ranks. Header sizes also differ;
        # compare the total payload and validate each partition's alignment.
        assert (
            old_offsets.Data[-1] - old_offsets.Data[0]
            == new_offsets.Data[-1] - new_offsets.Data[0]
        )
        for reader, offsets in ((old, old_offsets), (new, new_offsets)):
            assert offsets.NumberOfRanks > 0
            chunks = np.diff(offsets.Data).astype(np.int64)
            chunks[0] -= 8  # timestep prefix precedes the first rank's records
            assert np.all(chunks >= 0)
            assert np.all(chunks % reader.GetFieldSpec().GetRecordLength() == 0)
            assert (
                np.sum(chunks) // reader.GetFieldSpec().GetRecordLength()
                == reader.siteCount
            )
    print(
        "Upstream snapshots match in SI units, with complete times, sites and offsets"
    )


def checkpoint_test(executable, fixture):
    for name in ("whole", "first_half", "second_half"):
        output = fixture / name
        if output.exists():
            shutil.rmtree(output)
        run("mpirun", executable, 3, prepare(fixture / f"{name}.xml"), output)
    for time in (100, 200):
        whole = fixture / f"whole/Extracted/checkpoint{time}.xtr"
        resumed = fixture / f"second_half/Extracted/checkpoint{time}.xtr"
        compare(whole, resumed)
    compare(
        fixture / "whole/Extracted/checkpoint100.xtr",
        fixture / "first_half/Extracted/checkpoint100.xtr",
    )
    print("Upstream checkpoint restart matches at timesteps 100 and 200")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hemelb", required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--mode", choices=("diff", "checkpoint"), required=True)
    args = parser.parse_args()
    {"diff": diff_test, "checkpoint": checkpoint_test}[args.mode](
        args.hemelb, args.fixture.resolve()
    )
