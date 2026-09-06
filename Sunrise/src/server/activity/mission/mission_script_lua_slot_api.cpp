#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../../middleware/bap/activity_message/auth_schema_catalog.h"
#include "../../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../../middleware/encoding/bit_writer.h"
#include "../../../state/activity_sdk/format.h"
#include "../../../state/activity_sdk/runtime.h"
#include "mission_script_lua_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

namespace format = state::activity_sdk::format;
namespace slot_transport = middleware::bap::activity_message::sensor_auth_update;
namespace scriptable_auth = middleware::bap::activity_message::scriptable_auth;
namespace auth_catalog = middleware::bap::activity_message::auth_schema_catalog;

namespace {
/** @return True when one live Slot row is an exact type-23 device. */
[[nodiscard]] bool exact_device_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kDeviceSlotType
           && definition.componentClass == format::kDeviceComponentClass
           && definition.senseSchema == format::kDeviceSenseSchema
           && definition.authSchema == format::kDeviceAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-4 authored object. */
[[nodiscard]] bool exact_object_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kObjectSlotType
           && definition.componentClass == format::kObjectComponentClass
           && definition.senseSchema == format::kObjectSenseSchema
           && definition.authSchema == format::kObjectAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-31 configured trigger. */
[[nodiscard]] bool exact_trigger_slot(const SlotDefinition& definition) noexcept {
    namespace auth = middleware::bap::activity_message::scriptable_auth;
    return definition.slotType == auth::kType31SlotType
           && definition.authSchema == auth::kType31Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_sequence_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kSequenceSlotType
           && definition.componentClass == format::kSequenceComponentClass
           && definition.authSchema == format::kSequenceAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_cinematic_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kCinematicSlotType
           && definition.componentClass == format::kCinematicComponentClass
           && definition.authSchema == format::kCinematicAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-3 objective reset control. */
[[nodiscard]] bool exact_objective_reset_slot(const SlotDefinition& definition) noexcept {
    namespace auth = middleware::bap::activity_message::scriptable_auth;
    return definition.slotType == auth::kType3SlotType
           && definition.authSchema == auth::kType3Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_task_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kTaskSlotType
           && definition.componentClass == format::kTaskComponentClass
           && definition.authSchema == format::kTaskAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_dialogue_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kDialogueSlotType
           && definition.componentClass == format::kDialogueComponentClass
           && definition.authSchema == format::kDialogueAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0
           && (definition.flags & format::kSlotDialogueCuesExact) != 0;
}

/** @return True when one live Slot row is an exact type-30 occupancy condition. */
[[nodiscard]] bool exact_occupancy_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kOccupancySlotType
           && definition.componentClass == format::kOccupancyComponentClass
           && definition.senseSchema == format::kOccupancySenseSchema
           && definition.authSchema == format::kOccupancyAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-68 HUD directive state. */
[[nodiscard]] bool exact_directive_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType68SlotType
           && definition.authSchema == scriptable_auth::kType68Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-70 engagement observer. */
[[nodiscard]] bool exact_engagement_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType70SlotType
           && definition.authSchema == scriptable_auth::kType70Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-71 public-event sensor. */
[[nodiscard]] bool exact_public_event_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType71SlotType
           && definition.authSchema == scriptable_auth::kType71Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-42 performance sensor. */
[[nodiscard]] bool exact_performance_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType42SlotType
           && definition.componentClass == scriptable_auth::kType42ComponentClass
           && definition.authSchema == scriptable_auth::kType42Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** The occupancy Auth body is a fixed 87 bits: a 55-bit client reference then one int32. */
constexpr std::size_t kOccupancyAuthBitCount = 87;
constexpr std::size_t kOccupancyAuthByteCount = 11;
} // namespace

/** @return True when one live Slot row is an exact type-2 combatant. */
[[nodiscard]] bool exact_combatant_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType2SlotType
           && definition.componentClass == scriptable_auth::kType2ComponentClass
           && definition.senseSchema == scriptable_auth::kType2SenseSchema
           && definition.authSchema == scriptable_auth::kType2Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** Stages one authored native Auth body on the guarded message-5 route. */
[[nodiscard]] int queue_slot_auth(lua_State* state,
                                  const SlotDefinition& slot,
                                  std::uint32_t schema,
                                  std::size_t bitCount,
                                  std::span<const std::byte> body) {
    Impl* const impl = impl_from_state(state);
    std::array<std::byte, 32> sdkBuildSha256{};
    if (!decode_sdk_build_sha256(std::string_view(impl->identity.sdkBuildId.data()),
                                 sdkBuildSha256)) {
        return luaL_error(state, "loaded SDK generation identity is invalid");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::applySlotAuth;
    intent.sdkBuildSha256 = sdkBuildSha256;
    intent.firstRow = slot.nativeRow;
    intent.objectTag = slot.objectTag;
    intent.registryKey = slot.registryKey;
    intent.authSchema = schema;
    intent.authBitCount = static_cast<std::uint16_t>(bitCount);
    intent.slotIndex = static_cast<std::uint16_t>(slot.slotIndex);
    intent.slotType = static_cast<std::uint8_t>(slot.slotType);
    intent.authByteCount = static_cast<std::uint16_t>(body.size());
    try {
        intent.authBody.assign(body.begin(), body.end());
    } catch (const std::bad_alloc&) {
        return luaL_error(state, "mission Auth body allocation failed");
    }
    return queue_intent(state, frame, intent);
}

/** Sets the authored object filter and caller value carried by one type-30 condition. */
[[nodiscard]] int slot_set_occupancy_condition(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 2> kDeclared{"filter", "value"};
    refuse_unknown_arguments(state, kDeclared);
    const SlotHandle reference = checked_argument<SlotHandle>(state, "filter", kSlotMetatable);
    const lua_Integer value = checked_integer_argument(state, "value");
    SlotDefinition slot{};
    SlotDefinition playerSet{};
    if (!current_slot(state, *handle, slot) || !current_slot(state, reference, playerSet)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_occupancy_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-30 occupancy condition");
    }
    // The Auth field is a full-range int32 and lua_Integer is wider, so the lane is still checked.
    if (value < (std::numeric_limits<std::int32_t>::min)()
        || value > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "value must be a 32-bit signed integer");
    }
    std::array<std::byte, kOccupancyAuthByteCount> body{};
    middleware::encoding::bits::Writer writer(body);
    const std::uint32_t encodedValue =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(value)) + 0x80000000U;
    if (!writer.write(playerSet.registryKey, 32)
        || !writer.write(static_cast<std::uint32_t>(playerSet.slotType) + 1U, 7)
        || !writer.write(static_cast<std::uint32_t>(playerSet.slotIndex) + 32768U, 16)
        || !writer.write(encodedValue, 32) || writer.bit_count() != kOccupancyAuthBitCount) {
        return luaL_error(state, "occupancy condition native encoder failed");
    }
    return queue_slot_auth(state, slot, format::kOccupancyAuthSchema, kOccupancyAuthBitCount, body);
}

