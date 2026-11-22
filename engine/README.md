# WEXYRON Engine

The reusable, game-agnostic core extracted from **WEXYRON**.

The engine is a small C++17/OpenGL library. It knows about windows, shaders,
textures, geometry, cameras, block-map data, worker threads and audio. It does
**not** know about the game: no `Game`, `Player`, `Weapon`, `Enemy`, `Bot`,
HUD, menu or game settings live here.

## Layout

```
engine/
├── include/engine/          public headers
│   ├── engine.hpp           umbrella header
│   ├── audio_manager.hpp
│   ├── camera.hpp
│   ├── font_atlas.hpp
│   ├── gltf_model.hpp
│   ├── graphics_capabilities.hpp  OS/GPU support probe (renderer + frame gen)
│   ├── map.hpp
│   ├── map_loader.hpp
│   ├── render_backend.hpp   API-neutral renderer <-> backend interface
│   ├── render_window.hpp    window creation per graphics API
│   ├── renderer.hpp
│   ├── shader.hpp
│   ├── texture.hpp
│   └── thread_pool.hpp
├── src/                     implementation
└── tests/engine_smoke_test.cpp
```

The game keeps its application layer in `src/core/game*.cpp`, `src/systems/`,
`src/network/`, `src/ui/` and `src/game/`. Those files consume the engine.

## Build

