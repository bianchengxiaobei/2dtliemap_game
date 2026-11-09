#include "items/ItemRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace zl::items {
namespace {

constexpr const char* kLogModule = "items";
constexpr const char* kFormat = "items-v1";
constexpr std::size_t kMaxIdLength = 64;
constexpr double kMaxWeightKg = 1000.0;
constexpr long long kMaxStackLimit = 9999;
constexpr const char* kAllowedTypes = "generic/weapon/consumable/clothing/container";

// ---- 原文定位：轻量 JSON 扫描（只找偏移，不做完整解析）----

std::size_t SkipWs(std::string_view text, std::size_t pos) {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }
    return pos;
}

// pos 指向字符串开头引号，返回闭引号之后的位置（未闭合返回 text.size()）
std::size_t SkipString(std::string_view text, std::size_t pos) {
    ++pos;
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == '\\') {
            pos += 2;
        } else if (c == '"') {
            return pos + 1;
        } else {
            ++pos;
        }
    }
    return text.size();
}

// pos 指向任意值开头，返回该值结束之后的位置
std::size_t SkipValue(std::string_view text, std::size_t pos) {
    if (pos >= text.size()) {
        return pos;
    }
    const char c = text[pos];
    if (c == '"') {
        return SkipString(text, pos);
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        while (pos < text.size()) {
            const char ch = text[pos];
            if (ch == '"') {
                pos = SkipString(text, pos);
                continue;
            }
            if (ch == '{' || ch == '[') {
                ++depth;
            } else if (ch == '}' || ch == ']') {
                --depth;
                if (depth == 0) {
                    return pos + 1;
                }
            }
            ++pos;
        }
        return pos;
    }
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' || ch == '\r' ||
            ch == '\n') {
            break;
        }
        ++pos;
    }
    return pos;
}

// 顶层对象中键 key（含引号，如 "\"items\""）对应值的起始偏移；找不到返回 npos
std::size_t FindTopLevelValue(std::string_view text, std::string_view key) {
    std::size_t pos = SkipWs(text, 0);
    if (pos >= text.size() || text[pos] != '{') {
        return std::string_view::npos;
    }
    ++pos;
    while (true) {
        pos = SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != '"') {
            return std::string_view::npos;
        }
        const std::size_t keyStart = pos;
        pos = SkipString(text, pos);
        const std::string_view found = text.substr(keyStart, pos - keyStart);
        pos = SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
            return std::string_view::npos;
        }
        pos = SkipWs(text, pos + 1);
        if (found == key) {
            return pos;
        }
        pos = SkipValue(text, pos);
        pos = SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        return std::string_view::npos;
    }
}

// 数组中第 index 个元素的起始偏移；越界返回 npos
std::size_t FindArrayElement(std::string_view text, std::size_t arrayPos, std::size_t index) {
    std::size_t pos = SkipWs(text, arrayPos);
    if (pos >= text.size() || text[pos] != '[') {
        return std::string_view::npos;
    }
    pos = SkipWs(text, pos + 1);
    if (pos < text.size() && text[pos] == ']') {
        return std::string_view::npos;
    }
    for (std::size_t i = 0;; ++i) {
        if (i == index) {
            return pos;
        }
        pos = SkipValue(text, pos);
        pos = SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != ',') {
            return std::string_view::npos;
        }
        pos = SkipWs(text, pos + 1);
    }
}

