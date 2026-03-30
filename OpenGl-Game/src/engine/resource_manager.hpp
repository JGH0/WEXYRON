#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class Texture;
class Shader;
class Mesh;
class Material;
class AudioBuffer;
class Font;

/**
 * @brief Resource management system
 * 
 * Handles loading, caching, and management of all game resources.
 * Provides automatic resource cleanup and memory management.
 */
class ResourceManager {
public:
    // Resource types
    enum class ResourceType {
        Texture,
        Shader,
        Mesh,
        Material,
        Audio,
        Font,
        Other
    };
    
    // Resource base class
    class Resource {
    public:
        virtual ~Resource() = default;
        virtual ResourceType getType() const = 0;
        virtual const std::string& getName() const = 0;
        virtual const std::string& getPath() const = 0;
        virtual size_t getSize() const = 0;
        virtual bool isLoaded() const = 0;
        virtual void unload() = 0;
        
    protected:
        std::string name;
        std::string path;
        bool loaded = false;
    };

private:
    // Resource storage
    std::unordered_map<std::string, std::shared_ptr<Resource>> resources;
    std::unordered_map<std::string, std::string> resourcePaths;
    
    // Resource paths
    std::vector<std::string> searchPaths;
    
    // Resource statistics
    size_t totalMemoryUsage = 0;
    int resourceCount = 0;
    
    bool isInitialized = false;

public:
    ResourceManager();
    ~ResourceManager();
    
    // Resource manager lifecycle
    bool initialize();
    void shutdown();
    void garbageCollect();
    
    // Path management
    void addSearchPath(const std::string& path);
    void removeSearchPath(const std::string& path);
    std::string findResourcePath(const std::string& filename) const;
    
    // Resource loading
    std::shared_ptr<Texture> loadTexture(const std::string& path);
    std::shared_ptr<Shader> loadShader(const std::string& vertexPath, const std::string& fragmentPath);
    std::shared_ptr<Shader> loadShader(const std::string& path);
    std::shared_ptr<Mesh> loadMesh(const std::string& path);
    std::shared_ptr<Material> loadMaterial(const std::string& path);
    std::shared_ptr<AudioBuffer> loadAudio(const std::string& path);
    std::shared_ptr<Font> loadFont(const std::string& path, float size = 24.0f);
    
    // Resource access
    template<typename T>
    std::shared_ptr<T> getResource(const std::string& name);
    
    template<typename T>
    std::shared_ptr<T> getResource(const std::string& name) const;
    
    // Resource management
    void unloadResource(const std::string& name);
    void unloadResourcesByType(ResourceType type);
    void unloadAllResources();
    
    // Resource checking
    bool hasResource(const std::string& name) const;
    bool isResourceLoaded(const std::string& name) const;
    
    // Resource information
    std::vector<std::string> getResourceNames() const;
    std::vector<std::string> getResourceNamesByType(ResourceType type) const;
    size_t getResourceCount() const { return resourceCount; }
    size_t getTotalMemoryUsage() const { return totalMemoryUsage; }
    
    // Utility
    void reloadResource(const std::string& name);
    void reloadAllResources();
    void printResourceInfo() const;

private:
    std::shared_ptr<Resource> loadResourceInternal(const std::string& path, ResourceType type);
    void addResource(std::shared_ptr<Resource> resource);
    void removeResource(const std::string& name);
    void updateMemoryUsage();
    std::string getFileExtension(const std::string& path) const;
    ResourceType getResourceTypeFromExtension(const std::string& extension) const;
};

// Template implementations
template<typename T>
std::shared_ptr<T> ResourceManager::getResource(const std::string& name) {
    auto it = resources.find(name);
    if (it != resources.end()) {
        return std::dynamic_pointer_cast<T>(it->second);
    }
    return nullptr;
}

template<typename T>
std::shared_ptr<T> ResourceManager::getResource(const std::string& name) const {
    auto it = resources.find(name);
    if (it != resources.end()) {
        return std::dynamic_pointer_cast<T>(it->second);
    }
    return nullptr;
}

#endif
