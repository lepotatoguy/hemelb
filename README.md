# HemeLB: haemodynamic simulation with lattice Boltzmann

[![Main application](https://github.com/lepotatoguy/hemelb/actions/workflows/main-app.yml/badge.svg?branch=feat%2Fscalability-input-improvements)](https://github.com/lepotatoguy/hemelb/actions/workflows/main-app.yml)
[![Python tools](https://github.com/lepotatoguy/hemelb/actions/workflows/py-hemetools.yml/badge.svg?branch=feat%2Fscalability-input-improvements)](https://github.com/lepotatoguy/hemelb/actions/workflows/py-hemetools.yml)
[![Geometry tool](https://github.com/lepotatoguy/hemelb/actions/workflows/gmy-tool.yml/badge.svg?branch=feat%2Fscalability-input-improvements)](https://github.com/lepotatoguy/hemelb/actions/workflows/gmy-tool.yml)

HemeLB simulates fluid flow through complex geometries, including vessel
networks, using the lattice Boltzmann method and MPI. The geometry tool turns
an STL surface into solver inputs; the Python tools read and convert results.

This branch adds CPU boundary models, sparse geometry setup, runtime
decomposition, coupling, and passive tracers. It reads existing HemePure XML3
and HemeLB XML5 inputs and extraction/checkpoint versions 4, 5, and 6. New XML
and output use version 6, with pressure in Pa. See the
[changelog](CHANGELOG.md) and [compatibility guide](doc/user/scalability-and-inputs.md).

## Install

On macOS or Debian/Ubuntu Linux, from a terminal:

```sh
git clone --branch feat/scalability-input-improvements https://github.com/lepotatoguy/hemelb.git
cd hemelb
Scripts/install_hemelb.sh
```

The script builds the solver into `~/.local/hemelb` and installs the geometry
and Python tools into a conda environment named `gmy-tool`. It installs system
packages unless `--no-system-deps` is given. For a solver-only installation,
add `--no-gmy-tool`.

```sh
export PATH="$HOME/.local/hemelb/bin:$PATH"
conda activate gmy-tool
```

Follow the [quick-start walkthrough](doc/user/getting-started.md) to run a bundled
example and inspect its results. The root-level [examples folder](examples/README.md)
contains the matching STL, geometry-tool profile, GMY, and XML configuration.

## Documentation

| Task | Guide |
| :--- | :--- |
| Install, customize paths, or resolve setup problems | [Installation](doc/user/install.md) |
| Run your first simulation | [Quick start](doc/user/getting-started.md) |
| Prepare your own STL and inlet/outlet profile | [Geometry tool](doc/user/geometry-tool.md) |
| Choose units, boundaries, and outputs | [XML configuration](doc/user/XmlConfiguration.md) |
| Build manually or on a cluster | [Build and run](doc/user/main-application.md), [CMake options](doc/user/CMakeOptions.md) |
| Restart from saved fluid distributions | [Checkpoint workflow](doc/user/checkpoints.md) |
| Read results in Python or export them for ParaView | [Python tools](doc/user/python-tools.md) |
| Tune geometry loading or use existing inputs | [Scalability and compatibility](doc/user/scalability-and-inputs.md) |
| Profile and tune CPU execution | [CPU performance](doc/dev/cpu-performance.md) |
| Select CPU boundaries and collision models | [CPU models](doc/user/cpu-models.md) |
| Exchange flow and pressure with a peer | [Coupling](doc/user/coupling.md) |
| Track passive particles | [Tracers](doc/user/tracers.md) |
| Select fields, regions, and sample times | [Field extraction](doc/user/extraction.md) |
| Run tests or understand the implementation | [Developer guide](doc/dev/README.md) |

The [documentation index](doc/README.md) lists the guides and defines common
terms. In-repository guides describe this branch; the older HemeLB Made Easy
[tutorial](https://docs.google.com/document/d/1_3WR3MR7mFyE9LxzcSeXy--G3qgnA3TBxO8aDqUDW2Q/edit?usp=sharing)
may use different installation steps or formats.

## Project and license

HemeLB began at University College London. Contributors and institutions are
listed in [AUTHORS](AUTHORS); the code is licensed under the
[LGPL](LICENSE). HemePure-derived components retain the notices in
[COPYING.HemePure](COPYING.HemePure). The upstream repository is
[hemelb-codes/hemelb](https://github.com/hemelb-codes/hemelb).

## Publications

- M.D. Mazzeo & P.V. Coveney, "HemeLB: A high performance parallel
  lattice-Boltzmann code for large scale fluid flow in complex
  geometries", Comput. Phys. Commun. (2008)
  https://doi.org/10.1016/j.cpc.2008.02.013

- D. Groen, J. Hetherington, H.B. Carver, R.W. Nash, M.O. Bernabeu,
  "Analysing and modelling the performance of the HemeLB
  lattice-Boltzmann simulation environment", J. Comput. Sci. (2013).
  https://doi.org/10.1016/j.jocs.2013.03.002

- R.W. Nash, H.B. Carver, M.O. Bernabeu, J. Hetherington, D. Groen, T.
  Krüger, P.V. Coveney, "Choice of boundary condition for
  lattice-Boltzmann simulation of moderate-Reynolds-number flow in
  complex domains", Phys. Rev. E (2014).
  https://doi.org/10.1103/PhysRevE.89.023303
