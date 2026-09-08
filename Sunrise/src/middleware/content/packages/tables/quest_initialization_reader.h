#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../state/build_data/items/quest_initialization.h"

namespace sunrise::middleware::content::packages::tables::items {

/** No supported set-bearing item was resolved from the pursuit definition. */
inline constexpr std::uint16_t kUnavailableQuestParent = 0xFFFFU;

/** Returns the objective block's set-bearing item index, or kUnavailableQuestParent. */
[[nodiscard]] std::uint16_t quest_parent(std::span<const std::byte> definition) noexcept;

/**
 * Resolves the first member of a supported quest set to its saved value-bank row.
 * @param definition Objective-bearing pursuit definition.
 * @param itemIndex Definition's ordinal in the item table.
 * @param parent Set-bearing definition named by quest_parent, possibly definition itself.
 * @param itemCount Bounds for authored item indices.
 * @param valueMap Serialized unlock value mapping table.
 * @return An empty plan for unsupported or malformed content; no guessed state write.
 */
[[nodiscard]] state::build_data::items::QuestInitialization
read_quest_initialization(std::span<const std::byte> definition,
                          std::uint16_t itemIndex,
                          std::span<const std::byte> parent,
                          std::size_t itemCount,
                          std::span<const std::byte> valueMap) noexcept;

} // namespace sunrise::middleware::content::packages::tables::items
