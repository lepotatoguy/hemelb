# XML configuration reference

This reference covers fluid simulation settings on `fix/hemelb-improvements`.
The solver requires `<hemelbsettings version="5">`. Physical pressures use
mmHg on this branch; do not substitute Pa or configurations from another branch
without checking its format. The geometry tool writes version 5 XML.

Start with the [ready-to-run example](../examples/first-run.xml) and
[first-run guide](getting-started.md). Check parsing, units, and compiled
boundary choices with:

```sh
hemelb-confcheck input.xml
```

This command does not load the geometry. A successful parse does not verify
its site coordinates, iolet IDs, or the existence of every referenced file.

## Structure and units

Required root children for a fluid run are `<simulation>`, `<geometry>`,
`<inlets>`, `<outlets>`, and `<initialconditions>`. The inlet/outlet containers
may be empty. `<properties>` enables field output or checkpoints;
`<monitoring>` enables additional run checks.

Quantities use `value` and `units` attributes. Units must match the expected
strings exactly. Vectors are written as one parenthesised `value`, not as
separate `x`, `y`, and `z` attributes:

```xml
<step_length value="0.0001" units="s" />
<position value="(0.0,0.0,0.05)" units="m" />
<normal value="(0.0,0.0,1.0)" units="dimensionless" />
```

Relative geometry, boundary-data, and checkpoint paths are resolved relative
to the XML file. Output filenames belong under the run's `Extracted/` folder.

## Simulation

| Child of `<simulation>` | Required | Units | Meaning or default |
| :--- | :--- | :--- | :--- |
| `stresstype` | Yes | No units attribute | Legacy required field; accepts 0, 1, or 2. Select saved stress fields in `<properties>` |
| `step_length` | Yes | `s` | Duration of one timestep |
| `steps` | Yes | `lattice` | Final timestep for the main simulation |
| `voxel_size` | Yes | `m` | Geometry lattice spacing |
| `origin` | Yes | `m` | Physical position of lattice coordinate `(0,0,0)` |
| `extra_warmup_steps` | No | `lattice` | Warmup steps added to the total; default 0 |
| `fluid_density` | No | `kg/m3` | Fluid density corresponding to lattice density 1; default 1000 |
| `fluid_viscosity` | No | `Pa.s` | Dynamic viscosity; default 0.004 |
| `reference_pressure` | No | `mmHg` | Physical pressure corresponding to lattice density 1; default 0 |

Use the voxel size and origin produced with the GMY. Changing them in XML
reinterprets the same lattice coordinates in physical space. Changing grid
resolution requires regenerating the geometry and choosing an appropriate
timestep for the new grid.

## Geometry

```xml
<geometry>
  <datafile path="mesh.gmy" />
</geometry>
```

The solver checks the path and validates geometry records when the run starts.
See [geometry reading](../dev/geometry-reading.md) for the reader's checks and
size limits.

## Inlets and outlets

Each `<inlet>` or `<outlet>` has a `<position>`, `<normal>`, and `<condition>`.
Iolets are numbered by order within their container, starting at zero; GMY
records refer to those IDs. Keep the order generated from the profile. There
must be enough configured iolets for every ID used by the geometry.

```xml
<inlet>
  <position value="(0.0,0.0,0.0)" units="m" />
  <normal value="(0.0,0.0,1.0)" units="dimensionless" />
  <condition type="pressure" subtype="cosine">
    <mean value="0.01" units="mmHg" />
    <amplitude value="0" units="mmHg" />
    <phase value="0" units="rad" />
    <period value="1" units="s" />
  </condition>
</inlet>
```

Positions are physical coordinates. Normals point into the fluid; use nonzero,
normalised vectors. Outlets use the same structure inside `<outlets>`.

