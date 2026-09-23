# Gothic II VR 0.2.0

[Install](INSTALL.md) | [Controls](CONTROLS.md) | [Changelog](CHANGELOG.md) | [Build from source](BUILDING.md) | [Limitations](KNOWN_ISSUES.md)

Gothic II: Night of the Raven in VR on standalone **Meta Quest 3** and **Windows PCVR**, with tracked hands, physical combat and a stereoscopic world. The Quest version runs on the headset; PCVR uses your Windows PC and an OpenXR connection.

The combined **Gothic-II-VR-0.2.0-PCVR-and-Quest.zip** contains separate `PCVR/` and `Quest/` folders. Both share the VR gameplay layer. Oculus/Meta Link is a PCVR connection; standalone Quest uses the APK in `Quest/`.

**This is a very early alpha. The game is not yet ready for a complete playthrough. Many interactions and original game features are unfinished or may fail.** Keep several save slots.

## VR features

- Positional head tracking, physical leaning and room-scale movement with collision.
- Visible hands, physical inventory holsters, hand-to-hand weapon transfers, throwing and catching.
- Physical melee and fist attacks, parrying and two-handed grips. Two-handed weapons require both hands to deal damage. Hits use OpenGothic's native damage, armor and reaction rules.
- Bow draw and release: the bow hand aims; pulling the other hand back sets shot power. The arrow and support hand attach to the bow when the second grip engages. Crossbows have separate alignment profiles.
- Weapon, support-hand, aiming and holster calibration. Bow and crossbow family defaults reduce repeated setup. Bow string height follows model dimensions, with manual position and height controls. The original string is removed from supported bow meshes at runtime.
- Optional red bow sight, nearby pickup highlighting and an enemy health HUD.
- An in-headset holster menu showing each slot's contents, with assignment, swapping and dropping controls. Original inventory and character stats through **Open game interface** and **Character stats** in a theater panel.
- Haptics, adjustable running speed, snap/smooth/physical turning, button mapping and saved settings.
- Adjustable HUD, resolution, draw distance, lighting, terrain detail and an optional profiler.

## Changes in 0.2.0

- Windows PCVR player package with separate SteamVR, Oculus/Meta Link and Virtual Desktop launchers. Runtime selection applies only to the game process.
- Fixed continuous camera rotation during head-relative movement on PCVR; desktop flat and Android touch camera behavior are preserved.
- **Both grips + Y** opens/closes the game menu. **L3 + R3** opens/closes VR settings, including when Steam Link reserves the controller Menu button. Normal Y, L3 and R3 actions remain configurable.
- Menu shortcuts consume their input and wait for neutral controls before returning to gameplay. Their hints appear in VR settings and the welcome panel.
- Resizing, maximizing or minimizing the desktop mirror no longer changes the VR UI dimensions. HUD composition is bounded by the headset image.
- Runtimes must offer the supported RGBA sRGB swapchain format; unsupported formats now produce an explicit error instead of incorrect colours.

- Physical hand-stroke swimming, diving and treading water on Quest and PCVR, with visible hands and a corrected transition to walking on the bank.
- L3 toggles running; **Locomotion > Run button > Hold** restores hold-to-run.
- VR settings use the stick only to select rows. Change values with the triggers or A.
- Fixed outdoor SteamVR flashes caused by object uploads being submitted before the upload worker finished.
- Fixed an opaque view when entering water by classifying each tracked eye against the water surface.

See [CHANGELOG.md](CHANGELOG.md) for the release notes.

## Earlier changes in 0.1.1

