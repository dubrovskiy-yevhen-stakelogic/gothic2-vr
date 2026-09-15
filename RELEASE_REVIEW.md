# Release verification — 0.1.0 Alpha

Public APK version: **0.1.0-alpha**, Android version code **60**.

- The APK was built from this public source kit with release compilation, signature verification and Android lint. All **33 artifact checks** passed.
- **3979 exported engine, packaging, dependency and resource files** match the current development sources byte for byte. Portable public scripts replace machine-specific development helpers.
- All **15 host suites (4895 checks)** passed against the public sources, including melee sweeps, two-hand grips, bow calibration, target HUD, celestial vertex math and first-run defaults.
- Eight deployment scenarios passed against a simulated ADB interface: update, dry run, game transfer, insufficient space, signature mismatch, changed settings, corrupt transfer and corrupt APK. Rejected operations do not uninstall or launch the app.
- Automatic downloads and checksum verification succeeded for portable Python, ADB, the official Meta USB driver, JDK, Android command-line tools, Vulkan Headers and OpenXR. SDK platform, Build Tools, NDK and CMake installed into a fresh local SDK. The Windows driver package was downloaded and inspected; an actual driver replacement was not needed on the test PC.
- The public installer passed a read-only preflight on Quest 3. No APK installation, game-data transfer or headset launch was performed during this release preparation.
- The game-data packaging plan recognized an owned Night of the Raven installation and excluded saves. No purchased game data is included in either public archive.
- Release defaults embed the tuned calibration/settings profile. The welcome flag is reset for new players; existing saved profiles are read instead of replaced.
- Reviewed VR source, rendering changes, packaging and public documentation for stale versions, internal planning prose, placeholder implementations and local paths. Removed a duplicate weather/roof cache update. Original upstream copyright and license notices remain intact.

The preceding gameplay build was accepted in headset testing by the maintainer. This release's build and host checks do not establish a complete playthrough, all weapon models or performance on other headsets. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

`SOURCE-MANIFEST.json` and `SOURCE-SHA256.txt` describe the complete public source snapshot. Player APK SHA256: `9913848bc523813df4abf48236097495396e88cdca779878ea02893ed3a364ef`.
