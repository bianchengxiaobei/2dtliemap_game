#include "items/ItemRegistry.h"

#include <algorithm>
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

        static const std::unordered_set<std::string> kAllowedKeys = {"id",       "name",
                                                                     "icon",     "weight",
                                                                     "maxStack", "type",
                                                                     "use"};
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (!kAllowedKeys.contains(it.key())) {
                return itemFail("未知字段 '" + it.key() +
                                "'（允许: id/name/icon/weight/maxStack/type/use）");
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

        // use 效果（M6-T5，可选）：heal 0~100 HP；hunger/thirst/fatigue 0~1（正值 = 减少需求）
        if (item.contains("use")) {
            const nlohmann::json& use = item["use"];
            if (!use.is_object()) {
                return itemFail("字段 'use' 必须是对象，实际: " + use.dump());
            }
            static const std::unordered_set<std::string> kUseKeys = {"heal", "hunger", "thirst",
                                                                     "fatigue"};
            for (auto it = use.begin(); it != use.end(); ++it) {
                if (!kUseKeys.contains(it.key())) {
                    return itemFail("use 未知字段 '" + it.key() +
                                    "'（允许: heal/hunger/thirst/fatigue）");
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
            def.use = UseEffect{heal, needs[0], needs[1], needs[2]};
        }

        idLines.emplace(def.id, LineOf(text, elemOff));
        byId.emplace(def.id, defs.size());
        defs.push_back(std::move(def));
    }

    m_defs = std::move(defs);
    m_byId = std::move(byId);
    ZL_INFO(kLogModule, "物品注册表已加载: {} 条定义 <- {}", m_defs.size(), source);
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
