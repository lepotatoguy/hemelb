# Restarting a checkpoint with a different MPI process count

A fluid checkpoint can be restarted with a different number of MPI
processes from the run that saved it. The checkpoint file format is
unchanged: version 5, one double-precision distributions field, no field offsets.
The [user workflow](../user/checkpoints.md) explains saving files and configuring
a restart.

## How the reader works

The reader (`Code/extraction/LocalDistributionInput.cc`) treats the saved
grid position as the identity of a site:

1. It checks the checkpoint header, offset file, site count, and
   distribution field against the current run.
2. Current processes divide the saved site records evenly between them for
   reading, regardless of how many processes wrote the file.
3. Each process finds the current owner of every site it read, and the
   processes exchange site records with `MPI_Alltoallv` in batches of at
   most 64 MiB per round.
4. Each owner copies the saved distributions into its site storage. Invalid,
   repeated, or missing sites are errors on every rank.

The offset file is still required. It gives the start and length of a
checkpoint record; its saved process count no longer has to match the
current one. The file layouts are described in
[extraction.md](file-formats/extraction.md) and
[offset.md](file-formats/offset.md).

The reader assumes the timestep is the first 8 bytes of each record, which
holds because the I/O rank (rank 0) writes it first
(`LocalPropertyOutput.cc`); a `static_assert` on `IOCommunicator::IO_RANK`
keeps this true.

## Requirements and limits

- The restarted run must have the same fluid site coordinates and the same
  number of distributions per site. Both are checked.
- The voxel size and origin in the checkpoint header must match the current
  geometry (to within 1e-9 of a voxel). `SimBuilder` passes them to
  `CheckpointInitialCondition`, and a mismatch is an error,
  so a checkpoint cannot be loaded into a different geometry that happens to
  have the same site coordinates.
- A checkpoint and its offset file must come from the same run.

## Testing

`Code/tests/checkpoint_restart_mpi.py` runs the bundled `large_cylinder`
case for four timesteps, restarts from timestep 2 with a different process
count, and compares the distributions at timestep 4 by grid position. It
also checks that a checkpoint is rejected by a run with a different voxel
size or origin (these rejection cases run on one rank):

```sh
python3 Code/tests/checkpoint_restart_mpi.py --hemelb /path/to/hemelb
```

`ctest` runs it (test `checkpoint-restart-mpi`), and so does the "Main
application" CI workflow. Extra `mpirun` flags (for example
`--oversubscribe`) can be passed in the `MPIRUN_FLAGS` environment variable.
