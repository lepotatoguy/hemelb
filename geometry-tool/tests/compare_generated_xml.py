#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Compare generated XML6 with the legacy upstream geometry fixture in SI units."""

import argparse
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET

from hlb.converters.Config import convert
from hlb.utils.xml_compare import ScalarQuantityCheck, XmlChecker


def compare(reference, generated):
    actual = ET.parse(generated).getroot()
    assert actual.tag == "hemelbsettings" and actual.get("version") == "6"
    with tempfile.TemporaryDirectory() as directory:
        canonical = convert(reference, Path(directory) / "reference.xml")
        expected = ET.parse(canonical).getroot()
    # The geometry generator does not prescribe solver monitoring or outputs.
    for tag in ("monitoring", "properties", "visualisation"):
        element = expected.find(tag)
        if element is not None:
            expected.remove(element)
    # Zero is the implicit reference pressure in the generated configuration.
    if actual.find("simulation/reference_pressure") is None:
        ET.SubElement(
            actual.find("simulation"), "reference_pressure", units="Pa", value="0"
        )
    # Conversion rebases this path into the temporary directory. The separate
    # GMY comparison checks the contents of the generated geometry file.
    expected.find("geometry/datafile").set(
        "path", Path(expected.find("geometry/datafile").get("path")).name
    )
    checker = XmlChecker(expected)
    # The upstream profile predates configurable pulse periods and defaults to
    # 1 second. Preserve its existing comparison tolerance for that quantity.
    checker.attr_checks = {
        "hemelbsettings/*/*/condition/period": ScalarQuantityCheck(1.0)
    }
    checker.check_elem(expected, actual)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("generated", type=Path)
    args = parser.parse_args()
    compare(args.reference, args.generated)
