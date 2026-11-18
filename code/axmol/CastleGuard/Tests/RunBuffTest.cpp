#include "RunBuff.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <set>

namespace
{
void selectTarget(RunBuffModel& model, RunBuffId target)
{
    for (uint32_t seed = 1; seed < 1000; ++seed)
    {
        REQUIRE(model.beginOffer(seed));
        const auto& choices = model.choices();
        if (std::find(choices.begin(), choices.end(), target) != choices.end())
        {
            REQUIRE(model.select(target));
            return;
        }
        REQUIRE(model.select(choices.front()));
    }
    FAIL("target buff was never offered");
}
}  // namespace

TEST_SUITE("RunBuffModel")
{
    TEST_CASE("every offer contains three unique choices and accepts one selection")
    {
        RunBuffModel model;
        REQUIRE(model.beginOffer(42));
        CHECK(model.hasPendingOffer());

        const auto& choices = model.choices();
        const std::set<RunBuffId> unique(choices.begin(), choices.end());
        CHECK_EQ(unique.size(), RunBuffModel::CHOICE_COUNT);
        CHECK_FALSE(model.beginOffer(43));

        REQUIRE(model.select(choices[1]));
        CHECK_FALSE(model.hasPendingOffer());
        CHECK_EQ(model.stacks(choices[1]), 1);
        CHECK_FALSE(model.select(choices[0]));
    }

    TEST_CASE("modifier chain composes run buffs over persistent base stats")
    {
        PlayerCombatStats base{3, 0.20f, 2, 0.0f, 2.0f};

        RunBuffModel multi;
        selectTarget(multi, RunBuffId::MultiShot);
        CHECK_EQ(multi.resolve(base).multiShotCount, 3);

        RunBuffModel rapid;
        selectTarget(rapid, RunBuffId::RapidFire);
        CHECK_EQ(rapid.resolve(base).fireInterval, doctest::Approx(0.16f));

        RunBuffModel critical;
        selectTarget(critical, RunBuffId::Critical);
        CHECK_EQ(critical.resolve(base).criticalChance, doctest::Approx(0.15f));

        RunBuffModel elemental;
        selectTarget(elemental, RunBuffId::ElementalArrow);
        CHECK_EQ(elemental.resolve(base).arrowDamage, 4);

        CHECK_EQ(elemental.resolve(base).knockbackDistance, doctest::Approx(36.0f));
    }

    TEST_CASE("reset removes temporary stacks and pending choices")
    {
        RunBuffModel model;
        selectTarget(model, RunBuffId::MultiShot);
        REQUIRE(model.beginOffer(8));

        model.reset();

        CHECK_FALSE(model.hasPendingOffer());
        CHECK_EQ(model.offerCount(), 0);
        for (size_t i = 0; i < RunBuffModel::BUFF_COUNT; ++i)
        {
            CHECK_EQ(model.stacks(static_cast<RunBuffId>(i)), 0);
        }
    }
}
