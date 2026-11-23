#pragma once

#include "CampaignConfig.h"

#include <array>
#include <cstddef>

struct MonsterParameters
{
    float maxHp              = 1.0f;
    float speedMultiplier    = 1.0f;
    float bodyScale          = 1.0f;
    int actionPower          = 5;
    float actionInterval     = 1.0f;
    float stopRatioMin       = 0.0f;
    float stopRatioMax       = 0.0f;
    float knockbackFactor    = 1.0f;
    int coinDrop             = 1;
};

// 游戏从 monsters.json 加载默认值；开发者工具只修改当前进程，重置恢复配表值。
class MonsterBalance
{
public:
    static constexpr size_t TYPE_COUNT = static_cast<size_t>(MonsterType::Count);

    bool loadFromJson(const std::string& json, std::string* error = nullptr);

    float baseSpeed() const { return _baseSpeed; }
    void setBaseSpeed(float value);
    float baseBodyRadius() const { return _baseBodyRadius; }
    void setBaseBodyRadius(float value);

    const MonsterParameters& parameters(MonsterType type) const;
    void setParameters(MonsterType type, const MonsterParameters& value);
    float healthAtStage(MonsterType type, int stage) const;
    float spawnSpeed(MonsterType type, float random01, float configuredSpeed = 0.0f) const;
    float resolveKnockbackDistance(MonsterType type, float sourceDistance) const;

    void reset(MonsterType type);
    void resetAll();

    float defaultBaseSpeed() const { return _defaultBaseSpeed; }
    float defaultBaseBodyRadius() const { return _defaultBaseBodyRadius; }
    const MonsterParameters& defaultParameters(MonsterType type) const;

private:
    static size_t indexOf(MonsterType type);
    static MonsterParameters sanitize(const MonsterParameters& value);

    float _baseSpeed = 0.0f;
    float _baseBodyRadius = 0.0f;
    std::array<MonsterParameters, TYPE_COUNT> _parameters{};
    float _defaultBaseSpeed = 0.0f;
    float _defaultBaseBodyRadius = 0.0f;
    std::array<MonsterParameters, TYPE_COUNT> _defaultParameters{};
};
