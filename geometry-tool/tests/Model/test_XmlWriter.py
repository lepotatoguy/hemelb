# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from types import SimpleNamespace
import xml.etree.ElementTree as ET

from HlbGmyTool.Model.Iolets import Inlet
from HlbGmyTool.Model.Vector import Vector
from HlbGmyTool.Model.XmlWriter import XmlWriter


def test_profile_pressure_is_written_in_mmhg_without_rescaling():
    inlet = Inlet()
    inlet.Pressure = Vector(80.0, 5.0, 0.5)
    profile = SimpleNamespace(
        Iolets=[inlet],
        PulsePeriodSeconds=1.0,
        StlFileUnit=SimpleNamespace(SizeInMetres=0.001),
    )
    writer = XmlWriter(profile)
    root = ET.Element("hemelbsettings")
    writer.DoIolets(root)
    writer.DoInitialConditions(root)
    for tag, value in (("mean", 80.0), ("amplitude", 5.0), ("uniform", 0.0)):
        quantity = root.find(".//" + tag)
        assert quantity.get("units") == "mmHg"
        assert float.fromhex(quantity.get("value")) == value
    assert float.fromhex(root.find(".//phase").get("value")) == 0.5
    assert root.find(".//phase").get("units") == "rad"
