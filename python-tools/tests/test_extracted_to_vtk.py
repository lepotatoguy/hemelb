# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import struct
import xml.etree.ElementTree as ET

import numpy as np
import pytest
import vtk
from vtk.util.numpy_support import vtk_to_numpy

from hlb.converters.ExtractedPropertyToVtk import export, main
from hlb.parsers import HemeLbMagicNumber
from hlb.parsers.extraction import ExtractedProperty, ExtractionMagicNumber


def _string(value):
    data = value.encode("ascii")
    return struct.pack(">I", len(data)) + data + b"\0" * (-len(data) % 4)


def _extraction(path, version=5, changed_sites=False):
    fields = [("pressure", 1), ("custom_velocity", 3), ("stress", 6)]
    if version == 5:
        fields.append(("large_integer", 1))
    headers = []
    for name, width in fields:
        header = _string(name) + struct.pack(">I", width)
        if version == 4:
            header += struct.pack(">d", 100.0 if name == "pressure" else 0.0)
        else:
            typecode = 5 if name == "large_integer" else 0
            offsets = [100.0] if name == "pressure" else []
            header += struct.pack(">II", typecode, len(offsets))
            header += b"".join(struct.pack(">f", value) for value in offsets)
        headers.append(header)
    field_header = b"".join(headers)
    output = (
        struct.pack(
            ">IIIddddQII",
            HemeLbMagicNumber,
            ExtractionMagicNumber,
            version,
            0.2,
            1.0,
            -2.0,
            3.0,
            2,
            len(fields),
            len(field_header),
        )
        + field_header
    )
    for time, coordinates in [
        (7, [(2, 3, 4), (3, 3, 4)]),
        (11, [(3, 3, 4), (2, 3, 4)]),
    ]:
        output += struct.pack(">Q", time)
        for coordinate in coordinates:
            actual = (
                (9, 3, 4)
                if changed_sites and time == 11 and coordinate[0] == 2
                else coordinate
            )
            output += struct.pack(">III", *actual)
            for name, width in fields:
                if name == "large_integer":
                    output += struct.pack(">Q", 2**60 + coordinate[0] + time)
                else:
                    output += struct.pack(
                        ">" + "f" * width,
                        *[
                            coordinate[0] + time + component * 0.125
                            for component in range(width)
                        ]
                    )
    path.write_bytes(output)
    return path


@pytest.mark.parametrize("version", [4, 5])
@pytest.mark.parametrize("step_length", [None, 0.01])
def test_roundtrip_fields_positions_and_row_order(tmp_path, version, step_length):
    source = _extraction(tmp_path / "source.xtr", version)
    parsed = ExtractedProperty(str(source))
    pvd = export(source, tmp_path / "nested/series.pvd", step_length)
    datasets = ET.parse(pvd).getroot().findall("Collection/DataSet")
    assert [float(node.get("timestep")) for node in datasets] == [
        time if step_length is None else time * step_length for time in parsed.times
    ]
    for timestep, node in zip(parsed.times, datasets):
        path = pvd.parent / node.get("file")
        assert all(
            part == "ascii"
            for part in [
                array.get("format") for array in ET.parse(path).iter("DataArray")
            ]
        )
        reader = vtk.vtkXMLUnstructuredGridReader()
        reader.SetFileName(str(path))
        reader.Update()
        grid = reader.GetOutput()
        assert reader.GetErrorCode() == 0
        assert grid.GetNumberOfCells() == 2
        assert grid.GetNumberOfPoints() == 12  # Adjacent voxels share four corners.
        assert grid.GetBounds() == pytest.approx((1.3, 1.7, -1.5, -1.3, 3.7, 3.9))
        centers = vtk.vtkCellCenters()
        centers.SetInputData(grid)
        centers.Update()
        positions = vtk_to_numpy(centers.GetOutput().GetPoints().GetData())
        data = parsed.GetByTimeStep(timestep)
        rows = {tuple(row): index for index, row in enumerate(data.grid)}
        coordinates = vtk_to_numpy(grid.GetCellData().GetArray("grid"))
        order = [rows[tuple(coordinate)] for coordinate in coordinates]
        np.testing.assert_allclose(positions, data.position[order], rtol=1e-6)
        for name, _, _, length, _ in parsed.GetFieldSpec():
            actual = vtk_to_numpy(grid.GetCellData().GetArray(name))
            expected = data[name][order]
            if length == (6,):
                expected = expected[:, [0, 3, 5, 1, 4, 2]]
            np.testing.assert_array_equal(actual, expected)
        if version == 5:
            assert (
                vtk_to_numpy(grid.GetCellData().GetArray("large_integer")).dtype
                == np.uint64
            )


def test_existing_frame_is_not_overwritten(tmp_path):
    source = _extraction(tmp_path / "source.xtr")
    existing = tmp_path / "series_11.vtu"
    existing.write_text("keep this file")
    with pytest.raises(ValueError, match="output already exists"):
        export(source, tmp_path / "series")
    assert existing.read_text() == "keep this file"
    assert not (tmp_path / "series_7.vtu").exists()
    assert not (tmp_path / "series.pvd").exists()


@pytest.mark.parametrize("step_length", [0, -1, float("nan"), float("inf")])
def test_invalid_time_scale(tmp_path, step_length):
    source = _extraction(tmp_path / "source.xtr")
    with pytest.raises(ValueError, match="step length must be finite and positive"):
        export(source, step_length=step_length)
    assert not (tmp_path / "source.pvd").exists()


def test_changed_site_set_is_rejected(tmp_path):
    source = _extraction(tmp_path / "source.xtr", changed_sites=True)
    with pytest.raises(ValueError, match="site set changed at timestep 11"):
        export(source)
    assert not (tmp_path / "source.pvd").exists()


def test_help_missing_input_and_default_prefix(tmp_path, capsys):
    with pytest.raises(SystemExit) as help_exit:
        main(["--help"])
    assert help_exit.value.code == 0
    assert "No GMY or XML" in capsys.readouterr().out
    with pytest.raises(SystemExit) as missing:
        main([str(tmp_path / "missing.xtr")])
    assert missing.value.code == 1
    assert "does not exist" in capsys.readouterr().err
    source = _extraction(tmp_path / "source.xtr")
    main([str(source)])
    assert (tmp_path / "source.pvd").is_file()
    assert "open this collection in ParaView" in capsys.readouterr().out
