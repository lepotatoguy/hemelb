# Save and restart a fluid checkpoint

Checkpoints store all fluid distributions at double precision. On this branch
they use extraction format version 5, with a companion `.off` file. The restart
may use a different number of MPI ranks, but needs the same fluid geometry and
lattice. This guide covers fluid checkpoints; it does not describe restoring
red blood cell or colloid state.

## Save checkpoints

Add this child to the configuration's `<properties>` section:

```xml
<checkpoint file="checkpoint_%d.xtr" period="100" />
```

The file pattern must contain exactly one `%d` and no other `%` characters.
HemeLB replaces it with the timestep, padded to at least three digits. Output
is under the run's `Extracted/` directory. For a 200-step run, the checkpoint
at step 100 is `checkpoint_100.xtr`; the matching offsets are
`checkpoint_.off`, with `%d` removed. Keep both files.

The [first-run configuration](../../examples/first-run.xml) already enables this
output. Complete that [walkthrough](getting-started.md) before the example below.

## Resume the first-run example

In the same folder as `first-run.xml` and `results/`, create `restart.xml`:

```sh
python - <<'PYCODE'
import xml.etree.ElementTree as ET

tree = ET.parse("first-run.xml")
initial = tree.getroot().find("initialconditions")
initial.clear()
ET.SubElement(initial, "checkpoint",
              file="results/Extracted/checkpoint_100.xtr",
              offsets="results/Extracted/checkpoint_.off")
tree.write("restart.xml", encoding="utf-8", xml_declaration=True)
PYCODE
```

Optionally check `restart.xml` with `hemelb-confcheck restart.xml`, then resume:

```sh
mpirun -n 4 hemelb -in restart.xml -out resumed
```

The fresh run used two ranks; this restart uses four. `<steps value="200" />`
still specifies the final timestep, so the resumed run continues from the
saved timestep to 200. Set it to a larger final timestep to continue longer.
Use a new output folder for the resumed run.

Checkpoint and offset paths are relative to `restart.xml`. Supplying `offsets`
is necessary for this filename pattern: the default would replace the loaded
`.xtr` extension with `.off`, looking for `checkpoint_100.off` instead of the
shared `checkpoint_.off`.

## Select a saved timestep

By default the reader uses the last timestep in the selected file. To choose
a particular stored timestep, add `<time>` beside `<checkpoint>`:

```xml
<initialconditions>
  <checkpoint file="saved.xtr" offsets="saved.off" />
  <time value="100" units="lattice" />
</initialconditions>
```

The requested timestep must exist in the file. Normal checkpoint output uses
one timestep per file.

## What must match

- Voxel size and origin must match the checkpoint header.
- Fluid site coordinates must match; missing, duplicate, and invalid sites are rejected.
- The executable must use the same number of lattice distributions per site.
- The checkpoint and its offsets must come from the same saved output series.

Keep the original configuration and build options alongside saved output. The
reader checks geometry and distribution metadata; it does not verify that
viscosity, boundary conditions, or the collision model are unchanged. Preserve
those when reproducing a continuation.

The loader on this branch expects a version 5 checkpoint containing one
`distributions` field, double precision, and no offsets on that field. Python
support for extraction version 4 does not imply version 4 checkpoint loading.
See [developer notes](../dev/checkpoint-restart.md) for the redistribution
algorithm and tests, or [troubleshooting](troubleshooting.md) for run failures.
