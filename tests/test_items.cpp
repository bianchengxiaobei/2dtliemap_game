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

std::string ReplaceOnce(std::string text, const std::string& from,
                        const std::string& to) {
    const auto pos = text.find(from);
    if (pos != std::string::npos) {
        text.replace(pos, from.size(), to);
    }
    return text;
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

    // M15-T2：bandage 0~1（止血/降伤口比例）
    REQUIRE(load(R"({ "id": "rag", "name": "rag", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic", "use": { "bandage": 0.35 } })"));
    const items::ItemDef* rag = registry.Find("rag");
    REQUIRE(rag != nullptr);
    CHECK(rag->use.bandage == doctest::Approx(0.35f));
    CHECK(rag->use.Any());

    // M15-T4：painRelief 0~1 + painReliefSeconds
    REQUIRE(load(R"({ "id": "painkillers", "name": "止痛药", "icon": "i", "weight": 0.05, "maxStack": 5, "type": "consumable", "use": { "painRelief": 0.65, "painReliefSeconds": 90 } })"));
    const items::ItemDef* painkillers = registry.Find("painkillers");
    REQUIRE(painkillers != nullptr);
    CHECK(painkillers->use.painRelief == doctest::Approx(0.65f));
    CHECK(painkillers->use.painReliefSeconds == doctest::Approx(90.0f));
    CHECK(painkillers->use.Any());

    // M20-F1：抗生素/消毒剂感染治疗字段
    REQUIRE(load(R"({ "id": "antibiotics", "name": "抗生素", "icon": "i", "weight": 0.05, "maxStack": 5, "type": "consumable", "use": { "infectionCure": 0.35, "infectionSlow": 0.5, "infectionSlowSeconds": 180 } })"));
    const items::ItemDef* antibiotics = registry.Find("antibiotics");
    REQUIRE(antibiotics != nullptr);
    CHECK(antibiotics->use.infectionCure == doctest::Approx(0.35f));
    CHECK(antibiotics->use.infectionSlow == doctest::Approx(0.5f));
    CHECK(antibiotics->use.infectionSlowSeconds == doctest::Approx(180.0f));
    CHECK(antibiotics->use.Any());

    // F6-T2：污染绷带的伤口感染风险
    REQUIRE(load(R"({ "id": "tainted_bandage", "name": "污染绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable", "use": { "bandage": 0.35, "infectionRisk": 0.4 } })"));
    const items::ItemDef* tainted = registry.Find("tainted_bandage");
    REQUIRE(tainted != nullptr);
    CHECK(tainted->use.infectionRisk == doctest::Approx(0.4f));
    CHECK(tainted->use.Any());

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

    // bandage 越界
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "use": { "bandage": 1.5 } })"));
    CHECK(Contains(error, "use.bandage"));

    // painRelief 越界 / 缺持续时间
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "painRelief": 1.5 } })"));
    CHECK(Contains(error, "use.painRelief"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "painRelief": 0.5 } })"));
    CHECK(Contains(error, "painReliefSeconds"));

    // 感染治疗字段越界 / 缺持续时间
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "infectionCure": 1.5 } })"));
    CHECK(Contains(error, "use.infectionCure"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "infectionSlow": 0.5 } })"));
    CHECK(Contains(error, "infectionSlowSeconds"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "infectionRisk": 1.5 } })"));
    CHECK(Contains(error, "use.infectionRisk"));

    // 类型错误
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "heal": "high" } })"));
    CHECK(Contains(error, "必须是数字"));
}

TEST_CASE("M12：unlocksRecipes 配方图纸字段校验") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    REQUIRE(load(R"({ "id": "magazine", "name": "book", "icon": "i", "weight": 0.2, "maxStack": 1, "type": "generic", "unlocksRecipes": ["campfire_beans", "field_bandage"] })"));
    const items::ItemDef* magazine = registry.Find("magazine");
    REQUIRE(magazine != nullptr);
    REQUIRE(magazine->unlocksRecipes.size() == 2u);
    CHECK(magazine->unlocksRecipes[0] == "campfire_beans");
    CHECK(magazine->unlocksRecipes[1] == "field_bandage");

    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "unlocksRecipes": "campfire_beans" })"));
    CHECK(Contains(error, "unlocksRecipes"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "unlocksRecipes": [] })"));
    CHECK(Contains(error, "unlocksRecipes"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "unlocksRecipes": ["a", "a"] })"));
    CHECK(Contains(error, "重复"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "consumable", "use": { "heal": 1 }, "unlocksRecipes": ["a"] })"));
    CHECK(Contains(error, "不能与 'use'"));
}

