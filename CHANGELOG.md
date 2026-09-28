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
- Profiles accept decimal exponents written without a point (`1e-5`), which
  YAML reads as text; they failed with "invalid hexadecimal floating-point
  string".

### Build

- Builds with AppleClang (`SimBuilder.h` template call), GCC 13
  (`<cstdint>` in `io/formats/geometry.h`) and CMake 4 (TinyXML and ParMETIS
  CMake minimums).
- The super build finds dependencies it builds itself in one CMake pass.
- `geometry-tool/conda-environment.yml` uses Python 3.11 and lists the build
  requirements.

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
