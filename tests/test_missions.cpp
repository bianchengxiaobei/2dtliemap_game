#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "game/Missions.h"

using namespace zl;

namespace {

std::filesystem::path WriteMissionFile(const std::string& name,
                                       const std::string& text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("zl_mod_missions_" + name + ".json");
    std::ofstream output(path, std::ios::binary);
    output << text;
    return path;
}

}  // namespace

TEST_CASE("M24-T3 内置任务配置包含一次性链与每日/随机事件") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    CHECK(error.empty());
    REQUIRE(missions.enabled);
    REQUIRE(missions.defs.size() == 31u);

    std::set<std::string> chains;
    for (const std::string& id :
         {"radio_signal", "radio_repair", "clinic_supply", "clinic_return",
          "farm_supply", "farm_tools", "hospital_supply",
          "hospital_quarantine", "military_armory", "military_extract",
          "super_stock", "super_return", "police_case", "police_return",
          "motel_guest", "motel_return", "relay_recover", "relay_extract"}) {
        const game::MissionDef* def = missions.Find(id);
        REQUIRE(def != nullptr);
        CHECK(def->kind == game::MissionKind::Once);
        chains.insert(def->chain);
    }
    CHECK(chains.size() == 9u);
    std::set<std::string> all_chains;
    for (const game::MissionDef& def : missions.defs) {
        all_chains.insert(def.chain);
    }
    CHECK(all_chains.size() == 19u);
    CHECK(missions.Find("hospital_quarantine")->prerequisite ==
          "hospital_supply");
    CHECK(missions.Find("military_extract")->prerequisite ==
          "military_armory");
    REQUIRE(missions.Find("hospital_quarantine")->objectives.size() == 2u);
    CHECK(missions.Find("hospital_quarantine")->objectives[0].kind ==
          game::MissionObjectiveKind::Loot);
    CHECK(missions.Find("hospital_quarantine")->objectives[0].target ==
          "poi_hospital_supplies");
    CHECK(missions.Find("military_extract")->objectives[0].kind ==
          game::MissionObjectiveKind::Loot);
    CHECK(missions.Find("military_extract")->objectives[0].target ==
          "poi_military_supplies");
    CHECK(missions.Find("hospital_supply")->objectives[0].kind ==
          game::MissionObjectiveKind::Poi);
    CHECK(missions.Find("hospital_supply")->objectives[0].target == "hospital");
    CHECK(missions.Find("military_armory")->objectives[0].target ==
          "military_checkpoint");
    const game::MissionDef* hospital_step =
        missions.Find("hospital_quarantine");
    const game::MissionDef* military_step = missions.Find("military_extract");
    REQUIRE(hospital_step != nullptr);
    REQUIRE(military_step != nullptr);
    REQUIRE(hospital_step->objectives.size() == 2u);
    REQUIRE(military_step->objectives.size() == 2u);
    CHECK(hospital_step->objectives[1].kind == game::MissionObjectiveKind::Reach);
    CHECK(military_step->objectives[1].kind == game::MissionObjectiveKind::Reach);
    CHECK(hospital_step->objectives[1].x == 70.0f);
    CHECK(hospital_step->objectives[1].y == 24.0f);
    CHECK(military_step->objectives[1].radius == 3.0f);
    CHECK(missions.Find("hospital_supply")->reward.xp +
              hospital_step->reward.xp == 190.0f);
    CHECK(missions.Find("military_armory")->reward.xp +
              military_step->reward.xp == 240.0f);
    CHECK(missions.Find("event_hospital_outbreak")->chance == 0.55f);
    CHECK(missions.Find("event_military_patrol")->chance == 0.40f);
    CHECK(missions.Find("daily_patrol") != nullptr);
    CHECK(missions.Find("daily_patrol")->kind == game::MissionKind::Daily);
    CHECK(missions.Find("event_supply_drop") != nullptr);
    CHECK(missions.Find("event_supply_drop")->kind ==
          game::MissionKind::Random);
    CHECK(missions.Find("event_supply_drop")->source ==
          game::MissionSource::Npc);
    CHECK(missions.Find("event_scout") != nullptr);
    CHECK(missions.Find("event_hospital_outbreak")->kind ==
          game::MissionKind::Random);
    CHECK(missions.Find("event_hospital_outbreak")->source ==
          game::MissionSource::Npc);
    CHECK(missions.Find("event_military_patrol")->kind ==
          game::MissionKind::Random);
    CHECK(missions.Find("event_military_patrol")->source ==
          game::MissionSource::Radio);
    const game::MissionDef* horde =
        missions.Find("event_wandering_horde");
    const game::MissionDef* house =
        missions.Find("event_infected_house");
    const game::MissionDef* ambush =
        missions.Find("event_survivor_ambush");
    const game::MissionDef* cold = missions.Find("event_cold_snap");
    REQUIRE(horde != nullptr);
    REQUIRE(house != nullptr);
    REQUIRE(ambush != nullptr);
    REQUIRE(cold != nullptr);
    CHECK(horde->kind == game::MissionKind::Random);
    CHECK(horde->minDay == 3);
    CHECK(horde->chance == 0.50f);
    CHECK(horde->objectives[0].target == "event_wandering_horde");
    CHECK(horde->objectives[0].count == 8);
    CHECK(house->kind == game::MissionKind::Random);
    CHECK(house->minDay == 2);
    CHECK(house->chance == 0.55f);
    REQUIRE(house->objectives.size() == 2u);
    CHECK(house->objectives[0].target == "event_infected_house");
    CHECK(house->objectives[1].kind == game::MissionObjectiveKind::Loot);
    CHECK(house->objectives[1].target == "event_infected_cache");
    CHECK(ambush->minDay == 4);
    CHECK(ambush->chance == 0.40f);
    CHECK(ambush->objectives[0].target == "event_survivor_ambush");
    CHECK(ambush->reward.item == "ammo_9mm");
    CHECK(ambush->reward.extraItem == "painkillers");
    CHECK(cold->minDay == 2);
    CHECK(cold->chance == 0.35f);
    CHECK(cold->objectives[0].kind == game::MissionObjectiveKind::Reach);
    CHECK(cold->objectives[0].x == 70.0f);
    CHECK(cold->objectives[0].y == 24.0f);
    int randomCount = 0;
    for (const game::MissionDef& def : missions.defs) {
        if (def.kind == game::MissionKind::Random) {
            ++randomCount;
        }
    }
    CHECK(randomCount == 8);

    const game::MissionDef* settlement_help = missions.Find("settlement_help");
    const game::MissionDef* settlement_escort =
        missions.Find("settlement_escort");
    const game::MissionDef* settlement_clear =
        missions.Find("settlement_clear");
    const game::MissionDef* settlement_supply =
        missions.Find("settlement_supply");
    REQUIRE(settlement_help != nullptr);
    REQUIRE(settlement_escort != nullptr);
    REQUIRE(settlement_clear != nullptr);
    REQUIRE(settlement_supply != nullptr);
    CHECK(settlement_help->task == game::MissionTaskKind::Help);
    CHECK(settlement_escort->task == game::MissionTaskKind::Escort);
    CHECK(settlement_clear->task == game::MissionTaskKind::Clear);
    CHECK(settlement_supply->task == game::MissionTaskKind::Supply);
    CHECK(settlement_help->chain == "settlement");
    CHECK(settlement_help->order == 1);
    CHECK(settlement_escort->order == 2);
    CHECK(settlement_clear->order == 3);
    CHECK(settlement_supply->order == 4);
    CHECK(settlement_escort->prerequisite == "settlement_help");
    CHECK(settlement_clear->prerequisite == "settlement_escort");
    CHECK(settlement_supply->prerequisite == "settlement_clear");
    for (const game::MissionDef* def :
         {settlement_help, settlement_escort, settlement_clear,
          settlement_supply}) {
        CHECK(def->kind == game::MissionKind::Once);
        CHECK(def->source == game::MissionSource::Npc);
        CHECK_FALSE(def->autoActivate);
        CHECK(def->timeLimitDays > 0);
        CHECK(def->retryDays == 1);
    }
    CHECK(settlement_escort->objectives.size() == 2u);
    CHECK(settlement_supply->objectives.size() == 2u);
    CHECK(settlement_supply->objectives[0].target ==
          "poi_hospital_supplies");

    const game::MissionDef* super_stock = missions.Find("super_stock");
    const game::MissionDef* super_return = missions.Find("super_return");
    const game::MissionDef* police_case = missions.Find("police_case");
    const game::MissionDef* police_return = missions.Find("police_return");
    const game::MissionDef* motel_guest = missions.Find("motel_guest");
    const game::MissionDef* motel_return = missions.Find("motel_return");
    const game::MissionDef* relay_recover = missions.Find("relay_recover");
    const game::MissionDef* relay_extract = missions.Find("relay_extract");
    for (const game::MissionDef* def :
         {super_stock, super_return, police_case, police_return, motel_guest,
          motel_return, relay_recover, relay_extract}) {
        REQUIRE(def != nullptr);
        CHECK(def->kind == game::MissionKind::Once);
        CHECK(def->autoActivate);
    }
    CHECK(super_stock->chain == "supermarket");
    CHECK(super_stock->objectives[0].kind == game::MissionObjectiveKind::Poi);
    CHECK(super_stock->objectives[0].target == "supermarket");
    CHECK(super_return->prerequisite == "super_stock");
    CHECK(super_return->objectives[0].kind == game::MissionObjectiveKind::Reach);
    CHECK(super_return->objectives[0].x == 70.0f);
    CHECK(super_return->objectives[0].y == 24.0f);
    CHECK(police_case->objectives[0].kind == game::MissionObjectiveKind::Loot);
    CHECK(police_case->objectives[0].target == "poi_police_evidence");
    CHECK(police_return->prerequisite == "police_case");
    CHECK(motel_guest->objectives[0].kind == game::MissionObjectiveKind::Poi);
    CHECK(motel_guest->objectives[0].target == "motel");
    CHECK(motel_return->prerequisite == "motel_guest");
    CHECK(relay_recover->prerequisite == "radio_repair");
    CHECK(relay_recover->source == game::MissionSource::Radio);
    REQUIRE(relay_recover->objectives.size() == 2u);
    CHECK(relay_recover->objectives[0].kind ==
          game::MissionObjectiveKind::Loot);
    CHECK(relay_recover->objectives[0].target == "poi_warehouse_relay");
    CHECK(relay_recover->objectives[1].kind ==
          game::MissionObjectiveKind::CollectItem);
    CHECK(relay_recover->objectives[1].target == "relay_part");
    CHECK(relay_recover->objectives[1].count == 1);
    CHECK(relay_extract->prerequisite == "relay_recover");
    CHECK(relay_extract->objectives[0].kind ==
          game::MissionObjectiveKind::Reach);
    CHECK(relay_extract->objectives[0].x == 66.5f);
    CHECK(relay_extract->objectives[0].y == 28.5f);
    CHECK(relay_extract->objectives[0].radius == 1.5f);
    CHECK(super_stock->reward.xp + super_return->reward.xp == 120.0f);
    CHECK(police_case->reward.xp + police_return->reward.xp == 170.0f);
    CHECK(motel_guest->reward.xp + motel_return->reward.xp == 120.0f);
    CHECK(relay_recover->reward.xp + relay_extract->reward.xp == 270.0f);
    CHECK(relay_recover->reward.item == "toolbox");
    CHECK(relay_extract->reward.item == "ammo_shells");

    std::set<std::string> sources;
    for (const game::MissionDef& def : missions.defs) {
        CHECK_FALSE(def.objectives.empty());
        CHECK_FALSE(def.reward.label.empty());
        sources.insert(game::MissionSourceName(def.source));
    }
    CHECK(sources.contains("radio"));
    CHECK(sources.contains("note"));
    CHECK(sources.contains("base"));
    CHECK(sources.contains("npc"));
}

