/**
 * @file audio_manager.cpp
 * @brief Sound engine implementation (miniaudio device + software 3D mixer)
 *
 * Signal path:
 *
 *   file -> AudioBuffer (decoded float PCM, interleaved)
 *        -> Voice (one playing source: position, pitch, per-sound gain)
 *        -> distance attenuation + constant-power panning -> stereo
 *        -> effects bus (sfx volume)  \
 *   music -> looped track, fade in/out -> music bus (music volume)  +-> master -> device
 *
 * mix() runs on the miniaudio device thread. The voice pool and music state
 * are guarded by Mixer::mutex, which the game thread only holds briefly
 * (starting a sound, changing music), so the mixer never waits on game logic.
 */

#include "audio_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <system_error>

// The implementation is compiled in third_party/miniaudio/miniaudio.c; this
// translation unit only needs the declarations.
#include "../../third_party/miniaudio/miniaudio.h"

namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Extensions miniaudio can decode and that make sense for the game.
const char* const kExtensions[] = {".wav", ".ogg", ".flac", ".mp3"};

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool hasAudioExtension(const std::string& path) {
    const std::string ext = toLower(fs::path(path).extension().string());
    for (const char* known : kExtensions) {
        if (ext == known) return true;
    }
    return false;
}

float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float clamp01(float v) {
    return std::min(1.0f, std::max(0.0f, v));
}

} // namespace

// ---------------------------------------------------------------------------
// Mixer state
// ---------------------------------------------------------------------------

/// One playing sound.
struct AudioManager::Voice {
    bool active = false;
    bool positional = false;     ///< distance attenuation + panning
    bool music = false;          ///< the music player (own bus, never stolen)
    std::shared_ptr<std::vector<float>> samples;
    int channels = 1;
    int sampleRate = 44100;
    double pos = 0.0;            ///< fractional frame position
    float rate = 1.0f;           ///< pitch / playback rate (1 = normal)

    glm::vec3 position = glm::vec3(0.0f);
    float volume = 1.0f;         ///< play volume * per-sound gain
};

/// One registered sound.
struct AudioManager::SoundEntry {
    std::shared_ptr<AudioBuffer> buffer;
    float volumeDb = 0.0f;                  ///< gain of the loadSound*() call
    std::atomic<float> volume{1.0f};        ///< setSoundVolume()
};

struct AudioManager::Mixer {
    static constexpr int kMaxVoices = 64;

    std::mutex mutex;                       ///< guards the members below
    Voice voices[kMaxVoices];
    int musicVoice = -1;                    ///< index into voices[]
    float musicGain = 0.0f;                 ///< fade envelope, 0..musicTarget
    float musicTarget = 0.0f;               ///< requested music level
    std::atomic<int> playingCount{0};
};

/// AudioSource (old stub API) -> mixer voice binding.
struct AudioManager::SourceRecord {
    std::shared_ptr<AudioSource> source;
    int voice = -1;                         ///< mixer slot; -1 = not playing

