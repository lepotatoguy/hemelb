# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import pytest

import filecmp
import os.path

import numpy as np
from numpy.linalg import norm

from HlbGmyTool.Model import OutputGeneration
from HlbGmyTool.Model.Profile import Profile
from HlbGmyTool.Model.Vector import Vector
from HlbGmyTool.Model.Iolets import Iolet
from vtk import vtkSphereSource, vtkTriangleFilter
from hlb.parsers.geometry.simple import ConfigLoader
from hlb.utils.xml_compare import XmlChecker
import fixtures

dataDir = os.path.join(os.path.split(__file__)[0], "data")


def test_pipeline_walk_stops_at_source():
    source = vtkSphereSource()
    disconnected_filter = vtkTriangleFilter()
    triangle_filter = vtkTriangleFilter()
    triangle_filter.SetInputConnection(source.GetOutputPort())

    assert OutputGeneration.getpipeline(disconnected_filter) == [disconnected_filter]
    assert OutputGeneration.getpipeline(triangle_filter) == [source, triangle_filter]


def test_pipeline_walk_reports_unexpected_failures():
    class FailingAlgorithm:
        def GetNumberOfInputPorts(self):
            raise RuntimeError("pipeline failed")

    with pytest.raises(RuntimeError, match="pipeline failed"):
        OutputGeneration.getpipeline(FailingAlgorithm())


