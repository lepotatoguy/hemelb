# Select fields, regions, and sample times

Add `<properties>` to the configuration to save fields for analysis. The
geometry tool does not add field output by default. New files use extraction
version 7; the bundled Python reader restores offsets and scales to return
physical values. See [Python tools](python-tools.md) for CSV and ParaView export.

Version 7 headers name each field's physical unit, including custom-named
fields. Existing version 6 pressure remains Pa when read; versions 4/5 retain
their stored physical units.

## Save a local region

This version 6 example saves velocity and pressure near the centre of the
bundled cylinder:

```xml
<properties>
  <propertyoutput file="region.xtr" period="10" start="20" stop="100">
    <geometry type="sphere">
      <point value="(0,0,0)" units="m" />
      <radius value="0.0003" units="m" />
    </geometry>
    <field type="velocity" datatype="double" />
    <field type="pressure" datatype="double" />
  </propertyoutput>
</properties>
```

`start` and `stop` are inclusive lattice timesteps. Sampling follows multiples
of `period` from global timestep zero, including after restart. A start of 23
and period 10 first samples at 30. The period must be positive and stop must be
at least start. Without a window, output covers the run's sampled timesteps.

Field samples label the timestep seen during the update; modern checkpoint
folder names count completed updates. In a fresh 200-update run, a period of
100 gives field samples at 0 and 100, and completed checkpoints at 100 and 200.
Do not infer that an extraction contains a sample at the final checkpoint label.

## Choose a geometry selector

| Selector | Required children | Region |
| :--- | :--- | :--- |
| `whole` | None | All fluid sites |
| `surface` | None | Fluid sites with a wall-crossing lattice link |
| `inlet`, `outlet` | None | Fluid sites touching any inlet or any outlet |
| `plane` | `point` in m, `normal` dimensionless; optional `radius` in m | Sites within half a voxel of the plane, optionally limited radially |
| `line` | Two `point` children in m | Sites selected along a finite line |
| `surfacepoint` | `point` in m | Surface-point selection near the requested point |
| `sphere` | `point` and `radius` in m | Fluid sites inside or on the sphere |
| `surfaceWithinSphere` | `point` and `radius` in m | Wall sites inside or on the sphere |

Selectors evaluate physical coordinates using the configured origin and voxel
size. An iolet selector covers all iolets of that kind; it has no ID filter.
Sphere selectors reduce saved data but do not change the simulated domain.

## Choose fields and precision

| Field type | Physical interpretation of version 7 output |
| :--- | :--- |
| `velocity` | Three components in m/s |
| `pressure` | Scalar pressure in mmHg, including reference pressure |
| `vonmisesstress`, `shearstress` | Scalar stress in Pa |
| `shearrate` | Scalar rate in s⁻¹ |
| `stresstensor` | Six symmetric components in Pa |
| `traction`, `tangentialprojectiontraction`, `normalprojectiontraction` | Three components in Pa |
| `wallextension` | Elastic-wall diagnostic in metres |
| `mpirank` | Integer owner rank |
| `distributions` | Lattice distributions, one component per lattice velocity |

`name="..."` supplies an output field name. Default names match the type.
Ordinary fields default to float, and `mpirank` defaults to int32. Optional
`datatype` accepts `float`, `double`, `int32`, `uint32`, `int64`, or `uint64`.
Choose a representation that preserves the field's range and fractional values.
Use [simulation checkpoints](checkpoints.md) for complete double-precision
fluid restart rather than a reduced-region distributions extraction.

Normal traction projects wall traction onto the wall normal. Elastic extension
is `(rho - 1) / (3 * stiffness) * voxel_size` where the site's selected boundary
uses the elastic wall rule. Rigid walls and bulk sites produce zero extension.
The geometry stays fixed: this field does not create a displaced surface mesh
or establish a validated structural vessel model. See [CPU models](cpu-models.md).

## File layout and export

`file="region.xtr"` with the default `timestep_mode="multi"` appends samples
to one file. For `timestep_mode="single"`, use exactly one `%d` in the filename,
for example `region_%d.xtr`. The companion offset filename removes `%d` and
uses `.off`. Output belongs under the run's `Extracted/` directory.

```sh
hlb-dump-extracted-properties results/Extracted/region.xtr region.csv
hlb-extracted-to-vtk results/Extracted/region.xtr region
```

Open `region.pvd` in ParaView. Version 7 exports use physical coordinates,
physical field values, and time in seconds from the header. Only selected
sites appear in the exported voxel grid. The exporter handles the new wall
fields and reorders symmetric tensors for VTK. For binary header details and
legacy units, see the [format reference](../dev/file-formats/extraction.md).
