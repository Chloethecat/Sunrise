#include "quest_initialization_reader.h"

#include <limits>

#include "definition_index_table.h"
#include "internal.h"

namespace sunrise::middleware::content::packages::tables::items {
namespace {

using Quest = state::build_data::items::QuestInitialization;

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
    if (target < 4 || static_cast<std::uint64_t>(target) > bytes.size()
        || bytes.size() - static_cast<std::size_t>(target) < 32) {
        return false;
    }
    offset = static_cast<std::size_t>(target);
    std::uint32_t actualClass = 0;
    return read(bytes, offset - 4, actualClass) && actualClass == expectedClass;
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
    std::uint16_t parent = 0xFFFFU;
    Array objectives{};
    if (definition.size() < 240 || !read(definition, kBucketIdOffset, bucket) || bucket != 40
        || !block(definition, 0x30, 0x808077EBU, objective)
        || !array(definition, objective, 0x808087B1U, 2, objectives)
        || !read(definition, objective + 0x1C, parent)) {
        return 0xFFFFU;
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
    if (itemIndex >= itemCount || parentIndex >= itemCount || parent.size() < 240
        || !block(parent, 0x60, 0x808077C8U, set) || !read(parent, set + 0x1C, mode) || mode != 1
        || !read(parent, set + 0x10, slot) || slot >= 32768
        || !array(parent, set, 0x808077CAU, 8, members) || members.count > itemCount) {
        return {};
    }
    std::int64_t unlockRelative = 0;
    if (!read(definition, 0x90, unlockRelative)
        || (unlockRelative != 0
            && (!block(definition, 0x90, 0x808077ABU, unlock)
                || !find_optional_array_at(definition, unlock, flags)
                || (flags.count != 0 && !array(definition, unlock, 0x80807D4BU, 2, flags))))) {
        return {};
    }
    for (std::size_t i = 0; i < flags.count; ++i) {
        std::uint16_t flag = 0;
        if (!read(definition, flags.dataOffset + i * 2, flag) || flag >= 32768) {
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
            || parentBucket != 37 || !read(parent, 0x30, parentObjective) || parentObjective != 0) {
            return {};
        }
    }
    Quest quest{};
    std::size_t matches = 0;
    for (std::size_t i = 0; i < members.count; ++i) {
        const auto at = members.dataOffset + i * 8;
        std::int32_t value = 0;
        std::uint16_t member = 0, reserved = 0;
        if (!read(parent, at, value) || !read(parent, at + 4, member)
            || !read(parent, at + 6, reserved) || reserved != 0 || member >= itemCount) {
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
    for (const std::size_t descriptor : {8U, 24U, 40U, 56U}) {
        Array rows{};
        if (!find_optional_array_at(valueMap, descriptor, rows) || rows.dataOffset > valueMap.size()
            || rows.count > (valueMap.size() - rows.dataOffset) / 8) {
            return {};
        }
        for (std::size_t i = 0; i < rows.count; ++i) {
            std::int16_t mappedSlot = -1;
            std::uint16_t reserved = 0;
            if (!read(valueMap, rows.dataOffset + i * 8 + 4, mappedSlot)
                || !read(valueMap, rows.dataOffset + i * 8 + 6, reserved)) {
                return {};
            }
            if (mappedSlot < 0 || static_cast<std::uint16_t>(mappedSlot) != slot) {
                continue;
            }
            if (reserved != 0 || ++matches != 1 || i >= 0xFFFFU
                || (descriptor != 8 && descriptor != 24)) {
                return {};
            }
            quest.row = static_cast<std::uint16_t>(i);
            quest.scope = descriptor == 8 ? Quest::Scope::account : Quest::Scope::character;
        }
    }
    return matches == 1 && (!separateRoot || quest.scope == Quest::Scope::character)
                   && state::build_data::items::valid(quest)
               ? quest
               : Quest{};
}

} // namespace sunrise::middleware::content::packages::tables::items
