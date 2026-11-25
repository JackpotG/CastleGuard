#include "GameData.h"
#include "doctest/doctest.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::string sourceConfig(const char* name)
{
    std::ifstream input(std::filesystem::path(__FILE__).parent_path().parent_path() / "Content/config" / name);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
std::string fixtureConfig(const char* name)
{
    std::ifstream input(std::filesystem::path(__FILE__).parent_path() / "fixtures" / name);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
const char* customCatalog = R"({"upgrades":[
 {"id":"training","name":"训练","description":"基础训练","currency":"coins","costs":[5,10],
  "track":"bow","treeRow":0,"treeX":0.5,"effects":[{"attribute":"arrow_damage","operation":"add","values":[0,2,4]}]},
 {"id":"focused_shot","name":"凝神射击","description":"复合效果","currency":"crystals","costs":[3],
  "track":"bow","treeRow":1,"treeX":0.5,"requires":"training","requiredLevel":2,"unlockStage":3,
  "effects":[{"attribute":"arrow_damage","operation":"multiply","values":[1,1.5]},
             {"attribute":"fire_rate","operation":"multiply","values":[1,0.8]}]}
]})";
}

TEST_SUITE("UpgradeCatalog")
{
    TEST_CASE("existing catalog retains its effects and legacy handles")
    {
        std::vector<UpgradeDefinition> nodes;
        std::vector<UpgradeAttribute> attributes;
        REQUIRE(parseUpgradeAttributes(sourceConfig("upgrade-attributes.json"), attributes));
        REQUIRE(parseUpgradeCatalog(sourceConfig("upgrades.json"), nodes));
        REQUIRE(nodes.size() >= 9);
        CHECK_EQ(nodes[0].key, "arrow_damage");
        CHECK_EQ(nodes[8].key, "spell_efficiency");
        ProgressionModel progress;
        CHECK_EQ(evaluateUpgradeAttribute(attributes[0], nodes, progress), doctest::Approx(1));
        progress.setUpgradeLevel(UpgradeId::ArrowDamage, 2);
        CHECK_EQ(evaluateUpgradeAttribute(attributes[0], nodes, progress), doctest::Approx(3));
    }

    TEST_CASE("arbitrary nodes gate purchases and apply all configured effects")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(customCatalog, nodes));
        REQUIRE(nodes.size() == 2);
        auto attributes = defaultUpgradeAttributes();
        ProgressionModel progress;
        EconomyModel wallet(100, 100);
        CHECK_EQ(progress.purchaseUpgrade(wallet, nodes[1], 3).status, PurchaseStatus::PrerequisiteLocked);
        REQUIRE(progress.purchaseUpgrade(wallet, nodes[0]).purchased());
        REQUIRE(progress.purchaseUpgrade(wallet, nodes[0]).purchased());
        CHECK_EQ(progress.purchaseUpgrade(wallet, nodes[1], 2).status, PurchaseStatus::PrerequisiteLocked);
        REQUIRE(progress.purchaseUpgrade(wallet, nodes[1], 3).purchased());
        CHECK_EQ(evaluateUpgradeAttribute(attributes[0], nodes, progress), doctest::Approx(7.5));
        CHECK_EQ(evaluateUpgradeAttribute(attributes[1], nodes, progress), doctest::Approx(.8));
        CHECK_EQ(wallet.totalCoins(), 85);
        CHECK_EQ(wallet.totalCrystals(), 97);
        CHECK_EQ(progress.purchaseUpgrade(wallet, nodes[1], 3).status, PurchaseStatus::MaxLevel);
    }

    TEST_CASE("browser-saved composite node is usable by the game parser and economy")
    {
        std::ifstream input(std::filesystem::path(__FILE__).parent_path() / "fixtures/editor-upgrades.json");
        std::ostringstream json;
        json << input.rdbuf();
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(json.str(), nodes));
        const auto child = std::find_if(nodes.begin(), nodes.end(), [](const auto& node) { return node.key == "focused_test_shot"; });
        REQUIRE(child != nodes.end());
        REQUIRE(child->effects.size() == 2);
        ProgressionModel progress;
        EconomyModel wallet(100, 100);
        progress.setUpgradeLevel(child->prerequisite, 2);
        CHECK_EQ(progress.purchaseUpgrade(wallet, *child, 2).status, PurchaseStatus::PrerequisiteLocked);
        REQUIRE(progress.purchaseUpgrade(wallet, *child, 3).purchased());
        const auto attributes = defaultUpgradeAttributes();
        CHECK_EQ(evaluateUpgradeAttribute(attributes[0], nodes, progress), doctest::Approx(7));
        CHECK_EQ(evaluateUpgradeAttribute(attributes[1], nodes, progress), doctest::Approx(.8));
        CHECK_EQ(wallet.totalCoins(), 90);
    }

    TEST_CASE("dynamic handles beyond the old enum limit remain purchasable")
    {
        ProgressionModel progress;
        EconomyModel wallet(20);
        UpgradeDefinition node{static_cast<UpgradeId>(20), "new_node", "New", "New", Currency::Coins, {5}, {0, 2}};
        node.effects = {{"arrow_damage", "add", {0, 2}}};
        CHECK(progress.purchaseUpgrade(wallet, node).purchased());
        CHECK_EQ(progress.upgradeLevel(node.id), 1);
        CHECK_EQ(wallet.totalCoins(), 15);
    }

    TEST_CASE("invalid dependencies and effects fail atomically")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(customCatalog, nodes));
        const auto rejected = [&](const std::string& from, const std::string& to) {
            std::string broken = customCatalog;
            broken.replace(broken.find(from), from.size(), to);
            CHECK_FALSE(parseUpgradeCatalog(broken, nodes));
            CHECK_EQ(nodes.size(), 2);
            CHECK_EQ(nodes[1].key, "focused_shot");
        };
        rejected("\"requires\":\"training\"", "\"requires\":\"missing\"");
        rejected("\"requiredLevel\":2", "\"requiredLevel\":3");
        rejected("\"attribute\":\"fire_rate\"", "\"attribute\":\"not_implemented\"");
        rejected("[1,0.8]", "[1,0]");
        rejected("[0,2,4]", "[1,2,4]");
        rejected("\"operation\":\"add\"", "\"operation\":\"typo\"");
    }

    TEST_CASE("browser-saved multiple connections work for both upgrades and spells")
    {
        std::ifstream input(std::filesystem::path(__FILE__).parent_path() / "fixtures/editor-multi-prerequisites.json");
        std::ostringstream json;
        json << input.rdbuf();
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(json.str(), nodes));
        const auto& child = nodes.back();
        CHECK_EQ(child.key, "combined_test_shot");
        REQUIRE(child.prerequisites.size() == 2);
        ProgressionModel progress;
        EconomyModel wallet(100, 100);
        progress.setUpgradeLevel(child.prerequisites[0].id, child.prerequisites[0].level);
        CHECK_EQ(progress.purchaseUpgrade(wallet, child).status, PurchaseStatus::PrerequisiteLocked);
        CHECK_EQ(wallet.totalCoins(), 100);
        progress.setUpgradeLevel(child.prerequisites[1].id, child.prerequisites[1].level);
        REQUIRE(progress.purchaseUpgrade(wallet, child).purchased());
        CHECK_EQ(wallet.totalCoins(), 90);

        std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)> skills;
        REQUIRE(parseSkillCatalog(json.str(), skills));
        const auto& lightning = skills[static_cast<size_t>(SkillId::Lightning)];
        REQUIRE(lightning.prerequisites.size() == 2);
        progress.setSkillUnlocked(SkillId::Fireball, true);
        CHECK_EQ(progress.unlockSkill(wallet, lightning).status, PurchaseStatus::PrerequisiteLocked);
        CHECK_EQ(wallet.totalCrystals(), 100);
        progress.setSkillUnlocked(SkillId::Freeze, true);
        REQUIRE(progress.unlockSkill(wallet, lightning).purchased());
        CHECK_EQ(wallet.totalCrystals(), 95);
        // Failed parsing is atomic, including malformed and duplicate spell requirements.
        for (const auto& replacement : std::vector<std::string>{
            R"("prerequisites": [{"id":"freeze","level":2}])",
            R"("prerequisites": [{"id":"freeze","level":1},{"id":"freeze","level":1}])"})
        {
            auto broken = json.str();
            const auto node = broken.find("\"id\": \"lightning\"");
            const auto start = broken.find("\"prerequisites\"", node);
            const auto end = broken.find(']', start);
            broken.replace(start, end - start + 1, replacement);
            CHECK_FALSE(parseSkillCatalog(broken, skills));
            CHECK_EQ(skills[3].prerequisites.size(), 2);
        }
    }

    TEST_CASE("multiple prerequisites all gate a purchase with independent levels")
    {
        const std::string json = R"({"upgrades":[
          {"id":"precision","name":"鹰眼","description":"暴击","currency":"coins","costs":[2,3],
           "track":"bow","treeRow":0,"treeX":0.27,"effects":[{"attribute":"precision","operation":"add","values":[0,0.1,0.2]}]},
          {"id":"heavy_strike","name":"强力一击","description":"击退","currency":"coins","costs":[2],
           "track":"bow","treeRow":0,"treeX":0.73,"effects":[{"attribute":"heavy_strike","operation":"add","values":[0,10]}]},
          {"id":"combined_shot","name":"协同射击","description":"复合前置","currency":"coins","costs":[5],
           "track":"bow","treeRow":1,"treeX":0.5,"unlockStage":3,
           "prerequisites":[{"id":"precision","level":2},{"id":"heavy_strike","level":1}],
           "effects":[{"attribute":"arrow_damage","operation":"add","values":[0,2]}]}
        ]})";
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(json, nodes));
        const auto& child = nodes.back();
        REQUIRE(child.prerequisites.size() == 2);
        // Built-in handles are reordered during parsing; both references must follow their stable keys.
        CHECK_EQ(nodes[static_cast<size_t>(child.prerequisites[0].id)].key, "precision");
        CHECK_EQ(nodes[static_cast<size_t>(child.prerequisites[1].id)].key, "heavy_strike");
        for (const auto& levels : std::vector<std::pair<int, int>>{{0,0}, {2,0}, {0,1}, {1,1}})
        {
            ProgressionModel progress;
            EconomyModel wallet(20);
            progress.setUpgradeLevel(child.prerequisites[0].id, levels.first);
            progress.setUpgradeLevel(child.prerequisites[1].id, levels.second);
            CHECK_EQ(progress.purchaseUpgrade(wallet, child, 3).status, PurchaseStatus::PrerequisiteLocked);
            CHECK_EQ(wallet.totalCoins(), 20);
            CHECK_EQ(progress.upgradeLevel(child.id), 0);
        }
        ProgressionModel progress;
        EconomyModel wallet(20);
        for (const auto& requirement : child.prerequisites) progress.setUpgradeLevel(requirement.id, requirement.level);
        CHECK_EQ(progress.purchaseUpgrade(wallet, child, 2).status, PurchaseStatus::PrerequisiteLocked);
        REQUIRE(progress.purchaseUpgrade(wallet, child, 3).purchased());
        CHECK_EQ(wallet.totalCoins(), 15);
        CHECK_EQ(evaluateUpgradeAttribute(defaultUpgradeAttributes()[0], nodes, progress), doctest::Approx(3));
        for (const auto& replacement : std::vector<std::string>{
                 R"({"id":"heavy_strike","level":2})", R"({"id":"precision","level":1})",
                 R"({"id":"missing","level":1})", R"({"id":"combined_shot","level":1})",
                 R"({"id":"heavy_strike","level":0})", R"({"id":"heavy_strike","level":1.5})"})
        {
            std::string broken = json;
            const std::string original = R"({"id":"heavy_strike","level":1})";
            broken.replace(broken.find(original), original.size(), replacement);
            CHECK_FALSE(parseUpgradeCatalog(broken, nodes));
            CHECK_EQ(nodes.back().key, "combined_shot");
        }
    }

    TEST_CASE("owned multi-shot compatibility never skips multiple conditions")
    {
        UpgradeDefinition node{UpgradeId::MultiShot, "multi_shot", "Multi", "Multi", Currency::Coins, {5, 10}, {1, 2, 3}};
        node.prerequisite = UpgradeId::FireRate; node.requiredLevel = 2;
        ProgressionModel progress;
        EconomyModel wallet(20);
        progress.setUpgradeLevel(node.id, 1);
        CHECK(progress.meetsUpgradePrerequisite(node));
        node.prerequisites = {{UpgradeId::FireRate, 2}};
        CHECK(progress.meetsUpgradePrerequisite(node));
        node.prerequisites.push_back({UpgradeId::Precision, 1});
        CHECK_EQ(progress.purchaseUpgrade(wallet, node).status, PurchaseStatus::PrerequisiteLocked);
        CHECK_EQ(wallet.totalCoins(), 20);
        progress.setUpgradeLevel(UpgradeId::FireRate, 2);
        CHECK_FALSE(progress.meetsUpgradePrerequisite(node));
        progress.setUpgradeLevel(UpgradeId::Precision, 1);
        CHECK(progress.purchaseUpgrade(wallet, node).purchased());
    }

    TEST_CASE("a dependency cycle on a second defense edge is rejected")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(customCatalog, nodes));
        for (auto& node : nodes) { node.track = UpgradeTrack::Defense; node.treeRow = -1; node.prerequisite = UpgradeId::None; node.requiredLevel = 0; }
        auto extra = nodes[0]; extra.id = static_cast<UpgradeId>(2); extra.key = "third";
        nodes.push_back(extra);
        nodes[0].prerequisites = {{nodes[1].id, 1}, {nodes[2].id, 1}};
        nodes[2].prerequisites = {{nodes[0].id, 1}};
        CHECK_FALSE(migrateLegacyUpgradeLayout(nodes));
    }

    TEST_CASE("browser-saved facilities share layout, AND gates, levels and effects")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(fixtureConfig("editor-shared-trees.json"), nodes));
        REQUIRE(nodes.size() == 18);
        for (const std::string track : {"bow", "magic", "defense"})
        {
            const auto find = [&](const std::string& suffix) -> const UpgradeDefinition& {
                const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const auto& item) { return item.key == track + suffix; });
                REQUIRE(node != nodes.end());
                return *node;
            };
            const auto& first = find("_training");
            const auto& second = find("_support");
            const auto& child = find("_focused");
            CHECK(first.track == child.track);
            CHECK(first.track == second.track);
            CHECK_EQ(first.treeRow, 0);
            CHECK_EQ(child.treeRow, 1);
            CHECK_EQ(child.treeX, doctest::Approx(.373333f));
            REQUIRE(child.prerequisites.size() == 2);
            CHECK_EQ(child.prerequisites[0].level, 2);
            CHECK_EQ(child.prerequisites[1].level, 1);
            REQUIRE(child.effects.size() == 2);
            CHECK_EQ(child.costs.size(), 2);
            ProgressionModel progress;
            EconomyModel wallet(100, 100);
            CHECK_EQ(progress.purchaseUpgrade(wallet, child, 3).status, PurchaseStatus::PrerequisiteLocked);
            REQUIRE(progress.purchaseUpgrade(wallet, first, 3).purchased());
            REQUIRE(progress.purchaseUpgrade(wallet, second, 3).purchased());
            CHECK_EQ(progress.purchaseUpgrade(wallet, child, 3).status, PurchaseStatus::PrerequisiteLocked);
            REQUIRE(progress.purchaseUpgrade(wallet, first, 3).purchased());
            const int balance = wallet.totalCoins();
            CHECK_EQ(progress.purchaseUpgrade(wallet, child, 2).status, PurchaseStatus::PrerequisiteLocked);
            CHECK_EQ(wallet.totalCoins(), balance);
            REQUIRE(progress.purchaseUpgrade(wallet, child, 3).purchased());
            REQUIRE(progress.purchaseUpgrade(wallet, child, 3).purchased());
            CHECK_EQ(progress.purchaseUpgrade(wallet, child, 3).status, PurchaseStatus::MaxLevel);
            const auto attributes = defaultUpgradeAttributes();
            CHECK_EQ(evaluateUpgradeAttribute(attributes[1], nodes, progress), doctest::Approx(.8f));
            auto invalid = nodes;
            invalid[static_cast<size_t>(child.id)].treeRow = 0;
            CHECK_FALSE(validUpgradeTree(invalid));
            invalid = nodes;
            invalid[static_cast<size_t>(child.id)].treeX = .95f;
            CHECK_FALSE(validUpgradeTree(invalid));
        }
    }

    TEST_CASE("legacy defense gains stable layout without changing progression data")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(fixtureConfig("legacy-defense-tree.json"), nodes));
        REQUIRE(nodes.size() == 3);
        CHECK_EQ(nodes[0].key, "wall_health");
        CHECK_EQ(nodes[0].treeRow, 0);
        CHECK_EQ(nodes[0].treeX, doctest::Approx(.73f));
        CHECK_EQ(nodes[1].treeX, doctest::Approx(.27f));
        CHECK_EQ(nodes[2].treeRow, 1);
        CHECK_EQ(nodes[2].treeX, doctest::Approx(.5f));
        CHECK((nodes[0].costs == std::vector<int>{3, 5}));
        CHECK((nodes[0].effects[0].values == std::vector<float>{100, 120, 140}));
        REQUIRE(nodes[2].prerequisites.size() == 2);
        CHECK_EQ(nodes[2].prerequisites[0].level, 2);
        CHECK_EQ(nodes[2].prerequisites[1].level, 1);
        const auto snapshot = nodes;
        REQUIRE(migrateLegacyUpgradeLayout(nodes));
        CHECK_EQ(nodes[0].treeX, snapshot[0].treeX);
        CHECK_EQ(nodes[2].treeRow, snapshot[2].treeRow);
    }

    TEST_CASE("active spell layout uses the same sixteen-layer range")
    {
        std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)> skills;
        REQUIRE(parseSkillCatalog(fixtureConfig("editor-shared-trees.json"), skills));
        CHECK_EQ(skills[static_cast<size_t>(SkillId::Lightning)].treeRow, 9);
        skills[static_cast<size_t>(SkillId::Lightning)].treeRow = 17;
        CHECK_FALSE(validResearchTree(skills));
    }

    TEST_CASE("node deletion leaves a usable base and empty catalog is safe")
    {
        std::vector<UpgradeDefinition> nodes;
        REQUIRE(parseUpgradeCatalog(R"({"upgrades":[]})", nodes));
        ProgressionModel progress;
        auto attributes = defaultUpgradeAttributes();
        CHECK_EQ(evaluateUpgradeAttribute(attributes[0], nodes, progress), doctest::Approx(1));
        CHECK_EQ(evaluateUpgradeAttribute(attributes[3], nodes, progress), doctest::Approx(100));
    }
}
