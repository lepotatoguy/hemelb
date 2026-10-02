# Geometry GUI validation

The `feat/scalability-input-improvements` geometry GUI (`hlb-gmy-gui`) was
checked through native GUI controls on 2 October 2026. The environment was
macOS 27.0 on Apple Silicon, with the Intel conda environment under Rosetta:
Python 3.8.18, wxPython 4.2.0 (wxWidgets 3.2.0), VTK 9.1.0 and VMTK 1.5.
Both the geometry tool and Python tools were installed from this branch.

## Controls and compatibility

The checks covered empty startup and `--profile` startup; choosing an STL;
opening, editing, saving and reopening `.pr2` profiles; opening a legacy `.pro`
profile after another profile was already selected; and returning to `.pr2`.
The loaded iolet names, pressure values, seed coordinates and preview were
checked, including switching between a large legacy cylinder and the small
bundled example.

The metre, millimetre and micrometre choices updated the coordinate labels.
Inlets and outlets could be added, selected, edited and removed. New default
names skipped names already present. A pressure amplitude of `0.005` mmHg
remained visible in the equation, with phase in radians. The output GMY and
XML chooser dialogs updated the paths, and saved profiles retained relative
paths and the edited values.

The camera axis controls, **Fit**, drag rotation and wheel zoom were exercised.
Both **Place** controls
updated coordinates when the surface was clicked; the other placement mode
was disabled until **Finish**. Seed picking selects the surface, so the seed
was then restored to the cylinder interior at `(0, 0, 0)` before generation.
Zero voxel size produced a validation dialog, malformed YAML produced an
open-profile error, and a negative radius was rejected. The GUI remained
usable after those inputs were corrected.

The automated GUI regression checks cover startup control visibility,
scrolling nested output controls into view in a shorter window, profile
switching with an active selection, clearing the selection, editor values,
and fitting the final scene bounds. Model checks cover loaded-name collisions,
legacy controller bindings, camera redraws, placement visibility, and rendering
through the interactor's window-readiness guard.

## Generated case

The bundled STL was copied to a separate working folder. Its saved profile
was edited through the GUI to use a mean inlet pressure of `0.02` mmHg,
amplitude `0.005` mmHg and phase `0.5` rad, with a zero-pressure outlet.
Voxel size was `0.1` mm, step length `0.0001` s and duration `0.02` s.
Original example files were preserved.

The GUI's **Generate** button wrote the GMY and XML successfully. The GMY
self-consistency check passed and the site counter reported 2,400 fluid
sites. The XML's hexadecimal pressure values matched conversion from mmHg
to Pa, and the phase remained `0.5` rad.

A separate copy of the generated XML was given the bundled example's field
output requests. A freshly rebuilt fluid solver (D3Q15, LBGK, simple
bounce-back walls) completed 200 updates with two MPI ranks. The installed
`hlb-extracted-to-vtk` exported two frames at `0` and `0.01` seconds, each
with 2,400 voxel cells and 3,131 points. An automated round-trip check
verified physical coordinates, finite fields, and exact equality of every
exported pressure and velocity value to the extraction file.

The generated case's PVD collection was opened and applied in the native
ParaView 5.13.3 GUI (arm64). Pressure and velocity magnitude rendered as cell
data in a side view. **First Frame** and **Next Frame** selected `0` and
`0.01` seconds and changed the displayed fields. Color legends and
**Rescale to Data Range** worked for the current frame.

## Redraws and regression checks

wx preview updates request a paint after the current edit has finished.
This avoids rendering intermediate widget states and lets wx combine
multiple requests. A running wx event-loop check used six bursts of 31
camera changes for each rendering method. Immediate rendering produced
31 renders per burst; deferred repainting produced one per burst. The
final camera position and focal point were identical. This measures
redundant renders during grouped updates, not continuous interaction FPS.
Mesh resolution and simulation settings were unchanged.

The geometry suite passed all 79 tests in the installed environment,
including the native-window regressions. The Python tools suite passed
all 62 tests with the external fixtures available. The CI-pinned Black 22
formatting check passed for all 254 geometry-tool Python files.

## Scope

This is a GUI and generated-fluid-case check in the environment above.
It does not establish GUI compatibility on other operating systems, graphics
drivers or dependency versions, or validate every vessel, collision model
and RBC configuration. See the [geometry guide](../user/geometry-tool.md),
[CPU verification guide](cpu-verification.md), and
[ParaView validation record](paraview-validation.md) for the separate workflows.
