# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import struct
import numpy as np
import pytest

from hlb.parsers import HemeLbMagicNumber
from hlb.parsers.extraction import ExtractedProperty, ExtractionMagicNumber


def write_extraction(path, version):
    name = b"pressure"
    field = struct.pack(">I", len(name)) + name
    if version == 4:
        field += struct.pack(">Id", 1, 10)
    else:
        field += struct.pack(">IIIf", 1, 0, 1, 10)
        if version == 6:
            field += struct.pack(">f", 2)
    header = struct.pack(">III", HemeLbMagicNumber, ExtractionMagicNumber, version)
    if version == 6:
        header += struct.pack(">7dQII", 0.5, 0.1, 1, 1, 2, 3, 0, 1, 1, len(field))
    else:
        header += struct.pack(">4dQII", 0.5, 1, 2, 3, 1, 1, len(field))
    path.write_bytes(header + field + struct.pack(">QIIIf", 7, 2, 4, 6, 3))


@pytest.mark.parametrize("version", [4, 5, 6])
def test_extraction_versions_preserve_coordinates_and_offsets(tmp_path, version):
    path = tmp_path / "sample.xtr"
    write_extraction(path, version)
    extraction = ExtractedProperty(path)
    values = extraction.GetByTimeStep(7)
    assert extraction.version == version
    assert extraction.siteCount == 1
    assert list(extraction.times) == [7]
    assert np.array_equal(values.grid, [[2, 4, 6]])
    assert np.array_equal(values.position, [[2, 4, 6]])
    assert values.pressure[0] == (26 if version == 6 else 13)


def test_version6_can_return_unscaled_values(tmp_path):
    path = tmp_path / "sample.xtr"
    write_extraction(path, 6)
    values = ExtractedProperty(path, physical_units=False).GetByTimeStep(7)
    assert values.pressure[0] == 13
