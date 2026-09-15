# Command-buffer chunk regression checks

`small-list-tests` checks clearing, reuse and destruction around the 32-entry node boundaries.
It has no graphics or game-data dependencies. On Linux, run it with memory diagnostics from the repository root:

```sh
clang++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer Tests/command-chunks/smalllist.cpp -o /tmp/tempest-small-list-tests
/tmp/tempest-small-list-tests
```

`command-chunk-tests` records up to 97 render passes, submits them and verifies every output pixel.
It reuses the same command buffer across sizes and destroys both small and overflowing lists.
Vulkan and, on Windows, DirectX 12 run with validation requested. No game assets or test shaders are required.

```sh
cmake -S Tests/command-chunks -B build/command-chunks
cmake --build build/command-chunks --config Release
ctest --test-dir build/command-chunks -C Release --output-on-failure
```

Use `-DTEMPEST_TEST_GPU=OFF` for header-only checks without the engine or graphics dependencies.
To reuse an existing Windows build, pass `-DTEMPEST_TEST_LIBRARY=C:/Path/To/Tempest.lib` at configuration.
Add the directories containing the matching `Tempest.dll` and its dependencies (including `dxcompiler.dll`) to `PATH` before running CTest.
GPU test failures are not silently skipped when a backend or validation dependency is unavailable.
