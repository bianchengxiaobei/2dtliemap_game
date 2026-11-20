#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/MenuUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

// M9-T6：主菜单、暂停菜单与设置页共用一份 RmlUi 文档。
// 面板只负责展示与键位采集；新游戏/继续/退出等动作通过回调交给游戏状态处理。
class MenuPanel {
public:
    static constexpr int kKeyBindingCount = 14;

    MenuPanel();
    ~MenuPanel();

    MenuPanel(const MenuPanel&) = delete;
    MenuPanel& operator=(const MenuPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetActionHandler(MenuActionHandler handler);
    void SetSettingsHandler(MenuSettingsHandler handler);
    void SetSoundHandler(std::function<void()> handler);
    void SetSettings(const MenuSettings& settings);
    void SetCanContinue(bool canContinue);

    void ShowMainMenu(bool canContinue);
    void ShowPauseMenu();
    // M14-T8：单机局结束（撤离成功/死亡）结算页；不复用主菜单/暂停的返回语义。
    void ShowEndScreen(bool victory, const std::string& title,
                       const std::string& subtitle, const std::string& stats,
                       bool showRestart = true);
    void Hide();
    bool IsOpen() const { return m_open; }
    bool IsSettingsOpen() const { return m_settingsOpen; }

    // 键位采集：BeginRebind 由设置页按钮触发；UiSystem 把下一枚物理按键交给 CaptureKey。
    bool IsCapturingKey() const { return m_rebindIndex >= 0; }
    void CancelRebind();

    // 返回 true 表示按键已被消费（ESC 取消采集也算消费）。
    bool CaptureKey(int glfwKeyCode);

    // ESC：取消采集 → 设置页返回 → 暂停菜单恢复；主菜单保持打开。
    bool OnEscape();

    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddClickListener(const char* elementId, std::function<void()> action);
    void AddEventListener(Rml::Element* element, const char* event,
                          std::function<void(Rml::Event&)> action);

    void ShowView(const char* viewId);
    void OpenSettings();
    void CloseSettings();
    void RefreshContinueButton();
    void RefreshSettingsFields();
    void AddVolumeHandler(Rml::Element* element, Rml::Element* valueElement);
    void RefreshKeyButton(int index);
    void BeginRebind(int index);
    void ApplySettings();
    void SetStatus(const std::string& text, const char* tone);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_mainView = nullptr;
    Rml::Element* m_pauseView = nullptr;
    Rml::Element* m_settingsView = nullptr;
    Rml::Element* m_endView = nullptr;
    Rml::Element* m_endKicker = nullptr;
    Rml::Element* m_endTitle = nullptr;
    Rml::Element* m_endSubtitle = nullptr;
    Rml::Element* m_endStats = nullptr;
    Rml::Element* m_endHint = nullptr;
    Rml::Element* m_continueButton = nullptr;
    Rml::Element* m_volumeElement = nullptr;
    Rml::Element* m_volumeValueElement = nullptr;
    Rml::Element* m_musicVolumeElement = nullptr;
    Rml::Element* m_musicVolumeValueElement = nullptr;
    Rml::Element* m_sfxVolumeElement = nullptr;
    Rml::Element* m_sfxVolumeValueElement = nullptr;
    Rml::Element* m_ambientVolumeElement = nullptr;
    Rml::Element* m_ambientVolumeValueElement = nullptr;
    Rml::Element* m_statusElement = nullptr;
    std::array<Rml::Element*, kKeyBindingCount> m_keyButtons{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;

    MenuActionHandler m_actionHandler;
    MenuSettingsHandler m_settingsHandler;
    std::function<void()> m_soundHandler;
    MenuSettings m_settings;
    MenuSettings m_editSettings;
    std::string m_settingsReturnView = "main-menu";
    bool m_continueEnabled = false;
    bool m_open = false;
    bool m_pauseMode = false;
    bool m_endMode = false;
    bool m_settingsOpen = false;
    int m_rebindIndex = -1;
};

}  // namespace zl::ui
