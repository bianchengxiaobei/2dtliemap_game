#include "game/CompanionUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

std::string ItemName(const items::ItemRegistry* items,
                     std::string_view defId) {
    if (items != nullptr) {
        if (const items::ItemDef* def = items->Find(defId); def != nullptr) {
            return def->name;
        }
    }
    return std::string(defId);
}

std::string EquipmentText(const CompanionUiInput& input) {
    if (input.state == nullptr) {
        return "未装备";
    }
    const auto& slots = input.state->equipment.slots;
    const std::size_t primary = static_cast<std::size_t>(
        ecs::EquipSlot::PrimaryHand);
    const std::size_t body = static_cast<std::size_t>(ecs::EquipSlot::Body);
    const bool hasPrimary =
        primary < slots.size() && slots[primary].has_value();
    const bool hasBody = body < slots.size() && slots[body].has_value();
    std::string text = "主手 ";
    text += hasPrimary ? ItemName(input.items, slots[primary]->defId)
                       : std::string("空");
    text += " · 躯干 ";
    text += hasBody ? ItemName(input.items, slots[body]->defId)
                    : std::string("空");
    return text;
}

std::string InventoryText(const CompanionUiInput& input) {
    if (input.networkSummary) {
        return "联机只同步装备摘要，背包未同步";
    }
    if (input.state == nullptr || input.state->inventory.items.empty()) {
        return "空";
    }
    std::string text;
    constexpr std::size_t kMaxRows = 8;
    const std::size_t rows =
        std::min(input.state->inventory.items.size(), kMaxRows);
    for (std::size_t i = 0; i < rows; ++i) {
        const ecs::ItemInstance& item = input.state->inventory.items[i];
        if (!text.empty()) {
            text += " · ";
        }
        text += ItemName(input.items, item.defId);
        if (item.count > 1) {
            text += "×" + std::to_string(item.count);
        }
    }
    if (input.state->inventory.items.size() > rows) {
        text += " 等 " +
                std::to_string(input.state->inventory.items.size() - rows) +
                " 堆";
    }
    const float weightKg =
        input.items != nullptr
            ? items::TotalWeight(input.state->inventory, *input.items)
            : 0.0f;
    char weight[64];
    std::snprintf(weight, sizeof(weight), "（%.1f/%.1fkg）",
                  static_cast<double>(weightKg),
                  static_cast<double>(input.state->inventory.capacityKg));
    text += weight;
    return text;
}

std::string QuestText(const CompanionUiInput& input) {
    if (input.def == nullptr || input.state == nullptr) {
        return "个人委托：无";
    }
    const CompanionQuestStatus status =
        CompanionQuestStatusFor(*input.def, *input.state);
    const CompanionQuestStageDef* stage =
        CurrentCompanionQuestStage(*input.def, *input.state);
    if (stage == nullptr) {
        return status == CompanionQuestStatus::Completed
                   ? "个人委托：已完成"
                   : "个人委托：无";
    }
    switch (status) {
        case CompanionQuestStatus::Available:
            return "个人委托：" + stage->name + "（可接取）";
        case CompanionQuestStatus::Active:
            return "个人委托：" + stage->name + " " +
                   std::to_string(input.state->personalProgress) + "/" +
                   std::to_string(std::max(1, stage->objective.count));
        case CompanionQuestStatus::Failed:
            return "个人委托：" + stage->name + "（失败，可重试）";
        case CompanionQuestStatus::Completed:
            return "个人委托：" + stage->name + "（已完成）";
        case CompanionQuestStatus::Locked:
            return "个人委托：" + stage->name + "（信任不足）";
    }
    return "个人委托：无";
}

const char* CommandLabel(CompanionCommand command) {
    switch (command) {
        case CompanionCommand::Follow:
            return "跟随";
        case CompanionCommand::Stay:
            return "停留";
        case CompanionCommand::Retreat:
            return "撤退";
    }
    return "跟随";
}

std::string StatusText(const CompanionState& state) {
    if (state.dead) {
        return "死亡";
    }
    if (state.downed) {
        char text[64];
        std::snprintf(text, sizeof(text), "倒地 · 剩余 %.0f 秒",
                      static_cast<double>(
                          std::max(0.0f, state.downedRemaining)));
        return text;
    }
    if (state.standby) {
        return "聚落待命";
    }
    return CommandLabel(state.command);
}

