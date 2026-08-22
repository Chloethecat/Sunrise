#pragma once

#include <atomic>
#include <cstdint>

namespace sunrise::state::activity::experimental::world_state {

/**
 * Operator-controlled Activity Message 1 override.
 *
 * The override is scoped to one scenario tag. A selection made for one destination therefore
 * cannot silently alter another destination after the player changes activities.
 */
struct Snapshot final {
    std::uint32_t scenarioTag{};
    std::uint32_t bubble{};
    std::uint32_t stateOrdinal{};
    bool bubbleEnabled{};
    bool overrideBubble{};
    bool overrideSliceSet{};
    bool enabled{};
};

inline std::atomic<std::uint32_t> g_scenarioTag{};
inline std::atomic<std::uint32_t> g_bubble{};
inline std::atomic<std::uint32_t> g_stateOrdinal{};
inline std::atomic_bool g_bubbleEnabled{true};
inline std::atomic_bool g_overrideBubble{true};
inline std::atomic_bool g_overrideSliceSet{true};
inline std::atomic_bool g_enabled{false};

inline void publish(std::uint32_t scenarioTag,
                    std::uint32_t bubble,
                    std::uint32_t stateOrdinal,
                    bool bubbleEnabled,
                    bool overrideBubble,
                    bool overrideSliceSet) noexcept {
    g_enabled.store(false, std::memory_order_release);
    g_scenarioTag.store(scenarioTag, std::memory_order_relaxed);
    g_bubble.store(bubble, std::memory_order_relaxed);
    g_stateOrdinal.store(stateOrdinal, std::memory_order_relaxed);
    g_bubbleEnabled.store(bubbleEnabled, std::memory_order_relaxed);
    g_overrideBubble.store(overrideBubble, std::memory_order_relaxed);
    g_overrideSliceSet.store(overrideSliceSet, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_release);
}

/** Restores the unmodified Sunrise activity-state path. */
inline void clear() noexcept {
    g_enabled.store(false, std::memory_order_release);
}

[[nodiscard]] inline Snapshot snapshot() noexcept {
    Snapshot value{};
    value.enabled = g_enabled.load(std::memory_order_acquire);
    if (!value.enabled) {
        return value;
    }
    value.scenarioTag = g_scenarioTag.load(std::memory_order_relaxed);
    value.bubble = g_bubble.load(std::memory_order_relaxed);
    value.stateOrdinal = g_stateOrdinal.load(std::memory_order_relaxed);
    value.bubbleEnabled = g_bubbleEnabled.load(std::memory_order_relaxed);
    value.overrideBubble = g_overrideBubble.load(std::memory_order_relaxed);
    value.overrideSliceSet = g_overrideSliceSet.load(std::memory_order_relaxed);
    return value;
}

} // namespace sunrise::state::activity::experimental::world_state