TEST_CASE("M15-T1：weapon combat 字段解析、缺省与校验") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    REQUIRE(load(R"({ "id": "axe", "name": "axe", "icon": "i", "weight": 2, "maxStack": 1, "type": "weapon", "combat": { "damage": 65, "range": 1.9, "arc": 110, "cooldown": 0.7, "knockback": 4, "stagger": 0.45, "wear": 0.035 } })"));
    const items::ItemDef* axe = registry.Find("axe");
    REQUIRE(axe != nullptr);
    REQUIRE(axe->combat.Any());
    CHECK(axe->combat.damage == doctest::Approx(65.0f));
    CHECK(axe->combat.range == doctest::Approx(1.9f));
    CHECK(axe->combat.arcDegrees == doctest::Approx(110.0f));
    CHECK(axe->combat.cooldownSeconds == doctest::Approx(0.7f));
    CHECK(axe->combat.knockback == doctest::Approx(4.0f));
    CHECK(axe->combat.staggerSeconds == doctest::Approx(0.45f));
    CHECK(axe->combat.wearPerHit == doctest::Approx(0.035f));

    items::WeaponAttackParams fallback;
    fallback.damage = 40.0f;
    fallback.range = 1.7f;
    fallback.arcDegrees = 100.0f;
    fallback.cooldownSeconds = 0.5f;
    fallback.knockback = 3.0f;
    fallback.staggerSeconds = 0.35f;
    fallback.wearPerHit = 0.0f;
    const items::WeaponAttackParams resolved =
        items::ResolveWeaponAttack(axe->combat, fallback);
    CHECK(resolved.damage == doctest::Approx(65.0f));
    CHECK(resolved.range == doctest::Approx(1.9f));
    CHECK(resolved.cooldownSeconds == doctest::Approx(0.7f));

    // 子字段可缺省：只覆盖 damage，其余沿用 fallback
    REQUIRE(load(R"({ "id": "knife", "name": "knife", "icon": "i", "weight": 0.5, "maxStack": 1, "type": "weapon", "combat": { "damage": 12 } })"));
    const items::ItemDef* knife = registry.Find("knife");
    REQUIRE(knife != nullptr);
    CHECK_FALSE(knife->combat.range.has_value());
    const items::WeaponAttackParams partial =
        items::ResolveWeaponAttack(knife->combat, fallback);
    CHECK(partial.damage == doctest::Approx(12.0f));
    CHECK(partial.range == doctest::Approx(fallback.range));
    CHECK(partial.arcDegrees == doctest::Approx(fallback.arcDegrees));
    CHECK(partial.cooldownSeconds == doctest::Approx(fallback.cooldownSeconds));
    CHECK(partial.wearPerHit == doctest::Approx(fallback.wearPerHit));

    // 非 weapon 类型不允许 combat
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "combat": { "damage": 1 } })"));
    CHECK(Contains(error, "仅适用于 weapon"));
    // combat 必须是对象且至少一个字段
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": 5 })"));
    CHECK(Contains(error, "combat"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": {} })"));
    CHECK(Contains(error, "至少需要一个参数"));
    // 未知字段
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "dmg": 1 } })"));
    CHECK(Contains(error, "combat 未知字段 'dmg'"));
    // 类型/范围校验
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": "high" } })"));
    CHECK(Contains(error, "必须是数字"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 0 } })"));
    CHECK(Contains(error, "combat.damage"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "range": 11 } })"));
    CHECK(Contains(error, "combat.range"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "arc": 5 } })"));
    CHECK(Contains(error, "combat.arc"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "cooldown": 0 } })"));
    CHECK(Contains(error, "combat.cooldown"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "knockback": -1 } })"));
    CHECK(Contains(error, "combat.knockback"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "stagger": 6 } })"));
    CHECK(Contains(error, "combat.stagger"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "wear": 2 } })"));
    CHECK(Contains(error, "combat.wear"));
}

