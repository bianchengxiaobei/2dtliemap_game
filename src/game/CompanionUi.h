#pragma once

#include "game/Companion.h"
#include "ui/CompanionUi.h"

namespace zl::ecs {
struct Health;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

struct CompanionUiInput {
    const CompanionDef* def = nullptr;
    const CompanionState* state = nullptr;
    const ecs::Health* health = nullptr;
    const items::ItemRegistry* items = nullptr;
    float ownerDistance = 0.0f;
    bool stuck = false;
    bool networkSummary = false;
};

// M33-F3-T7：把同伴运行态转换成可测试的只读 UI 文本/告警。
ui::CompanionUiState BuildCompanionUi(const CompanionUiInput& input);

}  // namespace zl::game
