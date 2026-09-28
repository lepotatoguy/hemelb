# Building and running HemeLB

The quickest way to get a working HemeLB is the install script, which does
everything on this page for you: [Installing HemeLB](install.md). Read on if
you want to build by hand, for example on a cluster.

## What you need

- Linux or macOS.
- A C++20 compiler. CI builds with GCC 11, 12 and 13; on macOS, AppleClang
  from the Xcode Command Line Tools works.
- CMake 3.13 or newer (CMake 4 works).
- MPI 3.0 or later (the code uses MPI-3 neighbourhood collectives), for
  example Open MPI or MPICH.

These libraries are also needed. The super build downloads and builds any
that are missing (see "Build" below), using the version in the last column:

| Library | Minimum | Built if missing |
| --- | --- | --- |
| Boost (header-only) | 1.77 | 1.77.0 |
| ParMETIS (graph partitioning) | 4.x | 4.0.2 |
| TinyXML (XML parsing) | 2.x | 2.6.2 |
| CTemplate | any | 2.4 |
| zlib (usually already on your system) | any | 1.2.6 |
| Catch2 (unit tests only) | 2.x | 2.13.9 |
| HDF5 (red blood cell model only, `HEMELB_BUILD_RBC=ON`) | 1.8 | 1.8.15 |
| VTK (red blood cell model only) | 9.0 | 9.1.0 |
| MPWide (multiscale builds only) | any | 1.1 |

Which versions were actually tested is listed in the `INSTALL` file at the
top of the repository.

## Get the code

```sh
git clone -b fix/hemelb-improvements https://github.com/lepotatoguy/hemelb.git
cd hemelb
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local/hemelb
cmake --build build -j 4
```

With the top-level folder as the source (`-S .`), this is the **super build**:
it builds any missing dependency first, then HemeLB, and installs everything
into `CMAKE_INSTALL_PREFIX`. You get:

| Program | What it does |
| --- | --- |
| `bin/hemelb` | The simulation |
| `bin/hemelb-confcheck` | Checks a configuration XML without running it |
| `bin/hemelb-tests` | The unit tests |

For each dependency you can choose how the super build gets it with a
`DEPS_<NAME>` option, for example `-DDEPS_PARMETIS=Build`:

| Value | Meaning |
| --- | --- |
| `Auto` (default) | Use the copy on your system if found, otherwise build it |
| `System` | Always use the system copy (fail if missing) |
| `Build` | Always build it |

**Code-only build.** If every dependency is already installed (for example
through a module system), point CMake at the `Code` folder instead:
`cmake -S Code -B build ...`.

**For development**, build the dependencies once with
`cmake -S dependencies -B deps-build -DHEMELB_DEPENDENCIES_INSTALL_PREFIX=...`,
then do code-only builds against them; rebuilding the code is then quicker.

Build options (lattice type, boundary conditions, logging and so on) are
listed in [CMakeOptions.md](CMakeOptions.md). Notes for particular machines
are in [machine-specific-build-notes](machine-specific-build-notes).

## Run

```sh
mpirun -n 4 hemelb -in config.xml -out results
```

| Option | Meaning |
| --- | --- |
| `-in FILE` | The configuration XML (required) |
| `-out FOLDER` | Where to write results. It must not exist yet. Default: `results` next to the XML |
| `-debug 0` or `1` | Start the built-in debugger (default 0) |

Check a configuration before a long run with `hemelb-confcheck config.xml`:
it prints nothing if the file is valid and names the problem if not.

The number of MPI processes (`-n`) can be anything up to the number of
geometry blocks that contain fluid; HemeLB tells you the limit if you ask
for too many. See [Getting started](getting-started.md) for a complete first
run and for what common error messages mean.

## Test

```sh
hemelb-tests          # unit tests; prints "All tests passed" at the end
```

In a build folder you can also run `ctest`, which runs the unit tests and a
checkpoint restart test. All test suites are described in the
[developer notes](../dev/README.md#how-to-run-the-tests).
