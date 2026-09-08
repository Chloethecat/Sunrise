#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <vector>

#include "middleware/content/packages/tables/definition_index_table.h"
#include "middleware/content/packages/tables/quest_initialization_reader.h"
#include "middleware/datagen/definitions.h"
#include "middleware/datagen/family4/account/account_encoder.h"
#include "middleware/datagen/family4/account/layout.h"
#include "middleware/datagen/family4/character/character_encoder.h"
#include "middleware/datagen/family4/character/layout.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "server/bap/encrypted/queuez/queuez_state_validation.h"
#include "state/build_data/cache/records/codec.h"
#include "state/build_data/inventory/buckets/inventory_bucket_catalog.h"
#include "state/build_data/items/details/item_detail_catalog.h"
#include "state/build_data/items/item_catalog.h"
#include "state/build_data/progressions/progression_catalog.h"
#include "state/build_data/runtime/domain_markers.h"
#include "state/build_data/socket_entry_lists/socket_entry_list_catalog.h"
#include "state/investment/store_internal.h"
#include "state/runtime/runtime.h"

namespace fs = std::filesystem;
namespace s = sunrise::state;
namespace items = s::build_data::items;
namespace tables = sunrise::middleware::content::packages::tables;
namespace store = s::investment::store;
using Quest = items::QuestInitialization;
using Bytes = std::vector<std::byte>;

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
Bytes read_bytes(const fs::path& path) {
    const auto text = read_text(path);
    Bytes bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

// Source-level wiring guard, not an end-to-end world-reward delivery test.
// Keep the real caller covered without requiring the game's compression DLL.
void world_reward_caller_check(const fs::path& repository) {
    auto source =
        read_text(repository / "Sunrise/src/server/bap/encrypted/queuez/queuez_deferred_push.cpp");
    std::erase_if(source, [](unsigned char c) { return std::isspace(c) != 0; });
    const auto function = source.find("boolconsume_world_item_acquisition(");
    assert(function != std::string::npos);
    const auto call = source.find("queuez::stage_item_acquisition(", function);
    const auto end = source.find("acquisition))", call);
    assert(call != std::string::npos && end != std::string::npos);
    auto arguments = source.substr(call, end + std::strlen("acquisition))") - call);
    constexpr auto expected = "queuez::stage_item_acquisition(session.queuez,pending.accountSoid,"
                              "pending.characterSoid,pending.acquiredInstanceSoid,"
                              "pending.updates_account(),acquisition))";
    assert(arguments == expected);
    // Negative control: restoring the old argument must fail this wiring check.
    const auto flag = arguments.find("pending.updates_account()");
    arguments.replace(flag, std::strlen("pending.updates_account()"), "pending.profileChanged");
    assert(arguments != expected);
    std::cout << "PASS world-reward caller source guard (old argument rejected)\n";
}

void check_acquisition_staging(const s::PendingItemAcquisition& mutation, bool updatesAccount) {
    namespace queuez = sunrise::server::bap::encrypted::queuez;
    namespace datagen = sunrise::middleware::datagen;
    queuez::SessionState before{};
    before.family4Active = true;
    before.family4RootSoid = mutation.accountSoid;
    before.family4ResidentCount = 2;
    before.family4Residents[0] = {mutation.accountSoid, datagen::kAccountObjectId};
    before.family4Residents[1] = {mutation.characterSoid, datagen::kCharacterObjectId};
    queuez::ItemAcquisition staged{};
    assert(queuez::stage_item_acquisition(before,
                                          mutation.accountSoid,
                                          mutation.characterSoid,
                                          mutation.acquiredInstanceSoid,
                                          mutation.updates_account(),
                                          staged));
    assert(staged.updatesAccount == updatesAccount);
    assert(staged.accountDefinitionId == datagen::kAccountObjectId);
    assert(staged.characterDefinitionId == datagen::kCharacterObjectId);
    assert(staged.itemInstanceDefinitionId == datagen::kItemInstanceObjectId);
    assert(staged.after.family4Version == before.family4Version + 1);
    assert(staged.after.family4ResidentCount == before.family4ResidentCount + 1);
    assert(staged.after.family4Residents[2].objectSoid == mutation.acquiredInstanceSoid);
    assert(before.family4Version == 0 && before.family4ResidentCount == 2);
}
template <class T> void put(Bytes& bytes, std::size_t at, T value) {
    assert(at <= bytes.size() && sizeof value <= bytes.size() - at);
    std::memcpy(bytes.data() + at, &value, sizeof value);
}
void block(Bytes& bytes, std::size_t field, std::size_t at, std::uint32_t cls) {
    put(bytes, field, static_cast<std::int64_t>(at) - static_cast<std::int64_t>(field));
    put(bytes, at - 4, cls);
}
void array(
    Bytes& bytes, std::size_t field, std::size_t at, std::uint64_t count, std::uint32_t cls) {
    put(bytes, field, count);
    put(bytes, field + 8, static_cast<std::int64_t>(at) - static_cast<std::int64_t>(field + 8));
    put(bytes, at - 4, std::uint32_t{0x80800000});
    put(bytes, at, count);
    put(bytes, at + 8, cls);
}

Quest parser_checks() {
    Bytes item(800), map(256);
    put(item, 184, std::uint8_t{40});
    block(item, 0x30, 260, 0x808077EB);
    put(item, 288, std::uint16_t{0});
    array(item, 260, 500, 1, 0x808087B1);
    block(item, 0x60, 320, 0x808077C8);
    put(item, 336, std::uint16_t{7});
    put(item, 348, std::uint8_t{1});
    array(item, 320, 550, 2, 0x808077CA);
    put(item, 566, std::int32_t{-123});
    put(item, 570, std::uint16_t{0});
    put(item, 574, std::int32_t{42});
    put(item, 578, std::uint16_t{1});
    block(item, 0x90, 380, 0x808077AB);
    array(item, 380, 600, 1, 0x80807D4B);
    put(item, 616, std::uint16_t{11});
    array(map, 8, 100, 1, 0x80800001);
    put(map, 120, std::int16_t{7});
    const Quest expected{-123, 0, Quest::Scope::account};
    const auto parse = [&](const Bytes& a, const Bytes& m) {
        return tables::items::read_quest_initialization(a, 0, a, 2, m);
    };
    assert(parse(item, map) == expected);
    assert(items::initialized_value(expected, 0) == -123);
    for (auto before : {100, 200, -1, -1583618456}) {
        assert(items::initialized_value(expected, before) == before);
    }
    assert(!items::valid(Quest{100, 6200, Quest::Scope::account}));
    assert(!items::valid(Quest{100, 768, Quest::Scope::character}));
    auto bad = item;
    put(bad, 0x60, (std::numeric_limits<std::int64_t>::max)());
    assert(parse(bad, map) == Quest{});
    bad = item;
    put(bad, 578, std::uint16_t{0});
    assert(parse(bad, map) == Quest{}); // Duplicate membership.
    bad = item;
    put(bad, 574, std::int32_t{-123});
    assert(parse(bad, map) == Quest{}); // Ambiguous initial identifier.
    auto separate = item;
    put(separate, 288, std::uint16_t{1});
    put(separate, 0x60, std::int64_t{0});
    assert(tables::items::read_quest_initialization(separate, 0, item, 2, map) == expected);
    bad = item;
    put(bad, 348, std::uint8_t{0});
    assert(parse(bad, map) == Quest{});
    bad = item;
    put(bad, 0x90, std::int64_t{0});
    assert(parse(bad, map) == Quest{});
    bad = item;
    bad.resize(240);
    assert(parse(bad, map) == Quest{});
    for (auto value : {0, -1}) {
        bad = item;
        put(bad, 566, value);
        assert(parse(bad, map) == Quest{});
    }
    assert(tables::items::read_quest_initialization(item, 1, item, 2, map) == Quest{});
    auto duplicateMap = map;
    array(duplicateMap, 24, 160, 1, 0x80800001);
    put(duplicateMap, 180, std::int16_t{7});
    assert(parse(item, duplicateMap) == Quest{});
    auto characterMap = map;
    put(characterMap, 8, std::uint64_t{0});
    put(characterMap, 16, std::int64_t{0});
    array(characterMap, 24, 160, 1, 0x80800001);
    put(characterMap, 180, std::int16_t{7});
    assert((parse(item, characterMap) == Quest{-123, 0, Quest::Scope::character}));
    auto contextMap = map;
    put(contextMap, 8, std::uint64_t{0});
    put(contextMap, 16, std::int64_t{0});
    array(contextMap, 40, 160, 1, 0x80800001);
    put(contextMap, 180, std::int16_t{7});
    assert(parse(item, contextMap) == Quest{});
    auto root = item;
    put(root, 184, std::uint8_t{37});
    put(root, 0x30, std::int64_t{0});
    put(root, 566, std::int32_t{-1583618456});
    auto noFlags = separate;
    put(noFlags, 380, std::uint64_t{0});
    put(noFlags, 388, std::int64_t{0});
    Bytes rootMap(3800);
    array(rootMap, 24, 100, 443, 0x80800001);
    put(rootMap, 116 + 442 * 8 + 4, std::int16_t{7});
    const auto rootParse = [&](const Bytes& a, const Bytes& p) {
        return tables::items::read_quest_initialization(a, 0, p, 2, rootMap);
    };
    const Quest rootQuest{-1583618456, 442, Quest::Scope::character};
    assert(rootParse(noFlags, root) == rootQuest);
    auto absent = noFlags;
    put(absent, 0x90, std::int64_t{0});
    assert(rootParse(absent, root) == rootQuest);
    auto malformed = noFlags;
    put(malformed, 0x90, (std::numeric_limits<std::int64_t>::max)());
    assert(rootParse(malformed, root) == Quest{});
    malformed = noFlags;
    put(malformed, 380, std::uint64_t{1}); // Nonempty flags need a valid array.
    assert(rootParse(malformed, root) == Quest{});
    auto wrongRoot = root;
    put(wrongRoot, 184, std::uint8_t{40});
    assert(rootParse(noFlags, wrongRoot) == Quest{});
    wrongRoot = root;
    put(wrongRoot, 0x30, std::int64_t{212});
    assert(rootParse(noFlags, wrongRoot) == Quest{});
    wrongRoot = root;
    put(wrongRoot, 570, std::uint16_t{1});
    put(wrongRoot, 578, std::uint16_t{0}); // Later members stay unsupported.
    assert(rootParse(noFlags, wrongRoot) == Quest{});
    assert(tables::items::read_quest_initialization(noFlags, 0, root, 2, map) == Quest{});
    std::cout << "PASS synthetic parser, signed values, bounds, ambiguity and preservation\n";
    std::cout << "PASS separate-root first step: empty/absent flags, malformed inputs, "
                 "wrong root/scope and later-step rejection\n";
    return rootQuest;
}

void content_checks(const fs::path& root) {
    struct Row {
        std::uint32_t hash{}, tag{};
        unsigned bucket{};
        bool objective{}, set{};
    };
    std::vector<Row> rows;
    std::istringstream input(read_text(root / "items.tsv"));
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::size_t index = 0;
        Row row;
        fields >> index >> row.hash >> row.tag >> row.bucket >> row.objective >> row.set;
        assert(fields && index == rows.size());
        rows.push_back(row);
    }
    auto blob = [&](std::size_t index) {
        std::ostringstream filename;
        filename << std::uppercase << std::hex << rows.at(index).tag << ".bin";
        return read_bytes(root / filename.str());
    };
    const auto map = read_bytes(root / "81319320.bin");
    std::vector<Quest> quests(rows.size());
    std::size_t account = 0, character = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].bucket != 40 || !rows[i].objective) {
            continue;
        }
        const auto item = blob(i);
        const auto parent = tables::items::quest_parent(item);
        if (parent >= rows.size()) {
            continue;
        }
        quests[i] = tables::items::read_quest_initialization(
            item, static_cast<std::uint16_t>(i), blob(parent), rows.size(), map);
        account += quests[i].scope == Quest::Scope::account;
        character += quests[i].scope == Quest::Scope::character;
    }
    assert((quests.at(15282) == Quest{100, 5762, Quest::Scope::account}));
    assert((quests.at(14844) == Quest{100, 442, Quest::Scope::character}));
    assert((quests.at(13138) == Quest{-1583618456, 162, Quest::Scope::character}));
    for (auto index : {15U, 13139U, 13140U, 13141U, 13142U, 13143U, 14845U, 15283U}) {
        assert(quests.at(index) == Quest{});
    }
    assert(account == 50 && character >= 133);
    std::cout << "PASS installed content: " << account << " account + " << character
              << " character first steps (structural coverage, not gameplay certification)\n";
}