TEST_CASE("M30-T1：weapon 体力/前摇/硬直/格挡与 heavy 数据") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    REQUIRE(load(R"({ "id": "heavy_axe", "name": "heavy_axe", "icon": "i", "weight": 3, "maxStack": 1, "type": "weapon", "combat": { "damage": 65, "range": 1.9, "arc": 110, "cooldown": 0.7, "knockback": 4, "stagger": 0.45, "wear": 0.035, "stamina": 15, "windup": 0.10, "poise": 11, "blockReduction": 0.55, "blockStamina": 16, "heavy": { "damage": 92, "range": 2.0, "arc": 100, "cooldown": 0.95, "knockback": 5, "stagger": 0.60, "wear": 0.045, "stamina": 28, "windup": 0.38, "poise": 38 } } })"));
    const items::ItemDef* axe = registry.Find("heavy_axe");
    REQUIRE(axe != nullptr);
    REQUIRE(axe->combat.Any());
    CHECK(axe->combat.staminaCost == doctest::Approx(15.0f));
    CHECK(axe->combat.windupSeconds == doctest::Approx(0.10f));
    CHECK(axe->combat.poiseDamage == doctest::Approx(11.0f));
    CHECK(axe->combat.blockReduction == doctest::Approx(0.55f));
    CHECK(axe->combat.blockStaminaCost == doctest::Approx(16.0f));
    REQUIRE(axe->heavy.has_value());
    REQUIRE(axe->heavy->Any());
    CHECK(axe->heavy->damage == doctest::Approx(92.0f));
    CHECK(axe->heavy->staminaCost == doctest::Approx(28.0f));
    CHECK(axe->heavy->windupSeconds == doctest::Approx(0.38f));
    CHECK(axe->heavy->poiseDamage == doctest::Approx(38.0f));

    items::WeaponAttackParams fallback;
    fallback.staminaCost = 9.0f;
    fallback.windupSeconds = 0.03f;
    fallback.poiseDamage = 7.0f;
    const items::WeaponAttackParams resolved =
        items::ResolveWeaponAttack(axe->combat, fallback);
    CHECK(resolved.staminaCost == doctest::Approx(15.0f));
    CHECK(resolved.windupSeconds == doctest::Approx(0.10f));
    CHECK(resolved.poiseDamage == doctest::Approx(11.0f));

    // 新字段可单独存在，旧字段仍按 fallback 补齐。
    REQUIRE(load(R"({ "id": "quick_knife", "name": "quick_knife", "icon": "i", "weight": 0.6, "maxStack": 1, "type": "weapon", "combat": { "stamina": 6 } })"));
    const items::ItemDef* knife = registry.Find("quick_knife");
    REQUIRE(knife != nullptr);
    CHECK(knife->combat.Any());
    CHECK_FALSE(knife->heavy.has_value());
    const items::WeaponAttackParams partial =
        items::ResolveWeaponAttack(knife->combat, fallback);
    CHECK(partial.staminaCost == doctest::Approx(6.0f));
    CHECK(partial.damage == doctest::Approx(fallback.damage));
    CHECK(partial.windupSeconds == doctest::Approx(fallback.windupSeconds));

    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "stamina": -1 } })"));
    CHECK(Contains(error, "combat.stamina"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "windup": 1.6 } })"));
    CHECK(Contains(error, "combat.windup"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "poise": 101 } })"));
    CHECK(Contains(error, "combat.poise"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "blockReduction": 0.96 } })"));
    CHECK(Contains(error, "combat.blockReduction"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "blockStamina": 101 } })"));
    CHECK(Contains(error, "combat.blockStamina"));

    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "heavy": 5 } })"));
    CHECK(Contains(error, "combat.heavy"));
    CHECK(Contains(error, "必须是对象"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "heavy": {} } })"));
    CHECK(Contains(error, "至少需要一个参数"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "heavy": { "dmg": 1 } } })"));
    CHECK(Contains(error, "combat.heavy 未知字段"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "damage": 10, "heavy": { "damage": 1, "blockReduction": 0.5 } } })"));
    CHECK(Contains(error, "combat.heavy 未知字段 'blockReduction'"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "combat": { "heavy": { "damage": 1 } } })"));
    CHECK(Contains(error, "至少需要一个参数"));
}

