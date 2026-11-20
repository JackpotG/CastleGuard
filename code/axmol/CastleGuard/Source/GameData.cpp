#include "GameData.h"

#include "axmol.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#if AX_TARGET_PLATFORM == AX_PLATFORM_WASM
#    include <emscripten/emscripten.h>
#endif

namespace
{
constexpr int SAVE_VERSION = 4;
constexpr const char* KEY_SAVE_VERSION = "cg.save.version";
constexpr const char* KEY_COINS        = "cg.save.coins";
constexpr const char* KEY_CRYSTALS     = "cg.save.crystals";
constexpr const char* KEY_HIGHEST_CLEARED_STAGE = "cg.save.campaign.highestClearedStage";
constexpr const char* KEY_HIGHEST_REWARDED_STAGE = "cg.save.campaign.highestRewardedStage";
constexpr const char* CATALOG_PATH     = "config/upgrades.json";

constexpr std::array<const char*, static_cast<size_t>(UpgradeId::Count)> UPGRADE_KEYS = {
    "arrow_damage", "fire_rate", "multi_shot", "wall_health"};
constexpr std::array<const char*, static_cast<size_t>(SkillId::Count)> SKILL_KEYS = {
    "fireball", "freeze", "arrow_rain", "lightning"};

int addClamped(int value, int amount)
{
    if (amount > 0 && value > std::numeric_limits<int>::max() - amount)
    {
        return std::numeric_limits<int>::max();
    }
    return value + amount;
}

int readStoredInt(const char* key, int defaultValue)
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_WASM
    return EM_ASM_INT({
        const key = UTF8ToString($0);
        const fallback = $1;
        try {
            let value = null;
            if (typeof wx !== 'undefined' && typeof wx.getStorageSync === 'function') {
                value = wx.getStorageSync(key);
            } else if (typeof localStorage !== 'undefined') {
                value = localStorage.getItem(key);
            }
            if (value === null || value === undefined || String(value).length === 0) return fallback;
            const parsed = parseInt(value, 10);
            return isNaN(parsed) ? fallback : parsed;
        } catch (e) {
            console.warn('[Campaign] read storage failed: ' + key, e);
            return fallback;
        }
    }, key, defaultValue);
#else
    return ax::UserDefault::getInstance()->getIntegerForKey(key, defaultValue);
#endif
}

void writeStoredInt(const char* key, int value)
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_WASM
    EM_ASM({
        const key = UTF8ToString($0);
        const value = $1;
        try {
            if (typeof wx !== 'undefined' && typeof wx.setStorageSync === 'function') {
                wx.setStorageSync(key, value);
            } else if (typeof localStorage !== 'undefined') {
                localStorage.setItem(key, String(value));
            }
        } catch (e) {
            console.warn('[Campaign] write storage failed: ' + key, e);
        }
    }, key, value);
#else
    ax::UserDefault::getInstance()->setIntegerForKey(key, value);
#endif
}

std::string upgradeStorageKey(const char* key)
{
    return std::string("cg.save.upgrade.") + key;
}

std::string skillStorageKey(const char* key)
{
    return std::string("cg.save.skill.") + key;
}

UpgradeId upgradeIdFromKey(const char* key)
{
    for (size_t i = 0; i < UPGRADE_KEYS.size(); ++i)
    {
        if (std::string(key) == UPGRADE_KEYS[i])
        {
            return static_cast<UpgradeId>(i);
        }
    }
    return UpgradeId::Count;
}

SkillId skillIdFromKey(const char* key)
{
    for (size_t i = 0; i < SKILL_KEYS.size(); ++i)
    {
        if (std::string(key) == SKILL_KEYS[i])
        {
            return static_cast<SkillId>(i);
        }
    }
    return SkillId::Count;
}

Currency currencyFromString(const char* value)
{
    return std::string(value) == "crystals" ? Currency::Crystals : Currency::Coins;
}

bool validUpgrade(const UpgradeDefinition& definition)
{
    if (definition.key.empty() || definition.name.empty() || definition.costs.empty() ||
        definition.values.size() != definition.costs.size() + 1)
    {
        return false;
    }
    return std::all_of(definition.costs.begin(), definition.costs.end(), [](int cost) { return cost > 0; }) &&
           std::all_of(definition.values.begin(), definition.values.end(), [](float value) {
               return std::isfinite(value) && value > 0.0f;
           });
}
}  // namespace