void cache_checks() {
    namespace cache = s::build_data::cache::records;
    items::Definition item{};
    item.definitionHash = 123;
    item.bucketId = 40;
    item.questInitialization = {-123, 7, Quest::Scope::character};
    cache::ItemRecord record{};
    items::Definition decoded{};
    assert(cache::encode(item, record) && cache::decode(record, decoded));
    assert(decoded.questInitialization == item.questInitialization);
    record.questValueScope = 255;
    assert(!cache::decode(record, decoded));
    std::cout << "PASS cache round trip and invalid scope rejection\n";
}

void runtime_checks(const fs::path& repo, const Quest& characterQuest) {
    const auto resources = repo / "Sunrise/resources/database";
    assert(store::open(":memory:",
                       read_text(resources / "investment_schema.sql"),
                       read_text(resources / "investment_defaults.sql"),
                       read_text(resources / "account_settings_schema.sql"),
                       read_text(resources / "account_settings_defaults.sql")));
    auto baseline = store::account();
    baseline.profileItems = {};
    baseline.profileItemCount = 0;
    for (std::size_t i = 0; i < baseline.characterCount; ++i) {
        auto& character = baseline.characters[i];
        character.equipment = {};
        character.inventory = {};
        character.stacks = {};
        character.selected = i == 0;
    }
    assert(baseline.characterCount >= 2);
    assert(store::write_account(baseline));
    std::array<items::Definition, 7> definitions{};
    std::array<items::details::Definition, 7> details{};
    for (std::uint16_t i = 0; i < definitions.size(); ++i) {
        definitions[i].definitionIndex = i;
        definitions[i].definitionHash = 1000U + i;
        definitions[i].bucketId = 40;
        details[i].definitionIndex = i;
        details[i].definitionHash = 1000U + i;
        details[i].bucketId = 40;
        details[i].maxStackSize = 1;
        details[i].instancedDefinitionState = items::details::InstancedDefinitionState::instanced;
    }
    definitions[0].questInitialization = {100, 5762, Quest::Scope::account};
    definitions[1].questInitialization = characterQuest;
    // The existing character encoder independently requires these four legacy prerequisites.
    constexpr std::array<std::uint32_t, 4> legacyQuests{
        0x57C4540AU, 0x85CC476EU, 0xB099029AU, 0xC3535D63U};
    for (std::size_t i = 0; i < legacyQuests.size(); ++i) {
        definitions[i + 3].definitionHash = details[i + 3].definitionHash = legacyQuests[i];
        details[i + 3].instancedDefinitionState =
            items::details::InstancedDefinitionState::stackable;
    }
    assert(items::replace(definitions));
    assert(items::details::replace(details));
    s::build_data::runtime::details::publish();
    namespace buckets = s::build_data::inventory::buckets;
    const std::array<buckets::Descriptor, 2> bucketRows{
        {{0, buckets::ArraySelector::character, 0, 1, 0},
         {40, buckets::ArraySelector::character, 1, 349}}};
    assert(buckets::replace(bucketRows));
    const s::build_data::socket_entry_lists::Definition sockets{1, 0, 0, 0};
    assert(s::build_data::socket_entry_lists::replace({&sockets, 1}));
    const s::build_data::progressions::Definition progression{};
    assert(s::build_data::progressions::replace({&progression, 1}, {}));

    const auto value = [](store::Bank bank, std::uint16_t row) {
        std::int32_t result = 0;
        assert(store::read_unlock(bank, row, result));
        return result;
    };
    const auto reset = [&] {
        assert(store::write_account(baseline));
        assert(store::execute("DELETE FROM unlocks"));
    };
    s::PendingItemAcquisition mutation{};
    s::AccountState after{};
    s::unlocks::Table unlocks{};
    const auto check_encoded = [&](bool accountScope, std::uint16_t row, std::int32_t expected) {
        namespace family4 = sunrise::middleware::datagen::family4;
        std::int32_t sent = 0;
        if (accountScope) {
            Bytes encoded(family4::account::layout::kObjectSize);
            assert(family4::account::encode(after, encoded, unlocks));
            std::memcpy(&sent,
                        encoded.data() + offsetof(family4::account::layout::Object, objectiveValues)
                            + row * sizeof sent,
                        sizeof sent);
        } else {
            Bytes encoded(family4::character::layout::kObjectSize);
            family4::loadout::ResolvedLoadout resolved{};
            assert(family4::loadout::resolve(after, 0, resolved));
            s::equipment::light::Evaluation light{};
            light.divisor = 1;
            assert(
                family4::character::encode(after.characters[0], resolved, light, encoded, unlocks));
            std::memcpy(&sent,
                        encoded.data()
                            + offsetof(family4::character::layout::Object, objectiveValues)
                            + row * sizeof sent,
                        sizeof sent);
        }
        assert(sent == expected);
    };
    reset();
    assert(s::prepare_item_acquisition_for_item(0, mutation));
    assert(mutation.updates_account());
    assert(!mutation.profileChanged); // The old world-reward flag misses this account write.
    check_acquisition_staging(mutation, true);
    assert(s::preview_item_acquisition(mutation, after, unlocks));
    assert(unlocks.objectiveValues[5762] == 100);
    check_encoded(true, 5762, 100);
    assert(value(store::Bank::objectiveValues, 5762) == 0);
    assert(store::account().characters[0].inventory.count == 0);
    assert(s::commit_item_acquisition(mutation) && !mutation.prepared);
    assert(value(store::Bank::objectiveValues, 5762) == 100);
    assert(store::account().characters[0].inventory.count == 1);

    for (auto previous : {100, 200, -1, -1583618456}) {
        reset();
        assert(store::write_unlock(store::Bank::objectiveValues, 5762, previous));
        assert(store::write_unlock(store::Bank::objectiveValues, 5763, 4));
        assert(s::prepare_item_acquisition_for_item(0, mutation));
        assert(!mutation.updates_account());
        check_acquisition_staging(mutation, false);
        assert(s::commit_item_acquisition(mutation));
        assert(value(store::Bank::objectiveValues, 5762) == previous);
        assert(value(store::Bank::objectiveValues, 5763) == 4);
    }
    reset();
    auto otherCharacter = baseline;
    otherCharacter.characters[0].selected = false;
    otherCharacter.characters[1].selected = true;
    assert(store::write_account(otherCharacter));
    assert(store::write_unlock(store::Bank::objectiveValues, 5762, 200));
    assert(s::prepare_item_acquisition(
        s::build_data::collectibles::kNoCollectibleIndex, definitions[0].definitionHash, mutation));
    assert(s::commit_item_acquisition(mutation));
    assert(value(store::Bank::objectiveValues, 5762) == 200);
    assert(store::account().characters[1].inventory.count == 1);
    reset();
    assert(s::prepare_item_acquisition_for_item(1, mutation));
    assert(!mutation.updates_account());
    check_acquisition_staging(mutation, false);
    assert(s::preview_item_acquisition(mutation, after, unlocks));
    assert(unlocks.characterObjectValues[442] == characterQuest.value);
    check_encoded(false, 442, characterQuest.value);
    assert(value(store::Bank::characterObjectValues, 442) == 0);
    assert(s::commit_item_acquisition(mutation));
    assert(value(store::Bank::characterObjectValues, 442) == characterQuest.value);
    assert(store::read_unlocks(unlocks, 1));
    assert(unlocks.characterObjectValues[442] == 0);
    for (auto previous : {characterQuest.value, 790208398, -1}) {
        reset();
        assert(store::write_unlock(store::Bank::characterObjectValues, 442, previous));
        assert(s::prepare_item_acquisition_for_item(1, mutation));
        assert(s::preview_item_acquisition(mutation, after, unlocks));
        check_encoded(false, 442, previous);
        assert(s::commit_item_acquisition(mutation));
        assert(value(store::Bank::characterObjectValues, 442) == previous);
    }

    reset();
    assert(s::prepare_item_acquisition_for_item(0, mutation));
    assert(store::write_unlock(store::Bank::objectiveValues, 5762, 200));
    assert(!s::commit_item_acquisition(mutation));
    assert(store::account().characters[0].inventory.count == 0);
    assert(value(store::Bank::objectiveValues, 5762) == 200);

    reset();
    assert(s::prepare_item_acquisition_for_item(1, mutation));
    auto changed = baseline;
    changed.characters[0].selected = false;
    changed.characters[1].selected = true;
    assert(store::write_account(changed));
    assert(!s::commit_item_acquisition(mutation));
    assert(value(store::Bank::characterObjectValues, 442) == 0);
    reset();
    assert(s::prepare_item_acquisition_for_item(0, mutation));
    mutation.questInitialization.row = 5763; // A changed contract cannot redirect the write.
    assert(!s::commit_item_acquisition(mutation));
    assert(store::account().characters[0].inventory.count == 0);

    reset();
    assert(s::prepare_item_acquisition_for_item(0, mutation));
    assert(store::execute("CREATE TEMP TRIGGER fail_item BEFORE INSERT ON items "
                          "BEGIN SELECT RAISE(ABORT,'injected inventory failure'); END;"));
    assert(!s::commit_item_acquisition(mutation));
    assert(store::execute("DROP TRIGGER fail_item"));
    assert(store::account().characters[0].inventory.count == 0);
    assert(value(store::Bank::objectiveValues, 5762) == 0);

    reset();
    auto full = baseline;
    auto& inventory = full.characters[0].inventory;
    for (std::size_t i = 0; i < inventory.values.size(); ++i) {
        auto& item = inventory.values[i];
        item.instanceSoid = 0x4000000000000001ULL + i;
        item.definitionHash = definitions[2].definitionHash;
        item.quantity = 1;
    }
    inventory.count = inventory.values.size();
    assert(store::write_account(full));
    assert(!s::prepare_item_acquisition_for_item(0, mutation));
    assert(!mutation.prepared && value(store::Bank::objectiveValues, 5762) == 0);
    reset();
    assert(!s::prepare_item_acquisition_for_item(65535, mutation));
    assert(!mutation.prepared && value(store::Bank::objectiveValues, 5762) == 0);

    reset();
    assert(s::prepare_item_acquisition_for_item(0, mutation));
    assert(store::execute("CREATE TEMP TRIGGER fail_quest BEFORE INSERT ON unlocks "
                          "BEGIN SELECT RAISE(ABORT,'injected quest write failure'); END;"));
    assert(!s::commit_item_acquisition(mutation));
    assert(store::execute("DROP TRIGGER fail_quest"));
    assert(store::account().characters[0].inventory.count == 0);
    assert(value(store::Bank::objectiveValues, 5762) == 0);

    reset();
    assert(s::prepare_item_acquisition_for_item(2, mutation));
    assert(mutation.questInitialization == Quest{});
    check_acquisition_staging(mutation, false);
    auto profileMutation = mutation;
    profileMutation.profileChanged = true;
    check_acquisition_staging(profileMutation, true); // Existing profile-charge behavior.
    assert(s::commit_item_acquisition(mutation));
    assert(value(store::Bank::objectiveValues, 5762) == 0);
    assert(value(store::Bank::characterObjectValues, 442) == 0);
    store::shutdown();
    std::cout << "PASS production queuez staging: fresh account/character quests, existing "
                 "progress, ordinary item, profile charge\n";
    std::cout << "PASS production acquisition + SQLite: atomic rollback, stale value/character, "
                 "both scopes, preservation, encoded after-image, full/refused grants, unsupported "
                 "control\n";
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    assert(argc == 2 || argc == 3);
    const auto characterQuest = parser_checks();
    cache_checks();
    world_reward_caller_check(argv[1]);
    runtime_checks(argv[1], characterQuest);
    if (argc == 3) {
        content_checks(argv[2]);
    }
}
