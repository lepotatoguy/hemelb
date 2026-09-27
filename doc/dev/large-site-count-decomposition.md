# Large site count decomposition

## What changed

The initial geometry split now uses 64-bit integer site counts throughout. It also leaves at least one active block for each MPI process and rejects a cumulative site count that exceeds the 64-bit range. Previously, the split converted counts to single precision floating point values. Large counts could lose the distinction between nearby block boundaries.

This change affects the initial block assignment. ParMETIS still performs the later optimisation.

## Tests

`Code/tests/geometry/BasicDecompositionTests.cc` checks the arithmetic edge cases with constructed block counts. These cases cannot be represented by the small sample geometry without allocating an impractically large simulation. The same test file now loads the repository's `Code/tests/resources/large_cylinder.gmy`, confirms its 5,576 fluid sites and 20 active blocks, and checks that the initial split assigns blocks to all four ranks.

I also ran the sample from *HemeLB Made Easy Documentation_Tutorial*: `large_cylinder.xml` and `large_cylinder.gmy` from `Code/tests/resources`, with four MPI processes. For a short check, I changed the XML from 50,000 to 200 steps and added `whole.xtr` extraction for velocity and pressure every 100 steps, as described in the tutorial. I ran this XML with this branch and with commit `432d3386`. The original commit needed the same one-line AppleClang build fix in `Code/configuration/SimBuilder.h` to compile locally.

Both runs completed 200 steps. Both reported 5,576 fluid sites and the same final site counts on ranks 0 through 3: 1,148, 1,640, 1,622, and 1,166. Their `Extracted/whole.xtr` files were byte identical, with SHA-256 `927a704c0b84fc64ad28ac4cfbc8527c93e5b4a5ac93293682aa11bf47e48c78`.

The test executable passed 77 cases with 32,403 assertions after the sample test was added. The real geometry confirms normal behaviour, while the constructed counts cover the large value problem that the sample cannot reach.
