#pragma once

#include <memory>
#include <string>

#include "core/AppState.h"
#include "game/GameContext.h"
#include "game/ServerList.h"
#include "ui/MenuUi.h"
#include "ui/MultiplayerUi.h"

namespace zl::game {

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
    void ShowMultiplayer();
    void HideMultiplayer();
    void RefreshServerList();
    void HandleMultiplayerAction(ui::MultiplayerActionType action,
                                 const ui::MultiplayerMenuState& state);
    bool SaveServerList();
    void SetMultiplayerStatus(std::string text, std::string tone);

    GameContext& m_ctx;
    std::string m_latestSaveId;
    std::unique_ptr<ServerListStore> m_serverList;
    ui::MultiplayerMenuState m_multiplayerState;
};

}  // namespace zl::game
