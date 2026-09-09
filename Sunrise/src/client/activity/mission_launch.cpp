#include "mission_launch.h"

#include <Windows.h>

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

#include "../../core/logging/log.h"
#include "../../state/activity/forced/activity_forced_destination.h"
#include "../../state/activity/runtime.h"
#include "../../state/build_data/activities/activity_catalog.h"
#include "../../state/build_data/runtime.h"
#include "mission_launch_options.h"

namespace sunrise::client::activity::mission_launch {
namespace {
SRWLOCK g_lock{SRWLOCK_INIT};
Snapshot g_state{};
std::uint64_t g_requestedAt{};
// Game-thread-only receipt state, scoped to a successfully submitted request.
state::activity::SessionBinding g_previousSession{};
bool g_leftOrbit{};
ManualScratch g_manualScratch{}; // Game-frame owner only, outside the UI arena and native stack.
using World = std::uintptr_t(__fastcall*)();
using Ready = bool(__fastcall*)(std::uintptr_t);
using Record = std::uintptr_t(__fastcall*)(std::uint32_t);
using Construct = void*(__fastcall*)(void*, std::uint32_t, std::int16_t);
using Valid = bool(__fastcall*)(const void*);
using Name = const char*(__fastcall*)(std::int16_t);
using Clear = void(__fastcall*)();
using Select = void(__fastcall*)(std::uint8_t, const void*);
using Commit = void(__fastcall*)(std::int32_t);
using Step = std::int32_t(__fastcall*)();
state::activity::SessionBinding newest_joined_session() noexcept {
    std::array<state::activity::SessionRosterRow, state::activity::kSessionCapacity> rows{};
    std::size_t count{};
    state::activity::SessionBinding newest{};
    if (!state::activity::snapshot_session_roster(rows, count)) {
        return newest;
    }
    for (const auto& row : std::span(rows).first(count)) {
        if (row.joined && row.binding.createdRevision > newest.createdRevision) {
            newest = row.binding;
        }
    }
    return newest;
}
template <class T> bool read(std::uintptr_t address, T& value) noexcept {
    SIZE_T copied{};
    return address != 0
           && ReadProcessMemory(GetCurrentProcess(),
                                std::bit_cast<const void*>(address),
                                &value,
                                sizeof(value),
                                &copied)
                  != FALSE
           && copied == sizeof(value);
}
template <class F>
F resolve(std::uintptr_t base, std::uintptr_t rva, std::array<unsigned char, 8> expected) noexcept {
    std::array<unsigned char, 8> actual{};
    return read(base + rva, actual) && actual == expected ? std::bit_cast<F>(base + rva) : nullptr;
}
void finish(Status status) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_state.status = status;
    g_state.busy = status == Status::queued;
    const auto state = g_state;
    ReleaseSRWLockExclusive(&g_lock);
    std::array<char, 512> line{};
    const int size =
        state.manual
            ? std::snprintf(line.data(),
                            line.size(),
                            "ev=mission_launch activity=%u manual=1 destination=%.*s bubble=%u "
                            "slice=%u spawn=%08X status=%u detail=%s",
                            state.index,
                            static_cast<int>(destination_name(state.destination).size()),
                            state.destination.packageName.data(),
                            state.destination.bubble,
                            state.destination.sliceSet,
                            state.destination.hasSpawnSetHash ? state.destination.spawnSetHash
                                                              : forced::kAbsentSpawnSetHash,
                            static_cast<unsigned>(status),
                            description(status))
            : std::snprintf(line.data(),
                            line.size(),
                            "ev=mission_launch activity=%u status=%u detail=%s",
                            state.index,
                            static_cast<unsigned>(status),
                            description(status));
    if (size > 0 && static_cast<std::size_t>(size) < line.size()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(size)});
    }
}
} // namespace
Snapshot snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    const auto result = g_state;
    ReleaseSRWLockShared(&g_lock);
    return result;
}
bool request(std::uint16_t index) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_state.busy) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_state = {Status::requested, index, true};
    g_requestedAt = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}
bool request_manual(std::uint16_t index, const forced::ForcedDestination& destination) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_state.busy) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_state = {Status::requested, index, true, true, destination};
    g_requestedAt = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}
