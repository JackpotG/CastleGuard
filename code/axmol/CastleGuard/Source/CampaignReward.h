#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

enum class CampaignRewardId
{
    CoinCache = 0,
    CrystalCache,
    SupplyCrate,
    WallKit,
    ManaCharm,
    Count,
};

struct CampaignRewardDefinition
{
    CampaignRewardId id = CampaignRewardId::CoinCache;
    const char* key      = "";
    const char* name     = "";
    bool tactical       = false;
};

struct CampaignRewardContext
{
    int stage = 1;
};

struct CampaignRewardGrant
{
    int coins        = 0;
    int crystals     = 0;
    int wallKits     = 0;
    int manaCharms   = 0;
};

// 推关模式的首通奖励候选器。它只负责抽取和选择校验，
// 实际入账与存档由 GameData 统一完成。
class CampaignRewardModel
{
public:
    static constexpr size_t CHOICE_COUNT = 3;
    static constexpr size_t REWARD_COUNT = static_cast<size_t>(CampaignRewardId::Count);

    bool beginOffer(uint32_t seed, const CampaignRewardContext& context);
    bool select(CampaignRewardId id);
    void reset();

    bool hasPendingOffer() const { return _pending; }
    int stage() const { return _stage; }
    const std::array<CampaignRewardId, CHOICE_COUNT>& choices() const { return _choices; }

    static const CampaignRewardDefinition& definition(CampaignRewardId id);
    static CampaignRewardGrant grant(CampaignRewardId id, int stage);
    static std::string description(CampaignRewardId id, int stage);

private:
    std::array<CampaignRewardId, CHOICE_COUNT> _choices{};
    int _stage   = 0;
    bool _pending = false;
};
