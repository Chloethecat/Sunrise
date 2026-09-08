#pragma once

#include <cstdint>

#include "../../unlocks/definition.h"

namespace sunrise::state::build_data::items {

/** Native pursuit bucket shared by quest items and bounties. */
inline constexpr std::uint8_t kPursuitBucketId = 40;
/** Zero is unset state; -1 is excluded as an authored first-step identifier. */
inline constexpr std::int32_t kUnsetQuestValue = 0;
inline constexpr std::int32_t kInvalidQuestInitialValue = -1;

/** Authored initial value and bank row for the first member of a supported quest set. */
struct QuestInitialization {
    enum class Scope : std::uint8_t { none, account, character };
    std::int32_t value{};
    std::uint16_t row{};
    Scope scope{};

    bool operator==(const QuestInitialization&) const = default;
};

/** An empty plan is valid; a supported plan must fit its persistent value bank. */
[[nodiscard]] constexpr bool valid(const QuestInitialization& quest) noexcept {
    using Scope = QuestInitialization::Scope;
    if (quest.scope == Scope::none) {
        return quest.row == 0 && quest.value == kUnsetQuestValue;
    }
    return quest.value != kUnsetQuestValue && quest.value != kInvalidQuestInitialValue
           && ((quest.scope == Scope::account && quest.row < unlocks::kObjectiveValueCapacity)
               || (quest.scope == Scope::character
                   && quest.row < unlocks::kCharacterObjectValueCapacity));
}

/** Set values are identifiers, not a numerically ordered progress counter. */
[[nodiscard]] constexpr std::int32_t initialized_value(const QuestInitialization& quest,
                                                       std::int32_t before) noexcept {
    return quest.scope != QuestInitialization::Scope::none && before == kUnsetQuestValue
               ? quest.value
               : before;
}

} // namespace sunrise::state::build_data::items
