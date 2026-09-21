# Build from source

Use Windows 10/11 x64 and a writable extracted source folder. No retail Gothic files are needed to build. A first build needs internet access and several gigabytes of free disk space.

Two targets are built from this tree: the **Quest APK**, which is what players install, and the **Windows PCVR** executable, which is a bring-up target and is described further down. Both use the same VR layer.

## Quest APK

Run **BUILD-APK.bat**. The script automatically prepares portable Python, JDK 21, Android command-line tools, the pinned SDK/NDK, CMake/Ninja, OpenXR and Vulkan Headers. Review and accept the Android SDK licenses when prompted. Gradle downloads its distribution and Maven dependencies through the generated wrapper. No Visual Studio or Android Studio installation is needed for the APK.

**SETUP-DEPENDENCIES.bat** prepares the toolchain in advance. **BUILD-APK.bat -Offline** works after a successful online build has populated dependency and Gradle caches. **VERIFY-APK.bat** reruns artifact checks after a build. **INSTALL.bat** installs it and imports your purchased game.

## Pinned tools

| Component | Version |
| --- | --- |
| Portable Python | 3.12.10 |
| Temurin JDK | 21.0.11+10 |
| Android SDK / Build Tools | API 35 / 35.0.0 |
| Android NDK | 27.2.12479018 |
| Android CMake / Ninja | 3.22.1 / SDK-provided |
| Platform Tools | 37.0.1 |
| Gradle / Android Gradle Plugin | 8.9 / 8.7.3 |
| OpenXR loader | 1.1.43 |

