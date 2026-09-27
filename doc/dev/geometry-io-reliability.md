# Geometry input and output reliability

Four geometry problems were fixed:

| Area | Earlier behavior | Current behavior |
| --- | --- | --- |
| Block lookup tree | A domain wider than 32,768 blocks could make the tree size calculation wrap and loop forever. | The size calculation uses 32 bits, so all block coordinates supported by the 16-bit geometry index can be covered. |
| Geometry reader | A compressed block larger than its 64 MiB read buffer could prevent the streaming loop from advancing. | The reader reports the offending block number and stops. An empty block data region also returns without attempting a zero-sized buffer allocation. |
| Geometry writer | Failed opens and short writes could leave an apparently successful but incomplete `.gmy` file. Header and block size calculations could exceed the writer's integer limits. | The writer checks file creation, block writes, closes, and the final header write. It rejects geometry sizes beyond its supported limits before writing. |
| Geometry generator | An unknown link cut type was printed, then the site record was left incomplete. | Generation stops with an error that identifies the cut type and site. The current block writer is released during error handling. |

The reader still has a 64 MiB limit for each compressed block. A file with a larger block now fails clearly; reading such files would require a separate change to the buffering strategy.

## Validation

The core test suite passed 79 cases with 32,411 assertions. New tests cover a block coordinate above 32,768 and a modified copy of the repository's `large_cylinder.gmy` sample whose header declares a compressed block larger than 64 MiB.

The provided `large_cylinder.xml` and `large_cylinder.gmy` sample completed a 200-step run with four MPI ranks. Its `whole.xtr` extraction was byte identical to the previous branch's sample run. A separately compiled C++ smoke test created a valid geometry file and confirmed that the writer rejects an invalid output path and an unsupported block count. The changed geometry generator source passed a syntax check. The full geometry tool was not run locally because its available Python environment is built for a different CPU architecture.
