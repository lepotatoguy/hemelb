#! /usr/bin/env python
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import sys
import numpy as np
from ..parsers.extraction import ExtractedProperty

def unpack(filename, out_csv):

    propFile = ExtractedProperty(filename)

    with open(out_csv, "w") as f:
        f.write(f'# Dump of file "{filename}"\n')
        f.write(f"# File has {propFile.siteCount} sites.\n")
        f.write(f"# File has {propFile.fieldCount} fields:\n")
        for name, xdrType, memType, length, offset in propFile._fieldSpec:
            f.write(f'#     "{name}", length {length}\n')
        f.write(f"# Geometry origin = {propFile.originMetres} m\n")
        f.write(f"# Voxel size = {propFile.voxelSizeMetres} m\n")

        header_names = [name for name, _, _, _, _ in propFile._fieldSpec]
        f.write("# " + ", ".join(header_names) + "\n")

        for t in propFile.times:
            f.write(f"# Timestep {t}\n")

            fields = propFile.GetByTimeStep(t)

            # Collect all columns into a 2D array
            data_cols = []
            for name in header_names:
                col_data = fields[name]
                # Flatten multi-dimensional fields per row
                if fields.dtype[name].shape != ():
                    col_data = col_data.reshape(col_data.shape[0], -1)
                else:
                    col_data = col_data.reshape(-1, 1)
                data_cols.append(col_data)

            data = np.hstack(data_cols)

            # Write all rows at once
            np.savetxt(f, data, delimiter=",", fmt="%s")

            f.write("\n")  # blank line after timestep


def main():
    if len(sys.argv) != 3:
        print("Usage: hlb-dump-extracted-properties input.xtr output.csv")
        sys.exit(1)

    unpack(sys.argv[1], sys.argv[2])
