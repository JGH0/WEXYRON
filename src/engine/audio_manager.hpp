#ifndef AUDIO_MANAGER_HPP
#define AUDIO_MANAGER_HPP

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// Forward declarations
class AudioBuffer;
class AudioSource;
class AudioListener;

/**
 * @brief Audio management system
 * 
 * Handles audio playback including:
 * - Sound effects
 * - Music streaming
 * - 3D positional audio
 * - Audio effects and processing
 */
class AudioManager {
public:
    struct AudioConfig {
        int sampleRate;
        int channels;
        int bufferSize;
        float masterVolume;
        float sfxVolume;
        float musicVolume;
        bool enable3D;
        int maxSources;
        
        AudioConfig() : sampleRate(44100), channels(2), bufferSize(512),
                       masterVolume(1.0f), sfxVolume(1.0f), musicVolume(1.0f),
                       enable3D(true), maxSources(32) {}
    };

private:
    AudioConfig config;
    std::vector<std::shared_ptr<AudioSource>> sources;
    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> buffers;
    std::shared_ptr<AudioListener> listener;
    
    bool isInitialized;

public:
    AudioManager();
    ~AudioManager();
    
    // Audio manager lifecycle
    bool initialize(const AudioConfig& config);
    void shutdown();
    void update(float deltaTime);
    
    // Audio buffer management
    std::shared_ptr<AudioBuffer> loadAudio(const std::string& path);
    std::shared_ptr<AudioBuffer> getAudio(const std::string& name);
    void unloadAudio(const std::string& name);
    void unloadAllAudio();
    
    // Audio source management
    std::shared_ptr<AudioSource> createSource();
    void playSound(const std::string& bufferName, const glm::vec3& position = glm::vec3(0.0f));
    void playSound(std::shared_ptr<AudioBuffer> buffer, const glm::vec3& position = glm::vec3(0.0f));
    void stopAllSounds();
    void pauseAllSounds();
    void resumeAllSounds();
    
    // Listener management
    void setListenerPosition(const glm::vec3& position);
    void setListenerOrientation(const glm::vec3& forward, const glm::vec3& up);
    void setListenerVelocity(const glm::vec3& velocity);
    
    // Volume control
    void setMasterVolume(float volume);
    void setSFXVolume(float volume);
    void setMusicVolume(float volume);
    
    float getMasterVolume() const { return config.masterVolume; }
    float getSFXVolume() const { return config.sfxVolume; }
    float getMusicVolume() const { return config.musicVolume; }
    
    // Configuration
    const AudioConfig& getConfig() const { return config; }
    void setConfig(const AudioConfig& newConfig);
    
    // Statistics
    int getPlayingSourceCount() const;
    int getLoadedBufferCount() const { return buffers.size(); }

private:
    void updateSources(float deltaTime);
    void cleanupStoppedSources();
};

/**
 * @brief Audio buffer for loaded audio data
 */
class AudioBuffer {
private:
    std::string name;
    std::string path;
    unsigned int bufferId = 0;
    int channels = 0;
    int sampleRate = 0;
    int size = 0;
    bool isLoaded = false;

public:
    AudioBuffer(const std::string& name, const std::string& path);
    ~AudioBuffer();
    
    bool load();
    void unload();
    
    const std::string& getName() const { return name; }
    const std::string& getPath() const { return path; }
    bool getIsLoaded() const { return isLoaded; }
    unsigned int getBufferId() const { return bufferId; }
    int getChannels() const { return channels; }
    int getSampleRate() const { return sampleRate; }
    int getSize() const { return size; }
};

/**
 * @brief Audio source for playing audio
 */
class AudioSource {
private:
    unsigned int sourceId = 0;
    std::shared_ptr<AudioBuffer> buffer;
    
    // Position and properties
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float volume = 1.0f;
    float pitch = 1.0f;
    bool looping = false;
    bool is3D = true;
    
    // State
    bool isPlaying = false;
    bool isPaused = false;

public:
    AudioSource();
    ~AudioSource();
    
    bool initialize();
    void shutdown();
    
    // Playback control
    void play();
    void pause();
    void stop();
    void rewind();
    
    // Buffer assignment
    void setBuffer(std::shared_ptr<AudioBuffer> newBuffer);
    std::shared_ptr<AudioBuffer> getBuffer() const { return buffer; }
    
    // Position and properties
    const glm::vec3& getPosition() const { return position; }
    void setPosition(const glm::vec3& pos);
    const glm::vec3& getVelocity() const { return velocity; }
    void setVelocity(const glm::vec3& vel);
    float getVolume() const { return volume; }
    void setVolume(float vol);
    float getPitch() const { return pitch; }
    void setPitch(float pitch);
    bool getLooping() const { return looping; }
    void setLooping(bool loop);
    bool getIs3D() const { return is3D; }
    void setIs3D(bool is3D);
    
    // State
    bool getIsPlaying() const { return isPlaying; }
    bool getIsPaused() const { return isPaused; }
    
    // Utility
    void update(float deltaTime);
};

/**
 * @brief Audio listener for 3D audio
 */
class AudioListener {
private:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

public:
    AudioListener() = default;
    ~AudioListener() = default;
    
    // Position and orientation
    const glm::vec3& getPosition() const { return position; }
    void setPosition(const glm::vec3& pos);
    const glm::vec3& getVelocity() const { return velocity; }
    void setVelocity(const glm::vec3& vel);
    const glm::vec3& getForward() const { return forward; }
    const glm::vec3& getUp() const { return up; }
    void setOrientation(const glm::vec3& forward, const glm::vec3& up);
    
    // Apply to OpenAL
    void apply();
};

#endif
