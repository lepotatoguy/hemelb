# Branching-vessel and collision-model CPU benchmarks

Recorded 2026-10-02. This comparison uses `feat/scalability-input-improvements`
with the MRT reconstruction repair at `fba2d6f9`, and HemePure CPU commit
`0bf67b16b23b41a06507810337a445f9916d62bf`. RBC is excluded at the user's request.

The MRT build exposed stale array/projection calls in equilibrium-only
reconstruction. The repair includes D3Q15/D3Q19 LBGK-equivalence checks for
that path; all six default CTest checks passed before the timing matrix.

## Workloads and controls

These are bundled branching fixtures, not a patient-specific validation set.
Each geometry is compared with D3Q19 LBGK, TRT and MRT, BFL walls and Nash
pressure iolets. Pressure driving is constant; every case writes one final
whole-domain pressure/velocity frame. Initial pressure, geometry, boundary
locations, voxel size and timestep come from the source case. The FiveExit
Windkessel outlets are replaced with zero-pressure outlets in both inputs;
these runs do not benchmark Windkessel behavior. Particle, visualization,
checkpoint and convergence-termination sections are removed from both inputs.

| Case | Fluid sites | Updates | Voxel size (m) | Timestep (s) |
| :--- | :--- | :--- | :--- | :--- |
| Six-branch vessel | 94323 | 600 | 2e-05 | 2e-06 |
| Bifurcation, lores fixture | 197431 | 300 | 6.667e-05 | 5e-05 |
| Bifurcation, hires fixture | 2010048 | 60 | 5e-05 | 1e-05 |

Across cases, timestep, driving pressure, physical dimensions and update count
can differ. This is a workload comparison, not a mesh-convergence study.
The larger case tests a larger site count with a short transient run; it does
not establish production throughput after reaching a steady state.

Machine: Apple M5, 16 GiB RAM, macOS 27.0, AppleClang 21.0.0 and Open MPI 5.0.9.
All six executables use scalar paths and `-O3 -DNDEBUG`. HemePure retains its
C++11 implementation and the existing macOS include/logging portability fixes;
the branch uses C++20. No relaxed floating-point option is added.

Both use NEUTRAL costs and disable ParMETIS refinement. The branch uses native
octree decomposition and its default automatic reader count; HemePure uses
its basic split and one compiled reader. These are end-to-end implementation
comparisons, not isolated collision-kernel measurements. Reader/decomposition
choices can affect setup and communication.

HemePure's explicit TRT parameter is set to `3/16`, matching the branch's
constant. For MRT, its explicit shear relaxation rate is set to `1/tau`, where
`tau = 0.5 + (dt * 0.004 / 1000) / ((1/3) * dx * dx)`, matching the branch's
viscosity-derived rate. Arbitrary legacy TRT/MRT `relaxation_parameter` values
are not validated by these runs; the branch's kernels use their native
parameter rules. This is a model-input difference, despite direct XML3 loading.

Three paired repeats alternate solver order at each worker count. Equal-worker
runs allocate 2/3/5 HemePure ranks and 1/2/4 branch ranks, because the reference
reserves rank zero. LBGK also has separate equal-allocation runs at 1/2/4 ranks.
Elapsed time includes MPI launch, geometry setup, monitoring and extraction.
Solver builds and simulation regression suites were completed before timing;
light metadata analysis and documentation preparation continued during runs.
Pilot validation and separate RSS-wrapper runs are excluded from timing medians.
Field agreement compares extracted outputs, not bitwise double-distribution
identity or independent physical accuracy. Phase maxima include the reference
reserved rank; MPI-wait maxima should not be interpreted as directly comparable
fluid-worker wait times. Overlapping timers must not be added.
No fixed CPU affinity or background-desktop-load isolation is claimed.

## Results

Speedup is HemePure median elapsed time divided by branch median elapsed
time, truncated to three decimal places. A value below one means the branch
took longer. These equal-worker runs are the primary comparison.

| Geometry | Collision model | 1 fluid worker | 2 fluid workers | 4 fluid workers |
| :--- | :--- | :--- | :--- | :--- |
| Six-branch | LBGK | 2.211× | 1.578× | 1.682× |
| Six-branch | TRT | 2.117× | 1.571× | 1.702× |
| Six-branch | MRT | 1.367× | 0.965× | 1.020× |
| Bifurcation, 197431 sites | LBGK | 1.500× | 1.456× | 1.596× |
| Bifurcation, 197431 sites | TRT | 1.943× | 1.529× | 1.559× |
| Bifurcation, 197431 sites | MRT | 1.199× | 1.000× | 0.996× |
| Bifurcation, 2010048 sites | LBGK | 1.380× | 1.236× | 1.311× |
| Bifurcation, 2010048 sites | TRT | 1.198× | 1.205× | 1.327× |
| Bifurcation, 2010048 sites | MRT | 1.211× | 0.917× | 0.963× |