void poll() noexcept {
    const auto state = snapshot();
    if (!state.busy) {
        return;
    }
    // Requests are immutable until this owner completes them; the timestamp follows that lock.
    AcquireSRWLockShared(&g_lock);
    const auto started = g_requestedAt;
    ReleaseSRWLockShared(&g_lock);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto step =
        resolve<Step>(base, 0xE2D510, {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x07, 0x83, 0x00});
    if (!step) {
        finish(Status::nativeUnavailable);
        return;
    }
    const auto currentStep = step();
    const auto rows = state::build_data::activities::entries();
    if (state.status == Status::queued) {
        g_leftOrbit = g_leftOrbit || currentStep != 29;
        const auto session = newest_joined_session();
        const auto& destination = session.destination;
        if (g_leftOrbit && currentStep == 38 && session.sessionId != 0
            && !state::activity::same_binding(session, g_previousSession)
            && state.index < rows.size()
            && destination.activityIndex == static_cast<std::int16_t>(state.index)
            && destination.packageNameLength
                   == (state.manual ? destination_name(state.destination)
                                    : rows[state.index].name())
                          .size()
            && std::memcmp(destination.packageName.data(),
                           state.manual ? state.destination.packageName.data()
                                        : rows[state.index].package.data(),
                           destination.packageNameLength)
                   == 0
            && (!state.manual
                || ((!state.destination.hasBubble
                     || (destination.hasArrivalBubbleOverride
                         && destination.arrivalBubbleOverride == state.destination.bubble))
                    && (!state.destination.hasSliceSet
                        || (destination.hasSliceSetOverride
                            && destination.sliceSetOverride == state.destination.sliceSet))
                    && (!state.destination.hasBubble
                        || (destination.hasSpawnSetOverride
                            && destination.spawnSetOverride
                                   == (state.destination.hasSpawnSetHash
                                           ? state.destination.spawnSetHash
                                           : forced::kAbsentSpawnSetHash)))))) {
            finish(Status::arrived);
            return;
        }
        if (GetTickCount64() - started > 120000) {
            finish(Status::timedOut);
        }
        return;
    }
    if (rows.empty()) {
        finish(Status::catalogUnavailable);
        return;
    }
    if (state.index >= rows.size() || rows[state.index].name().empty()) {
        finish(Status::entryUnavailable);
        return;
    }
    state::build_data::scenarios::Definition layout{};
    if (!state::build_data::find_scenario_layout(rows[state.index].name(), layout)) {
        finish(Status::entryUnavailable);
        return;
    }
    if (state.manual) {
        if (!manual_transport_valid(state.index, state.destination, rows)
            || validate_manual(state.destination, g_manualScratch) != ManualError::none) {
            finish(Status::manualRejected);
            return;
        }
    } else {
        forced::ForcedDestination effective{};
        forced::snapshot(effective);
        if (forced::active(effective) || forced::override_active()) {
            finish(Status::overrideActive);
            return;
        }
    }
    // Captured retail setup:orbit is 29. In-world exit/reclassification belongs to the native
    // activity lifecycle and is deliberately not synthesized by this UI request adapter.
    if (currentStep != 29) {
        finish(Status::returnToOrbit);
        return;
    }
    const auto world =
        resolve<World>(base, 0xC03430, {0x40, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B});
    const auto sessionReady =
        resolve<Ready>(base, 0x1788810, {0x83, 0xB9, 0x6C, 0x08, 0x00, 0x00, 0x00, 0x0F});
    const auto memberReady =
        resolve<Ready>(base, 0x178D740, {0x4C, 0x8B, 0xC1, 0x48, 0x63, 0x89, 0x3C, 0xE9});
    const auto record =
        resolve<Record>(base, 0xBFA030, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xD9});
    const auto construct =
        resolve<Construct>(base, 0xC061D0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74});
    const auto valid =
        resolve<Valid>(base, 0x4D5460, {0x0F, 0xB6, 0x11, 0xB0, 0x01, 0x80, 0xFA, 0xFF});
    const auto name =
        resolve<Name>(base, 0xDDECA0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83});
    const auto clear =
        resolve<Clear>(base, 0xBF95D0, {0x48, 0x83, 0xEC, 0x38, 0xE8, 0xE7, 0xE0, 0x7F});
    const auto select =
        resolve<Select>(base, 0xBFB1F0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74});
    const auto commit =
        resolve<Commit>(base, 0xBF97D0, {0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x38});
    if (!world || !sessionReady || !memberReady || !record || !construct || !valid || !name
        || !clear || !select || !commit) {
        finish(Status::nativeUnavailable);
        return;
    }
    const auto manager = world();
    std::int32_t primary{}, sessionState{}, member{};
    if (!manager || !read(manager + 0x10, primary) || primary < 0 || primary > 3) {
        finish(Status::notReady);
        return;
    }
    const auto session = manager + 0x18 + static_cast<std::uintptr_t>(primary) * 0x1C8A0;
    if (!read(session + 0x1AEF8, sessionState) || sessionState < 4 || sessionState > 9
        || !sessionReady(session) || !memberReady(session) || !read(session + 0xE93C, member)
        || member < 0 || member >= 12) {
        finish(Status::notReady);
        return;
    }
    const auto current = record(static_cast<std::uint32_t>(member));
    std::uint8_t launchState{};
    if (!current || !read(current + 0xA33, launchState) || launchState >= 3) {
        finish(Status::notReady);
        return;
    }
    const auto index = static_cast<std::int16_t>(state.index);
    std::array<char, 40> nativeName{};
    const auto package = reinterpret_cast<std::uintptr_t>(name(index));
    bool nameMatches = package != 0;
    for (std::size_t i = 0; nameMatches && i <= rows[state.index].name().size(); ++i) {
        nameMatches =
            read(package + i, nativeName[i]) && nativeName[i] == rows[state.index].package[i];
    }
    if (!nameMatches) {
        finish(Status::descriptorRejected);
        return;
    }
    alignas(16) std::array<std::byte, 0x120> selection{};
    if (construct(selection.data(), 0, index) != selection.data()) {
        finish(Status::descriptorRejected);
        return;
    }
    std::int16_t source{}, destination{};
    std::memcpy(&source, selection.data() + 2, sizeof(source));
    std::memcpy(&destination, selection.data() + 4, sizeof(destination));
    if (source != index || destination != index || selection[0] != std::byte{}
        || !valid(selection.data())) {
        finish(Status::descriptorRejected);
        return;
    }
    g_previousSession = newest_joined_session();
    g_leftOrbit = false;
    // Reuse the standalone override service only after all native readiness/descriptor checks.
    if (state.manual && !forced::publish(state.destination)) {
        finish(Status::manualRejected);
        return;
    }
    clear();
    select(0, selection.data());
    commit(1);
    finish(Status::queued);
}
const char* description(Status status) noexcept {
    switch (status) {
    case Status::idle:
        return "Choose an activity, return to orbit, then launch.";
    case Status::requested:
        return "Checking the native launch request...";
    case Status::queued:
        return "Submitted to the Director. Waiting for the native activity transition.";
    case Status::arrived:
        return "Native activity arrival confirmed.";
    case Status::catalogUnavailable:
        return "Activity catalog is still being extracted.";
    case Status::entryUnavailable:
        return "This entry has no available direct-launch scenario.";
    case Status::overrideActive:
        return "An Activity override is active. Disable it before launching this selection.";
    case Status::returnToOrbit:
        return "Return to orbit through the Director, then click Launch again.";
    case Status::nativeUnavailable:
        return "Native launch entry points are unavailable in this client build.";
    case Status::notReady:
        return "The fireteam is not ready to launch. Wait in orbit and try again.";
    case Status::descriptorRejected:
        return "The native client rejected this activity selection.";
    case Status::timedOut:
        return "No arrival confirmation was received. Check the Director before retrying.";
    case Status::manualRejected:
        return "The manual destination, bubble, slice, spawn or native launch route is no longer "
               "valid. Review the selection.";
    }
    return "Launch status unavailable.";
}
} // namespace sunrise::client::activity::mission_launch
