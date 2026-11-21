/**
 * @file engine_smoke_test.cpp
 * @brief Verifies that the extracted engine works without any game code.
 *
 * The test includes only engine headers. It deliberately avoids OpenGL calls
 * (no window/context is needed); the game build and its scripted UI test cover
 * the renderer in a real context.
 */

#include "engine/engine.hpp"

#include <cmath>
#include <cstdio>
#include <future>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void testCamera() {
    Camera camera;
    CHECK(std::isfinite(camera.Position.x));
    CHECK(std::isfinite(camera.Yaw));

    const float oldYaw = camera.Yaw;
    camera.ProcessMouseMovement(12.0f, 3.0f);
    CHECK(camera.Yaw != oldYaw);

    camera.ProcessKeyboard(0, 0.016f);
    const glm::mat4 view = camera.GetViewMatrix();
    CHECK(std::isfinite(view[0][0]));
    CHECK(std::isfinite(view[3][3]));
}

void testThreadPool() {
    ThreadPool pool(2);
    CHECK(pool.threadCount() >= 1);

    std::vector<std::future<int>> futures;
    futures.reserve(8);
    for (int i = 0; i < 8; ++i) {
        futures.push_back(pool.submit([i] { return i; }));
    }

    int sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }
    CHECK(sum == 28);
}

void testMap() {
    Map map("");
    CHECK(!map.getBlocks().empty());

    const glm::vec3 position(1000.0f, 1000.0f, 1000.0f);
    map.addBlock(MapBlock(position, glm::vec3(1.0f), 0, 0, true));
    CHECK(map.findBlockAt(position) >= 0);
    CHECK(map.checkCollision(position, 0.1f));

    MapBlock rotated(position, glm::vec3(2.0f, 1.0f, 1.0f), 0, 0, true,
                     glm::vec3(1.0f), 45.0f, 0.0f);
    CHECK(!rotated.isAxisAligned());
    glm::vec3 x, y, z;
    mapBlockBasis(rotated, x, y, z);
    CHECK(std::fabs(glm::length(x) - 1.0f) < 1e-4f);
    CHECK(std::fabs(glm::length(y) - 1.0f) < 1e-4f);
    CHECK(std::fabs(glm::length(z) - 1.0f) < 1e-4f);
    CHECK(std::fabs(glm::dot(x, y)) < 1e-4f);

    // Oriented ray cast: hits the real top face of a pitched block, not the
    // axis-aligned box that contains it (the old collision missed this and
    // shots stopped mid-air around rotated geometry).
    MapBlock pitched(glm::vec3(0.0f), glm::vec3(2.0f, 0.2f, 2.0f), 0, 0, true,
                     glm::vec3(1.0f), 0.0f, 45.0f);
    const MapBlockRayHit topHit = intersectMapBlockRay(
        pitched, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
    CHECK(topHit.hit);
    CHECK(topHit.distance > 4.8f && topHit.distance < 4.9f);
    CHECK(topHit.normal.y > 0.7f);
    CHECK(std::fabs(topHit.normal.z - 0.7071f) < 1.0e-3f);

    // A block behind the ray must not report an intersection at the origin.
    const MapBlockRayHit behindHit = intersectMapBlockRay(
        pitched, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 10.0f);
    CHECK(!behindHit.hit);

    // Ground queries use that same oriented face, so a ramp is walkable at its
    // real surface height (not the enclosing AABB top).
    Map probe;
    probe.clear();
    probe.addBlock(pitched);
    const Map::GroundHit ground = probe.groundSurfaceAt(0.0f, 0.0f, 5.0f, 10.0f);
    CHECK(ground.hit);
    CHECK(std::fabs(ground.height - 0.1414f) < 1.0e-3f);
    CHECK(std::fabs(ground.normal.z - 0.7071f) < 1.0e-3f);

    CHECK(map.deleteBlockAt(position));
    CHECK(map.findBlockAt(position) < 0);
}

void testMapEditing() {
    Map map("");
    map.clear();
    CHECK(map.getBlocks().empty());

    // Bulk replacement (undo/redo / new-map path) keeps every collection in
    // sync and refreshes the collision index.
    std::vector<MapBlock> blocks{
        MapBlock(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f), 1, 1, true),
        MapBlock(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(2.0f), 0, 1, true),
    };
    map.setBlocks(blocks);
    CHECK(map.getBlocks().size() == 2);
    CHECK(map.findBlockAt(glm::vec3(0.0f)) == 0);
    CHECK(map.checkCollision(glm::vec3(4.0f, 0.0f, 0.0f), 0.1f));

    map.setSpawnPoints({SpawnPoint(glm::vec3(1.0f, 0.0f, 1.0f))});
    map.setTeamSpawns({TeamSpawn(glm::vec3(2.0f, 0.0f, 2.0f), 1)});
    map.setBombSites({BombSiteData(glm::vec3(3.0f, 0.0f, 3.0f), 'B')});
    CHECK(map.getSpawnPoints().size() == 1);
    CHECK(map.getTeamSpawns().size() == 1);
    CHECK(map.getBombSites().size() == 1);

    // clear() must drop authored bomb sites too, or a new map would inherit
    // them from the previous parse.
    map.clear();
    CHECK(map.getBlocks().empty());
    CHECK(map.getSpawnPoints().empty());
    CHECK(map.getTeamSpawns().empty());
    CHECK(map.getBombSites().empty());
}

void testFontAtlas() {
    CHECK(font_atlas::kGlyphCount == 98);
    CHECK(font_atlas::glyphPattern('A') != 0);
    CHECK(font_atlas::glyphPattern(' ') == 0);
}
void testAudioLifecycle() {
    AudioManager audio;
    audio.setMasterVolume(0.25f);
    audio.setSFXVolume(0.5f);
    audio.setMusicVolume(0.75f);
    CHECK(std::fabs(audio.getMasterVolume() - 0.25f) < 1e-6f);
    CHECK(std::fabs(audio.getSFXVolume() - 0.5f) < 1e-6f);
    CHECK(std::fabs(audio.getMusicVolume() - 0.75f) < 1e-6f);
    audio.shutdown();
}

} // namespace

int main() {
    std::printf("cube engine: %s\n", cubeEngineName());

    testCamera();
    testThreadPool();
    testMap();
    testMapEditing();
    testFontAtlas();
    testAudioLifecycle();

    if (failures == 0) {
        std::printf("ENGINE SMOKE TEST: ALL OK\n");
        return 0;
    }

    std::printf("ENGINE SMOKE TEST: %d FAILURE(S)\n", failures);
    return 1;
}
