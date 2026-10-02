# Track passive particles

Tracers follow fluid velocity and produce trajectories for inspection. They
are available in the ordinary fluid build, without `HEMELB_BUILD_COLLOIDS`.
They have no mass or force feedback in the fluid equations.

## Seed particles and configure emission

Add a root `tracers` section. Coordinates and radii are in lattice units:

```xml
<tracers output_period="10" seed="1">
  <particles>
    <subgridParticle units="lattice" ParticleId="0" Radius="0.01">
      <initialPosition units="lattice" x="5.5" y="5.5" z="10"/>
    </subgridParticle>
    <sphereRadius units="lattice" value="0.1"/>
    <sphereCentre units="lattice" x="5.5" y="5.5" z="12"/>
    <emissionCount units="dimensionless" value="2"/>
    <emissionItrvl units="dimensionless" value="7"/>
  </particles>
</tracers>
```

Omit the emission fields for fixed seeds. The four-point Peskin kernel
interpolates fluid velocity, followed by explicit Euler advection after the
source model's two-update creation delay. Tracers exert no force on the fluid.
Emission is deterministic from seed and ID and samples the sphere surface
uniformly. Particle state and emission counters are checkpointed. Results go to
`tracers.csv`, with physical coordinates, velocity, time, ID, and active flag.

Legacy `colloids` particle spellings are accepted for passive use; their first
particle is the emission template. Inputs with body forces require explicit
`mode="tracer"` to discard those forces, as HemePure's tracer build did. Active
force-coupled colloids are unsupported. Boundary rules support `lubrication`,
`deletion`, and `spherical` with the source `appliesTo` and field spellings.
Lubrication damps normal motion as separation vanishes, leaving tangential
motion unchanged; deletion removes particles within the requested boundary
range. A zero-radius spherical rule is disabled. A zero emission interval disables emission. These corrections avoid the
source's singular wall correction and ineffective deletion range.

### Boundary rule parameters

Optional `<boundaryConditions>` is a child of `<tracers>`. `lubrication` and
`deletion` have attributes `appliesTo="Wall"`, `"Ilet"`, or `"Olet"`, and a
positive `effectiveRange` in lattice units. A `spherical` child uses
`appliesTo="Sphr"` with `sphereRadius` and `sphereCentre` children in lattice
units; particles outside that sphere become inactive. Capitalisation matches
the legacy schema. These rules apply to passive trajectories, not fluid walls.

Particle lists are replicated across ranks, with a limit of 100,000 particles.
This targets modest diagnostic tracer sets, not a demonstrated large-scale
particle simulation. Particles leaving the fluid domain become inactive.

The example coordinates describe lattice locations, not physical metres.
For the bundled cylinder, retain the shown seed coordinates or check a new
seed against the GMY. A particle outside fluid becomes inactive; a radius is
a tracer parameter rather than a resolved particle surface.

## Read trajectories

The run writes `tracers.csv` under its output directory with columns
`step,time_s,id,active,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms`. Unlike the extraction
text converter, this CSV includes time in each row. Match trajectories by
particle ID and use the active flag to distinguish particles that have left
the fluid or been deleted. Positions and velocity components are physical
values; emission and boundary configuration remain in lattice units.

## Restart

Use the saved `Checkpoints/<step>/restart.xml` together with its fluid
checkpoint. The XML retains particle positions, status, IDs, creation times,
and emission counters. Keep the saved seed and boundary rules to reproduce
emission. A legacy fluid-only checkpoint has no particle history to restore.
See [checkpoints](checkpoints.md).

## Checks and limits

`cpu-features-mpi` covers deterministic emission, trajectory continuation, and
rank-changing restart. See [CPU verification](../dev/cpu-verification.md).
The interpolation uses a four-point kernel with up to 64 neighbouring sites.
Replicated particle storage and per-particle communication limit this feature
to modest diagnostic sets; cluster-scale particle performance is unverified.