EconomyModel::EconomyModel(int totalCoins, int totalCrystals)
    : _totalCoins(std::max(0, totalCoins)), _totalCrystals(std::max(0, totalCrystals))
{}

void EconomyModel::startRun()
{
    _runKills        = 0;
    _runCoins        = 0;
    _checkpointKills = 0;
    _checkpointCoins = 0;
    _runActive       = true;
}

bool EconomyModel::recordKill()
{
    if (!_runActive)
    {
        return false;
    }
    _runKills = addClamped(_runKills, 1);
    _runCoins = addClamped(_runCoins, COINS_PER_KILL);
    return true;
}

void EconomyModel::checkpointRun()
{
    if (_runActive)
    {
        _checkpointKills = _runKills;
        _checkpointCoins = _runCoins;
    }
}

void EconomyModel::rewindToCheckpoint()
{
    if (_runActive)
    {
        _runKills = _checkpointKills;
        _runCoins = _checkpointCoins;
    }
}

SettlementSummary EconomyModel::settle(bool victory, int clearCrystalReward)
{
    SettlementSummary result;
    result.victory       = victory;
    result.totalCoins    = _totalCoins;
    result.totalCrystals = _totalCrystals;
    if (!_runActive)
    {
        return result;
    }

    result.committed       = true;
    result.kills           = _runKills;
    result.coinsEarned     = _runCoins;
    result.crystalsEarned  = victory ? std::max(0, clearCrystalReward) : 0;
    _totalCoins            = addClamped(_totalCoins, result.coinsEarned);
    _totalCrystals         = addClamped(_totalCrystals, result.crystalsEarned);
    result.totalCoins      = _totalCoins;
    result.totalCrystals   = _totalCrystals;
    _runActive             = false;
    return result;
}

SettlementSummary EconomyModel::discardRun(bool victory)
{
    SettlementSummary result;
    result.victory       = victory;
    result.totalCoins    = _totalCoins;
    result.totalCrystals = _totalCrystals;
    if (!_runActive)
    {
        return result;
    }

    result.committed = true;
    result.kills     = _runKills;
    _runKills        = 0;
    _runCoins        = 0;
    _checkpointKills = 0;
    _checkpointCoins = 0;
    _runActive       = false;
    return result;
}

bool EconomyModel::spendCoins(int amount)
{
    if (amount <= 0 || _totalCoins < amount)
    {
        return false;
    }
    _totalCoins -= amount;
    return true;
}

bool EconomyModel::spendCrystals(int amount)
{
    if (amount <= 0 || _totalCrystals < amount)
    {
        return false;
    }
    _totalCrystals -= amount;
    return true;
}

void EconomyModel::grantCoins(int amount)
{
    _totalCoins = addClamped(_totalCoins, std::max(0, amount));
}

void EconomyModel::grantCrystals(int amount)
{
    _totalCrystals = addClamped(_totalCrystals, std::max(0, amount));
}

#if CG_DEBUG_TOOLS
void EconomyModel::debugSetBalances(int coins, int crystals)
{
    _totalCoins    = std::max(0, coins);
    _totalCrystals = std::max(0, crystals);
}
#endif

int ProgressionModel::upgradeLevel(UpgradeId id) const
{
    const auto index = static_cast<size_t>(id);
    return index < _levels.size() ? _levels[index] : 0;
}

bool ProgressionModel::isSkillUnlocked(SkillId id) const
{
    const auto index = static_cast<size_t>(id);
    return index < _unlocked.size() && _unlocked[index];
}

void ProgressionModel::setUpgradeLevel(UpgradeId id, int level)
{
    const auto index = static_cast<size_t>(id);
    if (index < _levels.size())
    {
        _levels[index] = std::max(0, level);
    }
}

void ProgressionModel::setSkillUnlocked(SkillId id, bool unlocked)
{
    const auto index = static_cast<size_t>(id);
    if (index < _unlocked.size())
    {
        _unlocked[index] = unlocked;
    }
}

