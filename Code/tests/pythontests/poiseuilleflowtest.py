#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

"""Generate a pipe, run low-Mach steady flow, and check its Poiseuille profile.

Needs hemelb, hlb-gmy-cli and the hlb Python tools. HEMELB_EXECUTABLE selects
an alternative solver; MPIRUN_FLAGS supplies additional MPI launcher flags.
Run with: python3 poiseuilleflowtest.py
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

import numpy as np
import yaml

from hlb.parsers.extraction import ExtractedProperty

RESOURCES = Path(__file__).resolve().parent / "resources"
STEPS = 6000
DT = 1e-4
PRESSURE_DIFF = 0.02 * 133.3223874  # Pa, matching the upstream cylinder regression
VISCOSITY = 4e-3  # default dynamic viscosity, Pa s
LENGTH = 0.06  # inlet-to-outlet separation, m


def prepare_inputs(directory):
    for name in ("poiseuille_flow_test.pr2", "poiseuille_flow_test.stl"):
        shutil.copy(RESOURCES / name, directory)
    profile = directory / "poiseuille_flow_test.pr2"
    state = yaml.safe_load(profile.read_text())
    # Open both ends: the original cutting disks are no wider than the pipe.
    for iolet in state["Iolets"]:
        iolet["Radius"] = 1.5
        iolet["Pressure"]["x"] = 0.02 if iolet["Type"] == "Inlet" else 0.0
    profile.write_text(yaml.safe_dump(state))
    subprocess.run(
        ["hlb-gmy-cli", profile.name],
        cwd=directory,
        check=True,
        stdout=subprocess.DEVNULL,
        timeout=120,
    )
    xml_path = directory / "poiseuille_flow_test.xml"
    tree = ET.parse(xml_path)
    root = tree.getroot()
    root.find("simulation/steps").set("value", str(STEPS))
    # The former 5 microsecond step put tau very close to 0.5 and the run aborted.
    root.find("simulation/step_length").set("value", str(DT))
    properties = ET.SubElement(root, "properties")
    for filename, field_type, field_name in (
        ("velocity_40mm_in.dat", "velocity", "velocity_40mm_in"),
        ("shear_stress_40mm_in.dat", "shearstress", "shear_stress_40mm_in"),
    ):
        output = ET.SubElement(
            properties,
            "propertyoutput",
            file=filename,
            period="1",
            start=str(STEPS - 1),
            stop=str(STEPS - 1),
        )
        if field_type == "shearstress":
            # Wall shear stress is undefined on interior fluid sites.
            ET.SubElement(output, "geometry", type="surface")
        else:
            line = ET.SubElement(output, "geometry", type="line")
            ET.SubElement(line, "point", value="(-0.75e-3,0.0,10e-3)", units="m")
            ET.SubElement(line, "point", value="(0.75e-3,0.0,10e-3)", units="m")
        ET.SubElement(output, "field", type=field_type, name=field_name)
    tree.write(xml_path)


class TestPoiseuilleFlowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        hemelb = os.environ.get("HEMELB_EXECUTABLE") or shutil.which("hemelb")
        if hemelb is None:
            raise RuntimeError("hemelb not found; set HEMELB_EXECUTABLE")
        cls.workspace = tempfile.TemporaryDirectory(suffix="_HemeLB_RegressionTest")
        cls.addClassCleanup(cls.workspace.cleanup)
        cls.directory = Path(cls.workspace.name)
        prepare_inputs(cls.directory)
        command = (
            ["mpirun"]
            + shlex.split(os.environ.get("MPIRUN_FLAGS", ""))
            + [
                "-np",
                "4",
                str(Path(hemelb).resolve()),
                "-in",
                "poiseuille_flow_test.xml",
                "-out",
                "results",
            ]
        )
        subprocess.run(command, cwd=cls.directory, check=True, timeout=600)

    def sample(self, filename):
        prop = ExtractedProperty(str(self.directory / "results/Extracted" / filename))
        self.assertGreater(prop.siteCount, 0)
        self.assertEqual(list(prop.times), [STEPS - 1])
        return prop.GetByTimeStep(STEPS - 1)

    def test_velocity_profile(self):
        sites = self.sample("velocity_40mm_in.dat")
        velocity = sites.velocity_40mm_in[:, 2]
        # Simple bounce-back places the wall half a voxel beyond the outermost
        # fluid site. Use that effective radius, as in the original regression.
        dx = ExtractedProperty(
            str(self.directory / "results/Extracted/velocity_40mm_in.dat")
        ).voxelSizeMetres
        radius = (np.ptp(sites.position[:, 0]) + dx) / 2
        centre = (np.max(sites.position[:, 0]) + np.min(sites.position[:, 0])) / 2
        radial_distance_squared = (sites.position[:, 0] - centre) ** 2 + sites.position[
            :, 1
        ] ** 2
        analytical = (
            PRESSURE_DIFF
            / LENGTH
            / (4 * VISCOSITY)
            * (radius**2 - radial_distance_squared)
        )
        self.assertTrue(np.all(np.isfinite(velocity)))
        self.assertTrue(np.all(analytical > 0))
        self.assertGreater(float(np.max(velocity)), 0)
        relative_error = np.linalg.norm(velocity - analytical) / np.linalg.norm(
            analytical
        )
        print(f"Poiseuille relative L2 velocity error: {relative_error:.17g}")
        # The supplied pipe has about nine voxels across its diameter. Allow
        # its coarse bounce-back wall discretisation error, relative to the
        # analytical flow, so a zero or undeveloped profile cannot pass.
        self.assertLess(relative_error, 0.1)

    def test_shear_stress_is_finite_and_nonzero(self):
        stress = self.sample("shear_stress_40mm_in.dat").shear_stress_40mm_in
        self.assertTrue(np.all(np.isfinite(stress)))
        self.assertGreater(float(np.max(np.abs(stress))), 0)


if __name__ == "__main__":
    unittest.main()
