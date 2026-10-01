# Unreleased scalability and input improvements

Merged upstream/checkpoint. New simulation XML uses version 6 and Pa. Existing
HemePure version 3 and HemeLB version 5 XML load directly with their legacy mmHg
units. Checkpoints in extraction versions 4, 5, and 6 load across MPI rank counts.
Legacy pressure-file units and double-precision output settings survive saved
restart XML; source inputs remain unchanged.
Extraction output now uses version 6 with lattice-to-physical conversion metadata.
Checkpoint output moves to Checkpoints/<step>/ with restart.xml and distributions.xtr.
Rank-portable restart and geometry validation are retained. TinyXML-2 replaces TinyXML.
Pressure-file loading handles trailing newlines and unsorted, duplicate times safely.

Sparse geometry setup reads active compressed blocks once across ranks and caches
them through redistribution. Runtime decomposition selects octree or ParMETIS.
Reports include MLUPS, load imbalance, halo volume, and per-rank peak RSS.
`hlb-convert-config` converts XML versions 3 and 5 to version 6; `.gmy+` block
weights are ignored. Python extraction reads versions 4, 5, and 6. Inlet/outlet
extraction selectors are supported. Pressure extraction preserves nonzero
reference pressure. See [the guide](doc/user/scalability-and-inputs.md).

# Changelog

Changes in this fork relative to upstream `hemelb-codes/hemelb` commit
`432d3386`. Design notes for these changes live in [doc/dev](doc/dev);
this file records what changed and how it was checked at the time.

## Unreleased (`fix/hemelb-improvements`)

### Breaking

- Single-timestep extraction files are now named with zero-padded
  timesteps (pattern `%0*lu`). They were previously padded with spaces,
  which put spaces in file names. Scripts that expect the old names must be
  updated.

### Documentation

- Bundled matching cylinder STL, geometry-tool profile, GMY, and version 5 XML
  in [doc/examples](doc/examples/README.md), with a walkthrough from surface
  generation to simulation and CSV output. Regeneration reproduced the supplied
  GMY; two-rank runs and a four-rank restart matched final distributions exactly.
- Optional [Scripts/gmy-to-stl.py](Scripts/gmy-to-stl.py) exports GMY voxel boundaries as STL for visual inspection.
- Reorganised the branch's installation, first-run, XML, analysis, and developer
  guides around a runnable cylinder example. Added fluid checkpoint and
  troubleshooting guides, clarified version 5/mmHg conventions and CI path
  filters, and corrected the branch selected by `INSTALL`.
- Local `research/` files are ignored and excluded from distributed documentation.
- Verified the new example with a two-rank run and four-rank restart, matching
  final distributions exactly. CSV and ASCII VTU exports were exercised; VTU
  pressure and velocity values round-tripped by grid coordinate. Both local
  CTest targets passed; local Markdown links and code-block syntax were checked.
  The ParaView GUI was not exercised. The ASCII recipe avoids a local VTK 9.1
  appended-data read failure.

### Added

