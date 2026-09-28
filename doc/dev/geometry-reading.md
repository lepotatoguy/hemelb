# Reading, validating and decomposing geometry

This note describes how the main application reads a `.gmy` file, what
it rejects, and how it makes the initial domain decomposition. The file
layout itself is described in [file-formats](file-formats).

## Validation when reading

Before reading, `SimBuilder::ReadGmy` checks that the path from the XML is a
regular file, so a missing file or a folder gives a clear error naming the
path.

`GeometryReader` treats the file as untrusted input. Every failure raises
an `Exception` naming the block (and site, where relevant):

- **Preamble and header.** Block dimensions and block size must be
  positive. Block counts use checked 64-bit arithmetic, and the header must
  fit both the supported read size (`int` range) and the actual file before
  any block storage is allocated.
- **Block records.** A block cannot declare more fluid sites than sites per
  block, a fluid block must have compressed and uncompressed data, and the
  uncompressed length cannot exceed `sitesPerBlock *
  MaxFluidSiteRecordLength`. The sum of declared compressed lengths must be
  present in the file.
- **Decompression.** zlib must finish the stream and produce exactly the
  declared number of bytes.
- **Site records.** `XdrMemReader` checks bounds on every read in all build
  types (it previously relied on `HASSERT`, which only runs in debug
  builds). A block with trailing bytes after its last site is rejected.

Limit: each compressed block must fit the 64 MiB streaming buffer
(`MAX_GMY_BUFFER_SIZE`). Larger blocks are rejected with an error; reading
them would need a change to the buffering strategy.

The block lookup tree (`LookupTree.cc`) sizes itself with a 32-bit counter,
so any domain addressable by the 16-bit block coordinates is supported.

## Writing (geometry tool)

`GeometryWriter` checks file creation, every block write, the final header
write and each close, and rejects geometries whose header or block sizes
exceed its integer limits. `GeometryGenerator` stops with an error on an
unknown link cut type instead of writing an incomplete site record.

## Initial decomposition

`BasicDecomposition` splits blocks between ranks by cumulative fluid site
count using 64-bit integers (it previously used `float`, which cannot
distinguish nearby boundaries above about 16.7 million sites). Every rank
gets at least one non-solid block, and a cumulative count beyond the 64-bit
range is an error. ParMETIS then refines this split.

Because of that rule, a run cannot use more MPI processes than there are
blocks containing fluid. HemeLB then stops with a message giving both
numbers and the largest `mpirun -n` that will work.

## Checks after the domain is built

`SimBuilder` calls `CheckIoletIds` once the domain exists: every inlet and
outlet ID used by a site in the geometry must have a matching `<inlet>` or
`<outlet>` element in the XML. A geometry with three outlets and an XML with
two stops with "The geometry file uses 3 outlet(s) but the configuration
defines 2", instead of reading past the end of the outlet list.

## MPI helpers with empty buffers

The vector overloads in `net/mixins/InterfaceDelegationNet.h` accept empty
vectors for zero-count transfers. Scalar gathers size the receive vector to
the communicator; all-to-all helpers require buffers of communicator size;
variable-count gathers validate counts and use integer displacements.

## Tests

| Area | Tests |
| --- | --- |
| Header, record, decompression and truncation errors (modified copies of `large_cylinder.gmy`) | `Code/tests/geometry/GeometryReaderTests.cc` |
| Bounds-checked XDR reads | `Code/tests/io/XdrReaderTests.cc` |
| Lookup tree above 32,768 blocks | `Code/tests/geometry/LookupTreeTests.cc` |
| Decomposition arithmetic, the `large_cylinder` split and the too-many-processes message | `Code/tests/geometry/BasicDecompositionTests.cc` |
| Inlet and outlet IDs against the configuration | `Code/tests/configuration/IoletIdCheckTests.cc` |
| Empty MPI gathers (run with 4 ranks) | `Code/tests/net/MpiTests.cc` |
| Geometry tool error handling | `geometry-tool/tests/` |
