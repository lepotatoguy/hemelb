Configured by file {{CONFIG}} with a {{SITES}} site geometry.
There were {{BLOCKS}} blocks, each with {{SITESPERBLOCK}} sites (fluid and solid).
Ran with {{THREADS}} threads.
Ran for {{STEPS}} steps of an intended {{TOTAL_TIME_STEPS}}.
With {{TIME_STEP_LENGTH}} seconds per time step.
{{#DENSITIES}}
!! Maximum relative density difference allowed {{ALLOWED}} was violated: {{ACTUAL}} !!
{{/DENSITIES}}
{{#UNSTABLE}}
!! Simulation was unstable !!
{{/UNSTABLE}}
{{#SOLUTIONCONVERGED}}
Detected convergence of steady flow simulation
{{/SOLUTIONCONVERGED}}

Sub-domains info:
{{#PROCESSOR}}
rank: {{RANK}}, fluid sites: {{SITES}}
{{/PROCESSOR}}

Timing data:
Name Local Min Mean Max
{{#TIMER}}
{{NAME}} {{LOCAL}} {{MIN}} {{MEAN}} {{MAX}}
{{/TIMER}}

{{#BUILD}}
Revision number:{{REVISION}}
Build type: {{TYPE}}
Optimisation level: {{OPTIMISATION}}
Use SSE3: {{USE_SSE3}}
Built at: {{TIME}}
Lattice: {{LATTICE_TYPE}}
Kernel: {{KERNEL_TYPE}}
Wall boundary condition: {{WALL_BOUNDARY_CONDITION}}
Iolet boundary condition: {{IOLET_BOUNDARY_CONDITION}}
Wall/iolet boundary condition: {{WALL_IOLET_BOUNDARY_CONDITION}}

Communications options:
Point to point implementation: {{POINTPOINT_IMPLEMENTATION}}
All to all implementation: {{ALLTOALL_IMPLEMENTATION}}
Gathers implementation: {{GATHERS_IMPLEMENTATION}}
Separated concerns: {{SEPARATE_CONCERNS}}
{{/BUILD}}
Performance:
Geometry blocks read from disk: {{GEOMETRY_BLOCK_READS}}
Compressed geometry bytes read: {{GEOMETRY_BYTES_READ}}
Decomposition: {{DECOMPOSITION}}
Completed updates: {{UPDATES}}
Time loop seconds (maximum rank): {{LOOP_SECONDS}}
MLUPS: {{MLUPS}}
MLUPS per MPI rank: {{MLUPS_PER_RANK}}
Load imbalance (maximum/mean sites): {{LOAD_IMBALANCE}}
Peak RSS is a process lifetime high watermark; -1 means unavailable.
{{#MEMORY_RANK}}
rank {{RANK}}: setup peak RSS bytes {{SETUP_PEAK_RSS_BYTES}}, peak RSS bytes {{PEAK_RSS_BYTES}}, domain edge sites {{EDGE_SITES}}, halo send distributions {{HALO_DISTRIBUTIONS}}, halo send bytes {{HALO_BYTES}}
{{/MEMORY_RANK}}
