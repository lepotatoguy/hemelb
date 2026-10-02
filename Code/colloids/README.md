# Legacy subgrid colloidal particles

This force-coupled module remains unmaintained and is not covered by the current
CPU-feature validation. `HEMELB_BUILD_COLLOIDS` is off by default.

For passive particles, use the separate tracer controller in `Code/tracers` and
the [tracer guide](../../doc/user/tracers.md). It works in the fluid build and
accepts supported legacy colloid particle spellings. Nonempty legacy body-force
sections require explicit `mode="tracer"` to discard the forces; they do not
enable active force coupling.
