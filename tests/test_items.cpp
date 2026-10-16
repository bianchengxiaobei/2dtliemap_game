#include <doctest/doctest.h>

#include <string>

#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

using namespace zl;

namespace {

const std::string kValidJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "item.bandage", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "axe", "name": "消防斧", "icon": "item.axe", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

// 生成单条目/多条目测试 JSON：body 从第 4 行开始
std::string MakeJson(const std::string& body) {
    return std::string("{\n  \"format\": \"items-v1\",\n  \"items\": [\n") + body + "\n  ]\n}\n";
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("M6-T1：合法物品表加载与查询") {
    items::ItemRegistry registry;
    std::string error;
    REQUIRE(registry.LoadFromString(kValidJson, "test", &error));
    CHECK(error.empty());
    CHECK(registry.Size() == 2);

    const items::ItemDef* bandage = registry.Find("bandage");
    REQUIRE(bandage != nullptr);
    CHECK(bandage->name == "绷带");
    CHECK(bandage->icon == "item.bandage");
    CHECK(bandage->weight == doctest::Approx(0.1f));
    CHECK(bandage->maxStack == 5);
    CHECK(bandage->type == items::ItemType::Consumable);

    const items::ItemDef* axe = registry.Find("axe");
    REQUIRE(axe != nullptr);
    CHECK(axe->type == items::ItemType::Weapon);
    CHECK(axe->maxStack == 1);

    CHECK(registry.Find("no_such_item") == nullptr);
    CHECK(registry.All().size() == 2);
}

TEST_CASE("M6-T1：JSON 语法错误报行号") {
    items::ItemRegistry registry;
    std::string error;
    // 第 4 行缺少逗号
    const std::string bad =
        "{\n  \"format\": \"items-v1\",\n  \"items\": [\n    { \"id\": \"a\" \"name\": \"b\" }\n  ]\n}\n";
    CHECK_FALSE(registry.LoadFromString(bad, "test", &error));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "语法错误"));
}

TEST_CASE("M6-T1：条目字段非法时报错（含行号）") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    // 缺少 id
    CHECK_FALSE(load(R"({ "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "缺少字段 'id'"));

    // id 非法字符
    CHECK_FALSE(load(R"({ "id": "Big_Axe", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "仅允许小写字母"));

    // weight 类型错误
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": "heavy", "maxStack": 1, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "weight"));

    // weight 负值
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": -1, "maxStack": 1, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "weight 取值非法"));

    // maxStack 为 0
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 0, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "maxStack"));

    // maxStack 非整数
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1.5, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "必须是整数"));

    // type 不受支持
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "armor" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "type 不受支持"));

    // 未知字段（拼写错误）
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "wieght": 1, "maxStack": 1, "type": "generic" })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "未知字段 'wieght'"));

    // 条目不是对象
    CHECK_FALSE(load("42"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "必须是对象"));
}

TEST_CASE("M6-T1：结构错误（format/items/根节点/空表）") {
    items::ItemRegistry registry;
    std::string error;

    CHECK_FALSE(registry.LoadFromString("[1, 2]", "test", &error));
    CHECK(Contains(error, "根节点必须是对象"));

    CHECK_FALSE(registry.LoadFromString(R"({ "items": [] })", "test", &error));
    CHECK(Contains(error, "缺少 format"));

    CHECK_FALSE(registry.LoadFromString(R"({ "format": "items-v2", "items": [] })", "test", &error));
    CHECK(Contains(error, "format 不受支持"));

    CHECK_FALSE(registry.LoadFromString(R"({ "format": "items-v1" })", "test", &error));
    CHECK(Contains(error, "缺少 items"));

    CHECK_FALSE(registry.LoadFromString(R"({ "format": "items-v1", "items": {} })", "test", &error));
    CHECK(Contains(error, "items 必须是数组"));

    CHECK_FALSE(registry.LoadFromString(MakeJson(""), "test", &error));
    CHECK(Contains(error, "items 数组为空"));
}

TEST_CASE("M6-T1：重复 id 报错并给出两处行号") {
    items::ItemRegistry registry;
    std::string error;
    const std::string dup =
        "{\n  \"format\": \"items-v1\",\n  \"items\": [\n"
        "    { \"id\": \"dup\", \"name\": \"a\", \"icon\": \"i\", \"weight\": 1, \"maxStack\": 1, \"type\": \"generic\" },\n"
        "    { \"id\": \"other\", \"name\": \"b\", \"icon\": \"i\", \"weight\": 1, \"maxStack\": 1, \"type\": \"generic\" },\n"
        "    { \"id\": \"dup\", \"name\": \"c\", \"icon\": \"i\", \"weight\": 1, \"maxStack\": 1, \"type\": \"generic\" }\n"
        "  ]\n}\n";
    CHECK_FALSE(registry.LoadFromString(dup, "test", &error));
    CHECK(Contains(error, "重复"));
    CHECK(Contains(error, "第 6 行"));  // 当前（第二个）定义所在行
    CHECK(Contains(error, "第 4 行"));  // 首次定义所在行
    CHECK(registry.Size() == 0);        // 失败整体不生效
}

