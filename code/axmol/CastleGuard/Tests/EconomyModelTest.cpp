#include "GameData.h"

#include "doctest/doctest.h"

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
}
