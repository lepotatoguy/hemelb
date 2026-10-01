# HemeLB documentation

These guides describe `fix/hemelb-improvements`. The solver reads XML version 5,
uses mmHg for pressure, and writes extraction/checkpoint version 5. The Python
reader accepts extraction versions 4 and 5. Check the branch before following a
guide: other branches may use different versions and units.

## Start here

1. [Install](user/install.md) the solver and tools.
2. [Run the bundled cylinder](user/getting-started.md) and inspect its output.
3. [Generate a geometry](user/geometry-tool.md) from your own STL surface.
4. [Configure the simulation](user/XmlConfiguration.md), including field output.
5. [Analyse the results](user/python-tools.md) or [restart a checkpoint](user/checkpoints.md).

| Task | Reference |
| :--- | :--- |
| Manual installation or cluster build | [Build and run](user/main-application.md) |
| Change lattice, collision model, or boundary implementation | [CMake options](user/CMakeOptions.md) |
| Use a per-site velocity profile | [Non-cylindrical velocity inlets](user/non-cylindrical-velocity-inlets.md) |
| Diagnose installation or simulation errors | [Troubleshooting](user/troubleshooting.md) |
| Develop and test the code | [Developer guide](dev/README.md) |
| Inspect binary file layouts | [Geometry](dev/file-formats/geometry.md), [extraction](dev/file-formats/extraction.md), [offsets](dev/file-formats/offset.md) |
| Understand changes in this fork | [Changelog](../CHANGELOG.md) |

## Files and units

| File | Purpose | Units and interpretation |
| :--- | :--- | :--- |
| `.stl` | Triangulated vessel surface | Coordinates use the units selected in the geometry profile |
| `.pr2` | YAML geometry profile | Centres, radii, seed point, and voxel size use STL units; time settings use seconds |
| `.gmy` | Voxelised geometry and boundary links | Integer lattice coordinates; physical origin and voxel size come from the XML |
| `.xml` | Simulation configuration | Version 5: positions in metres, times in seconds, pressures in mmHg |
| `.xtr` | Extracted fields or checkpoint distributions | Version 5 output; velocity in m/s, pressure in mmHg; grid coordinates are integer lattice positions |
| `.off` | Companion extraction offsets | Byte offsets for the MPI ranks that wrote the data; required for checkpoint loading |

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
