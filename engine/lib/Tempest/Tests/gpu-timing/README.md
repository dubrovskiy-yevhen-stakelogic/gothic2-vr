# GPU marker timing smoke test

This test needs Vulkan but no window, game assets or compiled shaders. It clears an offscreen attachment in two render passes, submits, waits on the normal fence, and reads timestamp results. It also checks command-buffer reuse, disabling/re-enabling capture, marker overflow and counter wrap math. Unsupported timestamps produce a CTest skip, not a pass. Engine/validation errors fail the test.

From the Tempest checkout with the usual platform dependencies installed:

```powershell
cmake -S Tests/gpu-timing -B build/gpu-timing -DTEMPEST_BUILD_AUDIO=OFF -DTEMPEST_BUILD_DIRECTX12=OFF
cmake --build build/gpu-timing --config Release
ctest --test-dir build/gpu-timing -C Release --output-on-failure
```

Alternatively, `-DTEMPEST_TEST_LIBRARY=C:/path/to/Tempest.lib` reuses an existing shared-library build on Windows. Add its DLL directory to `PATH` before running CTest. Rebuild that library from the same sources first.

API usage:

```cpp
// The caller owns the usual command-buffer submission/fence lifecycle.
auto enc = cmd.startEncoding(device, true);
enc.setDebugMarker("My pass");
// Record work, destroy the encoder, submit, then wait for the submission fence.
// Before recording this command buffer again:
auto intervals = cmd.gpuTimings();
```

Profiling is off by default and implemented only for Vulkan; other backends return empty results. Query pools are allocated only on first opt-in, reset on the GPU before each profiled recording, and read without `VK_QUERY_RESULT_WAIT_BIT`. Read only after the matching submission fence completes, before resetting or re-recording the command buffer. The first interval is named `Unmarked`; subsequent labels describe work from that marker to the next marker or command-buffer end. More than 254 markers merges the tail into `[marker limit]`.

These are bottom-of-pipe elapsed intervals, not isolated shader execution times. Pipeline overlap, barriers, deferred/tiled rendering and work in other submissions can affect attribution. No extra pipeline barriers or render-pass splits are inserted. Profiling itself has overhead. Compare against profiling disabled and avoid claiming that a marker alone identifies an expensive shader.
