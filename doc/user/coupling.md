# Exchange flow and pressure with another program

Use file-based read/write coupling when an external program supplies flow and
needs sampled pressure in return. Build the coupled inlet with
`HEMELB_INLET_BOUNDARY=LADDIOLET`; a pressure outlet can keep the default Nash
boundary. This workflow requires no MPWide dependency. The separate upstream
multiscale interface needs `HEMELB_BUILD_MULTISCALE=ON` and MPWide; see
[its implementation notes](../../Code/multiscale/README.md).

## Configure a coupled inlet

Replace the inlet condition with this version 6 example, keeping the normal
and position matched to the geometry:

```xml
<condition type="velocity" subtype="readWrite" pressure_units="Pa" timeout_s="60">
  <radius value="0.0005" units="m" />
  <area value="7.853981633974483e-7" units="m^2" />
  <frequency value="10" units="lattice" />
  <flowRateFilePath value="exchange/flow.txt" />
  <pressureFilePath value="exchange/pressure.txt" />
  <flowRateConversionFactor value="1" units="dimensionless" />
  <pressureConversionFactor value="1" units="dimensionless" />
  <smoothingFactor value="1" units="dimensionless" />
</condition>
```

The values illustrate the schema. Choose geometry, exchange frequency, and
conversion factors for your peer. With unit factors, input flow is in m³/s
and output pressure is in Pa. Paths are relative to the configuration XML.
Create the exchange directory and initial flow record before launching HemeLB.

## Exchange protocol

A `type="velocity" subtype="readWrite"` condition accepts HemePure's fields:
`radius` (m), `area` (m²), `frequency` (lattice), `flowRateFilePath`,
`pressureFilePath`, `flowRateConversionFactor`, `pressureConversionFactor`, and
`smoothingFactor` (dimensionless). Paths use a `value` attribute and are relative
to the XML. Factors and smoothing must be finite; smoothing lies between 0 and 1.

The peer writes one ASCII record containing physical time and flow. HemeLB
converts flow to the peak parabolic speed, smooths it, and writes the next
exchange time and sampled pressure through an atomic file rename. New v6 inputs
use Pa by default; legacy inputs retain mmHg. Set `pressure_units="Pa"` or
`"mmHg"` explicitly when exchanging data with another program.

Initial speed and the time origin come from the first flow-file record. The first
exchange occurs at update 2 after warmup and reads that initial peer timestamp.
The new speed takes effect after that update, matching the source's pre/post
stream ordering. `timeout_s` defaults to 60 and bounds the wait for a peer.
Pressure writes no longer require an existing placeholder file. Checkpoints
retain speed, time origin, next exchange, and last sampled density. Optionally supply
`weightsFilePath` containing `x y z weight` records in lattice coordinates. The
profile then uses those nonnegative weights and normalises flow by their sum
times voxel area, supporting noncircular openings without a compile-time switch.

A flow file contains one record, for example:

```text
0 1e-9
```

The first column is the peer's physical time in seconds. HemeLB reads that
initial record during setup. During each exchange, the peer must supply the
expected timestamp, then read the pressure response and prepare the next flow
record. Replace peer-written files atomically so HemeLB sees a complete record.
Run the peer and HemeLB concurrently; an initial file alone is insufficient
for a simulation that reaches later exchange times.

Use the mock peer in
[cpu_boundary_models_mpi.py](../../Code/tests/cpu_boundary_models_mpi.py) as a
protocol example. It is a regression driver, not a production circulation model.
To exercise the protocol with a matching installed solver:

```sh
python3 Code/tests/cpu_boundary_models_mpi.py --hemelb /path/to/velocity-hemelb --model readWrite
python3 Code/tests/cpu_boundary_models_mpi.py --hemelb /path/to/velocity-hemelb --model weightedReadWrite
```

## Restart and limits

Resume from the generated `Checkpoints/<step>/restart.xml` to retain coupling
state. Restart or reconnect the peer at the saved exchange timestamp and keep
its own state consistent with HemeLB. A fluid-only legacy checkpoint cannot
reconstruct an external peer's state. See [checkpoints](checkpoints.md).

Local regressions use a mock file peer and rank-changing restart. Production
peer interoperability and the optional shared multiscale interface remain
unverified; do not infer their validation from the file-protocol tests.
