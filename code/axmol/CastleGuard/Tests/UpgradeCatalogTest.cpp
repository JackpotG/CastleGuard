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
