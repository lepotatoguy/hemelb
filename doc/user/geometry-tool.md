<!-- This file is part of HemeLB and is Copyright (C) -->
<!-- the HemeLB team and/or their institutions, as detailed in the -->
<!-- file AUTHORS. This software is provided under the terms of the -->
<!-- license in the file LICENSE. -->

# HemeLB geometry generation tool

This tool (and optional GUI) generate HemeLB input geometry files.
This was previously called the "setuptool" so you may see references
to that within the repository.


## Install

Build dependencies:

- Python >= 3.6
- Setuptools >= 21.1
- C++ compiler (C++20)
- CMake >= 3.13
- Pybind11
- scikit-build
- Boost (header only)
- VTK >= 9
- CGAL

Runtime dependencies:
- Python
- Numpy
- PyYAML
- VTK
- VMTK
- wxPython (only for the GUI)

Test dependencies:
- pytest
- hlb (NB: this must be compiled with the same version of numpy as
  used above)


## Conda install

VMTK is hard to build yourself but is easily installed via Conda, so
we present this process here.

### Virtual enviroment setup

We *strongly* recommend that you install into an isolated virtual
environment.

Create the environment for the tool and specify required
packages. They live in the `conda-forge` "channel". The
dependencies are recorded in the `conda-environment.yml` file.

```
conda env create --file conda-environment.yml
```

By default this will use `gmy-tool` as the name, but you can use
anything you wish by adding `--name PREFERED_NAME` to the command
above.


You then need to activate this for your shell session:

```
conda activate gmy-tool
```

### Install

With the environment above, install should be as simple as

```
pip install '.[gui]'
```

(If your pip version is less than 21.3, add the extra flag `--use-feature=in-tree-build`)

If you don't want the GUI, you can drop the `[gui]` extra
specification.

**Note for macOS**: On macOS GUI applications have to be linked
against some special framework. If you don't fix this up you will get
a message:

```
This program needs access to the screen. Please run with a
Framework build of python, and only when you are logged in
on the main display of your Mac.
```

To fix this, you have edit the shebang (`#!`) line in the launcher
script (`hlb-gmy-gui`) to point to an appropriately linked python
executable. This is typically `pythonw` but for conda it is part of
the `python.app` package. Delightfully, this is not in the typical
`$CONDA_PREFIX/bin` directory, instead its location is (2021)
`$CONDA_PREFIX/python.app/Contents/MacOS/python`.

We include a script to fix this for you until pip and scikit build
support doing this automatically:

```
python macos-fix-gui-launcher.py
```

## Install with custom VMTK

The hemelb-codes organisation on GitHub includes a project to build
VMTK  for Ubuntu: https://github.com/hemelb-codes/vmtk-build/

That will hopefully give you an idea for how to proceed. If you are
lucky you can just download the tarball. (Some Ubuntu packages will
have to be installed - see that repo)

## Test

Install pytest the usual way via `pip install pytest`.

The Conda VMTK package forces you to use an old version of numpy. To
ensure that the `hlb` package in `python-tools` is built with the same
one, you need to change to that directory, install Cython (`pip
install cython`) and then install the package with:

```
pip install --no-build-isolation .
```

Run the tests by invoking `py.test` in this directory.


## Run GUI

Ensure your environment is activated then run `hlb-gmy-gui`. There is
basic command line help available:

```
$ hlb-gmy-gui --help
usage: hlb-gmy-gui [-h] [--profile PATH] [--stl PATH] [--geometry PATH]
                   [--xml PATH]

Process an input STL file intosuitable input for HemeLB.

optional arguments:
  -h, --help       show this help message and exit
  --profile PATH   Load the profile to use from an existing file. Other
                   options givenoverride those inthe profile file.
  --stl PATH       The STL file to use as input
  --geometry PATH  Config output file
  --xml PATH       XML output file
```

The mesh preview stays empty until an STL file has loaded. If VTK
reports an STL reading or pipeline error after a file is selected,
check the file and the error instead of ignoring it.


