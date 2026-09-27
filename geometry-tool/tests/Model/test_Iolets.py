# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import numpy as np
import pytest

from HlbGmyTool.Model.Iolets import Inlet, Iolet
from HlbGmyTool.Model.Vector import Vector


def test_pressure_equation_formats_valid_values():
    inlet = Inlet()
    assert inlet.PressureEquation == "p = 0.00 + 0.00 cos(wt + 0°)"


def test_pressure_equation_reports_invalid_values():
    inlet = Inlet()
    inlet.Pressure.x = "invalid"

    with pytest.raises(TypeError):
        _ = inlet.PressureEquation


def test_pressure_equation_does_not_hide_unexpected_failures():
    class BrokenPressure:
        @property
        def x(self):
            raise RuntimeError("pressure failed")

    inlet = Inlet()
    object.__setattr__(inlet, "Pressure", BrokenPressure())

    with pytest.raises(RuntimeError, match="pressure failed"):
        _ = inlet.PressureEquation


def test_intersection_uses_radius_squared():
    iolet = Iolet(Centre=Vector(0.0, 0.0, 0.0), Normal=Vector(0.0, 0.0, 1.0), Radius=2.0)

    t, point = iolet.IntersectWithLine(np.array([1.5, 0.0, -1.0]), np.array([1.5, 0.0, 1.0]))

    assert t == pytest.approx(0.5)
    assert point == pytest.approx(np.array([1.5, 0.0, 0.0]))


def test_intersection_rejects_hit_outside_segment():
    iolet = Iolet(Centre=Vector(0.0, 0.0, 0.0), Normal=Vector(0.0, 0.0, 1.0), Radius=2.0)

    t, point = iolet.IntersectWithLine(np.array([0.0, 0.0, 1.0]), np.array([0.0, 0.0, 2.0]))

    assert t == np.finfo(float).max
    assert point is None
