#include "GameData.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>

std::vector<UpgradeAttribute> defaultUpgradeAttributes()
{
    return {{"arrow_damage", "箭矢伤害", 1, 1, 99999},
            {"fire_rate", "射击间隔倍率", 1, .05f, 10},
            {"multi_shot", "齐射箭数", 1, 1, 12},
            {"wall_health", "城墙生命", 100, 1, 99999},
            {"heavy_strike", "击退距离", 36, 0, 1000},
            {"precision", "暴击概率", 0, 0, 1},
            {"mana_flow", "每秒额外回蓝", 0, 0, 1000},
            {"wall_armor", "城墙护甲", 0, 0, 9999},
            {"spell_efficiency", "法术耗蓝倍率", 1, .05f, 10}};
}

bool parseUpgradeAttributes(const std::string& json, std::vector<UpgradeAttribute>& attributes)
{
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("attributes") || !doc["attributes"].IsArray()) return false;
    auto parsed = defaultUpgradeAttributes();
    if (doc["attributes"].Size() != parsed.size()) return false;
    std::set<std::string> seen;
    for (const auto& item : doc["attributes"].GetArray())
    {
        if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsString() ||
            !item.HasMember("baseValue") || !item["baseValue"].IsNumber()) return false;
        auto found = std::find_if(parsed.begin(), parsed.end(), [&](const auto& a) { return a.key == item["id"].GetString(); });
        if (found == parsed.end() || !seen.insert(found->key).second ||
            !item.HasMember("name") || !item["name"].IsString() ||
            !item.HasMember("minimum") || !item["minimum"].IsNumber() ||
            !item.HasMember("maximum") || !item["maximum"].IsNumber()) return false;
        const float lo = item["minimum"].GetFloat(), hi = item["maximum"].GetFloat();
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo < found->minimum || hi > found->maximum || lo > hi) return false;
        found->name = item["name"].GetString();
        found->minimum = lo;
        found->maximum = hi;
        const float value = item["baseValue"].GetFloat();
        if (!std::isfinite(value) || value < found->minimum || value > found->maximum) return false;
        found->baseValue = value;
    }
    attributes = std::move(parsed);
    return true;
}

