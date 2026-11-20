#include "CampaignConfig.h"

#include "axmol.h"
#include "doctest/doctest.h"

#include <array>

TEST_SUITE("CampaignConfig")
{
    TEST_CASE("campaign keeps the five tuned stages and loads fifty levels")
    {
        CampaignConfig campaign;
        std::string error;
        const auto json = ax::FileUtils::getInstance()->getStringFromFile("config/waves.json");

        REQUIRE(campaign.loadFromJson(json, &error));
        REQUIRE_EQ(campaign.levels().size(), 50);

        const std::array<int, 5> expectedEnemies = {10, 19, 29, 40, 51};
        const std::array<int, 5> expectedCrystals = {1, 1, 2, 2, 4};
        for (size_t i = 0; i < campaign.levels().size(); ++i)
        {
            const auto& level = campaign.levels()[i];
            CHECK_FALSE(level.waves.empty());
            if (i < expectedEnemies.size())
            {
                CHECK_EQ(level.configuredEnemyCount(), expectedEnemies[i]);
                CHECK_EQ(level.clearCrystals, expectedCrystals[i]);
                if (i > 0)
                    CHECK_GT(level.configuredEnemyCount(), campaign.levels()[i - 1].configuredEnemyCount());
            }
            else
            {
                CHECK_GE(level.configuredEnemyCount(), 20);
                CHECK_LE(level.configuredEnemyCount(), 55);
                CHECK_EQ(level.waves.size(), (i + 1) % 5 == 0 ? 4 : 3);
                if (i > 5 && i % 5 != 0)
                    CHECK_GT(level.configuredEnemyCount(), campaign.levels()[i - 1].configuredEnemyCount());
            }
            bool hasBoss = false;
            for (const auto& wave : level.waves)
            {
                for (const auto& group : wave.groups)
                {
                    for (int spawnIndex = 0; spawnIndex < group.count; ++spawnIndex)
                        hasBoss |= group.monsterTypeAt(static_cast<size_t>(spawnIndex)) == MonsterType::Boss;
                }
            }
            CHECK_EQ(hasBoss, (i + 1) % 5 == 0);
        }
    }

    TEST_CASE("campaign stages include continuous columns and simultaneous rows")
    {
        CampaignConfig campaign;
        std::string error;
        const auto json = ax::FileUtils::getInstance()->getStringFromFile("config/waves.json");
        REQUIRE(campaign.loadFromJson(json, &error));
        CHECK_EQ(campaign.levels()[0].waves[1].groups[0].pattern, SpawnPattern::Column);
        CHECK_EQ(campaign.levels()[1].waves[1].groups[1].pattern, SpawnPattern::Row);
        CHECK_EQ(campaign.levels()[3].waves[1].groups[2].pattern, SpawnPattern::Column);

        const auto& mixedColumn = campaign.levels()[5].waves[1].groups[0];
        CHECK_EQ(mixedColumn.pattern, SpawnPattern::Column);
        CHECK_EQ(mixedColumn.count, 6);
        CHECK_EQ(mixedColumn.monsterTypeAt(0), MonsterType::Normal);
        CHECK_EQ(mixedColumn.monsterTypeAt(1), MonsterType::Fast);
        CHECK_EQ(mixedColumn.monsterTypeAt(3), MonsterType::Shield);
        CHECK_EQ(mixedColumn.monsterTypeAt(5), MonsterType::Fast);
        CHECK_EQ(mixedColumn.monsterTypeAt(7), MonsterType::Fast);

        const auto& mixedRow = campaign.levels()[6].waves[1].groups[1];
        CHECK_EQ(mixedRow.pattern, SpawnPattern::Row);
        CHECK_EQ(mixedRow.count, 7);
        CHECK_EQ(mixedRow.monsterTypeAt(0), MonsterType::Normal);
        CHECK_EQ(mixedRow.monsterTypeAt(1), MonsterType::Fast);
        CHECK_EQ(mixedRow.monsterTypeAt(3), MonsterType::Ranged);
        CHECK_EQ(mixedRow.monsterTypeAt(5), MonsterType::Shield);
        CHECK_EQ(mixedRow.monsterTypeAt(6), MonsterType::Normal);
    }

    TEST_CASE("core battle keeps the committed twelve-wave baseline")
    {
        CampaignConfig core;
        std::string error;
        const auto json = ax::FileUtils::getInstance()->getStringFromFile("config/core-waves.json");

        REQUIRE(core.loadFromJson(json, &error));
        REQUIRE_EQ(core.levels().size(), 1);
        CHECK_EQ(core.levels().front().waves.size(), 12);
        CHECK_EQ(core.levels().front().configuredEnemyCount(), 163);
        CHECK_EQ(core.levels().front().clearCrystals, 0);
    }

    TEST_CASE("formation must be known and enter through the main spawn line")
    {
        CampaignConfig campaign;
        std::string error;
        const std::string invalid = R"({"levels":[{"name":"broken","waves":[{"name":"wave","groups":[{"type":"normal","count":2,"formation":"diagonal"}]}]}]})";
        CHECK_FALSE(campaign.loadFromJson(invalid, &error));
        CHECK_FALSE(error.empty());

        error.clear();
        const std::string flank = R"({"levels":[{"name":"broken","waves":[{"name":"wave","groups":[{"type":"normal","count":2,"from":"left","formation":"row"}]}]}]})";
        CHECK_FALSE(campaign.loadFromJson(flank, &error));
        CHECK_FALSE(error.empty());

        error.clear();
        const std::string oversized = R"({"levels":[{"name":"broken","clearCrystals":1,"waves":[{"name":"wave","groups":[{"types":["normal","normal","normal","normal","normal","normal","normal","normal","normal","normal","normal","normal","normal"],"formation":"row"}]}]}]})";
        CHECK_FALSE(campaign.loadFromJson(oversized, &error));
        CHECK_EQ(error, "row formation must fit within the available lanes");

        error.clear();
        const std::string overflowingLane = R"({"levels":[{"name":"broken","clearCrystals":1,"waves":[{"name":"wave","groups":[{"types":["normal","fast"],"lane":11,"formation":"row"}]}]}]})";
        CHECK_FALSE(campaign.loadFromJson(overflowingLane, &error));
        CHECK_EQ(error, "row formation must fit within the available lanes");
    }

    TEST_CASE("invalid group data rejects the whole campaign")
    {
        CampaignConfig campaign;
        std::string error;
        const std::string json = R"({
          "levels": [{
            "name": "broken", "clearCrystals": 1,
            "waves": [{"name": "wave", "groups": [{"type": "unknown", "count": 1}]}]
          }]
        })";

        CHECK_FALSE(campaign.loadFromJson(json, &error));
        CHECK(campaign.empty());
        CHECK_FALSE(error.empty());
    }

    TEST_CASE("mixed group rejects empty, unknown and ambiguous type lists")
    {
        const std::array<std::string, 5> groups = {
            R"({"types":[],"formation":"row"})",
            R"({"types":["normal","unknown"],"formation":"column"})",
            R"({"types":["normal",7],"formation":"row"})",
            R"({"type":"normal","count":2,"types":["fast","normal"],"formation":"row"})",
            R"({"types":["normal","fast"],"count":2,"formation":"column"})",
        };
        for (const auto& group : groups)
        {
            CampaignConfig campaign;
            std::string error;
            const std::string json = R"({"levels":[{"name":"broken","clearCrystals":1,"waves":[{"name":"wave","groups":[)" +
                                     group + R"(]}]}]})";
            CHECK_FALSE(campaign.loadFromJson(json, &error));
            CHECK(campaign.empty());
            CHECK_FALSE(error.empty());
        }
    }
}
