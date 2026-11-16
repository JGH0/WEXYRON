/**
 * @file engine.hpp
 * @brief Umbrella header for the reusable Cube Engine library.
 *
 * The engine contains only game-agnostic code:
 *   - rendering: Renderer, Shader, Texture, Mesh
 *   - scene/math: Camera
 *   - world: Map, MapLoader
 *   - utilities: ThreadPool, FontAtlas
 *   - audio: AudioManager
 *
 * It deliberately does not include any game code (Game, Player, Weapon, Bot,
 * menus, HUD, settings). A game links `cube_engine` and supplies its own
 * application layer.
 */

#ifndef CUBE_ENGINE_ENGINE_HPP
#define CUBE_ENGINE_ENGINE_HPP

#include "engine/audio_manager.hpp"
#include "engine/camera.hpp"
#include "engine/font_atlas.hpp"
#include "engine/map.hpp"
#include "engine/map_loader.hpp"
#include "engine/mesh.hpp"
#include "engine/renderer.hpp"
#include "engine/shader.hpp"
#include "engine/texture.hpp"
#include "engine/thread_pool.hpp"

/// Human-readable engine name, useful for logs and demos.
inline const char* cubeEngineName() { return "Cube Engine"; }

#endif // CUBE_ENGINE_ENGINE_HPP
