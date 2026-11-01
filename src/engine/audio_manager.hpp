/**
 * @file audio_manager.hpp
 * @brief Sound engine: file-based sound effects and music on top of miniaudio
 *
 * The game talks to this class through one member (Game::audio):
 *
 *     audio.playAt("pistol_fire", camera.Position);   // 3D, mixed from the camera
 *     audio.playUi("ui_select");                      // 2D interface blip
 *     audio.setMusic("menu");                         // looping track, cross-faded
 *
 * All audio is file-based and lives in assets/audio/ :
 *
 *     assets/audio/effects/<name>.wav   (or .ogg / .flac / .mp3)
 *     assets/audio/music/<name>.wav
 *
 * A missing device, a missing file or a corrupt file never crashes the game:
 * playback turns into a no-op and the reason is printed once to the console.
 * `tools/generate_sounds.py` regenerates the shipped WAV files.
 *
 * The AudioBuffer / AudioSource / AudioListener classes keep the API of the
 * old TODO stub so any code built against it still compiles.
 */

#ifndef AUDIO_MANAGER_HPP
#define AUDIO_MANAGER_HPP

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>

class AudioBuffer;
class AudioSource;
class AudioListener;

/**
 * @brief Audio management system
 *
 * Handles audio playback including:
 * - Sound effects (files, played from a pool of voices)
 * - Music (looping, faded tracks, own volume bus)
 * - 3D positional audio with distance attenuation and stereo panning
 * - Master / effects / music volume buses
 */
class AudioManager {
public:
    struct AudioConfig {
        int sampleRate;      ///< preferred device rate (the device may differ)
        int channels;        ///< always 2 (stereo output)
        int bufferSize;      ///< mixer period in frames
        float masterVolume;  ///< 0..1
        float sfxVolume;     ///< 0..1, sound-effect bus
        float musicVolume;   ///< 0..1, music bus
        bool enable3D;
        int maxSources;      ///< size of the voice pool (max 64)

        AudioConfig() : sampleRate(44100), channels(2), bufferSize(512),
                       masterVolume(1.0f), sfxVolume(1.0f), musicVolume(1.0f),
                       enable3D(true), maxSources(32) {}
    };

