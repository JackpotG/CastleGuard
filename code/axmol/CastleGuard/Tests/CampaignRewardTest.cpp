#include "CampaignReward.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <set>

TEST_SUITE("CampaignRewardModel")
{
    TEST_CASE("each offer has three unique rewards including resources and tactical supplies")
    {
        CampaignRewardModel model;
        CampaignRewardContext context;
        context.stage = 3;

        for (uint32_t seed = 1; seed <= 50; ++seed)
        {
            REQUIRE(model.beginOffer(seed, context));
            const auto choices = model.choices();
            CHECK_EQ(std::set<CampaignRewardId>(choices.begin(), choices.end()).size(),
                     CampaignRewardModel::CHOICE_COUNT);
            CHECK(std::any_of(choices.begin(), choices.end(), [](CampaignRewardId id) {
                return !CampaignRewardModel::definition(id).tactical;
            }));
            CHECK(std::any_of(choices.begin(), choices.end(), [](CampaignRewardId id) {
                return CampaignRewardModel::definition(id).tactical;
            }));
            CHECK_FALSE(model.beginOffer(seed + 1, context));
            CHECK_FALSE(model.select(CampaignRewardId::Count));
            CHECK(model.select(choices.front()));
            CHECK_FALSE(model.hasPendingOffer());
        }
    }

    TEST_CASE("rewards grant resources or one-use supplies without permanent upgrades")
    {
        const auto coins = CampaignRewardModel::grant(CampaignRewardId::CoinCache, 3);
        CHECK_EQ(coins.coins, 25);
        CHECK_EQ(coins.crystals, 0);

        const auto crystals = CampaignRewardModel::grant(CampaignRewardId::CrystalCache, 4);
        CHECK_EQ(crystals.crystals, 2);

        const auto mixed = CampaignRewardModel::grant(CampaignRewardId::SupplyCrate, 2);
        CHECK_EQ(mixed.coins, 11);
        CHECK_EQ(mixed.crystals, 1);

        const auto wall = CampaignRewardModel::grant(CampaignRewardId::WallKit, 2);
        CHECK_EQ(wall.wallKits, 1);
        CHECK_EQ(wall.manaCharms, 0);

        const auto mana = CampaignRewardModel::grant(CampaignRewardId::ManaCharm, 5);
        CHECK_EQ(mana.manaCharms, 1);
        CHECK_EQ(mana.wallKits, 0);
    }
}