TEST_CASE("M19-F3 任务目标、进度文案与完成判定") {
    game::MissionDef def;
    def.id = "test_chain_1";
    def.name = "测试任务";
    def.chain = "test";
    def.source = game::MissionSource::Note;
    def.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::CollectItem, "bandage", 2, 0.0f, 0.0f,
        1.5f, "收集绷带"});
    def.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::Interact, "radio", 1, 0.0f, 0.0f,
        1.5f, "修理电台"});
    def.reward.label = "80 生存经验 + 绷带×2";

    ecs::MissionRunState state;
    state.id = def.id;
    state.progress = {1, 0};
    CHECK_FALSE(game::IsMissionComplete(def, state));
    CHECK(game::MissionObjectiveProgress(def.objectives[0], &state, 0) == 1);
    CHECK_FALSE(game::IsMissionObjectiveComplete(def.objectives[0], &state, 0));
    state.progress = {2, 1};
    CHECK(game::IsMissionComplete(def, state));
    CHECK(game::IsMissionObjectiveComplete(def.objectives[1], &state, 1));

    const std::string guide = game::MissionGuideText(def, state);
    CHECK(guide.find("任务[纸条]") != std::string::npos);
    CHECK(guide.find("测试任务") != std::string::npos);
    CHECK(guide.find("收集绷带 2/2") != std::string::npos);
    CHECK(guide.find("奖励 80 生存经验") != std::string::npos);
    CHECK(guide.find("L切换") != std::string::npos);
    const std::string directed =
        game::MissionGuideText(def, state, "目标东南 34格");
    CHECK(directed.find("目标东南 34格") != std::string::npos);
    CHECK(directed.find("奖励 80 生存经验") != std::string::npos);
    CHECK(std::string(game::MissionGuideTone(def)) == "info");
    def.kind = game::MissionKind::Random;
    CHECK(std::string(game::MissionGuideTone(def)) == "warn");
}

