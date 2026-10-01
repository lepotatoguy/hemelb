# HemePure example cases

These cases are copied unchanged from HemePure (https://github.com/UCL-CCS/HemePure),
`cases/` directory at commit `0bf67b16b23b41a06507810337a445f9916d62bf` (2025-10-29).
HemePure is distributed under the BSD 3-Clause License; its license text is kept in
`LICENSE.HemePure` alongside these files, as that license requires.

Excluded from the copy: the four `hemeXtract` files, which are prebuilt Linux x86-64
executables. Build `hemeXtract` from its own source instead.

These original input files use XML version 3 and now load directly in the
solver. `hlb-convert-config input.xml converted/input.xml` can optionally save
version 6 XML. See [the conversion guide](../../doc/user/scalability-and-inputs.md) and
[the case audit](../../research/CASE_STATUS.json). Missing geometry, unsupported
features, malformed source XML, and invalid boundary radii are recorded there.
