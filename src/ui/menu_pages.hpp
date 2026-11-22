/**
 * @file menu_pages.hpp
 * @brief Ready-made menu pages built from the settings registry
 *
 * game_menus.cpp composes its screens from these: the main menu, the settings
 * page (one row per setting), the controls page (one row per key binding) and
 * helper pages for data the game provides at runtime (maps, servers).
 */

#ifndef MENU_PAGES_HPP
#define MENU_PAGES_HPP

#include "menu_core.hpp"
#include "../core/settings.hpp"
#include <functional>
#include <string>
#include <vector>

namespace menu {

/// Which setting rows appendSettingRows() emits, and where callers may add
/// their own rows.
struct SettingRowsOptions {
    std::string onlyGroup;                ///< empty = every group
    std::vector<std::string> skipGroups;  ///< groups that live on their own page
    /// Called after the last row of each group (e.g. to add a submenu link).
    std::function<void(Page&, const std::string& group)> afterGroup;
    /// Optional platform filter: false greys the row out and skips it.
    std::function<bool(const settings::SettingDef&)> rowEnabled;
    /// Optional per-option filter for Cycle rows (false options are skipped).
    std::function<bool(const settings::SettingDef&, int optionIndex)> optionEnabled;
    /// Optional help override (e.g. why the current value is unavailable).
    std::function<std::string(const settings::SettingDef&)> rowHelp;
};

/// Settings screen: one row per setting the registry describes, grouped by
/// "GAMEPLAY" / "VIDEO" / "AUDIO" headers. Key bindings live on the controls
/// page instead. `onChanged` runs after any value changed (save the file).
Page buildSettingsPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged = nullptr,
                       const SettingRowsOptions& options = SettingRowsOptions());

/// FRAME GENERATION sub-screen: the rows of settings::kFrameGenGroup.
Page buildFrameGenPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged = nullptr,
                       const SettingRowsOptions& options = SettingRowsOptions());

/// Only the VIDEO group (for a future "video" sub-screen).
Page buildVideoGroupPage(const std::vector<settings::SettingDef>& defs,
                         std::function<void()> onChanged = nullptr);

/// Controls screen: one key-cap row per binding, grouped like the registry.
Page buildControlsPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged = nullptr);

/// Append one key-cap row per binding of `group` (empty = all groups),
/// grouped like the registry. Used to place controls inside another page.
void appendBindingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const std::string& groupFilter = std::string());

/// Append one row per setting in `groupFilter` (empty = all groups).
void appendSettingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const std::string& groupFilter = std::string());
void appendSettingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const SettingRowsOptions& options);

} // namespace menu

#endif // MENU_PAGES_HPP