TEST_CASE("M24-T3 中继链按 radio_repair 解锁且必须取得 relay_part") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    const game::MissionDef* recover = missions.Find("relay_recover");
    const game::MissionDef* extract = missions.Find("relay_extract");
    REQUIRE(recover != nullptr);
    REQUIRE(extract != nullptr);

    ecs::MissionLog untouched;
    untouched.missions.push_back(ecs::MissionRunState{
        "radio_repair", {1}, ecs::MissionRunStatus::Active, 2, false});
    CHECK_FALSE(game::MissionCanActivate(*recover, untouched, 3));

    ecs::MissionLog finished;
    finished.missions.push_back(ecs::MissionRunState{
        "radio_repair", {1}, ecs::MissionRunStatus::Completed, 2, true});
    CHECK(game::MissionCanActivate(*recover, finished, 3));
    CHECK_FALSE(game::MissionCanActivate(*extract, finished, 3));

    ecs::MissionRunState recovered{"relay_recover", {1, 0},
                                   ecs::MissionRunStatus::Active, 3, false};
    CHECK_FALSE(game::IsMissionComplete(*recover, recovered));
    recovered.progress[1] = 1;
    CHECK(game::IsMissionComplete(*recover, recovered));
    const std::string guide =
        game::MissionGuideText(*recover, recovered, "目标东南 34格");
    CHECK(guide.find("如果你听到这段广播") != std::string::npos);
    CHECK(guide.find("把中继零件放进背包 1/1") != std::string::npos);
    CHECK(guide.find("目标东南 34格") != std::string::npos);

    finished.missions.push_back(recovered);
    finished.missions.back().status = ecs::MissionRunStatus::Completed;
    finished.missions.back().rewardClaimed = true;
    CHECK(game::MissionCanActivate(*extract, finished, 4));
}

