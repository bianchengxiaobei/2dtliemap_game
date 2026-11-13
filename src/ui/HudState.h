#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace zl::ui {

struct HudSlot {
    std::string label;
    int count = 0;
};

inline constexpr std::size_t kMaxModHudFields = 8;
inline constexpr std::size_t kMaxDialogueOptions = 5;

struct HudModField {
    std::string modId;
    std::string id;
    std::string label;
    std::string text;
    std::string tone = "info";

    bool operator==(const HudModField&) const = default;
};

struct HudState {
    bool active = false;
    std::string day;
    std::string time;
    std::string speed;
    bool playerValid = false;
    float health = 0.0f;
    float healthMax = 100.0f;
    float stamina = 0.0f;
    float staminaMax = 100.0f;
    // M30-T6：联机服务器权威姿态（单机留空）。
    std::string combatStatus;
    std::string combatTone = "info";
    // M23-F1-T8：主手远程武器 HUD（单机本地状态/联机 owner-only 镜像）。
    bool rangedValid = false;
    std::string rangedName;
    int rangedLoaded = 0;
    int rangedMagazine = 0;
    int rangedReserve = 0;
    bool rangedReloading = false;
    float rangedReloadProgress = 0.0f;
    float rangedReloadRemaining = 0.0f;
    std::string rangedStatus;
    std::string rangedTone = "info";
    // M23-F2a-T5：附近资源节点/当前采集状态（单机）。
    bool forageValid = false;
    std::string forageName;
    std::string forageToolName;
    bool forageMissingTool = false;
    bool forageExhausted = false;
    double forageRespawnMinutes = 0.0;
    float forageSeconds = 0.0f;
    bool forageSearching = false;
    float forageProgress = 0.0f;
    std::string forageStatus;
    std::string forageTone = "info";
    bool fishingValid = false;
    std::string fishingName;
    std::string fishingRodName;
    std::string fishingBaitName;
    bool fishingMissingRod = false;
    bool fishingMissingBait = false;
    float fishingSeconds = 0.0f;
    bool fishingWaiting = false;
    float fishingProgress = 0.0f;
    bool fishingBiting = false;
    float fishingBiteRemaining = 0.0f;
    std::string fishingStatus;
    std::string fishingTone = "info";
    bool needsValid = false;
    std::string needsStatus;
    std::string needsTone = "info";
    std::string woundStatus;
    std::string woundTone = "info";
    std::string painStatus;
    std::string painTone = "info";
    std::string armorStatus;
    std::string armorTone = "info";
    std::string infectionStatus;
    std::string infectionTone = "info";
    std::string weatherStatus;
    std::string weatherTone = "info";
    std::string weatherClass = "clear";
    std::string threatStatus;
    std::string threatDirection;
    float threatRemaining = 0.0f;
    float threatDuration = 0.0f;
    std::string objective;
    std::string objectiveTone = "info";
    std::string objectiveHint;
    std::string poiGuide;
    std::string poiGuideTone = "info";
    std::string missionGuide;
    std::string missionTone = "info";
    std::string eventGuide;
    std::string eventTone = "warn";
    // M33-F2-T4: single-player interactive world event panel and radio log.
    bool worldEventVisible = false;
    bool worldEventCanChoose = false;
    bool worldEventChoiceOpen = false;
    std::string worldEventName;
    std::string worldEventMeta;
    std::string worldEventStage;
    std::string worldEventHint;
    std::string worldEventTone = "info";
    std::string worldEventResult;
    std::string worldEventResultTone = "ok";
    std::array<std::string, 8> worldEventOptions{};
    int worldEventOptionCount = 0;
    std::array<std::string, 6> worldEventLog{};
    int worldEventLogCount = 0;
    // M31-T7: current region and nearby road-sign hint.
    std::string regionGuide;
    std::string regionTone = "info";
    // M33-F1-T3: current settlement identity, reputation, services and defense.
    std::string settlementGuide;
    std::string settlementTone = "info";
    // M32-T3: strategic warning/active siege summary for the HUD.
    std::string strategicGuide;
    std::string strategicTone = "info";
    std::string siegeStatus;
    std::string siegeTone = "info";
    // M21-F1c：独立战报面板。行文本来自服务器/单机结束提示，客户端只读展示；
    // 固定最多 8 行，避免用 InnerRML 拼接不可信文本。
    bool siegeReportOpen = false;
    std::string siegeReportTitle;
    std::string siegeReportRating;
    std::string siegeReportTone = "info";
    std::string siegeReportHint;
    std::array<std::string, 8> siegeReportLines{};
    std::array<HudModField, kMaxModHudFields> modFields{};
    int modFieldCount = 0;
    bool baseActive = false;
    int baseStorageCount = 0;
    float baseStorageWeight = 0.0f;
    float baseStorageCapacity = 0.0f;
    int baseBeds = 0;
    int baseCampfires = 0;
    int baseWorkbenches = 0;
    int baseDefensesIntact = 0;
    int baseDefensesDamaged = 0;
    int baseFarms = 0;
    int baseFarmGrowing = 0;
    int baseFarmMature = 0;
    int baseFarmDiseased = 0;
    int baseFarmThirsty = 0;
    int baseFarmFertilized = 0;
    int baseGenerators = 0;
    int baseGeneratorsOnline = 0;
    int baseFridges = 0;
    int baseFridgesPowered = 0;
    int baseTraps = 0;
    int baseTrapsArmed = 0;
    int baseFloodlights = 0;
    int baseFloodlightsPowered = 0;
    int baseElectricFences = 0;
    int baseElectricFencesPowered = 0;
    float basePowerFuelMinutes = 0.0f;
    float basePowerLoad = 0.0f;
    bool basePowerLowFuel = false;
    bool basePowerOutage = false;
    std::string baseStatus;
    std::string baseTone = "info";
    bool progressionValid = false;
    int progressionLevel = 1;
    float progressionXp = 0.0f;
    float progressionXpNext = 0.0f;
    int progressionPerkPoints = 0;
    std::array<std::string, 4> progressionSkills{};
    std::string progressionPerks;
    std::array<std::string, 3> progressionChoices{};
    int progressionChoiceCount = 0;
    std::string perkPrompt;
    bool characterOpen = false;
    bool dialogueOpen = false;
    std::string dialogueSpeaker;
    std::string dialogueText;
    std::array<std::string, kMaxDialogueOptions> dialogueOptions{};
    int dialogueOptionCount = 0;
    int dialogueSelected = 0;
    std::string dialogueHint;
    std::string actionHint;
    std::string actionTone = "info";
    std::array<HudSlot, 5> slots{};
    int selectedSlot = 0;
    std::string notice;
    std::string noticeTone = "info";
    float noticeRemaining = 0.0f;
    float noticeDuration = 1.0f;
};