TEST_CASE("M23-F1：远程武器 ranged 字段解析、引用与校验") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };
    const std::string ranged =
        R"({ "damage": 18, "range": 18, "spreadDegrees": 3, "pellets": 1, "cooldownSeconds": 0.45, "magazine": 8, "reloadSeconds": 2.0, "ammoDefId": "ammo_9mm", "noiseRadius": 28, "wearPerShot": 0.01, "projectileSpeed": 20, "falloffStart": 8, "falloffEnd": 18, "falloffScale": 0.55 })";
    auto weaponJson = [](const std::string& type, const std::string& body) {
        return std::string(
                   R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": ")") +
               type + R"(", "ranged": )" + body + " }";
    };
    auto loadWeapon = [&](const std::string& type, const std::string& body) {
        return load(weaponJson(type, body));
    };

    const std::string valid =
        std::string(R"(
    { "id": "ammo_9mm", "name": "9mm", "icon": "i", "weight": 0.01, "maxStack": 60, "type": "generic" },
    { "id": "pistol", "name": "手枪", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "ranged": )") +
        ranged + " }";
    REQUIRE(load(valid));
    const items::ItemDef* pistol = registry.Find("pistol");
    REQUIRE(pistol != nullptr);
    REQUIRE(pistol->ranged.has_value());
    CHECK(pistol->ranged->damage == doctest::Approx(18.0f));
    CHECK(pistol->ranged->range == doctest::Approx(18.0f));
    CHECK(pistol->ranged->spreadDegrees == doctest::Approx(3.0f));
    CHECK(pistol->ranged->pellets == 1);
    CHECK(pistol->ranged->cooldownSeconds == doctest::Approx(0.45f));
    CHECK(pistol->ranged->magazine == 8);
    CHECK(pistol->ranged->reloadSeconds == doctest::Approx(2.0f));
    CHECK(pistol->ranged->ammoDefId == "ammo_9mm");
    CHECK(pistol->ranged->noiseRadius == doctest::Approx(28.0f));
    CHECK(pistol->ranged->wearPerShot == doctest::Approx(0.01f));
    CHECK(pistol->ranged->projectileSpeed == doctest::Approx(20.0f));
    CHECK(pistol->ranged->falloffStart == doctest::Approx(8.0f));
    CHECK(pistol->ranged->falloffEnd == doctest::Approx(18.0f));
    CHECK(pistol->ranged->falloffScale == doctest::Approx(0.55f));

    REQUIRE(load(R"({ "id": "axe", "name": "axe", "icon": "i", "weight": 2, "maxStack": 1, "type": "weapon", "combat": { "damage": 50 } })"));
    REQUIRE(registry.Find("axe") != nullptr);
    CHECK_FALSE(registry.Find("axe")->ranged.has_value());

    CHECK_FALSE(loadWeapon("generic", ranged));
    CHECK(Contains(error, "仅适用于 weapon"));
    CHECK_FALSE(loadWeapon("weapon", "5"));
    CHECK(Contains(error, "必须是对象"));
    CHECK_FALSE(loadWeapon("weapon", R"({ "bad": 1 })"));
    CHECK(Contains(error, "ranged 未知字段"));
    CHECK_FALSE(loadWeapon("weapon", R"({ "damage": 18 })"));
    CHECK(Contains(error, "ranged.range"));
    CHECK_FALSE(loadWeapon(
        "weapon", ReplaceOnce(ranged, R"("damage": 18)", R"("damage": 0)")));
    CHECK(Contains(error, "ranged.damage"));
    CHECK_FALSE(loadWeapon(
        "weapon", ReplaceOnce(ranged, R"("pellets": 1)", R"("pellets": 1.5)")));
    CHECK(Contains(error, "ranged.pellets"));
    CHECK_FALSE(loadWeapon(
        "weapon", ReplaceOnce(ranged, R"("falloffEnd": 18)", R"("falloffEnd": 4)")));
    CHECK(Contains(error, "falloffEnd"));
    CHECK_FALSE(loadWeapon(
        "weapon", ReplaceOnce(ranged, "ammo_9mm", "missing_ammo")));
    CHECK(Contains(error, "未注册物品"));
}

