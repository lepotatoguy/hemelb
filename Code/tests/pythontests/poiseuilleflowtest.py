#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

"""Poiseuille flow regression test.

Generates a straight pipe from resources/poiseuille_flow_test.pr2 with
hlb-gmy-cli, runs HemeLB with a 16 mmHg pressure difference, and compares
the velocity across the pipe with the analytical Poiseuille profile.

Needs hemelb, hlb-gmy-cli (geometry tool) and the hlb Python tools. Set
HEMELB_EXECUTABLE to choose the hemelb binary (default: hemelb on PATH) and
MPIRUN_FLAGS for extra launcher flags (for example --oversubscribe).

Run with:  python3 poiseuilleflowtest.py
"""

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

import yaml

from hlb.parsers.extraction import ExtractedProperty

try:
    import matplotlib

    matplotlib.use("Agg")  # write the plot to a file; no window needed
    from matplotlib import pyplot as plt
except ImportError:  # plotting is optional
    plt = None

RESOURCES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "resources")


def prepare_inputs(directory):
    """Generate the geometry and XML, then add the run length and outputs.

    The run length and the two line extractions are those of
    resources/poiseuille_flow_test_master.xml, written in the current XML format.
    """
    for name in ("poiseuille_flow_test.pr2", "poiseuille_flow_test.stl"):
        shutil.copy(os.path.join(RESOURCES, name), directory)
    # The sample profile's iolets (radius 0.75 mm) are no wider than the pipe,
    # so they do not open its ends and no flow develops. Use 1.5 mm.
    profile = os.path.join(directory, "poiseuille_flow_test.pr2")
    with open(profile) as stream:
        state = yaml.safe_load(stream)
    for iolet in state["Iolets"]:
        iolet["Radius"] = 1.5
    with open(profile, "w") as stream:
        yaml.safe_dump(state, stream)
    subprocess.run(
        ["hlb-gmy-cli", "poiseuille_flow_test.pr2"],
        cwd=directory,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    xml_path = os.path.join(directory, "poiseuille_flow_test.xml")
    tree = ET.parse(xml_path)
    root = tree.getroot()
    root.find("simulation/steps").set("value", "6000")
    root.find("simulation/step_length").set("value", "5e-06")
    properties = ET.SubElement(root, "properties")
    for filename, field_type, field_name in (
        ("velocity_40mm_in.dat", "velocity", "velocity_40mm_in"),
        ("shear_stress_40mm_in.dat", "shearstress", "shear_stress_40mm_in"),
    ):
        output = ET.SubElement(
            properties, "propertyoutput", file=filename, period="6000"
        )
        line = ET.SubElement(output, "geometry", type="line")
        ET.SubElement(line, "point", value="(-0.75e-3,0.0,10e-3)", units="m")
        ET.SubElement(line, "point", value="(0.75e-3,0.0,10e-3)", units="m")
        ET.SubElement(output, "field", type=field_type, name=field_name)
    tree.write(xml_path)


class TestPoiseuilleFlowTest(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.viscosity = 4e-3  # HemeLB's default dynamic viscosity (Pa s)
        self.pressure_diff = 16 * 133.3223874  # 16mmHg in Pa
        self.pipe_length = 6e-2  # 60mm in m

        self.temp_dir = tempfile.mkdtemp("_HemeLB_RegressionTest")
        hemelb = os.environ.get("HEMELB_EXECUTABLE") or shutil.which("hemelb")
        if hemelb is None:
            raise unittest.SkipTest("hemelb not found; set HEMELB_EXECUTABLE")
        shutil.copy(hemelb, os.path.join(self.temp_dir, "hemelb"))
        prepare_inputs(self.temp_dir)
        os.chdir(self.temp_dir)

    def test_run_simulation_and_check_output_created(self):
        command = (
            ["mpirun"]
            + shlex.split(os.environ.get("MPIRUN_FLAGS", ""))
            + ["-np", "4", "./hemelb", "-in", "poiseuille_flow_test.xml", "-out", "results"]
        )
        try:
            subprocess.call(command)
        except OSError as e:
            print("Call to HemeLB failed:", e, file=sys.stderr)

        # Make sure the .dat files have been created
        self.assertTrue(os.path.isfile("results/Extracted/velocity_40mm_in.dat"))
        self.assertTrue(os.path.isfile("results/Extracted/shear_stress_40mm_in.dat"))

    def compute_analytical_velocity(self, site_data):
        x_coord = site_data[0]
        dist_centre = abs(self.centre - x_coord)
        analytical_vel = (
            (1 / (4 * self.viscosity))
            * (self.pressure_diff / self.pipe_length)
            * (pow(self.radius, 2) - pow(dist_centre, 2))
        )
        return analytical_vel

    def test_velocity_profile(self):
        filename = "results/Extracted/velocity_40mm_in.dat"
        propFile = ExtractedProperty(filename)

        # Print some basic information about the properties extracted
        print('# Dump of file "{}"'.format(filename))
        print("# File has {} sites.".format(propFile.siteCount))
        print("# File has {} fields:".format(propFile.fieldCount))
        for name, xdrType, memType, length, offset in propFile._fieldSpec:
            print('#     "{0}", length {1}'.format(name, length))
        print("# Geometry origin = {} m".format(propFile.originMetres))
        print("# Voxel size = {} m".format(propFile.voxelSizeMetres))

        header = "# " + ", ".join(
            name for name, xdrType, memType, length, offset in propFile._fieldSpec
        )
        print(header)

        # Property extraction files could have info for more than one time step depending on the frequency requested
        for t in propFile.times:
            sites_along_line = propFile.GetByTimeStep(t)
            print("# Timestep {:d}".format(t))

            # Create a list of tuples (x_coordinate, z_velocity) for all the sites along the line
            coord_vel_along_line = [
                (site.position[0], site.velocity_40mm_in[2]) for site in sites_along_line
            ]
            coord_vel_along_line.sort()  # Sorting the lists helps with plotting

            # Work out pipe radius and z coordinate of the axis
            max_coord = max(coord_vel_along_line)[0]
            min_coord = min(coord_vel_along_line)[0]
            self.radius = (
                (max_coord - min_coord) / 2 + propFile.voxelSizeMetres / 2
            )  # With bounce back, the actual wall is half a lattice site away
            self.centre = (max_coord + min_coord) / 2

            # Compute Poiseuille flow analytical solution along the line
            analytical_solutions = list(
                map(self.compute_analytical_velocity, coord_vel_along_line)
            )

            # Plot analytical and simulated velocity profiles
            [coords, vels] = zip(*coord_vel_along_line)
            if plt is not None:
                plt.plot(coords, vels, "o-", label="HemeLB")
                plt.plot(coords, analytical_solutions, "o-", label="Analytical")
                plt.xlabel("Lattice site radius")
                plt.ylabel("Velocity along the z axis")
                plt.legend()
                plt.savefig("poiseuille_velocity_profile.png")

            # Compare simulation results with analytical solution
            for analytical, (z_coord, computed) in zip(
                analytical_solutions, coord_vel_along_line
            ):
                self.assertAlmostEqual(
                    analytical,
                    computed,
                    delta=1e-3,
                    msg="Velocity {0} differs from analytical solution {1} at site with z coordinate {2}".format(
                        computed, analytical, z_coord
                    ),
                )

    def test_shear_stress_profile(self):
        pass


if __name__ == "__main__":
    unittest.main()
