#pragma once

#include "CampaignReward.h"

#include <array>
#include <string>
#include <vector>

#ifndef CG_DEBUG_TOOLS
#define CG_DEBUG_TOOLS 1
#endif

// 一局战斗结算出的不可变结果。UI 只读这份结果，不自行计算奖励。
struct SettlementSummary
{
    bool committed       = false;
    bool victory         = false;
    bool stageAdvanced   = false;
    int kills            = 0;
    int coinsEarned      = 0;
    int crystalsEarned   = 0;
    int totalCoins       = 0;
    int totalCrystals    = 0;
};

struct CampaignBattleSupplies
{
    bool wallKit = false;
    bool manaCharm = false;
};

// 纯经济规则：不依赖引擎、不读写磁盘，便于单元测试和未来跨引擎复用。
class EconomyModel
{
public:
    static constexpr int COINS_PER_KILL       = 1;
    static constexpr int CLEAR_CRYSTAL_REWARD = 5;

    explicit EconomyModel(int totalCoins = 0, int totalCrystals = 0);

    void startRun();
    bool recordKill(int coins = COINS_PER_KILL);
    void checkpointRun();
    void rewindToCheckpoint();
    SettlementSummary settle(bool victory, int clearCrystalReward = CLEAR_CRYSTAL_REWARD);
    SettlementSummary discardRun(bool victory);
    bool spendCoins(int amount);
    bool spendCrystals(int amount);
    void grantCoins(int amount);
    void grantCrystals(int amount);
#if CG_DEBUG_TOOLS
    void debugSetBalances(int coins, int crystals);
#endif

    int totalCoins() const { return _totalCoins; }
    int totalCrystals() const { return _totalCrystals; }
    int runKills() const { return _runKills; }
    int runCoins() const { return _runCoins; }
    bool runActive() const { return _runActive; }

private:
    int _totalCoins      = 0;
    int _totalCrystals   = 0;
    int _runKills        = 0;
    int _runCoins        = 0;
    int _checkpointKills = 0;
    int _checkpointCoins = 0;
    bool _runActive      = false;
};

enum class Currency
{
    Coins,
    Crystals,
};

enum class UpgradeId
{
    ArrowDamage = 0,
    FireRate,
    MultiShot,
    WallHealth,
    HeavyStrike,
    Precision,
    ManaFlow,
    WallArmor,
    SpellEfficiency,
    Count,
};

enum class UpgradeTrack
{
    None,
    Bow,
    Magic,
};

enum class SkillId
{
    Fireball = 0,
    Freeze,
    ArrowRain,
    Lightning,
    Count,
};

struct UpgradeDefinition
{
    UpgradeId id = UpgradeId::ArrowDamage;
    std::string key;
    std::string name;
    std::string description;
    Currency currency = Currency::Coins;
    std::vector<int> costs;       // costs[level] 是从当前等级升一级的价格
    std::vector<float> values;    // values[level] 是等级效果；疾射使用相对基础间隔的倍率
    UpgradeId prerequisite = UpgradeId::Count;
    int requiredLevel = 0;
    int treeRow = -1;
    float treeX = 0.5f;
    UpgradeTrack track = UpgradeTrack::None;

    int maxLevel() const { return static_cast<int>(costs.size()); }
};

struct SkillDefinition
{
    SkillId id = SkillId::Fireball;
    std::string key;
    std::string name;
    std::string description;
    int unlockCost       = 0;
    bool defaultUnlocked = false;
    int unlockStage      = 0;  // >0 表示进入该关自动解锁；0 表示不由关卡里程碑解锁
    int treeRow          = 0;
    float treeX          = 0.5f;  // 研究树横向位置，0..1
    SkillId prerequisite = SkillId::Count;
};

bool validResearchTree(const std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)>& skills);
bool validUpgradeTree(const std::array<UpgradeDefinition, static_cast<size_t>(UpgradeId::Count)>& upgrades);
int wallDamageAfterArmor(int rawDamage, int armor);

enum class PurchaseStatus
{
    Purchased,
    InsufficientFunds,
    MaxLevel,
    AlreadyUnlocked,
    PrerequisiteLocked,
    InvalidDefinition,
};

struct PurchaseResult
{
    PurchaseStatus status = PurchaseStatus::InvalidDefinition;
    Currency currency     = Currency::Coins;
    int cost              = 0;

    bool purchased() const { return status == PurchaseStatus::Purchased; }
};

// 纯成长规则：只保存等级/解锁状态，价格和效果由外部数值表提供。
class ProgressionModel
{
public:
    int upgradeLevel(UpgradeId id) const;
    bool isSkillUnlocked(SkillId id) const;
    bool meetsUpgradePrerequisite(const UpgradeDefinition& definition) const;
    void setUpgradeLevel(UpgradeId id, int level);
    void setSkillUnlocked(SkillId id, bool unlocked);