    // Last values handed to the voice, so update() writes only on change.
    AudioBuffer* samples = nullptr;
    glm::vec3 position{0.0f};
    float pitch = 1.0f;
    float volume = 1.0f;
    bool paused = false;
    uint64_t startSerial = 0;   ///< AudioSource::startSerial when the voice began
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioManager::AudioManager() {
    deviceOpen_ = false;
    listener = std::make_shared<AudioListener>();
    mixer_.reset(new Mixer());
    audioSources_.reset(new std::vector<SourceRecord>());
}

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::initialize(const AudioConfig& audioConfig) {
    if (deviceOpen_) return true;
    config = audioConfig;
    config.maxSources = std::min(std::max(config.maxSources, 1), Mixer::kMaxVoices);
    error_.clear();

    // Search paths relative to the working directory (the build dir for
    // `make run`, the project root for ./build/bin/game).
    sfxPaths_.clear();
    musicPaths_.clear();
    addSearchPath("assets/audio/effects");
    addSearchPath("assets/audio");
    addSearchPath("audio/effects");
    addSearchPath("audio");
    musicPaths_.push_back("assets/audio/music");
    musicPaths_.push_back("audio/music");
    musicPaths_.push_back("assets/audio");
    musicPaths_.push_back("audio");

    if (!startDevice()) {
        // No sound card, no AudioContext or the device is busy: keep going
        // silently. The game must never fail to start because of audio.
        std::cerr << "Audio: " << error_ << std::endl;
        return false;
    }
    deviceOpen_ = true;
    return true;
}

bool AudioManager::startDevice() {
    destroyDevice();

    ma_device_config devCfg = ma_device_config_init(ma_device_type_playback);
    devCfg.playback.format = ma_format_f32;
    devCfg.playback.channels = 2;
    devCfg.sampleRate = static_cast<ma_uint32>(config.sampleRate);
    devCfg.periodSizeInFrames = static_cast<ma_uint32>(std::max(config.bufferSize, 64));
    devCfg.performanceProfile = ma_performance_profile_low_latency;
    devCfg.pUserData = this;
    devCfg.dataCallback = [](ma_device* device, void* out, const void* /*in*/,
                             ma_uint32 frames) {
        AudioManager* self = static_cast<AudioManager*>(device->pUserData);
        if (!self || !out) return;
        self->mix(static_cast<float*>(out), frames);
    };

    ma_device* device = new ma_device();
    const ma_result rc = ma_device_init(nullptr, &devCfg, device);
    if (rc != MA_SUCCESS) {
        error_ = std::string("could not open an audio device (miniaudio error ") +
                 std::to_string(static_cast<int>(rc)) + "). The game runs without sound.";
        delete device;
        return false;
    }
    if (ma_device_start(device) != MA_SUCCESS) {
        ma_device_uninit(device);
        delete device;
        error_ = "the audio device did not start. The game runs without sound.";
        return false;
    }
    device_ = device;

    // Use the rate the device negotiated (the mixer resamples its sources).
    config.sampleRate = static_cast<int>(device->sampleRate);
    std::cout << "Audio: device ready (" << device->playback.name << ", "
              << device->sampleRate << " Hz)" << std::endl;
    return true;
}

void AudioManager::destroyDevice() {
    if (!device_) return;
    ma_device* device = device_;
    device_ = nullptr;
    ma_device_uninit(device);   // stops the device and joins its mixing thread
    delete device;
}

void AudioManager::shutdown() {
    // Stop mixing first: after ma_device_uninit() returns, mix() can no longer
    // run, so the samples below can be freed safely.
    destroyDevice();
    deviceOpen_ = false;
    if (mixer_) {
        std::lock_guard<std::mutex> lock(mixer_->mutex);
        for (auto& voice : mixer_->voices) voice = Voice{};
        mixer_->musicVoice = -1;
        mixer_->musicGain = 0.0f;
        mixer_->musicTarget = 0.0f;
        mixer_->playingCount.store(0);
    }
    if (audioSources_) audioSources_->clear();
    sources.clear();
    sounds_.clear();
    buffers.clear();
    {
        std::lock_guard<std::mutex> lock(nameMutex_);
        musicName_.clear();
    }
}

void AudioManager::update(float deltaTime) {
    if (!deviceOpen_) {
        // Retry the device every couple of seconds when it could not be
        // opened at startup (e.g. a headset was plugged in later).
        if (error_.empty()) return;
        retryTimer_ += std::max(deltaTime, 0.0f);
        if (retryTimer_ < 2.0f) return;
        retryTimer_ = 0.0f;
        if (startDevice()) {
            deviceOpen_ = true;
            error_.clear();
        }
        return;
    }

    collectSources();
}

void AudioManager::collectSources() {
    if (!mixer_ || !audioSources_) return;
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    for (SourceRecord& record : *audioSources_) {
        AudioSource& src = *record.source;

        // Start / restart a source that was told to play since the last frame.
        if (src.isPlaying && src.startSerial != record.startSerial) {
            // play() was called again: restart from the beginning of the
            // buffer. A running voice of this source is replaced.
            if (record.voice >= 0) {
                mixer_->voices[record.voice] = Voice{};
                record.voice = -1;
            }
            record.startSerial = src.startSerial;
        }
        if (record.voice >= 0 && (record.voice >= Mixer::kMaxVoices ||
                                  !mixer_->voices[record.voice].active ||
                                  mixer_->voices[record.voice].music)) {
            // The voice ended (buffer finished or was stolen).
            src.isPlaying = false;
            src.isPaused = false;
            record.voice = -1;
        }
        if (src.isPlaying && record.voice < 0) {
            if (!src.buffer || !src.buffer->getIsLoaded() || !src.buffer->getSamplesPtr()) {
                src.isPlaying = false;
                continue;
            }
            int slot = -1;
            for (int i = 0; i < Mixer::kMaxVoices - 1; ++i) {   // skip the music voice
                if (!mixer_->voices[i].active) { slot = i; break; }
            }
            if (slot < 0) continue;   // all voices busy: try again next frame
            record.voice = slot;
            Voice& voice = mixer_->voices[slot];
            voice = Voice{};
            voice.active = true;
            voice.positional = src.is3D;
            voice.samples = src.buffer->getSamplesPtr();
            voice.channels = std::max(src.buffer->getChannels(), 1);
            voice.sampleRate = std::max(src.buffer->getSampleRate(), 1);
            voice.position = src.position;
            voice.volume = src.volume;
            voice.rate = src.pitch;
            record.samples = src.buffer.get();
            record.position = src.position;
            record.pitch = src.pitch;
            record.volume = src.volume;
            record.paused = src.isPaused;
            record.startSerial = src.startSerial;
            continue;
        }
        if (record.voice < 0) continue;

        Voice& voice = mixer_->voices[record.voice];
        if (src.buffer.get() != record.samples) {
            voice.samples = src.buffer ? src.buffer->getSamplesPtr() : nullptr;
            if (src.buffer) {
                voice.channels = std::max(src.buffer->getChannels(), 1);
                voice.sampleRate = std::max(src.buffer->getSampleRate(), 1);
            }
            record.samples = src.buffer.get();
        }
        if (src.position != record.position) {
            voice.position = src.position;
            record.position = src.position;
        }
        if (src.pitch != record.pitch) {
            voice.rate = src.isPaused ? 0.0f : src.pitch;
            record.pitch = src.pitch;
        }
        if (src.volume != record.volume) {
            voice.volume = src.volume;
            record.volume = src.volume;
        }
        if (src.isPaused != record.paused) {
            voice.rate = src.isPaused ? 0.0f : src.pitch;
            record.paused = src.isPaused;
        }
    }
}

// ---------------------------------------------------------------------------
// Volume + listener
// ---------------------------------------------------------------------------

void AudioManager::setMasterVolume(float volume) { config.masterVolume = clamp01(volume); }
void AudioManager::setSFXVolume(float volume) { config.sfxVolume = clamp01(volume); }
void AudioManager::setMusicVolume(float volume) { config.musicVolume = clamp01(volume); }

void AudioManager::setConfig(const AudioConfig& newConfig) {
    config = newConfig;
    config.maxSources = std::min(std::max(config.maxSources, 1), Mixer::kMaxVoices);
}

void AudioManager::setListenerPosition(const glm::vec3& position) {
    listenerPos_ = position;
    if (listener) listener->setPosition(position);
}

void AudioManager::setListenerOrientation(const glm::vec3& forward, const glm::vec3& up) {
    if (glm::length(forward) > 1e-6f) listenerForward_ = glm::normalize(forward);
    if (glm::length(up) > 1e-6f) listenerUp_ = glm::normalize(up);
    if (listener) listener->setOrientation(listenerForward_, listenerUp_);
}

void AudioManager::setListenerVelocity(const glm::vec3& velocity) {
    if (listener) listener->setVelocity(velocity);
}

void AudioManager::setDistanceModel(DistanceModel model, float minDistance, float maxDistance) {
    distanceModel_ = model;
    minDistance_ = std::max(minDistance, 0.01f);
    maxDistance_ = std::max(maxDistance, minDistance_ + 0.01f);
}

void AudioManager::setSoundVolume(const std::string& name, float volume) {
    auto it = sounds_.find(name);
    if (it != sounds_.end()) it->second->volume.store(clamp01(volume));
}

void AudioManager::setSoundVolumeDb(const std::string& name, float volumeDb) {
    auto it = sounds_.find(name);
    if (it != sounds_.end()) it->second->volumeDb = volumeDb;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void AudioManager::addSearchPath(const std::string& directory) {
    if (std::find(sfxPaths_.begin(), sfxPaths_.end(), directory) == sfxPaths_.end())
        sfxPaths_.push_back(directory);
}

std::string AudioManager::resolveFile(const std::string& name, bool music) const {
    std::error_code ec;
    if (fs::is_regular_file(name, ec)) return name;

    const std::vector<std::string>& paths = music ? musicPaths_ : sfxPaths_;
    const bool hasExt = hasAudioExtension(name);
    for (const std::string& dir : paths) {
        const std::string candidate = (fs::path(dir) / name).string();
        if (fs::is_regular_file(candidate, ec)) return candidate;
        if (!hasExt) {
            for (const char* ext : kExtensions) {
                const std::string withExt = candidate + ext;
                if (fs::is_regular_file(withExt, ec)) return withExt;
            }
        }
    }
    return {};
}

bool AudioManager::loadSound(const std::string& name, const std::string& path, float volumeDb) {
    const std::string resolved = resolveFile(path, false);
    if (resolved.empty()) {
        std::cerr << "Audio: sound file not found: " << path << std::endl;
        return false;
    }
    auto buffer = std::make_shared<AudioBuffer>(name, resolved);
    if (!buffer->load()) return false;

    auto entry = std::make_unique<SoundEntry>();
    entry->buffer = buffer;
    entry->volumeDb = volumeDb;
    sounds_[name] = std::move(entry);
    return true;
}

int AudioManager::loadSoundDirectory(const std::string& directory, float volumeDb) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        std::cerr << "Audio: sound directory not found: " << directory << std::endl;
        return 0;
    }

