# Save and resume a checkpoint

A checkpoint lets you continue a stopped simulation from its saved fluid
state. This branch writes double-precision fluid distributions in extraction
version 7 and saves a restart configuration with supported boundary, coupling,
and tracer state. A restart can use a different MPI rank count while keeping
the same fluid geometry and lattice.

## Save checkpoints

Add a checkpoint request inside `<simulation>`:

```xml
<checkpoint period="100" />
```

The [first-run example](../../examples/first-run.xml) already includes this
request. HemeLB saves the initial state and checkpoints after each scheduled
number of completed updates. A 200-update run with period 100 produces:

| File | Purpose |
| :--- | :--- |
| `results/Checkpoints/100/distributions.xtr` | Fluid state after 100 updates |
| `results/Checkpoints/100/restart.xml` | Matching configuration and supported model state |
| `results/Checkpoints/200/distributions.xtr` | Fluid state after 200 updates |
| `results/Checkpoints/200/restart.xml` | Matching configuration at completion |
| `results/Checkpoints/distributions.off` | Shared offsets for the checkpoint series |

Keep each checkpoint folder, the shared offsets, and all referenced geometry
and boundary input files together. Moving only `distributions.xtr` loses the
saved model state and can break relative paths.

## Resume the example

After completing the [quick start](getting-started.md), launch its generated
restart configuration from the first-run folder:

```sh
mpirun -n 4 hemelb -in results/Checkpoints/100/restart.xml -out resumed
```

The original run used two ranks; this run resumes on four. A new output directory
is required. The original final timestep is still 200, so the restarted run
continues from 100 to 200. To continue longer, change `<simulation><steps>`
in the restart XML to the desired total timestep. It is the final timestep,
not the number of additional updates. Preserve the saved initial-condition
time and checkpoint references.

`hemelb-confcheck results/Checkpoints/100/restart.xml` is an optional parsing
and compiled-boundary check. The simulation verifies the files and geometry.
You may also change the runtime decomposition method or reader settings, but
reader count and spacing must fit the new rank count.

## Load an existing fluid checkpoint

HemePure version 4 and HemeLB version 5 checkpoints load directly, as do version
6 and 7 checkpoints. To use a fluid-only file, configure initial conditions as:

```xml
<initialconditions>
  <checkpoint file="saved/distributions.xtr" offsets="saved/distributions.off" />
</initialconditions>
```

Paths are relative to the configuration. If `offsets` is absent, HemeLB
replaces the checkpoint extension with `.off`. Supply it explicitly for a
legacy `checkpoint_%d.xtr` series, whose companion name is `checkpoint_.off`.
Old XML3/5 `<properties><checkpoint file="checkpoint_%d.xtr" period="100" />`
requests still load and keep their output filenames; their paths are not
changed to the modern `Checkpoints/` layout.

By default the reader uses the last stored timestep in the selected file.
To select one explicitly, place this beside `<checkpoint>`:

```xml
<time value="100" units="lattice" />
```

The timestep must exist in the file. Legacy float distributions are promoted
to double and stored offsets are restored; precision lost by the old writer
cannot be recovered.

## What must match

- Voxel size, physical origin, and fluid-site coordinates must match.
- The executable must use the same number of lattice distributions per site.
- Checkpoint and offsets must belong to the same saved series.
- Preserve fluid parameters and boundary/model choices when reproducing a continuation.

The loader rejects missing, duplicate, and invalid sites, and mismatched
geometry or distribution metadata. It does not prove that changed viscosity,
collision rules, or boundary values describe the same physical continuation.

Generated restart XML retains Windkessel pressure/flow history, read/write
coupling state, passive particles, waveform units and timing, output precision,
and decomposition choices. An old fluid-only checkpoint cannot recover model
state that its original writer never saved. Red blood cell and active colloid
restart validation is outside this fluid workflow. For the redistribution
algorithm and regression coverage, see [developer notes](../dev/checkpoint-restart.md).