Downloads and checksums are in `config/bootstrap.lock.json`, `config/source-lock.json`, `config/openxr-sdk.lock.json` and `config/android-toolchain.lock.json`. Sources are from [Python](https://www.python.org/downloads/release/python-31210/), [Adoptium](https://github.com/adoptium/temurin21-binaries), [Android](https://developer.android.com/studio) and the dependency authors. Archive hashes are checked before extraction. No global PATH changes are made.

To use an existing toolchain:

```powershell
.\tools\build-android.ps1 -SdkRoot 'D:\Android\sdk' -JavaRoot 'D:\Java\jdk-21'
```

Optional `-Python` selects Python 3.10+, and `-GradleHome` selects a Gradle cache. Otherwise generated tools and caches remain in `toolchain/`. Output APK: `build/android/Gothic2VR/app/build/outputs/apk/release/app-release.apk`. Build and verification manifests are in `build/android/`; detailed build logs are in `logs/`.

## Signing and updates

A self-build creates a private local development signing key in `toolchain/android-debug.keystore`. Keep it for your future updates. Its key differs from the official release key, so a self-build generally cannot update an official APK in place.

Maintainers can supply an existing key using `GOTHIC2VR_KEYSTORE`, `GOTHIC2VR_KEY_ALIAS`, `GOTHIC2VR_STORE_PASSWORD` and `GOTHIC2VR_KEY_PASSWORD`. Never commit or distribute keys or passwords. The scripts verify the APK signature and do not uninstall apps to bypass a mismatch.

## Windows PCVR

**This target has never been run on a headset.** It configures, compiles, links and starts, and the host suites cover its math, but every runtime claim below is reasoned from the code rather than observed in a HMD. Treat it as a bring-up target, not a release. The Quest APK remains the tested target.

Needs Visual Studio 2022 with the x64 C++ tools, CMake 3.22+, Ninja, and the [Vulkan SDK](https://vulkan.lunarg.com/). `vr/questxr.cpp` calls Vulkan directly, so `VULKAN_SDK` must be set in the shell that configures CMake. No Android toolchain, JDK or Gradle is involved.

The build only reads `%VULKAN_SDK%\include` and `%VULKAN_SDK%\lib\vulkan-1.lib` (see `engine/CMakeLists.txt`); it never calls the SDK's own tools, validation layers or installer. A full LunarG install can be skipped by pointing `VULKAN_SDK` at a smaller, hand-built folder instead:

- **`include/`** — the Vulkan-Headers tree. `config/source-lock.json` already pins and checksums `vulkan-sdk-1.4.357.0` for the Android build (`tempestvulkanheaders`); reuse that same archive's `include/` folder here instead of downloading it twice.
- **`lib/vulkan-1.lib`** — an import library generated from the graphics driver's own `vulkan-1.dll`, from an x64 Developer Command Prompt:

  ```powershell
  $dump = dumpbin /exports C:\Windows\System32\vulkan-1.dll
  "EXPORTS" | Out-File vulkan-1.def -Encoding ascii
  $dump | Select-String '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)' |
    ForEach-Object { $_.Matches[0].Groups[1].Value } |
    Out-File vulkan-1.def -Append -Encoding ascii
  lib /def:vulkan-1.def /out:vulkan-1.lib /machine:x64
  ```

- **`bin/glslangValidator.exe`** — the GLSL-to-SPIR-V compiler `shader/CMakeLists.txt` needs to build the engine's shaders. Unlike `include/` and `lib/`, this one isn't produced from anything already pinned in this repo; take it from an existing Vulkan SDK's `Bin/`, from the Android NDK's `shader-tools/windows-x86_64/` (once `BUILD-APK.bat` has fetched the NDK), or from a [glslang release](https://github.com/KhronosGroup/glslang/releases).

Lay the three folders side by side (e.g. `C:\vk-shim\include`, `C:\vk-shim\lib\vulkan-1.lib`, `C:\vk-shim\bin\glslangValidator.exe`) and set `VULKAN_SDK` to that folder. `vulkan-1.dll` itself is never copied in; it still comes from the installed graphics driver at run time, same as with a full SDK install.

`find_program(GLSLANGVALIDATOR glslangValidator ...)` in `shader/CMakeLists.txt` only searches `PATH` on this target, not `%VULKAN_SDK%\bin` — a real Vulkan SDK install adds its `Bin/` to `PATH` itself, but a hand-built shim folder needs that done explicitly: add `<shim>\bin` to `PATH` too, in the same shell (or persistently) alongside `VULKAN_SDK`, or CMake configure fails with `glslangValidator required`.

Fetch the pinned Windows OpenXR loader once. It is downloaded and checksum-verified from `config/openxr-sdk-windows.lock.json` and unpacked into `toolchain/openxr-1.1.43-win`:

```powershell
.\tools\prepare-openxr.ps1 -Windows
```

Then configure and build from an x64 Developer Command Prompt:

```powershell
cmake -S engine -B build/pcvr -G Ninja -DCMAKE_BUILD_TYPE=Release -DGOTHIC2VR_BUILD_PCVR=ON
cmake --build build/pcvr --target Gothic2Notr
```

`GOTHIC2VR_BUILD_PCVR=ON` defines `GOTHIC2VR_OPENXR`, links the imported `openxr_loader`, adds the Vulkan SDK include and library directories, and copies `openxr_loader.dll` and `android/assets/vrhands/` next to the executable after linking. It is `OFF` by default, so the same tree also configures as a flat desktop OpenGothic build with mouse and keyboard and no OpenXR; that build is useful for isolating whether a problem is in the VR layer or in the engine underneath.

`OPENGOTHIC_BUILD_SPACER` is likewise `OFF` by default, because the Spacer world editor sources are not part of this source kit. Turning it on stops configuration with a pointer to upstream OpenGothic instead of a missing-file error.

### Shipped folder

The build writes `build/pcvr/opengothic/`, which is the install layout. Copy the folder anywhere; nothing is registered and nothing is written outside it.

| File | Needed |
| --- | --- |
| `Gothic2Notr.exe` | The game. |
| `Tempest.dll` | Required; the engine's rendering and platform library. |
| `openxr_loader.dll` | Required; the executable imports it, and it finds the installed runtime. |
| `dxcompiler.dll` | Required. Tempest imports it at load time even on the Vulkan path, so the process will not start without it. |
| `dxil.dll` | Not needed. It is loaded only when the DirectX 12 backend is created, and the VR build rejects `-dx12`. Keep it only in a flat desktop build. |
| `vrhands/` | Required; the hand meshes and their albedo, read from beside the executable. Its `SOURCE.md` and `ULTIMATEXR_LICENSE.txt` ship with them. |
| `VR.ini` | Generated. Written beside the executable on first save, not into the working directory. |

The Vulkan runtime (`vulkan-1.dll`) comes from the graphics driver, and the Microsoft Visual C++ 2015-2022 x64 redistributable must be installed; neither is shipped in the folder. An OpenXR runtime - SteamVR, the Oculus app, Windows Mixed Reality or another - has to be installed and set as active.

### Running it

| Argument | Effect |
| --- | --- |
| `-g <GothicIIDir>` | The installed Gothic II Gold folder, the one containing `Data` and `System`. Optional; without it the engine tries to detect an installation. |
| `-vrinfo` | Creates an OpenXR instance, reports the runtime, the system, the view configurations with their recommended eye sizes, and which extensions this build wants against what the runtime offers, then exits. No session, no Vulkan device and no window, so it says nothing about interaction profiles or refresh rates. The report is printed and also written to `vrinfo.txt` in the working directory, so a launch from Explorer still leaves an answer on disk. Run this first when the game will not start. |
| `-window` | Flat desktop build only. The VR build's mirror is always a window. |
| `-dx12` | Rejected in the VR build: the OpenXR runtime creates the Vulkan instance and device, so a DirectX 12 backend would never reach the headset. |

Four `[ENGINE]` keys matter on this target. `vrBlankFrame` and `vrMirrorOff` are read through the engine's normal settings lookup, so they may sit in a `Gothic.ini` in the folder the game is started from or in `<GothicIIDir>\System\Gothic.ini`. `vrMaxEyeWidth` and `vrMaxEyeHeight` are read directly from a `Gothic.ini` in the working directory only. Other `vr*` keys exist. `vrSplitLeftOff`, `vrHudRectCopyOff`, `vrProfilerExtrasOff` and `vrSpaceWarpProbeOff` are read on both targets; the renderer's mobile escape hatches - `vrBakedShadowOff`, `vrFogFoldOff`, `vrScaleRectOff`, `vrStashHalfOff`, `vrShadowTilesOff`, `vrSkyRateOff` and the rest of that group - are read only on Android and do nothing here.

| Key | Default | Effect |
| --- | --- | --- |
| `vrBlankFrame` | `0` | `1` submits flat-coloured stereo frames and bypasses the game renderer. A bring-up aid for checking that the session, swapchain format negotiation and frame submission work. |
| `vrMirrorOff` | `0` | `1` turns off the desktop mirror window's content and restores the direct-output fast path. The two are mutually exclusive; see [KNOWN_ISSUES.md](KNOWN_ISSUES.md). |
| `vrMaxEyeWidth` | `1280` | Per-eye render width cap. Every full-resolution render target is sized from it. `0` or less keeps 1280. Raise it for sharpness at the cost of VRAM and frame time. |
| `vrMaxEyeHeight` | off | Extra height cap for runtimes that recommend an unusually tall eye image. Aspect is preserved, so the width shrinks with it. |

There is no installer and no player package for this target. Building it is the only way to get it, and it is not part of the released Quest ZIP.

## Host tests and source integrity

`tools/test-vr-source.py` compiles 15 C++20 host suites from the shipped code, including melee contact, grips, calibration, target HUD, celestial rendering and release defaults. These need a host C++20 compiler; with MSVC, run from an x64 Developer Command Prompt:

```powershell
.\toolchain\python-3.12.10\python.exe -B tools/test-vr-source.py --output build/host-tests
```

`tools/run-host-tests.ps1` is the same run as one command for a CI job or an ordinary shell. It resolves the portable Python and, when no compiler is on PATH, enters the x64 MSVC environment itself, so no Developer Command Prompt is needed first. It exits non-zero on the first failure. `-IncludeInstallers` adds the installer scenarios, `-Cxx` selects a compiler, and `-Output` moves the logs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run-host-tests.ps1
```

`powershell -NoProfile -ExecutionPolicy Bypass -File tests/installers.ps1` runs eight deployment scenarios against a simulated ADB boundary, without touching a headset.

This optional contributor test compiler is not required by players or for the Android build. Tests do not launch the game or establish headset performance.

**AUDIT-SOURCE-KIT.bat** checks the recorded source hashes, excluding local build/toolchain/log directories. For an extracted publication archive, use `tools/audit-source-kit.py` without `--allow-local-state`: archive validation rejects build outputs, game payloads and signing material. Maintainers must regenerate `SOURCE-MANIFEST.json` and `SOURCE-SHA256.txt` after reviewed source or documentation changes.