TEST_CASE("M19-F4：衣物 armor 与 repairEfficiency 字段解析") {
    items::ItemRegistry registry;
    std::string error;
    auto load = [&](const std::string& body) {
        error.clear();
        return registry.LoadFromString(MakeJson("    " + body), "test", &error);
    };

    REQUIRE(load(R"({ "id": "vest", "name": "背心", "icon": "i", "weight": 3, "maxStack": 1, "type": "clothing", "repairEfficiency": 0.75, "armor": { "claw": 0.4, "bite": 0.2, "blunt": 0.1, "wear": 0.02, "moveScale": 0.9, "insulation": 0.5, "waterproof": 0.7 } })"));
    const items::ItemDef* vest = registry.Find("vest");
    REQUIRE(vest != nullptr);
    CHECK(vest->armor.Any());
    CHECK(vest->armor.clawReduction == doctest::Approx(0.4f));
    CHECK(vest->armor.biteReduction == doctest::Approx(0.2f));
    CHECK(vest->armor.bluntReduction == doctest::Approx(0.1f));
    CHECK(vest->armor.wearPerHit == doctest::Approx(0.02f));
    CHECK(vest->armor.moveScale == doctest::Approx(0.9f));
    CHECK(vest->armor.insulation == doctest::Approx(0.5f));
    CHECK(vest->armor.waterproof == doctest::Approx(0.7f));
    CHECK(vest->repairEfficiency == doctest::Approx(0.75f));

    const items::ArmorProtection half = items::ResolveArmor(vest->armor, 0.5f);
    CHECK(half.clawReduction == doctest::Approx(0.2f));
    CHECK(half.biteReduction == doctest::Approx(0.1f));
    CHECK(half.bluntReduction == doctest::Approx(0.05f));
    CHECK(half.moveScale == doctest::Approx(0.9f));
    CHECK(half.insulation == doctest::Approx(0.5f));

    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "armor": { "claw": 0.1 } })"));
    CHECK(Contains(error, "仅适用于 clothing"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "clothing", "armor": { "claw": 1.0 } })"));
    CHECK(Contains(error, "armor.claw"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "clothing", "armor": { "moveScale": 0.4 } })"));
    CHECK(Contains(error, "armor.moveScale"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "clothing", "armor": { "armor": 1 } })"));
    CHECK(Contains(error, "armor 未知字段"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "clothing", "armor": { "insulation": 0 } })"));
    CHECK(Contains(error, "至少需要一个非零参数"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "generic", "repairEfficiency": 1.2 })"));
    CHECK(Contains(error, "仅适用于 weapon/clothing"));
    CHECK_FALSE(load(R"({ "id": "x", "name": "x", "icon": "i", "weight": 1, "maxStack": 1, "type": "weapon", "repairEfficiency": 3.1 })"));
    CHECK(Contains(error, "repairEfficiency"));
}

