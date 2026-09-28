# The HemeLB XML configuration file

This file is the main input file for a HemeLB simulation: it says which
geometry to use, how long to run, what happens at the inlets and outlets,
how to start, and which results to save.

The geometry tool writes a complete XML file next to the `.gmy`. It runs as
it is, but it has no `<properties>` section, so it saves no results until
you add one (see "(Extracted) Properties" below and the
[Getting started](getting-started.md) walkthrough). Check a file without
running it with `hemelb-confcheck config.xml`.

The rest of this page is a reference for every element.

All parameters that correspond to a property of the modelled system
should be given as follows:

    <$NAME value="$VALUE" units="$UNITS" />
where:
 * `$NAME` is a descriptive (considering the context of containing
   elements) name for the physical quantity;
 * `$UNITS` is a string specifying the units in which the value is
   given; and,
 * `$VALUE` is a string which can be converted to the C++ type of the
   quantity (`T`) by `operator>>(stream&, T&)`. For example:
```xml
<step_length value="0.0001" units="s" />
```
or:
```xml
<position value="(0.0,0.0,0.05)" units="m" />
```
   (since `operator>>` has been overriden to parse such a string into
   a `util::Vector3D<float>`).

At the top level there must be the root element: `<hemelbsettings
version="int">`. It MUST have a version attribute, an integer. This is
currently 5.

## Simulation
The `<simulation>` is required and specifies some global properties of
the simulation, mainly time-related.

Its child elements are:
* Required: `<stresstype value="int">` - the type of stresses to calculate. Must correspond to the `enum hemelb::lb::StressTypes`
* Required: `<step_length value="float" units="s" />` - the length of a time step; units must be s (seconds)
* Required: `<steps value="int" units="lattice" />` - the length of the main simulation; units must be lattice
* Required: `<voxel_size value="float" units="m" />` - the voxel size in the gmy file
* Required: `<origin value="(x,y,z)" units="m" />` - the location of lattice site (0,0,0) in world coordinates 
* Optional: `<extra_warmup_steps value="int" units="lattice" />` - the length of the simulation's warmup period; units must be lattice
* Optional: `<fluid_density value="float" units="kg/m3" />` - the
  density of the working fluid (corresponds to 1 in lattice
  units). Default is 1000 kg/m3.
* Optional: `<fluid_viscosity value="float" units="Pa.s" />` - the
  viscosity of the working fluid (corresponds to 1 in lattice
  units). Default is 0.004 Pa.s.
* Optional: `<reference_pressure value="float" units="mmHg" />` the
  physical pressure that corresponds to a lattice density
  of 1. Default is 0.


## Geometry
The `<geometry>` element is required. It has one, required, child element:
* `<datafile path="relative path to geometry file" />` - the path
  (relative to the XML file) of the GMY file.
  
## Inlets
`<inlets>` - the element contains zero or more `<inlet>` subelements

The inlets are numbered in the order they appear, starting from 0, and the
geometry file refers to them by these numbers (outlets likewise). There must
be at least as many `<inlet>` elements as the geometry uses; otherwise
HemeLB stops with "The geometry file uses N inlet(s) but the configuration
defines M". The geometry tool writes them in the same order as in its
profile.

Which condition types are allowed depends on how HemeLB was built
([CMakeOptions.md](CMakeOptions.md)): the default build
(NASHZEROTHORDERPRESSUREIOLET) needs `type="pressure"`, a LADDIOLET build
needs `type="velocity"`. The geometry tool always writes
`type="pressure" subtype="cosine"`, with the phase in radians and the period
taken from the profile's `PulsePeriodSeconds` (1 s by default).