CMake builds the library as the `cube_engine` target:

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
```

The game and the engine test are built from the same configuration:

```bash
./build-cmake/bin/game
./build-cmake/bin/engine_smoke_test
```

With the Makefile:

```bash
make                 # game + server, links build/lib/libcube_engine.a
make engine-test     # engine smoke test
./build/bin/engine_smoke_test
```

## Test

`engine_smoke_test` includes **only** engine headers and exercises the parts
that do not need a GL context:

- `Camera` math and look handling
- `ThreadPool` job execution
- `Map` block add/find/collision/delete
- `AudioManager` lifecycle without requiring a sound device

The game itself is verified separately with the existing scripted UI run:

```bash
CUBE_MENU_TEST="menu:main;dump" xvfb-run -a ./build/bin/game
```

## Design notes

- The API is intentionally in the global namespace for now. Moving it into a
  `cube` namespace is a later, mechanical refactor; it was not done in the same
  step because it would touch every game source file.
- `Map` still contains spawn-point and bomb-site data used by the game. They are
  plain data structures, not game logic. A future engine version can split
  them into a game-side metadata layer.
- glTF (`gltf_model.hpp`) is the only model format. The old Assimp-based
  `Model`/`Mesh` loader was never part of the build and has been removed.
- The earlier `src/engine/` prototype has been removed. The production engine
  is this directory.

## Render backends

`Renderer` (renderer.hpp) is an API-neutral frontend: it batches boxes, the UI
and glTF draws into a `FramePacket` and hands it to an `IRenderBackend`
(render_backend.hpp). Two backends draw the same image:

| Backend | Code | Notes |
|---|---|---|
| OpenGL 3.3 core | `src/backends/gl/` | Default. GLSL shaders in `assets/shaders/`. |
| Vulkan (NVRHI) | `src/backends/nvrhi/` | Needs Vulkan 1.2 with dynamic rendering and synchronization2. |

- **Choosing:** the in-game setting VIDEO > RENDERER (`render_api=opengl|vulkan`
  in settings.cfg) applies at the next start. `CUBE_RENDER_API=opengl|vulkan`
  overrides it. VIDEO > ACTIVE RENDERER shows what is actually running.
- **Fallback:** if Vulkan is selected but unusable (no driver, too old, a Mac
  without MoltenVK, or a build without the submodules), the game logs why and
  starts with OpenGL. Nothing links against the Vulkan loader; GLFW loads it.
  The RENDERER row is then greyed out (and the VIDEO page says why) so an
  unsupported choice cannot turn into a restart that changes nothing.
- **Building:** the Vulkan backend needs the `third_party/nvrhi` and
  `third_party/vulkan-headers` submodules (`git submodule update --init`). The
  Makefiles detect them and otherwise build OpenGL-only; a configuration stamp
  also recompiles the backend objects when the submodules appear later.
  CMake needs a re-run after initializing the submodules.
- **Shaders:** the Vulkan shaders are HLSL in `engine/shaders/`, compiled to
  SPIR-V headers (`src/backends/nvrhi/shaders/`, committed) by
  `python tools/compile_shaders.py` (needs `dxc` from the Vulkan SDK). Keep
  them in sync with the GLSL versions. `--check` reports stale headers.
- **macOS:** OpenGL works out of the box. Vulkan needs MoltenVK and the Vulkan
  loader (`brew install molten-vk vulkan-loader`); on Apple Silicon also set
  `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` so GLFW finds the loader.
- **Validation:** `CUBE_VULKAN_VALIDATION=1` enables the Khronos validation
  layer (Vulkan SDK) and NVRHI's validation; messages go to stderr.
- **Frame generation inputs (Vulkan):** the scene pass writes HUD-less colour,
  motion vectors (UV units, previous - current; previous camera, model
  matrices and bone palettes) and depth for the whole image (the viewmodel
  uses the depth range [0, 1e-4] instead of a depth clear). The HUD is its
  own premultiplied layer; a composite pass puts it on top. The game reports
  camera cuts (`Renderer::markCameraCut`) for map changes, respawns and
  teleports. `render_capture_test motion` checks the vectors by reprojection.
- **AMD FSR frame generation (Vulkan, Windows):** VIDEO > FRAME GENERATION >
  MODE = AMD FSR (or AUTO). Needs `python tools/fetch_sdks.py` (the SDK licence
  forbids committing its files; the build picks them up and copies the DLLs
  next to the game). FSR replaces the swapchain (`fsr_vulkan.cpp`), presents
  real and generated frames from its own queues and composes the HUD onto
  every frame. It generates one frame per rendered frame (2X).
  `render_capture_test framegen fsr` checks that about two frames are shown
  per rendered frame, across a resize and an off/on switch. Known: FSR SDK
  2.4 leaves 29 of its own Vulkan objects at device destruction (exit only),
  and 3 more each time FSR is switched off and on.
- **NVIDIA DLSS frame generation + Reflex (Vulkan, Windows):** MODE = NVIDIA
  DLSS, or AUTO (preferred over FSR where it runs: GeForce RTX 40 or newer).
  Also from `tools/fetch_sdks.py` (Streamline 2.14). On machines with an NVIDIA
  GPU, Vulkan is reached through Streamline's interposer
  (`streamline_vulkan.cpp`, signature checked) so it can add what DLSS-G needs
  to the device; other GPUs never load it. DLSS-G is loaded only while its
  swapchain exists (switching rebuilds the swapchain); it gets HUD-less colour,
  the HUD layer, depth, motion vectors and the camera every frame, pauses in
  full-screen menus and presents without vsync (a Streamline limitation on
  Vulkan). Reflex markers run every frame (`Renderer::beginSimulation`, called
  by the game before input). 3X/4X need an RTX 50; others are clamped to 2X.
  `render_capture_test framegen dlss` checks it like FSR. `CUBE_STREAMLINE=0`
  keeps Streamline out, `CUBE_STREAMLINE_LOG=1` prints its verbose log. Known:
  with `CUBE_VULKAN_VALIDATION=1`, Streamline's own queues, semaphores and
  swapchain images report validation messages (none name the game's objects
  except Streamline's swapchain image layout).
- **Parity check:** `render_capture_test capture <dir>` with each backend, then
  `render_capture_test compare <gl-dir> <vk-dir> 1.0`. Glyph edges can differ
  by one pixel (half-pixel rounding differs between the APIs); everything else
  matches. `render_capture_test bench` prints the average frame time.

## Extraction summary

Moved into `engine/`:

| Area | Files |
|---|---|
| Graphics | `renderer`, `shader`, `texture`, `mesh`, `font_atlas` |
| Scene / math | `camera` |
| World | `map`, `map_loader` |
| Utilities | `thread_pool` |
| Audio | `audio_manager` (moved from `src/engine/`) |

Stayed in the game:

| Area | Files |
|---|---|
| Application | `src/core/game*.cpp`, `game.hpp`, `main.cpp` |
| Game systems | `src/systems/*` |
| Multiplayer | `src/network/*`, `src/game/bot.*` |
| UI / HUD / menus | `src/ui/*`, `src/core/game_hud.cpp`, `src/core/game_menus.cpp` |
| Game settings | `src/core/settings.*` |
| Game entity glue | `src/core/humanoid.hpp`, `src/core/game_audio.cpp` |

Build-system changes:

- `cube_engine` static library in CMake and `build/lib/libcube_engine.a` in the
  Makefiles.
- `glad.c` and `third_party/miniaudio/miniaudio.c` now compile once into the
  library instead of into the game executable.
- `engine_boundary_check` fails the CMake build if an engine file includes
  game code.
- `engine_smoke_test` is registered with CTest and runs without a GL context.

## Verification on 16.11.2026

| Check | Result |
|---|---|
| CMake configure + build | pass |
| `ctest --test-dir build-cmake --output-on-failure` | 1/1 passed |
| Makefile build `make -j12` | pass |
| `make engine-test && ./build/bin/engine_smoke_test` | ALL OK |
| Game menu smoke: `CUBE_MENU_TEST="menu:main;dump" xvfb-run -a ./build/bin/game` | pass |
| Existing `tests/gameplay_test.cpp` (jump + grenade regression) | ALL OK |
| Dedicated server starts | pass |

