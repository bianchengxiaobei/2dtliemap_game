#include <string>

#include <doctest/doctest.h>

#include "ui/HudState.h"

TEST_CASE("M18-F5 base status text summarizes facilities and defenses") {
    zl::ui::HudState state;
    state.baseActive = true;
    state.baseStorageCount = 2;
    state.baseStorageWeight = 12.4f;
    state.baseStorageCapacity = 100.0f;
    state.baseBeds = 1;
    state.baseCampfires = 1;
    state.baseWorkbenches = 2;
    state.baseDefensesIntact = 5;
    state.baseDefensesDamaged = 0;
    state.baseFarms = 1;
    state.baseGenerators = 1;
    state.baseGeneratorsOnline = 1;
    state.baseFridges = 1;
    state.baseFridgesPowered = 1;
    state.baseTraps = 2;
    state.baseTrapsArmed = 1;
    state.baseFloodlights = 2;
    state.baseFloodlightsPowered = 2;
    state.baseElectricFences = 3;
    state.baseElectricFencesPowered = 2;
    state.basePowerFuelMinutes = 320.0f;
    state.basePowerLoad = 2.5f;

    std::string tone;
    const std::string text = zl::ui::BaseStatusText(state, &tone);
    CHECK(text.find("基地") != std::string::npos);
    CHECK(text.find("仓库 2") != std::string::npos);
    CHECK(text.find("12.4/100.0kg") != std::string::npos);
    CHECK(text.find("床位 1") != std::string::npos);
    CHECK(text.find("篝火 1") != std::string::npos);
    CHECK(text.find("工作台 2") != std::string::npos);
    CHECK(text.find("防御 5 完好 / 0 损坏") != std::string::npos);
    CHECK(text.find("电力：燃料 320 分 · 负载 2.5 · 供电中") !=
          std::string::npos);
    CHECK(tone == "info");
}

TEST_CASE("M21-F2 base status reports outage and low fuel") {
    zl::ui::HudState state;
    state.baseActive = true;
    state.baseGenerators = 1;
    state.baseGeneratorsOnline = 1;
    state.basePowerFuelMinutes = 8.0f;
    state.basePowerLoad = 3.0f;
    state.basePowerLowFuel = true;
    std::string tone;
    const std::string low = zl::ui::BaseStatusText(state, &tone);
    CHECK(low.find("燃料 8 分") != std::string::npos);
    CHECK(tone == "warn");

    state.baseGeneratorsOnline = 0;
    state.basePowerOutage = true;
    const std::string outage = zl::ui::BaseStatusText(state, &tone);
    CHECK(outage.find("电力：停电") != std::string::npos);
    CHECK(tone == "fail");
}

TEST_CASE("M18-F5 base status tone distinguishes warning and danger") {
    zl::ui::HudState state;
    state.baseActive = true;
    state.baseDefensesIntact = 4;
    state.baseDefensesDamaged = 1;
    std::string tone;
    (void)zl::ui::BaseStatusText(state, &tone);
    CHECK(tone == "warn");

    state.baseDefensesIntact = 2;
    state.baseDefensesDamaged = 2;
    (void)zl::ui::BaseStatusText(state, &tone);
    CHECK(tone == "fail");

    state.baseDefensesDamaged = 0;
    state.baseStorageCount = 1;
    state.baseStorageWeight = 95.0f;
    state.baseStorageCapacity = 100.0f;
    (void)zl::ui::BaseStatusText(state, &tone);
    CHECK(tone == "warn");
}

TEST_CASE("M18-F5 inactive base status is hidden") {
    zl::ui::HudState state;
    state.baseStorageCount = 3;
    state.baseBeds = 1;
    std::string tone = "warn";
    CHECK(zl::ui::BaseStatusText(state, &tone).empty());
    CHECK(tone == "info");
}

TEST_CASE("M21-F1c siege report parses end text into panel lines") {
    const std::string text =
        "守夜结束：损失 1 座栅栏 | 评级 S | 波次 3/3 | 结构损失 2 座（存活 90%）"
        " | 防线完好 | 击杀 12 | 奖励 燃料罐×1、维修包×1 | 来袭 北";
    std::string rating;
    std::array<std::string, 8> lines{};
    const int count = zl::ui::BuildSiegeReportLines(text, &rating, &lines);
    CHECK(rating == "S");
    REQUIRE(count == 7);
    CHECK(lines[0] == "损失 1 座栅栏");
    CHECK(lines[1] == "波次 3/3");
    CHECK(lines[2] == "结构损失 2 座（存活 90%）");
    CHECK(lines[3] == "防线完好");
    CHECK(lines[4] == "击杀 12");
    CHECK(lines[5] == "奖励 燃料罐×1、维修包×1");
    CHECK(lines[6] == "来袭 北");
    CHECK(lines[7].empty());
    CHECK(std::string(zl::ui::SiegeReportToneForRating(rating)) == "ok");
    CHECK(std::string(zl::ui::SiegeReportToneForRating("B")) == "warn");
    CHECK(std::string(zl::ui::SiegeReportToneForRating("C")) == "fail");
    CHECK(std::string(zl::ui::SiegeReportToneForRating("")) == "info");
}

TEST_CASE("M21-F1c siege report tolerates missing prefix and ratings") {
    std::string rating = "S";
    std::array<std::string, 8> lines{};
    CHECK(zl::ui::BuildSiegeReportLines("", &rating, &lines) == 0);
    CHECK(rating.empty());
    CHECK(lines[0].empty());

    const int count = zl::ui::BuildSiegeReportLines(
        "守夜结束：评级 C | 基地失守", &rating, &lines);
    CHECK(rating == "C");
    REQUIRE(count == 1);
    CHECK(lines[0] == "基地失守");
}

TEST_CASE("M32-T20 defense report parses into panel lines") {
    std::array<std::string, 8> lines{};
    const int count = zl::ui::BuildDefenseReportLines(
        "守城战报 · south_farmland · 守住 · t20-first 贡献+50 经验+80",
        &lines);
    REQUIRE(count == 3);
    CHECK(lines[0] == "south_farmland");
    CHECK(lines[1] == "守住");
    CHECK(lines[2] == "t20-first 贡献+50 经验+80");
    CHECK(lines[3].empty());

    CHECK(zl::ui::BuildDefenseReportLines("", &lines) == 0);
    CHECK(zl::ui::BuildDefenseReportLines("守城战报 · 守住", &lines) == 1);
    CHECK(lines[0] == "守住");
}

TEST_CASE("F5-T2 base status reports farm stages and crop warnings") {
    zl::ui::HudState state;
    state.baseActive = true;
    state.baseFarms = 3;
    state.baseFarmGrowing = 3;
    state.baseFarmMature = 1;
    state.baseFarmThirsty = 1;
    state.baseFarmDiseased = 1;
    state.baseFarmFertilized = 2;

    std::string tone;
    const std::string text = zl::ui::BaseStatusText(state, &tone);
    CHECK(text.find("农田 3") != std::string::npos);
    CHECK(text.find("生长 3") != std::string::npos);
    CHECK(text.find("成熟 1") != std::string::npos);
    CHECK(text.find("缺水 1") != std::string::npos);
    CHECK(text.find("病害 1") != std::string::npos);
    CHECK(tone == "warn");

    state.baseFarms = 0;
    state.baseFarmThirsty = 0;
    state.baseFarmDiseased = 0;
    const std::string clear = zl::ui::BaseStatusText(state, &tone);
    CHECK(clear.find("农田 0") != std::string::npos);
    CHECK(tone == "info");
}
