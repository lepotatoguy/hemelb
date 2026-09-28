# HemeLB documentation

HemeLB simulates blood flow (or any fluid flow) through complex 3D shapes
such as vessel networks, using the lattice Boltzmann method. It runs in
parallel with MPI.

## New to HemeLB? Start here

1. **Install** HemeLB, the geometry tool and the Python tools with one
   script: [Installing HemeLB](user/install.md).
2. **Do a first run** from a surface to results with the sample files in the
   repository: [Getting started](user/getting-started.md).
3. **Use your own geometry:** [Geometry tool](user/geometry-tool.md) explains
   how to turn an STL surface into a HemeLB geometry, with the GUI
   (`hlb-gmy-gui`) or on the command line (`hlb-gmy-cli`), and how to place
   inlets, outlets and the seed point.
4. **Configure a simulation** (time step, boundary conditions, outputs):
   [XML configuration](user/XmlConfiguration.md).
5. **Analyse results:** [Python tools](user/python-tools.md).

Every HemeLB run follows the same four steps:

| Step | What happens | Guide |
| --- | --- | --- |
| 1. Geometry | An STL surface and a profile become a `.gmy` geometry and a `.xml` configuration | [geometry-tool.md](user/geometry-tool.md) |
| 2. Configuration | Edit the `.xml`: time step, run length, boundary conditions, which results to save | [XmlConfiguration.md](user/XmlConfiguration.md) |
| 3. Simulation | `mpirun -n N hemelb -in config.xml -out results` | [main-application.md](user/main-application.md) |
| 4. Analysis | Convert the saved `.xtr` files to text or other formats | [python-tools.md](user/python-tools.md) |

## For developers

- [Building HemeLB by hand](user/main-application.md) and
  [CMake options](user/CMakeOptions.md) (the install script does this for
  you).
- [Developer notes](dev/README.md): how the code reads geometry, restarts
  checkpoints and lays out its files, and **how to run every test suite**.
- [CHANGELOG.md](../CHANGELOG.md): changes in this fork and how each was
  checked.
- Machine-specific build notes: [user/machine-specific-build-notes](user/machine-specific-build-notes).

## Glossary

| Term | Meaning |
| --- | --- |
| STL | A file describing a surface as triangles; the shape of your vessel |
| Profile (`.pr2`) | The geometry tool's settings: STL, units, voxel size, inlets, outlets, seed point, time step. Plain text (YAML) |
| Voxel, lattice site | The simulation divides space into small cubes of one size (the voxel size); each fluid cube is a site |
| Block | A group of 8×8×8 sites; the unit HemeLB shares out between processes |
| Geometry (`.gmy`) | The voxelised vessel: which sites are fluid and where the walls, inlets and outlets cut them |
| Configuration (`.xml`) | The simulation settings HemeLB reads |
| Inlet, outlet, iolet | Where fluid enters or leaves; "iolet" means either. Each is a disc with a centre, a normal pointing into the fluid and a radius |
| Seed point | Any point inside the fluid; the geometry tool keeps the part of the surface closest to it |
| Extraction file (`.xtr`) | Results saved during a run (for example velocity and pressure) |
| MPI process (rank) | One of the parallel copies of HemeLB started by `mpirun -n N` |
| Checkpoint | A saved state that a later run can restart from |
