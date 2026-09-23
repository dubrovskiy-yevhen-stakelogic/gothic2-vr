# Install Gothic II VR 0.2.0

The combined **Gothic-II-VR-0.2.0-PCVR-and-Quest.zip** contains both targets. Use `Quest/` for standalone play, or `PCVR/` for SteamVR, Oculus/Meta Link and Virtual Desktop. Extract the complete archive before running a script.

## Quest: you need

- Windows 10/11 x64, internet access, a USB data cable and a Quest 3.
- Your own installed **Gothic II Gold / Night of the Raven**. Clean game data is recommended; Windows DLL mods are unsupported.
- Developer Mode enabled for the headset. Connect USB and accept **Allow USB debugging** inside the headset. Account/device permission prompts cannot be completed by the installer.
- Space on the PC for a private game-data ZIP, and on Quest for both that ZIP and the imported game plus a 512 MiB reserve. The installer checks Quest free space.

## First installation

1. Extract the combined archive to a writable folder, such as `C:\Games\Gothic2VR`, and open its **Quest** folder. Do not run scripts inside the ZIP.
2. Connect the Quest with USB debugging allowed and run **INSTALL.bat**.
3. Enter the path to your purchased game's installation folder when asked. Choose the folder containing `Data`, `_work` and `System`, not a save folder.
4. The script automatically downloads checksum-verified ADB and portable Python when needed. It packages your local game files, verifies the APK, installs it and transfers the private data archive. Saves are excluded from packaging.
5. The installer opens the import screen. In the headset, choose the ZIP in **Download/Gothic2VR**, approve file access and wait for import to finish.
6. Launch **Gothic II VR** from the Quest library / Unknown Sources.

No Python, Java, Android Studio, SDK or SideQuest download is required from the player. Tools are stored locally under `toolchain/`. If Windows reports a missing Quest driver, the installer downloads the official [Meta USB driver](https://developers.meta.com/horizon/downloads/package/oculus-adb-drivers/) and requests administrator permission to install it. A headset that is not detected still needs Developer Mode, USB debugging permission and a working USB data connection; the script reports the problem rather than installing to a different device.

## Updates

Run **UPDATE.bat** for an APK-only update. **INSTALL.bat** also skips data import when an existing imported game is detected. Both preserve existing settings and saves and verify their hashes around APK installation. They never uninstall the app. If Android reports a signing mismatch, keep the installed app and obtain a compatible update; uninstalling can remove its data.

New installs use the release calibrations and settings. Updates keep your own profile, including whether the welcome panel has already been dismissed.

## Advanced commands

```powershell
.\tools\install-release.ps1 -GameDir 'D:\Games\Gothic II' -Serial YOUR_QUEST_SERIAL
.\tools\install-release.ps1 -UpdateOnly
.\tools\install-release.ps1 -DryRun
```

`-DryRun` checks the APK, device and available storage without installing, copying game data or launching an activity. `PACKAGE-GAME-DATA.bat` prepares data separately; `OPEN-DATA-IMPORT.bat` reopens the file picker after an interrupted import. A previously verified local game ZIP is reused on retry.

The source kit uses the same installer after **BUILD-APK.bat** completes. It verifies the local build manifest instead of the player package's `release.json`.

## File locations

- Quest app files: `/sdcard/Android/data/com.gothic2vr.quest/files/`
- Imported game: `Gothic2/` inside that directory.
- VR settings: `VR.ini` inside the app files directory.
- PC private data package: `build/private/game-data.zip`. This contains your purchased game: do not share it.
- PC install reports: `build/android/deployments/`. Reports can contain device IDs and local paths; review them before sharing.

## If installation stops

Keep the command window open and read the error. A checksum failure stops installation. Network interruption can be retried by running the script again. A previous partial *game transfer* is preserved for inspection; remove only that named `.part` file if you intend to restart it. Do not remove saves, settings or the installed app. The Android file picker requires confirmation in the headset even when all PC steps are automatic.

## Windows PCVR

1. Extract the combined archive to a writable folder and open **PCVR**. Keep the EXE, DLLs, launchers and `vrhands` together.
2. Connect the headset through SteamVR/Steam Link, Meta Link/Air Link, or Virtual Desktop with VDXR.
3. Run **START-STEAMVR.cmd**, **START-OCULUS.cmd**, or **START-VIRTUAL-DESKTOP.cmd** for that connection. These select OpenXR for this process without changing your system runtime.
4. Confirm the detected Gothic II Gold / Night of the Raven installation or enter its root folder containing `Data` and `System`. The validated path is remembered locally. Game files are read from your purchased installation.
5. Open the game menu with **both grips + Y**, or VR settings with **L3 + R3**. B goes back in menus.

Requires a Vulkan-capable graphics driver and the Microsoft Visual C++ 2015-2022 x64 runtime. `START-PCVR.cmd` uses your existing OpenXR selection instead of selecting a connection explicitly.

For an update, close the game, back up the existing PCVR folder, then copy the new archive's **PCVR contents** into that existing PCVR folder. Avoid creating a nested `PCVR/PCVR` folder. Player archives contain no `VR.ini`, `Gamepad.ini`, `Gothic.ini` or saves, so your existing files remain. Keep using the same working folder to retain its profile and save slots. An alternative is to extract separately and copy your own settings, `launcher-game-path.txt`, and save files into that folder.

If startup fails, run `CHECK-OPENXR.cmd -Runtime SteamVR` (or `Oculus` / `VirtualDesktop`) and read `vrinfo.txt` and `log.txt`. `XR_ERROR_FORM_FACTOR_UNAVAILABLE` means the selected runtime cannot currently provide a headset; connect it through the selected application. Merely starting SteamVR does not select it system-wide. `START-STEAMVR.cmd -CheckOnly` checks launcher paths without starting the game. Custom runtime locations can use `-RuntimeManifest <full-path-to-x64-runtime.json>`.
