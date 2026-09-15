# Build the APK

Use Windows 10/11 x64 and a writable extracted source folder. No retail Gothic files are needed to build. A first build needs internet access and several gigabytes of free disk space.

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

## Host tests and source integrity

`tools/test-vr-source.py` compiles 15 C++20 host suites from the shipped code, including melee contact, grips, calibration, target HUD, celestial rendering and release defaults. These need a host C++20 compiler; with MSVC, run from an x64 Developer Command Prompt:

```powershell
.\toolchain\python-3.12.10\python.exe -B tools/test-vr-source.py --output build/host-tests
```

`powershell -NoProfile -ExecutionPolicy Bypass -File tests/installers.ps1` runs eight deployment scenarios against a simulated ADB boundary, without touching a headset.

This optional contributor test compiler is not required by players or for the Android build. Tests do not launch the game or establish headset performance.

**AUDIT-SOURCE-KIT.bat** checks the recorded source hashes, excluding local build/toolchain/log directories. For an extracted publication archive, use `tools/audit-source-kit.py` without `--allow-local-state`: archive validation rejects build outputs, game payloads and signing material. Maintainers must regenerate `SOURCE-MANIFEST.json` and `SOURCE-SHA256.txt` after reviewed source or documentation changes.
