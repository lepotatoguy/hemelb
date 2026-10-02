# Quick start: a cylinder with field output

This walkthrough uses the bundled cylinder geometry and a configuration with
velocity, pressure, and checkpoint output already enabled. It checks that the
solver and analysis tools work together. A short run does not establish steady
flow or physical accuracy.

## Prepare the environment

Install HemeLB using the [installation guide](install.md). Run the following
from the repository root; replace the solver prefix if you chose another one.
The default executable uses pressure inlets and outlets, matching this example.

```sh
export PATH="$HOME/.local/hemelb/bin:$PATH"
conda activate gmy-tool

mkdir -p "$HOME/hemelb-first-run"
cp examples/first-run.* "$HOME/hemelb-first-run/"
cd "$HOME/hemelb-first-run"
```

If that folder already contains a `results` directory, use a new run folder or
a new output name. HemeLB refuses to overwrite an existing output directory.

The example is [first-run.xml](../../examples/first-run.xml). It keeps the
geometry's voxel size, origin, and pressure boundaries, runs for 200 timesteps,
and requests field output and fluid checkpoints every 100 timesteps. Paths in
the configuration are resolved relative to the XML file, so keep the copied
`first-run.gmy` beside it. The [examples folder](../../examples/README.md) also
contains the matching STL and profile, so the same case can be regenerated.

## Run

Optionally check the XML before starting:

```sh
hemelb-confcheck first-run.xml
```

`hemelb-confcheck` checks XML parsing, units, and compatibility with the compiled
boundary types. It does not load the GMY or verify the geometry's inlet IDs.
The simulation performs those checks during setup.

Start the simulation:

```sh
mpirun -n 2 hemelb -in first-run.xml -out results
```

A successful run writes these files:

| File | Purpose |
| :--- | :--- |
| `results/report.txt`, `results/report.xml` | Run configuration and timings |
| `results/Extracted/whole.xtr` | Velocity and pressure samples |
| `results/Extracted/whole.off` | Companion offsets for the field output |
| `results/Extracted/checkpoint_100.xtr` | Fluid distributions at timestep 100 |
| `results/Extracted/checkpoint_200.xtr` | Fluid distributions at timestep 200 |
| `results/Extracted/checkpoint_.off` | Offsets shared by the checkpoint series |

## Read the output

```sh
hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv
```

The file contains comment headers followed by comma-separated rows, one per
site and saved timestep. Headers identify the timestep and columns. Grid
coordinates are `grid_0` to `grid_2`; velocity components are in m/s and
pressure is in mmHg on this branch. Physical position is
`origin + voxel_size * grid`.

Each timestep has its own block of rows. For analysis that needs the timestep
as a separate array, use the [Python reader](python-tools.md#read-fields-in-python).
For ParaView, export a collection with the installed command:

```sh
hlb-extracted-to-vtk results/Extracted/whole.xtr whole --step-length 0.0001
```

Open `whole.pvd` in ParaView. The [VTK export guide](python-tools.md#export-for-paraview)
explains field selection, physical coordinates, and output options.
To resume at timestep 100 with another rank count, follow the
[checkpoint guide](checkpoints.md).

## Regenerate the case from STL

The copied `first-run.stl` contains a cylinder in millimetres. Its matching
`first-run.pr2` sets the voxel size, seed point, inlet/outlet clipping planes,
and pressure conditions. With `gmy-tool` active, run in the first-run folder:

```sh
hlb-gmy-cli first-run.pr2 --geometry regenerated.gmy --xml generated.xml
hlb-gmy-countsites regenerated.gmy
hlb-gmy-selfconsistent regenerated.gmy
```

Keep the supplied GMY/XML pair unchanged. Regeneration writes a separate pair,
including the geometry's newly computed origin. Generated XML has no field
output; copy the ready example's output requests into it:

```sh
python - <<'PYCODE'
import copy
import xml.etree.ElementTree as ET

tree = ET.parse("generated.xml")
outputs = ET.parse("first-run.xml").getroot().find("properties")
tree.getroot().append(copy.deepcopy(outputs))
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
or iolet settings. Re-run the geometry generator before adding `<properties>`
again, so the section is not duplicated.

Use `hlb-gmy-gui` to create a profile from your own STL. The
[geometry guide](geometry-tool.md) explains surface preparation, units, seed
points, and iolet placement. If a command fails, consult
[troubleshooting](troubleshooting.md).