    int loaded = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (entry.is_directory()) {
            loaded += loadSoundDirectory(entry.path().string(), volumeDb);
            continue;
        }
        if (!entry.is_regular_file() || !hasAudioExtension(entry.path().string())) continue;

        // Register under the file stem ("pistol_fire.wav" -> "pistol_fire").
        const std::string name = entry.path().stem().string();
        if (loadSound(name, entry.path().string(), volumeDb)) ++loaded;
    }
    return loaded;
}

std::shared_ptr<AudioBuffer> AudioManager::loadAudio(const std::string& path) {
    const std::string resolved = resolveFile(path, false);
    if (resolved.empty()) return nullptr;
    auto buffer = std::make_shared<AudioBuffer>(fs::path(resolved).stem().string(), resolved);
    return buffer->load() ? buffer : nullptr;
}

std::shared_ptr<AudioBuffer> AudioManager::getAudio(const std::string& name) {
    auto it = sounds_.find(name);
    if (it != sounds_.end()) return it->second->buffer;
    auto legacy = buffers.find(name);
    return legacy != buffers.end() ? legacy->second : nullptr;
}

bool AudioManager::hasSound(const std::string& name) const {
    return sounds_.find(name) != sounds_.end();
}

void AudioManager::unloadAudio(const std::string& name) {
    std::shared_ptr<AudioBuffer> buffer;
    auto it = sounds_.find(name);
    if (it != sounds_.end()) {
        buffer = it->second->buffer;
        sounds_.erase(it);
    }
    buffers.erase(name);

    if (!buffer || !mixer_) return;
    // Stop the voices still reading this buffer.
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    for (auto& voice : mixer_->voices) {
        if (voice.active && !voice.music && voice.samples == buffer->getSamplesPtr()) {
            voice = Voice{};
        }
    }
    for (SourceRecord& record : *audioSources_) {
        if (record.source && record.source->buffer == buffer) {
            record.source->isPlaying = false;
            record.voice = -1;
        }
    }
}

