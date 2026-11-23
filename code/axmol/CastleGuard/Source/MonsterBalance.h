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

// 怪物战斗数值的唯一来源。正式玩法读取默认值，开发者工具只通过公开接口改当前进程值。
class MonsterBalance
{
public:
    static constexpr size_t TYPE_COUNT = static_cast<size_t>(MonsterType::Count);

    MonsterBalance();

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

    static float defaultBaseSpeed();
    static float defaultBaseBodyRadius();
    static const MonsterParameters& defaultParameters(MonsterType type);

private:
    static size_t indexOf(MonsterType type);
    static MonsterParameters sanitize(const MonsterParameters& value);

    float _baseSpeed = 70.0f;
    float _baseBodyRadius = 15.0f;
    std::array<MonsterParameters, TYPE_COUNT> _parameters;
};
