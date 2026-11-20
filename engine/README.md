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
│   ├── map.hpp
│   ├── map_loader.hpp
│   ├── mesh.hpp
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
- `Mesh` is included. `Model` remains in `src/core/` because it requires Assimp,
  which is not part of the current build. It is optional and can move into the
  engine once Assimp is a declared dependency.
- The earlier `src/engine/` prototype has been removed. The production engine
  is this directory.

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
| Optional model loader | `src/core/model.*` (requires Assimp) |

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