void AudioManager::unloadAllAudio() {
    if (mixer_) {
        std::lock_guard<std::mutex> lock(mixer_->mutex);
        for (auto& voice : mixer_->voices) voice = Voice{};
        mixer_->musicVoice = -1;
        mixer_->musicGain = 0.0f;
        mixer_->musicTarget = 0.0f;
    }
    for (SourceRecord& record : *audioSources_) {
        record.source->isPlaying = false;
        record.voice = -1;
    }
    sources.clear();
    sounds_.clear();
    buffers.clear();
    std::lock_guard<std::mutex> lock(nameMutex_);
    musicName_.clear();
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

bool AudioManager::startVoice(const std::string& name, const glm::vec3& position,
                              float volume, float pitch, bool positional, bool looping) {
    if (!deviceOpen_ || !mixer_) return false;
    auto it = sounds_.find(name);
    if (it == sounds_.end()) {
        // Report an unknown name once (not on every shot) so a typo in the
        // gameplay code is visible in the console.
        static std::mutex reportedMutex;
        static std::unordered_map<std::string, bool> reported;
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(reportedMutex);
            first = !reported[name];
            reported[name] = true;
        }
        if (first) {
            std::cerr << "Audio: no sound named '" << name << "'. Expected assets/audio/effects/"
                      << name << ".wav (or .ogg/.flac/.mp3)" << std::endl;
        }
        return false;
    }
    SoundEntry& entry = *it->second;
    if (!entry.buffer || !entry.buffer->getIsLoaded()) return false;

    std::lock_guard<std::mutex> lock(mixer_->mutex);

    // Find a voice: a free one, else steal the quietest effect. Effects never
    // steal the music voice, and interface sounds never steal anything.
    int slot = -1;
    int quietest = -1;
    for (int i = 0; i < Mixer::kMaxVoices; ++i) {
        const Voice& v = mixer_->voices[i];
        if (v.music) continue;
        if (!v.active) { slot = i; break; }
        if (quietest < 0 || v.volume < mixer_->voices[quietest].volume) quietest = i;
    }
    if (slot < 0) {
        if (!positional && quietest >= 0) return false;   // blip: never cut a shot
        slot = quietest;
    }
    if (slot < 0) return false;

    Voice& voice = mixer_->voices[slot];
    voice = Voice{};
    voice.active = true;
    voice.positional = positional && config.enable3D;
    voice.samples = entry.buffer->getSamplesPtr();
    voice.channels = std::max(entry.buffer->getChannels(), 1);
    voice.sampleRate = std::max(entry.buffer->getSampleRate(), 1);
    voice.pos = 0.0;
    voice.rate = std::max(pitch, 0.01f);
    voice.position = positional ? position : listenerPos_;
    voice.volume = std::min(std::max(volume, 0.0f), 4.0f) * entry.volume.load()
                   * dbToLinear(entry.volumeDb);
    (void)looping;
    return true;
}