TEST_CASE("M6-T1：文件不存在与仓库内置物品文件") {
    items::ItemRegistry registry;
    std::string error;
    CHECK_FALSE(registry.LoadFile("assets/no_such_items_file.json", &error));
    CHECK(Contains(error, "不存在"));

    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    REQUIRE(registry.LoadFile(path, &error));
    CHECK(error.empty());
    CHECK(registry.Size() == 94);
    const items::ItemDef* bandage = registry.Find("bandage");
    REQUIRE(bandage != nullptr);
    CHECK(bandage->type == items::ItemType::Consumable);
    CHECK(bandage->use.bandage == doctest::Approx(0.7f));
    const items::ItemDef* rag = registry.Find("rag");
    REQUIRE(rag != nullptr);
    CHECK(rag->use.bandage == doctest::Approx(0.35f));
    const items::ItemDef* painkillers = registry.Find("painkillers");
    REQUIRE(painkillers != nullptr);
    CHECK(painkillers->use.painRelief == doctest::Approx(0.65f));
    CHECK(painkillers->use.painReliefSeconds == doctest::Approx(90.0f));
    CHECK(painkillers->use.heal == doctest::Approx(0.0f));
    for (const items::ItemDef& def : registry.All()) {
        CHECK_FALSE(def.id.empty());
        CHECK_FALSE(def.name.empty());
        CHECK_FALSE(def.icon.empty());
        CHECK(def.maxStack >= 1);
        CHECK(def.weight >= 0.0f);
        if (def.type == items::ItemType::Weapon) {
            INFO("weapon id: " << def.id);
            if (!def.combat.Any() && !def.ranged.has_value()) {
                FAIL("weapon missing combat/ranged: " << def.id);
            }
            if (def.combat.Any()) {
                CHECK(def.combat.damage.has_value());
                CHECK(def.combat.range.has_value());
                CHECK(def.combat.cooldownSeconds.has_value());
                CHECK(def.combat.wearPerHit.has_value());
            }
            if (def.ranged.has_value()) {
                CHECK(def.ranged->damage > 0.0f);
                CHECK(def.ranged->range > 0.0f);
                CHECK(def.ranged->magazine >= 1);
                CHECK_FALSE(def.ranged->ammoDefId.empty());
            }
        }
    }

    // M15-T1：内置武器参数应可区分（装备切换的实际数据来源）
    const items::ItemDef* axe = registry.Find("axe");
    const items::ItemDef* knife = registry.Find("kitchen_knife");
    REQUIRE(axe != nullptr);
    REQUIRE(knife != nullptr);
    REQUIRE(axe->combat.Any());
    REQUIRE(knife->combat.Any());
    CHECK(axe->combat.damage.value() > knife->combat.damage.value());
    CHECK(axe->combat.range.value() > knife->combat.range.value());
    CHECK(knife->combat.cooldownSeconds.value() < axe->combat.cooldownSeconds.value());

    // M30-T1：代表性武器拥有轻击体力/前摇/硬直/格挡与 heavy 覆盖。
    REQUIRE(axe->heavy.has_value());
    CHECK(axe->combat.staminaCost.value() > knife->combat.staminaCost.value());
    CHECK(axe->heavy->poiseDamage.value() > axe->combat.poiseDamage.value());
    const items::ItemDef* batData = registry.Find("baseball_bat");
    const items::ItemDef* sledgeData = registry.Find("sledgehammer");
    const items::ItemDef* spearData = registry.Find("makeshift_spear");
    REQUIRE(batData != nullptr);
    REQUIRE(sledgeData != nullptr);
    REQUIRE(spearData != nullptr);
    REQUIRE(batData->heavy.has_value());
    REQUIRE(sledgeData->heavy.has_value());
    REQUIRE(spearData->heavy.has_value());
    CHECK(sledgeData->heavy->poiseDamage.value() >=
          batData->heavy->poiseDamage.value());
    CHECK(sledgeData->combat.staminaCost.value() >=
          batData->combat.staminaCost.value());
    CHECK(spearData->heavy->range.value() > spearData->combat.range.value());
    CHECK(sledgeData->combat.blockReduction.value() >
          batData->combat.blockReduction.value());

    // M19-F4：新增武器形成速度/范围/控场/耐久的取舍；衣物护甲与维修效率可区分。
    const items::ItemDef* machete = registry.Find("machete");
    const items::ItemDef* bat = registry.Find("baseball_bat");
    const items::ItemDef* crowbar = registry.Find("crowbar");
    const items::ItemDef* jacket = registry.Find("jacket");
    const items::ItemDef* vest = registry.Find("riot_vest");
    REQUIRE(machete != nullptr);
    REQUIRE(bat != nullptr);
    REQUIRE(crowbar != nullptr);
    REQUIRE(jacket != nullptr);
    REQUIRE(vest != nullptr);
    REQUIRE(machete->combat.Any());
    REQUIRE(bat->combat.Any());
    REQUIRE(crowbar->combat.Any());
    CHECK(machete->combat.cooldownSeconds.value() <
          bat->combat.cooldownSeconds.value());
    CHECK(bat->combat.knockback.value() > machete->combat.knockback.value());
    CHECK(crowbar->combat.wearPerHit.value() <
          machete->combat.wearPerHit.value());
    CHECK(crowbar->repairEfficiency > axe->repairEfficiency);

    const items::ItemDef* pistol = registry.Find("pistol");
    const items::ItemDef* shotgun = registry.Find("shotgun");
    const items::ItemDef* ammo9 = registry.Find("ammo_9mm");
    const items::ItemDef* shells = registry.Find("ammo_shells");
    REQUIRE(pistol != nullptr);
    REQUIRE(shotgun != nullptr);
    REQUIRE(ammo9 != nullptr);
    REQUIRE(shells != nullptr);
    REQUIRE(pistol->ranged.has_value());
    REQUIRE(shotgun->ranged.has_value());
    CHECK(pistol->ranged->ammoDefId == "ammo_9mm");
    CHECK(shotgun->ranged->ammoDefId == "ammo_shells");
    CHECK(shotgun->ranged->pellets > pistol->ranged->pellets);
    CHECK(shotgun->ranged->noiseRadius > pistol->ranged->noiseRadius);
    CHECK(shotgun->ranged->spreadDegrees > pistol->ranged->spreadDegrees);
    CHECK(pistol->ranged->magazine > shotgun->ranged->magazine);

    REQUIRE(jacket->armor.Any());
    REQUIRE(vest->armor.Any());
    CHECK(vest->armor.biteReduction > jacket->armor.biteReduction);
    CHECK(vest->armor.moveScale < jacket->armor.moveScale);
    CHECK(vest->weight > jacket->weight);

    const items::ItemDef* antibiotics = registry.Find("antibiotics");
    const items::ItemDef* disinfectant = registry.Find("disinfectant");
    REQUIRE(antibiotics != nullptr);
    REQUIRE(disinfectant != nullptr);
    CHECK(antibiotics->use.infectionCure > disinfectant->use.infectionCure);
    CHECK(antibiotics->use.infectionSlowSeconds >
          disinfectant->use.infectionSlowSeconds);
}