LBGK and TRT had faster branch medians in all 18 tested equal-worker
configurations. MRT improved at one worker on all three geometries, but the
branch had slower medians in four multi-worker configurations. Near-one ratios
should be interpreted with the observed run variation, rather than as a
consistent advantage. The two-worker MRT regressions on the six-branch and
larger bifurcation cases had non-overlapping elapsed ranges in this series;
the smaller four-worker differences had overlapping ranges.

This suggests profiling multi-worker MRT and native partition/communication
behavior next. The timings do not identify a single cause. In particular,
phase boundaries and reserved-rank wait differ between implementations.

All 108 timing pairs and nine separate memory pairs passed the preset combined
absolute/relative field tolerances: pressure `5e-5 Pa`, velocity `1e-8 m/s`,
relative `1e-5`. Grid-coordinate sets and observed fluid-worker counts matched
the intended configurations. Maximum extracted-field differences across these
runs were `3.997918774700793e-05 Pa` and `6.705522537231445e-08 m/s`.

### Exact median elapsed times, equal workers

| Geometry | Model | Fluid workers | HemePure median elapsed (s) | Branch median elapsed (s) |
| :--- | :--- | :--- | :--- | :--- |
| Six-branch | LBGK | 1 | 8.715939833000004 | 3.940433250000005 |
| Six-branch | LBGK | 2 | 5.4953211659999965 | 3.480691582999995 |
| Six-branch | LBGK | 4 | 3.3740384999999975 | 2.0054263340000062 |
| Six-branch | TRT | 1 | 8.806760374999996 | 4.159474250000001 |
| Six-branch | TRT | 2 | 5.716867665999999 | 3.637791084000014 |
| Six-branch | TRT | 4 | 3.5226693750000067 | 2.0695874590000045 |
| Six-branch | MRT | 1 | 15.969805292000004 | 11.674239875000001 |
| Six-branch | MRT | 2 | 10.029392041999998 | 10.387860791999998 |
| Six-branch | MRT | 4 | 5.841952915999997 | 5.724744167000011 |
| Bifurcation, 197431 sites | LBGK | 1 | 8.402273124999997 | 5.600988624999999 |
| Bifurcation, 197431 sites | LBGK | 2 | 4.998410041999996 | 3.431027499999999 |
| Bifurcation, 197431 sites | LBGK | 4 | 3.0839371669999878 | 1.9312497079999957 |
| Bifurcation, 197431 sites | TRT | 1 | 8.662536208999999 | 4.457504708 |
| Bifurcation, 197431 sites | TRT | 2 | 5.6135114160000015 | 3.6711123749999928 |
| Bifurcation, 197431 sites | TRT | 4 | 3.3970199580000013 | 2.177760792000001 |
| Bifurcation, 197431 sites | MRT | 1 | 16.435407167000008 | 13.705193874999992 |
| Bifurcation, 197431 sites | MRT | 2 | 10.032953458000009 | 10.028355750000003 |
| Bifurcation, 197431 sites | MRT | 4 | 5.863794958 | 5.882789790999993 |
| Bifurcation, 2010048 sites | LBGK | 1 | 18.176223500000006 | 13.163557458999996 |
| Bifurcation, 2010048 sites | LBGK | 2 | 11.711796500000005 | 9.470232582999998 |
| Bifurcation, 2010048 sites | LBGK | 4 | 7.556493625000002 | 5.7638252499999965 |
| Bifurcation, 2010048 sites | TRT | 1 | 19.42929991599999 | 16.204740166 |
| Bifurcation, 2010048 sites | TRT | 2 | 11.320282500000005 | 9.387455166999999 |
| Bifurcation, 2010048 sites | TRT | 4 | 7.26498383400002 | 5.470861667000008 |
| Bifurcation, 2010048 sites | MRT | 1 | 31.798386084000004 | 26.252352541999997 |
| Bifurcation, 2010048 sites | MRT | 2 | 18.987744875000004 | 20.69804183299999 |
| Bifurcation, 2010048 sites | MRT | 4 | 11.567052042 | 11.999296875000027 |

### Equal allocated ranks, LBGK

