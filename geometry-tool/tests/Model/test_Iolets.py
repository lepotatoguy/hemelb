import numpy as np
import pytest

from HlbGmyTool.Model.Iolets import Iolet
from HlbGmyTool.Model.Vector import Vector


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
