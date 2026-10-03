#! /usr/bin/env python
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import argparse
import sys
import os

import numpy as np

from ..parsers.extraction import ExtractedProperty


def _columns(field_spec):
    columns = []
    for name, xdrType, memType, length, offset, *_ in field_spec:
        width = length[0] if isinstance(length, tuple) and length else 1
        if width == 1:
            columns.append(name)
        else:
            columns.extend("{}_{}".format(name, index) for index in range(width))
    return columns


def _write_dump(filename, stream):
    propFile = ExtractedProperty(filename)

    print('# Dump of file "{}"'.format(filename), file=stream)
    print("# File has {} sites.".format(propFile.siteCount), file=stream)
    print("# File has {} fields:".format(propFile.fieldCount), file=stream)
    for name, xdrType, memType, length, offset, *_ in propFile._fieldSpec:
        unit = getattr(propFile, "field_units", {}).get(name)
        suffix = ", units " + unit if unit else ""
        print('#     "{0}", length {1}{2}'.format(name, length, suffix), file=stream)
    print("# Geometry origin = {} m".format(propFile.originMetres), file=stream)
    print("# Voxel size = {} m".format(propFile.voxelSizeMetres), file=stream)

    header = "# " + ", ".join(_columns(propFile._fieldSpec))
    print(header, file=stream)
    for t in propFile.times:
        fields = propFile.GetByTimeStep(t)
        print("# Timestep {:d}".format(t), file=stream)
        _write_rows(fields, propFile._fieldSpec, stream)
        print("", file=stream)


def _write_rows(fields, field_spec, stream):
    """Write one comma-separated line per site.

    Each column is converted to text with NumPy in one step instead of one
    value at a time; astype(str) gives the same text as str() on each value,
    so the output is unchanged (tests/test_dumpextracted.py checks this).
    """
    columns = []
    for name, xdrType, memType, length, offset, *_ in field_spec:
        values = np.asarray(fields[name])
        values = values.reshape(values.shape[0], -1)
        columns.extend(values[:, i].astype(str) for i in range(values.shape[1]))
    if not columns or len(columns[0]) == 0:
        return
    stream.write("\n".join(map(",".join, zip(*columns))))
    stream.write("\n")


def unpack(filename, stream=sys.stdout, out_csv=None):
    """Dump an extraction file to a text stream or output file.

    ``stream`` is retained for API compatibility.  ``out_csv`` is a
    convenient file-path form for command-line callers.
    """
    if out_csv is not None:
        with open(os.fspath(out_csv), "w", newline="") as output:
            _write_dump(filename, output)
        return
    _write_dump(filename, stream)


def main(argv=None, stream=sys.stdout):
    parser = argparse.ArgumentParser(
        prog="hlb-dump-extracted-properties",
        description="Write a HemeLB extraction file (.xtr) as text: a commented "
        "header, then one comma-separated line per site and timestep. Vector "
        "fields are split into one column per component.",
    )
    parser.add_argument("input", help="extraction file (.xtr) written by HemeLB")
    parser.add_argument(
        "output",
        nargs="?",
        help="file to write; if omitted, the text is printed (redirect with > file.csv)",
    )
    args = parser.parse_args(argv)
    if not os.path.isfile(args.input):
        parser.error("extraction file %r does not exist" % args.input)
    if args.output is None:
        unpack(args.input, stream=stream)
    else:
        unpack(args.input, out_csv=args.output)
