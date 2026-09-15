# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

from argparse import ArgumentParser, RawDescriptionHelpFormatter

# Parse command line arguments
parser = ArgumentParser(
    description="Generate HemeLB geometry and XML files from a profile.",
    formatter_class=RawDescriptionHelpFormatter,
    epilog="""Examples:
  hlb-gmy-cli profile.pr2
  hlb-gmy-cli profile.pr2 --geometry output.gmy --xml output.xml
  hlb-gmy-cli profile.pr2 --voxel 5e-5

The --geometry, --xml, and --voxel options override values in the profile.
--voxel is specified in metres. Legacy .pro profiles are accepted, but .pr2
profiles are recommended for reproducible runs.""",
)

parser.add_argument(
    "profile",
    nargs=1,
    help="Profile to use (.pr2, or legacy .pro). Command-line options override it.",
    metavar="PATH",
)

parser.add_argument(
    "--geometry",
    default=None,
    dest="OutputGeometryFile",
    help="Output geometry file (.gmy). Overrides the profile value.",
    metavar="PATH",
)
parser.add_argument(
    "--xml",
    default=None,
    dest="OutputXmlFile",
    help="Output XML file. Overrides the profile value.",
    metavar="PATH",
)
parser.add_argument(
    "--voxel",
    default=None,
    type=float,
    dest="VoxelSizeMetres",
    help="Voxel size in metres. Overrides the profile value.",
    metavar="FLOAT",
)


def main():
    # Parse
    args = parser.parse_args()
    # Separate the profile argument (argparse puts it in a list)
    profile = args.profile[0]
    del args.profile

    # Import our module late to give erroneous args a chance to be caught
    # quickly
    from ..Model.Profile import Profile

    p = Profile()
    p.LoadFromFile(profile)

    # override any keys that have been set on cmdline.
    p.UpdateAttributesBasedOnCmdLineArgs(vars(args))

    p.Generate()