void SetAlert(ui::CompanionUiState* ui, std::string text,
              std::string tone) {
    if (ui == nullptr || ui->alert.empty()) {
        if (ui != nullptr) {
            ui->alert = std::move(text);
            ui->alertTone = std::move(tone);
        }
    }
}

}  // namespace

ui::CompanionUiState BuildCompanionUi(const CompanionUiInput& input) {
    ui::CompanionUiState ui;
    if (input.def == nullptr || input.state == nullptr ||
        input.health == nullptr || !std::isfinite(input.health->max) ||
        input.health->max <= 0.0f) {
        return ui;
    }
    const CompanionState& state = *input.state;
    const ecs::Health& health = *input.health;
    const float current =
        std::clamp(std::isfinite(health.current) ? health.current : 0.0f,
                   0.0f, health.max);
    const int percent = static_cast<int>(std::lround(
        std::clamp(current / health.max, 0.0f, 1.0f) * 100.0f));

    ui.valid = true;
    ui.name = input.def->name.empty() ? input.def->id : input.def->name;
    ui.healthCurrent = current;
    ui.healthMax = health.max;
    ui.loyalty = std::clamp(state.loyalty, 0, 100);
    ui.downed = state.downed;
    ui.dead = state.dead;
    ui.standby = state.standby;
    ui.lowHealth = !state.dead && !state.downed && current > 0.0f &&
                   current <= health.max * 0.30f;
    ui.away = input.ownerDistance > 18.0f;
    ui.stuck = input.stuck && !state.dead && !state.downed &&
               !state.standby;
    ui.canCommand = !state.dead && !state.downed && !state.standby;
    ui.canGift = !state.dead && !state.downed;
    ui.canRescue = state.downed && !state.dead;

    ui.health = "生命 " + std::to_string(static_cast<int>(std::lround(current))) +
                "/" + std::to_string(static_cast<int>(std::lround(health.max))) +
                "（" + std::to_string(percent) + "%）";
    ui.status = StatusText(state);
    ui.trust = "信任值 " + std::to_string(ui.loyalty) + "/100 · " +
               CompanionTrustTierLabel(
                   CompanionTrustTierForScore(ui.loyalty));
    ui.equipment = EquipmentText(input);
    ui.bag = InventoryText(input);
    ui.quest = QuestText(input);

    if (ui.dead) {
        SetAlert(&ui, "同伴已死亡 · 无法救援", "fail");
    } else if (ui.downed) {
        char text[96];
        std::snprintf(text, sizeof(text),
                      "同伴倒地 · %.0f 秒 · 靠近按 E 救援",
                      static_cast<double>(
                          std::max(0.0f, state.downedRemaining)));
        SetAlert(&ui, text, "fail");
    } else if (ui.lowHealth) {
        char text[96];
        std::snprintf(text, sizeof(text),
                      "同伴低血 %d%% · 按 P 选择撤退", percent);
        SetAlert(&ui, text, "warn");
    } else if (ui.away) {
        char text[96];
        std::snprintf(text, sizeof(text), "同伴远离 %.0f 格 · 按 P 查看",
                      static_cast<double>(input.ownerDistance));
        SetAlert(&ui, text, "warn");
    } else if (ui.stuck) {
        SetAlert(&ui, "同伴路径受阻 · 按 P 选择停留再跟随", "warn");
    } else if (ui.standby) {
        SetAlert(&ui, "同伴在聚落待命 · 前往所属聚落召回", "info");
    }

    if (ui.canRescue) {
        ui.hint = "E 救援 · P/ESC 关闭";
    } else if (ui.canCommand) {
        ui.hint = input.networkSummary
                      ? "1 跟随 · 2 停留 · 3 撤退 · 4 赠礼快捷栏物品 · E 交谈 · P/ESC 关闭"
                      : "1 跟随 · 2 停留 · 3 撤退 · 4 赠礼快捷栏物品 · E 交谈 · P/ESC 关闭";
    } else {
        ui.hint = "P/ESC 关闭";
    }
    return ui;
}

}  // namespace zl::game
