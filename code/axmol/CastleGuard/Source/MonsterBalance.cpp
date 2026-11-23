#include "MonsterBalance.h"

#include "rapidjson/document.h"

#include <algorithm>
#include <cmath>

namespace
{
bool fail(std::string* error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool readFloat(const rapidjson::Value& object, const char* key, float minimum, float maximum, float& output)
{
    auto member = object.FindMember(key);
    if (member == object.MemberEnd() || !member->value.IsNumber()) return false;
    const double number = member->value.GetDouble();
    if (!std::isfinite(number)) return false;
    output = static_cast<float>(number);
    return std::isfinite(output) && output >= minimum && output <= maximum;
}

bool readInt(const rapidjson::Value& object, const char* key, int minimum, int maximum, int& output)
{
    auto member = object.FindMember(key);
    if (member == object.MemberEnd() || !member->value.IsInt()) return false;
    output = member->value.GetInt();
    return output >= minimum && output <= maximum;
}
}  // namespace

bool MonsterBalance::loadFromJson(const std::string& json, std::string* error)
{
    rapidjson::Document document;
    document.Parse(json.c_str());
    if (json.empty() || document.HasParseError() || !document.IsObject() ||
        !document.HasMember("monsters") || !document["monsters"].IsObject())
        return fail(error, "monsters config must contain a monsters object");

    float baseSpeed = 0.0f;
    float baseBodyRadius = 0.0f;
    if (!readFloat(document, "baseSpeed", 1.0f, 500.0f, baseSpeed) ||
        !readFloat(document, "baseBodyRadius", 4.0f, 48.0f, baseBodyRadius))
        return fail(error, "monsters config baseSpeed or baseBodyRadius is invalid");

    const auto& monsters = document["monsters"];
    if (monsters.MemberCount() != TYPE_COUNT)
        return fail(error, "monsters config must contain every monster type exactly once");

    std::array<MonsterParameters, TYPE_COUNT> parsed{};
    for (size_t i = 0; i < TYPE_COUNT; ++i)
    {
        const std::string id{monsterTypeId(static_cast<MonsterType>(i))};
        auto member = monsters.FindMember(id.c_str());
        if (member == monsters.MemberEnd() || !member->value.IsObject())
            return fail(error, "missing monster type: " + id);
        const auto& value = member->value;
        MonsterParameters parameters;
        if (!readFloat(value, "maxHp", 1.0f, 999.0f, parameters.maxHp) ||
            !readFloat(value, "speedMultiplier", 0.1f, 10.0f, parameters.speedMultiplier) ||
            !readFloat(value, "bodyScale", 0.25f, 5.0f, parameters.bodyScale) ||
            !readInt(value, "actionPower", 0, 999, parameters.actionPower) ||
            !readFloat(value, "actionInterval", 0.05f, 60.0f, parameters.actionInterval) ||
            !readFloat(value, "stopRatioMin", 0.0f, 0.95f, parameters.stopRatioMin) ||
            !readFloat(value, "stopRatioMax", parameters.stopRatioMin, 0.95f, parameters.stopRatioMax) ||
            !readFloat(value, "knockbackFactor", 0.0f, 2.0f, parameters.knockbackFactor) ||
            !readInt(value, "coinDrop", 1, 100, parameters.coinDrop))
            return fail(error, "invalid monster attributes: " + id);
        parsed[i] = parameters;
    }

    _defaultBaseSpeed = baseSpeed;
    _defaultBaseBodyRadius = baseBodyRadius;
    _defaultParameters = parsed;
    resetAll();
    if (error) error->clear();
    return true;
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
    _parameters[indexOf(type)] = _defaultParameters[indexOf(type)];
}

void MonsterBalance::resetAll()
{
    _baseSpeed      = _defaultBaseSpeed;
    _baseBodyRadius = _defaultBaseBodyRadius;
    _parameters     = _defaultParameters;
}

const MonsterParameters& MonsterBalance::defaultParameters(MonsterType type) const
{
    return _defaultParameters[indexOf(type)];
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