/** Reads one integer field from a generated directive declaration. */
[[nodiscard]] lua_Integer directive_integer(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    const lua_Integer value = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return value;
}

/** Shows one generated directive through the exact type-68 Auth schema. */
[[nodiscard]] int slot_set_directive(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 2> kDeclared{"directive", "state"};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_directive_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-68 directive state");
    }
    lua_getfield(state, 2, "directive");
    luaL_checktype(state, -1, LUA_TTABLE);
    const lua_Integer slotRow = directive_integer(state, -1, "slot_row");
    const lua_Integer nameHash = directive_integer(state, -1, "name_hash");
    const lua_Integer element = directive_integer(state, -1, "element");
    lua_pop(state, 1);
    const lua_Integer directiveState = optional_integer_argument(state, "state", 0);
    if (slotRow < 0 || slotRow > (std::numeric_limits<std::uint32_t>::max)() || nameHash < 0
        || nameHash > (std::numeric_limits<std::uint32_t>::max)() || element < 0
        || element > (std::numeric_limits<std::int32_t>::max)() || directiveState < 0
        || directiveState > 2) {
        return luaL_error(state, "directive declaration is outside its native field width");
    }
    Impl* const impl = impl_from_state(state);
    DirectiveElementDefinition resolved{};
    if (impl == nullptr || impl->definitions.resolveDirectiveElement == nullptr
        || !impl->definitions.resolveDirectiveElement(impl->definitions.context,
                                                      static_cast<std::uint32_t>(slotRow),
                                                      static_cast<std::uint32_t>(nameHash),
                                                      static_cast<std::int32_t>(element),
                                                      resolved)
        || resolved.slotRow != slot.nativeRow) {
        return luaL_error(state, "directive does not belong to this slot");
    }
    scriptable_auth::Type68Preset preset{.nameHash = resolved.nameHash,
                                         .elementIndex = resolved.elementIndex,
                                         .state = static_cast<std::int8_t>(directiveState),
                                         .visible = true};
    std::array<std::byte, scriptable_auth::kType68ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type68(preset, body, written) || written != body.size()) {
        return luaL_error(state, "directive native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType68Schema, scriptable_auth::kType68BitCount, body);
}

/** Hides the active directive without naming an authored element. */
[[nodiscard]] int slot_clear_directives(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_directive_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-68 directive state");
    }
    scriptable_auth::Type68Preset preset{};
    preset.visible = false;
    std::array<std::byte, scriptable_auth::kType68ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type68(preset, body, written) || written != body.size()) {
        return luaL_error(state, "directive native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType68Schema, scriptable_auth::kType68BitCount, body);
}

/** Restores one encounter engagement sensor with script-declared native initial values. */
[[nodiscard]] int slot_set_engagement_state(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_engagement_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-70 engagement state");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 2> kDeclared{"flags", "revision"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer flags = optional_integer_argument(state, "flags", 1);
    const lua_Integer revision = optional_integer_argument(state, "revision", 1);
    if (flags < 0 || flags > 0x1F || revision < (std::numeric_limits<std::int16_t>::min)()
        || revision > (std::numeric_limits<std::int16_t>::max)()) {
        return luaL_error(state, "engagement state is outside its native field width");
    }
    const scriptable_auth::Type70Preset preset{
        .flags = static_cast<std::uint8_t>(flags),
        .revision = static_cast<std::int16_t>(revision),
    };
    std::array<std::byte, scriptable_auth::kType70ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type70(preset, body, written) || written != body.size()) {
        return luaL_error(state, "engagement native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType70Schema, scriptable_auth::kType70BitCount, body);
}

/** Names the player, the event area and the leave timeout one public-event sensor watches. */
[[nodiscard]] int slot_set_public_event_state(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_public_event_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-71 public-event sensor");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 4> kDeclared{
        "state", "player", "area", "leave_seconds"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer eventState = optional_integer_argument(state, "state", 0);
    if (eventState < (std::numeric_limits<std::int32_t>::min)()
        || eventState > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "state must be a 32-bit signed integer");
    }
    // Absent, the sensor watches this link's own player. Present, it is a decimal string, the
    // form every 64-bit key crosses into Lua in.
    std::uint64_t player = impl_from_state(state)->identity.playerKey;
    if (push_argument(state, "player") != LUA_TNIL) {
        lua_pop(state, 1);
        const std::string_view playerText = borrowed_string_argument(state, "player");
        const auto parsed =
            std::from_chars(playerText.data(), playerText.data() + playerText.size(), player);
        if (parsed.ec != std::errc{} || parsed.ptr != playerText.data() + playerText.size()) {
            return luaL_error(state, "player must be a decimal player key string");
        }
        lua_pop(state, 1);
    } else {
        lua_pop(state, 1);
    }
    if (player == 0) {
        return luaL_error(state, "no player key is known for this activity link");
    }
    const SlotHandle areaHandle = checked_argument<SlotHandle>(state, "area", kSlotMetatable);
    const lua_Number seconds = checked_number_argument(state, "leave_seconds");
    SlotDefinition area{};
    if (!current_slot(state, areaHandle, area)) {
        return luaL_error(state, "area slot is stale or invalid");
    }
    if (!std::isfinite(seconds) || seconds < 0.0
        || seconds > static_cast<lua_Number>((std::numeric_limits<float>::max)())) {
        return luaL_error(state, "leave_seconds must be a finite non-negative number");
    }
    const scriptable_auth::Type71Body body{
        .state = static_cast<std::int32_t>(eventState),
        .playerIdentity = player,
        .areaRegistryKey = area.registryKey,
        .areaSlotType = static_cast<std::uint8_t>(area.slotType),
        .areaSlotIndex = static_cast<std::uint16_t>(area.slotIndex),
        .leaveSeconds = static_cast<float>(seconds),
    };
    std::array<std::byte, scriptable_auth::kType71ByteCount> bytes{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type71(body, bytes, written) || written != bytes.size()) {
        return luaL_error(state, "public-event native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType71Schema, scriptable_auth::kType71BitCount, bytes);
}

/** Binds one exact combatant to its package-authored squad member. */
[[nodiscard]] int slot_bind_combatant_to_squad(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_combatant_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-2 combatant");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::bindCombatantToSquad;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/**
 * Reads the optional `with` list: more object slots that answer on this one's revision.
 * @return False with the Lua error already raised.
 */
[[nodiscard]] bool parse_object_burst(lua_State* state, Intent& intent) {
    lua_getfield(state, 2, "with");
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        static_cast<void>(luaL_argerror(state, 2, "object burst list is not a table"));
        return false;
    }
    const int list = lua_gettop(state);
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(state, list));
    if (count < 0
        || static_cast<std::size_t>(count)
               > ::sunrise::state::activity::mission::kIntentBurstCapacity) {
        lua_pop(state, 1);
        static_cast<void>(luaL_argerror(state, 2, "object burst list is too long"));
        return false;
    }
    for (lua_Integer entry = 1; entry <= count; ++entry) {
        lua_rawgeti(state, list, entry);
        SlotDefinition member{};
        const bool resolved =
            resolve_slot(state, lua_gettop(state), member) && exact_object_slot(member);
        lua_pop(state, 1);
        if (!resolved) {
            lua_pop(state, 1);
            static_cast<void>(
                luaL_argerror(state, 2, "object burst names a slot that is not an exact type-4"));
            return false;
        }
        intent.burstRows[static_cast<std::size_t>(entry - 1)] = member.nativeRow;
    }
    lua_pop(state, 1);
    intent.burstRowCount = static_cast<std::uint8_t>(count);
    return true;
}

/** Instantiates or removes the package-owned entry one type-4 slot selects. */
[[nodiscard]] int slot_set_object_active(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_object_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-4 authored object");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 2> kDeclared{"active", "with"};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setObjectActive;
    intent.firstRow = definition.nativeRow;
    intent.entryIndex = 0;
    intent.active = optional_boolean_argument(state, "active", true);
    if (!parse_object_burst(state, intent)) {
        return 0;
    }
    return queue_intent(state, frame, intent);
}

/** Sets one verified type-23 channel through the same guarded route. */
[[nodiscard]] int slot_set_channel(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_device_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-23 device");
    }
    // Both parameters carry their own bound, so neither the lane nor the range is tested here.
    static constexpr std::array<std::string_view, 3> kDeclared{"channel", "value", "snap"};
    refuse_unknown_arguments(state, kDeclared);
    const DeviceChannelHandle channel =
        checked_argument<DeviceChannelHandle>(state, "channel", kDeviceChannelMetatable);
    const UnitScalarHandle value =
        checked_argument<UnitScalarHandle>(state, "value", kUnitScalarMetatable);
    const bool snap = optional_boolean_argument(state, "snap", false);

    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setDeviceChannel;
    intent.firstRow = definition.nativeRow;
    intent.deviceValue = value.value;
    intent.deviceChannel = channel.channel;
    intent.deviceSnap = snap;
    return queue_intent(state, frame, intent);
}
/** Applies one named device transition through the same guarded channel route. */
[[nodiscard]] int slot_transition(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_device_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-23 device");
    }
    // The handle is a row of the closed vocabulary, so no word is matched here.
    static constexpr std::array<std::string_view, 2> kDeclared{"transition", "snap"};
    refuse_unknown_arguments(state, kDeclared);
    const DeviceTransitionHandle requested =
        checked_argument<DeviceTransitionHandle>(state, "transition", kDeviceTransitionMetatable);
    const bool snap = optional_boolean_argument(state, "snap", false);
    const DeviceTransition& transition = kDeviceTransitions[requested.row];
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setDeviceChannel;
    intent.firstRow = definition.nativeRow;
    intent.deviceValue = transition.value;
    intent.deviceChannel = static_cast<std::uint8_t>(transition.channel);
    intent.deviceSnap = snap;
    return queue_intent(state, frame, intent);
}
/**
 * Fires one type-31 configured trigger. One authored typed reference must be live and eligible.
 * The pulse carries no caller value. `enabled` is fixed true and the auxiliary stays zero, and
 * the Host mints the generation from the target's own guard so a replay cannot reorder.
 */
