# CPU performance and measured optimizations

Updated 2026-10-02. The solver advances fluid sites in C++ and communicates
through MPI. NumPy is useful for Python geometry/analysis operations; the
solver hot loop already runs as compiled native code.

## Changes measured against the previous branch code

The baseline is `b746135b`, built before these changes. This optimization:

- Accumulates scalar density and momentum in local variables, then writes
  the output references once. The per-direction accumulation order is retained.
- Makes the wall/iolet link bit-mask checks inline, so streamers can avoid
  a separate function call for every direction. The rest-direction rule and
  mask encoding are unchanged.

Both changes retain double precision, distribution storage, input conventions
and checkpoint formats. Scalar reductions apply when SSE3/AVX paths are not
selected; the inline boundary checks also apply to vector builds.

An eight-second macOS sampling profile identified density/momentum/equilibrium
calculation, bulk streaming and wall-link checks among the main CPU hotspots.
Background-thread idle samples were excluded from that interpretation.

## Measured results

Machine: Apple M5, macOS 27.0, AppleClang 21.0.0, Open MPI 5.0.9. Both binaries
use Release `-O3 -DNDEBUG`, D3Q19/LBGK/BFL/Nash pressure, scalar paths, NEUTRAL
decomposition costs and octree decomposition. Each paired run uses the same
GMY/XML, monitoring and one final pressure/velocity extraction.

The tables show median loop seconds from five alternating baseline/optimized
repeats. They exclude build time and MPI launch/setup; the loop includes
monitoring and extraction. No builds or tests ran during these final timing
series. The 2400-site case uses 6000 updates; the 18960-site case uses 3000.

| Fluid sites | MPI ranks | Baseline loop seconds | Optimized loop seconds | Result |
| :--- | :--- | :--- | :--- | :--- |
| 2400 | 1 | 1.44632 | 1.0973279999999999 | Faster |
| 2400 | 2 | 1.4555529999999999 | 1.081884 | Faster |
| 2400 | 4 | 0.8433799999999999 | 0.9561470000000001 | Slower |
| 18960 | 1 | 3.851935 | 3.414465 | Faster |
| 18960 | 2 | 3.3829059999999997 | 2.980272 | Faster |
| 18960 | 4 | 2.11722 | 1.7998230000000002 | Faster |

The refined cylinder improved at all tested rank counts. The smaller cylinder
improved at one and two ranks but regressed at four in this isolated repeat.
Earlier screening showed a four-rank improvement; these mixed results prevent
a consistent four-rank speed claim for that small case. Its isolated median
MPI-wait timer increased from 0.283 to 0.414 seconds. That identifies a timing
difference, not proof of its cause or a general communication regression.

A longer follow-up on the 2400-site case used 24000 updates at four ranks,
again with five alternating paired repeats. Its median loop time decreased
from 3.7019870000000004 to 3.261469 seconds; median MPI wait decreased from
1.32 to 1.24 seconds. This supports the longer run on this host, while the
short-run variation still warrants measuring the intended workload.

Every paired final pressure/velocity comparison in these tables and the
longer follow-up matched exactly: 35 pairs across the two geometries, with
zero maximum differences.
A separate baseline/optimized checkpoint comparison also matched every saved
double distribution at one and two MPI ranks. Equality applies to the tested
cases and executables, not every compiler or optional model.

The rebuilt default configuration passed all six CTest checks, including
legacy input, geometry setup and checkpoint/restart MPI checks. The scalar
arithmetic verifier passed D3Q15, D3Q19, D3Q27 and D3Q15i. Ten rebuilt optional
model checks passed their finite-state and two-to-one-rank restart checks:
Yang, Yang corners, elastic walls, LBGK/TRT/LES sponge, elastic Womersley,
read/write, weighted read/write and periodic velocity. These regression
checks do not establish a new physical-validation result. RBC and native
x86 builds were not rerun locally for this optimization.

Executable SHA256 values:

- Baseline: `c3f72fd515a3b7cee5192a3139720093c701bfba58f5a1b2bbd9784a3e178f32`.
- Optimized: `77074b4e5a5af520fad2e9b4b0cc48a6e419bf1177fa9c9ea87dea9d42fa9149`.