inline bool HudNoticeSuppressed(bool inventoryOpen, bool tradeOpen,
                                bool dialogueOpen) {
    return inventoryOpen || tradeOpen || dialogueOpen;
}

inline std::string TrimHudText(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// 把“守夜结束：损失... | 评级 S | 击杀... | ...”解析成战报行。
// 评级单独返回，其余段按顺序写入 lines；最多 8 行，避免面板溢出。
inline int BuildSiegeReportLines(const std::string& status, std::string* rating,
                                 std::array<std::string, 8>* lines) {
    if (rating != nullptr) {
        rating->clear();
    }
    if (lines != nullptr) {
        lines->fill({});
    }
    if (status.empty()) {
        return 0;
    }
    std::string text = TrimHudText(status);
    constexpr std::string_view kEndPrefix = "守夜结束：";
    if (text.rfind(kEndPrefix, 0) == 0) {
        text.erase(0, kEndPrefix.size());
    }
    constexpr std::string_view kRatingPrefix = "评级 ";
    int count = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(" | ", start);
        const std::size_t length =
            separator == std::string::npos ? std::string::npos
                                          : separator - start;
        std::string segment =
            TrimHudText(std::string_view(text).substr(start, length));
        if (!segment.empty()) {
            if (segment.rfind(kRatingPrefix, 0) == 0) {
                if (rating != nullptr) {
                    *rating = TrimHudText(
                        std::string_view(segment).substr(kRatingPrefix.size()));
                }
            } else if (lines != nullptr &&
                       count < static_cast<int>(lines->size())) {
                (*lines)[static_cast<std::size_t>(count)] = std::move(segment);
                ++count;
            }
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 3;
    }
    return count;
}

// 把 M32-T19 的“守城战报 · 区域 · 守住 · 玩家 贡献+N 经验+N”拆成
// 面板行；最多 8 行，不解析评级。
inline int BuildDefenseReportLines(const std::string& status,
                                   std::array<std::string, 8>* lines) {
    if (lines != nullptr) {
        lines->fill({});
    }
    if (status.empty()) {
        return 0;
    }
    std::string text = TrimHudText(status);
    constexpr std::string_view kPrefix = "守城战报";
    if (text.rfind(kPrefix, 0) == 0) {
        text.erase(0, kPrefix.size());
        if (text.rfind(" · ", 0) == 0) {
            text.erase(0, 3);
        }
    }
    int count = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(" · ", start);
        const std::size_t length =
            separator == std::string::npos ? std::string::npos
                                          : separator - start;
        std::string segment =
            TrimHudText(std::string_view(text).substr(start, length));
        if (!segment.empty() && lines != nullptr &&
            count < static_cast<int>(lines->size())) {
            (*lines)[static_cast<std::size_t>(count)] = std::move(segment);
            ++count;
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 3;
    }
    return count;
}

inline const char* SiegeReportToneForRating(std::string_view rating) {
    if (rating == "S" || rating == "A") {
        return "ok";
    }
    if (rating == "B") {
        return "warn";
    }
    if (rating == "C") {
        return "fail";
    }
    return "info";
}

inline std::string RangedStatusText(const HudState& state,
                                    std::string* tone = nullptr) {
    if (tone != nullptr) {
        *tone = "info";
    }
    if (!state.rangedValid) {
        return {};
    }
    const int magazine = std::max(1, state.rangedMagazine);
    const int loaded = std::clamp(state.rangedLoaded, 0, magazine);
    const int reserve = std::max(0, state.rangedReserve);
    const std::string name =
        state.rangedName.empty() ? "远程武器" : state.rangedName;
    const std::string rounds =
        std::to_string(loaded) + "/" + std::to_string(magazine);
    if (state.rangedReloading) {
        const int percent = static_cast<int>(
            std::lround(std::clamp(state.rangedReloadProgress, 0.0f, 1.0f) * 100.0f));
        if (tone != nullptr) {
            *tone = "warn";
        }
        return name + " 换弹中 " + std::to_string(percent) + "% · " + rounds +
               " · 备弹 " + std::to_string(reserve);
    }
    if (loaded <= 0) {
        if (tone != nullptr) {
            *tone = "fail";
        }
        if (reserve <= 0) {
            return name + " 空仓 · 无备弹 · " + rounds;
        }
        return name + " 空仓 · R 换弹 · " + rounds + " · 备弹 " +
               std::to_string(reserve);
    }
    std::string text = name + " " + rounds + " · 备弹 " + std::to_string(reserve);
    const bool low = loaded <= std::max(1, magazine / 4);
    if (reserve <= 0) {
        text += "（备弹耗尽）";
    }
    if (low) {
        text += " · 弹药不足";
    }
    if (tone != nullptr) {
        *tone = low || reserve <= 0 ? "warn" : "info";
    }
    return text;
}

inline std::string ForageStatusText(const HudState& state,
                                    std::string* tone = nullptr) {
    if (tone != nullptr) {
        *tone = "info";
    }
    if (!state.forageValid) {
        return {};
    }
    const std::string name =
        state.forageName.empty() ? std::string("资源点") : state.forageName;
    if (state.forageSearching) {
        const int percent = static_cast<int>(
            std::lround(std::clamp(state.forageProgress, 0.0f, 1.0f) * 100.0f));
        return "觅食中 · " + name + " " + std::to_string(percent) + "%";
    }
    if (state.forageExhausted) {
        const int minutes = std::max(
            1, static_cast<int>(std::ceil(state.forageRespawnMinutes)));
        if (tone != nullptr) {
            *tone = "warn";
        }
        return "觅食 · " + name + "（已耗尽，约 " + std::to_string(minutes) +
               " 分钟刷新）";
    }
    if (state.forageMissingTool) {
        if (tone != nullptr) {
            *tone = "warn";
        }
        return "可觅食 · " + name + "（需要 " +
               (state.forageToolName.empty() ? std::string("工具")
                                             : state.forageToolName) +
               "）";
    }
    char duration[32];
    std::snprintf(duration, sizeof(duration), "%.1f",
                  static_cast<double>(state.forageSeconds));
    return "可觅食 · " + name + "（" + duration + " 秒）";
}

inline std::string FishingStatusText(const HudState& state,
                                     std::string* tone = nullptr) {
    if (tone != nullptr) {
        *tone = "info";
    }
    if (!state.fishingValid) {
        return {};
    }
    const std::string name =
        state.fishingName.empty() ? std::string("钓鱼点") : state.fishingName;
    if (state.fishingWaiting) {
        const int percent = static_cast<int>(
            std::lround(std::clamp(state.fishingProgress, 0.0f, 1.0f) * 100.0f));
        return "钓鱼中 · " + name + " " + std::to_string(percent) + "%";
    }
    if (state.fishingBiting) {
        char left[32];
        std::snprintf(left, sizeof(left), "%.1f",
                      static_cast<double>(std::max(0.0f, state.fishingBiteRemaining)));
        if (tone != nullptr) {
            *tone = "warn";
        }
        return "咬钩！" + name + "（" + left + " 秒） · E 收杆";
    }
    if (state.fishingMissingRod) {
        if (tone != nullptr) {
            *tone = "warn";
        }
        return "可钓鱼 · " + name + "（需要 " +
               (state.fishingRodName.empty() ? std::string("鱼竿")
                                             : state.fishingRodName) +
               "）";
    }
    if (state.fishingMissingBait) {
        if (tone != nullptr) {
            *tone = "warn";
        }
        return "可钓鱼 · " + name + "（需要 " +
               (state.fishingBaitName.empty() ? std::string("鱼饵")
                                              : state.fishingBaitName) +
               "）";
    }
    char duration[32];
    std::snprintf(duration, sizeof(duration), "%.1f",
                  static_cast<double>(state.fishingSeconds));
    return "可钓鱼 · " + name + "（" + duration + " 秒）";
}

inline std::string BaseStatusText(const HudState& state,
                                  std::string* tone = nullptr) {
    if (!state.baseActive) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return {};
    }
    std::string text =
        "基地 · 仓库 " + std::to_string(state.baseStorageCount);
    if (state.baseStorageCount > 0) {
        char storage[96];
        std::snprintf(storage, sizeof(storage), "（%.1f/%.1fkg）",
                      static_cast<double>(state.baseStorageWeight),
                      static_cast<double>(state.baseStorageCapacity));
        text += storage;
    }
    text += " · 床位 " + std::to_string(state.baseBeds);
    text += " · 篝火 " + std::to_string(state.baseCampfires);
    text += " · 工作台 " + std::to_string(state.baseWorkbenches);
    text += " · 农田 " + std::to_string(state.baseFarms);
    if (state.baseFarms > 0) {
        text += "（生长 " + std::to_string(state.baseFarmGrowing) +
                " / 成熟 " + std::to_string(state.baseFarmMature);
        if (state.baseFarmThirsty > 0) {
            text += " / 缺水 " + std::to_string(state.baseFarmThirsty);
        }
        if (state.baseFarmDiseased > 0) {
            text += " / 病害 " + std::to_string(state.baseFarmDiseased);
        }
        text += "）";
    }
    text += " · 防御 " + std::to_string(state.baseDefensesIntact) +
            " 完好 / " + std::to_string(state.baseDefensesDamaged) +
            " 损坏";
    const bool hasPowerDevices =
        state.baseGenerators > 0 || state.baseFloodlights > 0 ||
        state.baseElectricFences > 0;
    if (hasPowerDevices) {
        if (state.baseGenerators <= 0) {
            text += " · 电力：无发电机";
        } else if (state.basePowerOutage) {
            text += " · 电力：停电";
        } else {
            char power[128];
            std::snprintf(
                power, sizeof(power), " · 电力：燃料 %d 分 · 负载 %.1f · %s",
                static_cast<int>(std::lround(state.basePowerFuelMinutes)),
                static_cast<double>(state.basePowerLoad),
                state.baseGeneratorsOnline > 0 ? "供电中" : "停止");
            text += power;
        }
    }
    if (tone != nullptr) {
        const int defenseCount =
            state.baseDefensesIntact + state.baseDefensesDamaged;
        const bool storageNearlyFull =
            state.baseStorageCapacity > 0.0f &&
            state.baseStorageWeight >= state.baseStorageCapacity * 0.9f;
        if (state.basePowerOutage ||
            (state.baseDefensesDamaged > 0 && defenseCount > 0 &&
             state.baseDefensesDamaged >= state.baseDefensesIntact)) {
            *tone = "fail";
        } else if (state.basePowerLowFuel || state.baseDefensesDamaged > 0 ||
                   state.baseFarmDiseased > 0 || state.baseFarmThirsty > 0 ||
                   storageNearlyFull) {
            *tone = "warn";
        } else {
            *tone = "info";
        }
    }
    return text;
}

}  // namespace zl::ui
