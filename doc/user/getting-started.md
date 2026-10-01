# First run: a cylinder with field output

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
cp Code/tests/resources/large_cylinder.gmy "$HOME/hemelb-first-run/"
cp doc/examples/first-run.xml "$HOME/hemelb-first-run/"
cd "$HOME/hemelb-first-run"
```

If that folder already contains a `results` directory, use a new run folder or
a new output name. HemeLB refuses to overwrite an existing output directory.

The example is [first-run.xml](../examples/first-run.xml). It keeps the
geometry's voxel size, origin, and pressure boundaries, runs for 200 timesteps,
and requests field output and fluid checkpoints every 100 timesteps. Paths in
the configuration are resolved relative to the XML file, so keep the copied
`.gmy` beside it.

## Check and run

```sh
hemelb-confcheck first-run.xml
mpirun -n 2 hemelb -in first-run.xml -out results
```

`hemelb-confcheck` checks XML parsing, units, and compatibility with the compiled
boundary types. It does not load the GMY or verify the geometry's inlet IDs.
The simulation performs those checks during setup.

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
For ParaView, use the [VTK export workflow](python-tools.md#export-for-paraview).
To resume at timestep 100 with another rank count, follow the
[checkpoint guide](checkpoints.md).

## Generate your own geometry

The solver example above starts with a prepared GMY. To try the geometry tool
as well, run these commands from the repository root with `gmy-tool` active:

```sh
mkdir -p "$HOME/hemelb-geometry-sample"
cp geometry-tool/tests/Model/data/test.pr2 "$HOME/hemelb-geometry-sample/"
cp geometry-tool/tests/Model/data/test.stl "$HOME/hemelb-geometry-sample/"
cd "$HOME/hemelb-geometry-sample"
hlb-gmy-cli test.pr2
hlb-gmy-countsites test.gmy
hlb-gmy-selfconsistent test.gmy
```

This produces `test.gmy` and `test.xml`. The sample profile imposes equal
pressures, so it is useful for generation checks rather than a driven flow.
Generated XML has no field output by default. Add a `<properties>` section as
shown in the [XML reference](XmlConfiguration.md#extracted-properties).

Use `hlb-gmy-gui` to create a profile from your own STL. The
[geometry guide](geometry-tool.md) explains surface preparation, units, seed
points, and iolet placement. If a command fails, consult
[troubleshooting](troubleshooting.md).