Raw inputs, timing JSON, profiling output and validation logs remain in ignored
`research/cpu-optimization/`. Initial single-change and combined screening
measurements are retained there; the tables use the isolated confirmation and
refined-case series, with the longer four-rank follow-up reported separately.

See the [branching-vessel and collision-model matrix](representative-cpu-benchmarks.md)
for subsequent comparisons with HemePure, including larger domains and MRT.

## Reproduce a baseline comparison

Build both revisions with the same compiler, dependencies, kernel, lattice,
boundary implementations, vector settings and optimization flags. Use a
separate checkout/build for the baseline and retain its executable.

From the repository root, prepare a single-frame input using the bundled case:

```sh
python - <<'PYCODE'
from pathlib import Path
import xml.etree.ElementTree as ET

tree = ET.parse("examples/first-run.xml")
sim = tree.find("simulation")
sim.find("steps").set("value", "6000")
checkpoint = sim.find("checkpoint")
if checkpoint is not None:
    sim.remove(checkpoint)
tree.find("geometry/datafile").set(
    "path", str(Path("examples/first-run.gmy").resolve()))
properties = tree.find("properties")
properties.clear()
output = ET.SubElement(properties, "propertyoutput", file="whole.xtr",
                       period="1", start="5999", stop="5999")
ET.SubElement(output, "geometry", type="whole")
ET.SubElement(output, "field", type="velocity")
ET.SubElement(output, "field", type="pressure")
ET.SubElement(tree.getroot(), "decomposition", method="octree")
tree.write("benchmark.xml", encoding="utf-8", xml_declaration=True)
PYCODE

python Scripts/compare-cpu-solvers.py \
  --reference /path/to/baseline/hemelb \
  --candidate /path/to/optimized/hemelb \
  --reference-config benchmark.xml --candidate-config benchmark.xml \
  --reference-pressure-unit Pa --candidate-pressure-unit Pa \
  --ranks 1 2 4 --repeats 5 \
  --pressure-atol 0 --velocity-atol 0 --rtol 0 \
  --output cpu-comparison
```

Activate the installed Python-tools environment first. The benchmark script
alternates execution order, records executable/input/geometry hashes and
compares fields by grid coordinate. It refuses an existing output directory.
Use separate `--peak-rss` runs for memory; wrapper timings should not be mixed
with unwrapped timings. The recorded coarse experiment used equivalent legacy
XML3, exercising direct loading; this preparation uses the bundled XML6.

For a refined case, regenerate the STL/profile with a smaller voxel size, pair
it with its newly generated XML, and select the same final frame for both
binaries. Record the timestep and site count rather than inferring them.

## Choose the next optimization from measurements

Read the existing [performance report definitions](../user/scalability-and-inputs.md).
Compare calculation, MPI-wait and extraction timers, imbalance, halo volume
and peak RSS on the geometry you intend to run. Phase timers can overlap, so
do not add them to estimate total time.

- If calculation dominates, profile the bulk and boundary streamers and assess
  cache locality, compiler vectorization and distribution layout.
- If MPI wait dominates, compare rank counts and octree/ParMETIS partitions;
  fewer ranks can be better for a small domain.
- If output dominates, adjust extraction regions/cadence and checkpoint periods
  to the data and recovery interval the study needs.
- If setup dominates, measure runtime reader count/spacing and compressed-block
  caching on the target filesystem.

The branch already supplies optional AVX2/AVX512 implementations on x86. These
do not run natively on Apple silicon; this work uses its portable scalar path.
Arithmetic checks alone do not establish full-solver vector speed. Avoid
changing precision or enabling relaxed floating-point rules merely to obtain
a faster timing. Check numerical and restart behavior after each change.

A subsequent [direct comparison with HemePure](comparison-and-roadmap.md#current-optimized-branch-vs-hemepure)
measures optimized commit `bab60b0f` against the reference at equal allocations
and fluid-worker counts. Its elapsed-time scores are also in the
[changelog](../../CHANGELOG.md#cpu-comparison-scores).

See [CPU verification](cpu-verification.md) for the regression commands and
[the HemePure comparison](comparison-and-roadmap.md) for model/scaling limits.
