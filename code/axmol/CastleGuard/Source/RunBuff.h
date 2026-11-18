#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class RunBuffId
{
    MultiShot = 0,
    RapidFire,
    Critical,
    ElementalArrow,
    Count,
};

struct RunBuffDefinition
{
    RunBuffId id = RunBuffId::MultiShot;
    const char* key = "";
    const char* name = "";
    const char* description = "";
    int maxStacks = 5;
};

struct PlayerCombatStats
{
    int arrowDamage       = 1;
    float fireInterval    = 0.18f;
    int multiShotCount    = 1;
    float criticalChance  = 0.0f;
    float criticalDamage  = 2.0f;
    float knockbackDistance = 36.0f;
};

// 局内增益的逻辑所有者：候选、叠层和属性修饰都在这里，UI 只读结果。
class RunBuffModel
{
public:
    static constexpr size_t BUFF_COUNT = static_cast<size_t>(RunBuffId::Count);
    static constexpr size_t CHOICE_COUNT = 3;
    using Choices = std::array<RunBuffId, CHOICE_COUNT>;

    bool beginOffer(uint32_t seed);
    bool select(RunBuffId id);
    void reset();

    bool hasPendingOffer() const { return _pending; }
    const Choices& choices() const { return _choices; }
    int stacks(RunBuffId id) const;
    int offerCount() const { return _offerCount; }

    PlayerCombatStats resolve(const PlayerCombatStats& base) const;

    static const RunBuffDefinition& definition(RunBuffId id);

private:
    static size_t indexOf(RunBuffId id);

    std::array<int, BUFF_COUNT> _stacks{};
    Choices _choices{RunBuffId::MultiShot, RunBuffId::RapidFire, RunBuffId::Critical};
    bool _pending = false;
    int _offerCount = 0;
};
