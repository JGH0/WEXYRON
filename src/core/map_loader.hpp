/**
 * @file map_loader.hpp
 * @brief Loads a map (file parse + texture upload + instance build) in the background
 *
 * OpenGL is single-threaded: a context is current on exactly one thread. Rather
 * than fight that, the loader does every piece of *CPU* work for a map on a
 * worker thread and leaves only the final, tiny "swap the map in" to the main
 * thread.
 *
 * Because textures are created on the worker, the worker needs its own context
 * that shares objects with the main one (GLFW supplies this on every desktop
 * driver). If sharing is unavailable the loader runs synchronously instead, so
 * the game still works everywhere.
 */

#ifndef MAP_LOADER_HPP
#define MAP_LOADER_HPP

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <atomic>

class Map;
struct GLFWwindow;

class MapLoader {
public:
    MapLoader() = default;
    ~MapLoader();

    MapLoader(const MapLoader&) = delete;
    MapLoader& operator=(const MapLoader&) = delete;

    /**
     * @brief Start loading `mapFile` on a worker thread.
     *
     * Any load already in flight is cancelled (its result is discarded).
     * @param sharedWith the main GLFW window; the worker creates a context
     *                   that shares GL objects with it
     * @return false if background loading isn't possible (use loadBlocking)
     */
    bool start(const std::string& mapFile, GLFWwindow* sharedWith);

    /// @return true while a background load is running
    bool isLoading() const { return loading_.load(std::memory_order_acquire); }

    /**
     * @brief Fetch a finished map, if any.
     *
     * Must be called on the GL thread. The returned map owns GL textures loaded
     * by the worker; it is safe to use immediately.
     * @return the finished map (caller takes ownership), or nullptr
     */
    std::unique_ptr<Map> pollFinished();

    /// Load on the calling thread (used at startup and as the fallback path).
    static std::unique_ptr<Map> loadBlocking(const std::string& mapFile);

    /// Forget any in-flight result; waits for the worker to finish its write.
    void cancel();

private:
    static void ensureTextures(Map& map);

    std::future<bool> future_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> hasResult_{false};
    std::mutex resultMutex_;
    std::unique_ptr<Map> pending_;
    std::string mapFile_;
    bool sharedContextReady_ = false;
};

#endif // MAP_LOADER_HPP
