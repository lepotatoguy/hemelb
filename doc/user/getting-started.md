# Getting started: from a surface to results

This walkthrough takes you through one complete HemeLB run using files that
come with the repository. It assumes HemeLB and the geometry tool are
installed ([Installing HemeLB](install.md)) and that you have a terminal open.
Replace `/path/to/hemelb` with the folder you cloned.

```sh
export PATH="$HOME/.local/hemelb/bin:$PATH"   # where install_hemelb.sh puts hemelb
conda activate gmy-tool                        # geometry tool and Python tools
```

## The four steps

| Step | Tool | You give it | You get |
| --- | --- | --- | --- |
| 1. Make a geometry | `hlb-gmy-cli` or `hlb-gmy-gui` | a surface (`.stl`) and a profile (`.pr2`) with inlets, outlets and a voxel size | `.gmy` (the voxelised vessel) and `.xml` (the simulation settings) |
| 2. Choose outputs | a text editor | the `.xml` | the `.xml` with a `<properties>` section |
| 3. Simulate | `mpirun ... hemelb` | the `.xml` and `.gmy` | a results folder with extraction files (`.xtr`) |
| 4. Look at the results | `hlb-dump-extracted-properties` | an `.xtr` file | text or CSV you can open in a spreadsheet, Python or ParaView |

## 1. Make a geometry

The repository has a small sample: a surface and a profile describing one
inlet and one outlet.

```sh
mkdir -p ~/hemelb-first-run && cd ~/hemelb-first-run
cp /path/to/hemelb/geometry-tool/tests/Model/data/test.pr2 .
cp /path/to/hemelb/geometry-tool/tests/Model/data/test.stl .
hlb-gmy-cli test.pr2
```

This writes `test.gmy` and `test.xml` next to the profile. Check the result:

```sh
hlb-gmy-countsites test.gmy        # prints 6803 for this sample
hlb-gmy-selfconsistent test.gmy    # checks the file is internally consistent
```

If the tool prints a `Warning:` about an inlet, outlet or the seed point,
read it: the geometry may have no way for fluid to enter or leave. See
"Preparing the surface" in [geometry-tool.md](geometry-tool.md). To make your
own profile, open your STL in `hlb-gmy-gui`, place the inlets, outlets and
seed point, and use "Save Profile".

## 2. Choose outputs

A generated `.xml` has no `<properties>` section, so HemeLB would run but
write no results. Add one before the closing `</hemelbsettings>` line. This
example writes velocity and pressure everywhere every 100 time steps:

```xml
<properties>
  <propertyoutput file="whole.xtr" period="100">
    <geometry type="whole" />
    <field type="velocity" />
    <field type="pressure" />
  </propertyoutput>
</properties>
```

Other output shapes (planes, lines, points, the wall surface) are described
in [XmlConfiguration.md](XmlConfiguration.md).

## 3. Simulate

The sample above has the same pressure at its inlet and outlet, so nothing
flows. For a run where fluid moves, use the cylinder that the tests use:

```sh
cp /path/to/hemelb/Code/tests/resources/large_cylinder.gmy .
cp /path/to/hemelb/Code/tests/resources/large_cylinder.xml .
```

Add the `<properties>` block from step 2 to `large_cylinder.xml`, and to keep
this first run short change `<steps units="lattice" value="50000" />` to
`value="200"`. Then run on 4 processes:

```sh
mpirun -n 4 hemelb -in large_cylinder.xml -out results
```

The run ends with `Finish running simulation.` and the results are in
`results/Extracted/whole.xtr`. The output folder must not exist yet; choose a
new name for each run.

## 4. Look at the results

```sh
hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv
```

`whole.csv` starts with comment lines (`#`) that give the number of sites,
the fields, the geometry origin and the voxel size in metres. Then, for each
saved time step, there is one line per site with its grid position
(`grid_0` to `grid_2`), velocity (`velocity_0` to `velocity_2`, in m/s) and
pressure (mmHg). A site's position in metres is
origin + voxel size × grid position.

## When something goes wrong

| Message | What to do |
| --- | --- |
| `Geometry file ... does not exist` | The `<datafile path="...">` in the XML is wrong; relative paths are relative to the XML file |
| `The geometry file uses N outlet(s) but the configuration defines M` | Add the missing inlets or outlets to the XML, in the same order as in the profile |
| `The geometry has N block(s) containing fluid but HemeLB is running on P MPI processes` | Run with fewer processes, as the message says |
| `Output directory ... already exists.` | Use a new `-out` folder |
| A `Warning:` from `hlb-gmy-cli` or the GUI | See "Things to watch" in [geometry-tool.md](geometry-tool.md) |

## Words used in these guides

See the glossary in the [documentation index](../README.md#glossary).
