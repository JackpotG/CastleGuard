#include "MonsterBalance.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float DEFAULT_BASE_SPEED = 70.0f;
constexpr float DEFAULT_BASE_BODY_RADIUS = 15.0f;

const std::array<MonsterParameters, MonsterBalance::TYPE_COUNT> DEFAULT_PARAMETERS = {{
    // HP, speed, body, power, interval, stop min, stop max, knockback, coins
    {2, 1.00f, 1.00f, 5, 1.0f, 0.00f, 0.00f, 1.00f, 1},   // Normal
    {2, 1.80f, 0.80f, 5, 1.0f, 0.00f, 0.00f, 1.15f, 2},   // Fast
    {3, 0.90f, 1.00f, 4, 1.8f, 0.62f, 0.62f, 1.00f, 2},   // Ranged
    {5, 0.75f, 1.25f, 5, 1.0f, 0.00f, 0.00f, 0.55f, 3},   // Shield
    {2, 1.40f, 0.90f, 15, 1.0f, 0.00f, 0.00f, 1.00f, 3},  // Bomber
    {3, 1.20f, 0.85f, 3, 2.2f, 0.25f, 0.62f, 0.80f, 2},   // Flyer
    {4, 0.70f, 1.00f, 1, 2.5f, 0.45f, 0.45f, 1.00f, 4},   // Healer
    {4, 1.00f, 1.15f, 5, 1.0f, 0.00f, 0.00f, 0.85f, 3},   // Splitter
    {5, 1.10f, 1.00f, 5, 1.0f, 0.00f, 0.00f, 0.80f, 3},   // Rager
    {3, 1.00f, 1.00f, 5, 1.0f, 0.00f, 0.00f, 1.00f, 3},   // Ghost
    {6, 0.60f, 1.00f, 1, 4.0f, 0.55f, 0.55f, 0.80f, 5},   // Summoner
    {3, 1.50f, 0.90f, 5, 1.0f, 0.00f, 0.00f, 0.65f, 4},   // Aegis
    {30, 0.55f, 2.40f, 4, 3.0f, 0.40f, 0.40f, 0.20f, 15},  // Boss
}};
}  // namespace

MonsterBalance::MonsterBalance()
{
    resetAll();
}

void MonsterBalance::setBaseSpeed(float value)
{
    _baseSpeed = std::clamp(value, 1.0f, 500.0f);
}

void MonsterBalance::setBaseBodyRadius(float value)
{
    _baseBodyRadius = std::clamp(value, 4.0f, 48.0f);
}

const MonsterParameters& MonsterBalance::parameters(MonsterType type) const
{
    return _parameters[indexOf(type)];
}

void MonsterBalance::setParameters(MonsterType type, const MonsterParameters& value)
{
    _parameters[indexOf(type)] = sanitize(value);
}

float MonsterBalance::healthAtStage(MonsterType type, int stage) const
{
    const int safeStage = std::clamp(stage, 1, 1000);
    return std::min(99999.0f, parameters(type).maxHp * (1.0f + 0.20f * (safeStage - 1)));
}

float MonsterBalance::spawnSpeed(MonsterType type, float random01, float configuredSpeed) const
{
    if (configuredSpeed > 0.0f)
        return configuredSpeed;
    return _baseSpeed * parameters(type).speedMultiplier * (0.85f + random01 * 0.3f);
}

float MonsterBalance::resolveKnockbackDistance(MonsterType type, float sourceDistance) const
{
    return std::max(0.0f, sourceDistance) * parameters(type).knockbackFactor;
}

void MonsterBalance::reset(MonsterType type)
{
    _parameters[indexOf(type)] = defaultParameters(type);
}

void MonsterBalance::resetAll()
{
    _baseSpeed      = DEFAULT_BASE_SPEED;
    _baseBodyRadius = DEFAULT_BASE_BODY_RADIUS;
    _parameters     = DEFAULT_PARAMETERS;
}

float MonsterBalance::defaultBaseSpeed()
{
    return DEFAULT_BASE_SPEED;
}

float MonsterBalance::defaultBaseBodyRadius()
{
    return DEFAULT_BASE_BODY_RADIUS;
}

const MonsterParameters& MonsterBalance::defaultParameters(MonsterType type)
{
    return DEFAULT_PARAMETERS[indexOf(type)];
}

size_t MonsterBalance::indexOf(MonsterType type)
{
    const size_t index = static_cast<size_t>(type);
    return index < TYPE_COUNT ? index : 0;
}

MonsterParameters MonsterBalance::sanitize(const MonsterParameters& value)
{
    MonsterParameters result = value;
    result.maxHp             = std::clamp(std::isfinite(result.maxHp) ? result.maxHp : 1.0f,
                                          1.0f, 999.0f);
    result.coinDrop          = std::clamp(result.coinDrop, 1, 100);
    result.speedMultiplier   = std::clamp(result.speedMultiplier, 0.1f, 10.0f);
    result.bodyScale         = std::clamp(result.bodyScale, 0.25f, 5.0f);
    result.actionPower       = std::clamp(result.actionPower, 0, 999);
    result.actionInterval    = std::clamp(result.actionInterval, 0.05f, 60.0f);
    result.stopRatioMin      = std::clamp(result.stopRatioMin, 0.0f, 0.95f);
    result.stopRatioMax      = std::clamp(result.stopRatioMax, result.stopRatioMin, 0.95f);
    result.knockbackFactor   = std::clamp(result.knockbackFactor, 0.0f, 2.0f);
    return result;
}
