#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Export fluid voxel faces as STL; this does not recover the original surface."""

import argparse
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="run XML (metres) or GMY (lattice units)")
    parser.add_argument("output", type=Path, help="new output STL file")
    parser.add_argument(
        "--scale", type=float, default=1.0,
        help="scale coordinates, e.g. 1000 for XML metres to STL millimetres",
    )
    args = parser.parse_args()
    if not args.input.is_file() or args.input.suffix.lower() not in (".xml", ".gmy"):
        parser.error("input must be an existing XML or GMY file")
    if args.output.suffix.lower() != ".stl" or not args.output.parent.is_dir():
        parser.error("output must end in .stl and its parent directory must exist")
    if args.output.exists():
        parser.error("output already exists; choose a new filename")
    if not math.isfinite(args.scale) or args.scale <= 0:
        parser.error("scale must be finite and positive")

    geometry_path = args.input
    voxel_size = 1.0
    origin = (0.0, 0.0, 0.0)
    if args.input.suffix.lower() == ".xml":
        try:
            root = ET.parse(args.input).getroot()
            data = root.find("geometry/datafile")
            voxel = root.find("simulation/voxel_size")
            position = root.find("simulation/origin")
            if voxel.get("units") != "m" or position.get("units") != "m":
                raise ValueError("voxel_size and origin must use metres")
            voxel_size = float(voxel.get("value"))
            text = position.get("value").strip()
            if not text.startswith("(") or not text.endswith(")"):
                raise ValueError("origin must be a parenthesised three-component vector")
            origin = tuple(float(v) for v in text[1:-1].split(","))
            geometry_path = args.input.parent / data.attrib["path"]
        except (ET.ParseError, AttributeError, KeyError, TypeError, ValueError) as error:
            parser.error("invalid geometry metadata in XML: " + str(error))
        if not math.isfinite(voxel_size) or voxel_size <= 0:
            parser.error("voxel size must be finite and positive")
        if len(origin) != 3 or not all(math.isfinite(v) for v in origin):
            parser.error("origin must have three finite components")
        if not geometry_path.is_file():
            parser.error("referenced geometry file does not exist: " + str(geometry_path))

    import vtk
    from hlb.converters.GmyUnstructuredGridReader import GmyUnstructuredGridReader

    # Half-integer lattice corners share exactly representable coordinates.
    # Extract faces before physical scaling to avoid duplicate internal faces.
    geometry = GmyUnstructuredGridReader(str(geometry_path))
    geometry.Update()
    if geometry.GetOutputDataObject(0).GetNumberOfCells() == 0:
        parser.error("geometry has no fluid cells")
    surface = vtk.vtkDataSetSurfaceFilter()
    surface.SetInputConnection(geometry.GetOutputPort())
    triangles = vtk.vtkTriangleFilter()
    triangles.SetInputConnection(surface.GetOutputPort())
    transform = vtk.vtkTransform()
    transform.Translate(*(value * args.scale for value in origin))
    transform.Scale(*(voxel_size * args.scale for _ in range(3)))
    scaled = vtk.vtkTransformPolyDataFilter()
    scaled.SetTransform(transform)
    scaled.SetInputConnection(triangles.GetOutputPort())
    scaled.Update()

    writer = vtk.vtkSTLWriter()
    writer.SetInputConnection(scaled.GetOutputPort())
    writer.SetFileName(str(args.output))
    writer.SetFileTypeToBinary()
    if writer.Write() != 1 or writer.GetErrorCode() != 0:
        raise RuntimeError("Could not write " + str(args.output))
    units = "metres" if args.input.suffix.lower() == ".xml" else "lattice units"
    print(
        "Wrote {} triangles to {}; coordinates are {} multiplied by {}".format(
            scaled.GetOutput().GetNumberOfCells(), args.output, units, args.scale
        )
    )


if __name__ == "__main__":
    main()
