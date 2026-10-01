# Read and convert HemeLB files

The `hlb` package provides geometry parsers, extraction readers, and command-line
converters. The [install script](install.md) installs it into `gmy-tool`:

```sh
conda activate gmy-tool
hlb-dump-extracted-properties --help
```

The Python reader accepts extraction versions 4 and 5. The solver on
`fix/hemelb-improvements` writes version 5, with velocity in m/s and pressure in
mmHg. This reader does not support version 6 output. See the
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

To load one timestep as arrays, use the Python interface below.

## Read fields in Python

```python
from hlb.parsers.extraction import ExtractedProperty

extraction = ExtractedProperty("results/Extracted/whole.xtr")
print(extraction.times)
fields = extraction.GetByIndex(0)
print(fields.grid)       # Integer lattice coordinates
print(fields.position)   # Positions in metres
print(fields.velocity)   # m/s, when the XML requested velocity
print(fields.pressure)   # mmHg for this branch's pressure output
```

`GetByTimeStep(t)` selects a stored timestep; `GetByIndex(i)` selects its index
in `times`. Fields use the names given in the XML, defaulting to their type
names. The reader restores field offsets from the header. Row order and its
synthetic `id` are not a persistent site identity: match different files or
rank counts by `grid` coordinates.

## Export for ParaView

Run this in the first-run folder with the tools environment active. It creates
one ASCII `.vtu` per saved timestep and a `whole.pvd` collection. ASCII files
are larger than binary files, but avoid appended-data parsing failures observed
in the local VTK 9.1 environment.

```sh
python - <<'PYCODE'
import vtk
from hlb.parsers.extraction import ExtractedProperty
from hlb.converters.GmyUnstructuredGridReader import GmyUnstructuredGridReader
from hlb.converters.ExtractedPropertyUnstructuredGridReader import (
    ExtractedPropertyUnstructuredGridReader,
    WritePVDFile,
)

geometry = GmyUnstructuredGridReader("first-run.xml")
extraction = ExtractedProperty("results/Extracted/whole.xtr")
converter = ExtractedPropertyUnstructuredGridReader()
converter.SetInputConnection(geometry.GetOutputPort())
converter.SetExtraction(extraction)
writer = vtk.vtkXMLUnstructuredGridWriter()
writer.SetInputConnection(converter.GetOutputPort())
writer.SetDataModeToAscii()
files = {}
for timestep in extraction.times:
    converter.SetTime(timestep)
    filename = "whole_{}.vtu".format(int(timestep))
    writer.SetFileName(filename)
    if writer.Write() != 1:
        raise RuntimeError("Could not write " + filename)
    files[int(timestep)] = filename
WritePVDFile(files, "whole")
PYCODE
```

Open `whole.pvd` in ParaView. Fields are attached to cells, not points.
The collection uses lattice timestep numbers; multiply by the XML `step_length`
to obtain seconds. The export recipe was checked with the bundled first-run
geometry; inspecting it in the ParaView GUI is a separate step.

Use the original XML as the geometry input. Passing only a `.gmy` creates
coordinates in lattice units, while extraction positions are in metres.
The geometry and extraction must refer to the same run geometry, and the
geometry must remain unscaled before the field-matching step.

## Geometry commands

Each command accepts `-h` for usage.

| Command | Purpose |
| :--- | :--- |
| `hlb-gmy-countsites mesh.gmy` | Print the fluid-site count |
| `hlb-gmy-selfconsistent mesh.gmy` | Check internal geometry consistency |
| `hlb-gmy-decompress` | Write an uncompressed geometry representation |
| `hlb-gmy-compress` | Compress that representation |
| `hlb-gmy-3to4 config.xml` | Convert legacy geometry format 3 and its XML into a `converted/` folder |

The `3to4` command concerns the legacy geometry format. It does not convert
arbitrary version 3 solver XML or newer branch formats. The format references
are [geometry](../dev/file-formats/geometry.md) and
[old geometry](../dev/file-formats/old-geometry.md).

## Tests and limitations

See the [developer test guide](../dev/README.md#how-to-run-the-tests) for tox and
external fixtures. Historical post-processing tools may expect other formats;
check their supported extraction version before using them. Prefer the bundled
reader for this branch's output.
