# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from argparse import ArgumentParser, RawDescriptionHelpFormatter
import os

# Index of each unit in Profile._UnitChoices (the profile's StlFileUnitId)
STL_UNIT_IDS = {"m": 0, "mm": 1, "um": 2}

parser = ArgumentParser(
    description="""\
Generate a HemeLB geometry (.gmy) and simulation configuration (.xml) from a
profile, without the GUI. This does what "Generate" does in hlb-gmy-gui:

  1. Load the profile (.pr2 YAML, or legacy .pro). Relative paths in it are
     resolved from the profile's folder.
  2. Apply any options below; they override the profile for this run only
     (the profile file is not changed).
  3. Check the inputs: the STL exists, the voxel size is positive, the seed
     point is set, and the outputs end in .gmy/.xml in existing folders.
  4. Clip the STL surface with a plane and sphere at each inlet and outlet,
     cap the openings, and keep the surface piece closest to the seed point.
  5. Voxelise the closed surface into 8x8x8-site blocks and write the .gmy.
  6. Write the .xml: time step, number of steps (duration / time step),
     voxel size and origin in metres, a cosine pressure condition for each
     inlet and outlet (mean, amplitude and phase from the profile, phase in
     radians, period from --period), and default visualisation and initial
     conditions. Other boundary types need the XML to be edited.

Inlets and outlets (centre, normal, radius, pressure) can only be set in the
profile; create them with hlb-gmy-gui and "Save Profile".""",
    formatter_class=RawDescriptionHelpFormatter,
    epilog="""\
Units: --stl-units sets how STL coordinates are read. The seed point and the
profile's iolet centres and radii are in those units; --voxel is in metres.

Examples:
  hlb-gmy-cli profile.pr2
  hlb-gmy-cli profile.pr2 --geometry out/run1.gmy --xml out/run1.xml
  hlb-gmy-cli profile.pr2 --voxel 5e-5              # finer grid, about 8x sites
  hlb-gmy-cli profile.pr2 --stl vessel.stl --stl-units mm --voxel 1e-4
  hlb-gmy-cli profile.pr2 --timestep 1e-5 --duration 2.0
  hlb-gmy-cli profile.pr2 --period 0.857              # 70 beats per minute

Check the result with hlb-gmy-countsites FILE.gmy and hlb-gmy-selfconsistent
FILE.gmy, then run: mpirun -n N hemelb -in FILE.xml -out RESULTS_DIR
The generated XML has no <properties> section, so HemeLB writes no field
output until one is added (see doc/user/XmlConfiguration.md).""",
)

parser.add_argument(
    "profile",
    nargs=1,
    help="Profile to use (.pr2, or legacy .pro).",
    metavar="PROFILE",
)

inputs = parser.add_argument_group("input surface")
inputs.add_argument(
    "--stl",
    default=None,
    dest="StlFile",
    help="STL surface to voxelise. Loading a new STL resets the voxel size to "
    "its average edge length unless --voxel is also given.",
    metavar="PATH",
)
inputs.add_argument(
    "--stl-units",
    default=None,
    choices=sorted(STL_UNIT_IDS),
    dest="stl_units",
    help="Units of the STL coordinates (profile field StlFileUnitId).",
)
inputs.add_argument(
    "--seed",
    default=None,
    type=float,
    nargs=3,
    dest="seed",
    help="A point inside the fluid, in STL units. After each iolet cut, the "
    "surface piece closest to this point is kept.",
    metavar=("X", "Y", "Z"),
)

grid = parser.add_argument_group("grid and time")
grid.add_argument(
    "--voxel",
    default=None,
    type=float,
    dest="VoxelSizeMetres",
    help="Voxel (lattice) size in metres. Halving it gives about 8x the sites.",
    metavar="METRES",
)
grid.add_argument(
    "--timestep",
    default=None,
    type=float,
    dest="TimeStepSeconds",
    help="Time step in seconds, written to the XML. It is not adjusted when "
    "the voxel size changes.",
    metavar="SECONDS",
)
grid.add_argument(
    "--period",
    default=None,
    type=float,
    dest="PulsePeriodSeconds",
    help="Period of the cosine pressure at every inlet and outlet, in seconds "
    "(profile field PulsePeriodSeconds; 1 s if not set).",
    metavar="SECONDS",
)
grid.add_argument(
    "--duration",
    default=None,
    type=float,
    dest="DurationSeconds",
    help="Simulated time in seconds; steps = round(duration / time step).",
    metavar="SECONDS",
)

outputs = parser.add_argument_group("outputs")
outputs.add_argument(
    "--geometry",
    default=None,
    dest="OutputGeometryFile",
    help="Geometry file to write (.gmy). Relative to the current folder.",
    metavar="PATH",
)
outputs.add_argument(
    "--xml",
    default=None,
    dest="OutputXmlFile",
    help="XML file to write (.xml). Relative to the current folder. It refers "
    "to the geometry by a path relative to itself.",
    metavar="PATH",
)


def check_profile(p):
    """Return a list of problems that would stop generation.

    Without these checks a missing STL crashes the C++ generator and a zero
    voxel size ends in a ZeroDivisionError.
    """
    problems = []
    if not p.HaveValidStlFile:
        problems.append("STL file %r does not exist or is not a .stl file" % p.StlFile)
    if not p.VoxelSize > 0:
        problems.append("voxel size must be positive, got %r" % p.VoxelSize)
    if not p.HaveValidSeedPoint:
        problems.append("seed point must have three finite coordinates")
    for label, ext, path, valid in (
        ("geometry", ".gmy", p.OutputGeometryFile, p.HaveValidOutputGeometryFile),
        ("XML", ".xml", p.OutputXmlFile, p.HaveValidOutputXmlFile),
    ):
        if not valid:
            problems.append("%s output %r must end in %s" % (label, path, ext))
        elif not os.path.isdir(os.path.dirname(os.path.abspath(path))):
            problems.append("folder for %s output %r does not exist" % (label, path))
    return problems


def main():
    args = vars(parser.parse_args())
    # argparse puts the positional argument in a list
    profile = args.pop("profile")[0]
    stl_units = args.pop("stl_units")
    seed = args.pop("seed")

    # Import our module late to give erroneous args a chance to be caught
    # quickly
    from ..Model.Profile import Profile
    from ..Model.Vector import Vector

    p = Profile()
    p.LoadFromFile(profile)

    if stl_units is not None:
        args["StlFileUnitId"] = STL_UNIT_IDS[stl_units]
    if seed is not None:
        args["SeedPoint"] = Vector(*seed)

    # Override any values given on the command line.
    p.UpdateAttributesBasedOnCmdLineArgs(args)

    problems = check_profile(p)
    if problems:
        parser.error("cannot generate from %s:\n  " % profile + "\n  ".join(problems))

    p.Generate()
