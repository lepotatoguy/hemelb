import os
import pickle
import sys

import pytest

from HlbGmyTool.Model.Profile import Profile
from HlbGmyTool.Util.ProfileUpdateTools import LoadFakeProfile, Upgrade


LEGACY_PROFILE = os.path.join(
    os.path.dirname(__file__),
    "../../../Code/tests/pythontests/resources/poiseuille_flow_test.pro",
)


class _MaliciousPayload:
    def __init__(self, marker):
        self.marker = marker

    def __reduce__(self):
        return os.system, ("touch %s" % self.marker,)


def test_profile_v1_rejects_globals_outside_legacy_namespace(tmp_path):
    marker = tmp_path / "marker"

    payload = tmp_path / "malicious.pro"
    with payload.open("wb") as stream:
        pickle.dump(_MaliciousPayload(str(marker)), stream, protocol=2)

    with pytest.raises(pickle.UnpicklingError):
        Profile().LoadFromFile(str(payload))

    assert not marker.exists()


def test_profile_update_loader_rejects_globals_outside_legacy_namespace(tmp_path):
    marker = tmp_path / "marker"

    payload = tmp_path / "malicious.pro"
    with payload.open("wb") as stream:
        pickle.dump(_MaliciousPayload(str(marker)), stream, protocol=2)

    with pytest.raises(pickle.UnpicklingError):
        LoadFakeProfile(str(payload))

    assert not marker.exists()


def test_legacy_profile_converts_old_timing_fields():
    profile = Profile()
    profile.LoadFromFile(LEGACY_PROFILE)

    assert profile.TimeStepSeconds == pytest.approx((60.0 / 70.0) / 1000)
    assert profile.DurationSeconds == pytest.approx((60.0 / 70.0) * 3)


def test_legacy_iolet_kind_comes_from_pickled_class(tmp_path):
    with open(LEGACY_PROFILE, "rb") as stream:
        data = stream.read()
    data = data.replace(b"Inlet1", b"Input1").replace(b"Outlet1", b"Output1")

    payload = tmp_path / "renamed-iolets.pro"
    payload.write_bytes(data)

    profile = Profile()
    profile.LoadFromFile(str(payload))

    assert [type(io).__name__ for io in profile.Iolets] == ["Inlet", "Outlet"]


def test_profile_upgrade_preserves_input_paths_when_output_moves(tmp_path):
    source = os.path.abspath(LEGACY_PROFILE)
    output = tmp_path / "converted.pr2"

    Upgrade(source, str(output))

    profile = Profile()
    profile.LoadFromFile(str(output))
    assert profile.StlFile == os.path.splitext(source)[0] + ".stl"


def test_pro_to_pr2_entrypoint_writes_a_loadable_profile(tmp_path, monkeypatch):
    from HlbGmyTool.scripts.pro_to_pr2 import main

    output = tmp_path / "converted.pr2"
    monkeypatch.setattr(
        sys,
        "argv",
        ["hlb-pro2pr2", LEGACY_PROFILE, str(output)],
    )

    main()

    profile = Profile()
    profile.LoadFromFile(str(output))
    assert profile.DurationSeconds == pytest.approx((60.0 / 70.0) * 3)