PurchaseResult ProgressionModel::purchaseUpgrade(EconomyModel& economy,
                                                 const UpgradeDefinition& definition)
{
    PurchaseResult result;
    result.currency = definition.currency;
    if (!validUpgrade(definition) || definition.id == UpgradeId::Count)
    {
        return result;
    }

    const int level = upgradeLevel(definition.id);
    if (level >= definition.maxLevel())
    {
        result.status = PurchaseStatus::MaxLevel;
        return result;
    }

    result.cost = definition.costs[level];
    const bool paid = definition.currency == Currency::Coins ? economy.spendCoins(result.cost)
                                                              : economy.spendCrystals(result.cost);
    if (!paid)
    {
        result.status = PurchaseStatus::InsufficientFunds;
        return result;
    }

    setUpgradeLevel(definition.id, level + 1);
    result.status = PurchaseStatus::Purchased;
    return result;
}

PurchaseResult ProgressionModel::unlockSkill(EconomyModel& economy, const SkillDefinition& definition)
{
    PurchaseResult result;
    result.currency = Currency::Crystals;
    result.cost     = definition.unlockCost;
    if (definition.id == SkillId::Count || definition.key.empty() || definition.unlockCost <= 0)
    {
        return result;
    }
    if (isSkillUnlocked(definition.id))
    {
        result.status = PurchaseStatus::AlreadyUnlocked;
        return result;
    }
    if (!economy.spendCrystals(definition.unlockCost))
    {
        result.status = PurchaseStatus::InsufficientFunds;
        return result;
    }
    setSkillUnlocked(definition.id, true);
    result.status = PurchaseStatus::Purchased;
    return result;
}

bool ProgressionModel::grantUpgradeLevel(const UpgradeDefinition& definition)
{
    if (!validUpgrade(definition) || definition.id == UpgradeId::Count)
    {
        return false;
    }
    const int level = upgradeLevel(definition.id);
    if (level >= definition.maxLevel())
    {
        return false;
    }
    setUpgradeLevel(definition.id, level + 1);
    return true;
}

std::vector<SkillId> ProgressionModel::unlockSkillsForStage(
    int stage, const std::array<SkillDefinition, static_cast<size_t>(SkillId::Count)>& definitions)
{
    std::vector<SkillId> result;
    for (const auto& definition : definitions)
    {
        if (definition.unlockStage > 0 && stage >= definition.unlockStage &&
            !isSkillUnlocked(definition.id))
        {
            setSkillUnlocked(definition.id, true);
            result.push_back(definition.id);
        }
    }
    return result;
}

CampaignProgressionModel::CampaignProgressionModel(int highestClearedStage, int highestRewardedStage)
{
    setHighestClearedStage(highestClearedStage);
    setHighestRewardedStage(highestRewardedStage);
}

int CampaignProgressionModel::pendingRewardStage() const
{
    return _highestRewardedStage < _highestClearedStage ? _highestRewardedStage + 1 : 0;
}

int CampaignProgressionModel::nextStage(int stageCount) const
{
    if (stageCount <= 0)
    {
        return 1;
    }
    return std::clamp(_highestClearedStage + 1, 1, stageCount);
}

bool CampaignProgressionModel::completeStage(int stage, int stageCount)
{
    if (stageCount <= 0 || stage < 1 || stage > stageCount || stage != _highestClearedStage + 1)
    {
        return false;
    }
    _highestClearedStage = stage;
    return true;
}

bool CampaignProgressionModel::claimRewardStage(int stage)
{
    if (stage < 1 || stage > _highestClearedStage || stage != _highestRewardedStage + 1)
    {
        return false;
    }
    _highestRewardedStage = stage;
    return true;
}

void CampaignProgressionModel::setHighestClearedStage(int stage)
{
    _highestClearedStage = std::max(0, stage);
    _highestRewardedStage = std::min(_highestRewardedStage, _highestClearedStage);
}

void CampaignProgressionModel::setHighestRewardedStage(int stage)
{
    _highestRewardedStage = std::clamp(stage, 0, _highestClearedStage);
}

GameData& GameData::getInstance()
{
    static GameData instance;
    return instance;
}

GameData::GameData()
{
    loadCatalog();
    load();
}

