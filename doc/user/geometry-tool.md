<!-- This file is part of HemeLB and is Copyright (C) -->
<!-- the HemeLB team and/or their institutions, as detailed in the -->
<!-- file AUTHORS. This software is provided under the terms of the -->
<!-- license in the file LICENSE. -->

# HemeLB geometry generation tool

This tool turns a closed STL surface of a vessel into the two input files
HemeLB needs: the voxelised geometry (`.gmy`) and a configuration (`.xml`).
It has a GUI, `hlb-gmy-gui`, for placing inlets, outlets and the seed point
by eye, and a command-line version, `hlb-gmy-cli`, for scripts and for
repeating a setup saved in a profile (`.pr2`). It used to be called the
"setuptool", so you may see that name in older files.

## Install

The easiest way is the install script, which installs the geometry tool and
the Python tools into a conda environment called `gmy-tool`
([Installing HemeLB](install.md)):

```sh
Scripts/install_hemelb.sh
conda activate gmy-tool
hlb-gmy-cli --help
```

### Installing the geometry tool manually

The tool needs VMTK 1.5, which is easiest to get from conda-forge. VMTK 1.5
is only published for Linux (x86_64) and Intel macOS with Python 3.8 to
3.11, so the tool supports those Python versions. The exact tested
environment is in `geometry-tool/conda-lock/` (every package pinned); the
main packages and their versions are also in
`geometry-tool/conda-environment.yml` and in the `INSTALL` file. You also
need a C++20 compiler (Xcode Command Line Tools on macOS, `build-essential`
on Ubuntu).

Run from the repository root and choose one platform lock. On Apple Silicon,
install Rosetta 2 first if it is unavailable, and create the Intel environment
with `CONDA_SUBDIR=osx-64`:

```sh
# macOS (Intel or Apple Silicon)
CONDA_SUBDIR=osx-64 conda create -n gmy-tool --file geometry-tool/conda-lock/osx-64.txt
conda activate gmy-tool
conda config --env --set subdir osx-64
```

On Linux x86_64 instead:

```sh
conda create -n gmy-tool --file geometry-tool/conda-lock/linux-64.txt
conda activate gmy-tool
```

Then install both packages against that environment's locked dependencies:

```sh
export CMAKE_PREFIX_PATH="$CONDA_PREFIX"
python -m pip install --no-deps --no-build-isolation ./python-tools
python -m pip install --no-deps --no-build-isolation ./geometry-tool
```

`--no-deps` keeps pip from replacing conda packages. `--no-build-isolation`
builds against the environment's NumPy, Cython, and build tools. This matches
the install script. An isolated pip build instead uses the pins in
`geometry-tool/pyproject.toml`.

The tool's `setup.py` recognizes VMTK installed by conda. Its CMake searches
the environment for CGAL, Boost, and VTK before system copies. The CGAL 5.6
header workaround is applied to a build-local copy, leaving the installed
header unchanged. Explicit locks are platform-specific; another platform needs
its own available packages and validation rather than either existing lock.

**macOS GUI.** On macOS a GUI program must run with a "framework" build of
Python, otherwise `hlb-gmy-gui` stops with "This program needs access to the
screen". Fix the launcher once after installing:

```sh
# python.app is already included in the macOS lock
python geometry-tool/macos-fix-gui-launcher.py
```