TEST_CASE("M24-T5 事件指引与任务焦点排序") {
    game::MissionDef event;
    event.id = "event_test";
    event.name = "测试事件";
    event.kind = game::MissionKind::Random;
    event.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::Loot, "event_box", 2, 0.0f, 0.0f, 1.5f,
        "搜索事件箱"});
    event.reward.label = "测试奖励";
    ecs::MissionRunState state{"event_test", {1}, ecs::MissionRunStatus::Active,
                               2, false};
    const std::string guide =
        game::MissionEventGuideText(event, state, "目标东南 12格");
    CHECK(guide.find("事件 · 测试事件") != std::string::npos);
    CHECK(guide.find("搜索事件箱 1/2") != std::string::npos);
    CHECK(guide.find("目标东南 12格") != std::string::npos);
    CHECK(guide.find("奖励 测试奖励") != std::string::npos);
    CHECK(guide.find("L切换") == std::string::npos);

    game::MissionDef once = event;
    once.id = "once_test";
    once.kind = game::MissionKind::Once;
    once.order = 1;
    game::MissionDef daily = event;
    daily.id = "daily_test";
    daily.kind = game::MissionKind::Daily;
    daily.order = 1;
    std::vector<game::MissionFocusCandidate> candidates{
        {&daily, 1.0f}, {&once, 30.0f}, {&event, 60.0f}};
    std::sort(candidates.begin(), candidates.end(), game::MissionFocusLess);
    REQUIRE(candidates.size() == 3u);
    CHECK(candidates[0].def == &event);
    CHECK(candidates[1].def == &once);
    CHECK(candidates[2].def == &daily);

    game::MissionDef onceNear = once;
    onceNear.id = "once_near";
    onceNear.order = 2;
    game::MissionDef onceFar = once;
    onceFar.id = "once_far";
    onceFar.order = 0;
    candidates = {{&onceFar, 40.0f}, {&onceNear, 10.0f}};
    std::sort(candidates.begin(), candidates.end(), game::MissionFocusLess);
    CHECK(candidates[0].def == &onceNear);
    CHECK(candidates[1].def == &onceFar);

    CHECK(game::MissionFocusPriority(event) == 0);
    CHECK(game::MissionFocusPriority(once) == 1);
    CHECK(game::MissionFocusPriority(daily) == 2);
}

