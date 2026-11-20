/**
 * @file server_config.hpp
 * @brief server_config.json loader for the standalone server
 *
 * The JSON file is a plain nested object of scalars:
 *
 *   {
 *     "server": {
 *       "name": "WEXYRON Server",
 *       "port": 7777,
 *       "maxPlayers": 16,
 *       "maxPlayersPerMachine": 2,
 *       ...
 *     },
 *     ...
 *   }
 *
 * Only the flat keys `section key` are needed, so a small purpose-built
 * parser is used instead of pulling in a JSON library for the server alone.
 * Every value is optional: missing or unreadable ones keep their default.
 *
 * Precedence (highest first):
 *   1. command line argument (e.g. `./server 7778` sets the port)
 *   2. environment variable (docker-compose / .env: MAX_PLAYERS_PER_MACHINE)
 *   3. server_config.json (`server.maxPlayersPerMachine`)
 *   4. built-in default
 */

#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>
#include <map>

/// Flat "section.key" -> raw JSON scalar text, plus the accessor helpers.
class ServerConfig {
public:
    /// Read `path`. Returns false (and leaves the object empty) when the file
    /// is missing or not valid JSON; the defaults then apply.
    bool load(const std::string& path);

    /// True when `section.key` is present.
    bool has(const std::string& key) const;

    /// Raw value text (empty when absent).
    std::string raw(const std::string& key) const;

    std::string getString(const std::string& key, const std::string& fallback) const;
    /// Integer value; `fallback` when absent or not a number (a fractional
    /// number is truncated, as the config is hand-edited).
    int getInt(const std::string& key, int fallback) const;
    double getDouble(const std::string& key, double fallback) const;
    bool getBool(const std::string& key, bool fallback) const;

    const std::string& path() const { return path_; }
    size_t size() const { return values_.size(); }

private:
    std::string path_;
    std::map<std::string, std::string> values_;   ///< "section.key" -> text
};

#endif // SERVER_CONFIG_HPP
