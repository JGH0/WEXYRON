/**
 * @file settings.hpp
 * @brief Game settings: typed values, defaults and the settings file
 *
 * Every persistent option of the game lives here as a small typed value
 * (bool / int / enum / key / mouse button / volume). The menu framework
 * (ui/menu_core.hpp) lists these values generically, so adding an option means
 * adding one member to GameSettings and one description in makeGameSettingDefs()
 * - the settings screen, the settings file and the console dump all pick it up.
 *
 * The file format is the same key=value convention as the map files:
 *
 *     # WEXYRON settings
 *     show_fps=true
 *     max_fps=144
 *     bind_forward=87          (GLFW key code)
 *     bind_shoot=mouse:0       (GLFW mouse button)
 *
 * Unknown keys are ignored, missing keys keep their default, malformed values
 * are reported and the default is kept. When the file does not exist it is
 * written with the defaults (which are exactly the values the game launched
 * with before settings were persisted).
 */

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cstdlib>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace settings {

/// Human-readable name of a GLFW key code ("W", "SPACE", "LEFT SHIFT", ...).
/// Mouse buttons must be converted first (see keyName / mouseButtonName).
std::string keyName(int key);

/// Name of a GLFW mouse button ("LEFT MOUSE", ...).
std::string mouseButtonName(int button);

/// True if `code` is a GLFW mouse button rather than a keyboard key.
inline bool isMouseBinding(int code) { return code >= 1000; }

/// Encode a mouse button as a binding code (keeps keys and buttons apart in
/// one integer, so a binding can be either).
inline int mouseBinding(int button) { return 1000 + button; }

/// Decode an encoded mouse binding; returns -1 when `code` is a key.
inline int mouseButtonOf(int code) { return isMouseBinding(code) ? code - 1000 : -1; }

/// Display name for any binding code (key or mouse button).
std::string bindingName(int code);

// ---------------------------------------------------------------------------
// Tagged values
// ---------------------------------------------------------------------------

enum class ValueType { Bool, Int, Choice, Key, Volume };

/// One setting value. `Choice` stores the index into `choices`.
struct Value {
    ValueType type;
    bool b;               ///< Bool
    int i;                ///< Int / Choice / Key / Volume (0..100)

    Value() : type(ValueType::Bool), b(false), i(0) {}
    Value(ValueType t, bool boolean, int integer) : type(t), b(boolean), i(integer) {}
};

/// Possible values for the choice settings (kept next to the defaults so the
/// file loader can parse and validate names).
enum PerformanceLevel { PERF_LOW = 0, PERF_BALANCED = 1, PERF_HIGH = 2 };
const int kMaxFpsOptions[] = {20, 30, 60, 100, 120, 144, 200, 240, 0}; // 0 = unlimited
const int kMaxFpsOptionCount = 9;

/// Bound on frame time used by the main loop (kept in sync with main.cpp).
const float kMaxFrameTime = 0.05f;

// ---------------------------------------------------------------------------
// The settings themselves
// ---------------------------------------------------------------------------

/**
 * @brief All persistent game settings.
 *
 * Defaults are the values the game has always started with.
 */
struct GameSettings {
    // Gameplay ---------------------------------------------------------------
    bool infiniteAmmo = false;
    /// C toggles lying down when ON; OFF makes it a held key (release to get up).
    bool proneToggle = true;

    // Video ------------------------------------------------------------------
    bool showFps = false;
    int  maxFps = 144;             ///< 0 = unlimited
    bool vsync = true;
    int  performance = PERF_BALANCED;

    // Audio ------------------------------------------------------------------
    int masterVolume = 100;        ///< 0..100, applied live
    int musicVolume = 70;          ///< 0..100, applied live
    int effectsVolume = 90;        ///< 0..100, applied live

    // Interface --------------------------------------------------------------
    /// Show "WEXYRON" + map/mode in Discord while the game runs.
    bool discordRichPresence = true;

    // Controls ---------------------------------------------------------------
    int bindForward = 87;           ///< GLFW_KEY_W
    int bindBackward = 83;          ///< GLFW_KEY_S
    int bindLeft = 65;              ///< GLFW_KEY_A
    int bindRight = 68;             ///< GLFW_KEY_D
    int bindJump = 32;              ///< GLFW_KEY_SPACE
    int bindSprint = 340;           ///< GLFW_KEY_LEFT_SHIFT
    int bindSneak = 341;            ///< GLFW_KEY_LEFT_CONTROL (crouch)
    int bindProne = 67;             ///< GLFW_KEY_C (lie down / crawl, toggle)
    int bindShoot = 1000 + 0;       ///< GLFW_MOUSE_BUTTON_LEFT
    int bindScope = 1000 + 1;       ///< GLFW_MOUSE_BUTTON_RIGHT (sniper)
    int bindWeapon1 = 49;           ///< GLFW_KEY_1 (pistol)
    int bindWeapon2 = 50;           ///< GLFW_KEY_2 (shotgun)
    int bindWeapon3 = 51;           ///< GLFW_KEY_3 (sniper)
    int bindWeapon4 = 52;           ///< GLFW_KEY_4 (grenade)
    int bindWeapon5 = 53;           ///< GLFW_KEY_5 (flashbang)
    int bindGrenade = 71;           ///< GLFW_KEY_G
    int bindUse = 69;               ///< GLFW_KEY_E (plant / defuse)
    int bindUnstuck = 85;           ///< GLFW_KEY_U
    int bindMenu = 256;             ///< GLFW_KEY_ESCAPE
};

/// One entry of the settings registry: a value plus everything the UI and the
/// file need to know about it. `value` points into the registry storage
/// (see makeGameSettingDefs).
struct SettingDef {
    std::string key;        ///< file key, e.g. "show_fps"
    std::string label;      ///< menu label, e.g. "SHOW FPS"
    std::string group;      ///< menu group, e.g. "VIDEO"
    std::string help;       ///< one-line description
    Value*      value;

    /// Apply the stored value to the game member this row mirrors. Called by
    /// the menu whenever the value changes. Empty means the row is read-only.
    std::function<void(const Value&)> apply;

    /// Current member value, for refreshing a row (see syncSettingDefs).
    std::function<Value(const GameSettings&)> get;

    /// Bool/Conditional rows are toggled, Int/Choice rows cycle, Keys are
    /// rebound. Volume rows cycle in steps of 10.
    bool isToggle() const { return value->type == ValueType::Bool; }
    /// True for the volume rows (Int with a percent representation).
    bool isVolume() const { return value->type == ValueType::Volume; }
};

/// Describe every setting of `s` (one def per member, in menu order).
///
/// The defs copy the members of `s` when they are created; call
/// syncSettingDefs afterwards to refresh them. The defs keep a reference to
/// `s` (that is where apply/get write and read), so `s` must outlive them.
/// There is one registry per process: calling this again returns the same
/// defs (still bound to the `s` of the first call).
std::vector<SettingDef> makeGameSettingDefs(GameSettings& s);

/// The registry built by makeGameSettingDefs. Rows are stored in stable
/// storage (a deque), so callers may keep references to individual rows.
const std::vector<SettingDef>& gameSettingDefs();

/// Apply the current value of registry row `index` to the GameSettings it was
/// built from (the menu calls this after editing a row's value).
void applySettingDef(size_t index);

/// Change one registry row from its file text (e.g. "off", "144", "mouse:1").
/// Returns false (value unchanged) when the text is invalid.
bool setSettingFromText(size_t index, const std::string& text);

/// Write every registry row's value into its GameSettings member. Use after the
/// settings file was loaded (file -> registry -> GameSettings).
void applySettingDefs(GameSettings& s, std::vector<SettingDef>& defs);
void applySettingDefs(GameSettings& s);

/// Re-read all GameSettings members into the registry rows (GameSettings ->
/// rows). Use after the settings were changed programmatically (e.g. reset).
void syncSettingDefs(GameSettings& s);

// Small helpers used by the menu pages (they only touch a row's Value; the
// page calls applySettingDef() afterwards to write it to the GameSettings).
void setChoiceIndex(Value& v, int index);
void setKeyValue(Value& v, int code);

/// Copy the current member values of `s` into `defs` again (used when the file
/// has been reloaded behind the menu's back).
void syncSettingDefs(GameSettings& s, std::vector<SettingDef>& defs);

/// Make `defs` act on the `s` passed to makeGameSettingDefs (kept for symmetry;
/// the defs are bound when they are built).
void bindSettingDefs(GameSettings& s, std::vector<SettingDef>& defs);

/// Settings file used by the game (relative to the working directory).
/// CUBE_SETTINGS_PATH overrides it, so tests can run on their own file
/// without touching the player's settings.
inline const char* defaultSettingsPath() {
    const char* overridePath = std::getenv("CUBE_SETTINGS_PATH");
    return (overridePath && *overridePath) ? overridePath : "assets/settings.cfg";
}

// ---------------------------------------------------------------------------
// Values <-> text
// ---------------------------------------------------------------------------

/// Text shown on the right of the row ("ON", "144", "BALANCED", "W"...).
std::string displayValue(const SettingDef& def);

/// Value text for the settings file (stable, machine readable).
std::string serializeValue(const SettingDef& def);

/// Parse a file value into `def.value`. Returns false (and leaves the value
/// untouched) when the text is invalid.
bool parseValue(const SettingDef& def, const std::string& text);

} // namespace settings

// ---------------------------------------------------------------------------
// Settings file
// ---------------------------------------------------------------------------

namespace settings {

/**
 * @brief Loads/saves the collection of SettingDefs to a key=value file.
 *
 * A missing file is created from the current values (the defaults).
 */
class SettingsFile {
public:
    explicit SettingsFile(std::string path = "assets/settings.cfg");

    /// Load def values from the file (creating it when missing).
    /// Returns false when the file exists but could not be read.
    bool load(std::vector<SettingDef>& defs);

    /// Write the current def values to the path.
    bool save(const std::vector<SettingDef>& defs) const;

    const std::string& path() const { return path_; }

    /// Message for the "settings written/read" line in the console (empty
    /// when nothing notable happened).
    const std::string& lastMessage() const { return message_; }

private:
    bool writeFile(const std::vector<SettingDef>& defs) const;
    /// Replace `file` atomically (write temp + rename), creating directories.
    bool writeText(const std::string& text) const;

    std::string path_;
    std::string message_;
};

} // namespace settings

#endif // SETTINGS_HPP