bool AudioManager::playAt(const std::string& name, const glm::vec3& position,
                          float volume, float pitch) {
    if (name.empty()) return false;
    return startVoice(name, position, volume, pitch, true, false);
}

bool AudioManager::play(const std::string& name, float volume, float pitch) {
    if (name.empty()) return false;
    return startVoice(name, listenerPos_, volume, pitch, true, false);
}

bool AudioManager::playUi(const std::string& name, float volume, float pitch) {
    if (name.empty()) return false;
    return startVoice(name, listenerPos_, volume, pitch, false, false);
}

std::shared_ptr<AudioSource> AudioManager::createSource() {
    auto source = std::make_shared<AudioSource>();
    SourceRecord record;
    record.source = source;
    audioSources_->push_back(record);
    sources.push_back(source);
    return source;
}

void AudioManager::playSound(const std::string& bufferName, const glm::vec3& position) {
    playAt(bufferName, position);
}

void AudioManager::playSound(std::shared_ptr<AudioBuffer> buffer, const glm::vec3& position) {
    if (!buffer || !buffer->getIsLoaded() || !mixer_) return;
    // Old stub API: register the buffer under its name, then play it.
    if (sounds_.find(buffer->getName()) == sounds_.end()) {
        auto entry = std::make_unique<SoundEntry>();
        entry->buffer = buffer;
        sounds_[buffer->getName()] = std::move(entry);
    }
    playAt(buffer->getName(), position);
}

