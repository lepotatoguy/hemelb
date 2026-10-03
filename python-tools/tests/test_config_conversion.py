# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from pathlib import Path
import xml.etree.ElementTree as ET

import pytest

from hlb.converters.Config import convert, UnsupportedFeature, MMHG_TO_PA


def input_xml(tmp_path):
    path = tmp_path / "old.xml"
    path.write_text(
        """<hemelbsettings version="3">
      <simulation><step_length units="s" value="0.1"/><steps units="lattice" value="4"/>
      <stresstype value="1"/><voxel_size units="m" value="0.5"/>
      <origin units="m" value="(1,2,3)"/></simulation>
      <geometry><datafile path="mesh.gmy"/></geometry>
      <initialconditions><pressure><uniform units="mmHg" value="2"/></pressure></initialconditions>
      <inlets><inlet><position units="lattice" value="(2,4,6)"/>
      <normal units="dimensionless" value="(0,0,1)"/>
      <condition type="pressure" subtype="cosine"><mean units="mmHg" value="2"/>
      <amplitude units="mmHg" value="1"/><phase units="rad" value="0"/>
      <period units="s" value="1"/></condition></inlet></inlets><outlets/>
      </hemelbsettings>"""
    )
    return path


def test_units_coordinates_and_paths(tmp_path):
    source = input_xml(tmp_path)
    original = source.read_bytes()
    destination = tmp_path / "converted" / "new.xml"
    convert(source, destination)
    root = ET.parse(destination).getroot()
    assert source.read_bytes() == original
    assert root.get("version") == "6"
    assert root.find("simulation/stresstype") is None
    assert root.find("simulation/reference_pressure").get("value") == "0"
    assert root.find("inlets/inlet/position").attrib == {
        "units": "m",
        "value": "(2,4,6)",
    }
    assert float(root.find(".//mean").get("value")) == 2
    assert root.find(".//mean").get("units") == "mmHg"
    assert root.find("geometry/datafile").get("path") == "../mesh.gmy"


def test_unsupported_physics_does_not_write_output(tmp_path):
    source = input_xml(tmp_path)
    source.write_text(
        source.read_text().replace(
            'type="pressure" subtype="cosine"', 'type="windkessel" subtype="GKmodel"'
        )
    )
    with pytest.raises(UnsupportedFeature, match="windkessel/GKmodel"):
        convert(source, tmp_path / "output.xml")
    assert not (tmp_path / "output.xml").exists()


@pytest.mark.parametrize("kind", ["pressure", "yangpressure"])
def test_pressure_files_preserve_mmhg_without_modifying_source(tmp_path, kind):
    source = input_xml(tmp_path)
    tree = ET.parse(source)
    condition = tree.find(".//condition")
    condition.clear()
    condition.attrib = {"type": kind, "subtype": "file"}
    ET.SubElement(condition, "path", value="pressure.txt")
    tree.write(source)
    data = tmp_path / "pressure.txt"
    data.write_text("0 2\n1 3\n2 2\n")
    destination = tmp_path / "new.xml"
    convert(source, destination)
    converted = ET.parse(destination).find(".//condition/path").get("value")
    pairs = [
        tuple(map(float, line.split()))
        for line in (tmp_path / converted).read_text().splitlines()
    ]
    assert pairs == [(0, 2), (1, 3), (2, 2)]
    assert ET.parse(destination).find(".//condition").get("units") == "mmHg"
    assert data.read_text() == "0 2\n1 3\n2 2\n"


def test_existing_output_is_preserved(tmp_path):
    source = input_xml(tmp_path)
    destination = tmp_path / "new.xml"
    destination.write_text("existing")
    with pytest.raises(ValueError, match="already exists"):
        convert(source, destination)
    assert destination.read_text() == "existing"


def test_legacy_checkpoint_paths_and_precision_are_preserved(tmp_path):
    source = input_xml(tmp_path)
    tree = ET.parse(source)
    initial = tree.find("initialconditions")
    initial.clear()
    pressure = ET.SubElement(initial, "pressure")
    ET.SubElement(pressure, "checkpoint", file="saved.xtr", offsets="saved.off")
    properties = ET.SubElement(tree.getroot(), "properties")
    ET.SubElement(properties, "checkpoint", file="old-%d.xtr", period="10")
    tree.write(source)
    destination = tmp_path / "converted" / "config.xml"
    convert(source, destination)
    root = ET.parse(destination).getroot()
    assert root.find("initialconditions/pressure") is None
    assert root.find("initialconditions/checkpoint").attrib == {
        "file": "../saved.xtr",
        "offsets": "../saved.off",
    }
    output = root.find("properties/propertyoutput")
    assert output.attrib == {
        "file": "old-%d.xtr",
        "period": "10",
        "timestep_mode": "single",
    }
    assert output.find("field").get("datatype") == "double"
    assert root.find("simulation/checkpoint") is None


@pytest.mark.parametrize("units,scale", [("mmHg/m", 1.0), ("Pa/m", MMHG_TO_PA)])
def test_pressure_gradient_is_mmhg_but_elastic_modulus_stays_pa(tmp_path, units, scale):
    source = input_xml(tmp_path)
    tree = ET.parse(source)
    condition = tree.find(".//condition")
    condition.clear()
    condition.attrib = {"type": "velocity", "subtype": "womersleyElastic"}
    ET.SubElement(
        condition, "pressure_gradient_amplitude", units=units, value=str(2 * scale)
    )
    ET.SubElement(condition, "youngs_modulus", units="Pa", value="10000")
    tree.write(source)
    destination = tmp_path / "new.xml"
    convert(source, destination)
    converted = ET.parse(destination)
    gradient = converted.find(".//pressure_gradient_amplitude")
    assert gradient.get("units") == "mmHg/m"
    assert float(gradient.get("value")) == pytest.approx(2.0)
    assert converted.find(".//youngs_modulus").attrib == {
        "units": "Pa",
        "value": "10000",
    }


def test_explicit_pa_pressure_converts_to_mmhg(tmp_path):
    source = input_xml(tmp_path)
    tree = ET.parse(source)
    mean = tree.find(".//mean")
    mean.set("units", "Pa")
    mean.set("value", str(2 * MMHG_TO_PA))
    tree.write(source)
    destination = tmp_path / "new.xml"
    convert(source, destination)
    mean = ET.parse(destination).find(".//mean")
    assert mean.get("units") == "mmHg"
    assert float(mean.get("value")) == pytest.approx(2.0)
