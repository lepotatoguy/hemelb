# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from pathlib import Path

import pytest
from vtk import vtkRenderWindowInteractor

# ProfileController is part of the GUI and imports wxPython, which is only
# installed with the [gui] extra.
pytest.importorskip("wx")

from HlbGmyTool.Controller.PipelineController import PipelineController
from HlbGmyTool.Controller.ProfileController import ProfileController
from HlbGmyTool.Model.Pipeline import Pipeline
from HlbGmyTool.Model.Profile import Profile
from HlbGmyTool.Model.Iolets import Inlet, Outlet


def test_stl_preview_waits_for_a_loaded_mesh(capfd):
    profile = Profile()
    pipeline = Pipeline()
    profile_controller = ProfileController(profile)
    PipelineController(pipeline, profile_controller)

    assert not profile.HasLoadedStlFile
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 0

    profile.StlFile = "missing.stl"
    assert not profile.HasLoadedStlFile
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 0

    sample = Path(__file__).parent / "data" / "test.stl"
    profile.StlFile = str(sample)
    assert profile.HasLoadedStlFile
    assert profile.VoxelSize > 0
    assert profile.BoundingBoxSize > 0
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 1
    assert pipeline.SurfaceActor.GetVisibility()

    profile.StlFile = ""
    assert not profile.HasLoadedStlFile
    assert profile.BoundingBoxSize == 0
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 0
    assert not pipeline.SurfaceActor.GetVisibility()

    profile.StlFile = str(sample)
    assert profile.HasLoadedStlFile
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 1
    assert pipeline.SurfaceActor.GetVisibility()

    output = capfd.readouterr()
    assert "A FileName must be specified" not in output.err
    assert "UpdateInformation invoked during another request" not in output.err


def test_sample_profile_loads_after_preview_setup(capfd):
    profile = Profile()
    pipeline = Pipeline()
    profile_controller = ProfileController(profile)
    PipelineController(pipeline, profile_controller)
    pipeline.PlacedIolets.SetInteractor(vtkRenderWindowInteractor())

    sample = Path(__file__).parent / "data" / "test.pr2"
    profile.LoadFromFile(str(sample))

    assert profile.HasLoadedStlFile
    assert pipeline.SurfaceMapper.GetNumberOfInputConnections(0) == 1
    assert len(profile.Iolets) == 2
    errors = capfd.readouterr().err
    assert "UpdateInformation invoked during another request" not in errors


class _FakeStdin:
    def __init__(self, tty):
        self.tty = tty

    def isatty(self):
        return self.tty


def test_debug_needs_a_terminal(monkeypatch):
    import HlbGmyTool.Controller.ProfileController as pc

    calls = []
    monkeypatch.setattr(pc.pdb, "set_trace", lambda: calls.append("pdb"))
    monkeypatch.setattr(pc, "ShowMessage", lambda text, icon: calls.append(text))
    controller = ProfileController(Profile())

    monkeypatch.setattr(pc.sys, "stdin", _FakeStdin(False))
    assert not pc.StartedFromTerminal()
    controller.Debug()
    assert calls and "needs a terminal" in calls[-1]

    monkeypatch.setattr(pc.sys, "stdin", _FakeStdin(True))
    assert pc.StartedFromTerminal()
    controller.Debug()
    assert calls[-1] == "pdb"


def test_started_from_terminal_without_stdin(monkeypatch):
    import HlbGmyTool.Controller.ProfileController as pc

    monkeypatch.setattr(pc.sys, "stdin", None)
    assert not pc.StartedFromTerminal()


def test_new_iolets_do_not_reuse_loaded_names():
    profile = Profile()
    profile.Iolets.append(Inlet(Name="Inlet1"))
    profile.Iolets.append(Outlet(Name="Outlet1"))
    controller = ProfileController(profile)
    controller.Iolets.AddInlet()
    controller.Iolets.AddOutlet()
    assert [iolet.Name for iolet in profile.Iolets] == [
        "Inlet1",
        "Outlet1",
        "Inlet2",
        "Outlet2",
    ]


def test_legacy_profile_keeps_controller_bindings():
    profile = Profile()
    profile.Iolets.append(Inlet(Name="Previous inlet"))
    controller = ProfileController(profile)
    iolets = profile.Iolets
    seed = profile.SeedPoint
    legacy = Path(__file__).parents[3] / (
        "Code/tests/pythontests/resources/poiseuille_flow_test.pro"
    )
    expected = Profile()
    expected.LoadFromFile(str(legacy))
    profile.LoadFromFile(str(legacy))

    assert profile.Iolets is iolets is controller.Iolets.delegate
    assert profile.SeedPoint is seed is controller.SeedPoint.delegate
    assert [io.Name for io in controller.Iolets.delegate] == [
        io.Name for io in expected.Iolets
    ]
    controller.SeedPoint.SetValueForKey("x", 0.25)
    assert profile.SeedPoint.x == 0.25
    controller.Iolets.AddOutlet()
    assert profile.Iolets[-1].Name == "Outlet2"


@pytest.mark.parametrize("action", ["SetViewX", "SetViewY", "SetViewZ"])
def test_camera_actions_redraw_the_preview(monkeypatch, action):
    pipeline = Pipeline()
    renders = []
    monkeypatch.setattr(pipeline, "Render", lambda: renders.append(True))
    getattr(pipeline, action)()
    assert renders == [True]


def test_placed_items_redraw_without_hiding_the_surface(monkeypatch):
    pipeline = Pipeline()
    renders = []
    monkeypatch.setattr(pipeline, "Render", lambda: renders.append(True))
    pipeline.PlacedSeed.Enabled = True
    assert pipeline.Renderer.HasViewProp(pipeline.PlacedSeed.actor)
    pipeline.PlacedSeed.Enabled = False
    assert not pipeline.Renderer.HasViewProp(pipeline.PlacedSeed.actor)
    assert pipeline.Renderer.HasViewProp(pipeline.SurfaceActor)
    assert renders == [True, True]


def test_pipeline_uses_the_interactors_window_readiness_guard():
    class Interactor:
        def __init__(self):
            self.window = None
            self.renders = 0

        def GetRenderWindow(self):
            return self.window

        def Render(self):
            self.renders += 1

    pipeline = Pipeline()
    pipeline.Interactor = Interactor()
    pipeline.Render()
    assert pipeline.Interactor.renders == 0
    # A wx interactor can have a render window before its native handle is
    # ready. Its Render method owns that check; the pipeline must use it.
    pipeline.Interactor.window = object()
    pipeline.Render()
    assert pipeline.Interactor.renders == 1


def test_wx_preview_repaints_after_the_current_edit():
    class Interactor:
        def GetRenderWindow(self):
            return object()

        def Refresh(self, erase):
            self.erase = erase

        def Render(self):
            pytest.fail("wx repainting must wait for the paint event")

    pipeline = Pipeline()
    pipeline.Interactor = Interactor()
    pipeline.Render()
    assert pipeline.Interactor.erase is False