TEST_CASE("M19-F3 随机事件判定稳定且支持必中/必不中") {
    for (int day = 1; day <= 8; ++day) {
        const bool first = game::MissionRandomRoll(20261024u, day,
                                                   "event_supply_drop", 0.65f);
        const bool second = game::MissionRandomRoll(20261024u, day,
                                                    "event_supply_drop", 0.65f);
        CHECK(first == second);
        CHECK_FALSE(game::MissionRandomRoll(20261024u, day, "event_supply_drop",
                                            0.0f));
        CHECK(game::MissionRandomRoll(20261024u, day, "event_supply_drop",
                                      1.0f));
    }
}

TEST_CASE("M24-T4 随机事件按 minDay 与概率触发且不是每天必出") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    for (const std::string& id :
         {"event_wandering_horde", "event_infected_house",
          "event_survivor_ambush", "event_cold_snap"}) {
        const game::MissionDef* def = missions.Find(id);
        REQUIRE(def != nullptr);
        int hits = 0;
        int eligible = 0;
        for (int day = 1; day <= 30; ++day) {
            if (day < def->minDay) {
                continue;
            }
            ++eligible;
            if (game::MissionRandomRoll(missions.randomSeed, day, def->id,
                                        def->chance)) {
                ++hits;
            }
        }
        CHECK(eligible > 0);
        CHECK(hits > 0);
        CHECK(hits < eligible);
    }
}

TEST_CASE("M19-F3 任务链下一阶段判断") {
    game::MissionConfig config;
    game::MissionDef first;
    first.id = "step_one";
    first.chain = "chain";
    first.order = 1;
    game::MissionDef second;
    second.id = "step_two";
    second.chain = "chain";
    second.order = 2;
    second.prerequisite = "step_one";
    config.defs = {first, second};
    CHECK(game::MissionHasNextStep(config, first));
    CHECK_FALSE(game::MissionHasNextStep(config, second));
    ecs::MissionLog log;
    log.lastDay = 2;
    log.randomDay = 2;
    log.missions.push_back(ecs::MissionRunState{
        "step_one", {1}, ecs::MissionRunStatus::Completed, 1, true});
    log.missions.push_back(ecs::MissionRunState{
        "step_two", {0}, ecs::MissionRunStatus::Active, 2, false});
    REQUIRE(game::FindMissionState(log, "step_one") != nullptr);
    CHECK(game::FindMissionState(log, "missing") == nullptr);
    CHECK(game::AbandonMission(&log, "step_two"));
    CHECK(log.missions[1].status == ecs::MissionRunStatus::Failed);
    CHECK_FALSE(log.missions[1].rewardClaimed);
    CHECK_FALSE(game::AbandonMission(&log, "step_two"));
}

