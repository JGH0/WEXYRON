/**
 * @file gltf_json.hpp
 * @brief Minimal, dependency-free JSON reader used by the glTF loader.
 *
 * The project intentionally has no third-party JSON dependency. glTF only
 * needs a small, well-defined subset of JSON (objects, arrays, strings,
 * numbers, booleans and null), so this header implements exactly that with a
 * recursive-descent parser. It is internal to the engine and not installed as
 * a public header.
 */

#ifndef CUBE_ENGINE_GLTF_JSON_HPP
#define CUBE_ENGINE_GLTF_JSON_HPP

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace gltfjson {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    double asDouble(double fallback = 0.0) const {
        return type_ == Type::Number ? number_ : fallback;
    }
    float asFloat(float fallback = 0.0f) const {
        return type_ == Type::Number ? static_cast<float>(number_) : fallback;
    }
    int asInt(int fallback = 0) const {
        return type_ == Type::Number ? static_cast<int>(number_) : fallback;
    }
    const std::string& asString() const {
        static const std::string empty;
        return type_ == Type::String ? string_ : empty;
    }
    std::string asStringOr(const std::string& fallback) const {
        return type_ == Type::String ? string_ : fallback;
    }

    std::size_t size() const {
        if (type_ == Type::Array) return array_.size();
        if (type_ == Type::Object) return object_.size();
        return 0;
    }

    const Value& at(std::size_t index) const {
        static const Value nullValue;
        if (type_ != Type::Array || index >= array_.size()) return nullValue;
        return array_[index];
    }

    const Value* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        for (const auto& entry : object_) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }

    bool has(const std::string& key) const { return find(key) != nullptr; }

    const Value& operator[](const std::string& key) const {
        const Value* found = find(key);
        static const Value nullValue;
        return found ? *found : nullValue;
    }

    const std::vector<Value>& array() const { return array_; }
    const std::vector<std::pair<std::string, Value>>& object() const { return object_; }

    static bool parse(const std::string& text, Value& out, std::string& error);

private:
    friend class Parser;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

class Parser {
public:
    Parser(const char* begin, const char* end) : begin_(begin), cursor_(begin), end_(end) {}

    bool parseDocument(Value& out, std::string& error) {
        skipWhitespace();
        if (!parseValue(out, 0)) {
            error = error_.empty() ? "invalid JSON" : error_;
            return false;
        }
        skipWhitespace();
        if (cursor_ != end_) {
            error = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    const char* begin_;
    const char* cursor_;
    const char* end_;
    std::string error_;

    void skipWhitespace() {
        while (cursor_ < end_) {
            const unsigned char c = static_cast<unsigned char>(*cursor_);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++cursor_;
            } else {
                break;
            }
        }
    }

    bool fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > 256) return fail("JSON nesting too deep");
        skipWhitespace();
        if (cursor_ >= end_) return fail("unexpected end of JSON");

        const char c = *cursor_;
        switch (c) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                out.type_ = Value::Type::String;
                return parseString(out.string_);
            }
            case 't':
            case 'f':
            case 'n': return parseLiteral(out);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
                return fail("unexpected character in JSON");
        }
    }

    bool parseLiteral(Value& out) {
        auto matches = [&](const char* text) {
            const char* p = cursor_;
            while (*text) {
                if (p >= end_ || *p != *text) return false;
                ++p;
                ++text;
            }
            cursor_ = p;
            return true;
        };
        if (matches("true")) { out.type_ = Value::Type::Bool; out.bool_ = true; return true; }
        if (matches("false")) { out.type_ = Value::Type::Bool; out.bool_ = false; return true; }
        if (matches("null")) { out.type_ = Value::Type::Null; return true; }
        return fail("invalid JSON literal");
    }

    bool parseNumber(Value& out) {
        const char* start = cursor_;
        if (cursor_ < end_ && *cursor_ == '-') ++cursor_;
        while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') ++cursor_;
        if (cursor_ < end_ && *cursor_ == '.') {
            ++cursor_;
            while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') ++cursor_;
        }
        if (cursor_ < end_ && (*cursor_ == 'e' || *cursor_ == 'E')) {
            ++cursor_;
            if (cursor_ < end_ && (*cursor_ == '+' || *cursor_ == '-')) ++cursor_;
            while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') ++cursor_;
        }
        if (cursor_ == start) return fail("invalid JSON number");

        std::string token(start, cursor_);
        char* parsedEnd = nullptr;
        const double value = std::strtod(token.c_str(), &parsedEnd);
        if (!parsedEnd || parsedEnd == token.c_str()) return fail("invalid JSON number");
        out.type_ = Value::Type::Number;
        out.number_ = value;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int cp) {
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }

    bool parseHex4(unsigned int& out) {
        if (end_ - cursor_ < 4) return fail("truncated \\u escape");
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *cursor_++;
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else return fail("invalid \\u escape");
        }
        out = value;
        return true;
    }

    bool parseString(std::string& out) {
        out.clear();
        if (cursor_ >= end_ || *cursor_ != '"') return fail("expected string");
        ++cursor_;
        while (cursor_ < end_) {
            const char c = *cursor_++;
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (cursor_ >= end_) return fail("unterminated escape");
            const char esc = *cursor_++;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        // High surrogate: combine with a following low surrogate.
                        if (end_ - cursor_ >= 6 && cursor_[0] == '\\' && cursor_[1] == 'u') {
                            cursor_ += 2;
                            unsigned int low = 0;
                            if (!parseHex4(low)) return false;
                            if (low >= 0xdc00 && low <= 0xdfff) {
                                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                            } else {
                                appendUtf8(out, 0xfffd);
                                cp = low;
                            }
                        } else {
                            cp = 0xfffd;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape character");
            }
        }
        return fail("unterminated string");
    }

    bool parseArray(Value& out, int depth) {
        out.type_ = Value::Type::Array;
        out.array_.clear();
        ++cursor_;  // [
        skipWhitespace();
        if (cursor_ < end_ && *cursor_ == ']') { ++cursor_; return true; }
        while (true) {
            Value element;
            if (!parseValue(element, depth + 1)) return false;
            out.array_.push_back(std::move(element));
            skipWhitespace();
            if (cursor_ >= end_) return fail("unterminated array");
            if (*cursor_ == ',') { ++cursor_; skipWhitespace(); continue; }
            if (*cursor_ == ']') { ++cursor_; return true; }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parseObject(Value& out, int depth) {
        out.type_ = Value::Type::Object;
        out.object_.clear();
        ++cursor_;  // {
        skipWhitespace();
        if (cursor_ < end_ && *cursor_ == '}') { ++cursor_; return true; }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (cursor_ >= end_ || *cursor_ != ':') return fail("expected ':' in object");
            ++cursor_;
            Value value;
            if (!parseValue(value, depth + 1)) return false;
            out.object_.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (cursor_ >= end_) return fail("unterminated object");
            if (*cursor_ == ',') { ++cursor_; skipWhitespace(); continue; }
            if (*cursor_ == '}') { ++cursor_; return true; }
            return fail("expected ',' or '}' in object");
        }
    }
};

inline bool Value::parse(const std::string& text, Value& out, std::string& error) {
    Parser parser(text.data(), text.data() + text.size());
    return parser.parseDocument(out, error);
}

}  // namespace gltfjson

#endif  // CUBE_ENGINE_GLTF_JSON_HPP