* `<inlet>` - describes the position and orientation of an inlet plane
  as well as the boundary conditions to impose upon it. Inlets always
  have the following two sub elements
  * `<position value="(x,y,z)" units="m" />` - the location of a point
    on the inlet plane (should be the centre if that makes
    sense). Must have three attributes (x,y,z) which give the location
    in metres in the input coordinate system
  * `<normal value="(x,y,z)" units="dimensionless" />` - a vector
    normal to the plane. Must have x,y,z attributes. Does not *have*
    to be normalised but for good practice should be.
  * `<condition type="" subtype="">` - Gives the BC. There are several
    types available, in two classes, pressure-based and
    velocity-based.
    * `type="pressure"`
      * `subtype="cosine"` - all subelements required
	    * `<amplitude value="float" units="mmHg" />`
		* `<mean value="float" units="mmHg" />`
        * `<phase value="float" units="rad" />`
        * `<period value="float" units="s" />`
	  * `subtype="file"` - all subelements required
	    * `<path value="relative/path/to/pressure/data/file" />`
	  * `subtype="multiscale"` - all subelements required
	    * `<pressure value="float" units="mmHg" />`
        * `<velocity value="velocity" units="m/s" />`
        * `<label value="multiscale_label_string" />`
    * `type="velocity"`
      * `subtype="parabolic"` - Poiseuille flow in a cylinder, i.e. parabolic
		* `<radius value="float" units="lattice" />` -  radius of tube (in lattice units)
		* `<maximum value="float" units="lattice">` -  maximum velocity (in lattice units)
	  * `subtype="womersley"`
		* `<womersley_velocity>` - a Womersley flow in a cylinder
		* `<pressure_gradient_amplitude value="float" units="lattice"/>`
        * `<period value="float" units="lattice"/>`
        * `<womersley_number value="float" units="dimensionless"/>`
        * `<radius value="float" units="lattice"/>`
    * `subtype="file"` - all subelements required
        * `<path value="relative/path/to/velocity/data/file" />`
        * `<radius value="float" units="lattice"/>` or `<radius value="float" units="m"/>`
        (for inlets that are not circular, see
        [non-cylindrical-velocity-inlets.md](non-cylindrical-velocity-inlets.md))

## Outlets
As for "inlets" but with `s/inlet/outlet/`

## Initial Conditions
`<initialconditions>` - describe initial conditions. Child elements:

* `<pressure>` - start at rest and equilibrium at the given pressure
  field
  * `<uniform value="float" units="mmHg">` - a uniform pressure at all
    sites. Value must be in mmHg.

* `<checkpoint file="rel/path/to/file" offsets="rel/path">` - restart from a
  checkpoint + offset file. Attribute `file` is required and gives
  path to the checkpoint. The `offsets` attribute is optional; if given it
  must be a relative path to the offset file, otherwise HemeLB uses the
  checkpoint path with the extension replaced by ".off". The restart may use
  a different number of MPI processes from the run that wrote the
  checkpoint, but must use the same geometry: HemeLB stops if the voxel size
  or origin differ.

## (Extracted) Properties
Describe what data to extract under the `<properties>` element. Child elements:

* `<propertyoutput file="path.xtr" period="int"
  timestep_mode="[multi|single]">` - specify the file (under the
  `results/Extracted` directory) and the output period (in time
  steps). The way that multiple timesteps of data will be handled is
  set by the `timestep_mode` attribute. Valid values are `multi` (the
  default if the attribute is not present) or `single`. For `multi`,
  each subsequent timestep's data will be appended to the same
  file. For `single`, only a single timestep will be written to each
  file; in this case the `file` attribute must contain exactly one
  `%d` which will be replaced with the timestep number, padded with
  leading zeros (at least 3 digits, more if the run is longer) so the files
  sort in order, for example `flow_%d.xtr` gives `flow_0100.xtr`.
  - `<geometry type="type">` - the type string must be one of the following:
    + `type="whole"` - all lattice points - no subelements needed
	+ `type="surface"` - all lattice points with one or more links
      (defined by the active velocity set) intersecting a wall (not
      inlet/outlet) - no sublements needed
    + `type="plane"` - all lattice points within sqrt(3)*voxel size of the specified plane
      * Required: `<point value="(x,y,z)" units="m" />`
      * Required: `<normal value="(x,y,z)" units="dimensionless" />`
      * Optional: `<radius value="float" units="m" />` If absent assume r == 0, => infinite plane
    + `type="line"` - all lattice points close to a finite line between the two points specified
      * Required twice: `<point value="(x,y,z)" units="m" />`
    + `type="surfacepoint"` - all lattice points close to the points specified
      * Required: `<point value="(x,y,z)" units="m" />`
  - `<field type="type" name="optional name">` - the type string must be one of the following:
    + `type="velocity"`
    + `type="pressure"`
    + `type="vonmisesstress"`
    + `type="shearstress"`
    + `type="shearrate"`
    + `type="stresstensor"`
    + `type="traction"`
    + `type="tangentialprojectiontraction"`
    + `type="mpirank"`

* `<checkpoint file="path" period="int">` - save a checkpoint file at the
  given interval (in timesteps). Each checkpoint is a separate file, so the
  path must contain `%d` as for `timestep_mode="single"` (for example
  `checkpoint_%d.xtr` gives `checkpoint_0200.xtr`). HemeLB also writes an
  offset file (here `checkpoint_.off`), which a restart needs.

## Changes

### Version 5

Added checkpoint element.
