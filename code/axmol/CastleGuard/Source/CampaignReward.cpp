#include "CampaignReward.h"

#include <algorithm>
#include <array>

namespace
{
constexpr std::array<CampaignRewardDefinition, CampaignRewardModel::REWARD_COUNT> DEFINITIONS = {{
    {CampaignRewardId::CoinCache, "coin_cache", "金币宝箱", false},
    {CampaignRewardId::CrystalCache, "crystal_cache", "水晶匣", false},
    {CampaignRewardId::SupplyCrate, "supply_crate", "混合补给", false},
    {CampaignRewardId::WallKit, "wall_kit", "城墙补给", true},
    {CampaignRewardId::ManaCharm, "mana_charm", "法力符文", true},
}};

uint32_t nextRandom(uint32_t& state)
{
    if (state == 0)
    {
        state = 0xA341316Cu;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
}  // namespace

bool CampaignRewardModel::beginOffer(uint32_t seed, const CampaignRewardContext& context)
{
    if (_pending)
    {
        return false;
    }

    std::array<CampaignRewardId, 3> resources = {
        CampaignRewardId::CoinCache,
        CampaignRewardId::CrystalCache,
        CampaignRewardId::SupplyCrate,
    };
    std::array<CampaignRewardId, 2> tactics = {
        CampaignRewardId::WallKit, CampaignRewardId::ManaCharm,
    };

    uint32_t state = seed ^ static_cast<uint32_t>(std::max(1, context.stage)) * 0x9E3779B9u;
    for (size_t i = resources.size(); i > 1; --i)
        std::swap(resources[i - 1], resources[nextRandom(state) % i]);
    if (nextRandom(state) & 1u) std::swap(tactics[0], tactics[1]);
    const std::array<CampaignRewardId, 3> remaining = {
        resources[1], resources[2], tactics[1],
    };
    _choices = {resources[0], tactics[0], remaining[nextRandom(state) % remaining.size()]};
    for (size_t i = _choices.size(); i > 1; --i)
        std::swap(_choices[i - 1], _choices[nextRandom(state) % i]);
    _stage   = std::max(1, context.stage);
    _pending = true;
    return true;
}

bool CampaignRewardModel::select(CampaignRewardId id)
{
    if (!_pending || std::find(_choices.begin(), _choices.end(), id) == _choices.end())
    {
        return false;
    }
    _pending = false;
    return true;
}

void CampaignRewardModel::reset()
{
    _choices.fill(CampaignRewardId::CoinCache);
    _stage   = 0;
    _pending = false;
}

const CampaignRewardDefinition& CampaignRewardModel::definition(CampaignRewardId id)
{
    const size_t index = std::min(static_cast<size_t>(id), DEFINITIONS.size() - 1);
    return DEFINITIONS[index];
}

CampaignRewardGrant CampaignRewardModel::grant(CampaignRewardId id, int stage)
{
    CampaignRewardGrant result;
    const int safeStage = std::max(1, stage);
    switch (id)
    {
    case CampaignRewardId::CoinCache:
        result.coins = 10 + safeStage * 5;
        break;
    case CampaignRewardId::CrystalCache:
        result.crystals = safeStage >= 4 ? 2 : 1;
        break;
    case CampaignRewardId::SupplyCrate:
        result.coins    = 5 + safeStage * 3;
        result.crystals = 1;
        break;
    case CampaignRewardId::WallKit:
        result.wallKits = 1;
        break;
    case CampaignRewardId::ManaCharm:
        result.manaCharms = 1;
        break;
    default:
        break;
    }
    return result;
}

std::string CampaignRewardModel::description(CampaignRewardId id, int stage)
{
    const auto value = grant(id, stage);
    if (value.wallKits > 0) return "下一局城墙生命 +20";
    if (value.manaCharms > 0) return "下一局回蓝速度 +2";
    if (value.coins > 0 && value.crystals > 0)
    {
        return "+" + std::to_string(value.coins) + "  /  +" +
               std::to_string(value.crystals);
    }
    if (value.coins > 0)
    {
        return "+" + std::to_string(value.coins);
    }
    return "+" + std::to_string(value.crystals);
}
