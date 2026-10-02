#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
"""Export extraction files as a ParaView VTU time series and PVD collection."""

import argparse
import itertools
import math
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np
import vtk
from vtk.util.numpy_support import numpy_to_vtk

from ..parsers.extraction import ExtractedProperty


def _voxel_grid(coordinates, voxel_size, origin):
    """Share corners in integer lattice coordinates, then scale to metres."""
    points = vtk.vtkPoints()
    points.SetDataTypeToDouble()
    grid = vtk.vtkUnstructuredGrid()
    grid.SetPoints(points)
    grid.Allocate(len(coordinates))
    point_ids = {}
    # vtkVoxel numbers corners with x changing fastest, followed by y and z.
    corners = [(x, y, z) for z, y, x in itertools.product((-1, 1), repeat=3)]
    voxel = vtk.vtkVoxel()
    for coordinate in coordinates:
        for index, delta in enumerate(corners):
            corner = tuple(2 * int(c) + d for c, d in zip(coordinate, delta))
            point_id = point_ids.get(corner)
            if point_id is None:
                position = origin + 0.5 * voxel_size * np.asarray(corner)
                point_id = points.InsertNextPoint(*position)
                point_ids[corner] = point_id
            voxel.GetPointIds().SetId(index, point_id)
        grid.InsertNextCell(voxel.GetCellType(), voxel.GetPointIds())
    return grid


def export(input_file, output_prefix=None, step_length=None):
    """Write ASCII VTU files and return the PVD path. Existing outputs are refused.

    Collection times use v6 timestep metadata or an explicit step_length in seconds.
    Legacy files without either use lattice timesteps.
    Six-component fields follow HemeLB's symmetric tensor convention.
    """
    source = Path(input_file)
    if not source.is_file():
        raise ValueError("extraction file does not exist: " + str(source))
    if step_length is not None and (not math.isfinite(step_length) or step_length <= 0):
        raise ValueError("step length must be finite and positive")
    extraction = ExtractedProperty(str(source))
    if extraction.siteCount == 0 or len(extraction.times) == 0:
        raise ValueError("extraction has no sites or saved timesteps")
    if not math.isfinite(extraction.voxelSizeMetres) or extraction.voxelSizeMetres <= 0:
        raise ValueError("extraction voxel size must be finite and positive")
    if not np.all(np.isfinite(extraction.originMetres)):
        raise ValueError("extraction origin must be finite")
    if step_length is None and extraction.version >= 6:
        step_length = extraction.timeStepSeconds
    times = [int(time) for time in extraction.times]
    if len(set(times)) != len(times):
        raise ValueError("extraction has duplicate timesteps")

    prefix = (
        Path(output_prefix) if output_prefix is not None else source.with_suffix("")
    )
    if prefix.suffix.lower() == ".pvd":
        prefix = prefix.with_suffix("")
    pvd = prefix.parent / (prefix.name + ".pvd")
    files = [prefix.parent / (prefix.name + "_{}.vtu".format(time)) for time in times]
    for path in [pvd] + files:
        if path.exists():
            raise ValueError("output already exists: " + str(path))

    first = extraction.GetByIndex(0)
    coordinates = [tuple(int(c) for c in row) for row in first.grid]
    if len(set(coordinates)) != len(coordinates):
        raise ValueError("extraction contains duplicate grid coordinates")
    grid = _voxel_grid(coordinates, extraction.voxelSizeMetres, extraction.originMetres)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    writer = vtk.vtkXMLUnstructuredGridWriter()
    writer.SetInputData(grid)
    # Avoid appended-data parsing failures seen with VTK 9.1.
    writer.SetDataModeToAscii()
    collection = ET.Element("VTKFile", type="Collection", version="0.1")
    datasets = ET.SubElement(collection, "Collection")
    for index, (time, path) in enumerate(zip(times, files)):
        data = first if index == 0 else extraction.GetByIndex(index)
        row_indices = {tuple(int(c) for c in row): i for i, row in enumerate(data.grid)}
        if len(row_indices) != len(coordinates) or row_indices.keys() != set(
            coordinates
        ):
            raise ValueError("extracted site set changed at timestep {}".format(time))
        order = np.asarray([row_indices[coordinate] for coordinate in coordinates])
        grid.GetCellData().Initialize()
        for (
            name,
            xdr_type,
            mem_type,
            length,
            offset,
            data_offset,
            scale,
        ) in extraction.GetFieldSpec():
            values = data[name][order]
            if length == (6,):
                # HemeLB: XX XY XZ YY YZ ZZ. VTK: XX YY ZZ XY YZ XZ.
                values = values[:, [0, 3, 5, 1, 4, 2]]
            array = numpy_to_vtk(np.ascontiguousarray(values), deep=True)
            array.SetName(name)
            grid.GetCellData().AddArray(array)
        grid.Modified()
        writer.SetFileName(str(path))
        if writer.Write() != 1 or writer.GetErrorCode() != 0:
            raise RuntimeError("could not write " + str(path))
        collection_time = time if step_length is None else time * step_length
        ET.SubElement(
            datasets,
            "DataSet",
            timestep=str(collection_time),
            group="",
            part="0",
            file=path.name,
        )
    ET.ElementTree(collection).write(pvd, encoding="utf-8", xml_declaration=True)
    return pvd


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="hlb-extracted-to-vtk",
        description="Export a HemeLB extraction (.xtr) to ASCII VTU files and a "
        "ParaView PVD collection. Positions are metres; fields are cell data. "
        "No GMY or XML is required.",
    )
    parser.add_argument("input", help="extraction file (.xtr)")
    parser.add_argument(
        "output", nargs="?", help="output prefix or .pvd path (default: input basename)"
    )
    parser.add_argument(
        "--step-length",
        type=float,
        help="seconds per lattice timestep; v6 uses embedded metadata, legacy files otherwise use lattice steps",
    )
    args = parser.parse_args(argv)
    try:
        output = export(args.input, args.output, args.step_length)
    except (OSError, ValueError, AssertionError, RuntimeError) as error:
        parser.exit(1, "{}: error: {}\n".format(parser.prog, error))
    print("Wrote {}; open this collection in ParaView".format(output))


if __name__ == "__main__":
    main()
