# Verify CPU features and compatibility

Use a clean build folder for each kernel and boundary combination. These
commands describe regression checks that exist in the repository. Recorded
results and their limits are in the [CPU comparison](comparison-and-roadmap.md);
a configured CI job is not evidence that a remote run passed.

## Default fluid build

Configure a code-only build with tests and your dependency prefix:

```sh
cmake -S Code -B build-code \
  -DCMAKE_BUILD_TYPE=Release \
  -DHEMELB_DEPENDENCIES_INSTALL_PREFIX=/path/to/dependencies \
  -DHEMELB_BUILD_TESTS=ON -DHEMELB_BUILD_RBC=OFF
cmake --build build-code -j 4
ctest --test-dir build-code --output-on-failure
```

When Python is found, CTest registers these checks:

| Test | Coverage |
| :--- | :--- |
| `hemelb-tests` | C++ unit tests, geometry validation, interpolation, and model equations |
| `cpu-features-mpi` | Passive tracers, periodic pressure, output windows, reader controls, and restart |
| `windkessel-mpi` | WK2/WK3/fileWK responses and rank-changing restart |
| `legacy-compatibility-mpi` | XML3/5 units and extraction/checkpoint4/5/6 loading |
| `geometry-setup-mpi` | Octree/ParMETIS equivalence, read-once accounting, reference pressure, and report formulas |
| `checkpoint-restart-mpi` | Fluid continuation across rank counts and geometry-mismatch rejection |

The installed `hemelb-tests` executable runs only the C++ suite. To invoke
selected MPI drivers directly, from the repository root:

```sh
python3 Code/tests/cpu_features_mpi.py --hemelb /path/to/hemelb
python3 Code/tests/legacy_compatibility_mpi.py --hemelb /path/to/hemelb --mpirun mpirun
python3 Code/tests/geometry_setup_mpi.py --hemelb /path/to/hemelb --mpirun mpirun
python3 Code/tests/checkpoint_restart_mpi.py --hemelb /path/to/hemelb
```

Set `MPIRUN_FLAGS` if the launcher needs extra arguments, for example
`MPIRUN_FLAGS=--oversubscribe` on a small Open MPI host.

## Optional model builds

The [main application workflow](../../.github/workflows/main-app.yml) contains
the model matrix. Each build uses D3Q19, with the kernel and boundaries required
by the corresponding [CPU model](../user/cpu-models.md). For example:

```sh
cmake -S Code -B build-yang \
  -DCMAKE_BUILD_TYPE=Release \
  -DHEMELB_DEPENDENCIES_INSTALL_PREFIX=/path/to/dependencies \
  -DHEMELB_BUILD_RBC=OFF -DHEMELB_BUILD_TESTS=OFF \
  -DHEMELB_LATTICE=D3Q19 -DHEMELB_KERNEL=LBGK \
  -DHEMELB_WALL_BOUNDARY=BFL \
  -DHEMELB_INLET_BOUNDARY=YANGPRESSUREIOLET \
  -DHEMELB_OUTLET_BOUNDARY=YANGPRESSUREIOLET
cmake --build build-yang -j 4
python3 Code/tests/cpu_boundary_models_mpi.py --hemelb "$PWD/build-yang/hemelb" --model yang
```

| Driver `--model` | Matching build |
| :--- | :--- |
| `yang` | LBGK, Yang inlet/outlet, BFL wall |
| `yang-corners` | LBGK, Nash inlet/outlet, BFL wall, explicit Yang wall inlet/outlet |
| `elastic` | LBGK, GZSElastic wall, Nash pressure iolets |
| `sponge` | LBGKSL, BFL wall, Nash pressure iolets |
| `trt-sponge` | TRTSL, BFL wall, Nash pressure iolets |
| `les-sponge` | LBGKLESSL, BFL wall, Nash pressure iolets |
| `womersleyElastic`, `readWrite`, `weightedReadWrite`, `periodicVelocity` | LBGK, BFL wall, Ladd inlet, Nash outlet |

Drivers check finite positive distributions, a nonzero response, saved model
state, and two-to-one rank restart. Coupling modes use a mock peer. The Yang
driver also exercises legacy `type="yangpressure"` inputs; the `yang-corners`
driver uses legacy `type="pressure"` for its pure Nash iolets. Both exercise
pressure-file loading and reject unsupported relaxation times. Yang requires
LBGK, valid two-site interior stencils, and tau at least 0.8.

## Python and geometry tools

Use the [developer test guide](README.md#geometry-and-analysis-tools) for tox,
geometry tests, and the external fixture repository. The Python tests include
legacy and version 6 extraction, pressure offsets, config conversion, and VTK
round-trip checks. Verify generated XML with the optional `hemelb-confcheck`
and run the [bundled example](../user/getting-started.md) after changing its
configuration or commands.

## Performance and physical validation

The [scalability guide](../user/scalability-and-inputs.md) defines report MLUPS,
imbalance, halo volume, and peak RSS. Record compiler and MPI versions, build
options, geometry/site count, output cadence, rank allocation, and worker count
when comparing solvers. HemePure may reserve rank zero at multiple ranks, so
compare both equal allocations and equal fluid-worker counts.

The comparison document records measured local CPU runs, a Yang pipe check at
two resolutions, and a paired sponge-pulse diagnostic. Finite-state and restart
regressions alone do not establish physical accuracy, an outlet reflection
coefficient, or a stability envelope for a new geometry. Native AVX execution,
cluster-scale performance, production peer coupling, active colloids, and RBC
validation for the new features remain unverified. Retain the original inputs
and raw results when adding a verification record.

## Documentation workflow checked on 2026-10-01

The bundled 200-update example ran on two ranks; restart from its 100-update
checkpoint ran on four ranks and reproduced the final fluid distributions
exactly. CSV export and VTU read-back passed, including velocity/pressure cell
arrays and collection times of 0 and 0.01 seconds. Configuration snippets in
the CPU, coupling, tracer, and extraction guides passed syntax checks.

STL regeneration used this checkout's Python geometry code with the available
installed voxelisation extension, then ran the generated XML6 configuration
on two ranks. This checks the documented workflow, not a fresh package install
or a rebuild of that extension. The locally installed older Python writer
still emits XML5 until reinstalled; users should reinstall the tools after
switching branches.

### CI fixtures and extraction precision

CI loads the upstream XML5 cylinder and checkpoint fixtures directly. Its
comparison driver sets their historical initial timestep of 1 explicitly and
includes the final extraction. It compares pressure in Pa and permits only a
float32 rounding bound when matching physical legacy snapshots to scaled XML6
output. Double checkpoint distributions retain the `1e-12` bound. Site sets,
extraction times, field shapes, finiteness and offset payloads are checked.

The installer regression generates a pipe and runs a stable, low-Mach pressure
difference. It requires a successful solver exit and a final velocity sample,
then checks the Poiseuille profile relative to the coarse bounce-back radius.
Wall shear stress is extracted from surface sites, where it is defined.
