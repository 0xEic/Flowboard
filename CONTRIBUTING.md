# Contributing to Flowboard

Thanks for considering a contribution. Flowboard is MIT-licensed; by submitting
patches you agree to license them under the same terms.

## Building

Prerequisites:
- C++20 compiler (gcc 11+, clang 14+, MSVC 19.34+ / VS 2022)
- CMake 3.20+
- Node 20+ and npm (for the web UI; pass `-DFLOWBOARD_BUILD_WEB=OFF` to skip)
- The real Rheinmetall OnboardAPI SDK (used by default). To build without it,
  pass `-DFLOWBOARD_USE_STUB_SDK=ON` to use the in-tree stub. See
  `docs/INTEGRATING_THE_REAL_SDK.md`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows (PowerShell), the `--config Release` flag matters — Visual Studio is a
multi-config generator and `CMAKE_BUILD_TYPE` has no effect there.

## Adding a node

See [`docs/ADDING_A_NODE.md`](docs/ADDING_A_NODE.md). Short version:

1. Add `src/nodes/my_node.cpp` with a class deriving `Node`.
2. Register via `OP_REGISTER_NODE("Foo.MyNode", MyNode)`.
3. If your node introduces a new value type, also call
   `OP_DECLARE_TYPE(MyType, "MyType")` and add a dispatch arm in
   `src/engine/graph.cpp`.

## Code style

- C++ is `clang-format`-driven by `.clang-format` (LLVM-based, 100-col).
- TypeScript follows the default Vite + ESLint shape (no project-specific rules).
- Every C++ source file carries `// SPDX-License-Identifier: MIT` as line 1.
- Commit messages follow Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`).

## Pull requests

- One feature per PR; rebase on `main` before requesting review.
- Include tests when you add features. The bench in `tests/perf/` is expected to
  stay above 100 k msg/s on a Release build.
- If you modify `.rmodel` parsing in `pipegen/`, run the full smoke set in
  `pipegen/tests/test_parser.cpp` against the real OnboardAPI rmodels.

## Testing locally against the real OnboardAPI SDK

See [`docs/INTEGRATING_THE_REAL_SDK.md`](docs/INTEGRATING_THE_REAL_SDK.md).
