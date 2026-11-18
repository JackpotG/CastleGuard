#include "GameData.h"

#include "doctest/doctest.h"

#include <algorithm>

TEST_SUITE("EconomyModel")
{
    TEST_CASE("victory banks kill coins and clear crystals")
    {
        EconomyModel economy(10, 2);
        economy.startRun();
        CHECK(economy.recordKill());
        CHECK(economy.recordKill());
        CHECK_EQ(economy.runKills(), 2);
        CHECK_EQ(economy.runCoins(), 2 * EconomyModel::COINS_PER_KILL);

        const auto result = economy.settle(true);
        CHECK(result.committed);
        CHECK(result.victory);
        CHECK_EQ(result.kills, 2);
        CHECK_EQ(result.coinsEarned, 2);
        CHECK_EQ(result.crystalsEarned, EconomyModel::CLEAR_CRYSTAL_REWARD);
        CHECK_EQ(result.totalCoins, 12);
        CHECK_EQ(result.totalCrystals, 7);
    }

    TEST_CASE("defeat keeps kill coins but grants no crystals")
    {
        EconomyModel economy;
        economy.startRun();
        CHECK(economy.recordKill());

        const auto result = economy.settle(false);
        CHECK(result.committed);
        CHECK_FALSE(result.victory);
        CHECK_EQ(result.coinsEarned, EconomyModel::COINS_PER_KILL);
        CHECK_EQ(result.crystalsEarned, 0);
        CHECK_EQ(result.totalCoins, EconomyModel::COINS_PER_KILL);
        CHECK_EQ(result.totalCrystals, 0);
    }

    TEST_CASE("each level can provide its configured crystal reward")
    {
        EconomyModel economy;
        economy.startRun();

        const auto result = economy.settle(true, 2);

        CHECK(result.committed);
        CHECK_EQ(result.crystalsEarned, 2);
        CHECK_EQ(result.totalCrystals, 2);
    }

    TEST_CASE("settlement is idempotent and kills outside a run are ignored")
    {
        EconomyModel economy(3, 4);
        CHECK_FALSE(economy.recordKill());

        economy.startRun();
        CHECK(economy.recordKill());
        const auto first = economy.settle(true);
        const auto second = economy.settle(true);

        CHECK(first.committed);
        CHECK_FALSE(second.committed);
        CHECK_EQ(economy.totalCoins(), 4);
        CHECK_EQ(economy.totalCrystals(), 9);
        CHECK_FALSE(economy.recordKill());
    }

    TEST_CASE("starting another run clears only temporary rewards")
    {
        EconomyModel economy(8, 6);
        economy.startRun();
        CHECK(economy.recordKill());
        economy.startRun();

        CHECK_EQ(economy.runKills(), 0);
        CHECK_EQ(economy.runCoins(), 0);
        CHECK_EQ(economy.totalCoins(), 8);
        CHECK_EQ(economy.totalCrystals(), 6);
    }

    TEST_CASE("practice settlement reports kills without banking temporary rewards")
    {
        EconomyModel economy(8, 6);
        economy.startRun();
        CHECK(economy.recordKill());
        CHECK(economy.recordKill());

        const auto result = economy.discardRun(true);

        CHECK(result.committed);
        CHECK(result.victory);
        CHECK_EQ(result.kills, 2);
        CHECK_EQ(result.coinsEarned, 0);
        CHECK_EQ(result.crystalsEarned, 0);
        CHECK_EQ(result.totalCoins, 8);
        CHECK_EQ(result.totalCrystals, 6);
        CHECK_FALSE(economy.runActive());
        CHECK_FALSE(economy.recordKill());
    }

    TEST_CASE("permanent grants are clamped to non-negative amounts")
    {
        EconomyModel economy(3, 2);
        economy.grantCoins(7);
        economy.grantCrystals(4);
        economy.grantCoins(-100);
        economy.grantCrystals(-100);

        CHECK_EQ(economy.totalCoins(), 10);
        CHECK_EQ(economy.totalCrystals(), 6);
    }

    TEST_CASE("replaying the current wave rewinds only unbanked wave rewards")
    {
        EconomyModel economy(5, 1);
        economy.startRun();
        CHECK(economy.recordKill());
        economy.checkpointRun();
        CHECK(economy.recordKill());
        CHECK(economy.recordKill());

        economy.rewindToCheckpoint();

        CHECK_EQ(economy.runKills(), 1);
        CHECK_EQ(economy.runCoins(), EconomyModel::COINS_PER_KILL);
        CHECK_EQ(economy.totalCoins(), 5);
        CHECK_EQ(economy.totalCrystals(), 1);
    }
}

