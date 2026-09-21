# Gothic II VR — Technical Overview

*An engineering walkthrough of what this repository actually is: the OpenGothic
engine underneath, and the standalone Quest VR layer built on top of it.*

Scope: repository state at `d74ccbf` (v0.1.1). Line counts and file references
are from that snapshot.

---

## 1. What this repository is

Three things stacked on each other:

| Layer | Origin | What it provides |
| --- | --- | --- |
| **Tempest** (`engine/lib/Tempest`) | [Try/Tempest](https://github.com/Try/Tempest), via [Solessfir's Android fork](https://github.com/Solessfir/Tempest) | Graphics API abstraction (Vulkan / DX12 / Metal), windowing, input, audio, 2D/UI, math, asset formats |
| **OpenGothic** (`engine/common`) | [Try/OpenGothic](https://github.com/Try/OpenGothic), via [Solessfir's Android fork](https://github.com/Solessfir/OpenGothic) | The game: world loading, Daedalus scripting, NPC AI, quests, dialogue, inventory, combat rules, save games, renderer |
| **Gothic II VR** (`engine/common/vr`, plus targeted edits elsewhere) | This repository | OpenXR session, tracked hands, physical combat, VR menus, stereo rendering, mobile GPU work, Android packaging and installer tooling |

The baseline revisions are pinned in `config/source-lock.json` — the repo is a
**reviewed source snapshot**, not a live fork with submodules. Every shipped
file is hashed in `SOURCE-MANIFEST.json` / `SOURCE-SHA256.txt`, and
`AUDIT-SOURCE-KIT.bat` re-verifies them. No retail Gothic data, APK, or signing
key lives in the tree.

Rough size (C++ only, `engine/common`, ~87.5k lines):

```
graphics/   22.1k    world/      15.5k    game/       15.9k
ui/          6.1k    vr/          5.8k    utils/       3.8k
dmusic/      3.7k    physics/     2.7k    bink/        2.5k
```

Plus 193 GLSL shader sources under `engine/shader/`.

---

## 2. OpenGothic — the engine underneath

### 2.1 Goal and completeness

OpenGothic is a **clean-room reimplementation of the Gothic II: Night of the
Raven client**. It ships no assets: you must own and supply the original game.
Upstream describes the original game as "completely replicated" — both the main
campaign and the addon are finishable. Gothic 1 is *unofficially* supported
(`VersionInfo::game` carries 1 or 2 and gates behaviour like dialogue gesture
counts and `ZS_` state loops), but G2 NotR is the target that receives attention.

Upstream's open-issue tracker shows where the seams still are — the recurring
themes are lighting calibration (torches and fire blowing out highlights),
collision and fall-damage divergence from the original, water surface polish,
animation transitions, and platform-specific hangs. These are *fidelity* gaps,
not structural ones; the game systems themselves are in place.

### 2.2 Tech stack

- **C++20**, CMake >= 3.16, one static library (`OpenGothic`) plus thin
  executables (`Gothic2Notr`, and a `Spacer` world editor on desktop).
- **Tempest** as the entire platform layer. This matters: OpenGothic does not
  use SDL/GLFW/Qt. Tempest is the author's own engine providing
  `Device`/`CommandBuffer`/`RenderPipeline` over **Vulkan, DirectX 12 and
  Metal**, with SPIR-V reflection and cross-compilation (`libspirv`,
  SPIRV-Cross), its own 2D vector renderer, fonts, and **OpenAL Soft** for
  spatial audio.
- **ZenKit** (GothicKit) parses every Gothic file format — `.ZEN` worlds, `.VDF`
  archives, `.MSH`/`.MRM`/`.MDL` meshes, `.MAN` animations, textures — and
  hosts the **Daedalus script VM** (`zenkit::DaedalusVm`).
- **Bullet 2** (via `lib/bullet3`) for collision and dynamics.
- **dmusic** + **TinySoundFont** for DirectMusic segment playback and SoundFont
  synthesis — the original game's music system reimplemented, not stubbed.
- **miniz** (archives), **RapidJSON** (config), **edd-dbg** (Windows crash
  backtraces).

Shaders are GLSL compiled to SPIR-V at build time by `glslangValidator` and
embedded into a generated `shader.cpp` — no runtime shader files on disk.

### 2.3 Architecture map

```
Gothic (singleton)          global state, ini/settings, VDF mounts, load/save orchestration
 +- GameSession             one loaded game: script VM + worlds + time
     +- GameScript          Daedalus bindings: the Gothic externals, dialogue, quests, AI
     +- World               one .ZEN: waynet, VOB tree, triggers
     |   +- WorldObjects    Npc / Item / Interactive / StaticObj / Sound / PfxEmitter / VobLight
     |   +- DynamicWorld    Bullet collision, ray and character queries
     |   +- WorldView       render-side mirror of the world
     +- WorldStateStorage   serialized inactive worlds (travel between Khorinis/Jharkendar/...)
```

Notable engine-side subsystems worth knowing about:

- **`game/definitions/`** — parsers for the data-driven definition files the
  original engine used: camera modes, fight AI, music themes, particle FX,
  sound FX, spells, SVM (speech) tables, visual FX.
- **`game/movealgo.cpp`, `fightalgo.cpp`, `damagecalculator.cpp`** — the actual
  Gothic movement/combat/damage rules. The VR layer routes physical hits
  through `DamageCalculator`, which is why armor, resistances and NPC reactions
  behave natively.
- **`game/serialize.cpp` + `savegameheader.cpp`** — versioned save format.
- **`world/aiqueue.cpp`, `aistate.cpp`, `perceptionmsg.h`** — the AI state
  machine and perception system that Daedalus scripts drive.
- **`ui/`** — the original menus (`GameMenu` parses `MENU.DAT`), inventory,
  dialogue, document/book rendering, chapter screens, console.
- **`bink/`** — a from-scratch Bink video decoder for the cutscene `.BIK` files.
- **`marvin.cpp`** — the original developer console (`zrmode wire`,
  `ztoggle vobbox`, `insert`, `goto waypoint`, ...) reimplemented
  command-for-command; genuinely useful for debugging.

### 2.4 Rendering

This is the most modern part of OpenGothic and, frankly, a research-grade
renderer for a 2002 game. `Renderer::draw` orchestrates (desktop path):

- **GPU-driven visibility** — `drawclusters` / `drawbuckets` / `drawcommands`
  build cluster lists; HiZ occlusion culling; optional **mesh shaders**
  (`materials/*.mesh`/`.task`) and **bindless** descriptor paths
  (`indexed_bindless.comp`).
- **Shadows**, three routes: classic cascades, **Virtual Shadow Maps**
  (`shader/virtual_shadow/`, page allocation/clumping/trimming compute passes),
  and **RTSM** — ray-traced soft shadows with its own light culling, meshlet
  culling and software rasterizer (`shader/rtsm/`, 29 shaders).
- **Global illumination** — surfel-based GI (`lighting/surfels/`) with binning
  and tracing passes; plus a path-tracing reference mode.
- **Software ray tracing** (`shader/swrt/`) as a fallback where hardware ray
  query is absent, over a custom BVH.
- **Sky/atmosphere** — Bruneton-style transmittance / multi-scattering / view
  LUTs, volumetric clouds, and **epipolar** volumetric fog sampling.
- **Software rendering path** (`shader/software_rendering/`) — a compute-based
  tiled/immediate rasterizer with a visibility buffer, for hardware that cannot
  do the normal path.
- SSAO, screen-space reflections, underwater, CMAA2 anti-aliasing, upscaling,
  HDR output, ACES-style tonemapping with a Purkinje shift for night vision.

Feature selection is capability-gated in `Gothic::Options` (`doRayQuery`,
`doMeshShading`, `doBindless`, `doVirtualShadow`, `doSoftwareShadow`,
`doSoftwareRT`, `doGi`, ...).

### 2.5 Extensibility and mod compatibility

This is the sharpest constraint to understand.

**Works:** content mods that ship standard Daedalus scripts, world files,
meshes, textures and audio. OpenGothic reads compiled `.DAT` script files, so
most script-only modifications load.

**Does not work:** anything that reaches into the original 32-bit Windows
binary — **Union** plugins and Windows DLL hooks (architecturally impossible on
an ARM64 Android target), the DX11 renderer, AST SDK, Ninja.

**Partially emulated — the interesting bit:** `game/compatibility/` contains a
genuine attempt at **Ikarus/LeGo support**:

- `mem32.cpp` — a simulated 32-bit address space with typed regions that shadow
  real engine objects (`zCParser`, `zCPar_Symbol`, variable tables).
- `cpu32.cpp` — an **x86 interpreter** with `stdcall` / `thiscall` / `cdecl`
  calling conventions, so script-injected machine code can be executed.
- `directmemory.cpp` — the Ikarus externals themselves (`MEM_ReadInt`,
  `MEM_WriteInt`, `MEM_ReplaceFunc`, and naked-call implementations of
  `MEM_Loop` / `repeat` / `while`), with symbol lookup by simulated address and
  loop-trap detection.

So the upstream README's blanket "Ikarus/LeGo unsupported" understates the
current state: there is a working emulation layer, activated on demand
(`DirectMemory::isRequired`) when a script's symbols indicate it. Treat it as
best-effort, not a guarantee.

---

## 3. The VR layer

### 3.1 Shape of the integration

Everything VR is compiled behind `GOTHIC2VR_OPENXR`, defined only for the
Android target (`engine/CMakeLists.txt`). Desktop builds are unaffected.

`engine/common/vr/` is ~5.8k lines across 25 files. The style is deliberate:
**almost everything is a header-only, engine-independent value type** —
`vrcombatmath.h`, `vrbowmath.h`, `vrinteractionmath.h`, `vrroomscale.h`,
`vrstereohiz.h`, `vrshadowcache.h`, `vrskyrate.h`, `vrfoveation.h`,
`lightroutecontroller.h`, `vrbakedlight.h`, `uxrh.h`. Only three translation
units are "impure": `questxr.cpp` (OpenXR), `vrgameplay.cpp` (world
interaction), `vrwindow.cpp` (the frame loop). That split is what makes the
host test suites possible (see 3.7).

Outside `vr/`, the touch on OpenGothic is surgical — 18 files carry VR code:
`mainwindow`, `camera`, `gothic`, `resources`, `gamepad`, `marvin`,
`commandline`, `gamemusic`, `ui/gamemenu`, `world/objects/npc`,
`physics/dynamicworld`, and the renderer cluster (`renderer`, `worldview`,
`drawcommands`, `sceneglobals`, `landscape`, `packedmesh`).

### 3.2 OpenXR session — `questxr.cpp` (955 lines)

A `QuestXr` singleton owning instance/system/session/spaces. Concretely:

- **Vulkan interop**, not a separate context: it hooks
  `xrCreateVulkanInstanceKHR` / `GetVulkanGraphicsDevice2KHR` /
  `CreateVulkanDeviceKHR` as callbacks so Tempest's `Device` is created *by*
  the OpenXR runtime and shares the queue. Swapchain images are wrapped as
  Tempest `Attachment`s.
- **Three swapchain outputs**: eye 0, eye 1, and a HUD/overlay image composited
  as a quad layer at a configurable distance (`hudDistance`, default 2 m) with
  a sub-rectangle optimisation (`setHudRect`) so only the dirty region is
  cleared and copied.
- **Two submission routes**: `acquireRenderTarget` renders *directly* into the
  XR image ("direct output", the fast path), or `copyEye` blits from an
  intermediate attachment. Fences are drained per-output (`drainCopy(eye)`)
  rather than globally, which is what lets the early-left-eye pipelining work.
- **Actions**: grip/trigger/stick/buttons/haptics per hand, grip *and* aim
  poses, plus `XR_EXT_performance_settings` CPU/GPU level requests and display
  refresh-rate query. Visibility masks are queried (with retry) for peripheral
  culling.
- `headView` / `eyeView` / `projection` convert tracking space into Gothic's
  centimetre world space via `unitsPerMeter`, honouring the user's world-scale
  setting and a recenterable reference pose.
- `AndroidManifest.xml` declares the OpenXR immersive-HMD category, required
  head tracking, Vulkan 1.1, and `com.oculus.trade_cpu_for_gpu_amount=1`.

### 3.3 Frame loop — `vrwindow.cpp`

`MainWindow::renderVr()` replaces the desktop render path. The sequence, per
stereo frame:

1. `xr.beginFrame()` (blocks in the runtime's frame pacing).
2. Drain the *previous* frame's fences — but selectively: with **early left
   eye** enabled, only the previous left eye's pinned command slot and XR image
   are drained, so the next left eye starts recording while the previous right
   eye and HUD are still executing on the GPU.
3. Game tick / simulation, room-scale reconciliation, VR gameplay update.
4. Record eye 0, then eye 1 (with `secondEye=true`, which unlocks the stereo
   reuse optimisations below).
5. Record the HUD layer into its own command buffer and fence.
6. `xr.endFrame()` submits the projection layer(s) plus the quad layer.

Every phase is timestamped into `Vr::Profiler` (CPU ms per phase, GPU ms from
command-buffer timestamps, GPU clock MHz), surfaced in-headset as a panel, a
compact line, or CSV. Frames over 250 ms log a per-phase breakdown.

### 3.4 Stereo and mobile rendering work

This is the densest engineering in the repo. The Quest 3 budget forced a set of
optimisations that exploit the fact that **two eyes are nearly the same image**:

- **`vrstereohiz.h` — stereo HiZ reprojection.** Instead of running an occluder
  seed pass for the second eye, its HiZ is derived from the first eye's final
  depth. The derivation is exact: the two eyes share a rotation and differ by a
  translation, so a first-eye pixel maps to `u_r = u_l + c + k/z`. Because that
  shift is monotone in depth, each source tile in the window has a maximum
  depth at which it can contribute, so the seed is
  `max over i of min(hiZLeft[T+i], depth(zHi(i)))` — conservative but far
  tighter than a plain maximum. It falls back to plain-max-with-margin when the
  model does not hold.
- **`vrshadowcache.h` — cached sun shadows.** Desktop cascades are head-centred
  *and head-rotated*, so they invalidate on every head turn — useless in VR.
  Here both maps are world-aligned orthographic projections with no rotation
  term: map 0 holds animated casters (rebuilt each frame, cheap), map 1 holds
  the static world, built across N frames into a back buffer and swapped in.
  Centres are snapped to the light-space texel grid so rebuilt maps do not swim
  against the previous one. Shading samples both and keeps the darker result.
- **`vrskyrate.h` — reduced-rate atmosphere.** The 128x64 view LUT, the 512x256
  cloud LUT and the irradiance pass are interleaved across frames on an
  even/odd phase, with per-input epsilons (sun direction, camera height, sun
  intensity, night factor) forcing a full refresh on teleports, time skips and
  weather changes. The second eye never draws LUTs and inherits the first eye's
  exposure.
- **`lightroutecontroller.h` — measured-cost light routing.** Two local-light
  strategies exist (masked depth-slab fullscreen pass vs. plain light volumes).
  Rather than a cost model, the controller *measures*: it runs the current
  route, probes the other every ~90 frames, feeds back real GPU timestamps, and
  switches when a probe beats the smoothed cost by a margin. A camera-side
  prior (projected light coverage) only seeds the initial route and can request
  an early probe, backing off if it keeps disagreeing with the measurement.
- **`vrfoveation.h` — fixed foveation via `VK_EXT_fragment_density_map`.**
  Notable for being *empirically corrected*: on the Adreno 740 the driver
  applies density per tiler bin — a full-width horizontal strip — and renders a
  bin at the finest density found inside it, so a conventional radial FFR map
  only ever reduced the bottom strip. The shipped profiles are therefore
  horizontal bands in 196 px steps from the top and bottom edges. Still off by
  default.
- **`vrbakedlight.h` — recovering 2002's baked shadows.** Gothic world meshes
  carry Spacer-compiled per-vertex light. Fitting `ambient + sun * max(0, n.d)`
  by least squares (with a coarse search over `d`) and dividing out the residual
  yields a per-vertex *sun visibility* that still encodes the compiled cast
  shadows (forest floors, under roofs and cliffs). It rides free in the
  landscape vertex-colour alpha and the GBuffer hint bits, and multiplies the
  direct sun term. This is what makes the default "2002 static" lighting mode
  look right without a dynamic shadow map.
- **Render scale as a sub-rectangle.** Below 1.0, an eye tonemaps into the
  top-left rect of its target and the projection layer shows only that
  rectangle — no upscale pass at all. The fog composite is folded into
  tonemapping when conditions allow (`vrFogFold`), saving a full-screen
  load/store.
- **Shader precompilation.** The 0.1.1 headline fix: world material pipelines
  are warmed on background threads and cached to disk between sessions,
  converting multi-second first-sight stalls into an object appearing a few
  frames late.

Every one of these is exposed as a toggle in the in-headset Performance /
Render tests menu, and each has a `Gothic.ini [ENGINE]` kill switch
(`vrStereoHiZOff`, `vrSkyRateOff`, `vrFogFoldOff`, `vrScaleRectOff`,
`vrHudRectCopyOff`, `vrProfilerExtrasOff`).

### 3.5 Interaction and physical combat — `vrgameplay.cpp` (1022 lines)

`Vr::Gameplay::update()` is the per-frame bridge between tracked poses and the
Gothic world. It owns:

- **Hands** — grip pose, aim pose, derived palm pose, squeeze/trigger analog
  values, and a held-item record per hand (`Held`: item id, inventory slot,
  holster, swing history, strike window, auto-arrow state).
- **Melee** (`vrcombatmath.h`) — a `Swing` tracks the blade's base and tip
  across frames; contact is a segment-vs-body test, and a strike window
  prevents multi-hits. Damage goes through OpenGothic's own `DamageCalculator`,
  so armor, resistances, weapon requirements and NPC reactions are the original
  rules. Two-handed weapons require both grips; parrying is a blade placed into
  an incoming attack.
- **Archery** (`vrbowmath.h`) — the bow hand alone controls aim; the string hand
  contributes only axial travel. `bowPower(draw) = clamp(draw/0.60)`, speed
  scale `0.35 + 0.65 * power`. `BowGesture` is a small state machine
  (Nocked -> Fired / Cancelled) with a minimum 8 cm pull and a 95 % overdraw
  cancel. `arrowPath` deliberately reproduces Gothic's gravity integration
  *including its one-step downward term*, so VR arrows land where engine arrows
  land. Bow string tips are derived from calibrated model dimensions, and the
  original string geometry is stripped from supported meshes at runtime.
- **Focus and targeting** (`vrcombatmath.h`) — gaze cones rather than rays:
  15 degrees to acquire an NPC and 22 to retain, scored against the NPC's *body
  axis* (shins to head) with head/chest line-of-sight probes. This is the 0.1.1
  fix that removed "aim at the waist to talk".
- **Holsters** — four slots (right belt, chest, back-left, back-right) with
  per-slot position, grab radius, assigned item, and auto-assignment on pickup.
  Hand-to-hand transfers, throwing, catching, and a ~1.8 s auto-return to
  inventory for uncaught weapons.
- **Calibration** — per-item and per-family (bow / crossbow / melee default)
  offset, rotation, grip point, scale, and bow string height/centre/side/depth.
  Persisted to `VR.ini` as `ItemCal_<INSTANCE>` lines with strict key and range
  validation on load.
- **Cheats/debug** — spawn NPC, give item, set time, set weather, god mode,
  heal. These queue as `Action`s and run only after a complete frame, because
  they can mutate the world mid-pipeline.

### 3.6 Hands, HUD and menus

- **`uxrh.h` / `vrhandrenderer.cpp`** — hand meshes come from **UltimateXR**
  (MIT) in a tiny custom binary format `UXRH`: four position/normal morph
  targets per vertex plus UV, blended by `(grip, trigger)` into four weights
  (relaxed / gripped / pointing / both). Strictly validated on load. Rendered by
  a dedicated pipeline (`shader/vr_hands.*`, `shader/vr_pickup.*`) *inside* the
  tonemapping render pass with its own cleared, discarded depth attachment — so
  hands and held items cost no extra load/store of the eye image — sampling
  scene depth in the fragment shader for occlusion. Vertex buffers are
  double-buffered by frame slot for the early-left-eye path.
- **`vrcontrols.h`** — the whole VR settings system: a flat `Row` enum of ~170
  entries organised into 21 `Page`s (Main, Locomotion, HUD, Performance, Render
  tests, Holsters, Cheats, Controls, Calibration and its sub-pages, Power, ...),
  plus `Settings` with `sanitize()` clamping every field and a hand-rolled
  `VR.ini` reader/writer carrying migration flags (`holsterLayoutVersion`,
  `mirrorVersion`, `buttonVersion`) so older profiles upgrade rather than reset.
- **Comfort** — snap (default 30 degrees) / smooth / physical turning, run speed
  0.75-2.0x, world scale 0.5-2.0x, adjustable HUD distance and X/Y offset.
- **Room scale** (`vrroomscale.h`) — a small, elegant piece: the head's
  horizontal tracking delta is offered to the game as movement, and *only
  collision-accepted* movement is subtracted from the eye pose. Rejected
  movement accumulates as a bounded correction (capped at 8 cm of give), so
  walking into a wall in your room stops the avatar without the camera
  detaching from your body.
- **Original UI reuse** — rather than reimplementing Gothic's inventory and
  status screens, "Open game interface" and "Character stats" render the
  original menus into a theater panel.

### 3.7 Build, test and distribution pipeline

This is unusually complete for a hobby VR port.

- **`BUILD-APK.bat` -> `tools/build-android.ps1`** bootstraps a *fully portable*
  toolchain with no global PATH changes: Python 3.12.10, Temurin JDK 21,
  Android SDK API 35 / Build Tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1,
  Gradle 8.9 / AGP 8.7.3, OpenXR loader 1.1.43, Vulkan headers. Every download
  is hash-pinned in `config/*.lock.json` and verified before extraction.
  `-Offline` works after one successful online build.
- **Release build** uses ThinLTO with an explicit workaround for CMake 3.22's
  IPO check selecting the removed gold linker on the pinned NDK, and keeps debug
  symbols in build outputs while Gradle strips the shipped `.so`.
- **Signing** — self-builds generate a local dev keystore; maintainer keys come
  from environment variables only. The scripts verify the APK signature and
  explicitly refuse to uninstall an app to work around a key mismatch.
- **Installer** (`tools/install-release.ps1`) — downloads checksum-verified ADB,
  packages the user's *own* purchased game into a private ZIP (saves excluded),
  checks headset free space, pushes, verifies, installs, and hands off to an
  in-headset `SetupActivity` for the file-picker import. `-DryRun` validates
  without touching the device. Reports land in `build/android/deployments/`.
- **Host tests** — `tools/test-vr-source.py` compiles **15 C++20 suites against
  the shipped headers** on the host: `xrmath`, `vrcontrols`, `vrinteraction`,
  `sky`, `npc-attachments`, `calibration`, `combat`, `focus`,
  `release-defaults`, and bow/grip/melee/HUD/gameplay regression suites driven
  by Python fixture generators in `tests/`. This is the payoff of the
  header-only math design: interaction, calibration and rendering-schedule logic
  is testable without a headset or a GPU. `tests/installers.ps1` runs eight
  deployment scenarios against a simulated ADB boundary.
- **Source integrity** — `tools/audit-source-kit.py` verifies the recorded
  hashes and, in archive mode, *rejects* build outputs, game payloads and
  signing material.

### 3.8 What the VR layer deliberately does not do

- Quest 3 with Touch controllers only; other Quest models and other OpenXR
  runtimes are untested. **No controller-free hand tracking.**
- Physical crouching lowers the camera but does not resize the collision
  capsule.
- Some original spells, quests and interactions are not yet wired to VR input.
- VR menus are English-only; game language comes from the user's own data.
- No fixed frame rate is promised; foveation ships off.

---

## 4. How the two layers relate — the honest summary

The VR work is **additive and well-isolated**, not a rewrite:

- Gameplay rules stay OpenGothic's. Physical hits enter through
  `DamageCalculator`; holsters only draw items actually in inventory; arrows use
  the engine's ballistics; dialogue, quests, AI and saves are untouched.
- The renderer is *extended*, not replaced. Every VR path is a flag on the
  existing `Renderer`, with a `Gothic.ini` escape hatch back to the desktop
  behaviour, and the mobile optimisations sit alongside the desktop
  VSM/RTSM/GI paths rather than deleting them.
- The VR-specific logic that could be pure math *is* pure math, in headers,
  under host tests — which is why a 5.8k-line subsystem can carry this much
  behaviour without becoming unmaintainable.

The corresponding constraint: this is an **alpha**. It inherits every open
OpenGothic fidelity issue (lighting brightness, collision edge cases, water,
animation transitions) and adds its own — VR interaction coverage of the
original game's verbs is incomplete, and performance is scene- and
settings-dependent. Structurally sound, functionally unfinished.

---

## 5. Where to start reading

| Question | File |
| --- | --- |
| How does a frame happen in VR? | `engine/common/vr/vrwindow.cpp` -> `MainWindow::renderVr` |
| How does OpenXR bind to Tempest/Vulkan? | `engine/common/vr/questxr.cpp` |
| How does a swing become damage? | `engine/common/vr/vrgameplay.cpp` + `vrcombatmath.h` |
| How does the renderer decide what to draw? | `engine/common/graphics/renderer.cpp` -> `Renderer::draw` |
| How do Daedalus scripts reach the world? | `engine/common/game/gamescript.cpp` |
| How is Ikarus emulated? | `engine/common/game/compatibility/` |
| What can I tune without rebuilding? | `engine/common/vr/vrcontrols.h` (`Settings`) and `Gothic.ini [ENGINE]` |