void GameData::loadCatalog()
{
    _upgrades = {
        UpgradeDefinition{UpgradeId::ArrowDamage, "arrow_damage", "强弓", "每支箭造成更多伤害", Currency::Coins,
                          {8, 18, 34, 58, 90}, {1, 2, 3, 4, 5, 6}},
        UpgradeDefinition{UpgradeId::FireRate, "fire_rate", "疾射", "缩短两次射击的间隔", Currency::Coins,
                          {10, 22, 40, 65, 100},
                          {1.0f, 0.9166667f, 0.8333333f, 0.7611111f, 0.6944444f, 0.6388889f}},
        UpgradeDefinition{UpgradeId::MultiShot, "multi_shot", "多重箭", "一次射出更多箭矢", Currency::Coins,
                          {45, 120}, {1, 2, 3}},
        UpgradeDefinition{UpgradeId::WallHealth, "wall_health", "坚固城墙", "提高每局城墙生命上限", Currency::Coins,
                          {12, 26, 45, 70, 105}, {100, 120, 140, 160, 180, 200}},
    };
    _skills = {
        SkillDefinition{SkillId::Fireball, "fireball", "火球", "范围爆炸伤害", 0, false, 3},
        SkillDefinition{SkillId::Freeze, "freeze", "冰冻", "范围减速控制", 0, false, 4},
        SkillDefinition{SkillId::ArrowRain, "arrow_rain", "箭雨", "持续覆盖一片区域", 0, false, 5},
        SkillDefinition{SkillId::Lightning, "lightning", "闪电", "点杀主目标并麻痹周围敌人", 5, false, 0},
    };

    const std::string text = ax::FileUtils::getInstance()->getStringFromFile(CATALOG_PATH);
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (text.empty() || doc.HasParseError() || !doc.IsObject())
    {
        AXLOGW("[Progression] config invalid, using built-in catalog: {}", CATALOG_PATH);
        return;
    }

    if (doc.HasMember("upgrades") && doc["upgrades"].IsArray())
    {
        for (const auto& item : doc["upgrades"].GetArray())
        {
            if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsString())
            {
                continue;
            }
            const UpgradeId id = upgradeIdFromKey(item["id"].GetString());
            if (id == UpgradeId::Count)
            {
                continue;
            }
            UpgradeDefinition parsed;
            parsed.id  = id;
            parsed.key = item["id"].GetString();
            if (!item.HasMember("name") || !item["name"].IsString() ||
                !item.HasMember("description") || !item["description"].IsString() ||
                !item.HasMember("currency") || !item["currency"].IsString() ||
                !item.HasMember("costs") || !item["costs"].IsArray() ||
                !item.HasMember("values") || !item["values"].IsArray())
            {
                AXLOGW("[Progression] incomplete upgrade '{}', keeping fallback", parsed.key);
                continue;
            }
            parsed.name        = item["name"].GetString();
            parsed.description = item["description"].GetString();
            parsed.currency    = currencyFromString(item["currency"].GetString());
            for (const auto& cost : item["costs"].GetArray())
            {
                if (cost.IsInt()) parsed.costs.push_back(cost.GetInt());
            }
            for (const auto& value : item["values"].GetArray())
            {
                if (value.IsNumber()) parsed.values.push_back(value.GetFloat());
            }
            if (validUpgrade(parsed))
            {
                _upgrades[static_cast<size_t>(id)] = std::move(parsed);
            }
            else
            {
                AXLOGW("[Progression] invalid upgrade '{}', keeping fallback", item["id"].GetString());
            }
        }
    }

    if (doc.HasMember("skills") && doc["skills"].IsArray())
    {
        for (const auto& item : doc["skills"].GetArray())
        {
            if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsString())
            {
                continue;
            }
            const SkillId id = skillIdFromKey(item["id"].GetString());
            if (id == SkillId::Count || !item.HasMember("name") || !item["name"].IsString() ||
                !item.HasMember("description") || !item["description"].IsString() ||
                !item.HasMember("unlockCost") || !item["unlockCost"].IsInt() ||
                !item.HasMember("defaultUnlocked") || !item["defaultUnlocked"].IsBool())
            {
                continue;
            }
            const int unlockStage = item.HasMember("unlockStage") && item["unlockStage"].IsInt()
                                        ? std::max(0, item["unlockStage"].GetInt())
                                        : 0;
            _skills[static_cast<size_t>(id)] = SkillDefinition{
                id, item["id"].GetString(), item["name"].GetString(), item["description"].GetString(),
                item["unlockCost"].GetInt(), item["defaultUnlocked"].GetBool(), unlockStage};
        }
    }
    AXLOGI("[Progression] catalog loaded: {} upgrades, {} skills", _upgrades.size(), _skills.size());
}

