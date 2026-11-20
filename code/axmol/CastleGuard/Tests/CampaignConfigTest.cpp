#include "CampaignConfig.h"

#include "axmol.h"
#include "doctest/doctest.h"

#include <array>

TEST_SUITE("CampaignConfig")
{
    TEST_CASE("campaign starts gently and grows through authored levels")
    {
        CampaignConfig campaign;
        std::string error;
        const auto json = ax::FileUtils::getInstance()->getStringFromFile("config/waves.json");

        REQUIRE(campaign.loadFromJson(json, &error));
        REQUIRE_EQ(campaign.levels().size(), 5);

        const std::array<int, 5> expectedEnemies = {10, 19, 29, 40, 51};
        const std::array<int, 5> expectedCrystals = {1, 1, 2, 2, 4};
        for (size_t i = 0; i < campaign.levels().size(); ++i)
        {
            const auto& level = campaign.levels()[i];
            CHECK_FALSE(level.waves.empty());
            CHECK_EQ(level.configuredEnemyCount(), expectedEnemies[i]);
            CHECK_EQ(level.clearCrystals, expectedCrystals[i]);
            if (i > 0)
            {
                CHECK_GT(level.configuredEnemyCount(), campaign.levels()[i - 1].configuredEnemyCount());
            }
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
}
