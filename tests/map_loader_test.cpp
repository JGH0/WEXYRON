// MapLoader test: a background load must produce the same map as a blocking
// load, with every material texture decoded. Runs without any window or
// graphics context: the worker only parses files and decodes images.
//
// Must run from a directory with assets/ (build/bin or the project root).

#include "engine/map.hpp"
#include "engine/map_loader.hpp"
#include "engine/texture.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    const char* maps[] = {"simple.map", "arena.map", "large.map"};
    MapLoader loader;

    for (const char* mapFile : maps) {
        std::unique_ptr<Map> reference = MapLoader::loadBlocking(mapFile);
        CHECK(reference != nullptr);
        if (!reference) continue;

        loader.start(mapFile);
        CHECK(loader.isLoading());
        std::unique_ptr<Map> loaded;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!loaded && std::chrono::steady_clock::now() < deadline) {
            loaded = loader.pollFinished();
            if (!loaded) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(loaded != nullptr);
        if (!loaded) continue;
        CHECK(!loader.isLoading());

        CHECK(loaded->getBlocks().size() == reference->getBlocks().size());
        int textures = 0;
        for (int id = 0; id < 8; ++id) {
            Texture* a = loaded->getTexture(id);
            Texture* b = reference->getTexture(id);
            CHECK(a && a->valid() && a->width > 0 && a->nrChannels == 4);
            CHECK(b && a && a->width == b->width && a->height == b->height);
            if (a && a->valid()) ++textures;
        }
        std::printf("%-12s blocks=%zu textures=%d\n", mapFile, loaded->getBlocks().size(), textures);
    }

    // A second start() discards the first load's result.
    loader.start("arena.map");
    loader.start("simple.map");
    std::unique_ptr<Map> last;
    for (int i = 0; i < 4000 && !last; ++i) {
        last = loader.pollFinished();
        if (!last) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::unique_ptr<Map> simple = MapLoader::loadBlocking("simple.map");
    CHECK(last && simple && last->getBlocks().size() == simple->getBlocks().size());

    std::printf(failures ? "MAP LOADER TEST: %d FAILURES\n" : "MAP LOADER TEST: ALL OK\n", failures);
    return failures ? 1 : 0;
}