void GameData::load()
{
    const int version  = readStoredInt(KEY_SAVE_VERSION, 0);
    const int coins    = version >= 1 ? readStoredInt(KEY_COINS, 0) : 0;
    const int crystals = version >= 1 ? readStoredInt(KEY_CRYSTALS, 0) : 0;
    _economy = EconomyModel(coins, crystals);
    const int highestCleared = version >= 3 ? readStoredInt(KEY_HIGHEST_CLEARED_STAGE, 0) : 0;
    // v4 之前通关的玩家已经拿过旧版固定奖励，迁移时不补发三选一。
    const int highestRewarded = version >= 4 ? readStoredInt(KEY_HIGHEST_REWARDED_STAGE, 0)
                                             : highestCleared;
    _campaign = CampaignProgressionModel(highestCleared, highestRewarded);
    _progression = ProgressionModel();
    for (const auto& definition : _upgrades)
    {
        const std::string key = upgradeStorageKey(definition.key.c_str());
        const int stored      = version >= 2 ? readStoredInt(key.c_str(), 0) : 0;
        _progression.setUpgradeLevel(definition.id, std::clamp(stored, 0, definition.maxLevel()));
    }
    for (const auto& definition : _skills)
    {
        const std::string key = skillStorageKey(definition.key.c_str());
        const bool unlocked   = version >= 2 ? readStoredInt(key.c_str(), definition.defaultUnlocked ? 1 : 0) != 0
                                             : definition.defaultUnlocked;
        _progression.setSkillUnlocked(definition.id, unlocked);
    }
    AXLOGI("[Campaign] save loaded: version={}, highestClearedStage={}, highestRewardedStage={}, "
           "coins={}, crystals={}", version, _campaign.highestClearedStage(),
           _campaign.highestRewardedStage(), _economy.totalCoins(), _economy.totalCrystals());
}

void GameData::save() const
{
    writeStoredInt(KEY_SAVE_VERSION, SAVE_VERSION);
    writeStoredInt(KEY_COINS, _economy.totalCoins());
    writeStoredInt(KEY_CRYSTALS, _economy.totalCrystals());
    writeStoredInt(KEY_HIGHEST_CLEARED_STAGE, _campaign.highestClearedStage());
    writeStoredInt(KEY_HIGHEST_REWARDED_STAGE, _campaign.highestRewardedStage());
    for (const auto& definition : _upgrades)
    {
        const std::string key = upgradeStorageKey(definition.key.c_str());
        writeStoredInt(key.c_str(), _progression.upgradeLevel(definition.id));
    }
    for (const auto& definition : _skills)
    {
        const std::string key = skillStorageKey(definition.key.c_str());
        writeStoredInt(key.c_str(), _progression.isSkillUnlocked(definition.id) ? 1 : 0);
    }
#if AX_TARGET_PLATFORM != AX_PLATFORM_WASM
    ax::UserDefault::getInstance()->flush();
#endif
    AXLOGI("[Campaign] save committed: highestClearedStage={}, highestRewardedStage={}, coins={}, "
           "crystals={}", _campaign.highestClearedStage(), _campaign.highestRewardedStage(),
           _economy.totalCoins(), _economy.totalCrystals());
}

const UpgradeDefinition& GameData::upgradeDefinition(UpgradeId id) const
{
    const size_t index = std::min(static_cast<size_t>(id), _upgrades.size() - 1);
    return _upgrades[index];
}

const SkillDefinition& GameData::skillDefinition(SkillId id) const
{
    const size_t index = std::min(static_cast<size_t>(id), _skills.size() - 1);
    return _skills[index];
}

int GameData::upgradeLevel(UpgradeId id) const
{
    return _progression.upgradeLevel(id);
}

