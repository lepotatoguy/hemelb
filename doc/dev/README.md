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
