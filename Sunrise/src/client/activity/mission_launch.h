#pragma once
#include <cstdint>

#include "../../state/activity/forced/definition.h"
namespace sunrise::client::activity::mission_launch {
enum class Status : std::uint8_t {
    idle,
    requested,
    queued,
    arrived,
    catalogUnavailable,
    entryUnavailable,
    overrideActive,
    returnToOrbit,
    nativeUnavailable,
    notReady,
    descriptorRejected,
    timedOut,
    manualRejected
};
struct Snapshot {
    Status status{Status::idle};
    std::uint16_t index{};
    bool busy{};
    bool manual{};
    state::activity::forced::ForcedDestination destination{};
};
/** Render thread: enqueue one immutable public activity ordinal. No native calls here. */
[[nodiscard]] bool request(std::uint16_t index) noexcept;
/** Enqueues a copied draft. The game-frame owner validates and publishes it immediately before
 * launch. */
[[nodiscard]] bool
request_manual(std::uint16_t index,
               const state::activity::forced::ForcedDestination& destination) noexcept;
/** @return A synchronized copy of the latest request and its native transition status. */
[[nodiscard]] Snapshot snapshot() noexcept;
/** Existing game-frame owner only. Orbit launch uses retail selection wrappers unchanged. */
void poll() noexcept;
/** @return A static UI message explaining the request result or next required action. */
[[nodiscard]] const char* description(Status status) noexcept;
} // namespace sunrise::client::activity::mission_launch
