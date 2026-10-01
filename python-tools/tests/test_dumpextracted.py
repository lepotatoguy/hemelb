# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from io import StringIO
import os.path

import pytest

import numpy as np

import hlb.converters.ExtractedPropertyTextDump as dump_module
from hlb.converters.ExtractedPropertyTextDump import unpack


def test_simple(diffTestDir):
    # Just a smoke test
    snap = os.path.join(diffTestDir, "CleanExtracted", "flow_snapshot.xtr")
    buf = StringIO()
    unpack(snap, stream=buf)


def test_unpack_expands_vector_headers_and_values(monkeypatch):
    fields = np.zeros(
        1,
        dtype=[("grid", ">i4", (3,)), ("velocity", ">f4", (3,)), ("pressure", ">f4")],
    )
    fields["grid"][0] = (1, 2, 3)
    fields["velocity"][0] = (0.1, 0.2, 0.3)
    fields["pressure"][0] = 4.0

    class FakeProperty:
        siteCount = 1
        fieldCount = 3
        originMetres = np.zeros(3)
        voxelSizeMetres = 0.5
        _fieldSpec = [
            ("grid", None, None, (3,), 0),
            ("velocity", None, None, (3,), 0),
            ("pressure", None, None, (), 0),
        ]
        times = [7]

        def __init__(self, filename):
            pass

        def GetByTimeStep(self, timestep):
            return fields

    monkeypatch.setattr(dump_module, "ExtractedProperty", FakeProperty)
    output = StringIO()
    unpack("fake.xtr", stream=output)

    lines = output.getvalue().splitlines()
    header = next(line for line in lines if line.startswith("# grid_0"))
    data = next(line for line in lines if line.startswith("1,"))
    assert (
        header
        == "# grid_0, grid_1, grid_2, velocity_0, velocity_1, velocity_2, pressure"
    )
    assert data.split(",") == ["1", "2", "3", "0.1", "0.2", "0.3", "4.0"]


def test_unpack_can_write_to_a_file(monkeypatch, tmp_path):
    class FakeProperty:
        siteCount = 0
        fieldCount = 0
        originMetres = np.zeros(3)
        voxelSizeMetres = 0.5
        _fieldSpec = []
        times = []

        def __init__(self, filename):
            pass

    monkeypatch.setattr(dump_module, "ExtractedProperty", FakeProperty)
    output = tmp_path / "dump.csv"
    unpack("fake.xtr", out_csv=output)
    assert output.read_text().splitlines()[-1] == "# "


def test_command_line_help_and_missing_file(capsys):
    from hlb.converters.ExtractedPropertyTextDump import main

    with pytest.raises(SystemExit) as help_exit:
        main(["--help"])
    assert help_exit.value.code == 0
    assert "extraction file (.xtr)" in capsys.readouterr().out

    with pytest.raises(SystemExit) as missing:
        main(["no-such-file.xtr"])
    assert missing.value.code == 2
    assert "does not exist" in capsys.readouterr().err


def test_command_line_writes_to_stdout_or_file(monkeypatch, tmp_path):
    import hlb.converters.ExtractedPropertyTextDump as dump

    source = tmp_path / "in.xtr"
    source.write_bytes(b"")
    calls = []
    monkeypatch.setattr(
        dump, "unpack", lambda f, stream=None, out_csv=None: calls.append((f, out_csv))
    )
    dump.main([str(source)])
    dump.main([str(source), str(tmp_path / "out.csv")])
    assert calls == [(str(source), None), (str(source), str(tmp_path / "out.csv"))]


def _reference_rows(fields, field_spec):
    # The original one-value-at-a-time writer, kept to check that the faster
    # column-at-a-time writer produces exactly the same text.
    import csv

    out = StringIO()
    writer = csv.writer(out, lineterminator="\n")
    for row in fields:
        values = []
        for name, xdrType, memType, length, offset in field_spec:
            value = row[name]
            width = length[0] if isinstance(length, tuple) and length else 1
            if width == 1:
                values.append(value)
            else:
                values.extend(value)
        writer.writerow(values)
    return out.getvalue()


def test_fast_writer_matches_reference(diffTestDir):
    from hlb.parsers.extraction import ExtractedProperty

    snap = os.path.join(diffTestDir, "CleanExtracted", "flow_snapshot.xtr")
    prop = ExtractedProperty(snap)
    for t in prop.times:
        fields = prop.GetByTimeStep(t)
        fast = StringIO()
        dump_module._write_rows(fields, prop._fieldSpec, fast)
        assert fast.getvalue() == _reference_rows(fields, prop._fieldSpec)
