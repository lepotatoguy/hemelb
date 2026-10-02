# Multiscale pressure coupling

The optional MPWide module has been adapted to the current builder and MPI
datatype APIs. Build with `HEMELB_BUILD_MULTISCALE=ON` and an MPWide dependency.
The local multiscale build and its fluid regression suite have been exercised;
production coupled-peer operation and the shared interface remain unverified.

For file-based flow/pressure exchange, use the separate
[read/write coupling guide](../../doc/user/coupling.md). That workflow uses a
velocity boundary and requires no MPWide build option.