[[nodiscard]] int slot_fire_trigger(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_trigger_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-31 trigger");
    }
    // The pulse has no parameters, so an unsafe call cannot be spelled.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::fireTrigger;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Lua `play_sequence` on a slot. Errors unless the slot is an exact type-5 sequence. */
[[nodiscard]] int slot_play_sequence(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_sequence_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-5 authored sequence");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playSequence;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Lua `set_cinematic_active` on a slot. Errors unless the slot is an exact cinematic. */
[[nodiscard]] int slot_set_cinematic_active(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_cinematic_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-6 authored cinematic");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 1> kDeclared{"active"};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setCinematicActive;
    intent.firstRow = definition.nativeRow;
    intent.active = optional_boolean_argument(state, "active", true);
    return queue_intent(state, frame, intent);
}

/** Queues the parameter-free reset of every objective lane owned by one type-3 slot. */
[[nodiscard]] int slot_reset_objectives(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_objective_reset_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-3 objective reset");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::resetObjectives;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Advances the exact objective bit authored by one type-38 task slot. */
[[nodiscard]] int slot_advance_task(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_task_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-38 authored task");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::advanceTask;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/**
 * Starts one state of the actor a type-42 sensor drives. With no `state` the slot's target must
 * declare exactly one state; a generated `state` row must belong to this slot.
 */
[[nodiscard]] int slot_play_performance(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_performance_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-42 performance sensor");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 1> kDeclared{"state"};
    refuse_unknown_arguments(state, kDeclared);
    lua_Integer slotRow = static_cast<lua_Integer>(definition.nativeRow);
    lua_Integer nameHash = 0;
    if (!lua_isnoneornil(state, 2)) {
        lua_getfield(state, 2, "state");
        if (!lua_isnil(state, -1)) {
            luaL_checktype(state, -1, LUA_TTABLE);
            slotRow = directive_integer(state, -1, "slot_row");
            nameHash = directive_integer(state, -1, "name_hash");
        }
        lua_pop(state, 1);
    }
    if (slotRow < 0 || slotRow > (std::numeric_limits<std::uint32_t>::max)() || nameHash < 0
        || nameHash > (std::numeric_limits<std::uint32_t>::max)()) {
        return luaL_error(state, "performance state declaration is outside its native field width");
    }
    Impl* const impl = impl_from_state(state);
    PerformanceStateDefinition resolved{};
    if (impl == nullptr || impl->definitions.resolvePerformanceState == nullptr
        || !impl->definitions.resolvePerformanceState(impl->definitions.context,
                                                      static_cast<std::uint32_t>(slotRow),
                                                      static_cast<std::uint32_t>(nameHash),
                                                      resolved)
        || resolved.slotRow != definition.nativeRow) {
        return luaL_error(state, "performance state does not belong to this slot");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playPerformance;
    intent.firstRow = definition.nativeRow;
    intent.secondRow = resolved.nameHash;
    return queue_intent(state, frame, intent);
}

/** Fires one bounded cue from an exact type-53 authored dialogue list. */
[[nodiscard]] int slot_play_dialogue_cue(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_dialogue_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-53 authored dialogue");
    }
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 1> kDeclared{"cue"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer cue = checked_integer_argument(state, "cue");
    if (cue < 0 || cue > (std::numeric_limits<std::uint16_t>::max)()) {
        return luaL_error(state, "dialogue cue must be a non-negative 16-bit integer");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playDialogueCue;
    intent.firstRow = definition.nativeRow;
    intent.secondRow = static_cast<std::uint32_t>(cue);
    return queue_intent(state, frame, intent);
}
/** Reads one Slot row member, its syntax methods, and its authorized actions. */
[[nodiscard]] int slot_index(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale");
    }
    const std::string_view key = lua_string_view(state, 2);
    if (key == "row") {
        lua_pushinteger(state, definition.localRow);
    } else if (key == "id") {
        lua_pushlstring(state, definition.id.data(), definition.id.size());
    } else if (key == "name") {
        lua_pushlstring(state, definition.name.data(), definition.name.size());
    } else if (key == "object_id") {
        lua_pushlstring(state, definition.objectId.data(), definition.objectId.size());
    } else if (key == "object_tag") {
        lua_pushinteger(state, definition.objectTag);
    } else if (key == "registry_key") {
        lua_pushinteger(state, definition.registryKey);
    } else if (key == "slot_index") {
        lua_pushinteger(state, definition.slotIndex);
    } else if (key == "slot_type") {
        lua_pushinteger(state, definition.slotType);
    } else if (key == "component_class") {
        lua_pushinteger(state, definition.componentClass);
    } else if (key == "sense_schema") {
        lua_pushinteger(state, definition.senseSchema);
    } else if (key == "auth_schema") {
        lua_pushinteger(state, definition.authSchema);
    } else if (key == "sense_schema_id") {
        lua_pushlstring(state, definition.senseSchemaId.data(), definition.senseSchemaId.size());
    } else if (key == "auth_schema_id") {
        lua_pushlstring(state, definition.authSchemaId.data(), definition.authSchemaId.size());
    } else if (key == "auth_type" || key == "auth_min_bits" || key == "auth_max_bits"
               || key == "auth_component_offset" || key == "auth_dynamic"
               || key == "auth_writable") {
        const auth_catalog::Type* const auth = auth_catalog::find(
            static_cast<std::uint8_t>(definition.slotType), definition.authSchema);
        if (auth == nullptr) {
            lua_pushnil(state);
        } else if (key == "auth_type") {
            lua_pushlstring(state, auth->name.data(), auth->name.size());
        } else if (key == "auth_min_bits") {
            lua_pushinteger(state, auth->minimumBits);
        } else if (key == "auth_max_bits") {
            lua_pushinteger(state, auth->maximumBits);
        } else if (key == "auth_component_offset") {
            if (auth->hasContiguousMirror) {
                lua_pushinteger(state, auth->componentOffset);
            } else {
                lua_pushnil(state);
            }
        } else if (key == "auth_dynamic") {
            lua_pushboolean(state, auth->hasDynamicBody);
        } else {
            lua_pushboolean(state, auth->writable);
        }
    } else if (key == "flags") {
        lua_pushinteger(state, definition.flags);
    } else if (key == "set_object_active") {
        lua_pushcfunction(state, &slot_set_object_active);
    } else if (key == "set_channel") {
        lua_pushcfunction(state, &slot_set_channel);
    } else if (key == "transition") {
        lua_pushcfunction(state, &slot_transition);
    } else if (key == "set_occupancy_condition") {
        lua_pushcfunction(state, &slot_set_occupancy_condition);
    } else if (key == "set_directive") {
        lua_pushcfunction(state, &slot_set_directive);
    } else if (key == "clear_directives") {
        lua_pushcfunction(state, &slot_clear_directives);
    } else if (key == "set_engagement_state") {
        lua_pushcfunction(state, &slot_set_engagement_state);
    } else if (key == "set_public_event_state") {
        lua_pushcfunction(state, &slot_set_public_event_state);
    } else if (key == "bind_combatant_to_squad") {
        lua_pushcfunction(state, &slot_bind_combatant_to_squad);
    } else if (key == "run_atoms") {
        lua_pushcfunction(state, &slot_run_atoms);
    } else if (key == "fire_trigger") {
        lua_pushcfunction(state, &slot_fire_trigger);
    } else if (key == "play_sequence") {
        lua_pushcfunction(state, &slot_play_sequence);
    } else if (key == "set_cinematic_active") {
        lua_pushcfunction(state, &slot_set_cinematic_active);
    } else if (key == "reset_objectives") {
        lua_pushcfunction(state, &slot_reset_objectives);
    } else if (key == "advance_task") {
        lua_pushcfunction(state, &slot_advance_task);
    } else if (key == "play_dialogue_cue") {
        lua_pushcfunction(state, &slot_play_dialogue_cue);
    } else if (key == "play_performance") {
        lua_pushcfunction(state, &slot_play_performance);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

void register_slot_metatables(lua_State* state) {
    register_metatable(state, kSlotMetatable, &slot_index);
}
} // namespace sunrise::server::activity::mission::lua_vm::detail
