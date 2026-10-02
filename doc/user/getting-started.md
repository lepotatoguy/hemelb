# Quick start: a cylinder with field output

This walkthrough uses the bundled cylinder geometry and a configuration with
velocity, pressure, and checkpoint output already enabled. It checks that the
solver and analysis tools work together.

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

The [example configuration](../../examples/first-run.xml) runs a cylinder
simulation for 200 steps and saves results and checkpoints every 100 steps.
Keep `first-run.xml` and `first-run.gmy` in the same folder.

## Run

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
| `results/Checkpoints/100/distributions.xtr` | Fluid distributions after 100 updates |
| `results/Checkpoints/200/distributions.xtr` | Fluid distributions after 200 updates |
| `results/Checkpoints/100/restart.xml` | Configuration and supported model state for restart |
| `results/Checkpoints/distributions.off` | Offsets shared by the checkpoint series |

## Read the output

```sh
hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv
```

`whole.csv` contains velocity in m/s and pressure in Pa. Results for each
saved timestep appear in a separate block; keep the comment headers to
identify each timestep.

For ParaView, export a collection with the installed command:

```sh
hlb-extracted-to-vtk results/Extracted/whole.xtr whole
```

Open `whole.pvd` in the **ParaView 5.13.3 GUI (tested)**. See the
[ParaView guide](python-tools.md#export-for-paraview) for field selection.
