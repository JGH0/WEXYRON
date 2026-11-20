#include "settings.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <deque>
#include <sstream>

namespace settings {

namespace {
/// std::clamp replacement (the CMake build uses C++11).
inline int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
} // namespace

// ---------------------------------------------------------------------------
// Key / button names
// ---------------------------------------------------------------------------

std::string keyName(int key) {
    if (key < 0) return "NONE";
    if (key >= GLFW_KEY_LAST) return "UNKNOWN";

    // GLFW has no printable name for these, so name them by hand.
    switch (key) {
        case GLFW_KEY_UNKNOWN:      return "UNKNOWN";
        case GLFW_KEY_SPACE:        return "SPACE";
        case GLFW_KEY_APOSTROPHE:   return "'";
        case GLFW_KEY_COMMA:        return ",";
        case GLFW_KEY_MINUS:        return "-";
        case GLFW_KEY_PERIOD:       return ".";
        case GLFW_KEY_SLASH:        return "/";
        case GLFW_KEY_0:            return "0";
        case GLFW_KEY_1:            return "1";
        case GLFW_KEY_2:            return "2";
        case GLFW_KEY_3:            return "3";
        case GLFW_KEY_4:            return "4";
        case GLFW_KEY_5:            return "5";
        case GLFW_KEY_6:            return "6";
        case GLFW_KEY_7:            return "7";
        case GLFW_KEY_8:            return "8";
        case GLFW_KEY_9:            return "9";
        case GLFW_KEY_SEMICOLON:    return ";";
        case GLFW_KEY_EQUAL:        return "=";
        case GLFW_KEY_LEFT_BRACKET: return "[";
        case GLFW_KEY_BACKSLASH:    return "\\";
        case GLFW_KEY_RIGHT_BRACKET:return "]";
        case GLFW_KEY_GRAVE_ACCENT: return "`";
        case GLFW_KEY_ESCAPE:       return "ESC";
        case GLFW_KEY_ENTER:        return "ENTER";
        case GLFW_KEY_KP_ENTER:     return "KP ENTER";
        case GLFW_KEY_TAB:          return "TAB";
        case GLFW_KEY_BACKSPACE:    return "BACKSPACE";
        case GLFW_KEY_INSERT:       return "INSERT";
        case GLFW_KEY_DELETE:       return "DELETE";
        case GLFW_KEY_RIGHT:        return "RIGHT";
        case GLFW_KEY_LEFT:         return "LEFT";
        case GLFW_KEY_DOWN:         return "DOWN";
        case GLFW_KEY_UP:           return "UP";
        case GLFW_KEY_PAGE_UP:      return "PAGE UP";
        case GLFW_KEY_PAGE_DOWN:    return "PAGE DOWN";
        case GLFW_KEY_HOME:         return "HOME";
        case GLFW_KEY_END:          return "END";
        case GLFW_KEY_CAPS_LOCK:    return "CAPS LOCK";
        case GLFW_KEY_SCROLL_LOCK:  return "SCROLL LOCK";
        case GLFW_KEY_NUM_LOCK:     return "NUM LOCK";
        case GLFW_KEY_PRINT_SCREEN: return "PRINT SCREEN";
        case GLFW_KEY_PAUSE:        return "PAUSE";
        case GLFW_KEY_LEFT_SHIFT:   return "LEFT SHIFT";
        case GLFW_KEY_LEFT_CONTROL: return "LEFT CTRL";
        case GLFW_KEY_LEFT_ALT:     return "LEFT ALT";
        case GLFW_KEY_LEFT_SUPER:   return "LEFT SUPER";
        case GLFW_KEY_RIGHT_SHIFT:  return "RIGHT SHIFT";
        case GLFW_KEY_RIGHT_CONTROL:return "RIGHT CTRL";
        case GLFW_KEY_RIGHT_ALT:    return "RIGHT ALT";
        case GLFW_KEY_RIGHT_SUPER:  return "RIGHT SUPER";
        case GLFW_KEY_MENU:         return "MENU";
        case GLFW_KEY_KP_0:         return "KP 0";
        case GLFW_KEY_KP_1:         return "KP 1";
        case GLFW_KEY_KP_2:         return "KP 2";
        case GLFW_KEY_KP_3:         return "KP 3";
        case GLFW_KEY_KP_4:         return "KP 4";
        case GLFW_KEY_KP_5:         return "KP 5";
        case GLFW_KEY_KP_6:         return "KP 6";
        case GLFW_KEY_KP_7:         return "KP 7";
        case GLFW_KEY_KP_8:         return "KP 8";
        case GLFW_KEY_KP_9:         return "KP 9";
        case GLFW_KEY_KP_DECIMAL:   return "KP .";
        case GLFW_KEY_KP_DIVIDE:    return "KP /";
        case GLFW_KEY_KP_MULTIPLY:  return "KP *";
        case GLFW_KEY_KP_SUBTRACT:  return "KP -";
        case GLFW_KEY_KP_ADD:       return "KP +";
        case GLFW_KEY_KP_EQUAL:     return "KP =";
        default: break;
    }

    // Printable keys (letters, digits, punctuation). GLFW names them for the
    // current keyboard layout, but some backends/platforms have no key names
    // for them (glfwGetKeyName returns NULL), so fall back to the US layout
    // the GLFW key constants are numbered from.
    const char* name = glfwGetKeyName(key, 0);
    if (name && name[0]) {
        std::string out(name);
        if (out.size() == 1) out[0] = static_cast<char>(toupper(static_cast<unsigned char>(out[0])));
        return out;
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return std::string(1, static_cast<char>('A' + (key - GLFW_KEY_A)));
    }
    // Every branch below returns a fresh string: a function-local buffer would
    // be shared by all callers, and building a std::string from it while
    // another thread calls keyName() can read freed memory.
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
        return "KP " + std::to_string(key - GLFW_KEY_KP_0);
    }
    // Other printable keys already have a fixed name in the switch above.