TEST_SUITE("ProgressionModel")
{
    const UpgradeDefinition damageUpgrade{UpgradeId::ArrowDamage, "arrow_damage", "Damage", "More damage",
                                             Currency::Coins, {8, 18}, {1, 2, 3}};
    const SkillDefinition lightning{SkillId::Lightning, "lightning", "Lightning", "Stun enemies", 5, false};

    TEST_CASE("upgrade purchase spends the configured currency and raises one level")
    {
        EconomyModel economy(20, 0);
        ProgressionModel progression;

        const auto result = progression.purchaseUpgrade(economy, damageUpgrade);

        CHECK(result.purchased());
        CHECK_EQ(result.cost, 8);
        CHECK_EQ(progression.upgradeLevel(UpgradeId::ArrowDamage), 1);
        CHECK_EQ(economy.totalCoins(), 12);
    }

    TEST_CASE("failed purchase never changes wallet or progression")
    {
        EconomyModel economy(7, 4);
        ProgressionModel progression;

        const auto upgradeResult = progression.purchaseUpgrade(economy, damageUpgrade);
        const auto skillResult   = progression.unlockSkill(economy, lightning);

        CHECK_EQ(upgradeResult.status, PurchaseStatus::InsufficientFunds);
        CHECK_EQ(skillResult.status, PurchaseStatus::InsufficientFunds);
        CHECK_EQ(progression.upgradeLevel(UpgradeId::ArrowDamage), 0);
        CHECK_FALSE(progression.isSkillUnlocked(SkillId::Lightning));
        CHECK_EQ(economy.totalCoins(), 7);
        CHECK_EQ(economy.totalCrystals(), 4);
    }

    TEST_CASE("max level and repeated skill unlock are idempotent")
    {
        EconomyModel economy(100, 20);
        ProgressionModel progression;
        progression.setUpgradeLevel(UpgradeId::ArrowDamage, damageUpgrade.maxLevel());

        const auto maxResult    = progression.purchaseUpgrade(economy, damageUpgrade);
        const auto firstUnlock  = progression.unlockSkill(economy, lightning);
        const auto secondUnlock = progression.unlockSkill(economy, lightning);

        CHECK_EQ(maxResult.status, PurchaseStatus::MaxLevel);
        CHECK(firstUnlock.purchased());
        CHECK_EQ(secondUnlock.status, PurchaseStatus::AlreadyUnlocked);
        CHECK_EQ(economy.totalCoins(), 100);
        CHECK_EQ(economy.totalCrystals(), 15);
    }

    TEST_CASE("free progression reward raises a level without spending currency")
    {
        EconomyModel economy(5, 2);
        ProgressionModel progression;

        CHECK(progression.grantUpgradeLevel(damageUpgrade));
        CHECK_EQ(progression.upgradeLevel(UpgradeId::ArrowDamage), 1);
        CHECK_EQ(economy.totalCoins(), 5);
        progression.setUpgradeLevel(UpgradeId::ArrowDamage, damageUpgrade.maxLevel());
        CHECK_FALSE(progression.grantUpgradeLevel(damageUpgrade));
    }

    TEST_CASE("the first milestone skill unlocks at stage three")
    {
        ProgressionModel progression;
        const std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)> skills = {
            SkillDefinition{SkillId::Fireball, "fireball", "Fireball", "AOE", 0, false, 3},
            SkillDefinition{SkillId::Freeze, "freeze", "Freeze", "Control", 0, false, 4},
            SkillDefinition{SkillId::ArrowRain, "arrow_rain", "Rain", "AOE", 0, false, 5},
            SkillDefinition{SkillId::Lightning, "lightning", "Lightning", "Stun", 5, false, 0},
        };

        CHECK(progression.unlockSkillsForStage(2, skills).empty());
        CHECK_FALSE(progression.isSkillUnlocked(SkillId::Fireball));

        const auto stageThree = progression.unlockSkillsForStage(3, skills);
        REQUIRE_EQ(stageThree.size(), 1);
        CHECK_EQ(stageThree.front(), SkillId::Fireball);
        CHECK(progression.isSkillUnlocked(SkillId::Fireball));
        CHECK_FALSE(progression.isSkillUnlocked(SkillId::Freeze));
        CHECK(progression.unlockSkillsForStage(3, skills).empty());

        const auto stageFive = progression.unlockSkillsForStage(5, skills);
        CHECK_EQ(stageFive.size(), 2);
        CHECK(progression.isSkillUnlocked(SkillId::Freeze));
        CHECK(progression.isSkillUnlocked(SkillId::ArrowRain));
        CHECK_FALSE(progression.isSkillUnlocked(SkillId::Lightning));
    }
}

