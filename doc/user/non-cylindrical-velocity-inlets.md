# Velocity inlets with a per-site profile (non-cylindrical inlets)

Most users do not need this page. The default build uses pressure inlets and
outlets, which work for any inlet shape. Velocity inlets with a built-in
profile (`subtype="parabolic"` or `"womersley"`) assume a circular inlet. For
an inlet of another shape you can give HemeLB a weight for every inlet site
yourself, with a *weights file*.

## What HemeLB needs

1. **A build with velocity inlets and weights enabled** (see
   [CMakeOptions.md](CMakeOptions.md)):

   ```sh
   cmake ... -DHEMELB_INLET_BOUNDARY=LADDIOLET \
             -DHEMELB_OUTLET_BOUNDARY=NASHZEROTHORDERPRESSUREIOLET \
             -DHEMELB_USE_VELOCITY_WEIGHTS_FILE=ON
   ```

   Optionally add `-DHEMELB_COMPUTE_ARCHITECTURE=ISBFILEVELOCITYINLET`,
   which gives inlet sites a higher weight when the domain is shared between
   processes (tuned for Intel Sandy Bridge).

2. **An inlet in the XML with `type="velocity" subtype="file"`**:

   ```xml
   <inlet>
     <condition type="velocity" subtype="file">
       <path value="inlet0_velocity.txt" />
       <radius value="0.002" units="m" />
     </condition>
     <normal units="dimensionless" value="(0.0,0.0,1.0)" />
     <position units="m" value="(0.0,0.0,-0.05)" />
   </inlet>
   ```

   The velocity file has one `time velocity` pair per line (seconds and
   m/s); HemeLB interpolates between them.

3. **The weights file**, named after the velocity file with `.weights.txt`
   added (here `inlet0_velocity.txt.weights.txt`). Each line is

   ```text
   x y z weight
   ```

   where `x y z` are the lattice coordinates of an inlet site (whole numbers)
   and `weight` multiplies the velocity from the velocity file at that site.
   For each inlet boundary point HemeLB looks up the nearest site, stepping
   up to three sites along the inlet normal; if none of them is listed, the
   velocity there is zero. HemeLB stops with "File does not exist" if the
   weights file is missing. A small example is
   `Code/tests/resources/velocity_inlet.txt.weights.txt`.

The outlets still use pressure conditions, so they need `type="pressure"` in
the XML.

For a coupled `subtype="readWrite"` inlet, use runtime `weightsFilePath`
instead of this compile-time file-waveform option; see [coupling](coupling.md).

## Making the weights file

`geometry-tool/InletProcessing/CreateEmptyWeightsFile.py` lists the inlet
sites of a geometry and writes them with weight 0, for you to fill in:

```sh
cd geometry-tool/InletProcessing
python CreateEmptyWeightsFile.py profile.pr2 inlet0_velocity.txt.weights.txt
```

It reads the `.xml` and `.gmy` named in the profile, so generate those first.
This script dates from 2016 and still uses a VTK 5 call (`GetProducerPort`)
that current VTK versions do not have, so expect to update it before it runs.
It is not covered by the tests. Any other way of writing a file in the format
above works just as well.

## Then

Build HemeLB with the options above, add the `<properties>` you want saved
(see [XmlConfiguration.md](XmlConfiguration.md)), and run as usual.
