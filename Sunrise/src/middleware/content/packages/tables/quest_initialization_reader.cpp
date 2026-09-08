#include "quest_initialization_reader.h"

#include <limits>

#include "definition_index_table.h"
#include "internal.h"

namespace sunrise::middleware::content::packages::tables::items {
namespace {

using Quest = state::build_data::items::QuestInitialization;

// Serialized layout offsets are relative to the definition, block, or row named below,
// not process addresses. Class IDs identify the expected serialized block/element type.
constexpr std::size_t kMinimumQuestDefinitionSize = 0xF0;
constexpr std::size_t kBlockClassPrefixSize = sizeof(std::uint32_t);
constexpr std::size_t kMinimumQuestBlockSize = 0x20;

constexpr std::size_t kItemObjectiveBlockOffset = 0x30;
constexpr std::uint32_t kItemObjectiveBlockClass = 0x808077EBU;
constexpr std::size_t kObjectiveParentItemOffset = 0x1C;
constexpr std::size_t kObjectiveReferenceStride = sizeof(std::uint16_t);

constexpr std::size_t kItemQuestSetBlockOffset = 0x60;
constexpr std::uint32_t kQuestSetBlockClass = 0x808077C8U;
constexpr std::size_t kQuestSetValueSlotOffset = 0x10;
constexpr std::size_t kQuestSetModeOffset = 0x1C;
/** Only this authored mode is supported; no semantics are assumed for other modes. */
constexpr std::uint8_t kSupportedQuestSetMode = 1;
constexpr std::uint32_t kQuestSetMemberClass = 0x808077CAU;
constexpr std::size_t kQuestSetMemberStride = 8;
constexpr std::size_t kQuestSetMemberValueOffset = 0;
constexpr std::size_t kQuestSetMemberItemOffset = 4;
constexpr std::size_t kQuestSetMemberReservedOffset = 6;

constexpr std::size_t kItemUnlockBlockOffset = 0x90;
constexpr std::uint32_t kItemUnlockBlockClass = 0x808077ABU;
constexpr std::uint32_t kItemPresenceFlagClass = 0x80807D4BU;
constexpr std::size_t kItemPresenceFlagStride = sizeof(std::uint16_t);
/** Authored value/flag slots must fit the nonnegative range of a signed 16-bit mapping. */
constexpr std::uint16_t kUnlockSlotLimit = 0x8000U;
/** Separate objective-free roots are supported only in this native bucket. */
constexpr std::uint8_t kSeparateQuestRootBucketId = 37;

/** The remaining maps are scanned to reject matches outside account/character state. */
constexpr std::size_t kThirdValueMapDescriptor = 40;
constexpr std::size_t kFourthValueMapDescriptor = 56;
constexpr std::size_t kValueMapRowStride = 8;
constexpr std::size_t kValueMapSlotOffset = 4;
constexpr std::size_t kValueMapReservedOffset = 6;
constexpr std::uint16_t kUnavailableValueMapRow = 0xFFFFU;

/** These are serialized block pointers, not count/relative array descriptors. */
[[nodiscard]] bool block(std::span<const std::byte> bytes,
                         std::size_t field,
                         std::uint32_t expectedClass,
                         std::size_t& offset) noexcept {
    std::int64_t relative = 0;
    if (!read(bytes, field, relative) || relative == 0
        || relative
               > (std::numeric_limits<std::int64_t>::max)() - static_cast<std::int64_t>(field)) {
        return false;
    }
    const auto target = relative + static_cast<std::int64_t>(field);
    if (target < static_cast<std::int64_t>(kBlockClassPrefixSize)
        || static_cast<std::uint64_t>(target) > bytes.size()
        || bytes.size() - static_cast<std::size_t>(target) < kMinimumQuestBlockSize) {
        return false;
    }
    offset = static_cast<std::size_t>(target);
    std::uint32_t actualClass = 0;
    return read(bytes, offset - kBlockClassPrefixSize, actualClass) && actualClass == expectedClass;
}

/** Bounds an authored array using its element class and fixed stride. */
[[nodiscard]] bool array(std::span<const std::byte> bytes,
                         std::size_t field,
                         std::uint32_t expectedClass,
                         std::size_t stride,
                         Array& rows) noexcept {
    return find_array_at(bytes, field, rows) && rows.elementClass == expectedClass
           && rows.dataOffset <= bytes.size()
           && rows.count <= (bytes.size() - rows.dataOffset) / stride;
}

} // namespace

std::uint16_t quest_parent(std::span<const std::byte> definition) noexcept {
    std::uint8_t bucket = 0;
    std::size_t objective = 0;
    std::uint16_t parent = kUnavailableQuestParent;
    Array objectives{};
    if (definition.size() < kMinimumQuestDefinitionSize
        || !read(definition, kBucketIdOffset, bucket)
        || bucket != state::build_data::items::kPursuitBucketId
        || !block(definition, kItemObjectiveBlockOffset, kItemObjectiveBlockClass, objective)
        || !array(definition,
                  objective,
                  kObjectiveReferenceArrayClass,
                  kObjectiveReferenceStride,
                  objectives)
        || !read(definition, objective + kObjectiveParentItemOffset, parent)) {
        return kUnavailableQuestParent;
    }
    return parent;
}

Quest read_quest_initialization(std::span<const std::byte> definition,
                                std::uint16_t itemIndex,
                                std::span<const std::byte> parent,
                                std::size_t itemCount,
                                std::span<const std::byte> valueMap) noexcept {
    std::size_t set = 0;
    std::size_t unlock = 0;
    std::uint8_t mode = 0;
    std::uint16_t slot = 0;
    Array members{}, flags{};
    const auto parentIndex = quest_parent(definition);
    if (itemIndex >= itemCount || parentIndex >= itemCount
        || parent.size() < kMinimumQuestDefinitionSize
        || !block(parent, kItemQuestSetBlockOffset, kQuestSetBlockClass, set)
        || !read(parent, set + kQuestSetModeOffset, mode) || mode != kSupportedQuestSetMode
        || !read(parent, set + kQuestSetValueSlotOffset, slot) || slot >= kUnlockSlotLimit
        || !array(parent, set, kQuestSetMemberClass, kQuestSetMemberStride, members)
        || members.count > itemCount) {
        return {};
    }
    std::int64_t unlockRelative = 0;
    if (!read(definition, kItemUnlockBlockOffset, unlockRelative)
        || (unlockRelative != 0
            && (!block(definition, kItemUnlockBlockOffset, kItemUnlockBlockClass, unlock)
                || !find_optional_array_at(definition, unlock, flags)
                || (flags.count != 0
                    && !array(definition,
                              unlock,
                              kItemPresenceFlagClass,
                              kItemPresenceFlagStride,
                              flags))))) {
        return {};
    }
    for (std::size_t i = 0; i < flags.count; ++i) {
        std::uint16_t flag = 0;
        if (!read(definition, flags.dataOffset + i * kItemPresenceFlagStride, flag)
            || flag >= kUnlockSlotLimit) {
            return {};
        }
    }
    // A separate bucket-37 set root can track a pursuit without an item-presence flag.
    // Only the objective-free root / character-value form is supported here; this is
    // first-step initialization, not a general interaction or eligibility evaluator.
    const bool separateRoot = flags.count == 0;
    if (separateRoot) {
        std::uint8_t parentBucket = 0;
        std::int64_t parentObjective = 0;
        if (parentIndex == itemIndex || !read(parent, kBucketIdOffset, parentBucket)
            || parentBucket != kSeparateQuestRootBucketId
            || !read(parent, kItemObjectiveBlockOffset, parentObjective) || parentObjective != 0) {
            return {};
        }
    }
    Quest quest{};
    std::size_t matches = 0;
    for (std::size_t i = 0; i < members.count; ++i) {
        const auto at = members.dataOffset + i * kQuestSetMemberStride;
        std::int32_t value = 0;
        std::uint16_t member = 0, reserved = 0;
        if (!read(parent, at + kQuestSetMemberValueOffset, value)
            || !read(parent, at + kQuestSetMemberItemOffset, member)
            || !read(parent, at + kQuestSetMemberReservedOffset, reserved) || reserved != 0
            || member >= itemCount) {
            return {};
        }
        if (member == itemIndex) {
            if (i != 0) {
                return {};
            }
            ++matches;
            quest.value = value;
        } else if (i != 0 && matches != 0 && value == quest.value) {
            return {}; // Two steps cannot give the initial identifier an unambiguous meaning.
        }
    }
    if (matches != 1) {
        return {};
    }

    // Resolve across all four maps: a context/roster-lane match or duplicate is unsupported.
    matches = 0;
    for (const std::size_t descriptor : {kAccountValueMapDescriptor,
                                         kCharacterValueMapDescriptor,
                                         kThirdValueMapDescriptor,
                                         kFourthValueMapDescriptor}) {
        Array rows{};
        if (!find_optional_array_at(valueMap, descriptor, rows) || rows.dataOffset > valueMap.size()
            || rows.count > (valueMap.size() - rows.dataOffset) / kValueMapRowStride) {
            return {};
        }
        for (std::size_t i = 0; i < rows.count; ++i) {
            std::int16_t mappedSlot = -1;
            std::uint16_t reserved = 0;
            if (!read(valueMap,
                      rows.dataOffset + i * kValueMapRowStride + kValueMapSlotOffset,
                      mappedSlot)
                || !read(valueMap,
                         rows.dataOffset + i * kValueMapRowStride + kValueMapReservedOffset,
                         reserved)) {
                return {};
            }
            if (mappedSlot < 0 || static_cast<std::uint16_t>(mappedSlot) != slot) {
                continue;
            }
            if (reserved != 0 || ++matches != 1 || i >= kUnavailableValueMapRow
                || (descriptor != kAccountValueMapDescriptor
                    && descriptor != kCharacterValueMapDescriptor)) {
                return {};
            }
            quest.row = static_cast<std::uint16_t>(i);
            quest.scope = descriptor == kAccountValueMapDescriptor ? Quest::Scope::account
                                                                   : Quest::Scope::character;
        }
    }
    return matches == 1 && (!separateRoot || quest.scope == Quest::Scope::character)
                   && state::build_data::items::valid(quest)
               ? quest
               : Quest{};
}

} // namespace sunrise::middleware::content::packages::tables::items
