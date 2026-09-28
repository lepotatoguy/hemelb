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
