from HlbGmyTool.scripts.cli import parser


def test_cli_help_documents_profile_overrides(capsys):
    try:
        parser.parse_args(["--help"])
    except SystemExit as error:
        assert error.code == 0

    help_text = capsys.readouterr().out
    assert "profile.pr2" in help_text
    assert "--geometry" in help_text
    assert "--voxel" in help_text
    assert "override" in help_text
