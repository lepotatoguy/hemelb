# ParaView export validation

The bundled example was tested from simulation through GUI rendering on
2 October 2026, using ParaView 5.13.3 (arm64) on macOS 27.0. Each branch ran
200 updates with two MPI ranks, D3Q15, LBGK, and simple bounce-back walls.
The Python tools were installed separately from each branch before export.

| Branch revision | Extraction version | PVD times in seconds |
| :--- | :--- | :--- |
| `fix/hemelb-improvements` at `05213b9e` | 5 | 0.01, 0.02 |
| `feat/scalability-input-improvements` at `3ce16fa2` | 6 | 0, 0.01 |

Both simulations and installed `hlb-extracted-to-vtk` commands completed.
Each exported frame contained 2,400 voxel cells and 3,131 points. The physical
voxel centres and every pressure and velocity value matched the source
extraction in automated round-trip checks.

In the ParaView GUI, both PVD collections were opened through the file dialog
and applied. The cylinder rendered in a side view, with pressure and velocity
magnitude available as cell data. The Next Frame control selected the second
saved time and updated the displayed fields. Color legends and rescaling to
the current data range also worked.

To repeat the check, follow the [quick start](../user/getting-started.md), export
`whole.xtr` using the [Python tools guide](../user/python-tools.md#export-for-paraview),
and open `whole.pvd` in ParaView. Click **Apply**, choose a side view and select
`pressure`, then `velocity` with **Magnitude**. Switch between saved frames
with the time controls. Use **Rescale to Data Range** if the legend retains a
range from another case.

This check covers the bundled fluid example and its two saved frames. Other
geometries, collision models, RBC output, and larger exports were not tested
in this GUI check.
