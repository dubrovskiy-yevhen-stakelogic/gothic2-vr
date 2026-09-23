# Release verification - 0.2.0

The final release retains the gameplay from the accepted heading-fix build. On 2026-09-23 the maintainer confirmed that Quest swimming and shore exit now work, after earlier confirmation of hand visibility and the water-entry visual fix. SteamVR startup, locomotion, swimming and the outdoor-flash fix were confirmed in earlier tests. The final shared water changes still lack a separate final PCVR retest; Oculus/Meta Link and Virtual Desktop gameplay are unverified.

## Review scope

The review covered controller ownership and menu shortcuts, toggle/hold running, neutral swim heading, native water transitions, hand visibility, per-eye water classification, the early GPU submission boundary, runtime launchers and the release installer. Release preparation changes documentation and comments only; it does not change gameplay logic.

Host regression coverage includes 29 suites, with PCVR, Quest, flat and touch camera cases. Android checks cover package identity, version 0.2.0 / code 62, ARM64 native libraries, signature, assets and Android lint. Separate launcher tests use a stub executable; installer tests use simulated ADB and do not touch a headset.

## Distribution checks

The source archive includes engine code, shared VR fixes, shaders, tests, Quest build/install scripts, PCVR launchers and dependency notices. A file manifest and SHA256 list cover the source snapshot. The archive is extracted into a clean directory and audited again without exclusions for local build state.

The player archive contains separate PCVR and Quest folders, current documentation, licenses and a file checksum manifest. Executable and APK hashes are checked against the verified builds. Purchased game files, personal settings, saves, signing keys and local diagnostic logs are excluded.

Source review and automated checks do not establish full-playthrough compatibility or support for untested devices. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