    /// Distance model for positional sounds (see setDistanceModel).
    enum class DistanceModel {
        Linear,     ///< 1 at minDistance, 0 at maxDistance
        Inverse,    ///< minDistance / distance, silent at maxDistance (default)
        None        ///< no attenuation
    };

    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Open the audio device. Returns false (and keeps every playback call a
    /// no-op) when no device is available, e.g. on a machine without sound.
    bool initialize(const AudioConfig& config);
    void shutdown();
    /// Per-frame housekeeping (retries a device that failed to open).
    void update(float deltaTime);

    bool isInitialized() const { return deviceOpen_; }
    /// Why initialize() returned false (empty when audio works).
    const std::string& getError() const { return error_; }

    // ------------------------------------------------------------------
    // Sound registry
    // ------------------------------------------------------------------

    /// Load every sound file in `directory` (recursively). Files are
    /// registered under their file stem: pistol_fire.wav -> "pistol_fire".
    /// `volumeDb` is added to their level. Returns the number loaded.
    int loadSoundDirectory(const std::string& directory, float volumeDb = 0.0f);

    /// Load one file and register it under `name`.
    bool loadSound(const std::string& name, const std::string& path, float volumeDb = 0.0f);

    /// Folder to look in when a bare name is played (first hit wins).
    void addSearchPath(const std::string& directory);

    std::shared_ptr<AudioBuffer> loadAudio(const std::string& path);
    std::shared_ptr<AudioBuffer> getAudio(const std::string& name);
    /// Drop a sound's gain offset + samples (stops voices using it).
    void unloadAudio(const std::string& name);
    void unloadAllAudio();

    bool hasSound(const std::string& name) const;

    // ------------------------------------------------------------------
    // Playing
    // ------------------------------------------------------------------

    /// Play sound `name` at a world position (3D: attenuated + panned).
    bool playAt(const std::string& name, const glm::vec3& position,
                float volume = 1.0f, float pitch = 1.0f);
    /// Play at the listener's position (full volume, centred).
    bool play(const std::string& name, float volume = 1.0f, float pitch = 1.0f);
    /// Interface sound: 2D, never attenuated, never steals a gameplay voice.
    bool playUi(const std::string& name, float volume = 1.0f, float pitch = 1.0f);

    /// Old stub API: play a registered sound / buffer at a position.
    void playSound(const std::string& bufferName, const glm::vec3& position = glm::vec3(0.0f));
    void playSound(std::shared_ptr<AudioBuffer> buffer, const glm::vec3& position = glm::vec3(0.0f));

    void stopAllSounds();
    void pauseAllSounds();
    void resumeAllSounds();

    /// Source pool used by the old stub API (createSource() objects).
    std::shared_ptr<AudioSource> createSource();
    /// Push source changes into the mixer and drop finished sources.
    /// Called once per frame by update().
    void collectSources();

    // ------------------------------------------------------------------
    // Music
    // ------------------------------------------------------------------

    /// Play a looping music track. `name` is a file stem under the music
    /// search paths ("menu") or a path. Calling it with the current track
    /// only changes the volume. Returns false when the file is missing.
    bool setMusic(const std::string& name, float volume = 1.0f);
    void stopMusic();
    bool isMusicPlaying() const;
    /// Name of the current music track ("" when silent).
    std::string currentMusic() const;

    // ------------------------------------------------------------------
    // Listener + mixing
    // ------------------------------------------------------------------

    void setListenerPosition(const glm::vec3& position);
    void setListenerOrientation(const glm::vec3& forward, const glm::vec3& up);
    void setListenerVelocity(const glm::vec3& velocity);

    /// 0..1 volumes; applied live to playing sounds.
    void setMasterVolume(float volume);
    void setSFXVolume(float volume);
    void setMusicVolume(float volume);

    float getMasterVolume() const { return config.masterVolume; }
    float getSFXVolume() const { return config.sfxVolume; }
    float getMusicVolume() const { return config.musicVolume; }

    /// Per-sound volume multiplier (1.0 = file level), e.g. footsteps quieter
    /// than gunshots.
    void setSoundVolume(const std::string& name, float volume);
    /// Add a gain (dB) to every play of a registered sound.
    void setSoundVolumeDb(const std::string& name, float volumeDb);

    /// How positional sounds fade with distance.
    void setDistanceModel(DistanceModel model, float minDistance, float maxDistance);
    DistanceModel getDistanceModel() const { return distanceModel_; }

    const AudioConfig& getConfig() const { return config; }
    void setConfig(const AudioConfig& newConfig);

    /// Number of effects currently playing (music excluded).
    int getPlayingSourceCount() const;
    int getLoadedBufferCount() const { return static_cast<int>(sounds_.size()); }

    // ------------------------------------------------------------------
    // Mixer internals. Public only because the miniaudio C callback calls
    // mix(); not part of the game-facing API.
    // ------------------------------------------------------------------

    void mix(float* out, unsigned int frames);

    struct Voice;        ///< one playing sound (audio_manager.cpp)
    struct SoundEntry;   ///< one registered sound (audio_manager.cpp)

private:
    struct Mixer;        ///< voice pool + music state (audio_manager.cpp)
    struct SourceRecord; ///< old-API AudioSource -> voice binding

    bool startDevice();
    void destroyDevice();
    float spatialGain(const glm::vec3& position) const;
    std::string resolveFile(const std::string& name, bool music) const;
    bool startVoice(const std::string& name, const glm::vec3& position,
                    float volume, float pitch, bool positional, bool looping);

    AudioConfig config;
    std::vector<std::shared_ptr<AudioSource>> sources;
    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> buffers;
    std::shared_ptr<AudioListener> listener;

    bool deviceOpen_ = false;
    std::string error_;

    std::unordered_map<std::string, std::unique_ptr<SoundEntry>> sounds_;
    std::vector<std::string> sfxPaths_;
    std::vector<std::string> musicPaths_;
    std::unique_ptr<Mixer> mixer_;
    std::unique_ptr<std::vector<SourceRecord>> audioSources_;

    // Playback-rate (pitch) of each non-music voice, indexed like voices[].
    std::array<float, 64> pitchRatios_{};
    uint64_t voiceGeneration_ = 0;

    // Listener.
    glm::vec3 listenerPos_ = glm::vec3(0.0f);
    glm::vec3 listenerForward_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 listenerUp_ = glm::vec3(0.0f, 1.0f, 0.0f);

    DistanceModel distanceModel_ = DistanceModel::Inverse;
    float minDistance_ = 1.5f;
    float maxDistance_ = 80.0f;

    mutable std::mutex nameMutex_;
    std::string musicName_;

    float retryTimer_ = 0.0f;
    std::atomic<double> maxMixLockSeconds_{0.0};

    struct ma_device* device_ = nullptr;
};

/**
 * @brief Audio buffer: one decoded file (interleaved 32-bit float PCM)
 *
 * Files are decoded once on load, so playing a sound never touches the disk.
 */
class AudioBuffer {
private:
    std::string name;
    std::string path;
    unsigned int bufferId = 0;
    int channels = 0;
    int sampleRate = 0;
    int size = 0;                          ///< frames per channel
    bool isLoaded = false;
    std::shared_ptr<std::vector<float>> samples;   ///< shared with the mixer

public:
    AudioBuffer(const std::string& name, const std::string& path);
    ~AudioBuffer();

