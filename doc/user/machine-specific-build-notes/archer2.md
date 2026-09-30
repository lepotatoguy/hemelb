# Building on ARCHER2

ARCHER2 is an HPE Cray EX system (documentation: https://docs.archer2.ac.uk).
These notes were written in December 2021 and have not been checked since;
module names and versions change, so check `module avail` first. HemeLB
needs a C++20 compiler (GCC 11 or newer).

```sh
module load cmake/3.21.3
module load PrgEnv-gnu
module swap gcc gcc/11.2.0
module load boost/1.72.0
module load parmetis/4.0.3
module load cray-hdf5-parallel

thisdir=$(readlink -f $(dirname $BASH_SOURCE))
build_dir=$thisdir/build
install_dir=$thisdir/install
source_dir=path/to/hemelb

cmake -S $source_dir -B $build_dir -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$install_dir -DHEMELB_BUILD_RBC=ON \
      -DHEMELB_SUBPROJECT_MAKE_JOBS=4
cmake --build $build_dir
```

`HEMELB_BUILD_RBC=ON` builds the red blood cell model; leave it out for plain
fluid runs. Keep `HEMELB_SUBPROJECT_MAKE_JOBS` small on login nodes, which
limit the number of processes. See [CMakeOptions.md](../CMakeOptions.md) for
the other options.
