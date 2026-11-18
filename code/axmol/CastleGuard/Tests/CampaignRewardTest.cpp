#include "CampaignReward.h"

#include "doctest/doctest.h"

#include <set>

TEST_SUITE("CampaignRewardModel")
{
    TEST_CASE("offer has three unique choices and excludes maxed upgrades")
    {
        CampaignRewardModel model;
        CampaignRewardContext context;
        context.stage      = 3;
        context.canUpgrade = {true, false, false, true};

        REQUIRE(model.beginOffer(42, context));
        const auto choices = model.choices();
        CHECK_EQ(std::set<CampaignRewardId>(choices.begin(), choices.end()).size(),
                 CampaignRewardModel::CHOICE_COUNT);
        for (const auto id : choices)
        {
            CHECK_NE(id, CampaignRewardId::RapidTraining);
            CHECK_NE(id, CampaignRewardId::MultiShotTraining);
        }
        CHECK_FALSE(model.beginOffer(43, context));
        CHECK_FALSE(model.select(CampaignRewardId::Count));
        CHECK(model.select(choices.front()));
        CHECK_FALSE(model.hasPendingOffer());
    }

    TEST_CASE("resource rewards scale by stage and training grants one upgrade")
    {
        const auto coins = CampaignRewardModel::grant(CampaignRewardId::CoinCache, 3);
        CHECK_EQ(coins.coins, 25);
        CHECK_EQ(coins.crystals, 0);

        const auto crystals = CampaignRewardModel::grant(CampaignRewardId::CrystalCache, 4);
        CHECK_EQ(crystals.crystals, 2);

        const auto training = CampaignRewardModel::grant(CampaignRewardId::WallTraining, 2);
        CHECK_EQ(training.upgradeIndex, 3);
        CHECK_EQ(training.coins, 0);
    }

    TEST_CASE("all maxed progression still has three resource choices")
    {
        CampaignRewardModel model;
        CampaignRewardContext context;
        context.stage = 5;
        REQUIRE(model.beginOffer(9, context));
        const auto choices = model.choices();
        for (const auto id : choices)
        {
            CHECK_LT(static_cast<int>(id), static_cast<int>(CampaignRewardId::ArrowTraining));
        }
    }
}
