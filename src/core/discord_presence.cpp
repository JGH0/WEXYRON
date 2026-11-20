/**
 * @file discord_presence.cpp
 * @brief Discord Rich Presence implementation (see discord_presence.hpp)
 */

#include "discord_presence.hpp"
#include "brand.hpp"

#include "discord_rpc.h"

#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace discord_presence {

namespace {

struct Activity {
    bool valid = false;
    std::string details;
    std::string state;
    std::string largeImage;
    std::string largeText;
    std::string smallImage;
    std::string smallText;

    bool operator==(const Activity& other) const {
        return valid == other.valid && details == other.details &&
               state == other.state && largeImage == other.largeImage &&
               largeText == other.largeText && smallImage == other.smallImage &&
               smallText == other.smallText;
    }
};

std::mutex g_mutex;
bool g_enabled = true;
bool g_initialized = false;
int64_t g_startTimestamp = 0;
Activity g_wanted;
Activity g_lastSent;
bool g_hasSent = false;
bool g_loggedUnavailable = false;

void applyPresenceLocked();

void onReady(const DiscordUser* user) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_loggedUnavailable = false;
    std::cout << "[discord] Rich Presence connected"
              << (user && user->username ? std::string(" as ") + user->username : std::string())
              << std::endl;
    applyPresenceLocked();
}

void onDisconnected(int errorCode, const char* message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hasSent = false;
    if (message && message[0]) {
        std::cout << "[discord] disconnected (" << errorCode << "): " << message << std::endl;
    } else {
        std::cout << "[discord] disconnected (" << errorCode << ")" << std::endl;
    }
}

void onErrored(int errorCode, const char* message) {
    // Discord not running is normal. Log the first error only so a machine
    // without Discord does not fill the console every frame.
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loggedUnavailable) return;
    g_loggedUnavailable = true;
    std::cout << "[discord] unavailable (" << errorCode << ")"
              << (message && message[0] ? std::string(": ") + message : std::string())
              << " - Rich Presence disabled until Discord is available" << std::endl;
}

void initializeLocked() {
    if (!g_enabled || g_initialized) return;

    g_startTimestamp = static_cast<int64_t>(std::time(nullptr));

    DiscordEventHandlers handlers{};
    handlers.ready = &onReady;
    handlers.disconnected = &onDisconnected;
    handlers.errored = &onErrored;

    // autoRegister = 0: Rich Presence only. We do not need to add a registry
    // entry for the executable or a Steam game id.
    Discord_Initialize(brand::kDiscordApplicationId, &handlers, 0, nullptr);
    g_initialized = true;
    g_hasSent = false;
    std::cout << "[discord] Rich Presence client started (app "
              << brand::kDiscordApplicationId << ")" << std::endl;
}

void applyPresenceLocked() {
    if (!g_initialized || !g_enabled) return;
    if (!g_wanted.valid) {
        if (g_hasSent) {
            Discord_ClearPresence();
            g_hasSent = false;
            g_lastSent = Activity{};
        }
        return;
    }
    if (g_hasSent && g_wanted == g_lastSent) return;

    DiscordRichPresence presence{};
    presence.details = g_wanted.details.c_str();
    presence.state = g_wanted.state.c_str();
    presence.startTimestamp = g_startTimestamp;
    presence.largeImageKey = g_wanted.largeImage.empty() ? nullptr : g_wanted.largeImage.c_str();
    presence.largeImageText = g_wanted.largeText.empty() ? nullptr : g_wanted.largeText.c_str();
    presence.smallImageKey = g_wanted.smallImage.empty() ? nullptr : g_wanted.smallImage.c_str();
    presence.smallImageText = g_wanted.smallText.empty() ? nullptr : g_wanted.smallText.c_str();
    presence.instance = 0;

    // The library serialises the strings immediately, so the pointers only
    // have to live for this call.
    Discord_UpdatePresence(&presence);
    g_lastSent = g_wanted;
    g_hasSent = true;
}

} // namespace

void setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_enabled = enabled;
    if (!enabled) {
        if (g_initialized) {
            Discord_ClearPresence();
            Discord_Shutdown();
            g_initialized = false;
                    g_hasSent = false;
            g_lastSent = Activity{};
        }
        g_loggedUnavailable = false;
        return;
    }
    initializeLocked();
    applyPresenceLocked();
}

bool isEnabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_enabled;
}

void setActivity(const std::string& details,
                 const std::string& state,
                 const std::string& largeImage,
                 const std::string& largeText,
                 const std::string& smallImage,
                 const std::string& smallText) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Activity next;
    next.valid = true;
    next.details = details;
    next.state = state;
    next.largeImage = largeImage;
    next.largeText = largeText;
    next.smallImage = smallImage;
    next.smallText = smallText;

    if (g_wanted.valid && g_wanted == next) {
        // Still make sure the client is running (first call may have happened
        // before the user started Discord).
        initializeLocked();
        applyPresenceLocked();
        return;
    }
    g_wanted = std::move(next);
    initializeLocked();
    applyPresenceLocked();
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted = Activity{};
    if (g_initialized) {
        Discord_ClearPresence();
        g_hasSent = false;
        g_lastSent = Activity{};
    }
}

void poll() {
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        initialized = g_initialized;
    }
    // Discord_RunCallbacks invokes onReady/onDisconnected, which take the
    // mutex, so it must be called outside the lock.
    if (initialized) Discord_RunCallbacks();
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        Discord_ClearPresence();
        Discord_Shutdown();
        g_initialized = false;
    }
    g_hasSent = false;
    g_wanted = Activity{};
    g_lastSent = Activity{};
    g_enabled = false;
}

} // namespace discord_presence
