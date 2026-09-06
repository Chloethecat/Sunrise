#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/content/packages/tables/unlock_expression.h"
#include "../../../../state/build_data/runtime.h"
#include "internal.h"

namespace sunrise::client::content::items::packages {
namespace {

namespace domain = state::build_data::records;

/** Reports where the record pass stopped, so a silent miss cannot look like a working claim. */
void report(const char* stage, unsigned long long detail) noexcept {
    std::array<char, 128> line{};
    const int count = std::snprintf(
        line.data(), line.size(), "ev=pkg stage=records result=%s detail=%llu", stage, detail);
    if (count > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** A record with no completion flag carries a non-positive slot, which addresses nothing. */
[[nodiscard]] constexpr bool addressable_slot(std::int16_t slot) noexcept {
    return slot > 0;
}

/** A slot is a signed 16-bit value, so the widest addressable slot space is this. */
constexpr std::size_t kSlotSpace = 32768;

/** A record score is held as 16 bits. */
constexpr std::uint32_t kScoreCeiling = 0xFFFFU;

/** Unlock slot to bank mapping row, indexed by slot. The first mapping row of a slot wins. */
using SlotMap = std::vector<std::uint16_t>;

/**
 * Builds one slot-to-bank-index map from an unlock mapping table.
 * @param blob Blob holding the mapping table.
 * @param descriptor Array descriptor of the mapping table inside that blob.
 * @param output Receives one bank index per addressable slot, or the unavailable index.
 * @return True when the table resolves and every row fits the blob.
 */
[[nodiscard]] bool
build_slot_map(std::span<const std::byte> blob, std::size_t descriptor, SlotMap& output) noexcept {
    output.clear();
    tables::Array rows{};
    if (!tables::find_array_at(blob, descriptor, rows) || rows.count == 0
        || rows.dataOffset + static_cast<std::size_t>(rows.count) * tables::kUnlockMapRowStride
               > blob.size()) {
        return false;
    }
    output.assign(kSlotSpace, domain::kUnavailableValueIndex);
    for (std::uint64_t row = 0; row < rows.count && row <= domain::kUnavailableValueIndex; ++row) {
        std::int16_t slot = 0;
        std::memcpy(&slot,
                    blob.data() + rows.dataOffset
                        + static_cast<std::size_t>(row) * tables::kUnlockMapRowStride
                        + tables::kUnlockMapDestinationSlotOffset,
                    sizeof slot);
        if (!addressable_slot(slot)) {
            continue;
        }
        std::uint16_t& existing = output[static_cast<std::size_t>(slot)];
        if (existing == domain::kUnavailableValueIndex) {
            existing = static_cast<std::uint16_t>(row);
        }
    }
    return true;
}

/** @param map Slot map. @param slot Raw unlock slot. @return Bank index, or the unavailable one. */
[[nodiscard]] std::uint16_t bank_index(const SlotMap& map, std::int32_t slot) noexcept {
    if (map.empty() || slot <= 0 || static_cast<std::size_t>(slot) >= kSlotSpace) {
        return domain::kUnavailableValueIndex;
    }
    return map[static_cast<std::size_t>(slot)];
}

/** One located inline array of a definition row. */
struct RowArray {
    std::size_t dataOffset{};
    std::uint64_t count{};
};

/**
 * Locates one inline array a row declares, and rejects a class or a length the row cannot own.
 * @param blob Blob holding the row.
 * @param at Row offset.
 * @param field Array descriptor offset inside the row.
 * @param elementClass Element class the array header must carry.
 * @param stride One element's size.
 * @param output Receives the located array; count zero when the row declares none.
 * @return True when the array is absent or ends inside the blob with that class.
 */
[[nodiscard]] bool row_array(std::span<const std::byte> blob,
                             std::size_t at,
                             std::size_t field,
                             std::uint32_t elementClass,
                             std::size_t stride,
                             RowArray& output) noexcept {
    output = {};
    tables::Array array{};
    if (!tables::find_optional_array_at(blob, at + field, array)) {
        return false;
    }
    if (array.count == 0) {
        return true;
    }
    if (array.elementClass != elementClass
        || array.dataOffset + static_cast<std::size_t>(array.count) * stride > blob.size()) {
        return false;
    }
    output = {array.dataOffset, array.count};
    return true;
}

/** Reads the completion threshold of one objective row. */
[[nodiscard]] bool objective_threshold(std::span<const std::byte> blob,
                                       const tables::Array& objectiveRows,
                                       std::uint64_t objectiveRow,
                                       std::int32_t& threshold) noexcept {
    threshold = 0;
    if (objectiveRow >= objectiveRows.count) {
        return false;
    }
    const std::size_t at = objectiveRows.dataOffset
                           + static_cast<std::size_t>(objectiveRow) * tables::kObjectiveRowStride;
    std::memcpy(
        &threshold, blob.data() + at + tables::kObjectiveCompletionValueOffset, sizeof threshold);
    return true;
}

/** Reads the unlock value slot one objective reads its progress from, or -1 when it names none. */
[[nodiscard]] std::int16_t objective_source_slot(std::span<const std::byte> blob,
                                                 const tables::Array& objectiveRows,
                                                 std::uint64_t objectiveRow) noexcept {
    if (objectiveRow >= objectiveRows.count) {
        return -1;
    }
    const std::size_t at = objectiveRows.dataOffset
                           + static_cast<std::size_t>(objectiveRow) * tables::kObjectiveRowStride;
    std::int16_t slot = 0;
    return tables::expression_value_slot(blob, at, tables::kObjectiveSourceExpressionField, slot)
               ? slot
               : static_cast<std::int16_t>(-1);
}

} // namespace

/**
 * Reads the records table and resolves each record's flags, objectives, intervals and rewards.
 * A row names an unlock slot, not an index; the index is the mapping row that feeds that slot.
 * @param source Installed package source.
 * @param storage Pass storage receiving the record rows and all three flat banks.
 * @param root Investment root bytes.
 * @return True when the record table read and produced at least one row.
 */
bool build_records(const reader::Source& source,
                   Storage& storage,
                   std::span<const std::byte> root) noexcept {
    storage.recordCount = 0;
    storage.recordObjectiveCount = 0;
    storage.recordIntervalCount = 0;
    storage.recordRewardCount = 0;

    // The account flag mapping table, read first because the record rows are matched against it.
    std::uint32_t mapTag = 0;
    SlotMap flagIndexBySlot{};
    if (!tables::slot_tag(root, tables::kUnlockFlagMapTableSlot, mapTag) || mapTag == 0
        || tables::package_of(mapTag) == tables::kAbsentPackageId
        || !reader::read_tag(source, storage.scratch, mapTag, storage.child)
        || !build_slot_map(std::span<const std::byte>{storage.child},
                           tables::kAccountFlagMapDescriptor,
                           flagIndexBySlot)) {
        report("flag_map_fail", mapTag);
        return false;
    }

    // The account value mapping table. A record names value slots for its category, its
    // redeemed-interval count and each objective's progress source; all become bank indices here.
    std::uint32_t valueMapTag = 0;
    SlotMap valueIndexBySlot{};
    if (!tables::slot_tag(root, tables::kUnlockValueMapTableSlot, valueMapTag) || valueMapTag == 0
        || tables::package_of(valueMapTag) == tables::kAbsentPackageId
        || !reader::read_tag(source, storage.scratch, valueMapTag, storage.child)
        || !build_slot_map(std::span<const std::byte>{storage.child},
                           tables::kAccountValueMapDescriptor,
                           valueIndexBySlot)) {
        report("value_map_fail", valueMapTag);
        return false;
    }

    // The objective table carries every threshold and progress source a record names.
    std::uint32_t objectiveTag = 0;
    tables::Array objectiveRows{};
    if (!tables::slot_tag(root, tables::kObjectiveTableSlot, objectiveTag) || objectiveTag == 0
        || tables::package_of(objectiveTag) == tables::kAbsentPackageId
        || !reader::read_tag(source, storage.scratch, objectiveTag, storage.objectiveTable)
        || !tables::find_array_at(std::span<const std::byte>{storage.objectiveTable},
                                  tables::kTableArrayDescriptor,
                                  objectiveRows)
        || objectiveRows.elementClass != tables::kObjectiveRowClass
        || objectiveRows.dataOffset
                   + static_cast<std::size_t>(objectiveRows.count) * tables::kObjectiveRowStride
               > storage.objectiveTable.size()) {
        report("objective_table_fail", objectiveTag);
        return false;
    }
    const std::span<const std::byte> objectiveTable{storage.objectiveTable};

    // Reward rows sit on the display half, whose rows are in record order.
    std::uint32_t displayClass = 0;
    tables::Array displayRows{};
    if (!reader::read_tag(source,
                          storage.scratch,
                          tables::kRecordDisplayTableTag,
                          storage.displayTable,
                          displayClass)
        || !tables::find_array_at(std::span<const std::byte>{storage.displayTable},
                                  tables::kTableArrayDescriptor,
                                  displayRows)
        || displayRows.elementClass != tables::kRecordDisplayRowClass
        || displayRows.dataOffset
                   + static_cast<std::size_t>(displayRows.count) * tables::kRecordDisplayRowStride
               > storage.displayTable.size()) {
        report("display_table_fail", tables::kRecordDisplayTableTag);
        return false;
    }
    const std::span<const std::byte> displayTable{storage.displayTable};

    std::uint32_t tableTag = 0;
    tables::Array rows{};
    if (!tables::slot_tag(root, tables::kRecordTableSlot, tableTag) || tableTag == 0
        || tables::package_of(tableTag) == tables::kAbsentPackageId
        || !reader::read_tag(source, storage.scratch, tableTag, storage.child)
        || !tables::find_array_at(
            std::span<const std::byte>{storage.child}, tables::kTableArrayDescriptor, rows)
        || rows.count == 0 || rows.count > storage.recordRows.size()
        || rows.count > displayRows.count
        || rows.dataOffset + static_cast<std::size_t>(rows.count) * tables::kRecordRowStride
               > storage.child.size()) {
        report("record_table_fail", tableTag);
        return false;
    }
    const std::span<const std::byte> table{storage.child};

    // The client places record objective values in one run, in record row order, so the base
    // advances by every record's reserved count whether or not the record names objective rows.
    std::uint32_t valueIndex = domain::kObjectiveValueIndexBase;
    for (std::uint64_t row = 0; row < rows.count; ++row) {
        const std::size_t at =
            rows.dataOffset + static_cast<std::size_t>(row) * tables::kRecordRowStride;
        const std::size_t displayAt =
            displayRows.dataOffset
            + static_cast<std::size_t>(row) * tables::kRecordDisplayRowStride;
        domain::Definition& definition = storage.recordRows[static_cast<std::size_t>(row)];
        definition = {};
        definition.definitionIndex = static_cast<std::uint16_t>(row);
        definition.objectiveValueIndex = static_cast<std::uint16_t>(valueIndex);
        definition.objectiveOffset = static_cast<std::uint16_t>(storage.recordObjectiveCount);
        definition.intervalOffset = static_cast<std::uint16_t>(storage.recordIntervalCount);
        definition.rewardOffset = static_cast<std::uint16_t>(storage.recordRewardCount);
        std::memcpy(&definition.definitionHash,
                    table.data() + at + tables::kRecordHashOffset,
                    sizeof definition.definitionHash);
        // The lore row this record displays, or 0xFFFF for a book's parent triumph.
        std::memcpy(&definition.loreRow,
                    table.data() + at + tables::kLoreRowOffset,
                    sizeof definition.loreRow);
        std::uint32_t score = 0;
        std::memcpy(&score, table.data() + at + tables::kRecordScoreOffset, sizeof score);
        // The score is held as 16 bits, so a wider field is not a score and is dropped.
        definition.scoreValue = score <= kScoreCeiling ? static_cast<std::uint16_t>(score) : 0U;
        std::uint32_t hasTitle = 0;
        std::memcpy(&hasTitle, table.data() + at + tables::kRecordHasTitleOffset, sizeof hasTitle);
        definition.hasTitle = hasTitle != 0;
        std::int16_t flagSlot = 0;
        std::memcpy(
            &flagSlot, table.data() + at + tables::kRecordCompletionFlagOffset, sizeof flagSlot);
        definition.completionFlagIndex = bank_index(flagIndexBySlot, flagSlot);
        std::int16_t categorySlot = 0;
        if (tables::expression_value_slot(
                table, at, tables::kRecordCategoryExpressionField, categorySlot)) {
            definition.categoryValueIndex = bank_index(valueIndexBySlot, categorySlot);
        }
        std::uint16_t redeemedSlot = 0;
        std::memcpy(&redeemedSlot,
                    table.data() + at + tables::kRecordRedeemedCountSlotOffset,
                    sizeof redeemedSlot);
        definition.redeemedCountValueIndex = bank_index(valueIndexBySlot, redeemedSlot);

        RowArray objectives{};
        RowArray intervals{};
        RowArray rewards{};
        if (!row_array(table,
                       at,
                       tables::kRecordObjectiveField,
                       tables::kRecordObjectiveRowClass,
                       tables::kRecordObjectiveStride,
                       objectives)
            || !row_array(table,
                          at,
                          tables::kRecordIntervalField,
                          tables::kRecordIntervalRowClass,
                          tables::kRecordIntervalStride,
                          intervals)
            || !row_array(displayTable,
                          displayAt,
                          tables::kRecordRewardField,
                          tables::kRecordRewardRowClass,
                          tables::kRecordRewardStride,
                          rewards)
            || objectives.count > (std::numeric_limits<std::uint8_t>::max)()
            || intervals.count > domain::kIntervalPerRecordCapacity
            || rewards.count > domain::kRewardPerRecordCapacity
            || objectives.count > domain::kObjectiveCapacity - storage.recordObjectiveCount
            || intervals.count > domain::kIntervalCapacity - storage.recordIntervalCount
            || rewards.count > domain::kRewardCapacity - storage.recordRewardCount) {
            report("record_row_fail", row);
            return false;
        }

        for (std::uint64_t entry = 0; entry < objectives.count; ++entry) {
            std::uint16_t objectiveRow = 0;
            std::memcpy(&objectiveRow,
                        table.data() + objectives.dataOffset
                            + static_cast<std::size_t>(entry) * tables::kRecordObjectiveStride,
                        sizeof objectiveRow);
            domain::Objective& objective = storage.recordObjectives[storage.recordObjectiveCount];
            objective = {};
            if (!objective_threshold(
                    objectiveTable, objectiveRows, objectiveRow, objective.completionValue)) {
                report("objective_row_fail", objectiveRow);
                return false;
            }
            objective.valueIndex = static_cast<std::uint16_t>(valueIndex + entry);
            objective.sourceValueSlot =
                objective_source_slot(objectiveTable, objectiveRows, objectiveRow);
            objective.sourceValueIndex = bank_index(valueIndexBySlot, objective.sourceValueSlot);
            ++storage.recordObjectiveCount;
        }

        for (std::uint64_t entry = 0; entry < intervals.count; ++entry) {
            const std::size_t stepAt =
                intervals.dataOffset
                + static_cast<std::size_t>(entry) * tables::kRecordIntervalStride;
            std::uint32_t objectiveRow = 0;
            std::memcpy(&objectiveRow,
                        table.data() + stepAt + tables::kRecordIntervalObjectiveRowOffset,
                        sizeof objectiveRow);
            domain::Interval& interval = storage.recordIntervals[storage.recordIntervalCount];
            interval = {};
            if (!objective_threshold(
                    objectiveTable, objectiveRows, objectiveRow, interval.completionValue)) {
                report("interval_row_fail", objectiveRow);
                return false;
            }
            std::memcpy(&interval.score,
                        table.data() + stepAt + tables::kRecordIntervalScoreOffset,
                        sizeof interval.score);
            std::memcpy(&interval.itemIndex,
                        table.data() + stepAt + tables::kRecordIntervalItemIndexOffset,
                        sizeof interval.itemIndex);
            ++storage.recordIntervalCount;
        }

        for (std::uint64_t entry = 0; entry < rewards.count; ++entry) {
            const std::size_t rewardAt =
                rewards.dataOffset + static_cast<std::size_t>(entry) * tables::kRecordRewardStride;
            std::uint32_t quantity = 0;
            std::memcpy(&quantity,
                        displayTable.data() + rewardAt + tables::kRecordRewardQuantityOffset,
                        sizeof quantity);
            std::uint32_t itemIndex = 0;
            std::memcpy(&itemIndex,
                        displayTable.data() + rewardAt + tables::kRecordRewardItemIndexOffset,
                        sizeof itemIndex);
            // A row the grant path cannot honour is dropped, not published as a zero grant.
            if (quantity == 0
                || quantity > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
                || itemIndex > domain::kUnavailableItemIndex) {
                continue;
            }
            storage.recordRewards[storage.recordRewardCount] = {
                static_cast<std::int32_t>(quantity), static_cast<std::uint16_t>(itemIndex)};
            ++storage.recordRewardCount;
            ++definition.rewardCount;
        }

        definition.objectiveCount = static_cast<std::uint8_t>(objectives.count);
        definition.intervalCount = static_cast<std::uint8_t>(intervals.count);
        valueIndex += definition.objectiveCount != 0 ? definition.objectiveCount
                                                     : domain::kReservedObjectiveValueCount;
        ++storage.recordCount;
    }
    report("ok", static_cast<unsigned long long>(storage.recordCount));
    return storage.recordCount != 0;
}

} // namespace sunrise::client::content::items::packages