**Without conda.** The geometry-tool CI installs VMTK through
[hemelb-codes/vmtk-build](https://github.com/hemelb-codes/vmtk-build). See the
[workflow](../../.github/workflows/gmy-tool.yml) for that separate setup.
Do not combine native solver dependencies with an Intel geometry environment
on Apple Silicon.

## Test

With the environment active:

```sh
cd geometry-tool/tests
pytest
```

Run the tests from `tests/`, not from `geometry-tool/`: from there Python
would import the source folder, which lacks the compiled extension. The GUI
tests are skipped if wxPython is not installed, and window layout tests need
a display. On macOS, run them with the framework interpreter:

```sh
"$CONDA_PREFIX/python.app/Contents/MacOS/python" -m pytest
```

The [developer notes](../dev/README.md#how-to-run-the-tests) list all test suites.

## Run GUI

Activate the environment, then run `hlb-gmy-gui`. Use `--help` to list startup
options. A saved profile can be reopened directly:

```sh
hlb-gmy-gui --profile vessel.pr2
```

| Option | Purpose |
| :--- | :--- |
| `--profile PATH` | Load a saved profile |
| `--stl PATH` | Select the input surface |
| `--geometry PATH` | Set the GMY output path |
| `--xml PATH` | Set the XML output path |

The tool opens with room for the editor and preview. The tools panel scrolls
when the window is too short to show the output controls. **Fit** frames the
geometry; **X**, **Y**, and **Z** switch the view immediately.

A typical session:

1. **Choose** the STL (or **Open Profile**, which accepts `.pr2` and legacy
   `.pro` files, to continue earlier work) and set
   its units. The voxel size is set to the surface's average edge length;
   change it, or press **Reset** to go back to that value.
2. **Add Inlet** / **Add Outlet**, then **Place** each one on the surface and
   adjust its centre, normal (pointing into the fluid) and radius. Pressure
   holds the cosine boundary condition: `x` is the mean and `y` the amplitude
   (mmHg), `z` the phase in radians. The label under it shows the resulting
   equation.
3. Use **Place** to pick a seed position on the visible surface, then **Finish**.
   Adjust its coordinates to put the seed strictly inside the fluid volume.
   Surface picking alone does not select an interior point. For the bundled
   cylinder, the seed is `(0, 0, 0)` in millimetres.
4. Choose the output `.gmy` and `.xml` files; **Generate** becomes available
   when all inputs are valid. Problems and warnings are shown in a message
   box.
5. **Save Profile** so you can repeat or script the run with `hlb-gmy-cli`.

The **DEBUG** button, which opens the Python debugger, only appears when
`hlb-gmy-gui` is started from a terminal, since the debugger reads commands
from there.

The mesh preview stays empty until an STL file has loaded. If VTK
reports an STL reading or pipeline error after a file is selected,
check the file and the error instead of ignoring it.

See the [GUI validation record](../dev/geometry-gui-validation.md) for the
tested environment, controls and generated-case checks.


## Preparing the surface: closed or open

The STL can be **closed** (sealed at the vessel ends, like a can) or **open**
(ends left open, like a pipe). Both give the same geometry, because the
tool closes open ends itself:

1. Every hole in the surface is filled with a flat triangulated cap, marked
   as **wall**.
2. Each inlet and outlet then removes the part of the surface behind its
   plane and inside its radius, and caps that opening as the inlet or
   outlet.
3. The piece of surface closest to the seed point is kept and voxelised.

Tested on a 10 mm long, 1 mm radius tube, with iolets of radius 1.5 mm and
a 0.1 mm voxel size:

| Iolet position | Closed tube | Open tube |
| --- | --- | --- |
| Inside the vessel, 0.5 mm from each end | 30,968 fluid sites, 316 inlet and 316 outlet sites | identical |
| Exactly at the ends | 31,600 fluid sites, 316 inlet and 316 outlet sites | identical |
| 0.5 mm beyond the ends | 31,600 fluid sites, **no inlet or outlet sites** | identical |

What this means in practice:

- **Put an iolet on every end where flow enters or leaves.** An open end
  without an iolet is filled as wall, so that branch becomes a dead end,
  without any warning.
- **Place each iolet on the vessel end or slightly inside it**, with its
  normal pointing into the fluid. An iolet beyond the end of the surface
  opens nothing; the tool warns that it "lies outside the vessel".
- **Make the iolet radius larger than the vessel.** An iolet that cannot
  reach the wall cuts nothing; the tool warns that it "does not reach the
  surface".
- Automatic caps are flat triangulations of each opening. An iolet placed
  slightly inside the vessel removes the automatic cap at that end (the
  "inside" row above), which avoids relying on it for irregular or
  non-planar openings (not tested here).
- Holes elsewhere in the wall are filled the same way, as wall. Check the
  generated geometry if the surface has gaps.

## Run the command-line generator

`hlb-gmy-cli` does what the GUI's "Generate" button does, without a window.
Save a `.pr2` profile in the GUI, then run:

```sh
hlb-gmy-cli example.pr2
```

The profile supplies the input STL and the output GMY/XML filenames. Keep the
STL beside the profile when `StlFile` contains just its filename. If it is
missing, the command reports the expected STL path and stops.

Command-line options can override profile settings for a single run;
`hlb-gmy-cli --help` lists them all.

```
hlb-gmy-cli PROFILE [--stl PATH] [--stl-units {m,mm,um}] [--seed X Y Z]
                    [--voxel METRES] [--timestep SECONDS] [--period SECONDS]
                    [--duration SECONDS] [--geometry PATH] [--xml PATH]
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
   cannot reach the surface or lies beyond the end of the vessel, or a seed
   point outside the capped surface.
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
| `--period SECONDS` | `PulsePeriodSeconds` | Period of the cosine pressure at every inlet and outlet; 1 s if not set. |
| `--duration SECONDS` | `DurationSeconds` | Number of steps in the XML is `round(duration / time step)`. |
| `--geometry PATH` | `OutputGeometryFile` | Geometry output. Relative to the current folder, not the profile's. |
| `--xml PATH` | `OutputXmlFile` | XML output. Relative to the current folder. It refers to the geometry by a path relative to itself. |

### What the XML contains

This branch writes XML version 6. The GUI and profile retain mmHg pressure
inputs; the writer converts them to Pa in XML. Reinstall the geometry package
after switching branches so the installed writer matches this reference.

| Element | Source |
| --- | --- |
| `simulation/step_length`, `steps` | `TimeStepSeconds`, `round(DurationSeconds / TimeStepSeconds)` |
| `simulation/voxel_size`, `origin` | Voxel size and domain origin, in metres |
| `geometry/datafile` | Path of the `.gmy`, relative to the XML |
| `inlets/inlet`, `outlets/outlet` | One per iolet: a `pressure`/`cosine` condition with `mean` = Pressure.x × 133.3223874 (Pa), `amplitude` = Pressure.y × 133.3223874 (Pa), `phase` = Pressure.z (rad), `period` = `PulsePeriodSeconds` (1 s if not set); the iolet `normal`; its centre in metres as `position` |
| `initialconditions` | Uniform pressure 0 Pa |

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
- **Iolet position.** An iolet placed beyond the end of the surface opens
  nothing and leaves that end closed; the tool warns that it "lies outside
  the vessel". See "Preparing the surface" above.
- **Iolet radius.** An iolet only cuts the vessel if the surface comes within
  its radius of its centre. If it does not, the tool warns that the iolet
  "does not reach the surface" and that end of the vessel stays closed (on
  the test vessel a too-small inlet changed the fluid site count from 6,803
  to 7,797). The check uses the nearest wall, so an off-centre iolet on a
  non-circular vessel can still cut only part of the cross-section without a
  warning; make iolet radii comfortably larger than the vessel. An iolet
  whose radius only just exceeds the distance to the wall (more than 0.9 of
  its radius) gets an "only just reaches the surface" warning: the Poiseuille
  sample profile in `Code/tests/pythontests/resources` (radius 0.75 mm in a
  pipe of the same width) produced a pipe with no inlet or outlet sites.
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

`hlb-config2gmy INPUT OUTPUT` does the same conversion (it calls the same
code) but always needs both file names. It is kept for older scripts.

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


## Regenerate the case from STL (optional)

The copied `first-run.stl` contains a cylinder in millimetres. Its matching
`first-run.pr2` sets the voxel size, seed point, inlet/outlet clipping planes,
and pressure conditions. With `gmy-tool` active, run in the first-run folder:

```sh
hlb-gmy-cli first-run.pr2
hlb-gmy-countsites first-run.gmy
hlb-gmy-selfconsistent first-run.gmy
```

The profile names the outputs `first-run.gmy` and `generated.xml`. Regeneration
replaces `first-run.gmy` in the working folder and writes a new XML with the
geometry's computed origin.

### Add output requests to generated XML

Generated XML has no field output; copy the ready example's output requests
into it:

```sh
python - <<'PYCODE'
import copy
import xml.etree.ElementTree as ET

tree = ET.parse("generated.xml")
example = ET.parse("first-run.xml").getroot()
tree.getroot().append(copy.deepcopy(example.find("properties")))
tree.getroot().find("simulation").append(
    copy.deepcopy(example.find("simulation/checkpoint")))
tree.write("generated.xml", encoding="utf-8", xml_declaration=True)
PYCODE
```

Optionally check the generated XML with `hemelb-confcheck generated.xml`, then
start the simulation and convert its output:

```sh
mpirun -n 2 hemelb -in generated.xml -out generated-results
hlb-dump-extracted-properties generated-results/Extracted/whole.xtr generated-whole.csv
```

This completes STL/profile to GMY/XML to simulation to field output. Use
`generated.xml` for the regenerated run, especially after changing voxel size
or iolet settings. Re-run the geometry generator before adding `<properties>` and the checkpoint
request again, so neither section is duplicated.

To edit the surface profile, follow the [GUI workflow](#run-gui).

## Profile (.pr2) files

The geometry tool can save everything it uses to generate a geometry in a
profile. Saving one is highly recommended, so you can repeat the run later
(for example with `hlb-gmy-cli`).

It is a YAML file which can be edited manually. Floating point values
are stored by default in hexadecimal to avoid precision loss
(using `float.hex()` notation) but can be
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
| `PulsePeriodSeconds` | Optional. Period of the cosine pressure at every inlet and outlet (default 1 s) |
| `OutputGeometryFile`, `OutputXmlFile` | Outputs, relative to the profile |

Paths in a profile are interpreted relative to that profile file's location.

For installation or import failures, see [troubleshooting](troubleshooting.md).
