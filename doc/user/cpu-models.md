# Optional CPU models

These options extend the default fluid solver. Existing XML3/XML5 inputs and
v4/v5 checkpoints still load through the compatibility reader. Choose a build
whose kernel and boundary implementations match the input. Keep the default
build for simulations that do not use these options.

## Build choices

Set these CMake options when configuring `Code`. Use a separate build folder
for each combination; see [build and run](main-application.md#code-only-build).

| Model | Required option |
| :--- | :--- |
| WK2, WK3, fileWK | Nash pressure boundaries, or Yang with LBGK |
| Yang pressure | `HEMELB_INLET_BOUNDARY=YANGPRESSUREIOLET` and/or `HEMELB_OUTLET_BOUNDARY=YANGPRESSUREIOLET`; `HEMELB_KERNEL=LBGK` |
| LBGK sponge | `HEMELB_KERNEL=LBGKSL` |
| TRT sponge | `HEMELB_KERNEL=TRTSL` |
| Smagorinsky LES with sponge | `HEMELB_KERNEL=LBGKLESSL` |
| Elastic-wall surrogate | `HEMELB_WALL_BOUNDARY=GZSElastic` |
| Elastic Womersley or read/write velocity | `HEMELB_INLET_BOUNDARY=LADDIOLET` for an inlet |
| Passive tracers and sphere extraction | Available in the fluid build |

New boundary models currently use the existing GZS or Nash decomposition costs
as proxies. Those costs are not measurements of the new implementations. A
GMY+ file can supply computational costs directly.

## Windkessel outlets

For WK2, put this condition inside an outlet with its usual normal and position:

```xml
<condition type="pressure" subtype="WK2">
  <R units="kg/m^4*s" value="2e8"/>
  <C units="m^4*s^2/kg" value="1e-8"/>
  <radius units="m" value="0.0005"/>
  <area units="m^2" value="7.853981633974483e-7"/>
</condition>
```

These are schema examples, not fitted physiological parameters. WK3 replaces
`R` and `C` with `Rc`, `Rp`, and `Cp`, using the same resistance and capacitance
units. `fileWK` uses WK2's `R`, `C`, and `area`, with a `path` child instead of
`radius`, matching HemePure's spelling.

Flow is the mean outward normal velocity at boundary sites multiplied by the
specified physical area. The current fileWK flow estimator follows the source
solver's area-average path; the profile path is retained in XML and does not
apply a spatial weighting to that estimator. Do not interpret it as a new
file-driven pressure waveform.

The 0D pressure is a physical gauge pressure. At the end of an update, the model
advances pressure from the saved completed-flow history, then stores the current
flow sample. WK3 retains the previous flow as well. This preserves the source
update ordering while avoiding decay of the baseline lattice density toward
zero. The new XML checkpoint stores all three state values. Restarting from an
old fluid-only file requires supplying any outlet state that the old solver did
not save.

## Sponge and LES

Add this to `initialconditions`, alongside the fluid initial condition:

```xml
<sponge_layer>
  <viscosity_ratio units="dimensionless" value="4"/>
  <width units="m" value="0.0008"/>
  <lifetime units="lattice" value="2000"/>
</sponge_layer>
```

Viscosity rises quadratically toward the nearest outlet inside the width. It
stays at full strength through half the lifetime, then decays linearly to the
base viscosity. Overlapping regions use the nearest outlet's damping rather
than multiplying damping factors. This avoids amplifying viscosity just because
outlet regions overlap.

For `LBGKLESSL`, optionally set the coefficient inside `simulation`:

```xml
<smagorinsky_constant units="dimensionless" value="0.1"/>
```

Zero disables the LES contribution. The local relaxation time uses the full
non-equilibrium stress norm and actual local density. TRT uses the even/odd
relaxation pair with magic parameter 3/16; the rest direction is handled once.

## Yang pressure and elastic boundaries

Yang accepts the pressure subtypes above and the usual cosine/file pressure
conditions. At wall/iolet intersections it retains Nash pressure with the selected
wall rule, matching HemePure's default corner configuration. `type="yangpressure"` requires a Yang build; `type="pressure"` is
also accepted in that build. The implementation requires a straight inlet or
outlet with two fluid sites behind each reconstruction link and uses the
single-relaxation LBGK equations. Invalid stencils fail during setup. The build
requires lattice relaxation time `tau >= 0.8`; the cylinder test was unstable at
0.62 in both this implementation and the reference HemePure CPU build. The
two-resolution steady-flow check uses tau 0.8; this is not a guarantee of
stability for every geometry at larger tau. This restriction applies
only to Yang builds; ordinary Nash pressure builds retain their existing range.

`HEMELB_WALL_INLET_BOUNDARY` and `HEMELB_WALL_OUTLET_BOUNDARY` default to
`AUTO`, preserving the selected wall/iolet combination (Nash at Yang corners).
Optional explicit choices combine a prefix `NASHZEROTHORDERPRESSURE`,
`YANGPRESSURE`, or `LADDIOLET` with `SBB`, `BFL`, `GZS`, or `GZSE`. For example,
`YANGPRESSUREBFL` applies Yang at BFL wall/iolet intersections. The pressure or
velocity family must match the corresponding pure iolet. Yang at corners also
requires LBGK and the tested relaxation-time restriction. Explicit choices
allow a different corner wall rule; elastic extension output follows the rule
actually selected for each site type.

For elastic walls, set these values in `simulation`:

```xml
<elastic_wall_stiffness units="lattice" value="0.01"/>
<boundary_velocity_ratio units="lattice" value="0"/>
```

This is HemePure's compliant-wall velocity surrogate on fixed geometry. It does
not move the GMY wall or implement structural vessel mechanics. Stiffness must
be positive. The ratio is optional and defaults to zero.

An elastic Womersley velocity condition uses `subtype="womersleyElastic"`, with
`radius` (m), `pressure_gradient_amplitude` (mmHg/m), `period` (s),
`womersley_number` (dimensionless), `poisson_ratio` (dimensionless),
`youngs_modulus` (Pa), and `axial_position` (m). Its analytical profile retains
the source assumptions: wall thickness/radius 0.1 and equal wall/fluid density.
Young modulus is converted as a pressure difference, independent of reference
pressure. Large or degenerate parameters that produce non-finite values fail
with a named error.

Legacy XML3/5 camera settings are accepted but ignored by the numerical solver;
use extracted fields and ParaView for geometry and result views.

## File waveforms

Pressure and velocity file records are `time_seconds value`, with pressure units
selected by the pressure condition and velocity in m/s. Unsorted times are
sorted, and the last record at a duplicate time wins. Malformed or non-finite
records are rejected.

Set `timing="periodic"` on a file condition to interpolate by physical time and
repeat the interval between the first and last timestamps. End values must
match. The phase follows the global timestep across restart. Without this
attribute, pressure retains the old `stretch` mode and velocity retains its
`legacy` mode, preserving existing input behavior. Periodic mode is independent
of the planned run length and does not allocate a table for every update.

## Coupling, tracers, and extraction

Use [read/write coupling](coupling.md) to exchange flow and pressure with an
external program. The [tracer guide](tracers.md) covers particle seeds, emission,
CSV output, and passive legacy colloid inputs. [Field extraction](extraction.md)
covers sphere and iolet selectors, timestep windows, normal traction, and wall
extension. These workflows have dedicated examples and checkpoint notes.

## Vector builds and validation

`HEMELB_USE_AVX2=ON` or `HEMELB_USE_AVX512=ON` selects an x86 vector path.
Choose one; either takes precedence over SSE3. The executable must run on a CPU
supporting its selected instructions. Scalar and SSE3 defaults are unchanged.
Unaligned loads/stores and scalar tails cover D3Q15, D3Q19, D3Q27, and D3Q15i.

The [comparison](../dev/comparison-and-roadmap.md) records validation and limits.
Unit tests check 0D Windkessel solutions, local conservation, interpolation,
Bessel accuracy, and wall limits. MPI regressions check positive finite states
and rank-changing restart for each new model. These checks do not replace
reflection, resolution, stability-envelope, or physiological validation for a
particular study.

See [CPU verification](../dev/cpu-verification.md) for executable commands
and the distinction between regression coverage and physical validation.
