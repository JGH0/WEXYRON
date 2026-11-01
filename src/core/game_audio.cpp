/**
 * @file game_audio.cpp
 * @brief Audio glue: loading sounds, listener updates, footsteps, music
 *
 * Gameplay code only ever calls playSoundEffect() / playUiSound(); the mixer
 * itself lives in src/engine/audio_manager.cpp. This file decides which file
 * plays when, how loud it is and where it is in the world.
 *
 * Sound files live in assets/audio/effects/<name>.wav and
 * assets/audio/music/<name>.wav (see tools/generate_sounds.py).
 */

#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

namespace {

/// Random values for per-shot pitch variation (sounds never repeat exactly).
float randomRange(float lo, float hi) {
    static std::mt19937 rng(0x5eedu);
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

/// Gain per sound (relative to its file level) so a busy firefight does not
/// drown out every other effect.
struct MixEntry { const char* name; float volume; };
const MixEntry kMix[] = {
    {"pistol_fire",       0.9f},
    {"shotgun_fire",      1.0f},
    {"dry_fire",          0.6f},
    {"reload",            0.7f},
    {"grenade_throw",     0.6f},
    {"flashbang_throw",   0.6f},
    {"explosion",         1.0f},
    {"flashbang_explode", 0.8f},
    {"bullet_impact",     0.45f},
    {"hit_flesh",         0.7f},
    {"player_damage",     0.8f},
    {"player_death",      0.9f},
    {"enemy_death",       0.8f},
    {"footstep_1",        0.32f},
    {"footstep_2",        0.32f},
    {"footstep_3",        0.32f},
    {"footstep_4",        0.32f},
    {"jump",              0.35f},
    {"land",              0.55f},
    {"round_start",       0.7f},
    {"round_end",         0.7f},
    {"bomb_plant",        0.7f},
    {"bomb_beep",         0.5f},
    {"bomb_defuse",       0.6f},
    {"bomb_explode",      1.0f},
    {"ui_hover",          0.35f},
    {"ui_select",         0.55f},
    {"ui_back",           0.5f},
    {"pickup",            0.6f},
    {"help",              0.6f},
};

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void Game::initAudio() {
    if (audio.isInitialized()) return;

    AudioManager::AudioConfig cfg;
    cfg.sampleRate = 44100;
    cfg.bufferSize = 512;
    cfg.maxSources = 48;
    cfg.masterVolume = gameSettings_.masterVolume / 100.0f;
    cfg.sfxVolume = gameSettings_.effectsVolume / 100.0f;
    cfg.musicVolume = gameSettings_.musicVolume / 100.0f;

    // Load sounds before opening the device: a missing sound folder should be
    // reported even when the machine has no sound card.
    const int count = audio.loadSoundDirectory("assets/audio/effects");
    applyAudioMix();
    audio.setDistanceModel(AudioManager::DistanceModel::Inverse, 1.5f, 80.0f);

    if (!audio.initialize(cfg)) {
        // No device: the game keeps running silently. A device that appears
        // later is picked up by AudioManager::update().
        return;
    }

    std::cout << "Audio: loaded " << count << " sound effect(s) from assets/audio/effects" << std::endl;
    if (count == 0) {
        std::cerr << "Audio: no sound files found. Run 'python3 tools/generate_sounds.py' "
                     "from the project root to create assets/audio/*." << std::endl;
    }
}

void Game::applyAudioMix() {
    for (const MixEntry& entry : kMix) {
        audio.setSoundVolume(entry.name, entry.volume);
    }
}

// ---------------------------------------------------------------------------
// Per-frame updates
// ---------------------------------------------------------------------------

void Game::updateAudio(float deltaTime) {
    audio.update(deltaTime);   // device retry + source housekeeping

    // The listener is the camera (ears at eye height, facing the view).
    audio.setListenerPosition(camera.Position);
    audio.setListenerOrientation(camera.Front, camera.Up);
    audio.setListenerVelocity(playerVelocity);

    updateUnderwater();
    updateMusicForState();
}

void Game::playSoundEffect(const std::string& name, const glm::vec3& position,
                           float volume, float pitch) {
    if (!audio.isInitialized()) return;
    audio.playAt(name, position, volume, pitch);
}

void Game::playUiSound(const std::string& name) {
    if (!audio.isInitialized()) return;
    audio.playUi(name);
}

void Game::playGunshot(const std::string& name, float volume) {
    if (!audio.isInitialized()) return;
    // 0.95..1.05 pitch (via std::rand, seeded by the C runtime): enough to
    // hear the difference, small enough to stay realistic.
    gunshotPitch = 0.95f + static_cast<float>(std::rand() % 100) / 1000.0f;
    audio.playAt(name, camera.Position, volume, gunshotPitch);
}

void Game::playDryFire() {
    // Trigger click: no ammunition. Throttled by the weapon cooldown upstream.
    static double lastDryFire = 0.0;
    const double now = glfwGetTime();
    if (now - lastDryFire < 0.15) return;   // holding the trigger: one click
    lastDryFire = now;
    playSoundEffect("dry_fire", camera.Position);
}

void Game::updateMusicForState() {
    // Menus get the calm track, a match the driving one, the editor none.
    std::string wanted;
    if (gameState == MENU || gameState == SETTINGS ||
        gameState == MAP_SELECTION || gameState == SERVER_CONNECT) {
        wanted = "menu";
    } else if (gameState == PLAYING || gameState == DEAD) {
        wanted = "game";
    }

    if (wanted == activeMusic_) return;
    activeMusic_ = wanted;
    if (wanted.empty()) audio.stopMusic();
    else audio.setMusic(wanted, 1.0f);
}

void Game::updateUnderwater() {
    // A camera below the water surface of an editor water block:
    // footsteps turn into splashes (slower/deeper) and nothing else changes.
    bool under = false;
    if (currentMap) {
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();
        for (const MapBlock& block : blocks) {
            if (!block.solid) continue;
            const glm::vec3 half = block.scale * 0.5f;
            const glm::vec3 d = camera.Position - block.position;
            if (std::abs(d.x) < half.x && std::abs(d.y) < half.y && std::abs(d.z) < half.z) {
                under = true;
                break;
            }
        }
    }
    underwater = under;
    footstepPitch = underwater ? 0.72f : 1.0f;
}

void Game::updateFootsteps(float deltaTime) {
    const bool onGround = !isJumping;
    const float horizontalSpeed = std::sqrt(playerVelocity.x * playerVelocity.x +
                                            playerVelocity.z * playerVelocity.z);

    // Landing: the longer the fall, the louder the thud.
    if (onGround && !wasOnGround) {
        const float strength = std::min(1.0f, 0.45f + fallTime * 0.5f);
        playSoundEffect("land", playerPos + glm::vec3(0.0f, -1.4f, 0.0f), strength,
                         underwater ? 0.8f : 1.0f);
        fallTime = 0.0f;
        footstepTimer = 0.0f;
    }
    if (!onGround) fallTime += deltaTime;

    // Walking: one step every ~0.4 s at full running speed.
    if (onGround && horizontalSpeed > 0.5f) {
        footstepTimer += deltaTime;
        const float interval = std::max(0.3f, 2.0f / std::max(horizontalSpeed, 1.0f));
        if (footstepTimer >= interval) {
            footstepTimer = 0.0f;
            char name[32];
            footstepIndex = (footstepIndex + 1) % 4;
            std::snprintf(name, sizeof(name), "footstep_%d", footstepIndex + 1);
            // A touch of random pitch so the four steps don't loop audibly.
            const float pitch = randomRange(0.92f, 1.08f) * footstepPitch;
            playSoundEffect(name, playerPos + glm::vec3(0.0f, -1.4f, 0.0f), 1.0f, pitch);
        }
    } else {
        footstepTimer = 0.0f;
    }

    wasOnGround = onGround;
}
