# Troubleshooting

Use the guides from the branch you built. These checks apply to
`feat/scalability-input-improvements`: new XML6 and extraction7, pressure in mmHg,
and TinyXML2. XML3/5 and extraction/checkpoint4/5/6 also load directly. For a repeatable baseline, try the
[first-run example](getting-started.md).

## Installation and imports

| Symptom | Check or action |
| :--- | :--- |
| `hemelb: command not found` | Add your chosen prefix's `bin/` to `PATH`; default `~/.local/hemelb/bin` |
| `hlb-gmy-cli` or analysis command not found | Activate the conda environment chosen at installation |
| Conda environment already exists | Choose another `--env-name`; use the existing environment only if its packages match the required setup |
| Missing `HlbGmyTool.Model.Generation`, `hlb.utils.xdr`, or geometry `BaseSite` | Install the packages with their compiled extensions and run from outside the source-package directories |
| VMTK cannot be installed through pip | Use the locked conda environment in the [geometry guide](geometry-tool.md#installing-the-geometry-tool-manually) |
| macOS GUI reports no screen access | Use the framework-Python launcher setup in the [geometry guide](geometry-tool.md#installing-the-geometry-tool-manually) |
| Architecture/linker mismatch on Apple Silicon | Keep the native solver build separate from the Intel geometry-tool environment; check which compiler, Python, and libraries are active |

`command -v hemelb`, `command -v mpirun`, and `python -m pip --version` identify
the tools your shell selects. If conda activation is unavailable, initialize
conda for your shell or source the installation's `etc/profile.d/conda.sh`.

## Build failures

| Symptom | Check or action |
| :--- | :--- |
| Library not found | Use the [dependency hints](CMakeOptions.md#how-dependencies-are-found); code-only builds do not download missing libraries |
| TinyXML headers/API do not match | This branch needs `tinyxml2.h` and the `tinyxml2` CMake package; use `tinyxml2_DIR` or a dependency prefix |
| Dependency CMake files rejected by CMake 4 | The bundled ParMETIS build supplies `CMAKE_POLICY_VERSION_MINIMUM=3.5`; use this branch's dependency files |
| Build killed or process limit exceeded | Reduce `--jobs` for the installer, or `HEMELB_SUBPROJECT_MAKE_JOBS` for the super build |
| Old compiler or MPI paths remain after changing modules | Configure a new build folder with the desired modules loaded |
| Geometry-tool CGAL headers fail | Use the locked CGAL 5 environment; its build applies the repository's header workaround |

Inspect the first compiler or configure error, not only the final `make` error.
The [build guide](main-application.md) explains the super-build and code-only
layouts.

## Simulation setup

| Message or symptom | Check or action |
| :--- | :--- |
| Geometry file does not exist | Check `<geometry><datafile path="..." /></geometry>`; relative paths start at the XML's folder |
| XML version or units rejected | Use XML6/mmHg for new configurations; retain XML3/5 legacy units when loading old files. Use the exact unit strings in the [XML reference](XmlConfiguration.md) |
| Boundary inconsistent with compile-time choice | Pressure requires `NASHZEROTHORDERPRESSUREIOLET` or `YANGPRESSUREIOLET`; velocity requires `LADDIOLET`, independently for inlets/outlets |
| Geometry uses more inlets/outlets than configured | Keep the XML iolet order and count from the profile that generated the GMY |
| Too many MPI processes for fluid blocks | Reduce the process count to the limit in the error message |
| Yang relaxation-time guard or invalid stencil | Use LBGK, tau at least 0.8, and straight iolets with two interior fluid sites; tau 0.8 means `step_length >= 0.1 * voxel_size^2 * density / viscosity`; see [Yang relaxation-time limit](cpu-models.md#yang-relaxation-time-limit) |
| Reader count/spacing exceeds available ranks | Adjust `<decomposition>` reader settings for the current launch, including restarts; see [scalability](scalability-and-inputs.md) |
| Coupling times out | Check peer timestamps, file paths, exchange units, and startup order; see [coupling](coupling.md) |
| Output directory already exists | Choose a new `-out` folder |
| Launcher cannot start ranks | Check the scheduler allocation and MPI runtime; the launcher must match the linked MPI |

Optionally use `hemelb-confcheck input.xml` to check configuration parsing and
boundary choices. Geometry and referenced-file checks happen during simulation setup.

## Results and restarts

| Symptom | Check or action |
| :--- | :--- |
| Run completes without `.xtr` field files | Add `<properties><propertyoutput ...>`; generated XML has no field output by default |
| No samples at the requested interval | Check inclusive `start`/`stop`, the global timestep phase of `period`, and early termination; see [field extraction](extraction.md) |
| CSV appears to lack timestep values | Timestep numbers are in comment headers, not a column; use the [Python reader](python-tools.md#read-fields-in-python) for arrays |
| Appended binary VTU cannot be read | Use `hlb-extracted-to-vtk`, which writes ASCII VTU; see [ParaView export](python-tools.md#export-for-paraview) |
| VTK conversion places cells incorrectly | Use `hlb-extracted-to-vtk` to export physical coordinates from the extraction header; check that the run used the matching GMY/XML pair |
| Restart cannot find an offset file | Pass the shared `.off` path explicitly; see the [checkpoint workflow](checkpoints.md) |
| Checkpoint geometry or distribution mismatch | Use the original voxel size, origin, fluid-site set, and lattice; keep the corresponding offsets |
| Newer extraction format rejected | The bundled Python reader handles versions 4, 5, 6, and 7; use tools from the branch that wrote other formats |

## Reporting a reproducible failure

Include the branch/commit, operating system and architecture, compiler/MPI
versions, configure command, launch command, and first relevant error. For
geometry failures, include the XML and a small GMY or profile/STL that reproduces
it when they can be shared. Keep the failing inputs unchanged while diagnosing
the issue.
