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
        field += struct.pack(">III", 1, 0, 1)
        field += struct.pack(">d" if version == 7 else ">f", 10)
        if version >= 6:
            field += struct.pack(">d" if version == 7 else ">f", 2)
        if version == 7:
            field += struct.pack(">I", 4) + b"mmHg"
    header = struct.pack(">III", HemeLbMagicNumber, ExtractionMagicNumber, version)
    if version >= 6:
        header += struct.pack(">7dQII", 0.5, 0.1, 1, 1, 2, 3, 0, 1, 1, len(field))
    else:
        header += struct.pack(">4dQII", 0.5, 1, 2, 3, 1, 1, len(field))
    path.write_bytes(header + field + struct.pack(">QIIIf", 7, 2, 4, 6, 3))


@pytest.mark.parametrize("version", [4, 5, 6, 7])
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
    assert values.pressure[0] == (26 if version >= 6 else 13)


def test_version6_can_return_unscaled_values(tmp_path):
    path = tmp_path / "sample.xtr"
    write_extraction(path, 6)
    values = ExtractedProperty(path, physical_units=False).GetByTimeStep(7)
    assert values.pressure[0] == 13


def test_version7_exposes_pressure_units_in_csv(tmp_path):
    from io import StringIO
    from hlb.converters.ExtractedPropertyTextDump import unpack

    path = tmp_path / "sample.xtr"
    write_extraction(path, 7)
    extraction = ExtractedProperty(path)
    assert extraction.field_units == {"pressure": "mmHg"}
    assert (
        ExtractedProperty(path, physical_units=False).GetByTimeStep(7).pressure[0] == 13
    )
    output = StringIO()
    unpack(str(path), stream=output)
    assert '"pressure", length (), units mmHg' in output.getvalue()


@pytest.mark.parametrize(
    "typecode,format_code", [(2, "i"), (3, "I"), (4, "q"), (5, "Q")]
)
def test_version7_integer_pressure_preserves_fractional_scale_and_offset(
    tmp_path, typecode, format_code
):
    path = tmp_path / "integer-pressure.xtr"
    field = (
        struct.pack(">I", 8)
        + b"pressure"
        + struct.pack(">IIIddI", 1, typecode, 1, 0.5, 0.075006157593, 4)
        + b"mmHg"
    )
    header = struct.pack(
        ">III7dQII",
        HemeLbMagicNumber,
        ExtractionMagicNumber,
        7,
        0.5,
        0.1,
        1,
        1,
        2,
        3,
        0,
        1,
        1,
        len(field),
    )
    path.write_bytes(
        header + field + struct.pack(">QIII" + format_code, 7, 2, 4, 6, 20)
    )
    extraction = ExtractedProperty(path)
    values = extraction.GetByIndex(0).pressure
    assert values.dtype == np.float64
    assert values[0] == pytest.approx(20.5 * 0.075006157593)
    assert extraction.field_units == {"pressure": "mmHg"}
    raw = ExtractedProperty(path, physical_units=False)
    assert raw.GetByIndex(0).pressure[0] == 20.5
    assert raw.field_units == {"pressure": "lattice"}
