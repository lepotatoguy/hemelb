# Building and running HemeLB

The quickest way to get a working HemeLB is the install script, which does
everything on this page for you: [Installing HemeLB](install.md). Read on if
you want to build by hand, for example on a cluster.

## What you need

- Linux or macOS.
- A C++20 compiler. CI builds with GCC 11, 12 and 13; on macOS, AppleClang
  from the Xcode Command Line Tools works.
- CMake 3.13 or newer (CMake 4 works).
- MPI (version 3.0 or later), for example Open MPI or MPICH.

These libraries are also needed. Those marked * are downloaded and built for
you if they are missing (see "Build" below):

- Boost* header-only libraries, version 1.54 or newer
- CTemplate*
- ParMETIS* (graph partitioning)
- TinyXML* (XML parsing)
- zlib* (compression; usually already on your system)
- Catch2* (only for the unit tests)
- MPWide* (only for multiscale builds)
- HDF5* and VTK* version 9 (only for the red blood cell model, `HEMELB_BUILD_RBC=ON`)

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