TEST_CASE("M23-F6-T5 任务失败次日重试、前置解锁与旧档进度兼容") {
    game::MissionDef first;
    first.id = "step_one";
    first.chain = "chain";
    first.order = 1;
    first.kind = game::MissionKind::Once;
    game::MissionDef second = first;
    second.id = "step_two";
    second.order = 2;
    second.prerequisite = "step_one";
    second.minDay = 3;

    ecs::MissionLog log;
    CHECK(game::MissionCanActivate(first, log, 1));
    CHECK_FALSE(game::MissionCanActivate(second, log, 3));

    log.missions.push_back(ecs::MissionRunState{
        "step_one", {1}, ecs::MissionRunStatus::Active, 1, false});
    REQUIRE(game::AbandonMission(&log, "step_one", 2));
    CHECK_FALSE(game::MissionCanActivate(first, log, 2));
    CHECK(game::MissionCanActivate(first, log, 3));

    log.missions[0].status = ecs::MissionRunStatus::Completed;
    CHECK(game::MissionCanActivate(second, log, 3));
    log.missions.push_back(ecs::MissionRunState{
        "step_two", {0}, ecs::MissionRunStatus::Active, 3, false});
    CHECK_FALSE(game::MissionCanActivate(second, log, 4));
    log.missions[1].status = ecs::MissionRunStatus::Failed;
    CHECK_FALSE(game::MissionCanActivate(second, log, 3));
    CHECK(game::MissionCanActivate(second, log, 4));

    game::MissionDef daily = first;
    daily.kind = game::MissionKind::Daily;
    CHECK_FALSE(game::MissionCanActivate(daily, log, 9));

    game::MissionDef two_steps;
    two_steps.id = "old_save";
    two_steps.kind = game::MissionKind::Once;
    two_steps.objectives.push_back(game::MissionObjectiveDef{});
    two_steps.objectives.push_back(game::MissionObjectiveDef{});
    ecs::MissionRunState sparse{"old_save", {1}, ecs::MissionRunStatus::Active,
                                2, false};
    CHECK(game::MissionObjectiveProgress(two_steps.objectives[0], &sparse, 0) == 1);
    CHECK(game::MissionObjectiveProgress(two_steps.objectives[1], &sparse, 1) == 0);
    CHECK_FALSE(game::IsMissionComplete(two_steps, sparse));

    ecs::MissionLog old_log;
    old_log.missions.push_back(ecs::MissionRunState{
        "radio_signal", {1}, ecs::MissionRunStatus::Completed, 1, true});
    game::MissionDef f6_root = first;
    f6_root.id = "hospital_supply";
    CHECK(game::MissionCanActivate(f6_root, old_log, 4));
}

TEST_CASE("M23-F4-T4 聚落委托手动接取、超时失败与重复限制") {
    game::MissionDef def;
    def.id = "settlement_help";
    def.name = "伤员的药";
    def.chain = "settlement";
    def.kind = game::MissionKind::Once;
    def.source = game::MissionSource::Npc;
    def.task = game::MissionTaskKind::Help;
    def.autoActivate = false;
    def.timeLimitDays = 2;
    def.retryDays = 1;
    def.objectives.push_back(game::MissionObjectiveDef{
        game::MissionObjectiveKind::CollectItem, "bandage", 2, 0.0f, 0.0f,
        1.5f, "为营地伤员收集绷带"});
    def.reward.label = "70 生存经验 + 抗生素×1";

    ecs::MissionLog log;
    CHECK_FALSE(def.autoActivate);
    CHECK(game::MissionCanActivate(def, log, 1));

    log.missions.push_back(ecs::MissionRunState{
        def.id, {0}, ecs::MissionRunStatus::Active, 1, false});
    CHECK_FALSE(game::MissionCanActivate(def, log, 2));
    CHECK(game::MissionDeadlineDay(def, log.missions[0]) == 3);
    CHECK_FALSE(game::MissionIsExpired(def, log.missions[0], 2));
    CHECK(game::MissionIsExpired(def, log.missions[0], 3));

    const std::string guide =
        game::MissionGuideText(def, log.missions[0]);
    CHECK(guide.find("[求助]") != std::string::npos);
    CHECK(guide.find("限2天") != std::string::npos);
    CHECK(guide.find("收集绷带 0/2") != std::string::npos);

    log.missions[0].status = ecs::MissionRunStatus::Failed;
    log.missions[0].day = 3;
    CHECK_FALSE(game::MissionCanActivate(def, log, 3));
    CHECK(game::MissionCanActivate(def, log, 4));
    def.retryDays = 2;
    CHECK_FALSE(game::MissionCanActivate(def, log, 4));
    CHECK(game::MissionCanActivate(def, log, 5));

    log.missions[0].status = ecs::MissionRunStatus::Completed;
    log.missions[0].rewardClaimed = true;
    CHECK_FALSE(game::MissionCanActivate(def, log, 99));
    CHECK_FALSE(game::MissionIsExpired(def, log.missions[0], 99));
    CHECK(std::string(game::MissionTaskKindName(def.task)) == "help");
    CHECK(std::string(game::MissionTaskKindLabel(def.task)) == "求助");

    game::MissionDef escort = def;
    escort.id = "settlement_escort";
    escort.order = 2;
    escort.task = game::MissionTaskKind::Escort;
    escort.prerequisite = "settlement_help";
    ecs::MissionLog chain;
    CHECK_FALSE(game::MissionCanActivate(escort, chain, 1));
    chain.missions.push_back(ecs::MissionRunState{
        "settlement_help", {1}, ecs::MissionRunStatus::Completed, 1, true});
    CHECK(game::MissionCanActivate(escort, chain, 1));
}