## Run the command-line generator

`hlb-gmy-cli` does what the GUI's "Generate" button does, without a window.
It needs a profile, because inlets and outlets can only be defined there
(create one in the GUI and use "Save Profile"). Every other setting can be
overridden on the command line; `hlb-gmy-cli --help` lists them all.

```
hlb-gmy-cli PROFILE [--stl PATH] [--stl-units {m,mm,um}] [--seed X Y Z]
                    [--voxel METRES] [--timestep SECONDS] [--duration SECONDS]
                    [--geometry PATH] [--xml PATH]
```

### What a run does

1. **Load the profile** (`.pr2` YAML, or legacy `.pro`). Relative paths in it
   are resolved from the profile's folder. Setting the STL computes its
   average edge length, which becomes the voxel size if the profile has none.
2. **Apply overrides** from the command line. The profile file itself is not
   changed.
3. **Check the inputs.** The STL must exist, the voxel size must be
   positive, the seed point must be set, and the outputs must end in `.gmy`
   and `.xml` in folders that exist. Problems are reported together and the
   command exits with status 2. The GUI's Generate button runs the same
   checks and shows problems in a message box.
4. **Clip and cap the surface.** At each inlet and outlet the surface is cut
   by the iolet's plane and a sphere of the iolet's radius, the opening is
   capped and labelled with the iolet's id, and the surface piece closest
   to the seed point is kept.
5. **Voxelise.** The capped surface is scaled by the voxel size and every
   lattice site is classified as fluid or solid, in blocks of 8x8x8 sites.
   The domain leaves at least one solid site beyond the surface on each side.
6. **Warn about silent problems** (see "Things to watch"): an iolet that
   cannot reach the surface, or a seed point outside the capped surface.
   Warnings do not stop generation.
7. **Write the `.gmy`** and then the **`.xml`**, and print the setup time.

### Options

| Option | Profile field | Meaning |
| --- | --- | --- |
| `PROFILE` | | `.pr2` or `.pro` file to start from |
| `--stl PATH` | `StlFile` | Surface to voxelise. A new STL resets the voxel size to its average edge length unless `--voxel` is also given. |
| `--stl-units {m,mm,um}` | `StlFileUnitId` (0, 1, 2) | Units of the STL coordinates. The seed point and the profile's iolet centres and radii use the same units. |
| `--seed X Y Z` | `SeedPoint` | A point inside the fluid, in STL units. Used only to pick which surface piece to keep after each iolet cut. |
| `--voxel METRES` | `VoxelSize` (STL units) | Lattice spacing, given in metres. Halving it gives about 8 times the sites. |
| `--timestep SECONDS` | `TimeStepSeconds` | Written to the XML. Not adjusted when the voxel size changes. |
| `--duration SECONDS` | `DurationSeconds` | Number of steps in the XML is `round(duration / time step)`. |
| `--geometry PATH` | `OutputGeometryFile` | Geometry output. Relative to the current folder, not the profile's. |
| `--xml PATH` | `OutputXmlFile` | XML output. Relative to the current folder. It refers to the geometry by a path relative to itself. |

### What the XML contains

| Element | Source |
| --- | --- |
| `simulation/step_length`, `steps` | `TimeStepSeconds`, `round(DurationSeconds / TimeStepSeconds)` |
| `simulation/voxel_size`, `origin` | Voxel size and domain origin, in metres |
| `simulation/stresstype` | Always 1 |
| `geometry/datafile` | Path of the `.gmy`, relative to the XML |
| `inlets/inlet`, `outlets/outlet` | One per iolet: a `pressure`/`cosine` condition with `mean` = Pressure.x (mmHg), `amplitude` = Pressure.y (mmHg), `phase` = Pressure.z (rad), `period` fixed at 1 s; the iolet `normal`; its centre in metres as `position` |
| `visualisation` | Fixed defaults |
| `initialconditions` | Uniform pressure 0 mmHg |

