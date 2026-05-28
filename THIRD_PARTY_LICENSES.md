# Third-Party Licenses

Flowboard itself is licensed under the [MIT License](LICENSE). It builds on
the third-party components listed below, each under its own license.

These dependencies are **not** vendored into this repository — the C++ libraries
are fetched at configure time (CMake `FetchContent`) and the web libraries via
`npm`. They are, however, included in **binary releases** (the single executable
links the C++ libraries and embeds the compiled web bundle). This file is
provided so those binary distributions carry the required attributions; include
it alongside any released binary.

All bundled dependencies use permissive licenses (MIT, BSD-3-Clause,
Boost-1.0, Apache-2.0), which are compatible with Flowboard' MIT license.

## C++ libraries (linked into the executable)

| Component | Version | License | Project |
|---|---|---|---|
| nlohmann/json | v3.11.3 | MIT | https://github.com/nlohmann/json |
| rigtorp/SPSCQueue | v1.1 | MIT | https://github.com/rigtorp/SPSCQueue |
| spdlog | v1.14.1 | MIT | https://github.com/gabime/spdlog |
| {fmt} (bundled inside spdlog) | bundled | MIT | https://github.com/fmtlib/fmt |
| Asio (standalone) | asio-1-30-2 | Boost Software License 1.0 | https://github.com/chriskohlhoff/asio |
| Crow | v1.2.0 | BSD-3-Clause | https://github.com/CrowCpp/Crow |

## Web libraries (embedded in the web bundle served by the binary)

| Component | Version | License | Project |
|---|---|---|---|
| React, React-DOM | ^18.3.1 | MIT | https://github.com/facebook/react |
| React Flow (`reactflow`) | ^11.11.4 | MIT | https://github.com/xyflow/xyflow |
| Zustand | ^4.5.5 | MIT | https://github.com/pmndrs/zustand |
| Ajv | ^8.17.0 | MIT | https://github.com/ajv-validator/ajv |
| react-jsonschema-form (`@rjsf/core`, `@rjsf/utils`, `@rjsf/validator-ajv8`) | ^5.21.0 | Apache-2.0 | https://github.com/rjsf-team/react-jsonschema-form |

## Build- and test-only (not shipped in release binaries)

| Component | License | Project |
|---|---|---|
| doctest | MIT | https://github.com/doctest/doctest |
| Vite, TypeScript, Tailwind CSS, ESLint, etc. | MIT | (dev tooling; produces output but is not redistributed) |

## OnboardAPI data model and SDK (not redistributed here)

- **OnboardAPI data model** (`.rmodel` IDL files): consumed via the
  [`Rheinmetall/onboardapi`](https://github.com/Rheinmetall/onboardapi) git
  submodule, licensed **EPL-2.0** by Rheinmetall. It is referenced as a
  submodule, not copied into this repository.
- **OnboardAPI SDK** (real, non-stub build): proprietary, distributed by
  Rheinmetall under EULA-RME-SDK-1.0. It is **not** included or redistributed
  here; users must obtain it separately. The default build uses the in-tree stub
  SDK (MIT, part of this project) instead.

Full license texts for each component are available in their linked
repositories and, after a build, in the fetched dependency sources under the
build directory (`build*/_deps/`) and `web/node_modules/`.
