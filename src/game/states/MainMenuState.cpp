#include "game/states/MainMenuState.h"

#include <memory>
#include <string>
#include <vector>

#include "core/GameTime.h"
#include "core/Log.h"
#include "game/states/WorldDemoState.h"
#include "platform/Window.h"
#include "save/SaveManager.h"
#include "ui/UiSystem.h"

namespace zl::game {

void MainMenuState::OnEnter() {
    ZL_INFO("ui", "进入主菜单");
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::Paused);
    }

    m_latestSaveId.clear();
    bool canContinue = false;
    if (m_ctx.saveManager != nullptr) {
        std::vector<save::SaveMetadata> saves;
        std::string saveError;
        if (m_ctx.saveManager->List(&saves, &saveError)) {
            if (!saves.empty()) {
                m_latestSaveId = saves.front().id;
                canContinue = true;
                ZL_INFO("save", "主菜单发现存档：{}（{} 个）", m_latestSaveId,
                        saves.size());
            }
        } else {
            ZL_WARN("save", "扫描存档失败：{}", saveError);
        }
    }

    if (m_ctx.ui == nullptr) {
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
        return;
    }

    m_ctx.ui->CloseInventory();
    m_ctx.ui->SetMenuActionHandler(
        [this](ui::MenuActionType action) { HandleMenuAction(action); });
    m_ctx.ui->ShowMainMenu(canContinue);
    if (!m_ctx.ui->IsMenuOpen()) {
        ZL_WARN("ui", "主菜单不可用（assets/ui/menu.rml 加载失败），直接进入世界");
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
    }
}

void MainMenuState::OnExit() {
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetMenuActionHandler({});
    }
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
    }
}

void MainMenuState::OnTick(double tickDt) {
    (void)tickDt;
}

void MainMenuState::OnFrame(double frameDt, double alpha) {
    (void)frameDt;
    (void)alpha;
}

void MainMenuState::HandleMenuAction(ui::MenuActionType action) {
    switch (action) {
        case ui::MenuActionType::NewGame:
            ZL_INFO("ui", "主菜单：新游戏");
            m_ctx.activeSaveId.clear();
            m_ctx.saveWarning.clear();
            if (m_ctx.gameTime != nullptr) {
                m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
            }
            if (m_ctx.states != nullptr) {
                m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
            }
            break;
        case ui::MenuActionType::Quit:
            if (m_ctx.window != nullptr) {
                m_ctx.window->SetShouldClose(true);
            }
            break;
        case ui::MenuActionType::Continue:
            if (!m_latestSaveId.empty() && m_ctx.states != nullptr) {
                ZL_INFO("save", "主菜单：继续存档 {}", m_latestSaveId);
                if (m_ctx.gameTime != nullptr) {
                    m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
                }
                m_ctx.states->SetState(
                    std::make_unique<WorldDemoState>(m_ctx, m_latestSaveId));
            }
            break;
        case ui::MenuActionType::Resume:
        case ui::MenuActionType::ReturnToMainMenu:
            break;
    }
}

}  // namespace zl::game