void AudioManager::stopAllSounds() {
    if (!mixer_) return;
    {
        std::lock_guard<std::mutex> lock(mixer_->mutex);
        for (auto& voice : mixer_->voices) {
            if (!voice.music) voice = Voice{};   // the music has its own bus
        }
    }
    for (SourceRecord& record : *audioSources_) {
        record.source->isPlaying = false;
        record.voice = -1;
    }
}

void AudioManager::pauseAllSounds() {
    if (!mixer_) return;
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    for (auto& voice : mixer_->voices) {
        if (!voice.music) voice.rate = 0.0f;
    }
}

void AudioManager::resumeAllSounds() {
    if (!mixer_) return;
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    for (auto& voice : mixer_->voices) {
        if (!voice.music && voice.rate == 0.0f) voice.rate = 1.0f;
    }
}

int AudioManager::getPlayingSourceCount() const {
    if (!mixer_) return 0;
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    int count = 0;
    for (const auto& voice : mixer_->voices) count += (voice.active && !voice.music) ? 1 : 0;
    return count;
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------

bool AudioManager::setMusic(const std::string& name, float volume) {
    if (!deviceOpen_ || !mixer_) return false;

    {
        std::lock_guard<std::mutex> lock(mixer_->mutex);
        if (mixer_->musicVoice >= 0 && mixer_->voices[mixer_->musicVoice].active &&
            currentMusic() == name) {
            mixer_->musicTarget = clamp01(volume);
            return true;
        }
    }

    const std::string resolved = resolveFile(name, true);
    if (resolved.empty()) {
        std::cerr << "Audio: music file not found: " << name
                  << " (expected assets/audio/music/" << name << ".wav/.ogg)" << std::endl;
        return false;
    }
    auto buffer = std::make_shared<AudioBuffer>(name, resolved);
    if (!buffer->load()) return false;

    std::lock_guard<std::mutex> lock(mixer_->mutex);
    // Music lives in the last voice slot: startVoice() never hands that slot
    // out (it skips music voices).
    const int slot = Mixer::kMaxVoices - 1;
    Voice& voice = mixer_->voices[slot];
    voice = Voice{};
    voice.active = true;
    voice.music = true;
    voice.positional = false;
    voice.samples = buffer->getSamplesPtr();
    voice.channels = std::max(buffer->getChannels(), 1);
    voice.sampleRate = std::max(buffer->getSampleRate(), 1);
    voice.pos = 0.0;
    voice.rate = 1.0f;
    mixer_->musicVoice = slot;
    mixer_->musicGain = 0.0f;               // fade in
    mixer_->musicTarget = clamp01(volume);
    {
        std::lock_guard<std::mutex> nameLock(nameMutex_);
        musicName_ = name;
    }
    return true;
}

void AudioManager::stopMusic() {
    if (mixer_) {
        std::lock_guard<std::mutex> lock(mixer_->mutex);
        if (mixer_->musicVoice >= 0) mixer_->voices[mixer_->musicVoice] = Voice{};
        mixer_->musicVoice = -1;
        mixer_->musicGain = 0.0f;
        mixer_->musicTarget = 0.0f;
    }
    std::lock_guard<std::mutex> lock(nameMutex_);
    musicName_.clear();
}

bool AudioManager::isMusicPlaying() const {
    if (!mixer_) return false;
    std::lock_guard<std::mutex> lock(mixer_->mutex);
    return mixer_->musicVoice >= 0 && mixer_->voices[mixer_->musicVoice].active;
}

std::string AudioManager::currentMusic() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return musicName_;
}

// ---------------------------------------------------------------------------
// Mixing (device thread)
// ---------------------------------------------------------------------------

