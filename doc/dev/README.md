# HemeLB developer guide

Start with the [manual build guide](../user/main-application.md) and
[CMake options](../user/CMakeOptions.md). These notes describe
`feat/scalability-input-improvements`; keep configuration units and binary formats aligned
with the branch under test.

## Repository layout

| Folder | Contents |
| :--- | :--- |
| `Code/` | C++ MPI solver; unit tests in `Code/tests/` |
| `geometry-tool/` | Python GUI/CLI and the C++ voxelisation extension |
| `python-tools/` | `hlb` parsers and converters, including Cython extensions |
| `dependencies/`, `CMake/` | Dependency builds, find modules, and shared options |
| `Scripts/` | Installer and repository checks |
| `doc/` | User guides, examples, and implementation/file-format references |

## Implementation references

- [CPU verification commands and model restrictions](cpu-verification.md)
- [CPU profiling, optimization and measured timings](cpu-performance.md)
- [Branching-vessel and collision-model CPU benchmarks](representative-cpu-benchmarks.md)
- [CPU comparison and validation record](comparison-and-roadmap.md)
- [Geometry reading, validation, and decomposition](geometry-reading.md)
- [Checkpoint loading across rank counts](checkpoint-restart.md)
- File formats: [geometry](file-formats/geometry.md),
  [legacy geometry](file-formats/old-geometry.md),
  [extraction](file-formats/extraction.md), [offsets](file-formats/offset.md)
- [Legacy components](legacy-code.md)
- [Changelog](../../CHANGELOG.md), including recorded validation of earlier changes

## How to run the tests

### Solver tests

For a code-only build with `HEMELB_BUILD_TESTS=ON`:

```sh
ctest --test-dir build-code --output-on-failure
```

For a top-level super build, use its inner code build folder:

```sh
ctest --test-dir build/hemelb-prefix/src/hemelb-build --output-on-failure
```

CTest registers `hemelb-tests` and, if Python is found, five MPI regressions:
`cpu-features-mpi`, `windkessel-mpi`, `legacy-compatibility-mpi`,
`geometry-setup-mpi`, and `checkpoint-restart-mpi`.
The installed `hemelb-tests` executable runs only the C++ unit suite. Run the
portable-restart test separately when using an installed executable:

```sh
python3 Code/tests/checkpoint_restart_mpi.py --hemelb /path/to/hemelb
```

It covers 2-to-1, 1-to-2, 2-to-4, and 1-to-1 rank restarts, comparing final
fluid distributions exactly by grid coordinate. Geometry-mismatch rejection
is exercised on one rank. Set `MPIRUN_FLAGS` for launcher-specific flags if
needed, for example `MPIRUN_FLAGS=--oversubscribe` with Open MPI on a small host.

For an RBC-enabled build, CI also runs:

```sh
mpirun -n 4 build-code/tests/mpi_redblood_tests
```

### Geometry and analysis tools

Install both packages and activate their environment first. Run the geometry
suite from `tests/` so it imports the installed generation extension:

```sh
cd geometry-tool/tests
pytest
```

GUI tests skip when wxPython is absent. For the Python tools, use tox, which
builds the package in its test environment:

```sh
cd python-tools
HEMELB_TESTS_DIR=/path/to/hemelb-tests tox -e py311
```

The external fixtures come from
[hemelb-codes/hemelb-tests](https://github.com/hemelb-codes/hemelb-tests), fetched
by the [CI action](../../.github/actions/get-tests-repo/action.yml).
Keep `HEMELB_TESTS_DIR` as an absolute path. Full fixture-dependent coverage
requires that checkout; missing fixtures are not a successful full test run.

### End-to-end and stored-result tests

| Test | Invocation or reference |
| :--- | :--- |
| Geometry generation, solver, and analytical pipe profile | From the repository root: `HEMELB_EXECUTABLE=/path/to/hemelb python Code/tests/pythontests/poiseuilleflowtest.py -v` in `gmy-tool` |
| Stored fluid outputs and checkpoints | External `hemelb-tests` repository; commands and environment in [main-app.yml](../../.github/workflows/main-app.yml) |
| Full installation and short solver run | [install-script.yml](../../.github/workflows/install-script.yml) |
| Documentation example | [quick-start walkthrough](../user/getting-started.md) and [restart walkthrough](../user/checkpoints.md) |

## CI coverage and triggers

The workflows select pushes to `main`, `fix/**`, `feature/**`, and `feat/**`, with path
filters. A documentation-only push does not automatically run all suites.
Pull requests, scheduled runs, and manual dispatch are also configured.
Read each workflow before inferring coverage from a push or status badge.

| Workflow | Configured coverage |
| :--- | :--- |
| [Main application](../../.github/workflows/main-app.yml) | GCC 11/12/13; fluid and RBC builds; unit, regression, and MPI tests; optional CPU-model matrix and x86 vector checks |
| [Geometry tool](../../.github/workflows/gmy-tool.yml) | Python 3.8 through 3.11; generation and profile tests; Python/C++ formatting |
| [Python tools](../../.github/workflows/py-hemetools.yml) | Python 3.8 through 3.13; tox and Python formatting |
| [Install script](../../.github/workflows/install-script.yml) | Ubuntu 24.04, macOS 14, macOS 15 Intel; install and end-to-end flow test |

This table describes the workflow configuration, not the result of any particular
run. Check that run's logs before claiming a platform or configuration passes.

Repository checks include `python Scripts/checkCopyright.py`,
`python Scripts/check_once_guards.py`, Black 22 for Python code, and the geometry
extension's clang-format check. Follow the workflow's versions and scope.

## Updating dependencies

Library fallback URLs and versions live in `dependencies/<Name>/build.cmake`.
The tool environment's main pins are in `geometry-tool/conda-environment.yml`;
explicit locks in `geometry-tool/conda-lock/` contain exact package URLs.
Build-isolation pins are separate in `geometry-tool/pyproject.toml`.

When changing versions:

1. Update the relevant source of pins and regenerate both platform locks using
   conda/conda-lock tooling. Keep the environment file and lock files consistent.
2. Verify each target platform can create the locked environment. An explicit
   conda file starts with `@EXPLICIT`; it is not a cross-platform environment.
3. Build the Python packages and run their tests in those environments. Check
   the geometry tool's supported Python range when changing build-isolation pins.
4. Update [INSTALL](../../INSTALL) and the changelog with what changed and what
   actually ran. Keep unresolved platforms or missing fixtures explicit.

## Documentation changes

Keep user commands consistent with the CLI entry points and CMake options.
Run the bundled example after changing its XML or walkthrough, and check local
Markdown links, including fragments. Format changes need updates to the user
reference, binary-format notes, readers, and compatibility documentation together.
Local `research/` files are ignored and are not part of distributed documentation.
