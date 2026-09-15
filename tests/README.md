# VR host regression tests

From the source-kit root, with Python 3.10+ and a C++20 compiler:

```text
python tools/test-vr-source.py
```

For MSVC, open an **x64 Developer Command Prompt** first. GCC/Clang can be
selected with `--cxx` or the `CXX` environment variable; these accept an
executable path, with compiler flags managed by the runner.

The runner builds eight small executables from this kit's current headers,
math and input source. The Python fixtures extract current native adapter
methods and supply deterministic world, inventory and rendering boundaries.
They test coordinates, snap turning, settings/menu behavior, room movement,
holsters, physical swings, bow gestures, calibration, native combat/parry/drop,
bow/potion/fist adapters, focus and selected gameplay regressions.

The interaction suite excludes the original file-backed hand-mesh parsing
checks, so no game or hand assets are needed. Hand pose weights remain tested.
No existing executable is reused, and the game is not linked or launched.

Files are generated in a fresh temporary directory and removed after the run.
To retain compiler output, test logs, executables and `report.json`, use:

```text
python tools/test-vr-source.py --output ../vr-host-results
```

The three source guards check APK/OpenXR version consistency, the removed
rain-cache duplication, and obvious assistant placeholder phrases in the VR
source. These are narrow regression guards, not a complete code review.
Host passes do not establish headset visuals, frame pacing, collision feel or
runtime stability.