float AudioManager::spatialGain(const glm::vec3& position) const {
    const float distance = glm::length(position - listenerPos_);
    const float range = maxDistance_ - minDistance_;
    switch (distanceModel_) {
        case DistanceModel::None:
            return 1.0f;
        case DistanceModel::Linear:
            if (distance <= minDistance_) return 1.0f;
            if (distance >= maxDistance_) return 0.0f;
            return 1.0f - (distance - minDistance_) / range;
        case DistanceModel::Inverse:
        default:
            if (distance >= maxDistance_ || range <= 0.0f) return 0.0f;
            if (distance <= minDistance_) return 1.0f;
            // 1/r falloff, faded out towards the edge of the range so a sound
            // never pops when it reaches maxDistance.
            return (minDistance_ / distance) * clamp01(1.0f - (distance - minDistance_) / range);
    }
}

void AudioManager::mix(float* out, unsigned int frames) {
    std::memset(out, 0, sizeof(float) * frames * 2);
    if (!mixer_) return;

    const int outRate = std::max(config.sampleRate, 1);
    const float master = clamp01(config.masterVolume);
    const float sfxVolume = master * clamp01(config.sfxVolume);
    const float musicVolume = master * clamp01(config.musicVolume);

    // Listener right vector for panning (the listener looks down -Z).
    glm::vec3 right = glm::cross(listenerForward_, listenerUp_);
    if (glm::length(right) < 1e-5f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    right = glm::normalize(right);

    const auto lockStart = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mixer_->mutex);

        // Music fade envelope for the whole block. A short fade-in avoids a
        // click at the start of a track and when switching tracks.
        if (mixer_->musicVoice >= 0) {
            const float target = mixer_->musicTarget;
            const float fadeSeconds = mixer_->musicGain <= 0.001f ? 0.12f : 0.35f;
            const float coeff = 1.0f - std::exp(-static_cast<float>(frames) / (outRate * fadeSeconds));
            mixer_->musicGain += (target - mixer_->musicGain) * coeff;
            if (std::abs(mixer_->musicGain - target) < 1e-4f) mixer_->musicGain = target;
        }

        for (int i = 0; i < Mixer::kMaxVoices; ++i) {
            Voice& voice = mixer_->voices[i];
            if (!voice.active || !voice.samples) continue;
            const std::vector<float>& data = *voice.samples;
            const int channels = std::max(voice.channels, 1);
            const long long totalFrames = static_cast<long long>(data.size()) / channels;
            if (totalFrames <= 0) { voice = Voice{}; continue; }

            const double step = static_cast<double>(voice.sampleRate) / static_cast<double>(outRate)
                                * voice.rate;

            float gain;
            float pan = 0.0f;
            if (voice.music) {
                gain = mixer_->musicGain * musicVolume;
            } else {
                gain = voice.volume * sfxVolume;
                if (voice.positional) {
                    gain *= spatialGain(voice.position);
                    const glm::vec3 toSource = voice.position - listenerPos_;
                    const float dist = glm::length(toSource);
                    if (dist > 1e-3f) {
                        // Only pan once the sound is a few metres away, so
                        // close sounds (the player's own gun) stay centred.
                        pan = glm::dot(toSource / dist, right) * std::min(1.0f, dist / 6.0f) * 0.9f;
                    }
                    pan = std::min(1.0f, std::max(-1.0f, pan));
                }
            }

            // Constant-power pan: a centred source is heard equally in both
            // ears at full level, hard left/right goes to one ear.
            const float angle = (pan * 0.5f + 0.5f) * 0.5f * kPi;   // 0..pi/2
            const float kSqrt2 = 1.41421356f;
            const float leftGain = std::cos(angle) * kSqrt2;
            const float rightGain = std::sin(angle) * kSqrt2;

            const int srcChannels = channels;
            const bool stereoSource = !voice.music && channels >= 2;
            for (unsigned int f = 0; f < frames; ++f) {
                if (voice.pos >= static_cast<double>(totalFrames)) {
                    voice = Voice{};
                    break;
                }
                long long frame = static_cast<long long>(voice.pos);
                float l = 0.0f, r = 0.0f;
                if (srcChannels == 1) {
                    l = r = data[frame];
                } else {
                    const long long base = frame * srcChannels;
                    l = data[base + 0];
                    r = data[base + (srcChannels > 1 ? 1 : 0)];
                    if (srcChannels > 2) {
                        // Downmix the extra channels into both sides.
                        for (int c = 2; c < srcChannels; ++c) {
                            l += data[base + c] * 0.5f;
                            r += data[base + c] * 0.5f;
                        }
                    }
                }
                voice.pos += step;

                if (voice.music) {
                    out[f * 2 + 0] += l * gain;
                    out[f * 2 + 1] += r * gain;
                } else if (stereoSource) {
                    out[f * 2 + 0] += l * gain;
                    out[f * 2 + 1] += r * gain;
                } else {
                    out[f * 2 + 0] += l * gain * leftGain;
                    out[f * 2 + 1] += r * gain * rightGain;
                }
            }
        }
    }
    const double lockSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - lockStart).count();
    double previous = maxMixLockSeconds_.load();
    while (lockSeconds > previous &&
           !maxMixLockSeconds_.compare_exchange_weak(previous, lockSeconds)) {}

    // Soft limiter: overlapping sounds saturate gently instead of clipping.
    for (unsigned int f = 0; f < frames * 2; ++f) {
        out[f] = std::tanh(out[f]);
    }
}

