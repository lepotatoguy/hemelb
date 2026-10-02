# Read and convert HemeLB files

The `hlb` package provides geometry parsers, extraction readers, and command-line
converters. The [install script](install.md) installs it into `gmy-tool`:

```sh
conda activate gmy-tool
hlb-dump-extracted-properties --help
```

The Python reader accepts extraction versions 4, 5, and 6. This branch writes
version 6 with lattice-to-physical conversion metadata. By default the reader
returns velocity in m/s and pressure in Pa. Legacy version 4/5 values retain
their writer's physical units, typically mmHg for pressure; no automatic
conversion to Pa is applied to those old files. See the
[format reference](../dev/file-formats/extraction.md) for binary layouts.

## Install separately

The package needs a C compiler, NumPy, Cython, and VTK's Python bindings.
Python tools CI covers Python 3.8 through 3.13; the geometry tool has a narrower
Python range because of VMTK. From the repository root:

```sh
python -m venv "$HOME/.venvs/hemelb-tools"
. "$HOME/.venvs/hemelb-tools/bin/activate"
python -m pip install ./python-tools
```

Use the [locked conda environment](geometry-tool.md#installing-the-geometry-tool-manually) when
installing both tools. Run converters outside the source-package directories
so Python uses the installed compiled extensions.

## Export results to readable CSV

```sh
hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv
```

Omit the second argument to print to standard output. The text has comment
headers (`#`), a separate block for each saved timestep, and one row per site.
Vector fields are split into component columns. A timestep appears in its
comment header, not in every row, so do not discard the headers when combining
multiple timesteps. The file suffix `.csv` does not remove those comments.

To load one timestep as arrays, use the Python interface below. See
[field extraction](extraction.md) for selectors, precision, and timestep windows.

## Convert existing configuration XML

Existing XML3/5 can run directly on this branch with a compatible executable.
Conversion is optional:

```sh
hlb-convert-config old.xml converted/input.xml
```

The converter writes version 6, converts pressure quantities to Pa, converts
legacy lattice iolet positions to metres, and rebases file paths. It can create
converted pressure-data sidecars; keep them with the new XML. It refuses an
existing output path. The [compatibility guide](scalability-and-inputs.md)
explains legacy checkpoint and waveform behavior.

## Read fields in Python

```python
from hlb.parsers.extraction import ExtractedProperty

extraction = ExtractedProperty("results/Extracted/whole.xtr")
print(extraction.times)
fields = extraction.GetByIndex(0)
print(fields.grid)       # Integer lattice coordinates
print(fields.position)   # Positions in metres
print(fields.velocity)   # m/s, when the XML requested velocity
print(fields.pressure)   # Pa for version 6 output
```

`GetByTimeStep(t)` selects a stored timestep; `GetByIndex(i)` selects its index
in `times`. Fields use the names given in the XML, defaulting to their type
names. The reader restores field offsets from the header. Row order and its
synthetic `id` are not a persistent site identity: match different files or
rank counts by `grid` coordinates. For version 6,
`ExtractedProperty(path, physical_units=False)` skips multiplication by field
scales but still restores offsets. This option is unavailable for versions 4/5.

## Export for ParaView

Use the **ParaView 5.13.3 GUI (tested)**. Export the results:

```sh
hlb-extracted-to-vtk results/Extracted/whole.xtr whole
```

Open `whole.pvd`, click **Apply**, and select `pressure` or `velocity`
(**Magnitude**) under cell data. Use the time controls to view the saved frames.

## Geometry commands

Each command accepts `-h` for usage.

| Command | Purpose |
| :--- | :--- |
| `hlb-gmy-countsites mesh.gmy` | Print the fluid-site count |
| `hlb-gmy-selfconsistent mesh.gmy` | Check internal geometry consistency |
| `hlb-gmy-decompress` | Write an uncompressed geometry representation |
| `hlb-gmy-compress` | Compress that representation |
| `hlb-convert-config old.xml converted/input.xml` | Optionally convert XML3/5 to XML6 and rebase file paths |
| `hlb-gmy-3to4 config.xml` | Convert legacy geometry format 3 and its XML into a `converted/` folder |

The `3to4` command concerns the legacy geometry format. It does not convert
arbitrary version 3 solver XML or newer branch formats. The format references
are [geometry](../dev/file-formats/geometry.md) and
[old geometry](../dev/file-formats/old-geometry.md).

Optional: [Scripts/gmy-to-stl.py](../../Scripts/gmy-to-stl.py) creates a stepped, capped STL preview when the original surface is unavailable (`python /path/to/hemelb/Scripts/gmy-to-stl.py mesh.gmy preview.stl`; see `--help` for XML scaling).

## Tests and limitations

See the [developer test guide](../dev/README.md#how-to-run-the-tests) for tox and
external fixtures. Historical post-processing tools may expect other formats;
check their supported extraction version before using them. Prefer the bundled
reader for this branch's output.