    PurchaseResult purchaseUpgrade(EconomyModel& economy, const UpgradeDefinition& definition);
    PurchaseResult unlockSkill(EconomyModel& economy, const SkillDefinition& definition);
    bool grantUpgradeLevel(const UpgradeDefinition& definition);
    std::vector<SkillId> unlockSkillsForStage(
        int stage, const std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)>& definitions);

private:
    std::array<int, static_cast<size_t>(UpgradeId::Count)> _levels{};
    std::array<bool, static_cast<size_t>(SkillId::Count)> _unlocked{};
};

// 纯关卡进度规则：只允许顺序通关，防止异常结算或调试跳关越级写坏存档。
class CampaignProgressionModel
{
public:
    explicit CampaignProgressionModel(int highestClearedStage = 0, int highestRewardedStage = 0);

    int highestClearedStage() const { return _highestClearedStage; }
    int highestRewardedStage() const { return _highestRewardedStage; }
    int pendingRewardStage() const;
    int nextStage(int stageCount) const;
    bool completeStage(int stage, int stageCount);
    bool claimRewardStage(int stage);
    void setHighestClearedStage(int stage);
    void setHighestRewardedStage(int stage);

private:
    int _highestClearedStage = 0;
    int _highestRewardedStage = 0;
};

#if CG_DEBUG_TOOLS
// 开发者面板的可编辑快照。只在调试构建存在，正式包不暴露改档入口。
struct DebugPlayerData
{
    int coins    = 0;
    int crystals = 0;
    int highestClearedStage = 0;
    int highestRewardedStage = 0;
    int wallKitStock = 0;
    int manaCharmStock = 0;
    std::array<int, static_cast<size_t>(UpgradeId::Count)> upgradeLevels{};
    std::array<bool, static_cast<size_t>(SkillId::Count)> skillUnlocked{};
};
#endif

// 跨平台玩家数据入口。钱包、升级与技能解锁统一持久化；局内临时状态仍由场景管理。
class GameData
{
public:
    static GameData& getInstance();

    GameData(const GameData&)            = delete;
    GameData& operator=(const GameData&) = delete;

    void startRun();
    void abandonRun();
    bool recordKill(int coins = EconomyModel::COINS_PER_KILL);
    void checkpointRun();
    void rewindToCheckpoint();
    SettlementSummary settleRun(bool victory, int stage, int stageCount, int clearCrystalReward);
    SettlementSummary settleEndlessRun();
    SettlementSummary settlePracticeRun(bool victory);
    std::vector<SkillId> beginStage(int stage);
    int nextStage(int stageCount) const { return _campaign.nextStage(stageCount); }
    int highestClearedStage() const { return _campaign.highestClearedStage(); }
    int pendingCampaignRewardStage() const { return _campaign.pendingRewardStage(); }
    CampaignRewardContext campaignRewardContext(int stage) const;
    bool claimCampaignReward(int stage, CampaignRewardId id);
    CampaignBattleSupplies consumeCampaignSupplies();
    int wallKitStock() const { return _wallKitStock; }
    int manaCharmStock() const { return _manaCharmStock; }

    const UpgradeDefinition& upgradeDefinition(UpgradeId id) const;
    const SkillDefinition& skillDefinition(SkillId id) const;
    int upgradeLevel(UpgradeId id) const;
    bool upgradePrerequisiteMet(UpgradeId id) const;
    bool isSkillUnlocked(SkillId id) const;
    PurchaseResult purchaseUpgrade(UpgradeId id);
    PurchaseResult unlockSkill(SkillId id);

    int arrowDamage() const;
    float fireIntervalMultiplier() const;
    int multiShotCount() const;
    int wallMaxHealth() const;
    int wallArmor() const;
    float knockbackDistance() const;
    float criticalChance() const;
    float manaRegenBonus() const;
    float spellManaCostMultiplier() const;

#if CG_DEBUG_TOOLS
    DebugPlayerData debugPlayerData() const;
    void debugApplyPlayerData(const DebugPlayerData& data);
    void debugResetPlayerData();
#endif

    const EconomyModel& economy() const { return _economy; }

private:
    GameData();

    void loadCatalog();
    void load();
    void save() const;
    float upgradeValue(UpgradeId id) const;

    EconomyModel _economy;
    ProgressionModel _progression;
    CampaignProgressionModel _campaign;
    int _wallKitStock = 0;
    int _manaCharmStock = 0;
    std::array<UpgradeDefinition, static_cast<size_t>(UpgradeId::Count)> _upgrades;
    std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)> _skills;
};