bool GameData::isSkillUnlocked(SkillId id) const
{
    return _progression.isSkillUnlocked(id);
}

PurchaseResult GameData::purchaseUpgrade(UpgradeId id)
{
    auto result = _progression.purchaseUpgrade(_economy, upgradeDefinition(id));
    if (result.purchased())
    {
        save();
        AXLOGI("[Progression] upgrade purchased: key={}, level={}, cost={}", upgradeDefinition(id).key,
               upgradeLevel(id), result.cost);
    }
    return result;
}

PurchaseResult GameData::unlockSkill(SkillId id)
{
    auto result = _progression.unlockSkill(_economy, skillDefinition(id));
    if (result.purchased())
    {
        save();
        AXLOGI("[Progression] skill unlocked: key={}, cost={}", skillDefinition(id).key, result.cost);
    }
    return result;
}

CampaignRewardContext GameData::campaignRewardContext(int stage) const
{
    CampaignRewardContext context;
    context.stage = std::max(1, stage);
    for (size_t i = 0; i < context.canUpgrade.size(); ++i)
    {
        const auto id         = static_cast<UpgradeId>(i);
        context.canUpgrade[i] = upgradeLevel(id) < upgradeDefinition(id).maxLevel();
    }
    return context;
}

bool GameData::claimCampaignReward(int stage, CampaignRewardId id)
{
    if (stage != _campaign.pendingRewardStage())
    {
        AXLOGW("[GameModes][CampaignReward] claim rejected: stage={}, pending={}", stage,
               _campaign.pendingRewardStage());
        return false;
    }

    const auto grant = CampaignRewardModel::grant(id, stage);
    if (grant.upgradeIndex >= 0)
    {
        const auto upgrade = static_cast<UpgradeId>(grant.upgradeIndex);
        if (upgrade >= UpgradeId::Count || !_progression.grantUpgradeLevel(upgradeDefinition(upgrade)))
        {
            AXLOGW("[GameModes][CampaignReward] upgrade claim rejected: stage={}, reward={}", stage,
                   CampaignRewardModel::definition(id).key);
            return false;
        }
    }
    _economy.grantCoins(grant.coins);
    _economy.grantCrystals(grant.crystals);
    if (!_campaign.claimRewardStage(stage))
    {
        return false;
    }
    save();
    AXLOGI("[GameModes][CampaignReward] claimed: stage={}, reward={}, coins={}, crystals={}, upgradeIndex={}",
           stage, CampaignRewardModel::definition(id).key, grant.coins, grant.crystals,
           grant.upgradeIndex);
    return true;
}

std::vector<SkillId> GameData::beginStage(int stage)
{
    auto unlocked = _progression.unlockSkillsForStage(stage, _skills);
    if (!unlocked.empty())
    {
        save();
        for (const auto id : unlocked)
        {
            AXLOGI("[Campaign] stage milestone unlocked skill: stage={}, key={}", stage,
                   skillDefinition(id).key);
        }
    }
    return unlocked;
}

float GameData::upgradeValue(UpgradeId id) const
{
    const auto& definition = upgradeDefinition(id);
    const int level        = std::clamp(upgradeLevel(id), 0, static_cast<int>(definition.values.size()) - 1);
    return definition.values[level];
}

int GameData::arrowDamage() const
{
    return static_cast<int>(std::lround(upgradeValue(UpgradeId::ArrowDamage)));
}

float GameData::fireIntervalMultiplier() const
{
    return upgradeValue(UpgradeId::FireRate);
}

int GameData::multiShotCount() const
{
    return static_cast<int>(std::lround(upgradeValue(UpgradeId::MultiShot)));
}

int GameData::wallMaxHealth() const
{
    return static_cast<int>(std::lround(upgradeValue(UpgradeId::WallHealth)));
}

#if CG_DEBUG_TOOLS
DebugPlayerData GameData::debugPlayerData() const
{
    DebugPlayerData result;
    result.coins               = _economy.totalCoins();
    result.crystals            = _economy.totalCrystals();
    result.highestClearedStage = _campaign.highestClearedStage();
    result.highestRewardedStage = _campaign.highestRewardedStage();
    for (size_t i = 0; i < result.upgradeLevels.size(); ++i)
    {
        result.upgradeLevels[i] = _progression.upgradeLevel(static_cast<UpgradeId>(i));
    }
    for (size_t i = 0; i < result.skillUnlocked.size(); ++i)
    {
        result.skillUnlocked[i] = _progression.isSkillUnlocked(static_cast<SkillId>(i));
    }
    return result;
}

