/**
 * Inactivity timeout override.
 *
 * The Client keeps 14 activity timeouts in one live object and ends a session idle for longer.
 * The pointer to that object is obfuscated, so this module resolves the Client's own getter by
 * signature and calls it. The block is re-applied on an interval because an activity change
 * re-authors it.
 */

#include "inactivity_override.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../player/player_settings_store.h"

namespace sunrise::client::hooks::inactivity {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * The activity config getter. Every obfuscated pointer getter shares this prologue, so the load of
 * its own global stays unwildcarded; it is image-relative, so ASLR does not move it.
 */
constexpr std::string_view kConfigGetterText =
    "40 53 48 83 EC 20 48 8B 1D 2B 10 1A 02 48 85 DB 0F 84 ? ? ? ? 48 89 5C 24 30 "
    "E8 ? ? ? ? 33 C3";
/** Compiled pattern bytes of the config getter signature. */
constexpr auto kConfigGetter = signature<signature_length(kConfigGetterText)>(kConfigGetterText);

/** Activity lanes the Client keeps a separate inactivity timeout for. */
constexpr std::size_t kActivityCount = 14;
/** Where the lanes start in the object the getter returns. */
constexpr std::size_t kTimeoutBlockOffset = 0xAC;
/** Milliseconds every lane is held at. A day outlasts any session. */
constexpr std::uint32_t kHeldTimeoutMs = 86400000;
/** Milliseconds between re-applications, so an activity change cannot outlast the hold. */
constexpr std::uint64_t kHoldIntervalMs = 2000;

/** Fourteen consecutive milliseconds, in block order. */
using Lanes = std::array<std::uint32_t, kActivityCount>;
/** Bytes of the block. */
constexpr std::size_t kBlockBytes = sizeof(Lanes);

/** @return Every lane at its longest. */
[[nodiscard]] consteval Lanes held_lanes() noexcept {
    Lanes values{};
    values.fill(kHeldTimeoutMs);
    return values;
}

/** The block a hold writes. */
constexpr Lanes kHeldLanes = held_lanes();

/** Returns the activity config object. The pointer in its global is obfuscated, so we call it. */
using ConfigGetter = std::byte*(__fastcall*)();

SRWLOCK g_lock{SRWLOCK_INIT};
ConfigGetter g_getter{};
std::uint64_t g_nextHoldTick{};
/** The Client's own lanes for the activity in play. */
Lanes g_captured{};
bool g_capturedValid{};
/** Set while a hold is in place, so releasing it writes the captured lanes exactly once. */
bool g_holding{};
/** The setting the last poll acted on, so a change does not wait for the hold interval. */
bool g_intentHolding{};
bool g_intentValid{};

/**
 * Calls the getter without faulting. The body is obfuscated game code, and it runs before the
 * Client has published its global on an early frame.
 * @return The activity config object, or null.
 */
[[nodiscard]] std::byte* config_object() noexcept {
    if (g_getter == nullptr) {
        return nullptr;
    }
    __try {
        return g_getter();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

/**
 * Reads the block out of the object.
 * @param object Config object.
 * @param values Receives the lanes.
 * @return True when Windows copied all of them.
 */
[[nodiscard]] bool read_block(const std::byte* object, Lanes& values) noexcept {
    SIZE_T read = 0;
    return ReadProcessMemory(
               GetCurrentProcess(), object + kTimeoutBlockOffset, values.data(), kBlockBytes, &read)
               != FALSE
           && read == kBlockBytes;
}

/**
 * Writes one run of milliseconds into the object.
 * @param object Config object.
 * @param values Lanes in block order.
 * @return True when Windows copied all of them.
 */
[[nodiscard]] bool write_block(std::byte* object, const Lanes& values) noexcept {
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(),
                              object + kTimeoutBlockOffset,
                              values.data(),
                              kBlockBytes,
                              &written)
               != FALSE
           && written == kBlockBytes;
}

/**
 * Takes the Client's own lanes, which are any lanes this module did not write.
 * @param current Block just read out of the object.
 */
void capture_locked(const Lanes& current) noexcept {
    // An all-zero block is an object the Client has published but not filled in. One zero lane is
    // the Client's own way of switching that lane off, so it is captured like any other value.
    const bool authored = std::any_of(
        current.begin(), current.end(), [](std::uint32_t value) noexcept { return value != 0; });
    if (!authored || (g_holding && current == kHeldLanes)) {
        return;
    }
    g_captured = current;
    g_capturedValid = true;
}

/**
 * Writes the captured lanes back and ends the hold.
 * @return True when lanes were put back, false when there was no hold to end.
 */
[[nodiscard]] bool release_locked(std::byte* object) noexcept {
    if (!g_holding || !g_capturedValid || !write_block(object, g_captured)) {
        return false;
    }
    g_holding = false;
    return true;
}

} // namespace

/** Resolves the activity config getter. */
bool install() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_getter != nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    std::byte* const match = scan_main_image_unique(kConfigGetter, "inactivity_config_getter");
    if (match == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=inactivity stage=install result=fail reason=target");
        return false;
    }
    g_getter = reinterpret_cast<ConfigGetter>(match);
    ReleaseSRWLockExclusive(&g_lock);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=inactivity stage=install result=ok");
    return true;
}

/** Puts the Client's own lanes back and drops the resolved getter. */
void uninstall() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (std::byte* const object = config_object(); object != nullptr) {
        // Nothing to report on the way out; the lanes are put back or there was no hold.
        (void)release_locked(object);
    }
    g_getter = nullptr;
    g_nextHoldTick = 0;
    g_captured = Lanes{};
    g_capturedValid = false;
    g_holding = false;
    g_intentHolding = false;
    g_intentValid = false;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Holds every lane at its longest, or puts back the ones the Client authored. */
void poll() noexcept {
    const bool holding = client::player::get().antiAfkEnabled;
    AcquireSRWLockExclusive(&g_lock);
    const std::uint64_t now = GetTickCount64();
    // A changed setting is the operator waiting on this call, so it does not wait for the interval.
    const bool changed = !g_intentValid || g_intentHolding != holding;
    if (g_getter == nullptr || (now < g_nextHoldTick && !changed)) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    g_nextHoldTick = now + kHoldIntervalMs;
    // Recorded before the object is reached, so a poll that finds no activity cannot leave the
    // setting looking changed and skip the interval on every later frame.
    g_intentHolding = holding;
    g_intentValid = true;
    std::byte* const object = config_object();
    if (object == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (Lanes current{}; read_block(object, current)) {
        capture_locked(current);
    }
    if (!holding) {
        const bool released = release_locked(object);
        ReleaseSRWLockExclusive(&g_lock);
        // Nothing to put back is not a failure: it is the ordinary state with the feature off.
        if (changed) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             released ? "ev=inactivity stage=release result=ok"
                                      : "ev=inactivity stage=release result=noop");
        }
        return;
    }
    // Held rather than written once, because an activity change re-authors these lanes.
    const bool wrote = write_block(object, kHeldLanes);
    if (wrote) {
        g_holding = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) {
        // Only on a change, so a steady hold does not fill the log every interval.
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         wrote ? "ev=inactivity stage=hold result=ok"
                               : "ev=inactivity stage=hold result=fail");
    }
}

} // namespace sunrise::client::hooks::inactivity
