# Build and run HemeLB

Use the [install script](install.md) for a local installation. This guide covers
manual builds, including clusters where dependencies come from environment
modules. Run commands from the repository root unless stated otherwise.

## Requirements

- A C++20 compiler; the main application CI matrix uses GCC 11, 12, and 13.
- CMake 3.13 or newer, as declared by the project.
- MPI 3.0 or newer, with compiler wrappers and a compatible launcher.
- The libraries below. Optional models require additional dependencies.

| Library | Requirement | Bundled build version |
| :--- | :--- | :--- |
| Boost headers | At least 1.77 | 1.77.0 |
| ParMETIS | 4.x, with METIS | 4.0.2 |
| TinyXML | 2.x, not TinyXML2 | 2.6.2 |
| CTemplate | Required | 2.4 |
| zlib | Required | 1.2.6 |
| Catch2 | 2.x, when tests are enabled | 2.13.9 |
| HDF5 | At least 1.8, RBC builds only | 1.8.15 |
| VTK | At least 9.0, RBC builds only | 9.1.0 |
| MPWide | Multiscale builds only | 1.1 |

[INSTALL](../../INSTALL) records dependency pins and previously tested
platforms. The geometry tool has a separate Python/VTK environment; see its
[installation guide](geometry-tool.md#install).

## Installing HemeLB manually

Choose a build layout for your installation:

| Source folder | Purpose | Installation |
| :--- | :--- | :--- |
| Repository root | Super build: find or build dependencies, then build the solver | The external build installs into the chosen prefix |
| `Code/` | Solver only, using existing dependencies | Run `cmake --install` explicitly |
| `dependencies/` | Prepare dependencies for repeated solver builds | External dependency builds install into the chosen dependency prefix |

### Super build

```sh
git clone --branch fix/hemelb-improvements https://github.com/lepotatoguy/hemelb.git
cd hemelb

cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/hemelb" \
  -DHEMELB_SUBPROJECT_MAKE_JOBS=4
cmake --build build -j 4
export PATH="$HOME/.local/hemelb/bin:$PATH"
```

The super build invokes `make` for the solver subproject, so use Unix Makefiles.
`HEMELB_SUBPROJECT_MAKE_JOBS` controls that inner build; the outer `-j 4` alone
does not set its parallelism. One configure pass is sufficient.

For each dependency, `DEPS_<NAME>` chooses `Auto` (use a system library when
found, otherwise build it), `System` (fail if missing), or `Build` (build the
bundled version). For example, add `-DDEPS_PARMETIS=Build`.

### Code-only build

After loading your compiler/MPI modules and installing dependencies:

```sh
cmake -S Code -B build-code \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/hemelb" \
  -DHEMELB_DEPENDENCIES_INSTALL_PREFIX="$HOME/.local/hemelb-deps"
cmake --build build-code -j 4
cmake --install build-code
```

Replace the dependency prefix with yours. If libraries are spread across
several prefixes, use the hints in [CMake options](CMakeOptions.md#how-dependencies-are-found).
To build dependencies first, use `cmake -S dependencies -B build-deps` with
`-DHEMELB_DEPENDENCIES_INSTALL_PREFIX` set to that prefix, then
`cmake --build build-deps -j 4`.

Use a separate build directory for each lattice, boundary configuration, or
compiler/MPI combination. Model choices are fixed at compilation; see
[CMake options](CMakeOptions.md). The [ARCHER2 notes](machine-specific-build-notes/archer2.md)
provide a machine-specific example; use your site's current modules and job
launcher.

## Run

```sh
mpirun -n 2 hemelb -in input.xml -out results
```

| Solver option | Meaning |
| :--- | :--- |
| `-in FILE` | Configuration XML, required |
| `-out FOLDER` | New output directory; defaults to `results` beside the XML |
| `-debug 0` or `-debug 1` | Disable or enable the built-in debugger; default 0 |

Before launching, you can optionally check the XML with:

```sh
hemelb-confcheck input.xml
```

`hemelb-confcheck` checks XML parsing, units, and compiled boundary choices.
It does not load geometry or check that referenced input files are available.
The simulation checks those during setup. Relative input paths are relative
to the XML; the `-out` argument is relative to the shell's current directory.

The run writes `report.txt` and `report.xml`. Extracted fields and checkpoints
appear under `Extracted/` when requested in `<properties>`. Pressure is in mmHg
on this branch. Follow [quick start](getting-started.md) for a configuration
with output enabled and [checkpoints](checkpoints.md) for restart instructions.

Ranks cannot outnumber blocks containing fluid; the solver reports the limit
when this happens. On a cluster, launch within a scheduler allocation using
your site's MPI integration. The executable and launcher must use compatible
MPI installations. See [troubleshooting](troubleshooting.md) for common errors.

## Test

For the super build:

```sh
ctest --test-dir build/hemelb-prefix/src/hemelb-build --output-on-failure
```

For a code-only build:

```sh
ctest --test-dir build-code --output-on-failure
```

If your CTest does not accept `--test-dir`, change into the indicated build
folder and run `ctest --output-on-failure` there.

CTest includes `hemelb-tests` and, when Python is found during configuration,
`checkpoint-restart-mpi`. The installed `hemelb-tests` command runs only the
unit-test executable. See the [developer guide](../dev/README.md#how-to-run-the-tests)
for Python, geometry, regression, and optional RBC tests.