| Geometry | Allocated ranks | HemePure fluid workers | Branch fluid workers | HemePure median elapsed (s) | Branch median elapsed (s) | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Six-branch | 1 | 1 | 1 | 5.977210165999999 | 3.945278791999999 | 1.515× |
| Six-branch | 2 | 1 | 2 | 8.187143833 | 3.5559778339999966 | 2.302× |
| Six-branch | 4 | 3 | 4 | 4.03421666700001 | 1.9855241249999978 | 2.031× |
| Bifurcation, 197431 sites | 1 | 1 | 1 | 5.527505542 | 4.047094749999999 | 1.365× |
| Bifurcation, 197431 sites | 2 | 1 | 2 | 8.229099792 | 3.6033974169999965 | 2.283× |
| Bifurcation, 197431 sites | 4 | 3 | 4 | 4.052404708000012 | 2.069009875000006 | 1.958× |
| Bifurcation, 2010048 sites | 1 | 1 | 1 | 13.900292791999995 | 13.063124166999998 | 1.064× |
| Bifurcation, 2010048 sites | 2 | 1 | 2 | 16.993946290999986 | 9.189051708000008 | 1.849× |
| Bifurcation, 2010048 sites | 4 | 3 | 4 | 8.21628183300001 | 5.391202916999987 | 1.524× |

### Separate memory samples, LBGK

One paired run per geometry/worker count measured solver-child lifetime peak
RSS. The table shows the largest recorded rank peak, including all allocated
ranks. It is neither simultaneous total memory nor a separately isolated
loop-memory measurement. These wrapper runs do not contribute timing medians.

| Geometry | Fluid workers | HemePure maximum rank peak RSS (bytes) | Branch maximum rank peak RSS (bytes) |
| :--- | :--- | :--- | :--- |
| Six-branch | 1 | 163332096 | 167395328 |
| Six-branch | 2 | 96419840 | 97845248 |
| Six-branch | 4 | 62210048 | 64552960 |
| Bifurcation, 197431 sites | 1 | 326959104 | 329023488 |
| Bifurcation, 197431 sites | 2 | 177897472 | 186941440 |
| Bifurcation, 197431 sites | 4 | 103317504 | 105725952 |
| Bifurcation, 2010048 sites | 1 | 2090795008 | 2167767040 |
| Bifurcation, 2010048 sites | 2 | 1105166336 | 1238482944 |
| Bifurcation, 2010048 sites | 4 | 490323968 | 649805824 |

HemePure's maximum per-rank peak was lower in all nine recorded configurations.
The larger case at four workers recorded `490323968` bytes for HemePure and
`649805824` bytes for the branch. These are single memory samples per
configuration, not a general memory ranking across platforms or workloads.
Together with the timing results, they warrant investigating setup/storage
memory alongside multi-worker MRT before claiming a broad performance lead.

### Executable hashes

- HemePure LBGK: `86038964e80296a72f4c2eee52f5a959838bc2a4bd017aa5da9988bf718e7c26`.
- HemePure TRT: `d73a3e953d41b216572949b8aca2d4e66339103dbd7d8c249e4d7db9754a5e85`.
- HemePure MRT: `e992d329dd1280228c4b1f7477a4945848fe27b12e05ac77348f21da0bede15d`.
- Branch LBGK: `77074b4e5a5af520fad2e9b4b0cc48a6e419bf1177fa9c9ea87dea9d42fa9149`.
- Branch TRT: `8bb36bbebf371f428f4bd376c2bd108697d53b6da46c34f6ec16e2292d68c648`.
- Branch MRT: `73d17f933d68904e611672ed0582ffc8bce0f537e48c716eae1ae8fbf4b39100`.

## Reproduce the inputs and a comparison

Activate the Python-tools environment, build one executable per kernel for
both solvers, and verify the actual compiler flags. The historical small-cylinder
HemePure build omitted `-DNDEBUG` despite its Release label. Its recorded scores
are retained with that qualification; this matrix aligns the assertion setting.

From the repository root:

```sh
python Scripts/prepare-cpu-benchmarks.py --output prepared-cpu-cases
python Scripts/compare-cpu-solvers.py \
  --reference /path/to/HemePure-LBGK \
  --candidate /path/to/hemelb-LBGK \
  --reference-config prepared-cpu-cases/sixbranch/LBGK/reference.xml \
  --candidate-config prepared-cpu-cases/sixbranch/LBGK/candidate.xml \
  --reference-pressure-unit mmHg --candidate-pressure-unit Pa \
  --reference-rank-offset 1 --ranks 1 2 4 --repeats 3 \
  --pressure-atol 5e-5 --velocity-atol 1e-8 --rtol 1e-5 \
  --output sixbranch-LBGK-workers
```

Repeat for TRT/MRT and the two bifurcation inputs. Omit
`--reference-rank-offset 1` for equal allocations. Use a separate output
folder and `--peak-rss --repeats 1` for memory recording; wrapper timings
must not be merged into timing medians. Input preparation refuses an existing
output folder and writes geometry hashes, site counts and parameter values
into `cases.json`. All 18 generated XML files reproduced the executed inputs
byte-for-byte in the local reproduction check.

Raw inputs, executable hashes, logs, per-run JSON and the summary CSV are in
ignored `research/cpu-representative/`. The preparation/comparison scripts
and this measurement record are tracked; raw research artifacts are excluded.
