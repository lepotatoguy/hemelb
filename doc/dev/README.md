# HemeLB developer documentation

## Where things are

| Folder | What it contains |
| --- | --- |
| `Code/` | The main application (C++, MPI) and its unit tests (`Code/tests`) |
| `geometry-tool/` | `hlb-gmy-gui` and `hlb-gmy-cli`: Python with a C++ extension that voxelises surfaces |
| `python-tools/` | The `hlb` package: file parsers and converters (Python with Cython) |
| `dependencies/`, `CMake/` | The super build that fetches and builds missing libraries |
| `Scripts/` | The install script and repository checks |
| `doc/` | User and developer documentation |

## Design notes

- [Reading, validating and decomposing geometry](geometry-reading.md)
- [Checkpoint restart across MPI process counts](checkpoint-restart.md)
- [Extraction file format](file-formats/extraction.md)
- [Offset file format](file-formats/offset.md)
- [Geometry file format](file-formats/geometry.md) (and the [old format](file-formats/old-geometry.md))
- [Legacy and abandoned components](legacy-code.md)

Changes in this fork, with how each was verified, are listed in
[CHANGELOG.md](../../CHANGELOG.md).

## How to run the tests

All of these also run in CI (`.github/workflows`) on every push to `main`,
`fix/**` and `feature/**`.

| Suite | What it covers | How to run it |
| --- | --- | --- |
| C++ unit tests | Core library: geometry reading and decomposition, lattice Boltzmann kernels, boundaries, extraction, MPI helpers, configuration | `hemelb-tests` (installed next to `hemelb`), or `ctest` in the code build folder |
| Checkpoint restart | Restarting on 1, 2 and 4 processes gives identical results; mismatched geometries are rejected | `python3 Code/tests/checkpoint_restart_mpi.py --hemelb /path/to/hemelb` (also run by `ctest`) |
| Poiseuille flow | Geometry tool, HemeLB and the Python tools together: the velocity across a pipe matches the analytical profile | `python3 Code/tests/pythontests/poiseuilleflowtest.py` in the `gmy-tool` environment, with `HEMELB_EXECUTABLE` set if `hemelb` is not on `PATH` |
| Regression tests | Output compared with stored results (`hemelb-codes/hemelb-tests`) | See `main-app.yml` ("Run the simple regression test", "Run the checkpoint test") |
| Geometry tool | Profiles, CLI, generation (byte-identical output), warnings, GUI controllers | Install the tool, then `cd geometry-tool/tests && pytest`. GUI tests are skipped without wxPython |
| Python tools | Parsers, converters, XDR reading, geometry compression | `cd python-tools && HEMELB_TESTS_DIR=/path/to/hemelb-tests tox -e py311` (tox builds the package first) |
| Install script | A full install on Ubuntu and macOS | `install-script.yml` in CI |

With `ctest`, use the build folder of the code itself. With the super build
(`Scripts/install_hemelb.sh` or a top-level CMake build) that is
`build/hemelb-prefix/src/hemelb-build`.

Running `pytest` directly inside `python-tools/` or `geometry-tool/` picks up
the source folders, which lack the compiled modules; install the package and
run from `tests/` (geometry tool) or use tox (Python tools).

## Checks CI runs on every change

- `python Scripts/checkCopyright.py`: every source file starts with the copyright header.
- `python Scripts/check_once_guards.py`: C++ include guards.
- `black --check .` (version 22) in `geometry-tool/` and `python-tools/`.
- `clang-format` (version 14) on `geometry-tool/HlbGmyTool/Model/Generation`.

## Updating pinned versions

Versions are pinned exactly so installs stay reproducible (the full list is
in `INSTALL`). To move to newer versions:

1. Edit the versions in `geometry-tool/conda-environment.yml`.
2. Regenerate both lock files. On any machine with conda, solve for each
   platform with `conda create --dry-run --json -n tmp -c conda-forge
   --override-channels <pinned packages>`, using `CONDA_SUBDIR=osx-64` or
   `CONDA_SUBDIR=linux-64` (add `CONDA_OVERRIDE_GLIBC=2.39` when solving
   for Linux on a Mac). Write the package URLs as an `@EXPLICIT` list to
   `geometry-tool/conda-lock/<platform>.txt`, or run `conda list --explicit`
   in an environment created on that platform.
3. If the build tools change, update `geometry-tool/pyproject.toml`; each
   version must support Python 3.8 to 3.11.
4. Create an environment from each lock file, install both tools, and run
   the geometry-tool and Python-tools tests; then update `INSTALL` and the
   changelog. CI checks the Linux and macOS locks through the install job.
