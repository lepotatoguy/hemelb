# HemeLB build options

Pass these to CMake with `-D`, for example
`cmake -S . -B build -DHEMELB_LATTICE=D3Q19`. You can also browse and change
them interactively with `ccmake build`. They are defined in
`CMake/GlobalOptions.cmake` and `CMake/HemeLbOptions.cmake`.

This table covers frequently used options on `fix/hemelb-improvements`.
See the linked CMake files for the complete list. For build-folder and generator
choices, follow [build and run](main-application.md).

Most users only need the defaults. The model options (lattice, collision,
boundaries) are fixed when HemeLB is compiled, so to try another model you
build another executable (give it a different name with `HEMELB_EXECUTABLE`).

## Where things are installed

| Option | Default | Meaning |
| --- | --- | --- |
| `CMAKE_INSTALL_PREFIX` | system default | Where HemeLB is installed |
| `HEMELB_DEPENDENCIES_INSTALL_PREFIX` | same as above | Where the super build installs dependencies it builds, and an extra place to search for them |
| `HEMELB_EXECUTABLE` | `hemelb` | Name of the program |

## What gets built

| Option | Default | Meaning |
| --- | --- | --- |
| `HEMELB_BUILD_TESTS` | ON | Build `hemelb-tests` |
| `HEMELB_BUILD_RBC` | OFF | Resolved red blood cells (immersed boundary method); needs HDF5 and VTK 9 |
| `HEMELB_BUILD_MULTISCALE` | OFF | Multiscale coupling; needs MPWide |
| `HEMELB_BUILD_COLLOIDS` | OFF | Colloid particles |
| `HEMELB_BUILD_DEBUGGER` | ON | Built-in debugger, started with `hemelb -debug 1` |

## Lattice Boltzmann model

| Option | Default | Choices |
| --- | --- | --- |
| `HEMELB_LATTICE` (velocity set) | D3Q15 (D3Q19 with RBC) | D3Q15, D3Q19, D3Q27, D3Q15i |
| `HEMELB_KERNEL` (collision) | LBGK (GuoForcingLBGK with RBC) | LBGK, EntropicAnsumali, EntropicChik, MRT, TRT, NNCY, NNCYMOUSE, NNC, NNTPL, GuoForcingLBGK |
| `HEMELB_WALL_BOUNDARY` | SIMPLEBOUNCEBACK | SIMPLEBOUNCEBACK, BFL, GZS, JUNKYANG |
| `HEMELB_INLET_BOUNDARY` | NASHZEROTHORDERPRESSUREIOLET | NASHZEROTHORDERPRESSUREIOLET, LADDIOLET |
| `HEMELB_OUTLET_BOUNDARY` | NASHZEROTHORDERPRESSUREIOLET | NASHZEROTHORDERPRESSUREIOLET, LADDIOLET |
| `HEMELB_STENCIL` (RBC interpolation) | FourPoint | TwoPoint, ThreePoint, FourPoint, CosineApprox |
| `HEMELB_USE_VELOCITY_WEIGHTS_FILE` | OFF | ON reads per-site weights for `subtype="file"` velocity inlets ([non-cylindrical-velocity-inlets.md](non-cylindrical-velocity-inlets.md)) |

The inlet and outlet choice decides which conditions the XML may use:
NASHZEROTHORDERPRESSUREIOLET needs **pressure** conditions (this is what the
geometry tool writes), LADDIOLET needs **velocity** conditions. A mismatch
stops HemeLB with "XML configuration for inlet ... not consistent with
compile-time choice of boundary condition". See
[XmlConfiguration.md](XmlConfiguration.md).

### Example: velocity inlet, pressure outlet

Configure a separate build with `-DHEMELB_INLET_BOUNDARY=LADDIOLET` and
`-DHEMELB_OUTLET_BOUNDARY=NASHZEROTHORDERPRESSUREIOLET`. Change the XML inlet
condition to a velocity subtype and keep pressure outlets. Rebuild after
changing either option. Renaming the executable alone does not select a model.

## Logging and checks

| Option | Default | Meaning |
| --- | --- | --- |
| `HEMELB_LOG_LEVEL` | Info | Critical, Error, Warning, Info, Debug or Trace |
| `HEMELB_VALIDATE_GEOMETRY` | OFF | Extra consistency checks while reading the geometry |
| `HEMELB_USE_ALL_WARNINGS_GNU` | ON | Compiler warnings for developers |

## Performance

| Option | Default | Meaning |
| --- | --- | --- |
| `HEMELB_SUBPROJECT_MAKE_JOBS` | 1 | Parallel jobs when the super build compiles HemeLB and its dependencies. On HPC login nodes a large number can hit process limits; use a small one (for example 4) if the build fails mysteriously |
| `HEMELB_USE_SSE3` | ON on x86_64, OFF elsewhere | SSE3 vector instructions |
| `HEMELB_COMPUTE_ARCHITECTURE` | AMDBULLDOZER | INTELSANDYBRIDGE, AMDBULLDOZER, NEUTRAL, ISBFILEVELOCITYINLET |
| `HEMELB_POINTPOINT_IMPLEMENTATION` | Coalesce | MPI point-to-point method: Coalesce, Separated or Immediate |
| `HEMELB_GATHERS_IMPLEMENTATION`, `HEMELB_ALLTOALL_IMPLEMENTATION` | Separated | Separated or ViaPointPoint |
| `HEMELB_SEPARATE_CONCERNS` | OFF | Communicate for each concern separately |

## How dependencies are found

The super build chooses per dependency with `DEPS_<NAME>`: `Auto` (default:
use the system copy if found, otherwise build it), `System` or `Build`; for
example `-DDEPS_PARMETIS=Build`.

If CMake cannot find a library you have installed, tell it where:

| Library | Variables |
| --- | --- |
| Boost | `BOOST_ROOT`, or `BOOST_INCLUDEDIR` and `BOOST_LIBRARYDIR` |
| CTemplate | `CTEMPLATE_INCLUDE_DIR`, `CTEMPLATE_LIBRARIES` |
| METIS | `METIS_ROOT` or `METIS_DIR`, or `METIS_INCLUDE_DIR` and `METIS_LIBRARY` |
| ParMETIS | `ParMETIS_INCLUDE_DIR`, `ParMETIS_LIBRARY` |
| TinyXML | `TINYXML_INCLUDE_DIR`, `TINYXML_LIBRARIES` |
| VTK | `VTK_DIR` |

For code-only builds, pass dependency hints to that configure command. In a
super build, some hints must also be passed to the inner configure; the forwarded
variables are listed in `CMake/HemeLbOptions.cmake`. A dependency prefix via
`HEMELB_DEPENDENCIES_INSTALL_PREFIX` is usually easier to keep consistent.
