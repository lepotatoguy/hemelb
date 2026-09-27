# Restarting a checkpoint with a different MPI process count

HemeLB can now restart a fluid checkpoint with a different number of MPI
processes from the run that saved it. This change is in the checkpoint reader;
it does not change the checkpoint file format or the fluid calculation.

## What changed

Previously, each process read the block of sites written by the process with
the same rank. The reader required the saved and current process counts to
match. It also expected each site to have the same local index after restart.

The reader now treats the saved grid position as the identity of a site:

1. It checks the checkpoint header, offset file, site count, and distribution
   field against the current run.
2. Current processes divide the saved site records between them for reading.
   They do not need to have the same ranks as the processes that wrote the file.
3. Each process finds the current owner of every site it read. The processes
   exchange site records in batches so each owner receives its sites.
4. Each owner copies the saved distributions into its current site storage.
   The reader rejects invalid, repeated, or missing sites.

The offset file is still required. It identifies the start and length of a
checkpoint record, but its saved process count no longer has to equal the
current process count. The file layouts are described in
[extraction.md](file-formats/extraction.md) and
[offset.md](file-formats/offset.md).

## Requirements and limits

The restarted run must use the same fluid site coordinates and a build with
the same number of distributions per site. The reader checks those properties.
It does not compare the physical voxel size or origin in the checkpoint header
with the current configuration. A checkpoint and its offset file must refer to
the same saved run.

## Verification

The repeatable test is
[`Code/tests/checkpoint_restart_mpi.py`](../../Code/tests/checkpoint_restart_mpi.py).
It runs the bundled `large_cylinder` case for four timesteps, restarts from
timestep 2, and compares the saved distributions at timestep 4 by grid
position. Run it with a built executable and an MPI launcher:

```sh
python3 Code/tests/checkpoint_restart_mpi.py --hemelb /path/to/hemelb
```

Local runs passed for 2 to 1, 1 to 2, 2 to 4, and 1 to 1 processes. In each
case, all 83,640 distribution values across 5,576 sites matched exactly at
timestep 4. The existing unit suite also passed 32,386 assertions in 73 test
cases. These checks use a small geometry; large scale restart performance has
not been measured here.

The implementation is in
[`LocalDistributionInput.cc`](../../Code/extraction/LocalDistributionInput.cc).
The same change also removes an unnecessary `template` qualifier in
[`SimBuilder.h`](../../Code/configuration/SimBuilder.h), which allowed the
application to build with the local AppleClang compiler used for verification.