// 字节偏移 -> 1 基行号
std::size_t LineOf(std::string_view text, std::size_t offset) {
    const std::size_t end = std::min(offset, text.size());
    std::size_t line = 1;
    for (std::size_t i = 0; i < end; ++i) {
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

// 字节偏移 -> 1 基列号
std::size_t ColumnOf(std::string_view text, std::size_t offset) {
    const std::size_t pos = std::min(offset, text.size());
    std::size_t lineStart = 0;
    if (pos > 0) {
        const std::size_t nl = text.rfind('\n', pos - 1);
        if (nl != std::string_view::npos) {
            lineStart = nl + 1;
        }
    }
    return pos - lineStart + 1;
}

}  // namespace

bool ItemRegistry::LoadFromString(std::string_view text, const std::string& source,
                                  std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [&](const std::string& where, const std::string& msg) {
        std::string full = source + ": " + where + ": " + msg;
        if (error != nullptr) {
            *error = full;
        }
        ZL_ERROR(kLogModule, "{}", full);
        return false;
    };
    auto failAt = [&](std::size_t offset, const std::string& msg) {
        std::ostringstream where;
        where << "第 " << LineOf(text, offset) << " 行 第 " << ColumnOf(text, offset) << " 列";
        return fail(where.str(), msg);
    };

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        return failAt(e.byte, std::string("JSON 语法错误 — ") + e.what());
    } catch (const std::exception& e) {
        return fail("无法定位", std::string("JSON 解析失败 — ") + e.what());
    }

    if (!doc.is_object()) {
        return failAt(SkipWs(text, 0), "根节点必须是对象");
    }
    const std::size_t formatVal = FindTopLevelValue(text, "\"format\"");
    if (!doc.contains("format")) {
        return failAt(0, std::string("缺少 format 字段（期望 \"") + kFormat + "\"）");
    }
    if (!doc["format"].is_string()) {
        return failAt(formatVal, "format 必须是字符串");
    }
    const std::string format = doc["format"].get<std::string>();
    if (format != kFormat) {
        return failAt(formatVal, "format 不受支持: \"" + format + "\"（期望 \"" + kFormat +
                                     "\"，旧版物品文件需迁移）");
    }

    const std::size_t itemsVal = FindTopLevelValue(text, "\"items\"");
    if (!doc.contains("items")) {
        return failAt(0, "缺少 items 数组");
    }
    if (!doc["items"].is_array()) {
        return failAt(itemsVal, "items 必须是数组");
    }
    if (doc["items"].empty()) {
        return failAt(itemsVal, "items 数组为空（至少需要 1 条物品定义）");
    }

    std::vector<ItemDef> defs;
    std::unordered_map<std::string, std::size_t> byId;
    std::unordered_map<std::string, std::size_t> idLines;  // id -> 首次出现行号

    const nlohmann::json& items = doc["items"];
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::size_t elemOff = FindArrayElement(text, itemsVal, i);
        const std::string indexTag = "items[" + std::to_string(i) + "]: ";
        auto itemFail = [&](const std::string& msg) {
            if (elemOff == std::string_view::npos) {
                return fail("无法定位", indexTag + msg);
            }
            return failAt(elemOff, indexTag + msg);
        };

        const nlohmann::json& item = items[i];
        if (!item.is_object()) {
            return itemFail("条目必须是对象");
        }

        static const std::unordered_set<std::string> kAllowedKeys = {
            "id", "name", "icon", "weight", "maxStack", "type", "use",
            "unlocksRecipes", "combat", "ranged", "armor", "repairEfficiency",
            "spoilDays"};
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (!kAllowedKeys.contains(it.key())) {
                return itemFail("未知字段 '" + it.key() +
                                "'（允许: id/name/icon/weight/maxStack/type/use/"
                                "unlocksRecipes/combat/ranged/armor/"
                                "repairEfficiency/spoilDays）");
            }
        }

        ItemDef def;
        if (!item.contains("id")) {
            return itemFail("缺少字段 'id'（字符串）");
        }
        if (!item["id"].is_string()) {
            return itemFail("字段 'id' 必须是字符串，实际: " + item["id"].dump());
        }
        def.id = item["id"].get<std::string>();
        if (def.id.empty() || def.id.size() > kMaxIdLength) {
            return itemFail("id 长度非法（1~64 字符）: \"" + def.id + "\"");
        }
        const bool idCharsetOk = std::all_of(def.id.begin(), def.id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        });
        if (!idCharsetOk) {
            return itemFail("id \"" + def.id + "\" 非法：仅允许小写字母/数字/下划线/点");
        }
        const auto dupIt = byId.find(def.id);
        if (dupIt != byId.end()) {
            return itemFail("id \"" + def.id + "\" 重复（首次定义于第 " +
                            std::to_string(idLines[def.id]) + " 行）");
        }

        if (!item.contains("name")) {
            return itemFail("缺少字段 'name'（非空字符串）");
        }
        if (!item["name"].is_string() || item["name"].get<std::string>().empty()) {
            return itemFail("字段 'name' 必须是非空字符串，实际: " + item["name"].dump());
        }
        def.name = item["name"].get<std::string>();

        if (!item.contains("icon")) {
            return itemFail("缺少字段 'icon'（非空字符串，逻辑资源名）");
        }
        if (!item["icon"].is_string() || item["icon"].get<std::string>().empty()) {
            return itemFail("字段 'icon' 必须是非空字符串，实际: " + item["icon"].dump());
        }
        def.icon = item["icon"].get<std::string>();

        if (!item.contains("weight")) {
            return itemFail("缺少字段 'weight'（数字，单位 kg）");
        }
        if (!item["weight"].is_number()) {
            return itemFail("字段 'weight' 必须是数字，实际: " + item["weight"].dump());
        }
        const double weight = item["weight"].get<double>();
        if (!(weight >= 0.0) || weight > kMaxWeightKg) {
            return itemFail("weight 取值非法: " + item["weight"].dump() +
                            "（允许 0 ~ 1000，单位 kg）");
        }
        def.weight = static_cast<float>(weight);

        if (!item.contains("maxStack")) {
            return itemFail("缺少字段 'maxStack'（整数，≥ 1）");
        }
        if (!item["maxStack"].is_number_integer()) {
            return itemFail("字段 'maxStack' 必须是整数，实际: " + item["maxStack"].dump());
        }
        const long long maxStack = item["maxStack"].get<long long>();
        if (maxStack < 1 || maxStack > kMaxStackLimit) {
            return itemFail("maxStack 取值非法: " + item["maxStack"].dump() +
                            "（允许 1 ~ " + std::to_string(kMaxStackLimit) + "）");
        }
        def.maxStack = static_cast<int>(maxStack);

        if (!item.contains("type")) {
            return itemFail(std::string("缺少字段 'type'（") + kAllowedTypes + "）");
        }
        if (!item["type"].is_string()) {
            return itemFail("字段 'type' 必须是字符串，实际: " + item["type"].dump());
        }
        const std::string typeText = item["type"].get<std::string>();
        if (!ParseItemType(typeText, &def.type)) {
            return itemFail("type 不受支持: \"" + typeText + "\"（允许 " + kAllowedTypes + "）");
        }

        // M15-T1 / M30-T1：可选战斗参数（仅 weapon）。子字段可单独缺省，
        // 由运行时默认值补齐；M30 新增 heavy 子对象和体力/前摇/硬直/格挡字段。
        if (item.contains("combat")) {
            if (def.type != ItemType::Weapon) {
                return itemFail("字段 'combat' 仅适用于 weapon 类型");
            }
            const nlohmann::json& combat = item["combat"];
            if (!combat.is_object()) {
                return itemFail("字段 'combat' 必须是对象，实际: " + combat.dump());
            }

            static const std::unordered_set<std::string> kBaseCombatKeys = {
                "damage", "range", "arc", "cooldown", "knockback", "stagger",
                "wear", "stamina", "windup", "poise"};
            static const std::unordered_set<std::string> kBlockCombatKeys = {
                "blockReduction", "blockStamina"};
            auto parseCombatFields = [&](const nlohmann::json& object,
                                         const std::string& path, bool allowBlock,
                                         bool allowHeavy, WeaponCombatFields* out,
                                         WeaponCombat* blockOut) -> bool {
                for (auto it = object.begin(); it != object.end(); ++it) {
                    const bool known =
                        kBaseCombatKeys.contains(it.key()) ||
                        (allowBlock && kBlockCombatKeys.contains(it.key())) ||
                        (allowHeavy && it.key() == "heavy");
                    if (!known) {
                        std::string allowed =
                            "damage/range/arc/cooldown/knockback/stagger/wear/"
                            "stamina/windup/poise";
                        if (allowBlock) {
                            allowed += "/blockReduction/blockStamina";
                        }
                        if (allowHeavy) {
                            allowed += "/heavy";
                        }
                        return itemFail(path + " 未知字段 '" + it.key() +
                                        "'（允许: " + allowed + "）");
                    }
                }
                auto readCombat = [&](const char* key, float minValue, float maxValue,
                                      std::optional<float>* member) {
                    if (!object.contains(key)) {
                        return true;
                    }
                    const nlohmann::json& value = object[key];
                    if (!value.is_number()) {
                        return itemFail(path + "." + key +
                                        " 必须是数字，实际: " + value.dump());
                    }
                    const double number = value.get<double>();
                    if (!std::isfinite(number) || number < minValue ||
                        number > maxValue) {
                        return itemFail(path + "." + key + " 取值非法: " +
                                        value.dump() + "（允许 " +
                                        std::to_string(minValue) + " ~ " +
                                        std::to_string(maxValue) + "）");
                    }
                    *member = static_cast<float>(number);
                    return true;
                };
                if (!readCombat("damage", 0.1f, 500.0f, &out->damage) ||
                    !readCombat("range", 0.1f, 10.0f, &out->range) ||
                    !readCombat("arc", 10.0f, 360.0f, &out->arcDegrees) ||
                    !readCombat("cooldown", 0.05f, 5.0f, &out->cooldownSeconds) ||
                    !readCombat("knockback", 0.0f, 20.0f, &out->knockback) ||
                    !readCombat("stagger", 0.0f, 5.0f, &out->staggerSeconds) ||
                    !readCombat("wear", 0.0f, 1.0f, &out->wearPerHit) ||
                    !readCombat("stamina", 0.0f, 100.0f, &out->staminaCost) ||
                    !readCombat("windup", 0.0f, 1.5f, &out->windupSeconds) ||
                    !readCombat("poise", 0.0f, 100.0f, &out->poiseDamage)) {
                    return false;
                }
                if (allowBlock) {
                    if (blockOut == nullptr) {
                        return itemFail(path + " 缺少格挡输出对象");
                    }
                    if (!readCombat("blockReduction", 0.0f, 0.95f,
                                    &blockOut->blockReduction) ||
                        !readCombat("blockStamina", 0.0f, 100.0f,
                                    &blockOut->blockStaminaCost)) {
                        return false;
                    }
                }
                return true;
            };

            if (!parseCombatFields(combat, "combat", true, true, &def.combat,
                                   &def.combat)) {
                return false;
            }
            if (!def.combat.Any()) {
                return itemFail("字段 'combat' 至少需要一个参数"
                                "（damage/range/arc/cooldown/knockback/stagger/wear/"
                                "stamina/windup/poise/blockReduction/blockStamina）");
            }
            if (combat.contains("heavy")) {
                if (!combat["heavy"].is_object()) {
                    return itemFail("字段 'combat.heavy' 必须是对象，实际: " +
                                    combat["heavy"].dump());
                }
                WeaponHeavyCombat heavy;
                if (!parseCombatFields(combat["heavy"], "combat.heavy", false, false,
                                       &heavy, nullptr)) {
                    return false;
                }
                if (!heavy.Any()) {
                    return itemFail("字段 'combat.heavy' 至少需要一个参数");
                }
                def.heavy = std::move(heavy);
            }
        }

        if (item.contains("ranged")) {
            if (def.type != ItemType::Weapon) {
                return itemFail("字段 'ranged' 仅适用于 weapon 类型");
            }
            const nlohmann::json& ranged = item["ranged"];
            if (!ranged.is_object()) {
                return itemFail("字段 'ranged' 必须是对象，实际: " + ranged.dump());
            }
            static const std::unordered_set<std::string> kRangedKeys = {
                "damage", "range", "spreadDegrees", "pellets",
                "cooldownSeconds", "magazine", "reloadSeconds", "ammoDefId",
                "noiseRadius", "wearPerShot", "projectileSpeed",
                "falloffStart", "falloffEnd", "falloffScale"};
            for (auto it = ranged.begin(); it != ranged.end(); ++it) {
                if (!kRangedKeys.contains(it.key())) {
                    return itemFail("ranged 未知字段 '" + it.key() +
                                    "'（允许: damage/range/spreadDegrees/pellets/"
                                    "cooldownSeconds/magazine/reloadSeconds/"
                                    "ammoDefId/noiseRadius/wearPerShot/"
                                    "projectileSpeed/falloffStart/falloffEnd/"
                                    "falloffScale）");
                }
            }
            RangedCombat parsed;
            auto readRangedNumber = [&](const char* key, float minValue,
                                        float maxValue, float* out) {
                if (!ranged.contains(key)) {
                    return itemFail(std::string("缺少字段 'ranged.") + key + "'");
                }
                const nlohmann::json& value = ranged[key];
                if (!value.is_number()) {
                    return itemFail(std::string("ranged.") + key +
                                    " 必须是数字，实际: " + value.dump());
                }
                const double number = value.get<double>();
                if (!std::isfinite(number) || number < minValue ||
                    number > maxValue) {
                    return itemFail(std::string("ranged.") + key +
                                    " 取值非法: " + value.dump() + "（允许 " +
                                    std::to_string(minValue) + " ~ " +
                                    std::to_string(maxValue) + "）");
                }
                *out = static_cast<float>(number);
                return true;
            };
            auto readRangedInt = [&](const char* key, int minValue, int maxValue,
                                     int* out) {
                if (!ranged.contains(key)) {
                    return itemFail(std::string("缺少字段 'ranged.") + key + "'");
                }
                const nlohmann::json& value = ranged[key];
                if (!value.is_number_integer()) {
                    return itemFail(std::string("ranged.") + key +
                                    " 必须是整数，实际: " + value.dump());
                }
                const long long number = value.get<long long>();
                if (number < minValue || number > maxValue) {
                    return itemFail(std::string("ranged.") + key +
                                    " 取值非法: " + value.dump() + "（允许 " +
                                    std::to_string(minValue) + " ~ " +
                                    std::to_string(maxValue) + "）");
                }
                *out = static_cast<int>(number);
                return true;
            };
            if (!readRangedNumber("damage", 0.1f, 500.0f, &parsed.damage) ||
                !readRangedNumber("range", 0.1f, 100.0f, &parsed.range) ||
                !readRangedNumber("spreadDegrees", 0.0f, 90.0f,
                                  &parsed.spreadDegrees) ||
                !readRangedInt("pellets", 1, 32, &parsed.pellets) ||
                !readRangedNumber("cooldownSeconds", 0.05f, 10.0f,
                                  &parsed.cooldownSeconds) ||
                !readRangedInt("magazine", 1, 200, &parsed.magazine) ||
                !readRangedNumber("reloadSeconds", 0.1f, 30.0f,
                                  &parsed.reloadSeconds) ||
                !readRangedNumber("noiseRadius", 0.0f, 200.0f,
                                  &parsed.noiseRadius) ||
                !readRangedNumber("wearPerShot", 0.0f, 1.0f,
                                  &parsed.wearPerShot) ||
                !readRangedNumber("projectileSpeed", 0.1f, 100.0f,
                                  &parsed.projectileSpeed) ||
                !readRangedNumber("falloffStart", 0.0f, parsed.range,
                                  &parsed.falloffStart) ||
                !readRangedNumber("falloffEnd", 0.0f, parsed.range,
                                  &parsed.falloffEnd) ||
                !readRangedNumber("falloffScale", 0.0f, 1.0f,
                                  &parsed.falloffScale)) {
                return false;
            }
            if (parsed.falloffEnd < parsed.falloffStart) {
                return itemFail("ranged.falloffEnd 必须大于或等于 falloffStart");
            }
            if (!ranged.contains("ammoDefId")) {
                return itemFail("缺少字段 'ranged.ammoDefId'");
            }
            if (!ranged["ammoDefId"].is_string()) {
                return itemFail("ranged.ammoDefId 必须是物品 id 字符串，实际: " +
                                ranged["ammoDefId"].dump());
            }
            parsed.ammoDefId = ranged["ammoDefId"].get<std::string>();
            if (parsed.ammoDefId.empty() || parsed.ammoDefId.size() > kMaxIdLength) {
                return itemFail("ranged.ammoDefId 长度非法（1~64 字符）: \"" +
                                parsed.ammoDefId + "\"");
            }
            const bool ammoIdCharsetOk =
                std::all_of(parsed.ammoDefId.begin(), parsed.ammoDefId.end(),
                            [](char c) {
                                return (c >= 'a' && c <= 'z') ||
                                       (c >= '0' && c <= '9') || c == '_' ||
                                       c == '.';
                            });
            if (!ammoIdCharsetOk) {
                return itemFail("ranged.ammoDefId \"" + parsed.ammoDefId +
                                "\" 非法：仅允许小写字母/数字/下划线/点");
            }
            def.ranged = std::move(parsed);
        }

        // M19-F4：衣物护甲参数（仅 clothing）。condition 会在运行时削弱减伤。
        if (item.contains("armor")) {
            if (def.type != ItemType::Clothing) {
                return itemFail("字段 'armor' 仅适用于 clothing 类型");
            }
            const nlohmann::json& armor = item["armor"];
            if (!armor.is_object()) {
                return itemFail("字段 'armor' 必须是对象，实际: " + armor.dump());
            }
            static const std::unordered_set<std::string> kArmorKeys = {
                "claw", "bite", "blunt", "wear", "moveScale", "insulation",
                "waterproof"};
            for (auto it = armor.begin(); it != armor.end(); ++it) {
                if (!kArmorKeys.contains(it.key())) {
                    return itemFail("armor 未知字段 '" + it.key() +
                                    "'（允许: claw/bite/blunt/wear/moveScale/"
                                    "insulation/waterproof）");
                }
            }
            auto readArmor = [&](const char* key, float minValue, float maxValue,
                                 float* out) {
                if (!armor.contains(key)) {
                    return true;
                }
                const nlohmann::json& value = armor[key];
                if (!value.is_number()) {
                    return itemFail(std::string("armor.") + key +
                                    " 必须是数字，实际: " + value.dump());
                }
                const double number = value.get<double>();
                if (!std::isfinite(number) || number < minValue || number > maxValue) {
                    return itemFail(std::string("armor.") + key + " 取值非法: " +
                                    value.dump() + "（允许 " +
                                    std::to_string(minValue) + " ~ " +
                                    std::to_string(maxValue) + "）");
                }
                *out = static_cast<float>(number);
                return true;
            };
            if (!readArmor("claw", 0.0f, 0.95f, &def.armor.clawReduction) ||
                !readArmor("bite", 0.0f, 0.95f, &def.armor.biteReduction) ||
                !readArmor("blunt", 0.0f, 0.95f, &def.armor.bluntReduction) ||
                !readArmor("wear", 0.0f, 1.0f, &def.armor.wearPerHit) ||
                !readArmor("moveScale", 0.5f, 1.0f, &def.armor.moveScale) ||
                !readArmor("insulation", 0.0f, 1.0f, &def.armor.insulation) ||
                !readArmor("waterproof", 0.0f, 1.0f, &def.armor.waterproof)) {
                return false;
            }
            if (!def.armor.Any()) {
                return itemFail("字段 'armor' 至少需要一个非零参数"
                                "（claw/bite/blunt/wear/moveScale/insulation/waterproof）");
            }
        }

        // M19-F4：维修包恢复量倍率（武器/衣物；缺省 1.0）。
        if (item.contains("repairEfficiency")) {
            if (def.type != ItemType::Weapon && def.type != ItemType::Clothing) {
                return itemFail("字段 'repairEfficiency' 仅适用于 weapon/clothing 类型");
            }
            const nlohmann::json& value = item["repairEfficiency"];
            if (!value.is_number()) {
                return itemFail("字段 'repairEfficiency' 必须是数字，实际: " +
                                value.dump());
            }
            const double number = value.get<double>();
            if (!std::isfinite(number) || number < 0.1 || number > 3.0) {
                return itemFail("repairEfficiency 取值非法: " + value.dump() +
                                "（允许 0.1 ~ 3.0）");
            }
            def.repairEfficiency = static_cast<float>(number);
        }

        // use 效果（M6-T5，可选）：heal 0~100 HP；hunger/thirst/fatigue 0~1（正值 = 减少需求）
        if (item.contains("use")) {
            const nlohmann::json& use = item["use"];
            if (!use.is_object()) {
                return itemFail("字段 'use' 必须是对象，实际: " + use.dump());
            }
            static const std::unordered_set<std::string> kUseKeys = {
                "heal", "hunger", "thirst", "fatigue", "bandage", "painRelief",
                "painReliefSeconds", "healthPenalty", "infectionCure",
                "infectionSlow", "infectionSlowSeconds", "infectionRisk",
                "extinguish",
                "extinguishWear", "replaceWith", "nutrition",
                "foodPoisonChance"};
            for (auto it = use.begin(); it != use.end(); ++it) {
                if (!kUseKeys.contains(it.key())) {
                    return itemFail("use 未知字段 '" + it.key() +
                                    "'（允许: heal/hunger/thirst/fatigue/bandage/"
                                    "painRelief/painReliefSeconds/healthPenalty/"
                                    "infectionCure/infectionSlow/"
                                    "infectionSlowSeconds/infectionRisk/extinguish/"
                                    "extinguishWear/replaceWith/nutrition/"
                                    "foodPoisonChance）");
                }
                if (it.key() == "replaceWith") {
                    if (!it->is_string()) {
                        return itemFail("use.replaceWith 必须是物品 id 字符串");
                    }
                    continue;
                }
                if (!it->is_number()) {
                    return itemFail("use." + it.key() + " 必须是数字，实际: " + it->dump());
                }
            }
            const float heal = use.value("heal", 0.0f);
            if (!(heal >= 0.0f) || heal > 100.0f) {
                return itemFail("use.heal 取值非法: " + use["heal"].dump() + "（允许 0 ~ 100 HP）");
            }
            float needs[3] = {use.value("hunger", 0.0f), use.value("thirst", 0.0f),
                              use.value("fatigue", 0.0f)};
            const char* needNames[3] = {"hunger", "thirst", "fatigue"};
            for (int ni = 0; ni < 3; ++ni) {
                if (!(needs[ni] >= 0.0f) || needs[ni] > 1.0f) {
                    return itemFail(std::string("use.") + needNames[ni] +
                                    " 取值非法: " + use[needNames[ni]].dump() +
                                    "（允许 0 ~ 1，正值 = 减少需求）");
                }
            }
            const float bandage = use.value("bandage", 0.0f);
            if (!(bandage >= 0.0f) || bandage > 1.0f) {
                return itemFail("use.bandage 取值非法: " + use["bandage"].dump() +
                                "（允许 0 ~ 1，表示伤口严重度降低比例）");
            }
            const float painRelief = use.value("painRelief", 0.0f);
            if (!(painRelief >= 0.0f) || painRelief > 1.0f) {
                return itemFail("use.painRelief 取值非法: " + use["painRelief"].dump() +
                                "（允许 0 ~ 1）");
            }
            const float painReliefSeconds = use.value("painReliefSeconds", 0.0f);
            if (!(painReliefSeconds >= 0.0f) || painReliefSeconds > 600.0f) {
                return itemFail("use.painReliefSeconds 取值非法: " +
                                use["painReliefSeconds"].dump() +
                                "（允许 0 ~ 600 秒）");
            }
            if (painRelief > 0.0f && !(painReliefSeconds > 0.0f)) {
                return itemFail("use.painReliefSeconds 必须大于 0（当 painRelief > 0）");
            }
            const float healthPenalty = use.value("healthPenalty", 0.0f);
            if (!(healthPenalty >= 0.0f) || healthPenalty > 100.0f) {
                return itemFail("use.healthPenalty 取值非法: " +
                                use["healthPenalty"].dump() +
                                "（允许 0 ~ 100 HP）");
            }
            const float infectionCure = use.value("infectionCure", 0.0f);
            if (!(infectionCure >= 0.0f) || infectionCure > 1.0f) {
                return itemFail("use.infectionCure 取值非法: " +
                                use["infectionCure"].dump() + "（允许 0 ~ 1）");
            }
            const float infectionSlow = use.value("infectionSlow", 0.0f);
            if (!(infectionSlow >= 0.0f) || infectionSlow > 1.0f) {
                return itemFail("use.infectionSlow 取值非法: " +
                                use["infectionSlow"].dump() + "（允许 0 ~ 1）");
            }
            const float infectionSlowSeconds =
                use.value("infectionSlowSeconds", 0.0f);
            if (!(infectionSlowSeconds >= 0.0f) || infectionSlowSeconds > 600.0f) {
                return itemFail("use.infectionSlowSeconds 取值非法: " +
                                use["infectionSlowSeconds"].dump() +
                                "（允许 0 ~ 600 秒）");
            }
            if (infectionSlow > 0.0f && !(infectionSlowSeconds > 0.0f)) {
                return itemFail(
                    "use.infectionSlowSeconds 必须大于 0（当 infectionSlow > 0）");
            }
            const float infectionRisk = use.value("infectionRisk", 0.0f);
            if (!(infectionRisk >= 0.0f) || infectionRisk > 1.0f ||
                !std::isfinite(infectionRisk)) {
                return itemFail("use.infectionRisk 取值非法: " +
                                use["infectionRisk"].dump() + "（允许 0 ~ 1）");
            }
            const float extinguish = use.value("extinguish", 0.0f);
            if (!(extinguish >= 0.0f) || extinguish > 1.0f) {
                return itemFail("use.extinguish 取值非法: " +
                                use["extinguish"].dump() + "（允许 0 ~ 1）");
            }
            const float extinguishWear = use.value("extinguishWear", 0.0f);
            if (!(extinguishWear >= 0.0f) || extinguishWear > 1.0f) {
                return itemFail("use.extinguishWear 取值非法: " +
                                use["extinguishWear"].dump() +
                                "（允许 0 ~ 1）");
            }
            if (extinguish > 0.0f && extinguishWear <= 0.0f &&
                use.value("replaceWith", std::string()).empty()) {
                return itemFail(
                    "use.extinguishWear 必须大于 0（无 replaceWith 的灭火物品）");
            }
            const float nutrition = use.value("nutrition", 0.0f);
            if (!(nutrition >= 0.0f) || nutrition > 1.0f ||
                !std::isfinite(nutrition)) {
                return itemFail("use.nutrition 取值非法: " +
                                use["nutrition"].dump() + "（允许 0 ~ 1）");
            }
            const float foodPoisonChance =
                use.value("foodPoisonChance", 0.0f);
            if (!(foodPoisonChance >= 0.0f) || foodPoisonChance > 1.0f ||
                !std::isfinite(foodPoisonChance)) {
                return itemFail("use.foodPoisonChance 取值非法: " +
                                use["foodPoisonChance"].dump() +
                                "（允许 0 ~ 1）");
            }
            const std::string replaceWith =
                use.value("replaceWith", std::string());
            if (!replaceWith.empty()) {
                if (replaceWith.size() > kMaxIdLength) {
                    return itemFail("use.replaceWith 物品 id 过长");
                }
                const bool charsetOk =
                    std::all_of(replaceWith.begin(), replaceWith.end(),
                                [](char c) {
                                    return (c >= 'a' && c <= 'z') ||
                                           (c >= '0' && c <= '9') ||
                                           c == '_' || c == '.';
                                });
                if (!charsetOk) {
                    return itemFail("use.replaceWith 物品 id 非法：仅允许小写字母/"
                                    "数字/下划线/点");
                }
            }
            def.use = UseEffect{};
            def.use.heal = heal;
            def.use.hunger = needs[0];
            def.use.thirst = needs[1];
            def.use.fatigue = needs[2];
            def.use.bandage = bandage;
            def.use.painRelief = painRelief;
            def.use.painReliefSeconds = painReliefSeconds;
            def.use.healthPenalty = healthPenalty;
            def.use.infectionCure = infectionCure;
            def.use.infectionSlow = infectionSlow;
            def.use.infectionSlowSeconds = infectionSlowSeconds;
            def.use.infectionRisk = infectionRisk;
            def.use.extinguish = extinguish;
            def.use.extinguishWear = extinguishWear;
            def.use.replaceWith = replaceWith;
            def.use.nutrition = nutrition;
            def.use.foodPoisonChance = foodPoisonChance;
        }

        if (item.contains("spoilDays")) {
            const nlohmann::json& value = item["spoilDays"];
            if (!value.is_number()) {
                return itemFail("字段 'spoilDays' 必须是数字，实际: " + value.dump());
            }
            const double days = value.get<double>();
            if (!std::isfinite(days) || days < 0.0 || days > 3650.0) {
                return itemFail("spoilDays 取值非法: " + value.dump() +
                                "（允许 0 ~ 3650 天，0 = 不腐败）");
            }
            def.spoilDays = static_cast<float>(days);
        }

        if (item.contains("unlocksRecipes")) {
            const nlohmann::json& unlocks = item["unlocksRecipes"];
            if (!unlocks.is_array() || unlocks.empty() || unlocks.size() > 32) {
                return itemFail("字段 'unlocksRecipes' 必须是非空数组（1~32 个配方 id）");
            }
            if (def.use.Any()) {
                return itemFail("字段 'unlocksRecipes' 不能与 'use' 同时使用");
            }
            std::unordered_set<std::string> recipeIds;
            for (const nlohmann::json& value : unlocks) {
                if (!value.is_string()) {
                    return itemFail("unlocksRecipes 的每一项都必须是非空字符串");
                }
                const std::string recipeId = value.get<std::string>();
                if (recipeId.empty() || recipeId.size() > kMaxIdLength) {
                    return itemFail("unlocksRecipes 中的配方 id 长度非法（1~64 字符）");
                }
                const bool recipeCharsetOk =
                    std::all_of(recipeId.begin(), recipeId.end(), [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                               c == '_' || c == '.';
                    });
                if (!recipeCharsetOk) {
                    return itemFail("unlocksRecipes 中的配方 id 非法：仅允许小写字母/数字/下划线/点");
                }
                if (!recipeIds.insert(recipeId).second) {
                    return itemFail("unlocksRecipes 中的配方 id 重复: \"" + recipeId + "\"");
                }
                def.unlocksRecipes.push_back(recipeId);
            }
        }

        idLines.emplace(def.id, LineOf(text, elemOff));
        byId.emplace(def.id, defs.size());
        defs.push_back(std::move(def));
    }

    for (const ItemDef& def : defs) {
        if (!def.use.replaceWith.empty() &&
            !byId.contains(def.use.replaceWith)) {
            if (error != nullptr) {
                *error = "use.replaceWith 引用了未注册物品 '" +
                         def.use.replaceWith + "'（物品 " + def.id + "）";
            }
            ZL_ERROR(kLogModule, "物品定义非法: use.replaceWith 引用了未注册物品 '{}'",
                     def.use.replaceWith);
            return false;
        }
        if (def.ranged.has_value() &&
            !byId.contains(def.ranged->ammoDefId)) {
            if (error != nullptr) {
                *error = "ranged.ammoDefId 引用了未注册物品 '" +
                         def.ranged->ammoDefId + "'（物品 " + def.id + "）";
            }
            ZL_ERROR(kLogModule,
                     "物品定义非法: ranged.ammoDefId 引用了未注册物品 '{}'",
                     def.ranged->ammoDefId);
            return false;
        }
    }

    m_defs = std::move(defs);
    m_byId = std::move(byId);
    m_sourceText.assign(text.data(), text.size());
    ZL_INFO(kLogModule, "物品注册表已加载: {} 条定义 <- {}", m_defs.size(), source);
    return true;
}

