/**
 * @file map_loader.cpp
 * @brief Background map loading (see map_loader.hpp)
 */

#include "engine/map_loader.hpp"
#include "engine/map.hpp"

#include <iostream>

namespace {
/// The default texture set every map uses.
const char* const kDefaultTextures[] = {
    "grass.png", "stone.png", "brick.png", "metal.png",
    "wood.png",  "sand.png",  "ice.png",   "dirt.png",
};
constexpr int kDefaultTextureCount = 8;
} // namespace

MapLoader::~MapLoader() {
    cancel();
}

void MapLoader::ensureTextures(Map& map) {
    for (int i = 0; i < kDefaultTextureCount; ++i) {
        std::cout << "  loading " << kDefaultTextures[i] << "..." << std::endl;
        map.loadTexture(kDefaultTextures[i], i);
    }
}

std::unique_ptr<Map> MapLoader::loadBlocking(const std::string& mapFile) {
    // Parsing and texture decoding are CPU work; any thread will do.
    auto map = std::make_unique<Map>("assets/maps/" + mapFile);
    ensureTextures(*map);
    return map;
}

void MapLoader::start(const std::string& mapFile) {
    cancel();

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        mapFile_ = mapFile;
        hasResult_.store(false, std::memory_order_release);
    }
    loading_.store(true, std::memory_order_release);

    // Everything here is CPU work (file parse + image decode): no graphics
    // context is needed on the worker. The renderer uploads the decoded
    // pixels on the main thread when the map is swapped in.
    future_ = std::async(std::launch::async, [this, mapFile]() {
        auto map = std::make_unique<Map>("assets/maps/" + mapFile);
        ensureTextures(*map);

        std::lock_guard<std::mutex> lock(resultMutex_);
        pending_ = std::move(map);
        hasResult_.store(true, std::memory_order_release);
        return true;
    });
}

void MapLoader::cancel() {
    if (future_.valid()) {
        future_.wait();  // let the worker finish its write
        future_ = {};
    }
    std::lock_guard<std::mutex> lock(resultMutex_);
    pending_.reset();
    hasResult_.store(false, std::memory_order_release);
    loading_.store(false, std::memory_order_release);
}

std::unique_ptr<Map> MapLoader::pollFinished() {
    if (!hasResult_.load(std::memory_order_acquire)) return nullptr;

    // The worker signalled completion; collect it.
    if (future_.valid()) {
        future_.get();
    }

    std::lock_guard<std::mutex> lock(resultMutex_);
    std::unique_ptr<Map> result = std::move(pending_);
    hasResult_.store(false, std::memory_order_release);
    loading_.store(false, std::memory_order_release);
    return result;
}