TEST_CASE("X-F2b-2c mod 任务文件按顺序追加且失败不改动已有任务") {
    core::Config config;
    config.Set("missions", nlohmann::json::parse(R"({
      "enabled": true,
      "randomSeed": 7,
      "defs": [
        {
          "id": "base_one",
          "name": "基础任务",
          "chain": "base",
          "order": 1,
          "kind": "once",
          "source": "base",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 10.0, "skill": "melee", "label": "10 近战经验" }
        }
      ]
    })"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    REQUIRE(missions.defs.size() == 1u);

    const std::filesystem::path first = WriteMissionFile("first", R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "mod_one",
          "name": "模组任务一",
          "chain": "mod",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "base_one",
          "objectives": [
            { "kind": "loot", "target": "any", "count": 1, "description": "搜索容器" }
          ],
          "reward": { "xp": 20.0, "skill": "survival", "label": "20 生存经验" }
        }
      ]
    })");
    const std::filesystem::path second = WriteMissionFile("second", R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "mod_two",
          "name": "模组任务二",
          "chain": "mod",
          "order": 2,
          "kind": "once",
          "source": "note",
          "prerequisite": "mod_one",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 2, "description": "清理两只僵尸" }
          ],
          "reward": { "xp": 30.0, "skill": "melee", "label": "30 近战经验" }
        }
      ]
    })");
    game::MissionMergeStats stats;
    std::string acceptedJson;
    REQUIRE(game::MergeMissionFiles({first.generic_string(),
                                     second.generic_string()},
                                    config, &missions, &stats, &error,
                                    &acceptedJson));
    CHECK(error.empty());
    CHECK_FALSE(acceptedJson.empty());
    CHECK(acceptedJson.find("base_one") != std::string::npos);
    CHECK(acceptedJson.find("mod_one") != std::string::npos);
    CHECK(acceptedJson.find("mod_two") != std::string::npos);
    CHECK(stats.filesLoaded == 2);
    CHECK(stats.defsAdded == 2);
    REQUIRE(missions.defs.size() == 3u);
    REQUIRE(missions.Find("mod_one") != nullptr);
    REQUIRE(missions.Find("mod_two") != nullptr);
    CHECK(missions.Find("mod_two")->prerequisite == "mod_one");

    const std::filesystem::path duplicate = WriteMissionFile("duplicate", R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "base_one",
          "name": "重复任务",
          "chain": "dup",
          "order": 1,
          "kind": "once",
          "source": "note",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 1.0, "skill": "melee", "label": "1 经验" }
        }
      ]
    })");
    game::MissionMergeStats duplicateStats;
    REQUIRE(game::MergeMissionFiles({duplicate.generic_string()}, config,
                                    &missions, &duplicateStats, &error));
    CHECK(duplicateStats.filesLoaded == 0);
    CHECK(missions.defs.size() == 3u);

    const std::filesystem::path badFormat = WriteMissionFile("bad_format", R"({
      "format": "missions-v0",
      "defs": []
    })");
    game::MissionMergeStats badStats;
    REQUIRE(game::MergeMissionFiles({badFormat.generic_string()}, config,
                                    &missions, &badStats, &error));
    CHECK(badStats.filesLoaded == 0);
    CHECK(missions.defs.size() == 3u);

    const std::filesystem::path badPrereq = WriteMissionFile("bad_prereq", R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "bad_step",
          "name": "坏前置",
          "chain": "bad",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "missing",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 1.0, "skill": "melee", "label": "1 经验" }
        }
      ]
    })");
    game::MissionMergeStats badPrereqStats;
    REQUIRE(game::MergeMissionFiles({badPrereq.generic_string()}, config,
                                    &missions, &badPrereqStats, &error));
    CHECK(badPrereqStats.filesLoaded == 0);
    CHECK(missions.defs.size() == 3u);
    CHECK(missions.Find("bad_step") == nullptr);

    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
    std::filesystem::remove(duplicate, ec);
    std::filesystem::remove(badFormat, ec);
    std::filesystem::remove(badPrereq, ec);
}
