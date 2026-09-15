# Gothic II VR 0.1.0 Alpha

[Install](INSTALL.md) · [Controls](CONTROLS.md) · [Build from source](BUILDING.md) · [Limitations](KNOWN_ISSUES.md)

Gothic II: Night of the Raven on standalone **Meta Quest 3**, with tracked hands, physical combat and a stereoscopic world. A Windows PC is used for installation; the game itself runs on the headset.

**This is a very early alpha. The game is not yet ready for a complete playthrough. Many interactions and original game features are unfinished or may fail.** Keep several save slots.

## VR features

- Positional head tracking, physical leaning and room-scale movement with collision.
- Visible hands, physical inventory holsters, hand-to-hand weapon transfers, throwing and catching.
- Physical melee and fist attacks, parrying and two-handed grips. Two-handed weapons require both hands to deal damage. Hits use OpenGothic's native damage, armor and reaction rules.
- Bow draw and release: the bow hand aims; pulling the other hand back sets shot power. The arrow and support hand attach to the bow when the second grip engages. Crossbows have separate alignment profiles.
- Weapon, support-hand, aiming and holster calibration. Bow and crossbow family defaults reduce repeated setup. Bow string height follows model dimensions, with manual position and height controls. The original string is removed from supported bow meshes at runtime.
- Optional red bow sight, nearby pickup highlighting and an enemy health HUD.
- An in-headset holster menu showing each slot's contents, with assignment, swapping and dropping controls. Original inventory access through **Open game interface** in a theater panel.
- Haptics, adjustable running speed, snap/smooth/physical turning, button mapping and saved settings.
- Adjustable HUD, resolution, draw distance, lighting, terrain detail and an optional profiler.

## Install

Download the **Gothic-II-VR-0.1.0-Alpha-Quest.zip** player package, extract it, and run **INSTALL.bat**. It downloads its own tools, installs the APK and prepares your purchased Gothic II Gold / Night of the Raven data. See [INSTALL.md](INSTALL.md). No separate Python, Java, Android SDK or SideQuest installation is needed by players.

This source kit contains no APK, purchased game data, saves, toolchains or signing keys. To build your own APK, run **BUILD-APK.bat**; dependencies download automatically. Then use **INSTALL.bat** from this folder.

New players start with the release's tuned weapon calibrations, holster positions and VR settings. Existing `VR.ini` profiles are preserved. The welcome panel appears on the first movement attempt and stays until a button is pressed; it is shown once per profile.

## Credits and source

Built on **[OpenGothic by Try and its contributors](https://github.com/Try/OpenGothic)**. Their engine provides world loading, scripting, quests, dialogue, NPCs and core RPG systems. The Android baseline is **[Solessfir's OpenGothic port](https://github.com/Solessfir/OpenGothic)**, with Tempest and other open-source libraries. Our changes add Quest OpenXR support, VR interaction and rendering work.

Gothic II and its game materials belong to their respective rights holders. This is an unofficial community project. See [NOTICE.md](NOTICE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE).

The matching source archive is **Gothic-II-VR-0.1.0-Alpha-Source.zip**. Distribute it alongside the player package. Sources, build scripts and third-party notices are included; required external dependencies have pinned download URLs and checksums.

[Discord discussion and bug reports](https://discord.com/channels/747967102895390741/1543691482861408276)
