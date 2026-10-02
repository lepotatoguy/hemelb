# -*- coding: utf-8 -*-
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import os.path
import pickle
from copy import copy
import numpy as np
import yaml

from vtk import vtkSTLReader

from ..Util.Observer import Observable
from .SideLengthCalculator import AverageSideLengthCalculator
from .Vector import Vector
from .Iolets import ObservableListOfIolets, IoletLoader, Inlet, Outlet

import types


_LEGACY_MODULE = "HemeLbSetupTool"
# Legacy profiles store timing as cycles of a 70 beats per minute pulse.
LEGACY_CARDIAC_PERIOD_S = 60.0 / 70.0


class FakeUnpickler(pickle.Unpickler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._HST = types.ModuleType("HemeLbSetupTool")
        self._classes = {}

    def _get_or_make_mod(self, moduleName):
        parts = moduleName.split(".")
        hst = parts.pop(0)
        if hst != _LEGACY_MODULE:
            raise pickle.UnpicklingError("not a legacy profile module: %s" % moduleName)
        full = hst
        cur = self._HST
        while parts:
            name = parts.pop(0)
            if not hasattr(cur, name):
                mod = types.ModuleType(f"{full}.{name}")
                mod.__package__ = full
                full = mod.__name__
                setattr(cur, name, mod)
            cur = getattr(cur, name)
        return cur

    def _get_or_make_class(self, mod, className):
        try:
            return getattr(mod, className)
        except AttributeError:

            def __up__(this):
                return this  # no-op upgrade

            fake = type(
                className, (object,), {"__module__": mod.__name__, "__up__": __up__}
            )
            setattr(mod, className, fake)
            return fake

    def find_class(self, moduleName, className):
        if moduleName == _LEGACY_MODULE or moduleName.startswith(_LEGACY_MODULE + "."):
            mod = self._get_or_make_mod(moduleName)
            return self._get_or_make_class(mod, className)
        raise pickle.UnpicklingError(
            "global '%s.%s' is not allowed in legacy profiles" % (moduleName, className)
        )


class LengthUnit(Observable):
    def __init__(self, sizeInMetres, name, abbrv):
        self.SizeInMetres = sizeInMetres
        self.Name = name
        self.Abbrv = abbrv
        return

    pass


metre = LengthUnit(1.0, "metre", "m")
millimetre = LengthUnit(1e-3, "millimetre", "mm")
micrometre = micron = LengthUnit(1e-6, "micrometre", "µm")


class Profile(Observable):
    """This class represents the parameters necessary to perform a
    setup for HemeLb and supplies the functionality to do it.

    The required parameters are below with defaults and all must be
    specified to actually create the setup files.

    """

    # Required parameters and defaults.
    _CloneOrder = ["StlFileUnitId", "StlFile", "VoxelSize"]
    _Args = {
        "StlFile": None,
        "StlFileUnitId": 1,
        "Iolets": ObservableListOfIolets(),
        "VoxelSize": 0.0,
        "TimeStepSeconds": 1e-4,
        "DurationSeconds": 5.0,
        # Period of the cosine pressure at every inlet and outlet. Profiles
        # without it use 1 s, the value that was always written before.
        "PulsePeriodSeconds": 1.0,
        "SeedPoint": Vector(),
        "OutputGeometryFile": None,
        "OutputXmlFile": None,
    }
    _UnitChoices = [metre, millimetre, micrometre]

    def __init__(self, **kwargs):
        """Required arguments may be set here through keyword arguments."""
        # Set attributes on this instance according to the keyword
        # args given here or the default dict if they aren't present.
        for a, default in Profile._Args.items():
            setattr(self, a, kwargs.pop(a, copy(default)))
            continue
        # Raise an error on a kwarg we don't understand
        for k in kwargs:
            raise TypeError("__init__() got an unexpected keyword argument '%s'" % k)

        # We need a reader to get the polydata
        self.StlReader = vtkSTLReader()
        self.HasLoadedStlFile = False

        # And a way to estimate the voxel size
        self.SideLengthCalculator = AverageSideLengthCalculator()
        self.SideLengthCalculator.SetInputConnection(self.StlReader.GetOutputPort())

        # Dependencies for properties
        self.AddDependency("HaveValidStlFile", "StlFile")
        self.AddDependency("HaveValidOutputXmlFile", "OutputXmlFile")
        self.AddDependency("HaveValidOutputGeometryFile", "OutputGeometryFile")
        self.AddDependency("HaveValidSeedPoint", "SeedPoint.x")
        self.AddDependency("HaveValidSeedPoint", "SeedPoint.y")
        self.AddDependency("HaveValidSeedPoint", "SeedPoint.z")
        self.AddDependency("IsReadyToGenerate", "HaveValidStlFile")
        self.AddDependency("IsReadyToGenerate", "HaveValidOutputXmlFile")
        self.AddDependency("IsReadyToGenerate", "HaveValidOutputGeometryFile")
        self.AddDependency("IsReadyToGenerate", "HaveValidSeedPoint")
        self.AddDependency("StlFileUnit", "StlFileUnitId")
        self.AddDependency("VoxelSizeMetres", "VoxelSize")
        self.AddDependency("VoxelSizeMetres", "StlFileUnit.SizeInMetres")
        self.BoundingBoxSize = 0.0
        self.AddDependency("DefaultIoletRadius", "BoundingBoxSize")

        # Load a valid STL and update the mesh measurements when it changes.
        self.AddObserver("StlFile", self.OnStlFileChanged)
        return

    def UpdateAttributesBasedOnCmdLineArgs(self, cmdLineArgsDict):
        """Helper method that takes a dictionary with the arguments provided
        to the setup tool via command line (cmdLineArgsDict) and sets/updates
        the relevant class attributes.
        """
        # Some attributes need to be set in a given order to avoid side effects.
        # Set them first
        for attrName in self._CloneOrder:
            if attrName in cmdLineArgsDict:
                val = cmdLineArgsDict[attrName]
                if val is not None:
                    setattr(self, attrName, val)
                    cmdLineArgsDict.pop(attrName)

        # Set the rest
        for k, val in cmdLineArgsDict.items():
            if val is not None:
                setattr(self, k, val)

    def OnStlFileChanged(self, change):
        self.HasLoadedStlFile = False
        if not self.HaveValidStlFile:
            self.BoundingBoxSize = 0.0
            return

        self.StlReader.SetFileName(self.StlFile)
        self.StlReader.Update()
        self.VoxelSize = self.SideLengthCalculator.GetOutputValue()
        surf = self.StlReader.GetOutput()
        surf.ComputeBounds()
        bounds = surf.GetBounds()
        # VTK standard bounding box
        # Compute diagonal length
        self.BoundingBoxSize = np.sqrt(
            (bounds[1] - bounds[0]) ** 2
            + (bounds[3] - bounds[2]) ** 2
            + (bounds[5] - bounds[4]) ** 2
        )
        self.HasLoadedStlFile = True
        return

    @property
    def HaveValidStlFile(self):
        """Read only property indicating if our STL file is valid."""
        return IsFileValid(self.StlFile, ext=".stl", exists=True)

    @property
    def HaveValidSeedPoint(self):
        if (
            np.isfinite(self.SeedPoint.x)
            and np.isfinite(self.SeedPoint.y)
            and np.isfinite(self.SeedPoint.z)
        ):
            return True
        return False

    @property
    def HaveValidOutputXmlFile(self):
        return IsFileValid(self.OutputXmlFile, ext=".xml")

    @property
    def HaveValidOutputGeometryFile(self):
        return IsFileValid(self.OutputGeometryFile, ext=".gmy")

    @property
    def IsReadyToGenerate(self):
        """Read only property indicating if we have enough information
        to do the setup.
        """
        if not self.HaveValidSeedPoint:
            return False
        if not self.HaveValidOutputXmlFile:
            return False
        if not self.HaveValidOutputGeometryFile:
            return False
        if not self.HaveValidStlFile:
            return False
        return True

    @property
    def StlFileUnit(self):
        return self._UnitChoices[self.StlFileUnitId]

    @property
    def VoxelSizeMetres(self):
        return self.VoxelSize * self.StlFileUnit.SizeInMetres

    @VoxelSizeMetres.setter
    def VoxelSizeMetres(self, value):
        self.VoxelSize = value / self.StlFileUnit.SizeInMetres
        return

    @property
    def DefaultIoletRadius(self):
        return self.BoundingBoxSize / 20.0

    def LoadFromFile(self, filename):
        root, ext = os.path.splitext(filename)
        if ext == ".pro":
            return self.LoadProfileV1(filename)
        elif ext == ".pr2":
            return self.LoadProfileV2(filename)
        else:
            raise ValueError("Unexpected extension on profile file: " + ext)

    def LoadProfileV2(self, filename):
        with open(filename) as f:
            state = yaml.safe_load(f)
        if not isinstance(state, dict):
            raise ValueError("Profile file must contain a mapping")
        self._ResetPathsV2(state, filename)
        self.LoadFrom(state)
        return

    def LoadProfileV1(self, filename):
        with open(filename, "rb") as f:
            restored_fake = FakeUnpickler(f).load()
            halfway = restored_fake.__up__()

        has_steps = hasattr(halfway, "Steps")
        has_cycles = hasattr(halfway, "Cycles")
        if has_steps != has_cycles:
            raise ValueError("Profile has only one of Cycles and Steps")
        if has_steps and has_cycles:
            if hasattr(halfway, "TimeStepSeconds") or hasattr(
                halfway, "DurationSeconds"
            ):
                raise ValueError("Legacy profile mixes old and new timing fields")
            if halfway.Steps <= 0 or halfway.Cycles <= 0:
                raise ValueError("Profile Steps and Cycles must be positive")
            halfway.TimeStepSeconds = LEGACY_CARDIAC_PERIOD_S / halfway.Steps
            halfway.DurationSeconds = LEGACY_CARDIAC_PERIOD_S * halfway.Cycles

        base_path = os.path.dirname(os.path.abspath(filename))
        values = {}
        for attr in Profile._Args:
            val = getattr(halfway, attr, None)
            if attr == "SeedPoint" and val is not None:
                val = Vector(val.x, val.y, val.z)
            elif attr == "Iolets" and val is not None:
                val = self._ConvertLegacyIolets(val)
            elif attr in ("StlFile", "OutputGeometryFile", "OutputXmlFile"):
                if val is not None:
                    if not isinstance(val, str):
                        raise ValueError(
                            "Legacy profile path %s is not a string" % attr
                        )
                    val = os.path.abspath(os.path.join(base_path, val))
            if val is not None:
                values[attr] = val

        for attr in self._CloneOrder:
            if attr in values:
                setattr(self, attr, values.pop(attr))
        for attr, val in values.items():
            if attr == "SeedPoint":
                self.SeedPoint.CloneFrom(val)
            elif attr == "Iolets":
                # Keep the list that the GUI and preview controllers observe.
                while self.Iolets:
                    self.Iolets.pop()
                for iolet in val:
                    self.Iolets.append(iolet)
            else:
                setattr(self, attr, val)
        return

    @staticmethod
    def _ConvertLegacyIolets(iolets):
        real_iolets = ObservableListOfIolets()
        for io in iolets:
            class_name = type(io).__name__
            if class_name == "Inlet":
                iolet = Inlet()
            elif class_name == "Outlet":
                iolet = Outlet()
            else:
                name = getattr(io, "Name", "")
                if name.startswith("Inlet"):
                    iolet = Inlet()
                elif name.startswith("Outlet"):
                    iolet = Outlet()
                else:
                    raise ValueError("Unknown legacy iolet type: %s" % class_name)

            for attr in ("Name", "Radius"):
                value = getattr(io, attr, None)
                if value is not None:
                    setattr(iolet, attr, value)
            for attr in ("Centre", "Normal", "Pressure"):
                value = getattr(io, attr, None)
                if value is not None:
                    setattr(iolet, attr, Vector(value.x, value.y, value.z))
            real_iolets.append(iolet)
        return real_iolets

    def _ResetPaths(self, filename):
        # Now adjust the paths of filenames relative to the Profile file.
        # Note that this will work if an absolute path has been pickled as
        # os.path.join will discard previous path elements when it gets an
        # absolute path. (Of course, this will only work if that path is
        # correct!)
        basePath = os.path.dirname(os.path.abspath(filename))
        for attr in ("StlFile", "OutputGeometryFile", "OutputXmlFile"):
            value = getattr(self, attr, None)
            if value is not None:
                setattr(self, attr, os.path.abspath(os.path.join(basePath, value)))
        return

    def _ResetPathsV2(self, state, filename):
        # Now adjust the paths of filenames relative to the Profile file.
        basePath = os.path.dirname(os.path.abspath(filename))
        for attr in ("StlFile", "OutputGeometryFile", "OutputXmlFile"):
            value = state.get(attr)
            if value is not None:
                if not isinstance(value, str):
                    raise ValueError("Profile path %s is not a string" % attr)
                state[attr] = os.path.abspath(os.path.join(basePath, value))
        return

    def Save(self, filename):
        basePath = str(os.path.dirname(filename))
        state = self.Yamlify()
        for attr in ("StlFile", "OutputXmlFile", "OutputGeometryFile"):
            value = state[attr]
            if value is not None:
                if not isinstance(value, str):
                    raise ValueError("Profile path %s is not a string" % attr)
                value = value if os.path.isabs(value) else os.path.abspath(value)
                state[attr] = os.path.relpath(value, basePath)
        with open(filename, "w") as outfile:
            yaml.safe_dump(state, stream=outfile)

        return

    def Generate(self):
        from .OutputGeneration import PolyDataGenerator

        generator = PolyDataGenerator(self)
        generator.Execute()
        return generator.Warnings

    def ResetVoxelSize(self, ignored=None):
        """Action to reset the voxel size to its default value."""
        self.VoxelSize = self.SideLengthCalculator.GetOutputValue()
        return

    pass


def IsFileValid(path, ext=None, exists=None):
    if not isinstance(path, str):
        return False
    if path == "":
        return False

    if exists is not None:
        if os.path.exists(path) != exists:
            return False
        pass

    if ext is not None:
        ending = os.path.splitext(path)[1]
        if ending != ext:
            return False
        pass
    return True