void GameData::debugApplyPlayerData(const DebugPlayerData& data)
{
    _economy.debugSetBalances(data.coins, data.crystals);
    _campaign.setHighestClearedStage(data.highestClearedStage);
    _campaign.setHighestRewardedStage(data.highestRewardedStage);
    for (size_t i = 0; i < data.upgradeLevels.size(); ++i)
    {
        const auto id         = static_cast<UpgradeId>(i);
        const int maxLevel    = upgradeDefinition(id).maxLevel();
        const int safeLevel   = std::clamp(data.upgradeLevels[i], 0, maxLevel);
        _progression.setUpgradeLevel(id, safeLevel);
    }
    for (size_t i = 0; i < data.skillUnlocked.size(); ++i)
    {
        _progression.setSkillUnlocked(static_cast<SkillId>(i), data.skillUnlocked[i]);
    }
    save();
    AXLOGI("[Sandbox] player data applied: coins={}, crystals={}, highestClearedStage={}, "
           "highestRewardedStage={}, arrowDamage={}, multiShot={}, wallHP={}",
           _economy.totalCoins(), _economy.totalCrystals(), _campaign.highestClearedStage(),
           _campaign.highestRewardedStage(), arrowDamage(), multiShotCount(), wallMaxHealth());
}

void GameData::debugResetPlayerData()
{
    DebugPlayerData defaults;
    for (size_t i = 0; i < defaults.skillUnlocked.size(); ++i)
    {
        defaults.skillUnlocked[i] = skillDefinition(static_cast<SkillId>(i)).defaultUnlocked;
    }
    debugApplyPlayerData(defaults);
    AXLOGI("[Sandbox] player data reset to catalog defaults");
}
#endif

void GameData::startRun()
{
    _economy.startRun();
    AXLOGI("[Campaign][Economy] run started: bankCoins={}, bankCrystals={}", _economy.totalCoins(),
           _economy.totalCrystals());
}

void GameData::abandonRun()
{
    const auto result = _economy.settle(false, 0);
    if (result.committed)
    {
        save();
        AXLOGI("[CampaignCity][Economy] run abandoned: coinsBanked={}, totalCoins={}, crystals={}",
               result.coinsEarned, result.totalCoins, result.totalCrystals);
    }
}

bool GameData::recordKill()
{
    return _economy.recordKill();
}

void GameData::checkpointRun()
{
    _economy.checkpointRun();
}

void GameData::rewindToCheckpoint()
{
    _economy.rewindToCheckpoint();
}

SettlementSummary GameData::settleRun(bool victory, int stage, int stageCount, int clearCrystalReward)
{
    auto result = _economy.settle(victory, clearCrystalReward);
    if (result.committed)
    {
        const bool advanced  = victory && _campaign.completeStage(stage, stageCount);
        result.stageAdvanced = advanced;
        save();
        AXLOGI("[Campaign][Economy] run settled: stage={}/{}, victory={}, advanced={}, kills={}, "
               "coinsEarned={}, crystalsEarned={}", stage, stageCount, victory, advanced, result.kills,
               result.coinsEarned, result.crystalsEarned);
    }
    return result;
}

SettlementSummary GameData::settleEndlessRun()
{
    auto result = _economy.settle(false, 0);
    if (result.committed)
    {
        save();
        AXLOGI("[GameModes][Endless][Economy] run settled: kills={}, coinsEarned={}", result.kills,
               result.coinsEarned);
    }
    return result;
}

SettlementSummary GameData::settlePracticeRun(bool victory)
{
    auto result = _economy.discardRun(victory);
    if (result.committed)
    {
        AXLOGI("[DevelopmentMenu][Core] practice run discarded: victory={}, kills={}, bankCoins={}, "
               "bankCrystals={}", victory, result.kills, result.totalCoins, result.totalCrystals);
    }
    return result;
}
