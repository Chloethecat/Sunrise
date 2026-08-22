#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <string_view>

#include "../../../middleware/content/packages/tables/region_reader.h"
#include "../../../state/activity/experimental/world_state_override.h"
#include "../../../state/build_data/runtime.h"

namespace sunrise::server::ui::world_state {
namespace internal {

namespace build = state::build_data;
namespace layouts = state::build_data::scenarios;
namespace override_state = state::activity::experimental::world_state;
namespace tables = middleware::content::packages::tables;

inline std::array<layouts::Definition, layouts::kDefinitionCapacity> g_layouts{};
inline std::size_t g_layoutCount{};
inline std::size_t g_layoutRevision{};
inline std::size_t g_selectedLayout{};
inline int g_selectedBubble{};
inline int g_stateOrdinal{};
inline bool g_bubbleEnabled{true};
inline bool g_overrideBubble{true};
inline bool g_overrideSliceSet{true};
inline bool g_ready{};
inline bool g_applied{};

[[nodiscard]] inline std::string_view name_of(const layouts::Definition& layout) noexcept {
    return {layout.name.data(), layout.nameLength};
}

inline void refresh_layouts() noexcept {
    std::size_t count = 0;
    if (!build::snapshot_scenario_layouts(g_layouts, count)) {
        g_ready = false;
        g_layoutCount = 0;
        return;
    }
    g_layoutCount = count;
    g_layoutRevision = build::scenario_layout_count();
    if (g_layoutCount == 0) {
        g_selectedLayout = 0;
    } else if (g_selectedLayout >= g_layoutCount) {
        g_selectedLayout = g_layoutCount - 1;
    }
    g_ready = true;
}

inline void refresh_if_needed() noexcept {
    const std::size_t revision = build::scenario_layout_count();
    if (!g_ready || revision != g_layoutRevision) {
        refresh_layouts();
    }
}

inline void clamp_bubble(const layouts::Definition& layout) noexcept {
    const int maxBubble = layout.bubbleCount == 0 ? 0 : static_cast<int>(layout.bubbleCount) - 1;
    g_selectedBubble = (std::clamp)(g_selectedBubble, 0, maxBubble);
    const std::uint8_t count = layout.bubbleStateCounts[static_cast<std::size_t>(g_selectedBubble)];
    const int maxState = count == 0 ? 0 : static_cast<int>(count) - 1;
    g_stateOrdinal = (std::clamp)(g_stateOrdinal, 0, maxState);
}

inline void draw_group(std::uint16_t tableIndex, const char* label) noexcept {
    layouts::RosterGroup group{};
    if (!build::find_roster_group(tableIndex, group)) {
        ImGui::TextDisabled("%s group #%u unavailable", label, static_cast<unsigned>(tableIndex));
        return;
    }

    ImGui::PushID(static_cast<int>(tableIndex));
    const bool open = ImGui::TreeNodeEx(
        "group",
        ImGuiTreeNodeFlags_SpanAvailWidth,
        "%s  key=0x%08X  objectTag=0x%08X  slots=%u",
        label,
        group.registryKey,
        group.objectTag,
        static_cast<unsigned>(group.slotCount));

    if (open) {
        if (ImGui::BeginTable("slots", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, 230.0f))) {
            ImGui::TableSetupColumn("Slot");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Sense");
            ImGui::TableSetupColumn("Auth");
            ImGui::TableHeadersRow();

            for (std::size_t slot = 0; slot < group.slotCount; ++slot) {
                const std::uint8_t flags = group.slotFlags[slot];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", static_cast<unsigned>(group.slotIndices[slot]));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", static_cast<unsigned>(group.slotTypes[slot]));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    (flags & layouts::kSlotSenseFlag) != 0 ? "YES" : "-");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    (flags & layouts::kSlotAuthFlag) != 0 ? "YES" : "-");
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace internal

/** World activity-state inspector and safe experimental overrides. */
inline void draw() noexcept {
    using namespace internal;

    ImGui::TextUnformatted("WORLD STATE INSPECTOR v0.1");
    ImGui::TextWrapped(
        "Shows the scenario bubbles and roster object slots Sunrise extracted from the installed "
        "build. Sense/Auth are real slot flags from the destination's roster objects. The live "
        "controls below alter Activity Message 1 for the selected bubble; Restore disables every "
        "override made by this page.");
    ImGui::Spacing();

    refresh_if_needed();
    if (!build::scenario_layouts_ready() || !g_ready || g_layoutCount == 0) {
        ImGui::TextUnformatted("Waiting for Sunrise scenario extraction...");
        return;
    }

    const layouts::Definition& current = g_layouts[g_selectedLayout];
    const std::string_view currentName = name_of(current);

    if (ImGui::BeginCombo("Destination",
                          currentName.empty() ? "<unnamed>" : currentName.data())) {
        for (std::size_t index = 0; index < g_layoutCount; ++index) {
            const layouts::Definition& row = g_layouts[index];
            const std::string_view rowName = name_of(row);
            const bool selected = index == g_selectedLayout;
            std::array<char, 96> label{};
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s  [0x%08X]",
                                static_cast<int>(rowName.size()),
                                rowName.data(),
                                row.tag);
            if (ImGui::Selectable(label.data(), selected)) {
                g_selectedLayout = index;
                g_selectedBubble = 0;
                g_stateOrdinal = 0;
                g_applied = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const layouts::Definition& layout = g_layouts[g_selectedLayout];
    clamp_bubble(layout);

    ImGui::Text("Scenario tag: 0x%08X", layout.tag);
    ImGui::Text("Bubbles: %u", static_cast<unsigned>(layout.bubbleCount));

    if (layout.bubbleCount != 0) {
        if (ImGui::SliderInt(
                "Bubble", &g_selectedBubble, 0, static_cast<int>(layout.bubbleCount) - 1)) {
            g_stateOrdinal = 0;
            g_applied = false;
        }
    }

    const std::size_t bubble = static_cast<std::size_t>(g_selectedBubble);
    const std::uint8_t stateCount = layout.bubbleStateCounts[bubble];
    const int maxState = stateCount == 0 ? 0 : static_cast<int>(stateCount) - 1;
    (void)ImGui::SliderInt("Slice state", &g_stateOrdinal, 0, maxState);

    const std::uint32_t baseSlice =
        tables::region_index(static_cast<std::uint32_t>(bubble));
    const std::uint32_t selectedSlice =
        baseSlice + static_cast<std::uint32_t>(g_stateOrdinal);

    state::build_data::hash_names::Name bubbleName{};
    const bool hasName = build::find_hash_name(layout.bubbleHashes[bubble], bubbleName);
    ImGui::Text("Bubble hash: 0x%08X", layout.bubbleHashes[bubble]);
    ImGui::Text("Map bubble: %u",
                static_cast<unsigned>(layout.bubbleMapIndices[bubble]));
    ImGui::Text("State count: %u", static_cast<unsigned>(stateCount));
    ImGui::Text("Selected slice set: %u", static_cast<unsigned>(selectedSlice));
    if (hasName) {
        ImGui::Text("Resolved name: %.*s",
                    static_cast<int>(bubbleName.nameLength),
                    bubbleName.name.data());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Live activity-state experiment");

    ImGui::Checkbox("Override bubble enabled byte", &g_overrideBubble);
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &g_bubbleEnabled);
    ImGui::Checkbox("Override current slice state", &g_overrideSliceSet);

    if (ImGui::Button("Apply Live")) {
        override_state::publish(layout.tag,
                                static_cast<std::uint32_t>(bubble),
                                static_cast<std::uint32_t>(g_stateOrdinal),
                                g_bubbleEnabled,
                                g_overrideBubble,
                                g_overrideSliceSet);
        g_applied = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Sunrise Behavior")) {
        override_state::clear();
        g_applied = false;
    }

    if (g_applied) {
        ImGui::TextWrapped(
            "Override armed. Sunrise will substitute this state the next time it publishes global "
            "activity state. Watch doors, barriers, elevators, geometry, HUD, and loading behavior.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Roster objects / flags");

    ImGui::TextWrapped(
        "These are the server-owned roster groups for this destination. Sense/Auth are the "
        "installed slot flags. They are read-only in v0.1 because Sunrise does not yet expose a "
        "generic per-slot auth-state body; identifying which slot controls a door is the next layer.");

    for (std::size_t index = 0; index < layout.rosterGroupCount; ++index) {
        draw_group(layout.rosterGroups[index], "Global");
    }

    for (std::size_t index = 0; index < layout.bubbleGroupCount; ++index) {
        const bool inBubble =
            (layout.bubbleGroupMasks[index] & (std::uint64_t{1} << bubble)) != 0;
        if (inBubble) {
            draw_group(layout.bubbleGroups[index], "Bubble");
        }
    }
}

} // namespace sunrise::server::ui::world_state
