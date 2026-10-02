<!-- This file is part of HemeLB and is Copyright (C) -->
<!-- the HemeLB team and/or their institutions, as detailed in the -->
<!-- file AUTHORS. This software is provided under the terms of the -->
<!-- license in the file LICENSE. -->

# HemeLB Python tools

Read geometry and extraction files, export field data as text or VTK, and
check geometry consistency. The `hlb` package includes compiled Cython extensions.

After installation, export simulation fields with:

```sh
hlb-dump-extracted-properties results/Extracted/whole.xtr whole.csv
hlb-extracted-to-vtk results/Extracted/whole.xtr whole
```

Open `whole.pvd` in ParaView. Both converters read the extraction directly;
the VTK exporter uses the voxel size and origin in its header for metre coordinates.

[Installation and commands](../doc/user/python-tools.md),
[extraction format](../doc/dev/file-formats/extraction.md), and
[test instructions](../doc/dev/README.md#geometry-and-analysis-tools).