bool ItemRegistry::MergeFromString(std::string_view text, const std::string& source,
                                   std::size_t* added, std::string* error) {
    if (added != nullptr) {
        *added = 0;
    }
    if (error != nullptr) {
        error->clear();
    }
    auto fail = [&](const std::string& message) {
        const std::string full = source + ": " + message;
        if (error != nullptr) {
            *error = full;
        }
        ZL_ERROR(kLogModule, "{}", full);
        return false;
    };
    if (m_sourceText.empty()) {
        return fail("注册表没有可追加的基础文档");
    }

    nlohmann::json incoming;
    try {
        incoming = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        return fail(std::string("JSON 语法错误 — ") + e.what());
    } catch (const std::exception& e) {
        return fail(std::string("JSON 解析失败 — ") + e.what());
    }
    if (!incoming.is_object()) {
        return fail("根节点必须是对象");
    }
    if (!incoming.contains("format") || !incoming["format"].is_string() ||
        incoming["format"].get<std::string>() != kFormat) {
        return fail(std::string("format 必须是 ") + kFormat);
    }
    if (!incoming.contains("items") || !incoming["items"].is_array() ||
        incoming["items"].empty()) {
        return fail("items 必须是非空数组");
    }

    nlohmann::json candidate;
    try {
        candidate = nlohmann::json::parse(m_sourceText);
    } catch (const std::exception& e) {
        return fail(std::string("基础文档解析失败 — ") + e.what());
    }
    if (!candidate.is_object() || !candidate.contains("items") ||
        !candidate["items"].is_array()) {
        return fail("基础文档缺少 items 数组");
    }
    const std::size_t incomingCount = incoming["items"].size();
    for (const nlohmann::json& item : incoming["items"]) {
        candidate["items"].push_back(item);
    }
    if (!LoadFromString(candidate.dump(), source, error)) {
        return false;
    }
    if (added != nullptr) {
        *added = incomingCount;
    }
    return true;
}

bool ItemRegistry::LoadFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        const std::string msg = "物品定义文件不存在: " + path.string();
        if (error != nullptr) {
            *error = msg;
        }
        ZL_ERROR(kLogModule, "{}", msg);
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), path.string(), error);
}

const ItemDef* ItemRegistry::Find(std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    if (it == m_byId.end()) {
        return nullptr;
    }
    return &m_defs[it->second];
}

}  // namespace zl::items
