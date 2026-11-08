# Install Gothic II VR 0.2.1

## Standalone Quest

1. Extract **Gothic-II-VR-0.2.1-Quest.zip** into a writable folder on Windows 10/11.
2. Enable developer mode on the headset, connect it by USB, and accept USB debugging in the headset.
3. Run **INSTALL.bat**. The installer obtains its required tools and asks for the installed Gothic II Gold / Night of the Raven directory if game files have not already been imported.
4. Select the prepared ZIP under **Download/Gothic2VR** when the in-headset importer opens. Wait for import to finish, then launch Gothic II VR.

Purchased game data is required and is not included. Source build tools, Java and the Android SDK are not required for the player package. If several Quest devices are connected, the installer asks which one to use.

## Updating

Run **UPDATE.bat** for an APK-only update. Saves and settings are checked before and after installation. Do not uninstall the existing app to update. A different signing certificate requires resolving the signing mismatch rather than deleting the app.

Release version: 0.2.1. VRS defaults to Low when no preference is saved. Existing cutscene and graphics choices are retained.

## Controls

Hold Menu or press L3 + R3 to open VR settings. Both grips + Y opens the game menu. R3 cycles cutscene modes while a cutscene is active. See [CONTROLS.md](CONTROLS.md).

## Windows PCVR

1. Extract **Gothic-II-VR-0.2.1-PCVR.zip**, or open `PCVR` in the combined **Player.zip**.
2. Install the Microsoft Visual C++ 2015-2022 x64 runtime and an up-to-date Vulkan-capable graphics driver.
3. Connect the headset and open the appropriate launcher: **START-STEAMVR.cmd** for SteamVR / Steam Link, **START-OCULUS.cmd** for Meta Quest Link / Air Link, or **START-VIRTUAL-DESKTOP.cmd** for Virtual Desktop with VDXR. **START-PCVR.cmd** uses the active OpenXR runtime.
4. Select the installed Gothic II Gold / Night of the Raven folder containing `Data` and `System` when prompted.

For an update, close the game and replace the supplied program files. Keep existing saves, `VR.ini` and the purchased game installation. Game data is not included. Windows 10/11 x64 is required.

## Windows 7

The separate Windows 7 compatibility workflow is documented in [WINDOWS7.md](WINDOWS7.md). Its device acceptance remains limited; it does not add PCVR support to Windows 7.
