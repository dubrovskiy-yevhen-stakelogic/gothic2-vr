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

## Packages

- Standalone Quest and Windows PCVR builds, available separately or together in the Player archive. PCVR includes launchers for SteamVR, Meta Quest Link / Air Link and Virtual Desktop.

## Compatibility

- Existing saves and settings are retained when updating.
- Requires an owned Gothic II Gold / Night of the Raven installation. Game data is not included.
- Quest 3S feedback confirmed VRS, the TAA correction and the movement update. Full-game completion and a new PCVR gameplay pass have not been verified.
