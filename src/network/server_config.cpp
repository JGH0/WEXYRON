/**
 * @file server_config.cpp
 * @brief Minimal JSON reader for server_config.json (see server_config.hpp)
 *
 * Supports the subset the file uses: objects, arrays, strings, numbers,
 * true/false/null and // / # line comments (hand-edited files are friendlier
 * with comments). Nested objects are flattened to "section.key"; arrays are
 * stored as their raw text and are not used by the current settings.
 */

#include "server_config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

/// Recursive-descent reader over one JSON text.
class Reader {
public:
    explicit Reader(const std::string& text) : text_(text) {}

    bool parseDocument(std::map<std::string, std::string>& out) {
        skipSpace();
        if (!parseValue("", out, 0)) return false;
        skipSpace();
        return pos_ == text_.size();
    }

    const std::string& error() const { return error_; }

private:
    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;

    void skipSpace() {
        for (;;) {
            while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
            if (pos_ + 1 < text_.size() && text_[pos_] == '/' && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '#') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    bool fail(const std::string& what) {
        if (error_.empty()) {
            size_t line = 1;
            for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
                if (text_[i] == '\n') ++line;
            }
            error_ = what + " (line " + std::to_string(line) + ")";
        }
        return false;
    }

    bool parseString(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected a string");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= text_.size()) break;
            const char esc = text_[pos_++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    // Keep non-ASCII escapes as their UTF-8 bytes (BMP only).
                    if (pos_ + 4 > text_.size()) return fail("bad \\u escape");
                    const unsigned code = static_cast<unsigned>(
                        std::strtoul(text_.substr(pos_, 4).c_str(), nullptr, 16));
                    pos_ += 4;
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return fail("bad escape in string");
            }
        }
        return fail("unterminated string");
    }

    /// A number / literal token (true, false, null, 12, -1.5).
    bool parseToken(std::string& out) {
        const size_t start = pos_;
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) &&
               text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']') {
            ++pos_;
        }
        if (pos_ == start) return fail("expected a value");
        out = text_.substr(start, pos_ - start);
        return true;
    }

    /// Parse one value. Objects are flattened using `prefix` ("server.port").
    bool parseValue(const std::string& prefix, std::map<std::string, std::string>& out, int depth) {
        if (depth > 16) return fail("nesting too deep");
        skipSpace();
        if (pos_ >= text_.size()) return fail("unexpected end of file");

        if (text_[pos_] == '{') {
            ++pos_;
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
            for (;;) {
                skipSpace();
                std::string key;
                if (!parseString(key)) return false;
                skipSpace();
                if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
                ++pos_;
                const std::string child = prefix.empty() ? key : prefix + "." + key;
                if (!parseValue(child, out, depth + 1)) return false;
                skipSpace();
                if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
                return fail("expected ',' or '}'");
            }
        }

        if (text_[pos_] == '[') {
            // Arrays are kept as raw text (none of the settings use them).
            const size_t start = pos_;
            int level = 0;
            bool inString = false;
            for (; pos_ < text_.size(); ++pos_) {
                const char c = text_[pos_];
                if (inString) {
                    if (c == '\\') { ++pos_; continue; }
                    if (c == '"') inString = false;
                } else if (c == '"') {
                    inString = true;
                } else if (c == '[') {
                    ++level;
                } else if (c == ']') {
                    if (--level == 0) { ++pos_; break; }
                }
            }
            if (level != 0) return fail("unterminated array");
            if (!prefix.empty()) out[prefix] = text_.substr(start, pos_ - start);
            return true;
        }

        std::string value;
        if (text_[pos_] == '"') {
            if (!parseString(value)) return false;
            if (!prefix.empty()) out[prefix] = value;
            return true;
        }
        if (!parseToken(value)) return false;
        if (!prefix.empty() && value != "null") out[prefix] = value;
        return true;
    }
};

} // namespace

bool ServerConfig::load(const std::string& path) {
    path_ = path;
    values_.clear();

    std::ifstream in(path);
    if (!in) {
        // Not an error on its own: the caller decides how loud to be.
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    Reader reader(text);
    if (!reader.parseDocument(values_)) {
        std::cerr << "Invalid JSON in " << path << ": " << reader.error()
                  << " - using defaults." << std::endl;
        values_.clear();
        return false;
    }
    return true;
}

bool ServerConfig::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string ServerConfig::raw(const std::string& key) const {
    auto it = values_.find(key);
    return it == values_.end() ? std::string() : it->second;
}

std::string ServerConfig::getString(const std::string& key, const std::string& fallback) const {
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

int ServerConfig::getInt(const std::string& key, int fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const std::string& text = it->second;

    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || end == text.c_str()) return fallback;
    while (*end == ' ') ++end;
    if (*end == '.') {
        // Accept "2.0" (hand-edited config); reject "2.5".
        for (const char* p = end + 1; *p; ++p) {
            if (*p != '0' && *p != ' ') return fallback;
        }
    } else if (*end != '\0') {
        return fallback;
    }
    return static_cast<int>(value);
}

double ServerConfig::getDouble(const std::string& key, double fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(it->second.c_str(), &end);
    if (!end || end == it->second.c_str()) return fallback;
    while (*end == ' ') ++end;
    if (*end != '\0') return fallback;
    return value;
}

bool ServerConfig::getBool(const std::string& key, bool fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    std::string text = it->second;
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text == "true" || text == "1" || text == "yes") return true;
    if (text == "false" || text == "0" || text == "no") return false;
    return fallback;
}
