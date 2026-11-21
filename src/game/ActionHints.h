#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace zl::game {

struct TrapHintState {
    std::string name;
    bool armed = false;
    bool arming = false;
    float armRemaining = 0.0f;
    int uses = 0;
    int usesMax = 0;
    float cooldownRemaining = 0.0f;
    bool readOnly = false;
};

struct ActionHintState {
    bool bleeding = false;
    bool hasBandage = false;
    bool inPain = false;
    bool hasPainkiller = false;
    float barricadeHp = 0.0f;
    float barricadeMaxHp = 0.0f;
    bool canRepairBarricade = false;
    bool canRepairAll = false;
    int repairWoodCost = 1;
    float repairAmount = 30.0f;
    int repairAllCount = 0;
    int repairAllWood = 0;
    std::string structureName = "木栅栏";
    std::string nearbyDoor;
    bool doorOpen = false;
    bool structureReadOnly = false;
    std::string nearbyContainer;
    std::string nearbyStorage;
    std::string nearbyBed;
    std::string nearbyCampfire;
    std::string campfireState;
    // M23-F3-T4：附近活跃火源与灭火方案（高于未点燃篝火的提示）。
    std::string nearbyFire;
    std::string fireState;
    std::string fireExtinguishItem;
    float fireExtinguishChance = 0.0f;
    bool fireExtinguishBareHands = false;
    std::string nearbyItem;
    std::string nearbyAnimalCorpse;
    std::string animalCorpseToolName;
    bool animalCorpseMissingTool = false;
    float animalCorpseSeconds = 0.0f;
    std::string nearbyForage;
    std::string forageToolName;
    bool forageExhausted = false;
    bool forageMissingTool = false;
    float forageSeconds = 0.0f;
    double forageRespawnMinutes = 0.0;
    std::string nearbyFishing;
    std::string fishingRodName;
    std::string fishingBaitName;
    bool fishingMissingRod = false;
    bool fishingMissingBait = false;
    float fishingSeconds = 0.0f;
    int fishingBaitCount = 0;
    // M23-F1-T8：主手远程武器提示（R 换弹；建造模式 R 旋转优先）。
    bool hasRangedWeapon = false;
    bool rangedEmpty = false;
    bool rangedReloading = false;
    // M34-F2-T2：投掷物优先于枪械；瞄准时显示落点信息。
    bool hasThrowable = false;
    std::string throwableName;
    bool throwAiming = false;
    bool throwClamped = false;
    bool throwWallClipped = false;
    float throwDistance = 0.0f;
};

inline std::string TrapHintText(const TrapHintState& state,
                                std::string* tone = nullptr) {
    if (tone != nullptr) {
        *tone = "info";
    }
    if (state.name.empty()) {
        return {};
    }
    std::string text = state.name + " · " +
                       std::to_string(std::max(0, state.uses));
    if (state.usesMax > 0) {
        text += "/" + std::to_string(std::max(0, state.usesMax));
    }
    text += " 次";
    if (state.uses <= 0) {
        text += " · 已耗尽 · 需拆除重建";
        if (tone != nullptr) {
            *tone = "fail";
        }
    } else if (state.arming) {
        char remaining[32];
        std::snprintf(remaining, sizeof(remaining), "%.1f",
                      static_cast<double>(std::max(0.0f, state.armRemaining)));
        text += " · 布防中 " + std::string(remaining) + "s";
        if (tone != nullptr) {
            *tone = "warn";
        }
    } else if (state.armed) {
        text += " · 已布防";
    } else {
        text += " · 已解除";
        if (tone != nullptr) {
            *tone = "warn";
        }
    }
    if (state.cooldownRemaining > 0.0f) {
        char cooldown[32];
        std::snprintf(cooldown, sizeof(cooldown), "%.1f",
                      static_cast<double>(state.cooldownRemaining));
        text += " · 冷却 " + std::string(cooldown) + "s";
        if (tone != nullptr && state.uses > 0) {
            *tone = "warn";
        }
    }
    if (state.readOnly) {
        text += " · 仅所有者可操作";
        if (tone != nullptr && state.uses > 0) {
            *tone = "warn";
        }
    } else if (state.uses > 0) {
        if (state.armed) {
            text += " · E 解除";
        } else if (state.arming) {
            text += " · E 立即布防";
        } else {
            text += " · E 布防";
        }
        text += " · X 拆除（建造模式）";
    }
    return text;
}