- Multi-second stalls when new scenery first comes into view are reduced: world material shaders are prepared on background threads and cached between sessions. A newly seen object can appear a few frames late instead.
- Talking to NPCs no longer requires aiming at their waist. Look at any part of the body; the name on the HUD is the character that **B** talks to.
- Taking damage no longer removes a weapon whose requirements are not met. Such weapons deal a quarter of their damage and show a warning, unless **Ignore weapon requirements** is enabled.
- A brief grip dip or a stall no longer drops or stows the held item.
- **Character stats** in the VR menu opens level, experience, learning points, attributes and talents.
- Picked-up melee weapons are assigned to the right belt and bows to the back-left holster when those holsters are free.
- The original **Cloud shadows** option no longer enables screen-space ambient occlusion in VR.

## Install

Extract **Gothic-II-VR-0.2.0-PCVR-and-Quest.zip**, open its **Quest** folder, and run **INSTALL.bat**. It downloads its own tools, installs the APK and prepares your purchased Gothic II Gold / Night of the Raven data. See [INSTALL.md](INSTALL.md). No separate Python, Java, Android SDK or SideQuest installation is needed by players.

This source kit contains no APK, purchased game data, saves, toolchains or signing keys. To build your own APK, run **BUILD-APK.bat**; dependencies download automatically. Then use **INSTALL.bat** from this folder.

New players start with the release's tuned weapon calibrations, holster positions and VR settings. Existing `VR.ini` profiles are preserved. The welcome panel appears on the first movement attempt and stays until a button is pressed; it is shown once per profile.

## Windows PCVR

Extract **Gothic-II-VR-0.2.0-PCVR-and-Quest.zip** to a writable folder, open **PCVR**, and connect your headset. Start the game with the matching launcher:

| Connection | Launcher |
| --- | --- |
| SteamVR / Steam Link | `START-STEAMVR.cmd` |
| Meta Quest Link / Air Link | `START-OCULUS.cmd` |
| Virtual Desktop with VDXR | `START-VIRTUAL-DESKTOP.cmd` |

The launcher detects a Steam Gothic II installation or asks for the purchased game's root folder containing `Data` and `System`. Keep the release in its own folder. No retail files are included or replaced. Settings and saves are stored in the PCVR folder; see [INSTALL.md](INSTALL.md) for updates and diagnostics.

**Physical swimming:** paddle with either hand along your gaze, scull to stay afloat, and look down/up while stroking to dive/ascend. **L3** toggles running; **Locomotion > Run button > Hold** restores the previous behavior. In VR settings, the stick only selects rows; use LT/RT or A to change values.

**Both grips + Y** opens the game menu; **L3 + R3** opens VR settings. L3/R3 mean pressing the left/right stick inward. These combinations do not require the controller Menu button. Full controls require Touch-compatible buttons or an equivalent mapping; controller limitations are listed in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

Quest swimming, hand visibility, water entry and shore exit were confirmed in headset testing. SteamVR startup, locomotion, physical swimming and the outdoor-flash fix were also confirmed. Oculus/Meta Link and Virtual Desktop gameplay are not yet verified. See [BUILDING.md](BUILDING.md) to build from source.

## Credits and source

Built on **[OpenGothic by Try and its contributors](https://github.com/Try/OpenGothic)**. Their engine provides world loading, scripting, quests, dialogue, NPCs and core RPG systems. The Android baseline is **[Solessfir's OpenGothic port](https://github.com/Solessfir/OpenGothic)**, with Tempest and other open-source libraries. Our changes add Quest OpenXR support, VR interaction and rendering work. Thanks to **[JaXt0r](https://github.com/JaXt0r)** for contributing the Windows PCVR port in [PR #1](https://github.com/dubrovskiy-yevhen-stakelogic/gothic2-vr/pull/1).

Gothic II and its game materials belong to their respective rights holders. This is an unofficial community project. See [NOTICE.md](NOTICE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE).

The matching source archive is **Gothic-II-VR-0.2.0-Source.zip**. Distribute it alongside the player package. Sources, build scripts and third-party notices are included; required external dependencies have pinned download URLs and checksums.

[Discord discussion and bug reports](https://discord.com/channels/747967102895390741/1543691482861408276)
