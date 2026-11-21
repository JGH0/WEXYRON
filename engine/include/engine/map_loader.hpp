/**
 * @file map_loader.hpp
 * @brief Loads a map (file parse + texture decode) in the background
 *
 * The loader does every piece of *CPU* work for a map on a worker thread: it
 * parses the file and decodes the material textures into RGBA. No graphics API
 * is touched there. Only the final, tiny "swap the map in" (and the renderer's
 * texture upload) happens on the main thread.
 */

#ifndef MAP_LOADER_HPP
#define MAP_LOADER_HPP

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <atomic>

class Map;

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
     */
    void start(const std::string& mapFile);

    /// @return true while a background load is running
    bool isLoading() const { return loading_.load(std::memory_order_acquire); }

    /**
     * @brief Fetch a finished map, if any.
     *
     * Call on the main thread. The returned map holds decoded textures; the
     * renderer uploads them when the map is installed.
     * @return the finished map (caller takes ownership), or nullptr
     */
    std::unique_ptr<Map> pollFinished();

    /// Load on the calling thread (used at startup and as the fallback path).
    static std::unique_ptr<Map> loadBlocking(const std::string& mapFile);

    /// Load the eight stock material textures into `map`. A map that was not
    /// read from a file (the editor's "new map") starts with no textures, so
    /// it calls this to get the same look as the shipped maps.
    static void ensureTextures(Map& map);

    /// Forget any in-flight result; waits for the worker to finish its write.
    void cancel();

private:
    std::future<bool> future_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> hasResult_{false};
    std::mutex resultMutex_;
    std::unique_ptr<Map> pending_;
    std::string mapFile_;
};

#endif // MAP_LOADER_HPP
