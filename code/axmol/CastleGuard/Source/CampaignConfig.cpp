#include "CampaignConfig.h"

#include "rapidjson/document.h"

#include <cmath>
#include <optional>
#include <utility>

namespace
{
std::optional<MonsterType> monsterTypeFromString(const std::string& value)
{
    if (value == "normal") return MonsterType::Normal;
    if (value == "fast") return MonsterType::Fast;
    if (value == "ranged") return MonsterType::Ranged;
    if (value == "shield") return MonsterType::Shield;
    if (value == "bomber") return MonsterType::Bomber;
    if (value == "flyer") return MonsterType::Flyer;
    if (value == "healer") return MonsterType::Healer;
    if (value == "splitter") return MonsterType::Splitter;
    if (value == "rager") return MonsterType::Rager;
    if (value == "ghost") return MonsterType::Ghost;
    if (value == "summoner") return MonsterType::Summoner;
    if (value == "aegis") return MonsterType::Aegis;
    if (value == "boss") return MonsterType::Boss;
    return std::nullopt;
}

bool validSpawnSide(const std::string& value)
{
    return value == "spawn" || value == "left" || value == "right";
}

bool fail(std::string* error, const std::string& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
}  // namespace

int LevelConfig::configuredEnemyCount() const
{
    int result = 0;
    for (const auto& wave : waves)
    {
        for (const auto& group : wave.groups)
        {
            result += group.count;
        }
    }
    return result;
}

bool CampaignConfig::loadFromJson(const std::string& json, std::string* error)
{
    rapidjson::Document document;
    document.Parse(json.c_str());
    if (json.empty() || document.HasParseError() || !document.IsObject() ||
        !document.HasMember("levels") || !document["levels"].IsArray())
    {
        return fail(error, "root.levels must be an array");
    }

    std::vector<LevelConfig> parsedLevels;
    for (const auto& levelValue : document["levels"].GetArray())
    {
        if (!levelValue.IsObject() || !levelValue.HasMember("name") || !levelValue["name"].IsString() ||
            !levelValue.HasMember("clearCrystals") || !levelValue["clearCrystals"].IsInt() ||
            levelValue["clearCrystals"].GetInt() < 0 || !levelValue.HasMember("waves") ||
            !levelValue["waves"].IsArray() || levelValue["waves"].Empty())
        {
            return fail(error, "every level needs name, non-negative clearCrystals and non-empty waves");
        }

        LevelConfig level;
        level.name          = levelValue["name"].GetString();
        level.clearCrystals = levelValue["clearCrystals"].GetInt();
        for (const auto& waveValue : levelValue["waves"].GetArray())
        {
            if (!waveValue.IsObject() || !waveValue.HasMember("name") || !waveValue["name"].IsString() ||
                !waveValue.HasMember("groups") || !waveValue["groups"].IsArray() ||
                waveValue["groups"].Empty())
            {
                return fail(error, "every wave needs a name and non-empty groups");
            }

            WaveConfig wave;
            wave.name = waveValue["name"].GetString();
            if (waveValue.HasMember("restAfter"))
            {
                if (!waveValue["restAfter"].IsNumber() || waveValue["restAfter"].GetFloat() < 0.0f)
                {
                    return fail(error, "wave restAfter must be non-negative");
                }
                wave.restAfter = waveValue["restAfter"].GetFloat();
            }

            for (const auto& groupValue : waveValue["groups"].GetArray())
            {
                if (!groupValue.IsObject())
                {
                    return fail(error, "every group must be an object");
                }

                SpawnGroupConfig group;
                if (groupValue.HasMember("types"))
                {
                    if (groupValue.HasMember("type") || groupValue.HasMember("count") ||
                        !groupValue["types"].IsArray() || groupValue["types"].Empty())
                    {
                        return fail(error, "group types must be a non-empty array without type or count");
                    }
                    for (const auto& entry : groupValue["types"].GetArray())
                    {
                        if (!entry.IsString())
                            return fail(error, "group types must contain monster ids");
                        const auto type = monsterTypeFromString(entry.GetString());
                        if (!type)
                            return fail(error, "unknown monster type: " + std::string(entry.GetString()));
                        group.types.push_back(*type);
                    }
                    group.type  = group.types.front();
                    group.count = static_cast<int>(group.types.size());
                }
                else
                {
                    if (!groupValue.HasMember("type") || !groupValue["type"].IsString() ||
                        !groupValue.HasMember("count") || !groupValue["count"].IsInt() ||
                        groupValue["count"].GetInt() <= 0)
                    {
                        return fail(error, "every group needs a known type and positive count");
                    }
                    const auto type = monsterTypeFromString(groupValue["type"].GetString());
                    if (!type)
                        return fail(error, "unknown monster type: " + std::string(groupValue["type"].GetString()));
                    group.type  = *type;
                    group.count = groupValue["count"].GetInt();
                }
                if (groupValue.HasMember("interval"))
                {
                    if (!groupValue["interval"].IsNumber() || groupValue["interval"].GetFloat() <= 0.0f)
                    {
                        return fail(error, "group interval must be positive");
                    }
                    group.interval = groupValue["interval"].GetFloat();
                }
                if (groupValue.HasMember("delay"))
                {
                    if (!groupValue["delay"].IsNumber() || groupValue["delay"].GetFloat() < 0.0f)
                    {
                        return fail(error, "group delay must be non-negative");
                    }
                    group.delay = groupValue["delay"].GetFloat();
                }
                if (groupValue.HasMember("lane"))
                {
                    if (!groupValue["lane"].IsInt() || groupValue["lane"].GetInt() < -1)
                    {
                        return fail(error, "group lane must be -1 or greater");
                    }
                    group.lane = groupValue["lane"].GetInt();
                }
                if (groupValue.HasMember("from"))
                {
                    if (!groupValue["from"].IsString() || !validSpawnSide(groupValue["from"].GetString()))
                    {
                        return fail(error, "group from must be spawn, left or right");
                    }
                    group.from = groupValue["from"].GetString();
                }
                if (groupValue.HasMember("formation"))
                {
                    if (!groupValue["formation"].IsString())
                    {
                        return fail(error, "group formation must be scatter, row or column");
                    }
                    const std::string formation = groupValue["formation"].GetString();
                    if (formation == "row") group.pattern = SpawnPattern::Row;
                    else if (formation == "column") group.pattern = SpawnPattern::Column;
                    else if (formation != "scatter")
                    {
                        return fail(error, "group formation must be scatter, row or column");
                    }
                    if (group.pattern != SpawnPattern::Scatter && group.from != "spawn")
                    {
                        return fail(error, "row and column formations require spawn origin");
                    }
                }
                if (groupValue.HasMember("useConfiguredSpeed"))
                {
                    if (!groupValue["useConfiguredSpeed"].IsBool())
                        return fail(error, "group useConfiguredSpeed must be boolean");
                    group.useConfiguredSpeed = groupValue["useConfiguredSpeed"].GetBool();
                }
                if (groupValue.HasMember("configuredSpeed"))
                {
                    if (!groupValue["configuredSpeed"].IsNumber() ||
                        !std::isfinite(groupValue["configuredSpeed"].GetFloat()) ||
                        groupValue["configuredSpeed"].GetFloat() <= 0.0f)
                        return fail(error, "group configuredSpeed must be positive");
                    group.configuredSpeed = groupValue["configuredSpeed"].GetFloat();
                }
                if (group.useConfiguredSpeed && group.configuredSpeed <= 0.0f)
                    return fail(error, "group useConfiguredSpeed requires configuredSpeed");
                if (group.pattern == SpawnPattern::Row &&
                    (group.count > FORMATION_LANE_COUNT ||
                     (group.lane >= 0 && group.lane > FORMATION_LANE_COUNT - group.count)))
                {
                    return fail(error, "row formation must fit within the available lanes");
                }
                wave.groups.push_back(std::move(group));
            }
            level.waves.push_back(std::move(wave));
        }
        parsedLevels.push_back(std::move(level));
    }

    if (parsedLevels.empty())
    {
        return fail(error, "campaign must contain at least one level");
    }
    _levels = std::move(parsedLevels);
    if (error)
    {
        error->clear();
    }
    return true;
}

void CampaignConfig::useFallback()
{
    LevelConfig level;
    level.name          = "训练关";
    level.clearCrystals = 1;
    WaveConfig wave;
    wave.name = "训练波";
    wave.groups.push_back(SpawnGroupConfig{MonsterType::Normal, 6, 0.9f, 0.0f, -1, "spawn"});
    level.waves.push_back(std::move(wave));
    _levels = {std::move(level)};
}
