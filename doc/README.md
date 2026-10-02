# HemeLB documentation

These guides describe `feat/scalability-input-improvements`. New XML and
extraction/checkpoint output use version 6, with pressure in Pa. Existing
HemePure XML3 and HemeLB XML5 inputs load directly with their legacy units;
extraction readers and checkpoint loading accept versions 4, 5, and 6.
Use the solver and Python tools from this branch together.

## Start here

1. [Install HemeLB and all related tools](user/install.md).
2. [Run the example](user/getting-started.md) and inspect its output.
3. [Generate a geometry](user/geometry-tool.md) from your own STL surface.
4. [Configure the simulation](user/XmlConfiguration.md), including field output.
5. [Run the simulation](https://github.com/lepotatoguy/hemelb/blob/feat/scalability-input-improvements/doc/user/main-application.md#run).
6. [Analyse the results](user/python-tools.md).
7. (Optional) [Resume from a saved checkpoint](user/checkpoints.md) if the simulation stops before it finishes.

| Task | Reference |
| :--- | :--- |
| Manual installation or cluster build | [Build and run](user/main-application.md) |
| Change lattice, collision model, or boundary implementation | [CMake options](user/CMakeOptions.md) |
| Load legacy inputs or tune decomposition and readers | [Scalability and inputs](user/scalability-and-inputs.md) |
| Choose Windkessel, Yang, sponge/LES, or elastic models | [CPU models](user/cpu-models.md) |
| Couple to an external flow/pressure program | [Coupling](user/coupling.md) |
| Seed and emit passive particles | [Tracers](user/tracers.md) |
| Select output regions, fields, precision, and time windows | [Field extraction](user/extraction.md) |
| Profile and tune CPU execution | [CPU performance](dev/cpu-performance.md) |
| Compare branching vessels and collision models | [CPU benchmarks](dev/representative-cpu-benchmarks.md) |
| Reproduce CPU model and compatibility checks | [CPU verification](dev/cpu-verification.md) |
| Compare CPU capabilities and measured results | [Comparison and roadmap](dev/comparison-and-roadmap.md) |
| Use a per-site velocity profile | [Non-cylindrical velocity inlets](user/non-cylindrical-velocity-inlets.md) |
| Try a complete STL/profile/GMY/XML case | [Example files](../examples/README.md) |
| Diagnose installation or simulation errors | [Troubleshooting](user/troubleshooting.md) |
| Develop and test the code | [Developer guide](dev/README.md) |
| Inspect binary file layouts | [Geometry](dev/file-formats/geometry.md), [extraction](dev/file-formats/extraction.md), [offsets](dev/file-formats/offset.md) |
| Understand changes in this fork | [Changelog](../CHANGELOG.md) |

## Files and units

| File | Purpose | Units and interpretation |
| :--- | :--- | :--- |
| `.stl` | Triangulated vessel surface | Coordinates use the units selected in the geometry profile |
| `.pr2` | YAML geometry profile | Centres, radii, seed point, and voxel size use STL units; time settings use seconds |
| `.gmy`, `.gmy+` | Voxelised geometry and boundary links | Integer lattice coordinates; physical origin and voxel size come from the XML |
| `.xml` | Simulation configuration | Version 6: positions in metres, times in seconds, pressures in Pa; legacy XML3/5 retains mmHg |
| `.xtr` | Extracted fields or checkpoint distributions | Version 6 output stores lattice values and conversion metadata; the bundled reader returns velocity in m/s and pressure in Pa |
| `.off` | Companion extraction offsets | Byte offsets for the MPI ranks that wrote the data; required for checkpoint loading |
| `restart.xml` | Saved configuration and supported model state | Written beside `Checkpoints/<step>/distributions.xtr`; preserve the checkpoint folder and companion offsets |
| `tracers.csv` | Passive particle trajectories | Time in seconds, positions in metres, velocity in m/s |

A normal run creates `report.txt`, `report.xml`, and an `Extracted/` directory
inside the folder passed to `-out`. Field files are created only when the XML
requests them. See [build and run](user/main-application.md#run).

## Glossary

| Term | Meaning |
| :--- | :--- |
| Lattice site | A point in the regular simulation grid; fluid sites hold the simulated fluid |
| Voxel size | Distance between neighbouring lattice sites |
| Block | A group of sites distributed between MPI ranks; the geometry tool writes blocks of 8×8×8 sites |
| Iolet | An inlet or outlet, defined by its position, normal, and boundary condition |
| Seed point | A point inside the desired fluid region, used to keep the closest surface piece during geometry preparation |
| MPI rank | One process in the parallel solver run |
| Extraction | Fields sampled from the simulation at selected sites and timesteps |
| Checkpoint | Saved fluid distributions used to resume a run; keep the matching `.off` file |

## For developers

The [developer guide](dev/README.md) explains build folders, test suites, and
CI triggers. Implementation notes cover [geometry reading](dev/geometry-reading.md),
[checkpoint loading](dev/checkpoint-restart.md), and
[legacy components](dev/legacy-code.md). Historical machine notes are in
[machine-specific build notes](user/machine-specific-build-notes/archer2.md).