    /// Decode the file into memory (idempotent). False on a missing or
    /// unsupported file.
    bool load();
    void unload();

    const std::string& getName() const { return name; }
    const std::string& getPath() const { return path; }
    bool getIsLoaded() const { return isLoaded; }
    unsigned int getBufferId() const { return bufferId; }
    int getChannels() const { return channels; }
    int getSampleRate() const { return sampleRate; }
    int getSize() const { return size; }
    /// Decoded interleaved samples (`channels` per frame).
    const std::vector<float>& getSamples() const;
    /// Shared pointer to the samples, for the mixer.
    const std::shared_ptr<std::vector<float>>& getSamplesPtr() const { return samples; }
    float durationSeconds() const {
        return sampleRate > 0 ? static_cast<float>(size) / static_cast<float>(sampleRate) : 0.0f;
    }
};

/**
 * @brief Audio source: one playing sound controlled from game code
 *
 * Created with AudioManager::createSource(); assign a buffer with setBuffer()
 * and start it with play(). Positional sources follow setPosition().
 */
class AudioSource {
private:
    std::shared_ptr<AudioBuffer> buffer;

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float volume = 1.0f;
    float pitch = 1.0f;
    bool looping = false;
    bool is3D = true;

    bool isPlaying = false;
    bool isPaused = false;
    uint64_t startSerial = 0;   ///< bumped by play(): the manager (re)starts the voice

public:
    AudioSource();
    ~AudioSource();

    bool initialize();
    void shutdown();

    void play();
    void pause();
    void stop();
    void rewind();

    void setBuffer(std::shared_ptr<AudioBuffer> newBuffer);
    std::shared_ptr<AudioBuffer> getBuffer() const { return buffer; }

    const glm::vec3& getPosition() const { return position; }
    void setPosition(const glm::vec3& pos) { position = pos; }
    const glm::vec3& getVelocity() const { return velocity; }
    void setVelocity(const glm::vec3& vel) { velocity = vel; }
    float getVolume() const { return volume; }
    void setVolume(float vol);
    float getPitch() const { return pitch; }
    void setPitch(float pitch) { this->pitch = pitch < 0.01f ? 0.01f : pitch; }
    bool getLooping() const { return looping; }
    void setLooping(bool loop) { looping = loop; }
    bool getIs3D() const { return is3D; }
    void setIs3D(bool value) { is3D = value; }

    bool getIsPlaying() const { return isPlaying; }
    bool getIsPaused() const { return isPaused; }

    /// Called once per frame by the manager (state lives in the mixer).
    void update(float deltaTime);

    friend class AudioManager;
};

/**
 * @brief Audio listener for 3D audio (the player's ears: the camera)
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

    const glm::vec3& getPosition() const { return position; }
    void setPosition(const glm::vec3& pos) { position = pos; }
    const glm::vec3& getVelocity() const { return velocity; }
    void setVelocity(const glm::vec3& vel) { velocity = vel; }
    const glm::vec3& getForward() const { return forward; }
    const glm::vec3& getUp() const { return up; }
    void setOrientation(const glm::vec3& f, const glm::vec3& u) { forward = f; up = u; }

    /// The manager reads the listener members when mixing.
    void apply() {}
};

#endif // AUDIO_MANAGER_HPP
