# Geometry setup and legacy inputs

The solver reads XML versions 3, 5, and 6 directly. HemePure version 3 and
HemeLB version 5 inputs retain their mmHg pressure interpretation, including
pressure-file values. New version 6 configurations use mmHg for pressure and
mmHg/m for pressure gradients. Explicit Pa and Pa/m values remain accepted.
Version 6 pressure-file and coupling conditions without a unit attribute retain
their historical Pa interpretation. Legacy lattice iolet positions are converted in memory
using voxel size and origin. Source XML and
pressure files are preserved; no preprocessing command is required.

Run an existing input with `hemelb -in input.xml -out results`. Optionally
check its configuration first with `hemelb-confcheck input.xml`. Available boundary physics and the lattice
still depend on the executable's build configuration.

## Optionally save a converted configuration

Install the Python tools, then run:

```sh
hlb-convert-config old/input.xml converted/input.xml
hemelb-confcheck converted/input.xml  # optional
hemelb -in converted/input.xml -out results
```

From a source checkout, the converter also runs as:

```sh
PYTHONPATH=python-tools python -m hlb.converters.Config old/input.xml converted/input.xml
```

Conversion preserves the source files, rebases input paths, retains mmHg pressure,
and maps lattice iolet positions to metres using voxel size and origin. Pressure
file inputs write a copy beside the output XML, retaining mmHg pressure; the
source data files are unchanged. Existing output files are rejected. The geometry tool also writes version 6 XML, retaining the
mmHg pressure values stored in its GUI profiles.

Pressure and velocity boundary choices still depend on the executable's CMake
configuration. `hemelb-confcheck --syntax-only input.xml` checks the XML schema
without checking those build choices. It does not establish that a case can run.

The supported outlet, sponge, and elastic-wall options are described in
[CPU models](cpu-models.md). Separate guides cover [coupling](coupling.md),
[passive tracers](tracers.md), [field extraction](extraction.md), and
[checkpoint continuation](checkpoints.md). Unsupported physics and malformed XML produce a
named error. Force-coupled colloids remain unsupported; passive tracers use a
separate path. RBC configurations require an RBC build. A velocity boundary
whose radius does not cover its geometry is rejected during the run.

## Select decomposition

Add this child of `<hemelbsettings>`:

```xml
<decomposition method="octree"/>
```

`octree` uses the existing site-weighted block split directly. `parmetis` refines
that split and remains the default when the element is absent. The selection is
saved in generated restart XML. Both modes require at least one active block per
MPI rank.

Optional `reader_count` and `reader_spacing` attributes on `decomposition`
limit geometry readers. Count zero uses all eligible ranks; spacing one uses
consecutive ranks. For example, `reader_count="2" reader_spacing="2"` assigns
payload reads to ranks 0 and 2. The combination must fit the current rank count,
including after restart. Header broadcasts still use the I/O rank. Owner lookup
caching uses a 64 MiB budget with a minimum of one whole block; a block larger
than that budget can exceed it.

## Sparse geometry setup

Metadata and parsed blocks are keyed by active block. Header records are read in
chunks, so metadata memory follows active geometry rather than bounding-box
volume. The I/O rank broadcasts header chunks; assigned reader ranks fetch each
requested compressed block once and distribute it through sparse MPI exchanges.
Compressed blocks remain cached through ParMETIS redistribution, which fetches
only missing parsed blocks. The cache is released after decomposition.

The header still contains one record for every bounding-box block, so header I/O
and scan time grow with the box. Block coordinates retain the format's 16-bit
limit. Each compressed block must fit the existing 64 MiB limit.

The solver accepts `.gmy+` files with four header integers per block: fluid-site
count, weight, compressed length, and uncompressed length. It uses positive computational weights for the initial block split and
ParMETIS vertex costs. Zero block weights fall back to the fluid-site count.
ParMETIS converts average per-site costs to positive integers using a common
scale, with rounding to one part in a million of the largest per-site cost. The extension selects this layout;
keep `.gmy+` rather than renaming the file to `.gmy`.

## Read the performance report

Both `report.xml` and `report.txt` include completed updates, loop wall time,
MLUPS, MLUPS per MPI rank, max/mean fluid-site imbalance, and per-rank edge sites,
halo distributions, and halo send bytes. Existing phase timers remain available.
The loop includes extraction, checkpoint writes during the run, and monitoring;
it excludes initial checkpoint output.

MLUPS is global fluid sites multiplied by updates completed in this invocation,
divided by one million times the maximum rank's loop seconds. Resumed runs use
their own completed updates. Halo bytes count outgoing distribution values for
one exchange, excluding MPI protocol overhead.

RSS is reported in bytes. `setup_peak_rss_bytes` is the process high-water mark
before the loop; `peak_rss_bytes` is the lifetime high-water mark at finalisation.
It is not a separately measured loop-only peak. Unsupported platforms report -1.

## Extraction versions

The Python extraction reader accepts versions 4, 5, 6, and 7. Versions 4/5
retain physical values and their original units, typically mmHg pressure.
Version 6 uses lattice values and conversion scales, returning pressure in Pa.
Version 7 adds explicit field units and returns pressure in mmHg, while stress
and traction remain in Pa. Both modern formats store timestep, mass scale,
reference pressure, and per-field scales. Decode a field as
`(stored + offset) * scale`; a zero scale marks an unscaled field such as rank
IDs or distributions. Pressure bodies contain a lattice pressure difference;
the offset restores reference pressure in the field's declared units. With
`physical_units=False`, offsets still apply, but scales do not.

Checkpoint input accepts extraction versions 4, 5, 6, and 7 directly, with float or
double distributions as supported by the file format. Float values are promoted
to the solver's double precision and offsets are restored. The checkpoint must
contain one field named `distributions`, matching the executable's lattice and
current geometry, together with its `.off` file. Rank counts may differ between
saving and loading. Legacy nested `<pressure><checkpoint .../></pressure>`
initial conditions are accepted. Existing `<properties><checkpoint file="...%d..."
period="..."/></properties>` paths and periods are retained as distribution
outputs; new checkpoint and extraction data use version 7.

Pressure-file units are persisted in generated version 6 restart XML as
`<condition type="pressure" subtype="file" units="mmHg">`. Version 6 file
conditions accept `units="Pa"` or `units="mmHg"`. Omitted units in version 6
retain the historical Pa interpretation; versions 3/5 use mmHg. This avoids
reinterpreting a legacy file when restarting. Optional field `datatype` values
(`float`, `double`, `int32`, `uint32`, `int64`, `uint64`) are also preserved in
saved XML so double-precision legacy checkpoint outputs retain their precision.

Local validation covers legacy XML and checkpoint loading, geometry setup, and
rank-portable restarts through the CTest targets. The optional multiscale build
passes its fluid regression suite locally. File read/write coupling has separate
mock-peer tests; these do not exercise MPWide. External MPWide peer exchange,
RBC/active-colloid builds, and remote CI remain unverified.
