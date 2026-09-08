#pragma once

#include "../../../../state/build_data/items/quest_initialization.h"
#include "items.h"

namespace sunrise::middleware::content::packages::tables::items {

/** Returns the objective block's set-bearing item index, or 0xFFFF when unsupported. */
[[nodiscard]] std::uint16_t quest_parent(std::span<const std::byte> definition) noexcept;

/** Unsupported/malformed contracts return an empty plan; they never imply a guessed write. */
[[nodiscard]] state::build_data::items::QuestInitialization
read_quest_initialization(std::span<const std::byte> definition,
                          std::uint16_t itemIndex,
                          std::span<const std::byte> parent,
                          std::size_t itemCount,
                          std::span<const std::byte> valueMap) noexcept;

} // namespace sunrise::middleware::content::packages::tables::items
