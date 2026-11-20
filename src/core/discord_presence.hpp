/**
 * @file discord_presence.hpp
 * @brief Discord Rich Presence wrapper (local IPC, no bot token)
 *
 * The game only needs the public Discord Application ID. Presence is optional:
 * when Discord is not running (or the user turns the setting off) every call
 * becomes a no-op. See third_party/discord-rpc/LICENSE.
 */

#ifndef DISCORD_PRESENCE_HPP
#define DISCORD_PRESENCE_HPP

#include <string>

namespace discord_presence {

/// Enable / disable the integration. Safe to call repeatedly; enabling
/// starts the local Discord IPC client, disabling clears the status.
void setEnabled(bool enabled);

/// True when the integration is enabled (Discord may still be closed).
bool isEnabled();

/// Publish the current activity. Repeated identical calls are ignored.
/// Image keys must exist under Rich Presence -> Art Assets in the Discord
/// Developer Portal; unknown keys simply leave the image empty.
void setActivity(const std::string& details,
                 const std::string& state,
                 const std::string& largeImage = "logo",
                 const std::string& largeText = "WEXYRON",
                 const std::string& smallImage = "",
                 const std::string& smallText = "");

/// Remove the activity from the profile (used when disabling/at shutdown).
void clear();

/// Process Discord callbacks. Call once per frame on the main thread.
void poll();

/// Clear the presence and stop the IPC client. Idempotent.
void shutdown();

} // namespace discord_presence

#endif // DISCORD_PRESENCE_HPP
