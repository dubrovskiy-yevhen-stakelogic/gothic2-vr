# Gothic II VR 0.2.1

## Added

- Three cutscene modes: 2D theater, cinematic 3D with the original camera, and first person. Press R3 during a cutscene or select a mode in the VR menu. New profiles start in cinematic 3D; saved choices are retained. Prerecorded videos remain 2D.
- RDR-derived VRS with Off, Low, Medium, High and Ultra levels. Low is the default; unsupported hardware uses full-rate shading.
- Optional RDR-derived TAA with separate history for each eye. Reprojection uses camera-relative coordinates to prevent image drift at large world coordinates. Hands and HUD remain outside the temporal filter.

## Improved

- Ground movement follows the stick at a steady speed instead of the impulses in the walking animation. Direction changes respond immediately and releasing the stick stops movement without a stop-animation push.
- Free jumps use gravity and preserve launch momentum for a more natural VR feel.
- Hold Menu to open VR settings. A short press retains the game-menu action; L3 + R3 remains available.
- Performance renamed Graphics. Removed rendering comparison controls from the regular menu and replaced the detailed profiler with a simple FPS counter.

## Fixed

- Clipped dialogue subtitles near the top of the view. Dialogue choices retain their position.
- Stray pixels along the HUD edge.
- The mobile wheel opening when Menu was held.
- Image drift with TAA enabled while the headset was held still.
- Disabled the old fragment-density-map foveation path that produced artifacts.

## Known issues

- Magic is currently not working. This is a known issue; a fix is planned for a future update, with no release date yet.

## Compatibility

- Existing saves and settings are retained when updating.
- Requires an owned Gothic II Gold / Night of the Raven installation. Game data is not included.
- Quest 3S feedback confirmed VRS, the TAA correction and the movement update. Full-game completion and a new PCVR gameplay pass have not been verified.

---

# Gothic II VR 0.2.0

## Installer follow-up (2026-09-24)

- Added a separate offline Quest installer candidate for Windows 7 SP1 x86, with bundled 32-bit Python and ADB, indexed-ZIP preparation, APK-only updates and checks for save preservation. Windows 7 device validation is pending. The game APK and standard Windows 10/11 installer are unchanged. See [WINDOWS7.md](WINDOWS7.md).

Released package prepared on 2026-09-23. This remains an early alpha; full-game completion is not yet supported.

## Added

- Windows PCVR support, contributed by [JaXt0r](https://github.com/JaXt0r) in [PR #1](https://github.com/dubrovskiy-yevhen-stakelogic/gothic2-vr/pull/1).
- One player archive containing Windows PCVR and standalone Meta Quest builds.
- Separate launchers for SteamVR / Steam Link, Oculus / Meta Quest Link / Air Link, and Virtual Desktop with VDXR. Runtime selection applies only to the launched game.
- Physical swimming adapted from the GTA San Andreas VR Quest port: hand strokes propel you along your gaze, sculling keeps you afloat, and looking up or down while stroking controls ascent and diving.
- Both grips + Y opens the game menu. L3 + R3 opens VR settings, including over Steam Link when Menu is reserved by the runtime.
- Toggle running with one press of L3. Choose **Locomotion > Run button > Hold** for the previous hold-to-run behavior.

## Fixed

- Continuous camera rotation during head-relative PCVR movement, while preserving flat desktop and Android touch camera behavior.
- Menu shortcuts now consume their buttons instead of also opening the journal, running or crouching.
- Scrolling VR settings no longer changes values accidentally: the stick selects rows; triggers and A edit or confirm.
- Outdoor SteamVR flashes, including disappearing objects and lighting, caused by incomplete object uploads at early GPU submission.
- Missing tracked hands while swimming and diving.
- An opaque water view on entry; water classification now uses each tracked eye's position.
- Shore exit and the transition back to normal walking and jumping. Neutral swim input no longer turns the body 180 degrees away from the walking direction.
- Resizing or minimizing the desktop mirror no longer changes the headset UI dimensions.
- Unsupported OpenXR colour formats now fail with a clear message instead of displaying incorrect colours.

## Compatibility and updates

- Tested target: Quest 3 with Touch controllers. SteamVR startup, movement, swimming and the outdoor-flash fix were also tested. Oculus/Meta Link and Virtual Desktop gameplay remain unverified.
- Existing saves and settings are preserved. Use **Quest/UPDATE.bat** for an APK-only update; copy the new **PCVR** contents into the existing PCVR folder after backing it up.
- Requires your own Gothic II Gold / Night of the Raven installation. Retail game data is not included.

See [INSTALL.md](INSTALL.md), [CONTROLS.md](CONTROLS.md) and [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
