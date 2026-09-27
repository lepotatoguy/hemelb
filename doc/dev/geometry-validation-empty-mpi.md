# Geometry validation and empty MPI buffers

## Changes

Geometry block counts and block IDs now use checked 64-bit arithmetic. The reader checks that the declared header fits both its supported read size and the actual file before it allocates block storage. Zero dimensions and impossible fluid-site or decompressed-data counts produce errors.

Geometry records now fail with an error when the file ends early, decompression does not produce the declared number of bytes, or a site record is incomplete. The error for an incomplete site includes its block and site number. The reader checks that all declared compressed block data is present in the file. The existing 64 MiB compressed-block limit remains in place.

MPI gather helpers now accept empty vectors for zero-count transfers. Scalar gathers size their receive vector to the communicator, and all-to-all helpers reject buffers whose length does not match the communicator. Variable-count gathers validate their counts and use integer offsets without taking an element from an empty vector.

## Validation

The test suite passed 87 cases with 32,443 assertions. New tests use modified copies of the repository's `large_cylinder.gmy` to check oversized dimensions, impossible decompressed lengths, truncated block data, corrupt compressed data, and an incomplete site record. A four-rank MPI test passed for empty gathers.

The provided `large_cylinder.xml` and `large_cylinder.gmy` completed 200 simulation steps on four MPI ranks. The `whole.xtr` extraction was byte identical to the preceding geometry reliability branch's sample run.
