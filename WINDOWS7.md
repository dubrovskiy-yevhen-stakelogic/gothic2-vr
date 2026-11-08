# Quest installer for Windows 7 SP1 (32-bit)

The compatibility packaging tool produces **Gothic-II-VR-0.2.1-Quest-Win7-x86.zip** from a verified Quest player folder. This optional archive is separate from the standard release package. It installs the standalone Quest game; it does not add Windows 7 or 32-bit support to PCVR.

This package includes Python 3.8.10 x86, ADB 30.0.5 and the official Meta USB driver. It does not require PowerShell 5.1, .NET setup or an internet connection for downloading installer tools. The APK is unchanged from the 0.2.1 release. The tools have passed checks on a newer Windows host, including execution under 32-bit Python; installation on Windows 7 itself still needs a player test.

## Requirements

- Windows 7 **SP1**, with its runtime/security updates. Python requires Universal C Runtime (KB2999226); older systems may also need the loader update KB2533623. If Python fails before the installer opens, keep the exact missing-DLL or missing-entry-point error for support.
- A writable extracted folder on an NTFS drive, with enough room for the private game ZIP. FAT32 cannot hold a file larger than 4 GiB.
- Your purchased Gothic II Gold / Night of the Raven data.
- A Quest with Developer Mode enabled and USB debugging authorized.

## Install

1. Extract the complete compatibility ZIP. Keep `legacy/`, `tools/` and the APK together.
2. Connect the Quest and accept the USB debugging prompt in the headset.
3. Run **INSTALL-WIN7.bat**. In this compatibility package, **INSTALL.bat** is an alias for the same installer.
4. Select the purchased game folder when asked. The tool creates an indexed ZIP, installs the APK, checks saves/settings and transfers the ZIP.
5. Select the ZIP under **Download/Gothic2VR** in the headset's import screen and wait for extraction to finish.

If Windows does not recognize the Quest ADB interface, use Device Manager > Update Driver > Browse my computer and select `legacy/usbdriver/oculus-go-adb-driver-2.0/usb_driver`, including subfolders. Approve Windows' driver installation prompt. Do not disable signature checks or replace the INF. The installer reports device selection errors without modifying drivers automatically.

## Other actions

- **PACKAGE-GAME-DATA-WIN7.bat** prepares and verifies `build/private/game-data.zip` without contacting a headset. Copy that ZIP manually to `/sdcard/Download/Gothic2VR/` and select it in the game's importer. An ordinary ZIP of the three game folders is not accepted.
- **UPDATE-WIN7.bat** performs an APK-only update and verifies that existing saves/settings keep their hashes. It never uninstalls the app to bypass a signing mismatch.
- **OPEN-DATA-IMPORT.bat** reopens the import screen; it does not launch gameplay.
- **INSTALL-WIN7.bat --check-only** checks the tools, APK, device and headset storage without installing or transferring files.
- Optional arguments: `--game-root "D:\Games\Gothic II"` and `--serial QUEST_SERIAL`.

Prepared archives and partial transfers are preserved on failure. To prepare data from a different game installation, extract another copy of the installer. Diagnostic reports are saved in `build/win7-reports/`; they contain device identifiers and should be reviewed before sharing.

The compatibility ADB server uses local port 5038 to avoid replacing an existing SideQuest/SDK server on port 5037. Python and ADB are private to this folder; PATH and system runtime selection are unchanged. These legacy tool versions are for this offline compatibility workflow only.

## Rebuilding the compatibility package

From the source kit on a current Windows machine with Python 3.10+, run:

```powershell
python tools/package-win7-release.py --base-player "C:\Release\Quest" --output "C:\Release\Win7" --download
```

The base player must contain the verified APK and `release.json`. Downloads use the pinned official URLs and archive hashes in `config/win7-tools.lock.json`; the builder also checks extracted tool hashes. Omit `--download` to use the cached archives under `build/win7-downloads`. Tool binaries and purchased data are not included in the source kit.
