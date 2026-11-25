#include "GameData.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>

bool migrateLegacyUpgradeLayout(std::vector<UpgradeDefinition>& nodes)
{
    std::set<size_t> legacy;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].track == UpgradeTrack::Defense && nodes[i].treeRow == -1) legacy.insert(i);
    std::vector<int> rows(nodes.size(), -1), visiting(nodes.size());
    const std::function<bool(size_t)> resolve = [&](size_t index) {
        if (!legacy.count(index)) { rows[index] = nodes[index].treeRow; return true; }
        if (visiting[index]) return visiting[index] == 2;
        visiting[index] = 1;
        int row = 0;
        for (const auto requirement : upgradeRequirements(nodes[index]))
        {
            const auto parent = static_cast<size_t>(requirement.id);
            if (parent >= nodes.size() || !resolve(parent)) return false;
            row = std::max(row, rows[parent] + 1);
        }
        rows[index] = row;
        visiting[index] = 2;
        return true;
    };
    for (const auto index : legacy)
        if (!resolve(index)) return false;
    for (const auto index : legacy) nodes[index].treeRow = rows[index];
    std::vector<size_t> order(legacy.begin(), legacy.end()), placed;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return nodes[a].key < nodes[b].key; });
    for (size_t i = 0; i < nodes.size(); ++i) if (!legacy.count(i)) placed.push_back(i);
    for (const auto index : order)
    {
        auto& node = nodes[index];
        const auto count = std::count_if(nodes.begin(), nodes.end(), [&](const auto& other) {
            return other.track == node.track && other.treeRow == node.treeRow;
        });
        std::vector<float> candidates;
        if (count == 1) candidates = {.5f};
        else if (count == 2) candidates = {.27f, .73f};
        else for (int i = 0; i < count; ++i)
            candidates.push_back(std::round((.12 + i * .76 / (count - 1)) * 1000000) / 1000000);
        for (int i = 0; i <= 76; ++i) candidates.push_back(std::round((.12 + i * .01) * 1000000) / 1000000);
        const auto position = std::find_if(candidates.begin(), candidates.end(), [&](float x) {
            return std::all_of(placed.begin(), placed.end(), [&](size_t other) {
                return nodes[other].track != node.track || nodes[other].treeRow != node.treeRow ||
                    std::abs(nodes[other].treeX - x) >= .18f - 1e-6f;
            });
        });
        if (position == candidates.end()) return false;
        node.treeX = *position;
        placed.push_back(index);
    }
    return true;
}

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
        std::vector<std::vector<std::pair<std::string, int>>> parents;
        std::vector<bool> legacyParents;
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
            node.track = track == "bow" ? UpgradeTrack::Bow : track == "magic" ? UpgradeTrack::Magic : UpgradeTrack::Defense;
            node.treeRow = integer(item, "treeRow", -1);
            const bool legacyLayout = node.track == UpgradeTrack::Defense && node.treeRow == -1 && !item.HasMember("treeX");
            if (!legacyLayout && (!item.HasMember("treeX") || !item.HasMember("treeRow") || node.treeRow < 0))
                throw std::runtime_error("incomplete tree layout");
            if (item.HasMember("treeX"))
            {
                if (!item["treeX"].IsNumber()) throw std::runtime_error("invalid treeX");
                node.treeX = item["treeX"].GetFloat();
            }
            std::vector<std::pair<std::string, int>> requirements;
            const bool legacy = !item.HasMember("prerequisites");
            if (!legacy)
            {
                if (!item["prerequisites"].IsArray() || node.requiredLevel != 0 ||
                    (item.HasMember("requires") && !text(item, "requires").empty()))
                    throw std::runtime_error("invalid or mixed prerequisites");
                for (const auto& requirement : item["prerequisites"].GetArray())
                {
                    if (!requirement.IsObject()) throw std::runtime_error("invalid prerequisite");
                    requirements.emplace_back(text(requirement, "id"), integer(requirement, "level", 0));
                }
            }
            else if (item.HasMember("requires") && !text(item, "requires").empty())
                requirements.emplace_back(text(item, "requires"), node.requiredLevel);
            parents.push_back(std::move(requirements));
            legacyParents.push_back(legacy);
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
        std::vector<std::vector<std::pair<std::string, int>>> sortedParents;
        std::vector<bool> sortedLegacy;
        indices.clear();
        for (const auto index : order)
        {
            auto node = std::move(parsed[index]);
            node.id = static_cast<UpgradeId>(sorted.size());
            indices[node.key] = sorted.size();
            sortedParents.push_back(std::move(parents[index]));
            sortedLegacy.push_back(legacyParents[index]);
            sorted.push_back(std::move(node));
        }
        std::set<std::string> assigned;
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            for (const auto& [key, level] : sortedParents[i])
            {
                if (!indices.count(key)) throw std::runtime_error("missing prerequisite");
                const auto parent = static_cast<UpgradeId>(indices.at(key));
                if (sortedLegacy[i]) sorted[i].prerequisite = parent;
                else sorted[i].prerequisites.push_back({parent, level});
            }
            for (const auto& effect : sorted[i].effects)
                if (effect.operation == "set" && !assigned.insert(effect.attribute).second)
                    throw std::runtime_error("multiple base setters for one attribute");
        }
        if (!migrateLegacyUpgradeLayout(sorted) || !validUpgradeTree(sorted))
            throw std::runtime_error("invalid tree layout, costs or prerequisites");
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