class TestPolyDataGenerator:
    def test_regression(self, tmpdir):
        """Generate a gmy from a stored profile and check that the output is
        identical.
        """
        proFileName = os.path.join(dataDir, "test.pr2")

        p = Profile()
        p.LoadFromFile(proFileName)
        # Change the output to the tmpdir
        basename = tmpdir.join("test").strpath
        outGmyFileName = basename + ".gmy"
        outXmlFileName = basename + ".xml"
        p.OutputGeometryFile = outGmyFileName
        p.OutputXmlFile = outXmlFileName

        generator = OutputGeneration.PolyDataGenerator(p)
        generator.Execute()

        assert filecmp.cmp(outGmyFileName, os.path.join(dataDir, "test.gmy"))
        xmlChecker = XmlChecker.from_path(os.path.join(dataDir, "test.xml"))
        xmlChecker.check_path(outXmlFileName)

    @staticmethod
    def _test_profile(tmpdir):
        p = Profile()
        p.LoadFromFile(os.path.join(dataDir, "test.pr2"))
        p.OutputGeometryFile = tmpdir.join("test.gmy").strpath
        p.OutputXmlFile = tmpdir.join("test.xml").strpath
        return p

    @pytest.mark.parametrize("period, written", [(None, "1"), (0.8, "0.8")])
    def test_cosine_period_written_to_xml(self, tmpdir, period, written):
        p = self._test_profile(tmpdir)
        if period is not None:
            p.PulsePeriodSeconds = period
        OutputGeneration.PolyDataGenerator(p).Execute()
        import xml.etree.ElementTree as ET

        periods = [e.get("value") for e in ET.parse(p.OutputXmlFile).iter("period")]
        assert periods == [written, written]  # one inlet and one outlet

    def test_valid_profile_has_no_warnings(self, tmpdir):
        generator = OutputGeneration.PolyDataGenerator(self._test_profile(tmpdir))
        assert generator.Warnings == []

    def test_warns_when_an_iolet_cannot_reach_the_surface(self, tmpdir):
        p = self._test_profile(tmpdir)
        p.Iolets[0].Radius = 0.01
        generator = OutputGeneration.PolyDataGenerator(p)
        assert len(generator.Warnings) == 1
        assert "Inlet1 does not reach the surface" in generator.Warnings[0]

    def test_warns_when_the_seed_point_is_outside(self, tmpdir):
        p = self._test_profile(tmpdir)
        p.SeedPoint = Vector(50.0, 50.0, 50.0)
        generator = OutputGeneration.PolyDataGenerator(p)
        assert len(generator.Warnings) == 1
        assert "seed point is not inside" in generator.Warnings[0]

    @pytest.mark.parametrize("capped", [True, False])
    @pytest.mark.parametrize("end_z, expect_warning", [(4.5, False), (5.5, True)])
    def test_open_and_closed_tubes(self, tmpdir, capped, end_z, expect_warning):
        # Closed ("can") and open ("pipe") surfaces behave the same: open ends
        # are capped as wall, and iolets must lie on or inside the vessel end.
        from vtk import (
            vtkCylinderSource,
            vtkSTLWriter,
            vtkTransform,
            vtkTransformPolyDataFilter,
            vtkTriangleFilter,
        )
        from HlbGmyTool.Model.Iolets import Inlet, Outlet

        source = vtkCylinderSource()
        source.SetRadius(1.0)
        source.SetHeight(10.0)
        source.SetResolution(32)
        source.SetCapping(capped)
        rotate = vtkTransform()
        rotate.RotateX(90)
        transform = vtkTransformPolyDataFilter()
        transform.SetTransform(rotate)
        transform.SetInputConnection(source.GetOutputPort())
        triangles = vtkTriangleFilter()
        triangles.SetInputConnection(transform.GetOutputPort())
        writer = vtkSTLWriter()
        writer.SetFileName(tmpdir.join("tube.stl").strpath)
        writer.SetInputConnection(triangles.GetOutputPort())
        writer.Write()

        p = Profile()
        p.StlFile = tmpdir.join("tube.stl").strpath
        p.VoxelSize = 0.2
        p.SeedPoint = Vector(0.0, 0.0, 0.0)
        p.Iolets.append(
            Inlet(Centre=Vector(0, 0, -end_z), Normal=Vector(0, 0, 1), Radius=1.5)
        )
        p.Iolets.append(
            Outlet(Centre=Vector(0, 0, end_z), Normal=Vector(0, 0, -1), Radius=1.5)
        )
        p.OutputGeometryFile = tmpdir.join("tube.gmy").strpath
        p.OutputXmlFile = tmpdir.join("tube.xml").strpath

        generator = OutputGeneration.PolyDataGenerator(p)
        outside = [w for w in generator.Warnings if "lies outside the vessel" in w]
        assert len(outside) == (2 if expect_warning else 0)

    def test_cube(self, tmpdir):
        """Generate a gmy from a simple cubic profile and check the output"""
        cube = fixtures.cube(tmpdir)
        cube.VoxelSize = 0.23
        cube.StlFileUnitId = 0
        generator = OutputGeneration.PolyDataGenerator(cube)
        generator.Execute()
        # Load back the resulting geometry file and assert things are as
        # expected
        checker = CubeTestingGmyParser(cube.OutputXmlFile, cube.VoxelSize)
        checker.Load()

        fluid_sites = sum(checker.Domain.BlockFluidSiteCounts)
        block_count = len(checker.Domain.Blocks)
        block_size = checker.Domain.BlockSize
        sites = block_count * block_size**3
        # assert(sites==4096)
        # assert(fluid_sites==729)
        assert sites != fluid_sites
        # # Now, turn on the skip-non-intersecting-blocks optimisation, and
        # # assert same result
        # generator.skipNonIntersectingBlocks = True
        # generator.Execute()
        # checker_skip_nonintersecting = CubeTestingGmyParser(
        #     cube.OutputXmlFile, cube.VoxelSize)
        # checker_skip_nonintersecting.Load()
        # fluid_sites_nonintersecting = sum(
        #     checker_skip_nonintersecting.Domain.BlockFluidSiteCounts)
        # assert(fluid_sites_nonintersecting == fluid_sites)

    def test_cube_normals(self, tmpdir):
        """Generate a gmy from a simple cubic profile and check the computed
        normals.
        """
        cube = fixtures.cube(tmpdir)
        cube.VoxelSize = 0.23
        cube.StlFileUnitId = 0

        """The default VTK cube has 1m edges and it is centred at the origin 
        of coordinates. We place the inlet and the outlet at the faces 
        perpendicular to the z axis.
        """
        inlet = Iolet(
            Name="inlet",
            Centre=Vector(0.0, 0.0, -0.5),
            Normal=Vector(0.0, 0.0, -1.0),
            Radius=np.sqrt(2) / 2,
        )
        outlet = Iolet(
            Name="outlet",
            Centre=Vector(0.0, 0.0, 0.5),
            Normal=Vector(0.0, 0.0, 1.0),
            Radius=np.sqrt(2) / 2,
        )
        cube.Iolets = [inlet, outlet]

        generator = OutputGeneration.PolyDataGenerator(cube)
        # generator.skipNonIntersectingBlocks = True
        generator.Execute()

        """Load back the resulting geometry file and assert things are as 
        expected
        """
        checker = CubeNormalsTestingGmyParser(cube.OutputXmlFile, cube.VoxelSize)
        checker.Load()

    def test_cylinder(self, tmpdir):
        """Generate a gmy from a simple cylinder profile and check the output"""
        cylinder = fixtures.cylinder(tmpdir)
        cylinder.VoxelSize = 0.23
        cylinder.StlFileUnitId = 0

        """ The default VTK cylinder is 1 length unit long, aligned with the 
        y-axis, and centred at the origin of coordinates.
        """
        inlet = Iolet(
            Name="inlet",
            Centre=Vector(0.0, -0.5, 0.0),
            Normal=Vector(0.0, -1.0, 0.0),
            Radius=1,
        )
        outlet = Iolet(
            Name="outlet",
            Centre=Vector(0.0, 0.5, 0.0),
            Normal=Vector(0.0, 1.0, 0.0),
            Radius=1,
        )
        cylinder.Iolets = [inlet, outlet]

        generator = OutputGeneration.PolyDataGenerator(cylinder)
        generator.Execute()
        # Load back the resulting geometry file and assert things are as
        # expected
        checker = CylinderTestingGmyParser(
            cylinder.OutputXmlFile,
            cylinder.VoxelSize,
            np.array([0.0, 1.0, 0.0]),
            1.0,
            0.5,
        )
        checker.Load()

        fluid_sites = sum(checker.Domain.BlockFluidSiteCounts)
        block_count = len(checker.Domain.Blocks)
        block_size = checker.Domain.BlockSize
        sites = block_count * block_size**3
        # assert(sites==4096)
        # assert(fluid_sites==621)
        assert sites != fluid_sites
        # # Now, turn on the skip-non-intersecting-blocks optimisation, and
        # # assert same result
        # generator.skipNonIntersectingBlocks = True
        # generator.Execute()
        # checker_skip_nonintersecting = CylinderTestingGmyParser(
        #     cylinder.OutputGeometryFile, cylinder.VoxelSize,
        #     np.array([0.0, 1.0, 0.0]), 1.0, 0.5)
        # checker_skip_nonintersecting.Load()
        # fluid_sites_nonintersecting = sum(
        #     checker_skip_nonintersecting.Domain.BlockFluidSiteCounts)
        # assert(fluid_sites_nonintersecting == fluid_sites)


