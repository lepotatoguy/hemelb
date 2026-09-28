#! /usr/bin/env python
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import argparse
import sys
import csv
import os
from ..parsers.extraction import ExtractedProperty


def _columns(field_spec):
    columns = []
    for name, xdrType, memType, length, offset in field_spec:
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
    for name, xdrType, memType, length, offset in propFile._fieldSpec:
        print('#     "{0}", length {1}'.format(name, length), file=stream)
    print("# Geometry origin = {} m".format(propFile.originMetres), file=stream)
    print("# Voxel size = {} m".format(propFile.voxelSizeMetres), file=stream)

    header = "# " + ", ".join(_columns(propFile._fieldSpec))
    print(header, file=stream)
    writer = csv.writer(stream, lineterminator="\n")

    for t in propFile.times:
        fields = propFile.GetByTimeStep(t)
        print("# Timestep {:d}".format(t), file=stream)

        for row in fields:
            values = []
            for name, xdrType, memType, length, offset in propFile._fieldSpec:
                try:
                    value = row[name]
                except (TypeError, IndexError):
                    value = getattr(row, name)
                width = length[0] if isinstance(length, tuple) and length else 1
                if width == 1:
                    values.append(value)
                else:
                    values.extend(value)
            writer.writerow(values)

        print("", file=stream)


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
