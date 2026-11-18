#include "RunBuff.h"

#include <algorithm>
#include <cmath>

namespace
{
const std::array<RunBuffDefinition, RunBuffModel::BUFF_COUNT> DEFINITIONS = {{
    {RunBuffId::MultiShot, "multi_shot", "分裂箭", "每次射击额外发射 1 支箭", 5},
    {RunBuffId::RapidFire, "rapid_fire", "疾风弦", "射击间隔缩短 20%", 5},
    {RunBuffId::Critical, "critical", "致命瞄准", "暴击率提高 15%，暴击造成双倍伤害", 5},
    {RunBuffId::ElementalArrow, "elemental_arrow", "元素箭", "每支箭附加 1 点伤害", 5},
}};

uint32_t nextRandom(uint32_t& state)
{
    state = state == 0 ? 0x9E3779B9u : state;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
}  // namespace

bool RunBuffModel::beginOffer(uint32_t seed)
{
    if (_pending)
    {
        return false;
    }

    std::array<RunBuffId, BUFF_COUNT> pool = {
        RunBuffId::MultiShot, RunBuffId::RapidFire, RunBuffId::Critical, RunBuffId::ElementalArrow};
    uint32_t state = seed ^ static_cast<uint32_t>(_offerCount + 1) * 0x85EBCA6Bu;
    for (size_t i = 0; i < CHOICE_COUNT; ++i)
    {
        const size_t pick = i + nextRandom(state) % (BUFF_COUNT - i);
        std::swap(pool[i], pool[pick]);
        _choices[i] = pool[i];
    }
    _pending = true;
    ++_offerCount;
    return true;
}

bool RunBuffModel::select(RunBuffId id)
{
    if (!_pending || std::find(_choices.begin(), _choices.end(), id) == _choices.end())
    {
        return false;
    }
    const size_t index = indexOf(id);
    _stacks[index] = std::min(_stacks[index] + 1, definition(id).maxStacks);
    _pending = false;
    return true;
}

void RunBuffModel::reset()
{
    _stacks.fill(0);
    _pending    = false;
    _offerCount = 0;
}

int RunBuffModel::stacks(RunBuffId id) const
{
    return _stacks[indexOf(id)];
}

PlayerCombatStats RunBuffModel::resolve(const PlayerCombatStats& base) const
{
    PlayerCombatStats result = base;
    result.multiShotCount = std::clamp(result.multiShotCount + stacks(RunBuffId::MultiShot), 1, 8);
    result.fireInterval *= std::pow(0.8f, static_cast<float>(stacks(RunBuffId::RapidFire)));
    result.fireInterval = std::max(0.05f, result.fireInterval);
    result.criticalChance = std::clamp(result.criticalChance + 0.15f * stacks(RunBuffId::Critical),
                                       0.0f, 0.75f);
    result.criticalDamage = std::max(1.0f, result.criticalDamage);
    result.arrowDamage = std::max(1, result.arrowDamage + stacks(RunBuffId::ElementalArrow));
    return result;
}

const RunBuffDefinition& RunBuffModel::definition(RunBuffId id)
{
    return DEFINITIONS[indexOf(id)];
}

size_t RunBuffModel::indexOf(RunBuffId id)
{
    const size_t index = static_cast<size_t>(id);
    return index < BUFF_COUNT ? index : 0;
}
