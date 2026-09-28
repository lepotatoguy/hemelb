# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import os
import shutil

import pytest
import yaml

from HlbGmyTool.Model.Profile import Profile
from HlbGmyTool.scripts.cli import check_profile, parser

DATA = os.path.join(os.path.dirname(__file__), "data")


def test_cli_help_documents_profile_overrides(capsys):
    try:
        parser.parse_args(["--help"])
    except SystemExit as error:
        assert error.code == 0

    help_text = capsys.readouterr().out
    assert "profile.pr2" in help_text
    for option in ("--stl", "--stl-units", "--seed", "--voxel", "--timestep"):
        assert option in help_text
    for option in ("--duration", "--period", "--geometry", "--xml"):
        assert option in help_text
    assert "override" in help_text


def test_cli_parses_every_override():
    args = parser.parse_args(
        ["p.pr2", "--stl", "s.stl", "--stl-units", "um", "--seed", "1", "2", "3"]
        + [
            "--voxel",
            "1e-4",
            "--timestep",
            "1e-5",
            "--duration",
            "2",
            "--period",
            "0.8",
        ]
        + ["--geometry", "g.gmy", "--xml", "x.xml"]
    )
    assert args.StlFile == "s.stl"
    assert args.stl_units == "um"
    assert args.seed == [1.0, 2.0, 3.0]
    assert args.VoxelSizeMetres == 1e-4
    assert args.TimeStepSeconds == 1e-5
    assert args.DurationSeconds == 2.0
    assert args.PulsePeriodSeconds == 0.8
    assert args.OutputGeometryFile == "g.gmy"
    assert args.OutputXmlFile == "x.xml"


@pytest.fixture
def profile_dir(tmp_path):
    for name in ("test.pr2", "test.stl"):
        shutil.copy(os.path.join(DATA, name), tmp_path / name)
    return tmp_path


def load(path):
    p = Profile()
    p.LoadFromFile(str(path))
    return p


def test_check_profile_accepts_valid_profile(profile_dir):
    assert check_profile(load(profile_dir / "test.pr2")) == []


def test_check_profile_reports_missing_stl_and_bad_outputs(profile_dir):
    p = load(profile_dir / "test.pr2")
    p.StlFile = str(profile_dir / "missing.stl")
    p.OutputGeometryFile = str(profile_dir / "out.bin")
    p.OutputXmlFile = str(profile_dir / "no-such-folder" / "out.xml")

    problems = check_profile(p)

    assert any("missing.stl" in msg for msg in problems)
    assert any("must end in .gmy" in msg for msg in problems)
    assert any("does not exist" in msg and "out.xml" in msg for msg in problems)


def test_check_profile_rejects_non_positive_voxel(profile_dir):
    p = load(profile_dir / "test.pr2")
    p.VoxelSize = 0.0
    assert any("voxel size" in msg for msg in check_profile(p))


def test_profile_reads_decimal_exponent_written_without_a_point(profile_dir):
    # YAML reads 1e-5 (no ".") as a string, which is not valid hex float.
    path = profile_dir / "test.pr2"
    state = yaml.safe_load(path.read_text())
    state["TimeStepSeconds"] = "1e-5"
    path.write_text(yaml.safe_dump(state))

    assert load(path).TimeStepSeconds == 1e-5


def test_gui_help_text(capsys):
    # gui.py only imports wx inside main(), so its parser can be checked here.
    from HlbGmyTool.scripts.gui import parser as gui_parser

    with pytest.raises(SystemExit):
        gui_parser.parse_args(["--help"])
    help_text = " ".join(capsys.readouterr().out.split())
    assert "STL file into suitable input for HemeLB" in help_text
    assert "override those in the profile file" in help_text
