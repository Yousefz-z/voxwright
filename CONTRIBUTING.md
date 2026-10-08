# Contributing to Voxwright

Thanks for helping. This page lists exactly what CI runs, so you can
reproduce every check locally before opening a pull request.

## Toolchain

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.25 or newer | Presets in `CMakePresets.json` |
| Ninja | any recent | |
| Compiler | GCC 13, Clang 18, MSVC 2022, or Apple Clang 15 | C++20 |
| clang-format, clang-tidy | 18 | Linux checks |
| vcpkg | commit `9e593bb18ea69cc5095e012465dcd675a822ed0d` | Same as `builtin-baseline` in `vcpkg.json` |
| Qt | 6.8.3 | Modules: qtmultimedia, qtspeech, qtshadertools |
| Python | 3.10 or newer | Hygiene check, aqtinstall |

### One-time setup (Linux and macOS)

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
git -C ~/vcpkg checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg

python3 -m pip install aqtinstall==3.3.0
python3 -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
    -m qtmultimedia qtspeech qtshadertools --outputdir ~/Qt   # macOS: mac desktop 6.8.3 clang_64
export QT_ROOT_DIR=~/Qt/6.8.3/gcc_64                           # macOS: ~/Qt/6.8.3/macos
```

On Windows use a "x64 Native Tools Command Prompt for VS 2022", `bootstrap-vcpkg.bat`,
and `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 ...`, then set
`VCPKG_ROOT` and `QT_ROOT_DIR` (`...\6.8.3\msvc2022_64`).

The first configure builds the C/C++ dependencies from the pinned overlay ports
in `ports/`; later configures reuse vcpkg's binary cache.

## The checks CI runs

Run these from the repository root. Each must pass before a commit.

```sh
# 1. Repository hygiene (forbidden names, attribution trailers, em/en dashes)
python3 tools/hygiene/check_hygiene.py

# 2. Formatting
tools/ci/check-format.sh

# 3. Build and test (GCC, optimized)
cmake --preset release && cmake --build --preset release && ctest --preset release

# 4. Build and test with Clang, then clang-tidy on the same build
cmake --preset clang && cmake --build --preset clang && ctest --preset clang
tools/ci/run-tidy.sh build/clang

# 5. AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset asan && cmake --build --preset asan && ctest --preset asan

# 6. ThreadSanitizer (engine and the layers below it; the Qt app is excluded)
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

Windows CI runs `cmake --preset windows`, `cmake --build --preset windows`,
`ctest --preset windows`. macOS CI runs the `macos` and `macos-asan` presets.

Changes to the optional neural voice track (`ml/`, see
[docs/ml.md](docs/ml.md)) also need it built and tested with the flag on:

```sh
cmake --preset ml && cmake --build --preset ml && ctest --preset ml
```

The test models in `ml/tests/models` are written by
`tools/ml/make_test_models.py` (needs the `onnx` Python package).

Allocation-tracking tests report as skipped under ThreadSanitizer, whose
runtime owns `operator new`; they run in every other configuration.

## Commit messages

Run the hygiene check on your message too:

```sh
python3 tools/hygiene/check_hygiene.py --commit-msg .git/COMMIT_EDITMSG
```

To do it automatically, install it as a hook:

```sh
printf '#!/bin/sh\nexec python3 tools/hygiene/check_hygiene.py --commit-msg "$1"\n' > .git/hooks/commit-msg
chmod +x .git/hooks/commit-msg
```

## Code style

* Types `PascalCase`, functions and variables `camelCase`, private members
  end in `_`, constants `kPascalCase`, files `snake_case`.
* The audio thread never allocates, locks, or blocks. New DSP blocks need a
  test in `dsp/tests/realtime_tests.cpp`.
* Every failure path returns a specific `vox::ErrorCode` with a message that
  tells the user what to do. No catch-all handlers.
* Every claim in docs about quality or performance is backed by a test or by
  a tool under `tools/` whose output is quoted with the command that made it.
* Scope decisions go in `docs/decisions.md`.

## Pull requests

Fill in the template, link the issue, and include before/after measurements
for DSP changes (the tools in `tools/bench/` print Markdown tables).