TEST_SUITE("CampaignProgressionModel")
{
    TEST_CASE("progress advances sequentially and final stage remains replayable")
    {
        CampaignProgressionModel campaign;

        CHECK_EQ(campaign.nextStage(5), 1);
        CHECK_FALSE(campaign.completeStage(3, 5));
        CHECK_EQ(campaign.highestClearedStage(), 0);

        CHECK(campaign.completeStage(1, 5));
        CHECK_EQ(campaign.nextStage(5), 2);
        CHECK_FALSE(campaign.completeStage(1, 5));
        CHECK(campaign.completeStage(2, 5));
        CHECK(campaign.completeStage(3, 5));
        CHECK(campaign.completeStage(4, 5));
        CHECK(campaign.completeStage(5, 5));
        CHECK_EQ(campaign.nextStage(5), 5);
        CHECK_FALSE(campaign.completeStage(5, 5));
    }

    TEST_CASE("first-clear rewards remain pending and can only be claimed in order")
    {
        CampaignProgressionModel campaign(3, 1);
        CHECK_EQ(campaign.pendingRewardStage(), 2);
        CHECK_FALSE(campaign.claimRewardStage(3));
        CHECK(campaign.claimRewardStage(2));
        CHECK_EQ(campaign.pendingRewardStage(), 3);
        CHECK(campaign.claimRewardStage(3));
        CHECK_EQ(campaign.pendingRewardStage(), 0);
        CHECK_FALSE(campaign.claimRewardStage(3));
    }
}

TEST_SUITE("ProductionProgressionCatalog")
{
    TEST_CASE("production catalog places its first automatic skill at stage three")
    {
        const auto& data = GameData::getInstance();
        int firstUnlockStage = 999;
        for (int i = 0; i < static_cast<int>(SkillId::Count); ++i)
        {
            const auto& definition = data.skillDefinition(static_cast<SkillId>(i));
            if (definition.unlockStage > 0)
            {
                firstUnlockStage = std::min(firstUnlockStage, definition.unlockStage);
            }
        }

        CHECK_EQ(firstUnlockStage, 3);
        CHECK_EQ(data.skillDefinition(SkillId::Fireball).unlockStage, 3);
        CHECK_FALSE(data.skillDefinition(SkillId::Fireball).defaultUnlocked);
    }
}