TEST_CASE("M6-T1：加载失败不覆盖已有数据（不部分生效）") {
    items::ItemRegistry registry;
    std::string error;
    REQUIRE(registry.LoadFromString(kValidJson, "test", &error));
    REQUIRE(registry.Size() == 2);

    CHECK_FALSE(registry.LoadFromString(MakeJson("    { \"id\": \"x\" }"), "test", &error));
    CHECK(registry.Size() == 2);
    CHECK(registry.Find("bandage") != nullptr);

    // 成功加载会整体替换
    REQUIRE(registry.LoadFromString(MakeJson(
                "    { \"id\": \"only\", \"name\": \"x\", \"icon\": \"i\", \"weight\": 1, "
                "\"maxStack\": 1, \"type\": \"generic\" }"),
            "test", &error));
    CHECK(registry.Size() == 1);
    CHECK(registry.Find("bandage") == nullptr);
    CHECK(registry.Find("only") != nullptr);
}

TEST_CASE("M6-T5：use 效果字段解析与校验") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    // 合法：heal + hunger
    REQUIRE(load(R"({ "id": "medkit", "name": "急救包", "icon": "i", "weight": 0.5, "maxStack": 2, "type": "consumable", "use": { "heal": 30, "hunger": 0.2 } })"));
    const items::ItemDef* medkit = registry.Find("medkit");
    REQUIRE(medkit != nullptr);
    CHECK(medkit->use.heal == doctest::Approx(30.0f));
    CHECK(medkit->use.hunger == doctest::Approx(0.2f));
    CHECK(medkit->use.thirst == doctest::Approx(0.0f));
    CHECK(medkit->use.Any());

    // 无 use → 不可使用
    REQUIRE(load(R"({ "id": "rock", "name": "石头", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic" })"));
    const items::ItemDef* rock = registry.Find("rock");
    REQUIRE(rock != nullptr);
    CHECK_FALSE(rock->use.Any());

    // use 非对象
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": 5 })"));
    CHECK(Contains(error, "第 4 行"));
    CHECK(Contains(error, "必须是对象"));

    // heal 越界（>100 / 负数）
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "heal": 101 } })"));
    CHECK(Contains(error, "use.heal"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "heal": -5 } })"));
    CHECK(Contains(error, "use.heal"));

    // 需求越界
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "hunger": 1.5 } })"));
    CHECK(Contains(error, "use.hunger"));

    // 未知字段
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "hp": 5 } })"));
    CHECK(Contains(error, "use 未知字段 'hp'"));

    // 类型错误
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "heal": "high" } })"));
    CHECK(Contains(error, "必须是数字"));
}

TEST_CASE("M6-T1：文件不存在与仓库内置物品文件") {
    items::ItemRegistry registry;
    std::string error;
    CHECK_FALSE(registry.LoadFile("assets/no_such_items_file.json", &error));
    CHECK(Contains(error, "不存在"));

    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    REQUIRE(registry.LoadFile(path, &error));
    CHECK(error.empty());
    CHECK(registry.Size() >= 5);
    const items::ItemDef* bandage = registry.Find("bandage");
    REQUIRE(bandage != nullptr);
    CHECK(bandage->type == items::ItemType::Consumable);
    for (const items::ItemDef& def : registry.All()) {
        CHECK_FALSE(def.id.empty());
        CHECK_FALSE(def.name.empty());
        CHECK_FALSE(def.icon.empty());
        CHECK(def.maxStack >= 1);
        CHECK(def.weight >= 0.0f);
    }
}

TEST_CASE("M6-T1：类型枚举双向映射") {
    for (const char* name : {"generic", "weapon", "consumable", "clothing", "container"}) {
        items::ItemType type = items::ItemType::Generic;
        REQUIRE(items::ParseItemType(name, &type));
        CHECK(std::string(items::ItemTypeToString(type)) == name);
    }
    items::ItemType type = items::ItemType::Generic;
    CHECK_FALSE(items::ParseItemType("armor", &type));
}
