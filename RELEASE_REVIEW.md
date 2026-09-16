# Release verification — 0.1.1 Alpha

Public APK version: **0.1.1-alpha**, Android version code **61**.

- The APK was built from this public source kit with release compilation, signature verification and Android lint. All **33 artifact checks** passed. It is signed with the same certificate as 0.1.0, so it installs as an update.
- **3979 exported engine, packaging, dependency and resource files** match the current development sources byte for byte.
- All **15 host suites (4926 checks)** passed against the public sources, including NPC gaze focus, weapon requirement damage, grip release debounce, pickup holster assignment and the character stats menu action.
- Eight deployment scenarios passed against a simulated ADB interface.
- A development build of these changes was played on Quest 3 by the maintainer: NPC dialogue, the character stats menu and weapons were checked, and the world-entry stall was no longer observed. Later review fixes (switching focus between adjacent NPCs, pipeline job cancellation, arrow damage qualification) are covered by host checks and the build only. This release preparation did not install or launch the public APK.
- Reviewed the changed engine, Tempest and VR source for threading and lifetime errors, stale versions, local paths and placeholder or narrative comments. Original upstream copyright and license notices remain intact.

Host checks and a short headset session do not establish a complete playthrough or performance on other headsets. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

`SOURCE-MANIFEST.json` and `SOURCE-SHA256.txt` describe the complete public source snapshot. Player APK SHA256: `a7cf1f3ecbc6dad30dc4ee824fb56d3bc3e92680c12946cc3c77d43ef7376d10`.
