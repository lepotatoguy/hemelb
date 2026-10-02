# Offset files

Each extraction or checkpoint file (`name.xtr`) has an offset file
(`name.off`) next to it. It stores where each MPI rank of the simulation wrote its contiguous
chunk of data. The checkpoint reader also uses the offsets when restarting
with a different number of ranks. See
[checkpoint-restart.md](../checkpoint-restart.md) for the current behavior.

The file has a header and a body.

## Header
This contains, encoded as uint32, in order:
 - HemeLbMagicNumber
 - OffsetMagicNumber (0x6F666604, that is "off" followed by 4)
 - OffsetVersionNumber (currently 1)
 Then, encoded as an int32 (because MPI defines it as signed)
 - Number of ranks

## Body

This holds number of ranks + 1 entries, XDR encoded as uint64. Each
gives the offset (in number of bytes) into the extraction file of the
start of that rank's chunk. The final value holds the
just-past-the-end value.

The byte length of a complete timestep record, including its 8-byte timestep
number, is `data[n_ranks] - data[0]`. These offsets describe the first record;
subsequent records use the same stride. For single-timestep file patterns,
HemeLB removes `%d` before choosing the shared offset filename, so
`checkpoint_%d.xtr` uses `checkpoint_.off`.

For modern checkpoints, `Checkpoints/distributions.off` is shared by the
`Checkpoints/<step>/distributions.xtr` files. The generated restart XML names
the matching offsets explicitly.
