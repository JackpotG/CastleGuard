#pragma once

#include <string>
#include <vector>

enum class MonsterType
{
    Normal,
    Fast,
    Ranged,
    Shield,
    Bomber,
    Flyer,
    Healer,
    Splitter,
    Rager,
    Ghost,
    Summoner,
    Aegis,
    Boss,
    Count,
};

struct SpawnGroupConfig
{
    MonsterType type = MonsterType::Normal;
    int count        = 0;
    float interval   = 1.0f;
    float delay      = 0.0f;
    int lane         = -1;
    std::string from = "spawn";
};

struct WaveConfig
{
    std::string name;
    float restAfter = 3.0f;
    std::vector<SpawnGroupConfig> groups;
};

struct LevelConfig
{
    std::string name;
    int clearCrystals = 0;
    std::vector<WaveConfig> waves;

    int configuredEnemyCount() const;
};

// 关卡配置的唯一解析入口。场景和自动测试都消费这里的结构，避免两套规则漂移。
class CampaignConfig
{
public:
    bool loadFromJson(const std::string& json, std::string* error = nullptr);
    void useFallback();

    const std::vector<LevelConfig>& levels() const { return _levels; }
    bool empty() const { return _levels.empty(); }

private:
    std::vector<LevelConfig> _levels;
};
