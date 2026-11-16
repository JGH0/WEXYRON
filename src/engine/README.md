# Engine prototype (legacy)

These files are an earlier, incomplete engine design kept for reference.

The production, game-agnostic engine is in [`../../engine/`](../../engine/) and
is built as the `cube_engine` library. It contains the rendering, map, camera,
audio and thread-pool code that the game actually uses.

This directory is **not** part of the CMake or Makefile build. Do not add new
engine code here; add it to `engine/` and keep the boundary check passing.