- `hlb-extracted-to-vtk` exports extraction files directly to ASCII VTU frames
  and a ParaView PVD collection ([guide](doc/user/python-tools.md#export-for-paraview)).
  It uses physical coordinates and cell fields without requiring GMY/XML,
  supports optional collection times in seconds, and refuses existing outputs.
  Eleven tests passed, covering extraction versions 4/5, reordered site rows,
  field offsets, tensors, integer precision, and output handling. Installed CLI
  and module exports round-tripped the bundled run's coordinates, velocity, and
  pressure through VTK; the ParaView GUI was not exercised.
- Checkpoints can be restarted with a different number of MPI processes
  ([design note](doc/dev/checkpoint-restart.md)).
- `Scripts/install_hemelb.sh` installs HemeLB, the geometry tool and the
  Python tools on macOS and Debian/Ubuntu ([guide](doc/user/install.md)).
- `hlb-pro2pr2` can run as `python -m HlbGmyTool.scripts.pro_to_pr2`.
- `hlb-gmy-cli` can override every profile setting except the iolets
  (`--stl`, `--stl-units`, `--seed`, `--timestep`, `--duration`, plus the
  existing `--voxel`, `--geometry`, `--xml`), and `--help` describes each
  step of a run ([guide](doc/user/geometry-tool.md#run-the-command-line-generator)).
- Profiles can set `PulsePeriodSeconds`, the period of the cosine pressure at
  every inlet and outlet (`hlb-gmy-cli --period`). Profiles without it write
  `value="1"` as before.
- Documentation for new users: a start-here index with a glossary
  ([doc/README.md](doc/README.md)), a first-run walkthrough
  ([getting-started.md](doc/user/getting-started.md)), a GUI walkthrough, and
  a guide to every test suite for developers ([doc/dev/README.md](doc/dev/README.md)).
- Every page under `doc/` was checked against the current code and rewritten
  where out of date: build and run guide (C++20, CMake 4, `hemelb-confcheck`,
  `-out` defaults), all CMake options with their real defaults, the XML
  reference (`units=`, `offsets=`, `results/Extracted`, zero-padded names,
  checkpoint outputs need `%d`, iolet order and build-dependent condition
  types), geometry-tool install by hand (conda environment, Rosetta,
  `--no-deps --no-build-isolation`), velocity weights files, the checkpoint
  and geometry-reading design notes, file formats, legacy components and
  ARCHER2 notes. All relative links and anchors resolve.

### Fixed

- Security: legacy `.pro` profiles are unpickled with a restricted loader,
  so opening one cannot run arbitrary code.
- Geometry reading validates headers, block records, decompression and site
  records, with bounds checks in all build types
  ([design note](doc/dev/geometry-reading.md)).
- The lookup tree no longer loops forever for domains wider than 32,768
  blocks; a compressed block above 64 MiB is an error instead of a hang.
- The initial decomposition uses 64-bit integer site counts instead of
  `float`.
- The geometry writer checks every file operation and size limit.
- MPI gather and all-to-all helpers handle empty vectors.
- Geometry tool: iolet intersection uses the squared radius and rejects
  hits outside the segment; error handling and the STL preview are fixed.
- `hlb-dump-extracted-properties` exports vector fields as separate columns.
- `hlb-gmy-cli` checks its inputs before generating: a missing STL used to
  crash the generator (segmentation fault) and a zero voxel size ended in a
  Python traceback. It also no longer leaves `exportedsurface.off` in the
  current folder.
- Geometry tool GUI: opening a profile when one was already loaded dropped
  inlets and outlets and left the iolet list out of step (clearing the list
  reported index -1, which the list widget rejected part way through the
  load). Generate now checks its inputs, always closes the progress window,
  and reports failures and warnings in a message box; Open Profile reports
  unreadable files instead of failing silently.
- `hlb-gmy-cli` and the GUI warn when an iolet cannot reach the surface,
  lies beyond the end of the vessel (it then opens nothing, and the vessel
  has no inlet or outlet there), or when the seed point lies outside the
  capped surface. Generation is unchanged. The geometry tool guide now
  explains that closed and open STL surfaces both work, and where iolets
  must be placed.
- Profiles accept decimal exponents written without a point (`1e-5`), which
  YAML reads as text; they failed with "invalid hexadecimal floating-point
  string".
- HemeLB stops with a clear message when the geometry uses more inlets or
  outlets than the XML defines. It used to crash (segmentation fault).
- A missing `.gmy` is reported by name, not as an MPI error in
  `MpiFile.cc`.
- Too many MPI processes for the geometry reports both counts and the
  largest usable process count.
- Checkpoint restarts reject a checkpoint written with a different voxel
  size or origin.
- The GUI labelled the iolet phase in degrees; HemeLB uses radians, as the
  XML always said. The label now shows radians; the XML is unchanged.
- The GUI's DEBUG button is only shown when the tool was started from a
  terminal; otherwise the debugger froze the window.
- The geometry tool warns when an iolet only just reaches the wall (nearest
  wall beyond 0.9 of its radius). The repository's Poiseuille sample profile
  (iolet radius 0.75 mm in a pipe of the same width) generated a pipe with
  no inlet or outlet sites and therefore no flow.
- `hlb-dump-extracted-properties` has `--help` and reports a missing file in
  one line; output is unchanged.
- `hlb-gmy-gui --help` typos; 5 broken documentation links.
- `pip install './geometry-tool[gui]'` failed with "No matching distribution
  found for vmtk~=1.5", because VMTK is only on conda-forge. `setup.py` now
  leaves VMTK out of the requirements when it is already installed
  (importable or in the active conda environment's package records) and
  keeps it otherwise; a missing VMTK at run time gives an ImportError that
  says how to install it. The tool's CMake now searches `$CONDA_PREFIX`
  first, because a plain pip build otherwise found Homebrew's CGAL 6 and
  failed on `CGAL/AABB_polyhedron_triangle_primitive.h`. In a fresh
  environment it then failed on CGAL 5.6's `iterator.h`, which current Clang
  rejects; the build now compiles against a corrected copy of that header in
  its build folder, so the installer no longer edits the environment's CGAL.
  The old workarounds (editing `setup.py`,
  `bodge-packages-for-setuptools.sh`) are no longer needed.
- `INSTALL` described the CMake 2.8-era build (CppUnit, `USE_MULTIMACHINE`);
  it now gives the install commands, the minimum, bundled and tested
  version of every dependency, and the tested platforms. The build guide
  said Boost 1.54; the build requires 1.77.
- Exact versions, so installs do not change as new releases appear:
  `geometry-tool/conda-lock/osx-64.txt` and `linux-64.txt` pin every package
  of the conda environment (220 and 266) to an exact build and the install
  script creates the environment from them;
  `geometry-tool/conda-environment.yml` pins every listed package exactly
  (Python 3.11.8, VMTK 1.5.0, VTK 9.2.6, CGAL 5.6.1, and so on);
  `geometry-tool/pyproject.toml` pins pip's build tools (setuptools 75.3.0,
  wheel 0.45.1, scikit-build 0.19.0, pybind11 2.13.6, cmake 3.28.3), chosen
  to support Python 3.8 to 3.11; the installer pins `python.app=1.4`. The
  exact compiler, CMake, MPI and library versions tested on each platform are
  listed in `INSTALL`.
- The install CI also runs on Intel macOS (`macos-15-intel`).
- Merged the fork's `development` branch (24 commits, 2025-02-07 to
  2025-05-08). Taken from it: the Vagrant provisioning script fix (it failed
  `bash -n` with "unexpected end of file"), the "Successfully" spelling in
  the generator's output, the HemeXtract note in the Python tools guide, the
  link to the HemeLB Made Easy tutorial in `README.md`, extra `.gitignore`
  folders, the velocity-inlet units in the XML guide, and two older linux-64
  environment lists (now `geometry-tool/conda-lock/older/`). Its faster
  `hlb-dump-extracted-properties` changed the output (integer grid columns
  became `1.0`, float32 values gained digits), so the speed-up was redone
  keeping the output byte-identical: 0.90 s to 0.55 s on the 44,250-site
  test file, with a new test that compares against the old writer. Kept
  from this branch instead of `development`: the `.pro` loader (development's
  version passes non-HemeLB classes to the normal unpickler, so a crafted
  `.pro` could run code), the Poiseuille test (development's needs a fixed
  `/usr/local/bin/hemelb` and pre-made inputs) and the ParMETIS URL (already
  the same).
- The XML guide's velocity conditions matched neither the code nor each
  other: all use physical units (`m`, `m/s`, `mmHg/m`, `s`), not lattice
  units, which stop HemeLB with "Invalid units for element"; the Womersley
  values sit directly under `<condition>` (there is no
  `<womersley_velocity>` element).

- Python tools work on Python 3.12 and 3.13. The standard library's
  `xdrlib` (removed in 3.13) is replaced by `hlb.utils.pyxdr`, which is
  tested against `xdrlib` for identical results, positions and errors. The
  Cython extensions now build with Cython 3 as well as 0.29
  (`language_level=2` is set explicitly, as 0.29 used by default). On Python
  3.11 (Cython 0.29 and 3.3) and 3.13 (Cython 3.3), the extraction dump,
  offset and geometry parsers, site counts and self-consistency check gave
  byte-identical output to the previous version.

### Tests

- `Code/tests/pythontests/poiseuilleflowtest.py` is Python 3. It generates
  its inputs with `hlb-gmy-cli` and checks the velocity across a pipe against
  the analytical Poiseuille profile; it runs in the install script CI job.
- The checkpoint restart script also checks that mismatched geometries are
  rejected, and runs under `ctest` and in the main CI workflow.
- New unit tests: iolet id checks, the too-many-processes message, the
  pulse period and phase label, the DEBUG button, the CLI and GUI help, the
  dump tool arguments, and the 'only just reaches' warning.

### Build

- Builds with AppleClang (`SimBuilder.h` template call), GCC 13
  (`<cstdint>` in `io/formats/geometry.h`) and CMake 4 (TinyXML and ParMETIS
  CMake minimums).
- The super build finds dependencies it builds itself in one CMake pass.
- `geometry-tool/conda-environment.yml` uses Python 3.11 and lists the build
  requirements.
- The geometry tool declares `python_requires=">=3.8,<3.12"`, matching the
  VMTK 1.5 builds.
- CI: the hemelb-tests branch lookup works (it always fell back to `main`)
  and no longer expands pull request branch names inside a shell script.

### Verification record

Every row is a run that was actually performed; nothing here is estimated.

On 2026-09-28 the branch history (53 commits) was squashed into five, one
per component: `29da7af6` main application, `a0d69912` geometry tool,
`e0429354` Python tools, `351a899e` installer and CI, and the documentation
commit that follows them. The commit hashes quoted below are from the
original history, which is kept in the tag `pre-squash-2026-09-28`.
Test counts grow as tests are added, so earlier rows show lower numbers.
"Byte-identical" means the output file was compared byte for byte with the
stated reference. Local runs are on macOS 27 (Apple Silicon, AppleClang
21, CMake 4.3.2) unless the platform column says otherwise.

#### Main application (C++)

| Date | Change (commits) | Platform | What was run | Result |
| --- | --- | --- | --- | --- |
| 2026-09-27 | 64-bit initial decomposition (`11eee676`, `f2d0deba`) | macOS | `hemelb-tests`; `large_cylinder`, 200 steps on 4 ranks, compared with upstream `432d3386` | 77 cases, 32,403 assertions passed. Same site count on every rank and byte-identical `whole.xtr` |
| 2026-09-27 | Lookup tree sizing and geometry file I/O guards (`6f3ca6fe`) | macOS | `hemelb-tests`; `large_cylinder` as above | 79 cases, 32,411 assertions passed; output byte-identical to the previous change |
| 2026-09-27 | Geometry validation, empty MPI buffers (`1ddfd2e8`) | macOS | `hemelb-tests`, including the empty-gather test on 4 ranks; `large_cylinder` as above | 87 cases, 32,443 assertions passed; 4-rank test passed; output byte-identical to the previous change |
| 2026-09-27 | Checkpoint restart across process counts (`2a0c3dbb`, `8548b4c4`) | macOS | `checkpoint_restart_mpi.py`: write on N processes, restart at step 2 on M, compare step 4 by grid position | 2 to 1, 1 to 2, 2 to 4 and 1 to 1 all matched exactly; 73 cases, 32,386 assertions passed |
| 2026-09-28 | Clear errors and checkpoint geometry check (`739df888`) | macOS | `hemelb-tests`; `ctest`; restart script; HemePure SixBranch (5 outlets) with a 1-inlet/1-outlet XML; `four_cube.gmy` (1 block) on 2 ranks; an XML pointing to a missing `.gmy` | 89 cases, 32,448 assertions passed; `ctest` passed. Restarts still matched exactly; a different voxel size or origin was rejected. SixBranch stopped with the outlet-count message instead of a segmentation fault (exit 139 before); `four_cube` gave the process-count message; the missing file was named |
| 2026-09-28 | Stricter `.gmy` reader on real files (#16) | macOS | 17 geometries (repository tests, `hemelb-tests`, HemePure cases, up to 2,010,048 sites) read by the new reader and by a build of the `432d3386` reader | Same active blocks, fluid sites and outcome for every file |

#### Installer

| Date | Change (commits) | Platform | What was run | Result |
| --- | --- | --- | --- | --- |
| 2026-09-27 | Install script, all branches merged (`c4bd8579` to `e24ff725`) | macOS | Fresh clone, `install_hemelb.sh --no-system-deps` (Homebrew packages already present) | HemeLB built; 87 cases, 32,443 assertions passed; geometry tool and Python tools installed; compiled modules import; `hlb-gmy-gui` launcher uses the framework Python |
| 2026-09-27 | ParMETIS and TinyXML built from source | macOS | Manual super build with both forced to `Build`; `large_cylinder`, 50,000 steps on 4 ranks | Built; tests passed; the run finished (203 s) |
| 2026-09-27 | Install script (`a6ae73ee`) | Ubuntu 24.04 amd64 in Docker (emulated), GCC | Fresh clone, full `install_hemelb.sh`, then `large_cylinder` 200 steps on 4 ranks | 87 cases, 32,443 assertions passed; the run finished; Miniforge, the environment and both tools installed; `hlb-gmy-cli` ran |
| 2026-09-28 | Install script on Intel macOS | GitHub `macos-15-intel` (macOS 15.7.9, AppleClang 17.0.0, Homebrew Open MPI 5.0.10, Boost 1.92.0) | Full install with the osx-64 lock file, short simulation, tool imports, Poiseuille flow test | Passed: 89 cases, 32,448 assertions; Poiseuille 3 tests OK |
| 2026-09-27 onward | Install script in CI (`57707e06`) | GitHub `ubuntu-24.04` and `macos-14` | `install-script.yml`: full install, then (from `72403769`) the Poiseuille flow test | Passed on both at `86baf636` (about 5 min on Ubuntu, 8 min on macOS) and at every later run recorded here, including `7864e1d0` |

Not tested: Debian itself (only Ubuntu), Ubuntu releases other
than 24.04, Linux on ARM, and non-apt distributions.

#### Geometry tool

| Date | Change (commits) | Platform | What was run | Result |
| --- | --- | --- | --- | --- |
| 2026-09-27 | `hlb-gmy-cli` checks and options (`68ac81bc`) | macOS, Python 3.11 | Test suite; experiments on `test.pr2` and the Poiseuille profile: missing STL, `--voxel 0`, wrong output extension, missing output folder, `1e-5` in a profile | 41 tests passed (39 plus 1 skipped module without wxPython). Missing STL: exit 2 with a message (segmentation fault before); voxel 0: exit 2 (traceback before); `1e-5` parsed |
| 2026-09-27 | GUI fixes and silent-case warnings (`a6ede457`, `8eb12d75`) | macOS, Python 3.11 | The real GUI window driven by button events: open profile twice, Generate with a missing folder, voxel 0, Add/Remove iolets, Save Profile; warnings checked on every repository profile | Iolets kept on reload (2 became 1, then 0, before); failures shown in message boxes; progress window closes. No warnings on valid profiles; `test.gmy` byte-identical to the stored reference |
| 2026-09-27 | Closed and open STL surfaces | macOS | The same tube generated from a closed and an open STL; iolets moved beyond the vessel end | Identical geometries; iolets beyond the end gave 0 inlet and 0 outlet sites, which now triggers a warning |
| 2026-09-28 | Phase label, pulse period, DEBUG button, help, Python range (`1cb3e02c`) | macOS, Python 3.11 | Test suite, with and without wxPython | 59 tests passed; 53 passed and 1 module skipped without wxPython; `test.gmy` byte-identical |
| 2026-09-28 | Poiseuille flow test in Python 3 (`72403769`) | macOS, 4 ranks | `poiseuilleflowtest.py`: generate with `hlb-gmy-cli`, run HemeLB, compare the velocity profile | 3 tests passed; velocity within 1e-3 m/s of the analytical profile. The first attempt with the sample profile's 0.75 mm iolets gave 0 inlet and 0 outlet sites and no flow, which led to the 1.5 mm iolets and the "only just reaches" warning |
| 2026-09-28 | Plain `pip install '.[gui]'` with VMTK from conda | macOS, conda env `gmy-tool` (Python 3.8.18, VMTK 1.5.0, CGAL 5.6) | The command that failed in the HemeLB Made Easy tutorial, without flags; the geometry-tool tests; `vmtk_requirement()` with and without VMTK present | Built and installed; 59 tests passed. With VMTK: no VMTK requirement; without: `vmtk ~= 1.5` kept. Before the CMake change the same command found Homebrew CGAL 6 and failed |
| 2026-09-28 | Exact versions and lock files | macOS; conda solves for osx-64 and linux-64 | Solved the exactly pinned environment for both platforms (linux-64 with a simulated glibc 2.39) and wrote lock files, checking every package URL exists; created a new environment from the osx-64 lock; installed the Python tools and, with plain `pip install './geometry-tool[gui]'`, the geometry tool using the pinned build tools; ran both test suites | Both solves succeeded (220 and 266 packages; all URLs found). The locked environment has Python 3.11.8, VMTK 1.5.0 and CGAL 5.6.1. Without the header fix the plain install failed on CGAL's `iterator.h`; with it, installed. Geometry tool 59 passed; Python tools 31 passed (with `HEMELB_TESTS_DIR`) |

#### Python tools

| Date | Change (commits) | Platform | What was run | Result |
| --- | --- | --- | --- | --- |
| 2026-09-27 | `xdrlib` replaced by `hlb.utils.pyxdr`, Cython 3 (`955870ea`) | macOS | `pyxdr` compared with `xdrlib` on fixed encodings and 3,000 random byte strings; test suite; extraction dump (11,165 lines), offset and geometry parsers, site counts, self-consistency check compared with the previous version | Same values, positions and exceptions as `xdrlib`. Python 3.11 with Cython 0.29.37: 29 passed; 3.11 with Cython 3.3.0: 29 passed; 3.13 with Cython 3.3.0: 20 passed, 9 skipped (they need `xdrlib`). All outputs byte-identical |
| 2026-09-28 | `.pxd` files in the sdist (`0c696dd7`) | macOS | tox from a clean `git archive` (the first CI run after `955870ea` failed on every Python version because the sdist lacked them) | 3.11: 29 passed; 3.13: 20 passed, 9 skipped; outputs byte-identical |
| 2026-09-28 | Dump tool arguments (`72403769`) | macOS, Python 3.11 | Test suite; dump of the stored extraction file | 31 tests passed; output byte-identical |

#### Continuous integration

| Date | Commit | Workflows | Result |
| --- | --- | --- | --- |
| 2026-09-27 | `86baf636` | All four, first full run on the fork | All passed after the GUI tests were made to skip without wxPython (the first Geometry tool run had failed on that import) |
| 2026-09-28 | `7864e1d0` (pre-squash) | Geometry tool: lint and Python 3.8 to 3.11 (6 jobs). Python tools: lint and Python 3.8 to 3.13 (7). Main application: code checks and GCC 11, 12 and 13 in fluid-only and RBC mode (7). Install script: Ubuntu 24.04 and macOS 14 (2) | All 22 jobs passed |
| 2026-09-28 | `29a5d0be` (squashed history, exact versions, lock files) | The four workflows, now with the install job on ubuntu-24.04, macos-14 and macos-15-intel (23 jobs) | All 23 jobs passed |

Documentation-only commits (`c9b8e012`, `c40ea158`, `8beba9a2` before the
squash) do not trigger the workflows. Their check was that every
relative link and anchor in `doc/`, `README.md` and this file resolves.
