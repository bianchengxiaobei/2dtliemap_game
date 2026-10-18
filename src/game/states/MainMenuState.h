#pragma once

#include <string>

#include "core/AppState.h"
#include "game/GameContext.h"
#include "ui/MenuUi.h"

namespace zl::game {

// M9-T6：启动主菜单。新游戏通过 AppState 切换到 WorldDemoState；
// 暂停菜单与“返回主菜单”是 UI 覆盖层，保留当前世界状态以便“继续”。
class MainMenuState final : public core::AppState {
public:
    explicit MainMenuState(GameContext& context) : m_ctx(context) {}

    const char* Name() const override { return "MainMenu"; }

    void OnEnter() override;
    void OnExit() override;
    void OnFrame(double frameDt, double alpha) override;
    void OnTick(double tickDt) override;

private:
    void HandleMenuAction(ui::MenuActionType action);

    GameContext& m_ctx;
    std::string m_latestSaveId;
};

}  // namespace zl::game
