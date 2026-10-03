# CPU comparison and implementation status

Updated 2026-10-02. The original source audit and CPU measurements compare
`feat/scalability-input-improvements` at `b746135bd212b23cd2dd1df703c1d7635d08a4fe` with the
[official HemePure CPU repository](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/README.md) at
`0bf67b16b23b41a06507810337a445f9916d62bf`. Both the official repository and the local
HemePure fork resolve to that reference commit. The feature branch is based on
HemeLB upstream `432d3386` and includes the `fix/hemelb-improvements` changes.

This branch has stronger input compatibility, portable restarts, runtime
controls, diagnostics, and an integrated geometry/analysis workflow. Its rebuilt
CPU executable finished faster in every historical small-cylinder configuration
measured below, with the reference assertion-setting difference now disclosed.
The newer [branching-vessel matrix](representative-cpu-benchmarks.md) aligns
`-DNDEBUG` and shows faster LBGK/TRT medians, mixed multi-worker MRT, and higher
branch maximum per-rank memory peaks in the separate LBGK samples. Large-cluster
scaling has not been compared. The original nine implementation priorities
are covered;
that does not establish complete parity with every optional HemePure path.

Subsequent [CPU optimization measurements](cpu-performance.md) compare the
branch baseline with local-reduction and inline-boundary changes, including
a small four-rank case with a slower timing. The earlier HemePure tables remain
pinned to the pre-optimization executable and commit stated above. A fresh
[current optimized comparison](#current-optimized-branch-vs-hemepure) below
measures `bab60b0f` directly against the same HemePure reference with historical
assertion settings. The branching-vessel record is the broader comparison at
`fba2d6f9`, with aligned assertions and the MRT reconstruction repair.

## Where this branch is better

| Area | Feature branch | HemePure CPU reference | Evidence or limit |
| :--- | :--- | :--- | :--- |
| Existing inputs | Direct XML3/5/6 loading, including decimal/hexadecimal values and legacy units/positions | XML3 reader | [Compatibility checks](../../Code/tests/legacy_compatibility_mpi.py); supported model/build combinations still required |
| Checkpoint portability | Reads extraction/checkpoint4/5/6; redistributes sites to the current decomposition | v4 loader requires the saved rank count and site ordering | [Actual HemePure checkpoint check](../../Code/tests/hemepure_checkpoint_mpi.py): 2 ranks to 1 and 3 |
| Model restart state | Saves Windkessel history, coupling time/state, and passive tracers in restart XML | Legacy fluid checkpoint does not reconstruct all that model state | [Model and restart checks](../../Code/tests/cpu_boundary_models_mpi.py), [tracer checks](../../Code/tests/cpu_features_mpi.py) |
| Geometry I/O controls | Cached compressed blocks through redistribution; runtime decomposition, reader count and spacing | Sparse block metadata already present; reader/decomposition controls are compiled in, and the ParMETIS path rereads blocks | [Reader](../../Code/geometry/GeometryReader.cc) versus [reference reader](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/geometry/GeometryReader.cc); cluster benefit unverified |
| Input diagnostics | Checks bounds, decompression, finite values, iolet IDs, weights and waveform records | Some geometry checks disabled; narrower validation paths | Named rejection checks; no exhaustive fault campaign against the reference |
| Model configuration | Runtime LES coefficient, weighted read/write profiles, bounded coupling waits, explicit periodic physical-time waveforms | Fixed LES coefficient, compile-time weighting switch, legacy file timing/coupling behavior | [CPU models](../user/cpu-models.md), [coupling](../user/coupling.md); equation differences listed below |
| Analysis workflow | Explicit units in v7 output, v4/v5/v6/v7 Python readers, installed CSV/ParaView commands | Legacy output with separately maintained preprocessing/extraction tools | [Python tools](../user/python-tools.md) and [root examples](../../examples/README.md) |
| Run diagnostics | Completed updates, MLUPS, imbalance, halo volume, setup/lifetime peak RSS | Phase timers and memory logs | [Metric definitions](../user/scalability-and-inputs.md); instrumentation alone does not make a run faster |
| Installation and regression coverage | Automated Linux/macOS installation, GNU build matrix, CPU model and MPI restart checks | Site-oriented FullBuild script; unit tests disabled by default | Verified GitHub evidence below; this does not measure scientific accuracy |
| Additional HemeLB modules | RBC builds and parallel regressions pass in CI; local MPWide multiscale build/mock checks pass | No corresponding RBC or multiscale module in this checkout | [Build options](../../CMake/GlobalOptions.cmake); production coupled physics remains unverified |
| Measured local runtime | Faster LBGK/TRT medians on the branching matrix | Faster in four tested multi-worker MRT configurations | [Broader matrix](representative-cpu-benchmarks.md); historical cylinder scores below have different assertion settings |

## CPU features available in both

The following are feature parity, rather than unique advantages of this branch.
Availability does not imply that every lattice/kernel/boundary combination has
been validated or that all model outputs are numerically identical.

| Capability | Comparison |
| :--- | :--- |
| Lattices and existing collision models | Both source trees contain D3Q15/19/27/15i, LBGK, TRT, MRT, entropic and non-Newtonian options. HemePure's README emphasizes D3Q19, but its CMake interface exposes the other lattices. |
| Rigid walls and standard iolets | Both contain bounce-back, BFL, GZS, Nash pressure, parabolic/file velocity and Womersley profiles. |
| Windkessel outlets | Both implement WK2, WK3 and fileWK. This branch adds validated state saving and stricter input handling. |
| Sponge and LES | Both contain LBGK/TRT sponge and Smagorinsky LES with sponge; coefficient controls and overlap rules differ. |
| Yang and elastic models | Both contain Yang pressure, GZS elastic walls and elastic Womersley velocity. This branch restricts Yang to LBGK, valid interior stencils and `tau >= 0.8`. |
| Velocity coupling | Both contain read/write flow/pressure exchange and weighted profiles. Production peer interoperability has not been compared. |
| Extraction | Both contain line/plane/sphere/surface/iolet selectors, start/stop windows, normal traction and wall extension. The branch's format metadata and tools are additional functionality. |
| Weighted geometry decomposition | Both support GMY+ computational weights. This branch selects octree/ParMETIS and readers at runtime. |
| CPU vector paths | Both contain SSE3, AVX2 and AVX512 implementations. Native arithmetic checks and measured solver speed are separate evidence. |
| TRT/MRT parameter rules | HemePure reads explicit legacy `relaxation_parameter`; the branch uses fixed TRT `3/16` and viscosity-derived MRT rates. The benchmark preparation matches those rates explicitly; arbitrary legacy parameter parity remains a gap. |

[HemePure's CMake interface](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/CMakeLists.txt),
[model build interface](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/BuildSystemInterface.h), and
[parser](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/configuration/SimConfig.cc) substantiate the reference
capabilities. Corresponding branch controls are in
[HemeLbOptions.cmake](../../CMake/HemeLbOptions.cmake) and
[SimConfig.cc](../../Code/configuration/SimConfig.cc).

## Where HemePure still has an advantage or a missing counterpart

The [branching-vessel measurements](representative-cpu-benchmarks.md) also show
lower HemePure maximum per-rank peak RSS in all nine recorded LBGK memory
samples and faster medians in four multi-worker MRT configurations. Some small
timing differences have overlapping repeat ranges; cluster-wide superiority
is not established.

| Area | HemePure CPU reference | Feature branch | Status |
| :--- | :--- | :--- | :--- |
| Shared geometry ownership metadata | Optional `HEMELB_USE_MPI_WIN` shares ownership arrays within a node | Active metadata and ownership structures remain replicated | Confirmed source gap. The reference README warns about implementation/platform limitations; its memory benefit has not been measured here. |
| Legacy particle distribution/output | ParticleSet contains ownership migration, neighbor communication and particle XDR output | New maintained passive tracers replicate their list and cap it at 100,000; old colloid code remains unmaintained | Source-level scalability/workflow gap. Neither active-colloid implementation was physically validated in this audit. |
| CPU production/scaling history | README reports CPU runs at large core counts and lists applications | Comparative measurements here use one workstation | Reference-reported experience; publication/scaling claims were not independently audited. No cluster winner established. |
| Existing ecosystem | Existing case scripts and external HemePure tools consume its legacy formats | New output is v7 with mmHg pressure and passive-tracer output is CSV | This branch reads old inputs/checkpoints; older binaries and external tools are not guaranteed to read its new output. |

The reference also exposes an optional `HEMELB_USE_BIGMPI` header-reading path
without a direct branch option. This branch instead scans/broadcasts bounded
header chunks. That is an implementation difference, not evidence of a missing
ability to read large geometry, nor a measured performance advantage.

The shared-memory and particle rows are concrete remaining CPU work. They
prevent a claim that every HemePure implementation has been brought over.
Native SIMD solver performance, cluster scaling, and physical validation remain
open comparisons for both implementations.

## Numerical and compatibility limits

Direct loading preserves XML3/5 unit and position conventions for supported
inputs. It does not promise identical trajectories for every optional model:
sponge overlap selects the nearest outlet instead of multiplying damping
factors, LES has explicit coefficient/density handling, and Yang rejects the
observed unstable relaxation range. These choices and defaults are documented
in [CPU models](../user/cpu-models.md).

Passive tracers use CSV rather than legacy particle XDR. `windkessel/GKmodel`
remains a supplied-case/source mismatch, not an alias for WK2. Shared
experimental virtual-site and active-colloid code remains outside the validated
model set. Restarting an old fluid-only checkpoint cannot recover outlet or
particle state that the original writer did not save. New v7 outputs are not
promised to work in older binaries or extraction tools.

## Priority list, implemented work and checks

| Priority | Implementation | Validation | Remaining limit |
| :--- | :--- | :--- | :--- |
| 1 | Matched CPU harness, RSS option, equal worker/allocated rank modes; CI accepts `feat/**` | Repeated reference/candidate outputs and timings below | Main application CI passed; HPC scaling unverified |
| 2 | Bundled STL/profile/GMY/XML, v6 example and `hlb-extracted-to-vtk` | Geometry regenerated byte-identically, generated legacy XML loaded directly, results exported, checkpoint resumed | Viewer interaction not automated |
| 3 | WK2, WK3/RCR and fileWK; units, reduction timing and saved state | Independent 0D and pulsatile timestep refinement; 2-to-1 MPI restarts | Area-average flow estimator follows active source equations; fileWK profile path is preserved but does not change that estimator |
| 4 | Sphere and surface-within-sphere selection | Analytic site sets, origin/radius bounds and roundtrip tests | Production-case sampling still needs study-specific checks |
| 5 | LBGK/TRT sponge and configurable Smagorinsky LES | Independent relaxation/stress/conservation checks, MPI restarts, pulse diagnostic below | Diagnostic does not establish a universal reflection reduction |
| 6 | Yang pressure, GZS elastic wall, elastic Womersley; optional independent corner rules | Bessel integral/derivative and wall limits, MPI restarts, Yang reference comparison and two resolutions | Yang restricted to straight iolets, LBGK and tested tau range; no full compliant-vessel validation |
| 7 | Read/write coupling, initial peer time, weighted flow normalisation, bounded waits, passive tracers | Conversion/timing/smoothing/timeout tests, kernel moments, emission/boundary rules, no fluid feedback, rank-changing restart | External production peer unverified; replicated particle limit 100,000 |
| 8 | GMY+ computational costs in octree/ParMETIS, runtime reader throttling, bounded owner cache | Unequal-cost geometry/state tests, existing sparse-box tests, report/setup/RSS measurements | Replicated active metadata and header scan remain; no whole-body cluster measurement |
| 9 | Optional AVX2/AVX512 paths, scalar tails; existing scalar/SSE3 defaults retained | Native AVX2 arithmetic check passed in CI; AVX512 compiled and returned skip code 77 | AVX512 execution and full-solver SIMD speed missing; distribution storage and indices unchanged |

The originally identified CPU parser/model gaps are covered: periodic file velocity, weighted read/write, initial coupling timestamp,
legacy passive-tracer spellings and boundary rules, normal traction, wall extension,
and explicit wall/inlet and wall/outlet combinations. HemeLB's existing collision,
non-Newtonian, rigid-wall, pressure/velocity and convergence options remain.
See [CPU model parameters and limits](../user/cpu-models.md).

## Executed validation

The default D3Q15/LBGK/simple-bounce-back pressure build passed all six CTest
targets. A multiscale build with MPWide also passed all six, including mock
coupling tests in the C++ suite. Optional D3Q19 builds cover Yang, elastic walls,
LBGK/TRT/LES sponge, velocity coupling and explicit Yang corners. Their ten
model runs check positive finite distributions and 2-to-1 checkpoint continuation.
The earlier model audit recorded 55 Python tests. After the compatibility and
CI fixes, the combined Python/geometry selection passed 126 local tests.

The actual HemePure v4 checkpoint test checks exact float promotion, then resumes
on 1 and 3 ranks from a 2-rank reference run. Continuation differed by zero between
the candidate rank counts. Synthetic legacy version fixtures also test offsets,
geometry/lattice mismatch rejection, and XML 3/5 units. Legacy camera-only
`visualisation` sections are ignored when loading XML 3/5; new v6 schema remains
strict. This changes no fluid parameters.

Yang at tau 0.62 became unstable on the cylinder in both implementations. The
reference aborted at update 805. The candidate now rejects tau below 0.8 with a
named error rather than relying on a short smoke test. At tau 0.8, a 3000-update
reference/candidate comparison passed the preset tolerances. Maximum differences
were 3.882632872320713e-06 Pa and 2.0023435354232788e-08 m/s. This does not imply stability on
every geometry at tau 0.8 or higher.

The independent Poiseuille check uses fixed 5% normalised RMS tolerance and
requires the finer grid to improve. At tau 0.8:

| Voxel size (m) | Time step (s) | Updates | Fluid sites | Normalised RMS velocity error |
| :--- | :--- | :--- | :--- | :--- |
| 0.0001 | 0.00025 | 1200 | 2400 | 0.043259937316179276 |
| 5e-05 | 6.25e-05 | 4800 | 18960 | 0.01475728489458561 |

The sponge diagnostic runs a finite viscous pipe with a 1 Pa pulse and measures
plane-averaged characteristic pressure in fixed incident/returned windows.
The returned-to-incident peak ratio was 0.38293850448908706 without sponge and
0.3565071450885767 with LBGK sponge. This is one finite-pipe diagnostic, not a calibrated
reflection coefficient or a guarantee for other outlets.

### GitHub evidence

[Main application run 36966075537](https://github.com/lepotatoguy/hemelb/actions/runs/36966075537)
passed all 14 jobs on the compared branch commit, including fluid-only and RBC
GNU 11/12/13 builds, parallel RBC regressions, and the optional CPU model matrix.
Its [Yang job](https://github.com/lepotatoguy/hemelb/actions/runs/36966075537/job/110709998697)
executed the AVX2 density/momentum/equilibrium check for D3Q15/19/27/15i.
AVX512 compiled but reported `SKIP: AVX512F unavailable`. No full-solver SIMD
speed comparison follows from these arithmetic checks.

The preceding solver/input commit `6db58a5b` passed all 30 checks across
[main application](https://github.com/lepotatoguy/hemelb/actions/runs/36962501930),
[installation](https://github.com/lepotatoguy/hemelb/actions/runs/36962501871),
[geometry](https://github.com/lepotatoguy/hemelb/actions/runs/36962501914), and
[Python tools](https://github.com/lepotatoguy/hemelb/actions/runs/36962501875).
The subsequent example relocation triggered the main/installation workflows;
the earlier geometry/Python runs are evidence for their checked commit, not
fresh runs on the new layout. RBC CI success establishes build/regression
coverage, not validated use of the new fluid models in a resolved-cell study.

## Current optimized branch vs HemePure

Measured on 2026-10-02: optimized branch commit `bab60b0f` versus HemePure
CPU commit `0bf67b16b23b41a06507810337a445f9916d62bf`. Apple M5, macOS 27.0,
AppleClang 21.0.0, Open MPI 5.0.9, Release `-O3`, scalar
D3Q19/LBGK/BFL/Nash pressure, NEUTRAL costs and ParMETIS disabled. The branch
used `-DNDEBUG`; HemePure retained assertions in this historical series. The cylinder
has 2400 fluid sites and runs 3000 updates with one matched field frame.
Each score is the median of three alternating paired runs; elapsed time
includes MPI launch, setup, monitoring and output. No builds or tests ran
concurrently.

Speedup is HemePure median elapsed time divided by branch median elapsed time,
truncated to three decimal places. HemePure reserves one rank when multiple
ranks are allocated, so equal fluid-worker counts are shown first:

| Fluid workers | HemePure allocated ranks | Branch allocated ranks | HemePure elapsed (s) | Branch elapsed (s) | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 2 | 1 | 1.6932546669999997 | 0.9896887909999998 | 1.710× |
| 2 | 3 | 2 | 1.1530365829999987 | 0.7254317500000003 | 1.589× |
| 4 | 5 | 4 | 1.2137617079999998 | 0.6344771250000001 | 1.913× |

Equal allocated MPI rank counts:

| Allocated ranks | HemePure fluid workers | Branch fluid workers | HemePure elapsed (s) | Branch elapsed (s) | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1 | 1 | 1.82107325 | 1.0320087920000005 | 1.764× |
| 2 | 1 | 2 | 1.8430617090000005 | 0.7372506249999997 | 2.499× |
| 4 | 3 | 4 | 1.0589593750000006 | 0.6180433329999993 | 1.713× |

All 18 paired pressure/velocity comparisons passed the prescribed combined
absolute/relative tolerances: pressure `5e-5 Pa`, velocity `1e-8 m/s`, relative
`1e-5`. Maximum differences were `4.487413297837861e-06 Pa` and
`2.60770320892334e-08 m/s`. These scores apply to this small cylinder and build;
they do not establish cluster scaling or a universal speed advantage.

Executable SHA256 values for these current timing runs:

- HemePure: `b5774e518c14b734cb24dbc13cde2b2971a58b7cb4669b093e62a4522ab13c58`.
- Branch: `77074b4e5a5af520fad2e9b4b0cc48a6e419bf1177fa9c9ea87dea9d42fa9149`.

Inputs and executable hashes, per-run timings, field comparisons and logs are
retained in ignored `research/cpu-comparison-bab60b0f/`. Inputs match the
previous comparison below. The separate memory samples below belong to the
previous `b746135b` executable. Current and historical timing series were run
at different times; do not combine them to estimate the optimization's effect.
The [branch-only optimization comparison](cpu-performance.md) provides that
measurement with alternating baseline/optimized pairs.

## Earlier matched CPU measurements at `b746135b`

These runs used a freshly rebuilt candidate from the compared branch commit.
Machine: Apple M5, macOS 27.0; both solver executables are arm64. Build settings:
AppleClang 21.0.0 (`clang-2100.1.1.101`), Release `-O3`, Open MPI 5.0.9,
D3Q19/LBGK/BFL/Nash pressure, scalar paths and NEUTRAL cost weights. The branch
used `-DNDEBUG`; the reference retained assertions. Both disable ParMETIS;
the candidate uses octree decomposition and the reference
uses its default basic decomposition. The reference MPI shared-memory option
is disabled, so these measurements do not evaluate that optional path. The reference uses C++11 and candidate C++20. The reference
binary retains the previously applied `cstddef` include and macOS logging
portability fixes; those changes did not alter its fluid equations. The Python
runner uses Rosetta, which explains its x86_64 platform string.

Both inputs are legacy XML3 with the same prepared cylinder GMY: 2400 fluid
sites, voxel size 0.0001 m, time step 0.0001 s, 3000 configured timesteps,
constant pressure difference 1.333223874 Pa, and one matched field frame.
HemePure labels the frame 3000 and HemeLB labels it 2999. The reference input
has its required radius/area metadata; the candidate uses its default Nash
iolet schema. No builds or tests ran concurrently with these timing repeats.

Each timing mode ran three alternating reference/candidate repeats at each rank
count. All 18 paired field comparisons passed the same preselected tolerances:
absolute pressure 5e-5 Pa, absolute velocity 1e-8 m/s, relative tolerance 1e-5.
Maximum differences were 4.487413297837861e-06 Pa and
2.60770320892334e-08 m/s. These are combined absolute/relative comparisons,
not a claim of bitwise equality or an independent physical-accuracy test.

HemePure reserves rank zero when multiple ranks are allocated; HemeLB uses all
ranks for fluid sites. Equal allocations therefore give different worker counts.
The following are medians in seconds; decimal values come from recorded runs.

| Allocated ranks | HemePure fluid workers | HemeLB fluid workers | HemePure elapsed | HemeLB elapsed | HemePure loop | HemeLB loop |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1 | 1 | 1.267602667 | 0.7719426249999999 | 1.03 | 0.649978 |
| 2 | 1 | 2 | 1.5791235830000003 | 0.7849984999999986 | 1.39 | 0.6719160000000001 |
| 4 | 3 | 4 | 0.9888604999999977 | 0.6095968340000013 | 0.724 | 0.44026 |

Loop values are the reference's `Lattice Boltzmann` phase maximum and the
candidate's documented loop metric. Their instrumentation boundaries and printed
precision differ, so elapsed time is the primary comparison. Elapsed time
includes MPI launch, setup, monitoring and output. These short runs do not
establish asymptotic scaling or sustained production MLUPS.

Equal fluid-worker runs allocate one additional reference rank:

| Fluid workers | HemePure allocated | HemeLB allocated | HemePure elapsed | HemeLB elapsed |
| :--- | :--- | :--- | :--- | :--- |
| 1 | 2 | 1 | 1.5807288750000001 | 0.8221152920000003 |
| 2 | 3 | 2 | 1.2053809579999992 | 0.8318095840000002 |
| 4 | 5 | 4 | 0.9873296669999991 | 0.6170803330000005 |

The candidate finished faster in every tested allocation and worker-count case.
This finding applies to this cylinder, build, machine and output cadence.

A separate RSS-wrapper run at each allocation measured solver-child lifetime
peak bytes per rank. These runs are excluded from timing medians; every paired
field comparison also passed. One memory sample per configuration is insufficient
to establish a consistent memory winner, especially at this small site count.

| Allocated ranks | HemePure per-rank peak bytes | HemeLB per-rank peak bytes |
| :--- | :--- | :--- |
| 1 | 18759680 | 19234816 |
| 2 | 16007168, 19218432 | 18153472, 17825792 |
| 4 | 16089088, 18595840, 17874944, 17989632 | 17580032, 17694720, 17874944, 17514496 |

Executable SHA256 values for the fresh timing and RSS runs:

- HemePure: `b5774e518c14b734cb24dbc13cde2b2971a58b7cb4669b093e62a4522ab13c58`.
- HemeLB: `c3f72fd515a3b7cee5192a3139720093c701bfba58f5a1b2bbd9784a3e178f32`.

## Reproduce and extend the checks

[compare-cpu-solvers.py](../../Scripts/compare-cpu-solvers.py) requires installed
Python tools and inputs writing one `whole.xtr` frame at the same physical state.
It records binary/input/geometry hashes, ranks, phase timers, accuracy tolerances,
MPI/platform information and repeats. Use `--reference-rank-offset 1` when
matching fluid workers with the reviewed HemePure build, or omit it for equal
allocations. Use `--peak-rss` separately from timing runs.

[The pipe check](../../Code/tests/cpu_pipe_verification.py) needs the geometry
CLI, NumPy and PyYAML. [The sponge check](../../Code/tests/cpu_sponge_verification.py)
compares matching baseline/sponge builds. [The actual legacy checkpoint check](../../Code/tests/hemepure_checkpoint_mpi.py)
needs a built reference executable. Fresh raw logs, inputs and JSON are in ignored
`research/cpu-comparison-b746135b/`; the scripts and measured summary are tracked.
The earlier `research/cpu-baseline/` records are retained as historical evidence.

## Remaining CPU priorities

| Order | Work | Evidence needed before claiming an advantage |
| :--- | :--- | :--- |
| 1 | Preserve explicit legacy TRT/MRT relaxation parameters alongside native HemeLB defaults | Reference-matched field/relaxation checks, existing HemeLB inputs and rank-changing restarts; reject unsupported values rather than silently changing physics |
| 2 | Profile multi-worker MRT and reduce setup/storage peak memory | Repeated branching-case timing/RSS, moment-transform and communication profiles, unchanged fields and checkpoints; address the recorded regressions |
| 3 | Compare setup, loop, restart and peak RSS on patient-specific geometries and an HPC filesystem | Matched compiler/options/output, equal allocations and workers, repeated strong/weak scaling, rank imbalance and halo volume; local fixture matrix already recorded |
| 4 | Share active geometry/ownership metadata within nodes, with a portable replicated fallback | Memory and setup-time measurements against both existing branch code and HemePure's optional MPI-window path; unchanged site ownership and restart results |
| 5 | Distribute maintained passive tracers and migrate particle ownership | Rank-independent trajectories/restart, boundary/emission checks, memory/communication scaling; preserve existing input and CSV behavior |
| 6 | Compare complete scalar/SSE3/AVX2/AVX512 solver builds on supporting x86 hardware | Numerical checks plus repeated full-solver timing; AVX2 arithmetic CI alone is insufficient |
| 7 | Extend Yang, compliant-wall, LES/sponge and coupled-model physical validation | Independent solutions, resolution/timestep refinement, geometry/stability envelopes, production peer interoperability |

Active maps/octrees remain replicated, GMY retains its existing 16-bit per-axis
coordinate limit, and header scanning grows with bounding-box volume. The owner
cache budget is 64 MiB with a minimum whole block; a larger block can exceed it.
Portable restart uses collective count exchange and `MPI_Alltoallv`; its
large-rank scalability is unmeasured. Compressed-block caching avoids rereads
but retains payload memory during setup, so its net memory benefit needs a
representative comparison.

External production read/write and MPWide peers, active colloids, and resolved
RBC use with the new fluid models remain unverified. Existing RBC build and
parallel regression CI now pass; that removes the previous build-evidence gap,
not the remaining coupled-physics validation work.

## Source references

The branch implementation links above resolve within this checkout. Reference
source is pinned to HemePure `0bf67b16b23b41a06507810337a445f9916d62bf`:

- [Boundary parser](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/configuration/SimConfig.cc), [boundary build interface](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/BuildSystemInterface.h).
- [WK2](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/iolets/InOutLetWK2.cc), [WK3](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/iolets/InOutLetWK3.cc), [fileWK](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/iolets/InOutLetFileWK.cc).
- [Yang reconstruction](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/streamers/YangPressureDelegate.h), [elastic wall](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/streamers/GuoZhengShiElasticWallDelegate.h), [elastic Womersley](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/iolets/InOutLetWomersleyElasticVelocity.cc).
- [Sponge and LES kernels](https://github.com/UCL-CCS/HemePure/tree/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/kernels), [read/write iolet](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/lb/iolets/InOutLetReadWriteVelocity.cc).
- [CMake options](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/CMakeLists.txt), [shared geometry metadata](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/geometry/GeometryReader.cc), [particle ownership/output](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/colloids/ParticleSet.cc).
- [GMY+ costs](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/geometry/decomposition/BasicDecomposition.cc), [checkpoint loader](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/src/extraction/LocalDistributionInput.cc), [README](https://github.com/UCL-CCS/HemePure/blob/0bf67b16b23b41a06507810337a445f9916d62bf/README.md).

Adapted source notices are retained in [COPYING.HemePure](../../COPYING.HemePure).