// ---------------------------------------------------------------------------
// AudioBuffer
// ---------------------------------------------------------------------------

AudioBuffer::AudioBuffer(const std::string& name, const std::string& path) {
    this->name = name;
    this->path = path;
    bufferId = 0;
    channels = 0;
    sampleRate = 0;
    size = 0;
    isLoaded = false;
}

AudioBuffer::~AudioBuffer() {
    unload();
}

bool AudioBuffer::load() {
    if (isLoaded) return true;

    ma_uint64 frames = 0;
    void* pcm = nullptr;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    const ma_result rc = ma_decode_file(path.c_str(), &cfg, &frames, &pcm);
    if (rc != MA_SUCCESS || !pcm || frames == 0) {
        if (pcm) ma_free(pcm, nullptr);
        std::cerr << "Audio: could not decode " << path
                  << " (supported: .wav .ogg .flac .mp3)" << std::endl;
        return false;
    }

    // Channel count and rate of the decoded data.
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) {
        ma_free(pcm, nullptr);
        std::cerr << "Audio: could not read " << path << std::endl;
        return false;
    }
    channels = static_cast<int>(decoder.outputChannels);
    sampleRate = static_cast<int>(decoder.outputSampleRate);
    ma_decoder_uninit(&decoder);
    if (channels <= 0 || sampleRate <= 0) {
        ma_free(pcm, nullptr);
        return false;
    }

    samples = std::make_shared<std::vector<float>>(
        static_cast<float*>(pcm), static_cast<float*>(pcm) + frames * channels);
    ma_free(pcm, nullptr);
    size = static_cast<int>(frames);
    bufferId = static_cast<unsigned int>(
        std::hash<std::string>{}(path) & 0x7fffffffU);
    isLoaded = true;
    return true;
}

void AudioBuffer::unload() {
    isLoaded = false;
    size = 0;
    channels = 0;
    sampleRate = 0;
    samples.reset();
}

const std::vector<float>& AudioBuffer::getSamples() const {
    static const std::vector<float> empty;
    return samples ? *samples : empty;
}

// ---------------------------------------------------------------------------
// AudioSource (old stub API)
// ---------------------------------------------------------------------------

AudioSource::AudioSource() = default;
AudioSource::~AudioSource() = default;

bool AudioSource::initialize() { return true; }
void AudioSource::shutdown() {}

void AudioSource::play() {
    isPlaying = true;
    isPaused = false;
    ++startSerial;
}
void AudioSource::pause() { isPaused = true; }
void AudioSource::stop() {
    isPlaying = false;
    isPaused = false;
}
void AudioSource::rewind() {
    // Restart from the beginning on the next play().
    if (isPlaying) {
        isPlaying = false;
    }
}

void AudioSource::update(float /*deltaTime*/) {
    // Playback state is tracked by the manager (AudioManager::collectSources).
}

void AudioSource::setBuffer(std::shared_ptr<AudioBuffer> newBuffer) {
    buffer = std::move(newBuffer);
}

void AudioSource::setVolume(float vol) {
    volume = std::min(std::max(vol, 0.0f), 1.0f);
}
