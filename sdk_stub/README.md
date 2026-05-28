# OnboardAPI Stub SDK

In-tree replacement for the real Rheinmetall OnboardAPI SDK. It exists so that:

- Flowboard builds and runs **without** access to the proprietary EULA-RME-SDK-1.0 binaries.
- CI / contributors can iterate on codegen + engine code freely.

The stub covers the full OnboardAPI interface surface that `pipegen` consumes
(all `M_*` interface headers), with an in-process bus standing in for DDS, so
the engine and codegen can be built and exercised end-to-end without the
proprietary SDK.

## Files

- `include/onboardapi/` — header skeletons, one `M_*.hpp` per OnboardAPI
  interface, plus shared `M_Common.hpp` and `ddkit/utils.hpp`.
- `src/` — corresponding implementations + an in-process `stub_bus` (no DDS).

The `.rmodel` IDL files that `pipegen` consumes are **not** kept here; they come
from the [`Rheinmetall/onboardapi`](https://github.com/Rheinmetall/onboardapi)
git submodule at `external/onboardapi/datamodel` (pinned to tag v9.9.0).

## License

Stub C++ code: MIT (this repo). The OnboardAPI data model (submodule) is
EPL-2.0 (Rheinmetall upstream).
