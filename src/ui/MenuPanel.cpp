#include "ui/MenuPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include "core/Log.h"
#include "platform/KeyNames.h"

namespace zl::ui {
namespace {

class EventFunctionListener final : public Rml::EventListener {
public:
    explicit EventFunctionListener(std::function<void(Rml::Event&)> action)
        : m_action(std::move(action)) {}

    void ProcessEvent(Rml::Event& event) override {
        if (m_action) {
            m_action(event);
        }
    }

private:
    std::function<void(Rml::Event&)> m_action;
};

void SetText(Rml::Element* element, const std::string& text) {
    if (element != nullptr) {
        element->SetInnerRML(text);
    }
}

void SetVisible(Rml::Element* element, bool visible) {
    if (element != nullptr) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

bool IsModifierKey(int key) {
    return key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT ||
           key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL ||
           key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
           key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER;
}

static_assert(sizeof(kMenuBindingDefs) / sizeof(kMenuBindingDefs[0]) ==
                  MenuPanel::kKeyBindingCount,
              "MenuPanel 键位行数量与 MenuUi 定义不一致");

}  // namespace

MenuPanel::MenuPanel() = default;

MenuPanel::~MenuPanel() { Shutdown(); }

bool MenuPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "菜单文档加载失败：{}", path);
        m_context = nullptr;
        return false;
    }

    m_mainView = m_document->GetElementById("main-menu");
    m_pauseView = m_document->GetElementById("pause-menu");
    m_settingsView = m_document->GetElementById("settings-menu");
    m_endView = m_document->GetElementById("end-menu");
    m_endKicker = m_document->GetElementById("end-kicker");
    m_endTitle = m_document->GetElementById("end-title");
    m_endSubtitle = m_document->GetElementById("end-subtitle");
    m_endStats = m_document->GetElementById("end-stats");
    m_continueButton = m_document->GetElementById("menu-continue");
    m_volumeElement = m_document->GetElementById("setting-volume");
    m_volumeValueElement = m_document->GetElementById("setting-volume-value");
    m_musicVolumeElement = m_document->GetElementById("setting-music-volume");
    m_musicVolumeValueElement = m_document->GetElementById("setting-music-volume-value");
    m_sfxVolumeElement = m_document->GetElementById("setting-sfx-volume");
    m_sfxVolumeValueElement = m_document->GetElementById("setting-sfx-volume-value");
    m_ambientVolumeElement = m_document->GetElementById("setting-ambient-volume");
    m_ambientVolumeValueElement = m_document->GetElementById("setting-ambient-volume-value");
    m_statusElement = m_document->GetElementById("settings-status");
    for (int i = 0; i < kKeyBindingCount; ++i) {
        m_keyButtons[static_cast<std::size_t>(i)] =
            m_document->GetElementById(kMenuBindingDefs[i].elementId);
        if (m_keyButtons[static_cast<std::size_t>(i)] == nullptr) {
            ZL_WARN("ui", "菜单键位按钮缺失：{}", kMenuBindingDefs[i].elementId);
        }
    }

    AddClickListener("menu-new-game", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::NewGame);
        }
    });
    AddClickListener("menu-continue", [this] {
        if (m_continueEnabled && m_actionHandler) {
            m_actionHandler(MenuActionType::Continue);
        }
    });
    AddClickListener("menu-multiplayer", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::Multiplayer);
        }
    });
    AddClickListener("menu-settings", [this] { OpenSettings(); });
    AddClickListener("menu-quit", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::Quit);
        }
    });

    AddClickListener("pause-resume", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::Resume);
        }
    });
    AddClickListener("pause-settings", [this] { OpenSettings(); });
    AddClickListener("pause-main-menu", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::ReturnToMainMenu);
        }
    });
    AddClickListener("pause-quit", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::Quit);
        }
    });

    AddClickListener("end-restart", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::NewGame);
        }
    });
    AddClickListener("end-main-menu", [this] {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::ReturnToMainMenu);
        }
    });

    AddClickListener("settings-back", [this] { CloseSettings(); });
    AddClickListener("settings-apply", [this] { ApplySettings(); });

    AddVolumeHandler(m_volumeElement, m_volumeValueElement);
    AddVolumeHandler(m_musicVolumeElement, m_musicVolumeValueElement);
    AddVolumeHandler(m_sfxVolumeElement, m_sfxVolumeValueElement);
    AddVolumeHandler(m_ambientVolumeElement, m_ambientVolumeValueElement);

    for (int i = 0; i < kKeyBindingCount; ++i) {
        Rml::Element* button = m_keyButtons[static_cast<std::size_t>(i)];
        if (button == nullptr) {
            continue;
        }
        AddEventListener(button, "click",
                        [this, i](Rml::Event&) { BeginRebind(i); });
    }

    SetVisible(m_mainView, false);
    SetVisible(m_pauseView, false);
    SetVisible(m_settingsView, false);
    SetVisible(m_endView, false);
    m_document->Hide();
    m_open = false;
    return true;
}

