# HemeLB python tools

These tools are a set of Python libraries and some command-line tools
that use them, for tasks such as converting HemeLB input and output
files into other formats.

Dependencies:

- Python 3.8 to 3.13 (tested in CI)
- setuptools
- Cython
- Numpy
- VTK (python bindings)
- C compiler

## Install

1. Create and activate a virtual environment
2. Install with pip:
```bash
cd hemelb/python-tools
pip install .
```

## Use

Each command prints help with `-h`. The install script installs these tools
into the `gmy-tool` conda environment.

Available command line tools:

- `hlb-gmy-decompress`: decompress a geometry file.

- `hlb-gmy-compress`: compress an uncompressed geometry file.

- `hlb-dump-extracted-properties`: convert an extraction file (`.xtr`) to
  text: `hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv`
  (without the second name it prints to the terminal). See
  [Getting started](getting-started.md) for what the columns mean.
  HemeXtract, which older guides mention, was written for a fork of HemeLB
  and cannot read these files; use this tool instead.

- `hlb-gmy-selfconsistent`: check if a geometry file is self-consistent.

- `hlb-gmy-countsites`: print some basic information about how many sites in a geometry file

- `hlb-gmy-3to4 config.xml`: upgrade an XML file and the version 3 geometry
  file it names to version 4; the results go in a new `converted` folder
  next to the XML.


Runnable modules (run with `python -m`):

Convert an XML + geometry file to VTK unstructured grid (.vtu)
```
python -m hlb.converters.GmyUnstructuredGridReader path/to/config.xml
```

Convert an extracted property file to VTK unstructured grid (.vtu).
Note you also require EITHER the .xml/.gmy used for the run OR the output of `GmyUnstructuredGridReader` to provide the
geometry information needed:
```
python -m hlb.converters.ExtractedPropertyUnstructuredGridReader geometry.vtu data.xtr
```