class TestCylinderGenerator:
    @pytest.mark.parametrize(("randSeed",), [(828,), (341,), (1432,)])
    def test_regression(self, tmpdir, randSeed):
        """Generate a small cylinder GMY with a random orientation. Then check
        that the output is correct by running it through a custom subclass of
        a ConfigLoader.
        """

        basename = tmpdir.join("cyl")
        OutputGeometryFile = basename.strpath + ".gmy"
        OutputXmlFile = basename.strpath + ".xml"
        VoxelSizeMetres = 0.1

        rng = np.random.RandomState(randSeed)
        Axis = rng.normal(size=(3,))
        Axis /= np.sqrt(np.dot(Axis, Axis))

        LengthMetres = 1.32
        RadiusMetres = 0.74

        # generator = OutputGeneration.CylinderGenerator(
        #     OutputGeometryFile, OutputXmlFile,
        #     VoxelSizeMetres, Axis, LengthMetres,
        #     RadiusMetres)
        # generator.Execute()

        # checker = CylinderTestingGmyParser(OutputGeometryFile, VoxelSizeMetres,
        #                                    Axis, LengthMetres, RadiusMetres)
        # checker.Load()
        return

    pass


class BaseTestingGmyParser(ConfigLoader):
    def __init__(self, filename, VoxelSize):
        ConfigLoader.__init__(self, filename)
        self.VoxelSize = VoxelSize

    def OnEndHeader(self):
        assert self.Domain.VoxelSize == self.VoxelSize

    def OnEndSite(self, block, site):
        if site.IsFluid:
            assert self.IsInside(site.Position)
        else:
            assert not self.IsInside(site.Position)
        return

    def OnEndBlock(self, bIdx, bIjk):
        self.Domain.DeleteBlock(bIdx)
        return

    pass


class CylinderTestingGmyParser(BaseTestingGmyParser):
    def __init__(self, filename, VoxelSize, Axis, Length, Radius):
        BaseTestingGmyParser.__init__(self, filename, VoxelSize)
        self.Axis = Axis
        self.Length = Length
        self.Radius = Radius
        return

    def IsInside(self, x):
        xDOTn = np.dot(x, self.Axis)
        if xDOTn < -0.5 * self.Length or xDOTn > 0.5 * self.Length:
            return False
        perp = x - xDOTn * self.Axis
        if np.dot(perp, perp) > self.Radius**2:
            return False
        return True

    def OnEndSite(self, block, site):
        BaseTestingGmyParser.OnEndSite(self, block, site)
        assert site.IsEdge == site.WallNormalAvailable
        is_cylinder_end = np.any(
            site.IntersectionType == site.INLET_INTERSECTION
        ) or np.any(site.IntersectionType == site.OUTLET_INTERSECTION)
        if site.IsEdge and not is_cylinder_end:
            assert np.any(site.IntersectionType == site.WALL_INTERSECTION)
            axis_perpendicular_at_site = (
                site.Position - np.dot(site.Position, self.Axis) * self.Axis
            )
            axis_perpendicular_at_site /= norm(axis_perpendicular_at_site)
            """ (ticket #597) 0.02 is an arbitrary tolerance for how accurate 
            the wall normal estimates are.
            """
            assert (
                np.absolute(1 - np.dot(site.WallNormal, axis_perpendicular_at_site))
                < 0.02
            )


class CubeTestingGmyParser(BaseTestingGmyParser):
    def IsInside(self, position):
        result = all(component < 0.5 and component > -0.5 for component in position)
        return result


class CubeNormalsTestingGmyParser(CubeTestingGmyParser):

    ValidNormals = np.array([(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0)], dtype=float)

    def OnEndSite(self, block, site):
        CubeTestingGmyParser.OnEndSite(self, block, site)
        assert site.IsEdge == site.WallNormalAvailable
        is_cube_edge = (site.Index[0] in [1, 4]) and (site.Index[1] in [1, 4])
        if site.IsEdge and not is_cube_edge:
            assert np.any(site.IntersectionType == site.WALL_INTERSECTION)
            assert np.any(np.all(self.ValidNormals == site.WallNormal, axis=1))