bool parseUpgradeCatalog(const std::string& json, std::vector<UpgradeDefinition>& upgrades, std::string* error, const std::vector<UpgradeAttribute>& attributes)
{
    try
    {
        rapidjson::Document doc;
        doc.Parse(json.c_str());
        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("upgrades") || !doc["upgrades"].IsArray())
            throw std::runtime_error("missing upgrades");
        std::vector<UpgradeDefinition> parsed;
        std::vector<std::string> parents;
        std::unordered_map<std::string, size_t> indices;
        const auto text = [](const auto& item, const char* key) -> std::string {
            if (!item.HasMember(key) || !item[key].IsString()) throw std::runtime_error(std::string("invalid ") + key);
            return item[key].GetString();
        };
        const auto integer = [](const auto& item, const char* key, int fallback) {
            if (!item.HasMember(key)) return fallback;
            if (!item[key].IsInt()) throw std::runtime_error(std::string("invalid ") + key);
            return item[key].GetInt();
        };
        for (const auto& item : doc["upgrades"].GetArray())
        {
            if (!item.IsObject() || parsed.size() >= 4096) throw std::runtime_error("invalid node count");
            UpgradeDefinition node;
            node.key = text(item, "id");
            if (!std::regex_match(node.key, std::regex("[a-z][a-z0-9_]{0,63}")) || indices.count(node.key))
                throw std::runtime_error("invalid or duplicate node id");
            node.name = text(item, "name");
            node.description = text(item, "description");
            const auto currency = text(item, "currency");
            if (currency != "coins" && currency != "crystals") throw std::runtime_error("invalid currency");
            node.currency = currency == "coins" ? Currency::Coins : Currency::Crystals;
            if (!item.HasMember("costs") || !item["costs"].IsArray()) throw std::runtime_error("invalid costs");
            for (const auto& cost : item["costs"].GetArray())
            {
                if (!cost.IsInt()) throw std::runtime_error("invalid cost");
                node.costs.push_back(cost.GetInt());
            }
            if (item.HasMember("effects"))
            {
                if (!item["effects"].IsArray() || item["effects"].Empty()) throw std::runtime_error("missing effects");
                for (const auto& value : item["effects"].GetArray())
                {
                    if (!value.IsObject()) throw std::runtime_error("invalid effect");
                    UpgradeEffect effect;
                    effect.attribute = text(value, "attribute");
                    effect.operation = text(value, "operation");
                    if (!value.HasMember("values") || !value["values"].IsArray()) throw std::runtime_error("invalid effect values");
                    for (const auto& number : value["values"].GetArray())
                    {
                        if (!number.IsNumber()) throw std::runtime_error("invalid effect number");
                        effect.values.push_back(number.GetFloat());
                    }
                    node.effects.push_back(std::move(effect));
                }
            }
            else
            {
                UpgradeEffect effect{node.key, "set", {}};
                if (!item.HasMember("values") || !item["values"].IsArray()) throw std::runtime_error("missing effects");
                for (const auto& value : item["values"].GetArray())
                {
                    if (!value.IsNumber()) throw std::runtime_error("invalid legacy value");
                    effect.values.push_back(value.GetFloat());
                }
                node.effects.push_back(std::move(effect));
            }
            for (const auto& effect : node.effects)
            {
                auto attribute = std::find_if(attributes.begin(), attributes.end(), [&](const auto& a) { return a.key == effect.attribute; });
                if (attribute == attributes.end()) throw std::runtime_error("unknown effect attribute");
                if (effect.operation != "set" && effect.operation != "add" && effect.operation != "multiply")
                    throw std::runtime_error("unknown effect operation");
                if (effect.values.size() != node.costs.size() + 1) throw std::runtime_error("effect level mismatch");
                for (const float value : effect.values)
                    if (!std::isfinite(value) || std::abs(value) > 99999 ||
                        (effect.operation == "multiply" && value <= 0) ||
                        (effect.operation == "set" && (value < attribute->minimum || value > attribute->maximum)))
                        throw std::runtime_error("effect value out of range");
                if ((effect.operation == "add" && effect.values[0] != 0) ||
                    (effect.operation == "multiply" && effect.values[0] != 1))
                    throw std::runtime_error("level zero must be neutral");
            }
            node.values = node.effects.front().values; // Legacy callers display the first effect only.
            node.unlockStage = integer(item, "unlockStage", 0);
            node.requiredLevel = integer(item, "requiredLevel", 0);
            const auto track = item.HasMember("track") ? text(item, "track") : "defense";
            if (track != "bow" && track != "magic" && track != "defense") throw std::runtime_error("invalid track");
            node.track = track == "bow" ? UpgradeTrack::Bow : track == "magic" ? UpgradeTrack::Magic : UpgradeTrack::None;
            node.treeRow = integer(item, "treeRow", -1);
            if (item.HasMember("treeX"))
            {
                if (!item["treeX"].IsNumber()) throw std::runtime_error("invalid treeX");
                node.treeX = item["treeX"].GetFloat();
            }
            parents.push_back(item.HasMember("requires") ? text(item, "requires") : "");
            indices[node.key] = parsed.size();
            parsed.push_back(std::move(node));
        }
        // Preserve the original handles when loading old catalogues, while allowing removal and arbitrary new IDs.
        std::vector<size_t> order;
        for (const auto& attribute : attributes)
            if (indices.count(attribute.key)) order.push_back(indices.at(attribute.key));
        for (size_t i = 0; i < parsed.size(); ++i)
            if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
        std::vector<UpgradeDefinition> sorted;
        std::vector<std::string> sortedParents;
        indices.clear();
        for (const auto index : order)
        {
            auto node = std::move(parsed[index]);
            node.id = static_cast<UpgradeId>(sorted.size());
            indices[node.key] = sorted.size();
            sortedParents.push_back(parents[index]);
            sorted.push_back(std::move(node));
        }
        std::set<std::string> assigned;
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (!sortedParents[i].empty())
            {
                if (!indices.count(sortedParents[i])) throw std::runtime_error("missing prerequisite");
                sorted[i].prerequisite = static_cast<UpgradeId>(indices.at(sortedParents[i]));
            }
            for (const auto& effect : sorted[i].effects)
                if (effect.operation == "set" && !assigned.insert(effect.attribute).second)
                    throw std::runtime_error("multiple base setters for one attribute");
        }
        if (!validUpgradeTree(sorted)) throw std::runtime_error("invalid tree layout, costs or prerequisites");
        upgrades = std::move(sorted);
        return true;
    }
    catch (const std::exception& failure)
    {
        if (error) *error = failure.what();
        return false;
    }
}

float evaluateUpgradeAttribute(const UpgradeAttribute& attribute, const std::vector<UpgradeDefinition>& upgrades,
                               const ProgressionModel& progression)
{
    double base = attribute.baseValue, addition = 0, logMultiplier = 0;
    for (const auto& node : upgrades)
    {
        const int level = std::clamp(progression.upgradeLevel(node.id), 0, node.maxLevel());
        for (const auto& effect : node.effects)
        {
            if (effect.attribute != attribute.key) continue;
            const double value = effect.values[level];
            if (effect.operation == "set") base = value;
            else if (effect.operation == "add") addition += value;
            else if (effect.operation == "multiply") logMultiplier += std::log(value);
        }
    }
    // Aggregation order is independent of file order. Clamp before consumers convert to integer combat stats.
    const double result = (base + addition) * std::exp(std::clamp(logMultiplier, -700.0, 700.0));
    return static_cast<float>(std::clamp(result, static_cast<double>(attribute.minimum), static_cast<double>(attribute.maximum)));
}