inline std::string ActionHintText(const ActionHintState& state,
                                  std::string* tone = nullptr) {
    const auto repairAllSuffix = [&state]() {
        if (!state.canRepairAll || state.repairAllCount <= 0) {
            return std::string();
        }
        return " · J 维修全部（" + std::to_string(state.repairAllCount) +
               " 个/" + std::to_string(state.repairAllWood) + " 木材）";
    };
    if (state.bleeding) {
        if (tone != nullptr) {
            *tone = "fail";
        }
        return state.hasBandage ? "流血中：按 I 打开背包使用绷带"
                                : "流血中：寻找绷带或破布止血";
    }
    if (state.inPain) {
        if (tone != nullptr) {
            *tone = "warn";
        }
        return state.hasPainkiller ? "疼痛中：按 I 打开背包使用止痛药"
                                   : "疼痛中：寻找止痛药";
    }
    if (state.throwAiming && state.hasThrowable) {
        if (tone != nullptr) {
            *tone = "info";
        }
        char distance[32];
        std::snprintf(distance, sizeof(distance), "%.1f",
                      static_cast<double>(std::max(0.0f, state.throwDistance)));
        return "松开 V 投掷：" +
               (state.throwableName.empty() ? std::string("投掷物")
                                            : state.throwableName) +
               " · 落点 " + distance + " 格" +
               (state.throwClamped ? "（已夹到最大射程）" : "") +
               (state.throwWallClipped ? "（墙前落点）" : "") +
               " · ESC 取消";
    }
    if (!state.nearbyDoor.empty()) {
        if (tone != nullptr) {
            *tone = "info";
        }
        std::string text =
            std::string(state.structureReadOnly ? "无法" : "按 E ") +
            (state.doorOpen ? "关门：" : "开门：") + state.nearbyDoor;
        if (!state.doorOpen && state.barricadeMaxHp > 0.0f) {
            text += "（" +
                    std::to_string(
                        static_cast<int>(std::lround(state.barricadeHp))) +
                    "/" +
                    std::to_string(
                        static_cast<int>(std::lround(state.barricadeMaxHp))) +
                    "）";
        }
        if (state.canRepairBarricade) {
            text += " · H 维修（" + std::to_string(state.repairWoodCost) +
                    " 木材 +" +
                    std::to_string(
                        static_cast<int>(std::lround(state.repairAmount))) +
                    "）";
        }
        if (state.structureReadOnly) {
            text += "（仅所有者）";
        }
        return text + repairAllSuffix();
    }
    if (state.barricadeMaxHp > 0.0f) {
        const int hp = static_cast<int>(std::lround(state.barricadeHp));
        const int maxHp = static_cast<int>(std::lround(state.barricadeMaxHp));
        if (tone != nullptr) {
            *tone = state.barricadeHp <= state.barricadeMaxHp * 0.25f
                        ? "fail"
                        : state.barricadeHp <= state.barricadeMaxHp * 0.6f ? "warn"
                                                                          : "info";
        }
        std::string text =
            state.structureName + " " + std::to_string(hp) + "/" +
            std::to_string(maxHp);
        if (state.structureReadOnly) {
            text += " · 仅所有者";
        } else if (state.canRepairBarricade) {
            text += " · H 维修（" + std::to_string(state.repairWoodCost) +
                    " 木材 +" +
                    std::to_string(
                        static_cast<int>(std::lround(state.repairAmount))) +
                    "）";
        }
        return text + repairAllSuffix();
    }
    if (!state.nearbyBed.empty()) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return "按 E 睡觉：" + state.nearbyBed;
    }
    if (!state.nearbyFire.empty()) {
        if (tone != nullptr) {
            *tone = state.fireState == "阴燃" ? "warn" : "fail";
        }
        const int chance = static_cast<int>(
            std::lround(std::clamp(state.fireExtinguishChance, 0.0f, 1.0f) *
                        100.0f));
        const std::string method =
            state.fireExtinguishBareHands
                ? std::string("徒手拍打")
                : "用" +
                      (state.fireExtinguishItem.empty()
                           ? std::string("灭火物品")
                           : state.fireExtinguishItem);
        if (chance <= 0) {
            return "火灾：" + state.nearbyFire + state.fireState +
                   " · 没有可用灭火手段";
        }
        return "火灾：" + state.nearbyFire + state.fireState + " · 按 E " +
               method + "（" + std::to_string(chance) + "%）" +
               (state.fireExtinguishBareHands ? "，会受伤" : "");
    }
    if (!state.nearbyCampfire.empty()) {
        if (!state.campfireState.empty()) {
            if (tone != nullptr) {
                *tone = state.campfireState == "燃烧" ? "warn" : "info";
            }
            return "篝火" + state.campfireState + " · 按 E 熄灭";
        }
        if (tone != nullptr) {
            *tone = "info";
        }
        return "按 E 点燃：" + state.nearbyCampfire;
    }
    if (!state.nearbyStorage.empty()) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return "按 I 打开仓库：" + state.nearbyStorage;
    }
    if (!state.nearbyContainer.empty()) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return "按 I 搜刮 " + state.nearbyContainer;
    }
    if (!state.nearbyAnimalCorpse.empty()) {
        if (state.animalCorpseMissingTool) {
            if (tone != nullptr) {
                *tone = "warn";
            }
            return "需要" +
                   (state.animalCorpseToolName.empty()
                        ? std::string("工具")
                        : state.animalCorpseToolName) +
                   "：无法剥取" + state.nearbyAnimalCorpse;
        }
        if (tone != nullptr) {
            *tone = "info";
        }
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.1f",
                      state.animalCorpseSeconds);
        return "按 E 剥取" + state.nearbyAnimalCorpse + "（" + duration +
               " 秒）";
    }
    if (!state.nearbyItem.empty()) {
        if (tone != nullptr) {
            *tone = "info";
        }
        return "按 F 拾取 " + state.nearbyItem;
    }
    if (!state.nearbyFishing.empty()) {
        if (state.fishingMissingRod) {
            if (tone != nullptr) {
                *tone = "warn";
            }
            return "需要" +
                   (state.fishingRodName.empty() ? std::string("鱼竿")
                                                 : state.fishingRodName) +
                   "：无法钓鱼 " + state.nearbyFishing;
        }
        if (state.fishingMissingBait) {
            if (tone != nullptr) {
                *tone = "warn";
            }
            return "需要" +
                   (state.fishingBaitName.empty() ? std::string("鱼饵")
                                                  : state.fishingBaitName) +
                   "：无法钓鱼 " + state.nearbyFishing;
        }
        if (tone != nullptr) {
            *tone = "info";
        }
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.1f",
                      state.fishingSeconds);
        return "按 E 钓鱼：" + state.nearbyFishing + "（" + duration + " 秒）";
    }
    if (!state.nearbyForage.empty()) {
        if (state.forageMissingTool) {
            if (tone != nullptr) {
                *tone = "warn";
            }
            return "需要" +
                   (state.forageToolName.empty() ? std::string("工具")
                                                 : state.forageToolName) +
                   "：无法采集 " + state.nearbyForage;
        }
        if (state.forageExhausted) {
            if (tone != nullptr) {
                *tone = "warn";
            }
            const int minutes = std::max(
                1, static_cast<int>(std::ceil(state.forageRespawnMinutes)));
            return "已耗尽：" + state.nearbyForage + "（约 " +
                   std::to_string(minutes) + " 分钟后刷新）";
        }
        if (tone != nullptr) {
            *tone = "info";
        }
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.1f", state.forageSeconds);
        return "按 E 觅食：" + state.nearbyForage + "（" + duration + " 秒）";
    }
    if (tone != nullptr) {
        *tone = "info";
    }
    if (state.hasThrowable) {
        return "V 投掷：" +
               (state.throwableName.empty() ? std::string("投掷物")
                                            : state.throwableName) +
               " · WASD 移动 · B 建造 · I 背包";
    }
    if (state.hasRangedWeapon) {
        if (state.rangedReloading) {
            return "换弹中 · WASD 移动 · B 建造 · I 背包";
        }
        if (state.rangedEmpty) {
            return "空仓：按 R 换弹 · WASD 移动 · V 开火 · B 建造 · I 背包";
        }
        return "V 开火 · R 换弹 · WASD 移动 · B 建造 · I 背包";
    }
    return "WASD 移动 · 左键点地 · SPACE 轻击 · LSHIFT 重击 · LCTRL 格挡 · Z 闪避 · B 建造 · I 背包";
}

}  // namespace zl::game
