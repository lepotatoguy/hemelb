<!-- This file is part of HemeLB and is Copyright (C) -->
<!-- the HemeLB team and/or their institutions, as detailed in the -->
<!-- file AUTHORS. This software is provided under the terms of the -->
<!-- license in the file LICENSE. -->

# Legacy and abandoned components

These parts of the repository are kept for history or for specialised
workflows. None of them is tested in CI, and some no longer run with current
Python or VTK. Do not change them as part of routine work unless someone owns
them and adds tests.

| Path | What it is | State |
| --- | --- | --- |
| `PiT/` | Older parareal (parallel-in-time) experiments | Legacy Python; its `Scripts/*.py` still import `xdrlib`, which Python 3.13 removed |
| `deploy/` | Fabric-based deployment for historical machines | Assumes an old build workflow |
| `python-tools/hlb/cache.py` | Old file-cache decorators with Python 2 compatibility code | Not used by the `hlb` command-line tools |
| `Tools/estimates/estimate.py` | Estimates voxel size, site count and time steps from a vessel diameter | Does not run: the file ends after `if __name__ == '__main__':`, and it needs the `unum` package |
| `geometry-tool/InletProcessing/` | Weights files for velocity inlets ([non-cylindrical-velocity-inlets.md](../user/non-cylindrical-velocity-inlets.md)) | Uses a VTK 5 call (`GetProducerPort`) |

Each of these should either be modernised as a separate piece of work or be
marked unsupported. They are deliberately not removed here.
