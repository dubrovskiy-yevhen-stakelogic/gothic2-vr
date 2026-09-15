# Alpha limitations

**0.1.0 Alpha is an early, incomplete VR adaptation. The game is not ready for a complete playthrough.** Many features still need work, and a successful build does not establish full game compatibility.

- Quest 3 with Touch controllers is the tested target. Other Quest models and OpenXR devices are not validated. Controller-free hand tracking is not implemented.
- Some original actions, spells, quests and interactions may not yet work correctly in VR. Keep several saves. Windows DLL plugins, Union and script extensions may not work with this engine.
- Weapon family calibrations and automatic bow dimensions provide a starting point. Unusual models or personal grip preferences may require manual adjustment.
- New profiles enable **Ignore weapon requirements**, matching the release's tested settings. Disable it in the VR hands/holster settings to enforce normal equipment requirements. It does not give the player items: assigned holsters only draw items present in inventory.
- The original inventory is available through **Open game interface** in a theater panel. Some interfaces still use the original navigation.
- Physical crouching lowers the camera but does not resize the collision capsule. Room-scale motion is constrained by world geometry.
- Performance varies by scene and settings. Dynamic lighting and diagnostic modes cost more. Foveation is experimental and off by default. No fixed frame rate is promised.
- VR menus and release documents are English. The original game's language comes from your own game data.
- Updates preserve saved settings. Release defaults apply only when there is no saved `VR.ini`; the welcome notice is once per profile, not once per app launch.

Report the version, headset model, reproduction steps and settings in the [Discord channel](https://discord.com/channels/747967102895390741/1543691482861408276). Do not attach purchased game archives, private signing files or personal data. Installation paths are in [INSTALL.md](INSTALL.md).
