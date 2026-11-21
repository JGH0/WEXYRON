/**
 * @file menu_pages.cpp
 * @brief Ready-made menu pages built from the settings registry
 *
 * The settings and controls screens are generated from
 * settings::makeGameSettingDefs(), so a new option shows up in both screens
 * (and in the settings file) as soon as it is added to GameSettings.
 */

#include "menu_pages.hpp"
#include "../core/settings.hpp"
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace menu {

namespace {

std::string fpsText(int maxFps) {
    return maxFps == 0 ? "INFINITY" : std::to_string(maxFps);
}

std::vector<std::string> volumeOptions() {
    std::vector<std::string> out;
    for (int v = 0; v <= 100; v += 10) out.push_back(std::to_string(v) + "%");
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void appendSettingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const std::string& groupFilter) {
    SettingRowsOptions options;
    options.onlyGroup = groupFilter;
    appendSettingRows(page, defs, onChanged, options);
}

void appendSettingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const SettingRowsOptions& options) {
    std::string group;
    auto closeGroup = [&]() {
        if (!group.empty() && options.afterGroup) options.afterGroup(page, group);
    };
    for (size_t index = 0; index < defs.size(); ++index) {
        const settings::SettingDef& def = defs[index];
        if (!options.onlyGroup.empty() && def.group != options.onlyGroup) continue;
        if (std::find(options.skipGroups.begin(), options.skipGroups.end(), def.group) !=
            options.skipGroups.end()) continue;
        if (def.value->type == settings::ValueType::Key) continue;   // controls page

        // Group header when the group changes.
        if (def.group != group) {
            closeGroup();
            group = def.group;
            page.add(separator(group));
        }

        // The callbacks edit the registry row's value and then apply that row
        // to the GameSettings. They use the row index (stable storage), never
        // a copy of the def.
        auto changed = [index, onChanged]() {
            settings::applySettingDef(index);
            if (onChanged) onChanged();
        };

        const settings::Value& v = *def.value;
        switch (v.type) {
            case settings::ValueType::Bool:
                // The row already flipped its own state; mirror it into the
                // registry value the file and the GameSettings read from.
                page.add(toggle(def.label, v.b,
                                [index, changed](bool on) {
                                    settings::gameSettingDefs()[index].value->b = on;
                                    changed();
                                },
                                def.help));
                break;

            case settings::ValueType::Int: {
                // FPS cap: a cycling list of presets.
                std::vector<std::string> options;
                int optionIndex = 0;
                for (int i = 0; i < settings::kMaxFpsOptionCount; ++i) {
                    options.push_back(fpsText(settings::kMaxFpsOptions[i]));
                    if (settings::kMaxFpsOptions[i] == v.i) optionIndex = i;
                }
                page.add(cycle(def.label, options, optionIndex,
                               [index, changed](int selected) {
                                   settings::gameSettingDefs()[index].value->i = settings::kMaxFpsOptions[selected];
                                   changed();
                               },
                               def.help));
                break;
            }

            case settings::ValueType::Choice: {
                const int count = static_cast<int>(def.choices.size());
                page.add(cycle(def.label, def.choices, std::min(std::max(v.i, 0), std::max(count - 1, 0)),
                               [index, count, changed](int selected) {
                                   settings::setChoiceIndex(*settings::gameSettingDefs()[index].value,
                                                            selected, count);
                                   changed();
                               },
                               def.help));
                break;
            }

            case settings::ValueType::Volume:
                page.add(cycle(def.label, volumeOptions(), v.i / 10,
                               [index, changed](int selected) {
                                   settings::gameSettingDefs()[index].value->i = selected * 10;
                                   changed();
                               },
                               def.help));
                break;

            case settings::ValueType::Key:
                break;   // rebinding happens on the controls page
        }
    }
    closeGroup();
}

Page buildSettingsPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged,
                       const SettingRowsOptions& options) {
    Page page("SETTINGS", "CHANGES ARE SAVED AUTOMATICALLY");
    appendSettingRows(page, defs, onChanged, options);
    return page;
}

Page buildFrameGenPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged) {
    Page page(settings::kFrameGenGroup, "CHANGES ARE SAVED AUTOMATICALLY");
    appendSettingRows(page, defs, onChanged, std::string(settings::kFrameGenGroup));
    return page;
}

Page buildVideoGroupPage(const std::vector<settings::SettingDef>& defs,
                         std::function<void()> onChanged) {
    Page page("VIDEO", "CHANGES ARE SAVED AUTOMATICALLY");
    appendSettingRows(page, defs, onChanged, "VIDEO");
    return page;
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

void appendBindingRows(Page& page, const std::vector<settings::SettingDef>& defs,
                       const std::function<void()>& onChanged,
                       const std::string& groupFilter) {
    // `defs` may be a copy owned by the caller (gameSettingDefs() returns
    // one) and the page lives on after this function returns, so the callbacks
    // capture neither: they use the caller's `onChanged`, the registry's live
    // defs (stable addresses) and their own row index.
    std::shared_ptr<std::vector<size_t>> rows = std::make_shared<std::vector<size_t>>();
    std::shared_ptr<std::vector<size_t>> keys = std::make_shared<std::vector<size_t>>();

    auto applyBinding = [onChanged, rows, keys](size_t row, size_t index, int code) {
        const std::vector<settings::SettingDef>& live = settings::gameSettingDefs();

        // One action per key (or mouse button): a key belongs to the most
        // recent binding, so binding G to JUMP frees G from GRENADE (before,
        // both fired and G only threw a grenade - "cannot jump").
        if (code >= 0) {
            for (size_t k = 0; k < keys->size(); ++k) {
                const size_t other = (*keys)[k];
                if (other == index) continue;
                const settings::SettingDef& def = live[other];
                if (def.value->type != settings::ValueType::Key) continue;
                if (def.value->i != code) continue;

                // Show it on that row first, then change the value.
                if ((*rows)[k] == row) continue;   // never the row being bound
                settings::setKeyValue(*def.value, -1);
                settings::applySettingDef(other);
            }
        }

        settings::setKeyValue(*live[index].value, code);
        settings::applySettingDef(index);
        if (onChanged) onChanged();
    };

    std::string group;
    for (const settings::SettingDef& def : defs) {
        if (def.value->type != settings::ValueType::Key) continue;
        if (!groupFilter.empty() && def.group != groupFilter) continue;
        if (def.group != group) {
            group = def.group;
            page.add(separator(group));
        }
        const size_t index = static_cast<size_t>(&def - defs.data());
        const size_t row = page.rows.size();
        keys->push_back(index);
        rows->push_back(row);
        page.add(keybind(def.label, def.value->i,
                         [row, index, applyBinding](int code) { applyBinding(row, index, code); },
                         def.help));
    }
}

Page buildControlsPage(const std::vector<settings::SettingDef>& defs,
                       std::function<void()> onChanged) {
    Page page("CONTROLS", "KEYBOARD AND MOUSE BINDINGS");
    appendBindingRows(page, defs, onChanged);
    return page;
}

} // namespace menu