void MenuPanel::AddVolumeHandler(Rml::Element* element, Rml::Element* valueElement) {
    if (element == nullptr || valueElement == nullptr) {
        return;
    }
    AddEventListener(element, "change", [element, valueElement](Rml::Event&) {
        auto* input = static_cast<Rml::ElementFormControlInput*>(element);
        const double value = std::strtod(input->GetValue().c_str(), nullptr);
        valueElement->SetInnerRML(std::to_string(static_cast<int>(std::lround(value))));
    });
}

void MenuPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event, binding.listener.get());
        }
    }
    m_listeners.clear();
    m_document = nullptr;
    m_context = nullptr;
    m_mainView = nullptr;
    m_pauseView = nullptr;
    m_settingsView = nullptr;
    m_endView = nullptr;
    m_endKicker = nullptr;
    m_endTitle = nullptr;
    m_endSubtitle = nullptr;
    m_endStats = nullptr;
    m_continueButton = nullptr;
    m_volumeElement = nullptr;
    m_volumeValueElement = nullptr;
    m_musicVolumeElement = nullptr;
    m_musicVolumeValueElement = nullptr;
    m_sfxVolumeElement = nullptr;
    m_sfxVolumeValueElement = nullptr;
    m_ambientVolumeElement = nullptr;
    m_ambientVolumeValueElement = nullptr;
    m_statusElement = nullptr;
    m_keyButtons.fill(nullptr);
    m_actionHandler = nullptr;
    m_settingsHandler = nullptr;
    m_soundHandler = nullptr;
    m_settings = MenuSettings{};
    m_editSettings = MenuSettings{};
    m_continueEnabled = false;
    m_open = false;
    m_pauseMode = false;
    m_endMode = false;
    m_settingsOpen = false;
    m_rebindIndex = -1;
}

void MenuPanel::SetActionHandler(MenuActionHandler handler) {
    m_actionHandler = std::move(handler);
}

void MenuPanel::SetSettingsHandler(MenuSettingsHandler handler) {
    m_settingsHandler = std::move(handler);
}

void MenuPanel::SetSoundHandler(std::function<void()> handler) {
    m_soundHandler = std::move(handler);
}

void MenuPanel::SetSettings(const MenuSettings& settings) {
    m_settings = settings;
    m_editSettings = settings;
    RefreshSettingsFields();
}

void MenuPanel::SetCanContinue(bool canContinue) {
    m_continueEnabled = canContinue;
    RefreshContinueButton();
}

void MenuPanel::ShowMainMenu(bool canContinue) {
    if (m_document == nullptr) {
        return;
    }
    m_pauseMode = false;
    m_endMode = false;
    m_settingsOpen = false;
    m_rebindIndex = -1;
    m_continueEnabled = canContinue;
    RefreshContinueButton();
    m_document->Show();
    m_document->PullToFront();
    ShowView("main-menu");
    m_open = true;
}

void MenuPanel::ShowPauseMenu() {
    if (m_document == nullptr) {
        return;
    }
    m_pauseMode = true;
    m_endMode = false;
    m_settingsOpen = false;
    m_rebindIndex = -1;
    m_document->Show();
    m_document->PullToFront();
    ShowView("pause-menu");
    m_open = true;
}

void MenuPanel::ShowEndScreen(bool victory, const std::string& title,
                              const std::string& subtitle, const std::string& stats) {
    if (m_document == nullptr) {
        return;
    }
    m_pauseMode = false;
    m_endMode = true;
    m_settingsOpen = false;
    m_rebindIndex = -1;
    m_document->Show();
    m_document->PullToFront();
    if (m_endView != nullptr) {
        m_endView->SetClass("victory", victory);
        m_endView->SetClass("death", !victory);
    }
    SetText(m_endKicker, victory ? "RUN COMPLETE" : "YOU DIED");
    SetText(m_endTitle, title);
    SetText(m_endSubtitle, subtitle);
    SetText(m_endStats, stats);
    ShowView("end-menu");
    m_open = true;
}

void MenuPanel::Hide() {
    if (m_context != nullptr) {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
    }
    if (m_document != nullptr) {
        m_document->Hide();
    }
    m_open = false;
    m_endMode = false;
    m_settingsOpen = false;
    m_rebindIndex = -1;
}