The executable's inlet and outlet implementations are selected independently
at build time. `NASHZEROTHORDERPRESSUREIOLET` accepts pressure conditions;
`LADDIOLET` accepts velocity conditions. A mismatch is an error, including in
`hemelb-confcheck`. See [CMake options](CMakeOptions.md#lattice-boltzmann-model).

### Pressure conditions

All listed children are required.

| Subtype | Children |
| :--- | :--- |
| `cosine` | `mean` and `amplitude` (`mmHg`), `phase` (`rad`), `period` (`s`) |
| `file` | `<path value="pressure.txt" />`; time/pressure pairs use seconds and mmHg |
| `multiscale` | `pressure` (`mmHg`), `velocity` (`m/s`), and `<label value="..." />`; use with the multiscale workflow |

The geometry tool generates cosine pressure conditions. Its profile stores
mean, amplitude, and phase in `Pressure.x`, `.y`, and `.z`; period comes from
`PulsePeriodSeconds`.

### Velocity conditions

| Subtype | Required children |
| :--- | :--- |
| `parabolic` | `radius` (`m`), `maximum` (`m/s`) |
| `womersley` | `radius` (`m`), `pressure_gradient_amplitude` (`mmHg/m`), `period` (`s`), `womersley_number` (`dimensionless`) |
| `file` | `<path value="velocity.txt" />`, `radius` (`m`); file time/velocity pairs use seconds and m/s |

Parabolic and Womersley profiles assume a circular cross-section. Per-site
weights for a file profile require a separate build option and weights file;
see [non-cylindrical velocity inlets](non-cylindrical-velocity-inlets.md).

## Initial conditions

Exactly one of `<pressure>` or `<checkpoint>` is required. A uniform-pressure
start initializes equilibrium at rest:

```xml
<initialconditions>
  <pressure>
    <uniform value="0" units="mmHg" />
  </pressure>
</initialconditions>
```

To load saved fluid distributions:

```xml
<initialconditions>
  <checkpoint file="results/Extracted/checkpoint_100.xtr"
              offsets="results/Extracted/checkpoint_.off" />
</initialconditions>
```

`file` is required. If `offsets` is absent, the reader replaces the checkpoint's
extension with `.off`. Supply it explicitly for checkpoint series whose shared
offset name omits `%d`. Optional `<time value="100" units="lattice" />` selects
a stored timestep; otherwise checkpoint loading uses the last one in that file.
The [checkpoint guide](checkpoints.md) covers changed rank counts and final
run timesteps.

## Extracted properties

Generated XML has no field output by default. Add a `<properties>` section
before `</hemelbsettings>`:

```xml
<properties>
  <propertyoutput file="whole.xtr" period="100">
    <geometry type="whole" />
    <field type="velocity" />
    <field type="pressure" />
  </propertyoutput>
  <checkpoint file="checkpoint_%d.xtr" period="100" />
</properties>
```

`period` is an interval in lattice timesteps. Choose a positive value. Whole
geometry output scales with fluid-site count and saved timesteps; use a smaller
selector or longer output interval when whole-volume data is unnecessary.

### Output files

`<propertyoutput file="..." period="..." timestep_mode="...">` accepts:

| Mode | Behaviour |
| :--- | :--- |
| `multi` (default) | Append saved timesteps to one `.xtr` file |
| `single` | One file per saved timestep; filename must contain exactly one `%d` and no other `%` |

For single files, timestep numbers are padded to at least three digits, and to
more digits when the final run timestep needs them. Offset filenames remove
`%d` and replace the extension with `.off`. `<checkpoint>` always writes single
timestep files with one double-precision distributions field. See
[checkpoint output](checkpoints.md#save-checkpoints).

### Geometry selectors

| `geometry type` | Children and selection |
| :--- | :--- |
| `whole` | No children; all fluid sites |
| `surface` | No children; fluid sites with a lattice link crossing a wall |
| `plane` | `point` (`m`), `normal` (`dimensionless`), optional `radius` (`m`); sites within half a voxel of the plane; nonpositive/absent radius means no radial limit |
| `line` | Two `point` children (`m`), defining the endpoints |
| `surfacepoint` | One `point` (`m`), defining the surface-point selector |

Example plane selector:

```xml
<geometry type="plane">
  <point value="(0,0,0)" units="m" />
  <normal value="(0,0,1)" units="dimensionless" />
  <radius value="0.002" units="m" />
</geometry>
```

### Fields

Each `<field type="..." name="..." />` has a required type and optional output
name, defaulting to the type. Supported types are `velocity`, `pressure`,
`vonmisesstress`, `shearstress`, `shearrate`, `stresstensor`, `traction`,
`tangentialprojectiontraction`, `mpirank`, and `distributions`.
Ordinary fields use float output, except integer `mpirank`. Use `<checkpoint>`
for distributions intended for a full-precision restart.

The [Python tools](python-tools.md) restore stored pressure offsets when reading
fields. See [extraction format](../dev/file-formats/extraction.md) for layout.

## Monitoring

`<monitoring><incompressibility /></monitoring>` enables the incompressibility
check. A steady-flow convergence check uses a velocity criterion:

```xml
<monitoring>
  <incompressibility />
  <steady_flow_convergence tolerance="1e-6" terminate="true">
    <criterion type="velocity" value="0.01" units="m/s" />
  </steady_flow_convergence>
</monitoring>
```

The criterion's value is the reference velocity for the relative check.
`terminate="true"` requests early termination on convergence; use `false` to
monitor without requesting that termination. This is a steady-flow criterion;
do not use it as evidence that a pulsatile flow has converged.

## Optional model sections

Resolved red blood cells, colloids, and multiscale coupling require matching
build options and model-specific input. See the [build options](CMakeOptions.md),
[RBC configuration fixtures](../../Code/tests/resources/large_cylinder_rbc.xml),
and [multiscale code](../../Code/multiscale). This fluid reference does
not specify those complete models or establish their physical validation.

Version 5 introduced the checkpoint initial-condition and output elements.
