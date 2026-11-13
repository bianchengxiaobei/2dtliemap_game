#include "ui/WorldEventUi.h"

#include <algorithm>
#include <cmath>

#include "game/WorldEventRuntime.h"

namespace zl::ui {
namespace {

void AppendEffect(std::string& text, const std::string& effect) {
    if (effect.empty()) {
        return;
    }
    if (!text.empty()) {
        text += " · ";
    }
    text += effect;
}

void AppendSigned(std::string& text, const char* label, int value) {
    if (value == 0) {
        return;
    }
    AppendEffect(text, std::string(label) + (value > 0 ? "+" : "") +
                           std::to_string(value));
}

}  // namespace

const char* WorldEventStageLabel(game::WorldEventStageKind kind) {
    switch (kind) {
        case game::WorldEventStageKind::Reach:
            return "抵达";
        case game::WorldEventStageKind::Interact:
            return "互动";
        case game::WorldEventStageKind::Kill:
            return "清剿";
        case game::WorldEventStageKind::Loot:
            return "搜刮";
        case game::WorldEventStageKind::Defend:
            return "防守";
        case game::WorldEventStageKind::Choice:
            return "抉择";
    }
    return "阶段";
}

std::string WorldEventCountdownText(std::int64_t minutes) {
    if (minutes <= 0) {
        return "已到期";
    }
    if (minutes < 60) {
        return std::to_string(minutes) + " 分钟";
    }
    const std::int64_t hours = minutes / 60;
    const std::int64_t remainder = minutes % 60;
    if (remainder == 0) {
        return std::to_string(hours) + " 小时";
    }
    return std::to_string(hours) + " 小时 " + std::to_string(remainder) +
           " 分";
}

bool WorldEventCanChoose(const game::WorldEventDef& def,
                         const game::WorldEventInstance& event,
                         float playerX, float playerY) {
    return game::WorldEventCanChoose(def, event, playerX, playerY);
}

std::string WorldEventChoiceLine(
    int index, const game::WorldEventChoiceDef& choice) {
    std::string text = std::to_string(index + 1) + ". " + choice.label;
    std::string effects;
    AppendSigned(effects, "声望", choice.reputation);
    if (choice.rewardXp > 0.0f) {
        AppendEffect(effects,
                     "经验+" +
                         std::to_string(static_cast<int>(
                             std::lround(choice.rewardXp))));
    }
    if (!choice.rewardItem.empty()) {
        const int count = std::max(1, choice.rewardItemCount);
        AppendEffect(effects,
                     "物资 " + choice.rewardItem + "×" +
                         std::to_string(count));
    }
    AppendSigned(effects, "压力", choice.pressure);
    if (!effects.empty()) {
        text += "  [" + effects + "]";
    }
    return text;
}

std::string WorldEventResultLine(
    const game::WorldEventDef& def, const game::WorldEventInstance& event,
    std::string_view choiceLabel, int reputationDelta, float xp,
    int pressureDelta, bool itemDropped, std::string_view item,
    int itemCount) {
    std::string text;
    switch (event.status) {
        case game::WorldEventStatus::Resolved:
            text = "事件完成：" + def.name;
            if (!choiceLabel.empty()) {
                text += " · " + std::string(choiceLabel);
            }
            break;
        case game::WorldEventStatus::Failed:
            text = "事件失败：" + def.name;
            break;
        case game::WorldEventStatus::Expired:
            text = "事件错过：" + def.name;
            break;
        default:
            text = "事件结束：" + def.name;
            break;
    }
    std::string effects;
    AppendSigned(effects, "声望", reputationDelta);
    if (xp > 0.0f) {
        AppendEffect(effects,
                     "经验+" +
                         std::to_string(static_cast<int>(std::lround(xp))));
    }
    if (!item.empty()) {
        AppendEffect(effects,
                     "物资 " + std::string(item) + "×" +
                         std::to_string(std::max(1, itemCount)));
        if (itemDropped) {
            AppendEffect(effects, "背包已满，掉落在地");
        }
    }
    AppendSigned(effects, "区域压力", pressureDelta);
    if (!effects.empty()) {
        text += " · " + effects;
    }
    return text;
}

}  // namespace zl::ui
