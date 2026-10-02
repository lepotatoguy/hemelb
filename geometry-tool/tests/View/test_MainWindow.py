# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import os
import sys

import pytest

wx = pytest.importorskip("wx")
if sys.platform.startswith("linux") and not os.environ.get("DISPLAY"):
    pytest.skip(
        "A display is required for window layout tests", allow_module_level=True
    )

from HlbGmyTool.App import SetupTool
from HlbGmyTool.View.VtkViewPanel import RWI, wxVTKRenderWindowInteractor


@pytest.mark.parametrize("platform", ["__WXMAC__", "__WXGTK__", "__WXMSW__"])
def test_preview_paint_propagation_is_only_changed_on_macos(monkeypatch, platform):
    class PaintEvent:
        def Skip(self, value=True):
            self.skipped = value

    event = PaintEvent()
    calls = []

    def vtk_paint(window, received):
        calls.append(received)
        received.Skip()

    monkeypatch.setattr(wx, "Platform", platform)
    monkeypatch.setattr(wxVTKRenderWindowInteractor, "OnPaint", vtk_paint)
    RWI.OnPaint(object(), event)
    assert calls == [event]
    assert event.skipped is (platform != "__WXMAC__")


@pytest.fixture(scope="module")
def app():
    application = SetupTool(redirect=False)
    application.Yield()
    yield application
    application.view.Destroy()
    application.Yield()
    application.Destroy()


def test_generate_button_is_visible_on_startup(app):
    window = app.view
    tools = window.toolPanel
    button = tools.controlPanel.generateButton
    assert tools.GetScreenRect().Contains(button.GetScreenRect())
    assert window.vtkPanel.GetClientSize().width >= window.FromDIP(160)


def test_output_controls_remain_reachable_in_a_small_window(app):
    window = app.view
    window.SetClientSize(window.FromDIP((720, 360)))
    window.Layout()
    app.Yield()
    tools = window.toolPanel
    assert tools.GetVirtualSize().height > tools.GetClientSize().height

    for button in (
        tools.outputPanel.xmlChooseButton,
        tools.outputPanel.geometryChooseButton,
        tools.controlPanel.generateButton,
    ):
        tools.ScrollChildIntoView(button)
        app.Yield()
        assert tools.GetScreenRect().Contains(button.GetScreenRect())


def test_opening_another_profile_detaches_the_old_selection(app, monkeypatch, capfd):
    from pathlib import Path
    import HlbGmyTool.Controller.ProfileController as pc

    sample = Path(__file__).parents[1] / "Model/data/test.pr2"
    legacy = Path(__file__).parents[3] / (
        "Code/tests/pythontests/resources/poiseuille_flow_test.pro"
    )

    class FileDialog:
        path = sample

        def __init__(self, *args, **kwargs):
            pass

        def ShowModal(self):
            return wx.ID_OK

        def GetPath(self):
            return str(self.path)

        def Destroy(self):
            pass

    errors = []
    monkeypatch.setattr(pc.wx, "FileDialog", FileDialog)
    monkeypatch.setattr(pc, "ShowMessage", lambda text, icon: errors.append(text))
    for path in (sample, legacy, sample):
        FileDialog.path = path
        app.controller.LoadFromFile()
        assert not errors
        assert app.controller.Iolets.SelectedIndex is None
        bounds = app.pipeline.Renderer.ComputeVisiblePropBounds()
        centre = [(bounds[i] + bounds[i + 1]) / 2 for i in (0, 2, 4)]
        assert app.pipeline.Renderer.GetActiveCamera().GetFocalPoint() == pytest.approx(
            centre
        )
        app.controller.Iolets.SelectedIndex = 0
        assert app.controller.Iolets.Selection.delegate is app.profile.Iolets[0]
        app.Yield()
        panel = app.view.toolPanel.ioletsPanel
        assert panel.detail.nameField.GetValue() == app.profile.Iolets[0].Name
        assert panel.ioletsListCtrl.GetSelectedItemCount() == 1
        app.controller.Iolets.SelectedIndex = None
        assert panel.ioletsListCtrl.GetSelectedItemCount() == 0
    assert "Traceback" not in capfd.readouterr().err