TEST_CASE("M24-T1：内容密度包物品加载与数值契约") {
    items::ItemRegistry registry;
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/items.json";
    std::string error;
    REQUIRE_MESSAGE(registry.LoadFile(path, &error), error);
    CHECK(registry.Size() == 94);

    const items::ItemDef* baton = registry.Find("police_baton");
    const items::ItemDef* sledge = registry.Find("sledgehammer");
    REQUIRE(baton != nullptr);
    REQUIRE(sledge != nullptr);
    REQUIRE(baton->combat.Any());
    REQUIRE(sledge->combat.Any());
    CHECK(baton->combat.damage.value() == doctest::Approx(36.0f));
    CHECK(sledge->combat.damage.value() == doctest::Approx(78.0f));
    CHECK(sledge->weight > baton->weight);

    const items::ItemDef* vest = registry.Find("police_vest");
    const items::ItemDef* suit = registry.Find("fire_suit");
    REQUIRE(vest != nullptr);
    REQUIRE(suit != nullptr);
    REQUIRE(vest->armor.Any());
    REQUIRE(suit->armor.Any());
    CHECK(vest->armor.biteReduction == doctest::Approx(0.28f));
    CHECK(suit->armor.waterproof == doctest::Approx(0.85f));

    const items::ItemDef* kit = registry.Find("first_aid_kit");
    REQUIRE(kit != nullptr);
    CHECK(kit->type == items::ItemType::Consumable);
    CHECK(kit->use.heal == doctest::Approx(40.0f));
    CHECK(registry.Find("herbal_salve")->use.infectionSlow ==
          doctest::Approx(0.2f));
    CHECK(registry.Find("carrot_seeds") != nullptr);
    CHECK(registry.Find("fresh_carrot") != nullptr);
    CHECK(registry.Find("raw_chicken") != nullptr);
    CHECK(registry.Find("cooked_chicken") != nullptr);
    CHECK(registry.Find("raw_goat_meat") != nullptr);
    CHECK(registry.Find("cooked_goat_meat") != nullptr);
    CHECK(registry.Find("relay_part") != nullptr);
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

TEST_CASE("X-F2b-2e：物品注册表事务式追加") {
    items::ItemRegistry registry;
    std::string error;
    REQUIRE(registry.LoadFromString(kValidJson, "base", &error));
    const std::size_t baseSize = registry.Size();

    const std::string dynamic = R"({
      "format": "items-v1",
      "items": [
        { "id": "dynamic_item", "name": "动态物品", "icon": "item.dynamic", "weight": 0.4, "maxStack": 2, "type": "generic" }
      ]
    })";
    std::size_t added = 0;
    REQUIRE(registry.MergeFromString(dynamic, "dynamic", &added, &error));
    CHECK(added == 1);
    CHECK(registry.Size() == baseSize + 1);
    const items::ItemDef* item = registry.Find("dynamic_item");
    REQUIRE(item != nullptr);
    CHECK(item->name == "动态物品");

    added = 99;
    CHECK_FALSE(registry.MergeFromString(dynamic, "duplicate", &added, &error));
    CHECK(added == 0);
    CHECK(registry.Size() == baseSize + 1);
    CHECK_FALSE(error.empty());

    const std::string badType = R"({
      "format": "items-v1",
      "items": [
        { "id": "bad_item", "name": "坏物品", "icon": "item.bad", "weight": 1, "maxStack": 1, "type": "armor" }
      ]
    })";
    CHECK_FALSE(registry.MergeFromString(badType, "bad", &added, &error));
    CHECK(registry.Find("bad_item") == nullptr);
    CHECK(registry.Size() == baseSize + 1);

    CHECK_FALSE(registry.MergeFromString(
        R"({"format":"items-v2","items":[]})", "bad-format", &added, &error));
    CHECK(registry.Size() == baseSize + 1);
}
