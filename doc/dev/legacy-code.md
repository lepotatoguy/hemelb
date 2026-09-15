<!-- This file is part of HemeLB and is Copyright (C) -->
<!-- the HemeLB team and/or their institutions, as detailed in the -->
<!-- file AUTHORS. This software is provided under the terms of the -->
<!-- license in the file LICENSE. -->

# Legacy and abandoned components

The following areas are retained for historical or specialised workflows and
should not be changed as part of routine geometry-tool or simulation work
without an owner and dedicated tests:

- `PiT/` contains the older parareal experimentation workflow. It uses legacy
  Python conventions and is not covered by the main CI jobs.
- `deploy/` contains the Fabric-based deployment tooling for historical
  machines. Its runtime assumptions differ from the current build workflow.
- `python-tools/hlb/cache.py` contains the older file-cache decorators and
  Python 2 compatibility code. It is not part of the current public CLI path.

These components should be either modernised in a separately scoped project or
explicitly marked unsupported. They are intentionally not removed here.
