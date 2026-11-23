#include "MonsterBalance.h"

#include "doctest/doctest.h"

TEST_SUITE("MonsterBalance")
{
    TEST_CASE("production defaults cover every monster type")
    {
        MonsterBalance balance;

        CHECK_EQ(MonsterBalance::TYPE_COUNT, 13);
        CHECK_EQ(balance.baseSpeed(), doctest::Approx(70.0f));
        CHECK_EQ(balance.baseBodyRadius(), doctest::Approx(15.0f));

        for (size_t i = 0; i < MonsterBalance::TYPE_COUNT; ++i)
        {
            const auto& value = balance.parameters(static_cast<MonsterType>(i));
            CHECK_GE(value.maxHp, 1);
            CHECK_GE(value.coinDrop, 1);
            CHECK_GT(value.speedMultiplier, 0.0f);
            CHECK_GT(value.bodyScale, 0.0f);
            CHECK_GE(value.actionPower, 0);
            CHECK_GT(value.actionInterval, 0.0f);
            CHECK_GE(value.stopRatioMax, value.stopRatioMin);
            CHECK_GE(value.knockbackFactor, 0.0f);
        }

        CHECK_EQ(balance.parameters(MonsterType::Boss).maxHp, 30);
        CHECK_EQ(balance.parameters(MonsterType::Normal).maxHp, doctest::Approx(2.0f));
        CHECK_LT(balance.parameters(MonsterType::Normal).coinDrop,
                 balance.parameters(MonsterType::Shield).coinDrop);
        CHECK_LT(balance.parameters(MonsterType::Shield).coinDrop,
                 balance.parameters(MonsterType::Boss).coinDrop);
        CHECK_EQ(balance.healthAtStage(MonsterType::Normal, 1), doctest::Approx(2.0f));
        CHECK_EQ(balance.healthAtStage(MonsterType::Normal, 2), doctest::Approx(2.4f));
        CHECK_EQ(balance.healthAtStage(MonsterType::Normal, 5), doctest::Approx(3.6f));
        for (const int stage : {6, 25, 50})
        {
            const float multiplier = 1.0f + 0.20f * (stage - 1);
            for (size_t i = 0; i < MonsterBalance::TYPE_COUNT; ++i)
            {
                const auto type = static_cast<MonsterType>(i);
                CHECK_EQ(balance.healthAtStage(type, stage),
                         doctest::Approx(balance.parameters(type).maxHp * multiplier));
            }
        }
        CHECK_EQ(balance.parameters(MonsterType::Boss).knockbackFactor, doctest::Approx(0.20f));
        CHECK_EQ(balance.spawnSpeed(MonsterType::Normal, 0.5f), doctest::Approx(70.0f));
        CHECK_EQ(balance.spawnSpeed(MonsterType::Fast, 0.5f), doctest::Approx(126.0f));
        CHECK_EQ(balance.spawnSpeed(MonsterType::Normal, 0.0f, 72.0f), doctest::Approx(72.0f));
        CHECK_EQ(balance.spawnSpeed(MonsterType::Fast, 1.0f, 72.0f), doctest::Approx(72.0f));
        CHECK_EQ(balance.resolveKnockbackDistance(MonsterType::Boss, 40.0f),
                 doctest::Approx(8.0f));
        CHECK_EQ(balance.parameters(MonsterType::Bomber).actionPower, 15);
        CHECK_EQ(balance.parameters(MonsterType::Healer).actionPower, 1);
        CHECK_EQ(balance.parameters(MonsterType::Summoner).actionPower, 1);
    }

    TEST_CASE("debug edits are validated and can be reset")
    {
        MonsterBalance balance;
        MonsterParameters invalid{-10, -2.0f, 20.0f, -3, 0.0f, 0.80f, 0.20f, -5.0f, -4};

        balance.setBaseSpeed(900.0f);
        balance.setBaseBodyRadius(100.0f);
        balance.setParameters(MonsterType::Fast, invalid);
        const auto& sanitized = balance.parameters(MonsterType::Fast);
        CHECK_EQ(balance.baseSpeed(), doctest::Approx(500.0f));
        CHECK_EQ(balance.baseBodyRadius(), doctest::Approx(48.0f));
        CHECK_EQ(sanitized.maxHp, 1);
        CHECK_EQ(sanitized.coinDrop, 1);
        CHECK_EQ(sanitized.speedMultiplier, doctest::Approx(0.1f));
        CHECK_EQ(sanitized.bodyScale, doctest::Approx(5.0f));
        CHECK_EQ(sanitized.actionPower, 0);
        CHECK_EQ(sanitized.actionInterval, doctest::Approx(0.05f));
        CHECK_EQ(sanitized.stopRatioMin, doctest::Approx(0.80f));
        CHECK_EQ(sanitized.stopRatioMax, doctest::Approx(0.80f));
        CHECK_EQ(sanitized.knockbackFactor, doctest::Approx(0.0f));
        CHECK_EQ(balance.resolveKnockbackDistance(MonsterType::Fast, -100.0f),
                 doctest::Approx(0.0f));

        balance.reset(MonsterType::Fast);
        CHECK_EQ(balance.parameters(MonsterType::Fast).speedMultiplier, doctest::Approx(1.8f));
        CHECK_EQ(balance.baseSpeed(), doctest::Approx(500.0f));

        balance.resetAll();
        CHECK_EQ(balance.baseSpeed(), doctest::Approx(MonsterBalance::defaultBaseSpeed()));
        CHECK_EQ(balance.baseBodyRadius(), doctest::Approx(MonsterBalance::defaultBaseBodyRadius()));
        CHECK_EQ(balance.parameters(MonsterType::Fast).speedMultiplier, doctest::Approx(1.8f));
    }
}
