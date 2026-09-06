#pragma once

#include <cstdint>

#include "definition.h"

namespace sunrise::state::activity::destination {

/**
 * Picks the spawn-set hash to send, dropping one the destination does not load.
 * A set declared only by a package the destination never loads cannot attach, and the player
 * arrives with no spawn point. The absent hash goes out instead, so the Client picks its own.
 * @param selection Committed destination.
 * @param fallback Authored fallback hash.
 * @return The resolved hash, or the absent hash when the set cannot attach.
 */
[[nodiscard]] std::uint32_t attachable_spawn_set_hash(const DestinationSelection& selection,
                                                      std::uint32_t fallback) noexcept;

/**
 * Finds the slice set the type-17 spawn override must name for one spawn-set hash.
 * The Client searches for the hash only inside the slice set the override names, so naming a
 * slice set the spawn set is not declared in yields no spawn point at all.
 * @param selection Committed destination.
 * @param spawnSetHash Hash the override will carry.
 * @param arrivalSliceSet Slice set the destination arrives in.
 * @return The arrival when it already declares the set, otherwise the set's own slice set.
 */
[[nodiscard]] std::uint16_t spawn_set_slice_set(const DestinationSelection& selection,
                                                std::uint32_t spawnSetHash,
                                                std::uint16_t arrivalSliceSet) noexcept;

} // namespace sunrise::state::activity::destination
