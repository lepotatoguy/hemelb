# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import os
import sys

from skbuild import setup

if sys.platform == "darwin":
    # Python thinks it's so smart and sets the
    # MACOSX_DEPLOYMENT_TARGET environment variable that messes around
    # with what features of the compiler and C++ std lib are
    # available. Set this to use your current one.
    import platform

    release, versioninfo, machine = platform.mac_ver()
    os.environ["MACOSX_DEPLOYMENT_TARGET"] = release


def vmtk_requirement():
    """The VMTK requirement to declare to pip.

    VMTK 1.5 is only published on conda-forge, not PyPI, so pip cannot
    install it and cannot see a conda-installed copy either. If VMTK is
    already installed (importable, or recorded in the active conda
    environment), leave it out so pip does not look for it on PyPI.
    Otherwise keep it, so pip still stops rather than install a tool that
    cannot run.
    """
    import glob
    import importlib.util

    if importlib.util.find_spec("vmtk") is not None:
        return []
    # pip builds in an isolated environment where the conda packages are not
    # importable, so also check the conda environment's package records.
    prefix = os.environ.get("CONDA_PREFIX")
    if prefix and glob.glob(os.path.join(prefix, "conda-meta", "vmtk-1.5*.json")):
        return []
    print(
        "VMTK 1.5 not found. It is only available from conda-forge: "
        "conda env create -f geometry-tool/conda-environment.yml",
        file=sys.stderr,
    )
    return ["vmtk ~= 1.5"]


setup(
    name="HlbGmyTool",
    version="1.2",
    author="Rupert Nash",
    author_email="r.nash@epcc.ed.ac.uk",
    packages=[
        "HlbGmyTool",
        "HlbGmyTool.Bindings",
        "HlbGmyTool.Util",
        "HlbGmyTool.Model",
        "HlbGmyTool.View",
        "HlbGmyTool.Controller",
        "HlbGmyTool.scripts",
    ],
    entry_points={
        "console_scripts": [
            "hlb-gmy-cli=HlbGmyTool.scripts.cli:main",
            "hlb-config2gmy=HlbGmyTool.scripts.config_to_geometry:main",
            "hlb-pro2pr2=HlbGmyTool.scripts.pro_to_pr2:main",
        ],
        "gui_scripts": [
            "hlb-gmy-gui=HlbGmyTool.scripts.gui:main[gui]",
        ],
    },
    # VMTK 1.5, which the tool needs, is published for Python 3.8 to 3.11.
    python_requires=">=3.8,<3.12",
    install_requires=[
        "pyyaml",
        # Numpy >= 1.20 requires python 3.7; VMTK conda binaries are 3.6 only
        "numpy < 1.20; python_version < '3.7'",
        "numpy; python_version >= '3.7'",
        "vtk ~= 9.0",
    ]
    + vmtk_requirement(),
    extras_require={
        "gui": ["wxPython"],
    },
)