void MenuPanel::ShowView(const char* viewId) {
    const std::string id(viewId);
    SetVisible(m_mainView, id == "main-menu");
    SetVisible(m_pauseView, id == "pause-menu");
    SetVisible(m_settingsView, id == "settings-menu");
    SetVisible(m_endView, id == "end-menu");
    m_settingsOpen = id == "settings-menu";
    if (m_settingsOpen) {
        m_editSettings = m_settings;
        RefreshSettingsFields();
    }
}

void MenuPanel::OpenSettings() {
    if (m_document == nullptr) {
        return;
    }
    m_settingsReturnView = m_pauseMode ? "pause-menu" : "main-menu";
    m_rebindIndex = -1;
    if (m_document != nullptr) {
        m_document->Show();
        m_document->PullToFront();
    }
    ShowView("settings-menu");
    SetStatus("键位与音量修改后按“应用”保存", "info");
    m_open = true;
}

void MenuPanel::CloseSettings() {
    if (m_rebindIndex >= 0) {
        CancelRebind();
    }
    m_editSettings = m_settings;
    m_settingsOpen = false;
    ShowView(m_settingsReturnView.c_str());
}

void MenuPanel::RefreshContinueButton() {
    if (m_continueButton == nullptr) {
        return;
    }
    m_continueButton->SetClass("disabled", !m_continueEnabled);
    m_continueButton->SetInnerRML(m_continueEnabled ? "继续" : "继续（暂无存档）");
}

void MenuPanel::RefreshSettingsFields() {
    if (m_volumeElement != nullptr) {
        auto* input = static_cast<Rml::ElementFormControlInput*>(m_volumeElement);
        input->SetValue(std::to_string(std::clamp(m_editSettings.volume, 0, 100)));
    }
    if (m_volumeValueElement != nullptr) {
        m_volumeValueElement->SetInnerRML(
            std::to_string(std::clamp(m_editSettings.volume, 0, 100)));
    }
    if (m_musicVolumeElement != nullptr) {
        auto* input = static_cast<Rml::ElementFormControlInput*>(m_musicVolumeElement);
        input->SetValue(std::to_string(std::clamp(m_editSettings.musicVolume, 0, 100)));
    }
    if (m_musicVolumeValueElement != nullptr) {
        m_musicVolumeValueElement->SetInnerRML(
            std::to_string(std::clamp(m_editSettings.musicVolume, 0, 100)));
    }
    if (m_sfxVolumeElement != nullptr) {
        auto* input = static_cast<Rml::ElementFormControlInput*>(m_sfxVolumeElement);
        input->SetValue(std::to_string(std::clamp(m_editSettings.sfxVolume, 0, 100)));
    }
    if (m_sfxVolumeValueElement != nullptr) {
        m_sfxVolumeValueElement->SetInnerRML(
            std::to_string(std::clamp(m_editSettings.sfxVolume, 0, 100)));
    }
    if (m_ambientVolumeElement != nullptr) {
        auto* input = static_cast<Rml::ElementFormControlInput*>(m_ambientVolumeElement);
        input->SetValue(std::to_string(std::clamp(m_editSettings.ambientVolume, 0, 100)));
    }
    if (m_ambientVolumeValueElement != nullptr) {
        m_ambientVolumeValueElement->SetInnerRML(
            std::to_string(std::clamp(m_editSettings.ambientVolume, 0, 100)));
    }
    for (int i = 0; i < kKeyBindingCount; ++i) {
        RefreshKeyButton(i);
    }
}

void MenuPanel::RefreshKeyButton(int index) {
    if (index < 0 || index >= kKeyBindingCount) {
        return;
    }
    Rml::Element* button = m_keyButtons[static_cast<std::size_t>(index)];
    if (button == nullptr) {
        return;
    }
    if (index < static_cast<int>(m_editSettings.bindings.size())) {
        SetText(button, m_editSettings.bindings[static_cast<std::size_t>(index)].key);
    } else {
        SetText(button, kMenuBindingDefs[index].defaultKey);
    }
    button->SetClass("capturing", index == m_rebindIndex);
}

void MenuPanel::BeginRebind(int index) {
    if (!m_settingsOpen || index < 0 || index >= kKeyBindingCount) {
        return;
    }
    m_rebindIndex = index;
    for (int i = 0; i < kKeyBindingCount; ++i) {
        RefreshKeyButton(i);
    }
    SetStatus("请按下新按键，ESC 取消", "info");
}

void MenuPanel::CancelRebind() {
    if (m_rebindIndex < 0) {
        return;
    }
    m_rebindIndex = -1;
    for (int i = 0; i < kKeyBindingCount; ++i) {
        RefreshKeyButton(i);
    }
    SetStatus("已取消改键", "info");
}

