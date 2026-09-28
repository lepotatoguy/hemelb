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

### Added

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

Results as recorded when each change was made. Test counts grow as tests are
added, so earlier numbers are lower.

| Date | Change | Platform | Result |
| --- | --- | --- | --- |
| 2026-09-27 | 64-bit decomposition | macOS, AppleClang | `hemelb-tests`: 77 cases, 32,403 assertions passed. `large_cylinder`, 200 steps, 4 ranks: same per-rank site counts and byte-identical `whole.xtr` as upstream `432d3386` |
| 2026-09-27 | Geometry I/O guards | macOS, AppleClang | 79 cases, 32,411 assertions passed; `large_cylinder` output byte-identical to the previous change |
| 2026-09-27 | Geometry validation, empty MPI buffers | macOS, AppleClang | 87 cases, 32,443 assertions passed; 4-rank empty-gather test passed; `large_cylinder` output byte-identical to the previous change |
| 2026-09-27 | Checkpoint restart | macOS, AppleClang | `checkpoint_restart_mpi.py` passed for 2 to 1, 1 to 2, 2 to 4 and 1 to 1 processes (all distributions matched); 73 cases, 32,386 assertions passed |
| 2026-09-27 | Install script, all changes merged | macOS 27 arm64, AppleClang 21, CMake 4.3.2 | Fresh clone: 87 cases, 32,443 assertions passed; geometry and Python tools installed and imported |
| 2026-09-27 | Install script, all changes merged | Ubuntu 24.04 amd64 (Docker), GCC | Fresh clone: 87 cases, 32,443 assertions passed; `large_cylinder` 200 steps on 4 ranks finished; tools installed; `hlb-gmy-cli` ran |
| 2026-09-28 | Error messages, checkpoint geometry check | macOS, AppleClang | 89 cases, 32,448 assertions passed; `ctest` (unit tests and restart script) passed; restarts 2 to 1, 1 to 2, 2 to 4, 1 to 1 matched exactly and mismatched voxel size and origin were rejected; SixBranch, `four_cube` and a missing `.gmy` gave the new messages |
| 2026-09-28 | Geometry tool changes | macOS, Python 3.11 | 59 tests passed; without wxPython 53 passed and 1 module skipped; `test.gmy` byte-identical to the stored reference |
| 2026-09-28 | Poiseuille flow test | macOS, 4 ranks | 3 tests passed; velocity within 1e-3 m/s of the analytical profile |
| 2026-09-28 | Python tools | macOS, Python 3.11 | 31 tests passed; extraction dump byte-identical |
