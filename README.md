# Gothic II VR 0.2.1

[Install](INSTALL.md) | [Controls](CONTROLS.md) | [Changelog](CHANGELOG.md) | [Build from source](BUILDING.md) | [Limitations](KNOWN_ISSUES.md)

Gothic II: Night of the Raven in VR, with tracked hands, physical combat and stereoscopic rendering. The standalone build runs on Meta Quest. Windows PCVR sources and OpenXR launchers are included in the source kit.

This is an early alpha. Full-game completion is not supported yet; keep several save slots.

## This release

- Steady stick-driven movement and gravity-based jumps.
- 2D, cinematic 3D and first-person cutscenes, switchable with R3 or the VR menu.
- RDR-derived VRS (Low by default) and optional TAA with corrected temporal reprojection.
- Hold Menu or press L3 + R3 for VR settings. Both grips + Y opens the game menu.
- A simplified Graphics menu and corrected subtitle/HUD placement.

See [RELEASE-NOTES.md](RELEASE-NOTES.md) for the full release changelog.

## VR features

Positional head tracking, room-scale collision, tracked hands, melee combat, bows, crossbows, physical holsters, weapon calibration, hand-stroke swimming and diving. Running supports toggle or hold. Turning, button mapping, world scale and HUD placement are configurable.

## Packages

- **Gothic-II-VR-0.2.1-Quest.zip**: APK and Windows installation/update scripts. Extract the complete archive and run **INSTALL.bat** for a new installation or **UPDATE.bat** for an APK-only update.
- **Gothic-II-VR-0.2.1-Source.zip**: engine, rendering backend, shaders, tests, Quest build/install tools, PCVR launchers, licenses and checksums. External build dependencies have pinned download URLs and hashes.

An owned Gothic II Gold / Night of the Raven installation is required. Retail game files, saves and personal settings are excluded. The standard installer supports Windows 10/11. See [WINDOWS7.md](WINDOWS7.md) for the separate compatibility workflow. No new PCVR binary is included in these packages; see [BUILDING.md](BUILDING.md) for the Windows build.

## Credits

Built on OpenGothic by Try and contributors, using Solessfir's Android OpenGothic and Tempest forks. Windows PCVR support was contributed by [JaXt0r](https://github.com/JaXt0r) in [PR #1](https://github.com/dubrovskiy-yevhen-stakelogic/gothic2-vr/pull/1).

Gothic II and its assets belong to their respective rights holders. This is an unofficial community project. See [NOTICE.md](NOTICE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE).

[Discord discussion and bug reports](https://discord.com/channels/747967102895390741/1543691482861408276)