bool MenuPanel::CaptureKey(int glfwKeyCode) {
    if (m_rebindIndex < 0) {
        return false;
    }
    if (glfwKeyCode == GLFW_KEY_ESCAPE) {
        CancelRebind();
        return true;
    }
    if (IsModifierKey(glfwKeyCode)) {
        SetStatus("修饰键不可单独作为操作键", "fail");
        return true;
    }
    const std::string key = platform::KeyNameFromCode(glfwKeyCode);
    if (key.empty() || platform::KeyCodeFromName(key) < 0) {
        SetStatus("该按键不支持绑定", "fail");
        return true;
    }
    if (m_rebindIndex < static_cast<int>(m_editSettings.bindings.size())) {
        m_editSettings.bindings[static_cast<std::size_t>(m_rebindIndex)].key = key;
    }
    const int index = m_rebindIndex;
    m_rebindIndex = -1;
    RefreshKeyButton(index);
    SetStatus("按键已修改，按“应用”保存", "info");
    return true;
}

void MenuPanel::ApplySettings() {
    if (!m_settingsOpen) {
        return;
    }
    if (m_rebindIndex >= 0) {
        SetStatus("请先完成改键，或按 ESC 取消", "fail");
        return;
    }
    auto readVolume = [](Rml::Element* element, int* out) {
        if (element == nullptr) {
            return;
        }
        auto* input = static_cast<Rml::ElementFormControlInput*>(element);
        const double value = std::strtod(input->GetValue().c_str(), nullptr);
        *out = std::clamp(static_cast<int>(std::lround(value)), 0, 100);
    };
    readVolume(m_volumeElement, &m_editSettings.volume);
    readVolume(m_musicVolumeElement, &m_editSettings.musicVolume);
    readVolume(m_sfxVolumeElement, &m_editSettings.sfxVolume);
    readVolume(m_ambientVolumeElement, &m_editSettings.ambientVolume);

    std::set<std::string> used;
    for (const MenuKeyBinding& binding : m_editSettings.bindings) {
        if (binding.key.empty() || platform::KeyCodeFromName(binding.key) < 0) {
            SetStatus("键位无效：" + binding.label, "fail");
            return;
        }
        const std::string canonical = platform::KeyNameFromCode(
            platform::KeyCodeFromName(binding.key));
        if (canonical.empty()) {
            SetStatus("键位无效：" + binding.label, "fail");
            return;
        }
        if (!used.insert(canonical).second) {
            SetStatus("键位重复：" + canonical + "（" + binding.label + "）", "fail");
            return;
        }
    }

    if (!m_settingsHandler) {
        SetStatus("设置保存不可用", "fail");
        return;
    }
    std::string error;
    if (!m_settingsHandler(m_editSettings, error)) {
        SetStatus(error.empty() ? "保存设置失败" : error, "fail");
        return;
    }
    m_settings = m_editSettings;
    RefreshSettingsFields();
    SetStatus("设置已保存", "ok");
    ZL_INFO("ui",
            "菜单设置已应用：主音量 {} / 音乐 {} / 音效 {} / 环境 {}，键位 {} 项",
            m_settings.volume, m_settings.musicVolume, m_settings.sfxVolume,
            m_settings.ambientVolume, m_settings.bindings.size());
}

void MenuPanel::SetStatus(const std::string& text, const char* tone) {
    if (m_statusElement == nullptr) {
        return;
    }
    m_statusElement->SetInnerRML(text);
    m_statusElement->SetClassNames(std::string("menu-status status-") +
                                   (tone != nullptr ? tone : "info"));
}

bool MenuPanel::OnEscape() {
    if (!m_open) {
        return false;
    }
    if (m_rebindIndex >= 0) {
        CancelRebind();
        return true;
    }
    if (m_settingsOpen) {
        CloseSettings();
        return true;
    }
    if (m_endMode) {
        return true;  // 结算页必须用按钮重开/返回主菜单，ESC 不绕过
    }
    if (m_pauseMode) {
        if (m_actionHandler) {
            m_actionHandler(MenuActionType::Resume);
        }
        return true;
    }
    return true;  // 主菜单仍打开：ESC 不退出游戏
}

void MenuPanel::AddClickListener(const char* elementId, std::function<void()> action) {
    if (m_document == nullptr) {
        return;
    }
    Rml::Element* element = m_document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "菜单元素不存在：{}", elementId);
        return;
    }
    AddEventListener(element, "click",
                    [this, action = std::move(action)](Rml::Event&) {
                        if (m_soundHandler) {
                            m_soundHandler();
                        }
                        action();
                    });
}

void MenuPanel::AddEventListener(Rml::Element* element, const char* event,
                                 std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<EventFunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back(ListenerBinding{element, event, std::move(listener)});
}

}  // namespace zl::ui
