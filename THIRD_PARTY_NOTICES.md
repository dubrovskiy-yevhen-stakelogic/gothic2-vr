# Third-party notices

Each component retains its own license. The root MIT license is the original
OpenGothic license; it does not replace the licenses of bundled dependencies.

| Component | Role | License and location |
| --- | --- | --- |
| OpenGothic, Try and contributors; Solessfir Android fork | Game engine and Android baseline | MIT, [engine/LICENSE](engine/LICENSE) |
| Tempest, Try and contributors; Solessfir Android fork | Renderer and platform layer | MIT, [engine/lib/Tempest/LICENSE](engine/lib/Tempest/LICENSE) |
| ZenKit, GothicKit contributors | Gothic file formats and script VM | MIT, [engine/lib/ZenKit/license.md](engine/lib/ZenKit/license.md) |
| dmusic, GothicKit contributors | Music playback | MIT, [engine/lib/dmusic/LICENSE.md](engine/lib/dmusic/LICENSE.md) |
| Bullet Physics | Collision and physics | zlib, [engine/lib/bullet3/LICENSE.txt](engine/lib/bullet3/LICENSE.txt) |
| TinySoundFont | SoundFont synthesizer | MIT, [engine/lib/TinySoundFont/LICENSE](engine/lib/TinySoundFont/LICENSE) |
| RapidJSON | JSON parser | MIT and included third-party notices, [engine/lib/rapidjson/license.txt](engine/lib/rapidjson/license.txt) |
| miniz | Archive support | MIT, [engine/lib/miniz/LICENSE](engine/lib/miniz/LICENSE) |
| OpenAL Soft | Spatial audio | LGPL-2.0-or-later and separately licensed portions, [COPYING](engine/lib/Tempest/Engine/thirdparty/openal-soft/COPYING), [BSD-3Clause](engine/lib/Tempest/Engine/thirdparty/openal-soft/BSD-3Clause), [LICENSE-pffft](engine/lib/Tempest/Engine/thirdparty/openal-soft/LICENSE-pffft) |
| OpenAL default HRTF | Spatialization filter data derived from MIT KEMAR | Provenance retained in [hrtf.txt](engine/lib/Tempest/Engine/thirdparty/openal-soft/docs/hrtf.txt); this is not recorded game audio |
| Roboto, Google | User interface fonts | Apache-2.0, [font notice](engine/lib/Tempest/Engine/fonts/NOTICE.md) and [license](engine/lib/Tempest/Engine/fonts/LICENSE.txt) |
| UltimateXR, VRMADA | Hand geometry, poses and derived hand texture | MIT, [source description](android/assets/vrhands/SOURCE.md), [license](android/assets/vrhands/ULTIMATEXR_LICENSE.txt) |
| Gradle wrapper | Build bootstrap | Apache-2.0; wrapper scripts retain their copyright headers; [Apache license](engine/lib/Tempest/Engine/fonts/LICENSE.txt) |

Tempest also includes zlib, libpng, stb headers, minivorbis, SPIRV-Cross,
libsquish and metal-cpp. ZenKit includes doctest and libsquish. Their notices
are retained beside their sources, including license text embedded in headers.
Development test frameworks keep their own notices. This source kit omits
upstream game samples, binary demos and media that are not required for the
Quest build.

OpenXR loader 1.1.43 (Khronos, Apache-2.0) and Vulkan-Headers are downloaded
separately from the pinned URLs with hash verification. Their archive licenses
remain in the extracted dependency directories. Android SDK/NDK, JDK, Gradle
and Android Maven dependencies are external build prerequisites and are not
the original Gothic game.

The APK includes the Khronos OpenXR loader; its [license and notices](licenses/OpenXR-LICENSE.txt) are retained here. The Android interface also uses Google's AndroidX and Material Components libraries under Apache-2.0; the [Apache-2.0 license text](engine/lib/Tempest/Engine/fonts/LICENSE.txt) is included. Their pinned versions and transitive dependencies are defined by the Android/Gradle build. Downloaded toolchains and the optional Meta USB driver keep their own upstream licenses and are not bundled in the release archives.

Source for the bundled OpenAL Soft version is included. Binary distributors
must retain its notices and meet its applicable source/relinking requirements;
the presence of the root MIT file does not remove those requirements.

## GTA San Andreas VR Quest swimming

`engine/common/vr/vrswimmotion.h` and the motion regression cases in `tests/swim-motion.cpp` are adapted from the GTA San Andreas VR Quest source kit (`native/src/SwimMotion.h` and `native/tests/swim_motion_test.cpp`). The stroke recognition, drag, support, gaze suppression, speed limit and waterline ceiling are retained. Gothic integration uses its own collision, water, climb and breath systems; GTA binary hooks and game assets are not included. Copyright (c) 2026 the GTA San Andreas VR Quest port contributors, MIT; see [license](licenses/GTA-SA-VR-swimming.txt).
