/**
 * @file map_loader.cpp
 * @brief Background map loading (see map_loader.hpp)
 */

#include "map_loader.hpp"
#include "map.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
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
    // Textures are created on the calling thread, which must own a context.
    auto map = std::make_unique<Map>("assets/maps/" + mapFile);
    ensureTextures(*map);
    return map;
}

bool MapLoader::start(const std::string& mapFile, GLFWwindow* sharedWith) {
    cancel();

    if (!sharedWith) {
        // No context to share: the caller must use loadBlocking() instead.
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        mapFile_ = mapFile;
        hasResult_.store(false, std::memory_order_release);
    }
    loading_.store(true, std::memory_order_release);

    const bool firstLoad = !sharedContextReady_;
    sharedContextReady_ = true;

    future_ = std::async(std::launch::async, [this, mapFile, sharedWith, firstLoad]() {
        // A worker needs its own current context. GLFW can create one that
        // shares objects (textures, buffers) with the main context, so the
        // texture objects built here are usable on the main thread.
        GLFWwindow* workerContext = glfwCreateWindow(1, 1, "map-loader", nullptr, sharedWith);
        if (workerContext) {
            glfwMakeContextCurrent(workerContext);
        } else if (firstLoad) {
            // Some drivers only allow sharing at creation time; report once.
            std::cerr << "MapLoader: sharing GL context unavailable, loading "
                         "synchronously on the main thread." << std::endl;
        }

        auto map = std::make_unique<Map>("assets/maps/" + mapFile);
        ensureTextures(*map);

        if (workerContext) {
            // Make the objects resident and finish all GPU work before the
            // context is destroyed.
            glFlush();
            glfwMakeContextCurrent(nullptr);
            glfwDestroyWindow(workerContext);
        }

        std::lock_guard<std::mutex> lock(resultMutex_);
        pending_ = std::move(map);
        hasResult_.store(true, std::memory_order_release);
        return true;
    });

    return true;
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

    // The worker signalled completion; collect it and release its context work.
    if (future_.valid()) {
        future_.get();
    }

    std::lock_guard<std::mutex> lock(resultMutex_);
    std::unique_ptr<Map> result = std::move(pending_);
    hasResult_.store(false, std::memory_order_release);
    loading_.store(false, std::memory_order_release);
    return result;
}