    // Function keys and anything else without a name.
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) {
        return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    }
    return "KEY " + std::to_string(key);
}

std::string mouseButtonName(int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:   return "LEFT MOUSE";
        case GLFW_MOUSE_BUTTON_RIGHT:  return "RIGHT MOUSE";
        case GLFW_MOUSE_BUTTON_MIDDLE: return "MIDDLE MOUSE";
        default: break;
    }
    return "MOUSE " + std::to_string(button + 1);
}

std::string bindingName(int code) {
    const int button = mouseButtonOf(code);
    return button >= 0 ? mouseButtonName(button) : keyName(code);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

static SettingDef makeDef(std::string key, std::string label, std::string group,
                          std::string help, Value* value) {
    SettingDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.group = std::move(group);
    d.help = std::move(help);
    d.value = value;
    return d;
}

namespace {

struct BoolMember { bool GameSettings::*ptr; };
struct IntMember  { int  GameSettings::*ptr; ValueType type; };

/// How one registry row reads/writes its GameSettings member.
struct RowAdapter {
    std::function<void(GameSettings&, const Value&)> applyTo;
    std::function<Value(const GameSettings&)> getFrom;
};

/// Storage for the setting values the menu edits. SettingDef::value points
/// into `values`, so the two vectors must never reallocate after the defs are
/// built - reserve() covers that for the life of the process.
///
/// There is exactly one registry per process (the game's settings). Building
/// it twice while the first defs are alive would invalidate them, so the
/// second call returns the defs that already exist.
struct RegistryStorage {
    std::vector<RowAdapter> adapters;
    std::vector<Value> values;
    std::deque<SettingDef> defs;      ///< stable addresses for the pages
    bool built = false;
};
RegistryStorage& storage() {
    static RegistryStorage s;
    return s;
}

} // namespace

void syncSettingDefs(GameSettings& s, std::vector<SettingDef>& defs) {
    for (SettingDef& def : defs) {
        if (def.get) *def.value = def.get(s);
    }
}

void applySettingDefs(GameSettings& s, std::vector<SettingDef>& defs) {
    // File -> GameSettings: write every row's value into its member.
    for (SettingDef& def : defs) {
        if (def.apply) def.apply(*def.value);
    }
}

const std::vector<SettingDef>& gameSettingDefs() {
    static std::vector<SettingDef> copy;
    static size_t builtCount = static_cast<size_t>(-1);
    RegistryStorage& store = storage();
    if (copy.size() != store.defs.size() || builtCount != store.defs.size()) {
        copy.assign(store.defs.begin(), store.defs.end());
        builtCount = store.defs.size();
    }
    return copy;
}

void applySettingDef(size_t index) {
    RegistryStorage& store = storage();
    if (index >= store.defs.size()) return;
    const SettingDef& def = store.defs[index];
    if (def.apply) def.apply(*def.value);
}

void setChoiceIndex(Value& v, int index) {
    v.i = clampInt(index, 0, 2);
}

void setKeyValue(Value& v, int code) {
    v.i = code;
}

void syncSettingDefs(GameSettings& s) {
    // GameSettings -> rows (after a reset).
    RegistryStorage& store = storage();
    for (SettingDef& def : store.defs) {
        if (def.get) *def.value = def.get(s);
    }
}

bool setSettingFromText(size_t index, const std::string& text) {
    RegistryStorage& store = storage();
    if (index >= store.defs.size()) return false;
    const SettingDef& def = store.defs[index];
    const std::string old = serializeValue(def);
    if (!parseValue(def, text)) return false;
    return serializeValue(def) != old || text != old;
}

void applySettingDefs(GameSettings& s) {
    RegistryStorage& store = storage();
    for (SettingDef& def : store.defs) {
        if (def.apply) def.apply(*def.value);
    }
}

std::vector<SettingDef> makeGameSettingDefs(GameSettings& s) {
    RegistryStorage& store = storage();
    if (store.built) return {store.defs.begin(), store.defs.end()};

    store.adapters.clear();
    store.values.clear();
    // SettingDef::value points into store.values, so the vector must never
    // reallocate after the defs are built. Keep this comfortably above the
    // number of rows (add new settings below and bump it if needed).
    store.adapters.reserve(32);
    store.values.reserve(32);

    std::vector<SettingDef> defs;
    defs.reserve(32);

    auto push = [&](const char* key, const char* label, const char* group,
                    const char* help, const Value& initial, RowAdapter adapter) {
        store.values.push_back(initial);
        store.adapters.push_back(std::move(adapter));
        SettingDef def = makeDef(key, label, group, help, &store.values.back());
        const size_t index = store.values.size() - 1;
        def.apply = [&s, index](const Value& v) { storage().adapters[index].applyTo(s, v); };
        def.get = [&s, index](const GameSettings&) { return storage().adapters[index].getFrom(s); };
        defs.push_back(std::move(def));
    };

    auto addBool = [&](const char* key, const char* label, const char* group,
                       const char* help, bool GameSettings::*member) {
        const BoolMember m{member};
        RowAdapter a;
        a.applyTo = [m](GameSettings& g, const Value& v) {
            if (v.type == ValueType::Bool) g.*(m.ptr) = v.b;
        };
        a.getFrom = [m](const GameSettings& g) {
            return Value{ValueType::Bool, g.*(m.ptr), 0};
        };
        push(key, label, group, help, a.getFrom(s), a);
    };

    auto addInt = [&](const char* key, const char* label, const char* group,
                      const char* help, ValueType type, int GameSettings::*member) {
        const IntMember m{member, type};
        RowAdapter a;
        a.applyTo = [m](GameSettings& g, const Value& v) {
            if (v.type == m.type) g.*(m.ptr) = v.i;
        };
        a.getFrom = [m](const GameSettings& g) {
            return Value{m.type, false, g.*(m.ptr)};
        };
        push(key, label, group, help, a.getFrom(s), a);
    };

    // Keys use the same integer storage as Int rows, with the Key value type.
    auto addKey = [&](const char* key, const char* label, const char* group,
                      const char* help, int GameSettings::*member) {
        addInt(key, label, group, help, ValueType::Key, member);
    };

    // Menu order: grouped gameplay -> video -> audio -> movement -> combat ->
    // interface. The settings screen renders a header whenever the group
    // changes, so this order is all it takes to lay the screen out.
    addBool("infinite_ammo", "INFINITE AMMO", "GAMEPLAY",
            "Never run out of ammunition", &GameSettings::infiniteAmmo);
    addBool("prone_toggle", "C LIE TOGGLE", "GAMEPLAY",
            "ON: C toggles lying down. OFF: hold C to lie down",
            &GameSettings::proneToggle);
    addBool("show_fps", "SHOW FPS", "GAMEPLAY",
            "Show the frame-rate counter", &GameSettings::showFps);

    addInt("max_fps", "MAX FPS", "VIDEO",
           "Frame-rate cap, INFINITY = uncapped", ValueType::Int, &GameSettings::maxFps);
    addBool("vsync", "V-SYNC", "VIDEO",
            "Lock the frame rate to the display", &GameSettings::vsync);
    addInt("performance", "PERFORMANCE", "VIDEO",
           "How much processing power the game may use", ValueType::Choice,
           &GameSettings::performance);

    addInt("master_volume", "MASTER VOLUME", "AUDIO",
           "Overall volume", ValueType::Volume, &GameSettings::masterVolume);
    addInt("music_volume", "MUSIC VOLUME", "AUDIO",
           "Music volume", ValueType::Volume, &GameSettings::musicVolume);
    addInt("effects_volume", "EFFECTS VOLUME", "AUDIO",
           "Sound-effect volume", ValueType::Volume, &GameSettings::effectsVolume);

    addKey("bind_forward", "MOVE FORWARD", "MOVEMENT",
           "Walk forward", &GameSettings::bindForward);
    addKey("bind_backward", "MOVE BACKWARD", "MOVEMENT",
           "Walk backward", &GameSettings::bindBackward);
    addKey("bind_left", "MOVE LEFT", "MOVEMENT",
           "Strafe left", &GameSettings::bindLeft);
    addKey("bind_right", "MOVE RIGHT", "MOVEMENT",
           "Strafe right", &GameSettings::bindRight);
    addKey("bind_jump", "JUMP", "MOVEMENT",
           "Jump", &GameSettings::bindJump);
    addKey("bind_sprint", "SPRINT", "MOVEMENT",
           "Hold to run faster (standing only)", &GameSettings::bindSprint);
    addKey("bind_sneak", "SNEAK", "MOVEMENT",
           "Hold to crouch and move quietly", &GameSettings::bindSneak);
    addKey("bind_prone", "LIE / CRAWL", "MOVEMENT",
           "Toggle lying down and crawling", &GameSettings::bindProne);
    addKey("bind_shoot", "SHOOT", "COMBAT",
           "Fire the current weapon", &GameSettings::bindShoot);
    addKey("bind_scope", "SCOPE", "COMBAT",
           "Scope with the sniper (hold)", &GameSettings::bindScope);
    addKey("bind_weapon1", "WEAPON 1 (PISTOL)", "COMBAT",
           "Select the pistol", &GameSettings::bindWeapon1);
    addKey("bind_weapon2", "WEAPON 2 (SHOTGUN)", "COMBAT",
           "Select the pump shotgun", &GameSettings::bindWeapon2);
    addKey("bind_weapon3", "WEAPON 3 (SNIPER)", "COMBAT",
           "Select the sniper rifle", &GameSettings::bindWeapon3);
    addKey("bind_weapon4", "WEAPON 4 (GRENADE)", "COMBAT",
           "Select the grenade", &GameSettings::bindWeapon4);
    addKey("bind_weapon5", "WEAPON 5 (FLASHBANG)", "COMBAT",
           "Select the flashbang", &GameSettings::bindWeapon5);
    addKey("bind_grenade", "GRENADE", "COMBAT",
           "Toggle between grenade and flashbang", &GameSettings::bindGrenade);
    addKey("bind_use", "PLANT / DEFUSE", "COMBAT",
           "Use the bomb at a site / on the planted bomb", &GameSettings::bindUse);
    addKey("bind_unstuck", "UNSTUCK", "INTERFACE",
           "Teleport back to the map centre if stuck", &GameSettings::bindUnstuck);
    addKey("bind_menu", "BACK TO MENU", "INTERFACE",
           "Leave the game and open the menu", &GameSettings::bindMenu);
    addBool("discord_presence", "DISCORD STATUS", "INTERFACE",
            "Show the game and current map in Discord", &GameSettings::discordRichPresence);

    for (SettingDef& def : defs) store.defs.push_back(std::move(def));
    store.built = true;
    return {store.defs.begin(), store.defs.end()};
}

// ---------------------------------------------------------------------------
// Values <-> text
// ---------------------------------------------------------------------------

std::string displayValue(const SettingDef& def) {
    const Value& v = *def.value;
    switch (v.type) {
        case ValueType::Bool:
            return v.b ? "ON" : "OFF";
        case ValueType::Int:
            return def.key == "max_fps" && v.i == 0 ? "INFINITY" : std::to_string(v.i);
        case ValueType::Choice: {
            static const char* const perf[] = {"LOW", "BALANCED", "HIGH"};
            return perf[clampInt(v.i, 0, 2)];
        }
        case ValueType::Key:
            return bindingName(v.i);
        case ValueType::Volume:
            return std::to_string(v.i) + "%";
    }
    return "";
}

static std::string boolText(bool b) { return b ? "true" : "false"; }

std::string serializeValue(const SettingDef& def) {
    const Value& v = *def.value;
    switch (v.type) {
        case ValueType::Bool:
            return boolText(v.b);
        case ValueType::Int:
        case ValueType::Choice:
            return std::to_string(v.i);
        case ValueType::Volume:
            return std::to_string(v.i);
        case ValueType::Key:
            if (isMouseBinding(v.i)) return "mouse:" + std::to_string(mouseButtonOf(v.i));
            return std::to_string(v.i);
    }
    return "";
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool parseBool(const std::string& text, bool& out) {
    const std::string t = lower(text);
    if (t == "true" || t == "on" || t == "1" || t == "yes")  { out = true;  return true; }
    if (t == "false" || t == "off" || t == "0" || t == "no") { out = false; return true; }
    return false;
}

static bool parseInt(const std::string& text, int& out) {
    try {
        size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseValue(const SettingDef& def, const std::string& text) {
    Value& v = *def.value;
    switch (v.type) {
        case ValueType::Bool: {
            bool b = false;
            if (!parseBool(text, b)) return false;
            v.b = b;
            return true;
        }
        case ValueType::Int: {
            int i = 0;
            if (!parseInt(text, i)) return false;
            if (def.key == "max_fps") {
                // Accept only the presets offered by the menu (plus a few
                // sensible extras the player may have edited in by hand).
                static const int presets[] = {15, 20, 30, 45, 60, 75, 90, 100, 120, 144,
                                              165, 180, 200, 240, 300, 360, 0};
                bool known = false;
                for (int p : presets) known = known || (p == i);
                if (!known) return false;
            }
            if (i < 0) return false;
            v.i = i;
            return true;
        }
        case ValueType::Choice: {
            int i = 0;
            const std::string t = lower(text);
            if (t == "low") i = PERF_LOW;
            else if (t == "balanced" || t == "medium") i = PERF_BALANCED;
            else if (t == "high") i = PERF_HIGH;
            else if (!parseInt(text, i)) return false;
            v.i = clampInt(i, 0, 2);
            return true;
        }
        case ValueType::Volume: {
            int i = 0;
            std::string t = text;
            if (!t.empty() && t.back() == '%') t.pop_back();
            if (!parseInt(t, i) || i < 0 || i > 100) return false;
            v.i = i;
            return true;
        }
        case ValueType::Key: {
            int code = -1;
            if (text.rfind("mouse:", 0) == 0) {
                int button = 0;
                if (!parseInt(text.substr(6), button) || button < 0 || button > 7) return false;
                code = mouseBinding(button);
            } else if (!parseInt(text, code)) {
                return false;
            }
            if (!isMouseBinding(code) && (code <= 0 || code > GLFW_KEY_LAST)) return false;
            v.i = code;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// SettingsFile
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

SettingsFile::SettingsFile(std::string path) : path_(std::move(path)) {}

bool SettingsFile::writeText(const std::string& text) const {
    // Make sure the folder exists (assets/ always does in a normal install).
    const size_t slash = path_.find_last_of("/\\");
    if (slash != std::string::npos && slash > 0) {
        const std::string dir = path_.substr(0, slash);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);   // ec ignored on purpose
    }

    // Write a temporary file and rename it, so a crash mid-write cannot leave
    // a truncated settings file behind.
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        out.flush();
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool SettingsFile::writeFile(const std::vector<SettingDef>& defs) const {
    std::ostringstream out;
    out << "# WEXYRON settings\n"
        << "# Written by the game. Missing keys keep their default value.\n"
        << "# Keys and mouse buttons are stored as GLFW codes (mouse:<button>).\n";

    std::string group;
    for (const SettingDef& d : defs) {
        if (d.group != group) {
            group = d.group;
            out << "\n# " << group << "\n";
        }
        out << d.key << "=" << serializeValue(d) << "\n";
    }
    return writeText(out.str());
}

bool SettingsFile::save(const std::vector<SettingDef>& defs) const {
    const_cast<SettingsFile*>(this)->message_.clear();
    if (!writeFile(defs)) {
        std::cerr << "Could not write settings file " << path_ << std::endl;
        return false;
    }
    return true;
}

bool SettingsFile::load(std::vector<SettingDef>& defs) {
    message_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        // First launch (or the file was deleted): persist the defaults.
        if (writeFile(defs)) {
            message_ = "No settings file found - created " + path_ + " with the defaults";
        } else {
            std::cerr << "Could not create settings file " << path_ << std::endl;
        }
        return true;
    }

    std::ifstream in(path_);
    if (!in) {
        std::cerr << "Could not open settings file " << path_ << " - using defaults" << std::endl;
        return false;
    }

    int applied = 0;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#') continue;

        const size_t eq = text.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Settings: line " << lineNo << " has no '=' - ignored" << std::endl;
            continue;
        }
        const std::string key = trim(text.substr(0, eq));
        const std::string valueText = trim(text.substr(eq + 1));

        auto it = std::find_if(defs.begin(), defs.end(),
                               [&](const SettingDef& d) { return d.key == key; });
        if (it == defs.end()) {
            // Unknown keys are ignored silently: an older build reading a newer
            // file must not complain about options it does not know.
            continue;
        }
        if (!parseValue(*it, valueText)) {
            std::cerr << "Settings: invalid value for '" << key << "' ("
                      << valueText << ") - using default" << std::endl;
            continue;
        }
        ++applied;
    }

    // Bring the file up to date with any keys this build knows but the file
    // does not have yet (new options, hand-edited files).
    if (applied < static_cast<int>(defs.size())) {
        writeFile(defs);
    }
    return true;
}

} // namespace settings
