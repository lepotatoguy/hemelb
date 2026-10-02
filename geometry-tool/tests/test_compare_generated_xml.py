#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Regression guards for the legacy geometry fixture comparison."""

from pathlib import Path
import xml.etree.ElementTree as ET

import pytest

from compare_generated_xml import compare


@pytest.fixture
def configs(tmp_path):
    current = Path(__file__).parent / "Model/data/test.xml"
    legacy = ET.parse(current)
    legacy.getroot().set("version", "5")
    for quantity in legacy.getroot().iter():
        if quantity.get("units") == "Pa":
            quantity.set("units", "mmHg")
            quantity.set("value", str(float(quantity.get("value")) / 133.3223874))
    reference = tmp_path / "legacy.xml"
    legacy.write(reference)
    generated = tmp_path / "generated.xml"
    generated.write_bytes(current.read_bytes())
    return reference, generated


def test_compares_pressure_in_si(configs):
    compare(*configs)


@pytest.mark.parametrize("change", ["pressure", "units", "version", "geometry"])
def test_rejects_changed_geometry_or_physics(configs, change):
    reference, generated = configs
    tree = ET.parse(generated)
    if change == "pressure":
        tree.find("inlets/inlet/condition/mean").set("value", "1")
    elif change == "units":
        tree.find("inlets/inlet/condition/mean").set("units", "mmHg")
    elif change == "version":
        tree.getroot().set("version", "5")
    else:
        tree.find("geometry/datafile").set("path", "another.gmy")
    tree.write(generated)
    with pytest.raises(AssertionError):
        compare(reference, generated)