There is no `<properties>` section, so HemeLB writes no field output until
you add one (see [XmlConfiguration.md](XmlConfiguration.md)). Inlet and
outlet types other than cosine pressure also have to be edited by hand.

### Checking the result

```
hlb-gmy-countsites output.gmy        # number of fluid sites
hlb-gmy-selfconsistent output.gmy    # checks the file's internal consistency
mpirun -n 4 hemelb -in output.xml -out results
```

### Things to watch

These were found by running the tool on the test profiles in the repository.

- **Seed point.** After clipping, the tool checks that the seed point is
  inside the capped surface and prints a warning if it is not (the GUI shows
  it in a message box). Generation still goes ahead, because on a simple
  vessel the result can be correct; on branched geometries the wrong piece
  may have been kept.
- **Iolet radius.** An iolet only cuts the vessel if the surface comes within
  its radius of its centre. If it does not, the tool warns that the iolet
  "does not reach the surface" and that end of the vessel stays closed (on
  the test vessel a too-small inlet changed the fluid site count from 6,803
  to 7,797). The check uses the nearest wall, so an off-centre iolet on a
  non-circular vessel can still cut only part of the cross-section without a
  warning; make iolet radii comfortably larger than the vessel.
- **Voxel size and units.** `--voxel` is in metres whatever the STL units.
  Reading a millimetre STL as metres (`--stl-units m`) with a fine voxel size
  makes the grid a thousand times too large; the writer then stops with
  "Geometry header is too large for the file writer".
- **Time step.** Changing the voxel size does not change the time step.
  Choose the time step for the new grid yourself.
- **Output folders** must already exist.

### Legacy `.pro` profiles

Convert a legacy `.pro` profile to `.pr2` with:

```
hlb-pro2pr2 old-profile.pro new-profile.pr2
```

If the output name is omitted, the new file is written next to the old one
with a `.pr2` extension. The converter can also be run as
`python -m HlbGmyTool.scripts.pro_to_pr2`.

Notes on legacy `.pro` files:

- A `.pro` file is a Python pickle. It is read with a restricted loader
  that only accepts the old `HemeLbSetupTool` classes. Any other object in
  the file is rejected, so opening a profile cannot run arbitrary code.
- Very old profiles store `Steps` and `Cycles` instead of times. They are
  converted assuming a 70 beats per minute pulse (period 60/70 s):
  `TimeStepSeconds = period / Steps` and `DurationSeconds = period * Cycles`.
  Both fields must be positive, and a profile may not mix the old and new
  timing fields.
- Paths to the STL, geometry and XML files are resolved relative to the
  profile's own folder.
- An example of the expected output is
  `Code/tests/pythontests/resources/poiseuille_flow_test.pr2`.


## Profile (.pr2) files

The geometry tool can store the the data it will use to generate a
geometry file. Saving this is highly recommended for reproducibility!

It's a YAML file which can be edited manually. Floating point values
are stored by default in hexadecimal to avoid precision loss
(https://docs.python.org/3/library/stdtypes.html#float.hex) but can be
set in decimal if more convenient. Integers (`3`), decimals (`0.5`,
`1.0e-5`) and exponents without a decimal point (`1e-5`, which YAML reads
as text) are all accepted.

| Field | Meaning |
| --- | --- |
| `StlFile` | Input surface |
| `StlFileUnitId` | Units of the STL: 0 = m, 1 = mm, 2 = µm |
| `VoxelSize` | Lattice spacing, in STL units |
| `SeedPoint` (`x`, `y`, `z`) | A point inside the fluid, in STL units |
| `Iolets` | List of inlets and outlets, each with `Type` (`Inlet` or `Outlet`), `Name`, `Centre`, `Normal` (pointing into the fluid), `Radius` (STL units) and `Pressure` (`x` mean mmHg, `y` amplitude mmHg, `z` phase rad) |
| `TimeStepSeconds`, `DurationSeconds` | Simulation time step and length |
| `OutputGeometryFile`, `OutputXmlFile` | Outputs, relative to the profile |

Paths in a profile are interpreted relative to that profile file's location.
