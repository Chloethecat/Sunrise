#pragma once

#include <cstdint>

#include "../../unlocks/definition.h"

namespace sunrise::state::build_data::items {

/** Only the first member of a supported, item-presence-gated quest set. */
struct QuestInitialization {
    enum class Scope : std::uint8_t { none, account, character };
    std::int32_t value{};
    std::uint16_t row{};
    Scope scope{};

    bool operator==(const QuestInitialization&) const = default;
};

[[nodiscard]] constexpr bool valid(const QuestInitialization& quest) noexcept {
    using Scope = QuestInitialization::Scope;
    if (quest.scope == Scope::none) {
        return quest.row == 0 && quest.value == 0;
    }
    return quest.value != 0 && quest.value != -1
           && ((quest.scope == Scope::account && quest.row < unlocks::kObjectiveValueCapacity)
               || (quest.scope == Scope::character
                   && quest.row < unlocks::kCharacterObjectValueCapacity));
}

/** Set values are identifiers, not a numerically ordered progress counter. */
[[nodiscard]] constexpr std::int32_t initialized_value(const QuestInitialization& quest,
                                                       std::int32_t before) noexcept {
    return quest.scope != QuestInitialization::Scope::none && before == 0 ? quest.value : before;
}

} // namespace sunrise::state::build_data::items
