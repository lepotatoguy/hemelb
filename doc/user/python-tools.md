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

Use the [locked conda environment](geometry-tool.md#installing-by-hand) when
installing both tools. Run converters outside the source-package directories
so Python uses the installed compiled extensions.

## Export comma-separated text

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

The installed command exports every saved timestep from an extraction file:

```sh
hlb-extracted-to-vtk results/Extracted/whole.xtr whole
```

This writes `whole_0.vtu`, `whole_100.vtu`, and `whole.pvd` for the bundled
first-run case. Open `whole.pvd` in ParaView, click **Apply**, and select a field
such as `pressure` or `velocity` under cell data. The time controls move through
the saved frames. The [ParaView PVD reader](https://www.paraview.org/paraview-docs/v5.9.1/python/paraview.simple.PVDReader.html)
loads the collection.

No GMY or XML is required. Each extracted site becomes a voxel centred on its
stored grid coordinate, with the extraction header's physical origin and voxel
size. Coordinates are metres; field names, types, and values are preserved,
including field offsets restored by the reader. Six-component symmetric tensors
are reordered from HemeLB's `XX XY XZ YY YZ ZZ` to VTK's `XX YY ZZ XY YZ XZ`.
Only sites present in the extraction are exported, so a plane or region output
shows that selected subset rather than the whole fluid geometry. Voxel faces
approximate the domain; they do not reconstruct sub-voxel wall cuts.

Version 6 collections use seconds from the embedded timestep duration. Legacy
files lack that metadata and use lattice timestep numbers unless you supply
`--step-length`. For a legacy run with a 0.0001 s timestep:

```sh
hlb-extracted-to-vtk results/Extracted/whole.xtr paraview/whole --step-length 0.0001
```

This creates the output folder and multiplies each saved timestep by 0.0001
seconds. VTU filenames retain lattice timesteps. The collection references
frames relative to its own folder, so move or copy the folder as a unit.

Omit the output argument to write beside the input, using its basename. An
output ending in `.pvd` is also accepted. Existing collection or frame files
are refused before writing. ASCII VTU is used to avoid appended-data parsing
failures observed with local VTK 9.1; it produces larger files than binary VTU.
See the [VTK writer documentation](https://vtk.org/doc/nightly/html/classvtkXMLUnstructuredGridWriter.html).

The command is implemented in
[ExtractedPropertyToVtk.py](../../python-tools/hlb/converters/ExtractedPropertyToVtk.py).
It can also be run as a Python module:

```sh
python -m hlb.converters.ExtractedPropertyToVtk results/Extracted/whole.xtr whole
hlb-extracted-to-vtk --help
```

If the command is missing after updating the repository, reinstall the package:

```sh
python -m pip install ./python-tools
```

Run the install command from the repository root with the tools environment
active. VTU round-trip checks cover coordinates and field values; the ParaView
GUI has not been exercised locally.

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
