#include "CampaignReward.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{
constexpr std::array<CampaignRewardDefinition, CampaignRewardModel::REWARD_COUNT> DEFINITIONS = {{
    {CampaignRewardId::CoinCache, "coin_cache", "金币宝箱", -1},
    {CampaignRewardId::CrystalCache, "crystal_cache", "水晶匣", -1},
    {CampaignRewardId::SupplyCrate, "supply_crate", "混合补给", -1},
    {CampaignRewardId::ArrowTraining, "arrow_training", "强弓训练", 0},
    {CampaignRewardId::RapidTraining, "rapid_training", "疾射训练", 1},
    {CampaignRewardId::MultiShotTraining, "multi_training", "多重箭训练", 2},
    {CampaignRewardId::WallTraining, "wall_training", "城墙加固", 3},
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

    std::vector<CampaignRewardId> candidates = {
        CampaignRewardId::CoinCache,
        CampaignRewardId::CrystalCache,
        CampaignRewardId::SupplyCrate,
    };
    for (size_t i = 0; i < context.canUpgrade.size(); ++i)
    {
        if (context.canUpgrade[i])
        {
            candidates.push_back(static_cast<CampaignRewardId>(
                static_cast<int>(CampaignRewardId::ArrowTraining) + static_cast<int>(i)));
        }
    }

    uint32_t state = seed ^ static_cast<uint32_t>(std::max(1, context.stage)) * 0x9E3779B9u;
    for (size_t i = candidates.size(); i > 1; --i)
    {
        const size_t j = nextRandom(state) % i;
        std::swap(candidates[i - 1], candidates[j]);
    }
    for (size_t i = 0; i < CHOICE_COUNT; ++i)
    {
        _choices[i] = candidates[i];
    }
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
    case CampaignRewardId::ArrowTraining:
    case CampaignRewardId::RapidTraining:
    case CampaignRewardId::MultiShotTraining:
    case CampaignRewardId::WallTraining:
        result.upgradeIndex = definition(id).upgradeIndex;
        break;
    default:
        break;
    }
    return result;
}

std::string CampaignRewardModel::description(CampaignRewardId id, int stage)
{
    const auto value = grant(id, stage);
    if (value.upgradeIndex >= 0)
    {
        return "对应永久养成等级 +1（已满级时不会进入候选）";
    }
    if (value.coins > 0 && value.crystals > 0)
    {
        return "永久获得 " + std::to_string(value.coins) + " 金币和 " +
               std::to_string(value.crystals) + " 水晶";
    }
    if (value.coins > 0)
    {
        return "永久获得 " + std::to_string(value.coins) + " 金币";
    }
    return "永久获得 " + std::to_string(value.crystals) + " 水晶";
}
