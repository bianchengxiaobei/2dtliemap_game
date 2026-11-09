#include "ui/SocialPanel.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include "core/Log.h"

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

std::string EscapeRml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char value : text) {
        switch (value) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out.push_back(value);
                break;
        }
    }
    return out;
}

void SetVisible(Rml::Element* element, bool visible) {
    if (element != nullptr) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

}  // namespace

SocialPanel::SocialPanel() = default;

SocialPanel::~SocialPanel() { Shutdown(); }

bool SocialPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "聊天/玩家列表加载失败：{}", path);
        m_context = nullptr;
        return false;
    }
    m_root = m_document->GetElementById("social-root");
    m_playersCard = m_document->GetElementById("social-players");
    m_playerCount = m_document->GetElementById("social-player-count");
    m_chatCard = m_document->GetElementById("social-chat");
    m_chatLog = m_document->GetElementById("social-chat-log");
    m_chatInputRow = m_document->GetElementById("social-chat-input-row");
    m_chatInput = m_document->GetElementById("social-chat-input");
    m_chatHint = m_document->GetElementById("social-chat-hint");
    m_status = m_document->GetElementById("social-status");
    for (int i = 0; i < kPlayerRows; ++i) {
        const std::string suffix = std::to_string(i);
        m_playerRows[static_cast<std::size_t>(i)] =
            m_document->GetElementById("social-player-" + suffix);
        m_playerNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("social-player-name-" + suffix);
    }
    for (int i = 0; i < kChatRows; ++i) {
        m_chatLines[static_cast<std::size_t>(i)] =
            m_document->GetElementById("social-chat-" + std::to_string(i));
    }
    AddEventListener(m_document->GetElementById("social-chat-send"), "click",
                     [this](Rml::Event&) { SubmitChat(); });
    m_document->Hide();
    m_chatOpen = false;
    return true;
}

void SocialPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event, binding.listener.get());
        }
    }
    m_listeners.clear();
    m_document = nullptr;
    m_context = nullptr;
    m_root = nullptr;
    m_playersCard = nullptr;
    m_playerCount = nullptr;
    m_chatCard = nullptr;
    m_chatLog = nullptr;
    m_chatInputRow = nullptr;
    m_chatInput = nullptr;
    m_chatHint = nullptr;
    m_status = nullptr;
    m_playerRows.fill(nullptr);
    m_playerNames.fill(nullptr);
    m_chatLines.fill(nullptr);
    m_sendHandler = nullptr;
    m_state = {};
    m_chatOpen = false;
}

void SocialPanel::SetSendHandler(SocialChatHandler handler) {
    m_sendHandler = std::move(handler);
}

void SocialPanel::SetSuppressed(bool suppressed) {
    if (m_suppressed == suppressed) {
        return;
    }
    m_suppressed = suppressed;
    if (m_suppressed) {
        CloseChat();
    }
    RefreshVisibility();
}

void SocialPanel::SetState(const SocialUiState& state) {
    m_state = state;
    if (!m_state.visible) {
        m_chatOpen = false;
    }
    RefreshPlayers();
    RefreshChat();
    RefreshVisibility();
}

void SocialPanel::OpenChat() {
    if (!m_state.visible || m_suppressed || m_chatOpen) {
        return;
    }
    m_chatOpen = true;
    if (m_chatInput != nullptr) {
        static_cast<Rml::ElementFormControlInput*>(m_chatInput)->SetValue("");
    }
    SetVisible(m_chatInputRow, true);
    if (m_chatHint != nullptr) {
        m_chatHint->SetInnerRML("Enter 发送 · Esc 取消");
    }
    RefreshVisibility();
    // 输入行从 display:none 切换为可见后，必须先完成布局，否则 RmlUi 会拒绝
    // Focus()，聊天期间的游戏按键也不会被阻断。
    if (m_context != nullptr) {
        m_context->Update();
    }
    if (m_chatInput != nullptr) {
        m_chatInput->Focus();
    }
}

void SocialPanel::CloseChat() {
    if (!m_chatOpen) {
        return;
    }
    m_chatOpen = false;
    if (m_context != nullptr) {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
    }
    SetVisible(m_chatInputRow, false);
    if (m_chatHint != nullptr) {
        m_chatHint->SetInnerRML("Y 聊天");
    }
    RefreshVisibility();
}

bool SocialPanel::SubmitChat() {
    if (!m_chatOpen || m_chatInput == nullptr) {
        return false;
    }
    std::string text =
        static_cast<Rml::ElementFormControlInput*>(m_chatInput)->GetValue();
    const std::size_t begin = text.find_first_not_of(" \t");
    const std::size_t end = text.find_last_not_of(" \t");
    if (begin == std::string::npos || end == std::string::npos) {
        CloseChat();
        return true;
    }
    text = text.substr(begin, end - begin + 1);
    if (!m_sendHandler) {
        SetStatus("聊天不可用", "fail");
        return false;
    }
    std::string error;
    if (!m_sendHandler(text, error)) {
        SetStatus(error.empty() ? "聊天发送失败" : error, "fail");
        return false;
    }
    SetStatus("", "info");
    CloseChat();
    return true;
}

void SocialPanel::RefreshPlayers() {
    if (m_playerCount != nullptr) {
        m_playerCount->SetInnerRML(std::to_string(m_state.players.size()));
    }
    const std::size_t visibleCount =
        std::min(m_state.players.size(), static_cast<std::size_t>(kPlayerRows));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kPlayerRows); ++i) {
        const bool visible = i < visibleCount;
        SetVisible(m_playerRows[i], visible);
        if (!visible) {
            continue;
        }
        const SocialPlayerUi& player = m_state.players[i];
        m_playerNames[i]->SetInnerRML(EscapeRml(player.name) +
                                      (player.local ? " <span class=\"social-me\">你</span>"
                                                    : std::string()));
        m_playerRows[i]->SetClass("local", player.local);
    }
}

void SocialPanel::RefreshChat() {
    const std::size_t count = std::min(
        m_state.chat.size(), static_cast<std::size_t>(kChatRows));
    const std::size_t first = m_state.chat.size() - count;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kChatRows); ++i) {
        const bool visible = i < count;
        SetVisible(m_chatLines[i], visible);
        if (!visible) {
            continue;
        }
        const SocialChatUi& line = m_state.chat[first + i];
        m_chatLines[i]->SetInnerRML("<span class=\"social-sender\">" +
                                    EscapeRml(line.sender) + "：</span>" +
                                    EscapeRml(line.text));
    }
    SetVisible(m_chatCard, !m_state.chat.empty() || m_chatOpen);
}

void SocialPanel::RefreshVisibility() {
    if (m_document == nullptr) {
        return;
    }
    const bool visible = m_state.visible && !m_suppressed;
    if (visible) {
        m_document->Show();
        m_document->PullToFront();
    } else {
        m_document->Hide();
    }
    SetVisible(m_playersCard, visible);
    SetVisible(m_chatCard, visible && (!m_state.chat.empty() || m_chatOpen));
    SetVisible(m_chatInputRow, visible && m_chatOpen);
    SetVisible(m_status, visible && !m_state.status.empty());
    if (m_chatHint != nullptr) {
        m_chatHint->SetInnerRML(m_chatOpen ? "Enter 发送 · Esc 取消" : "Y 聊天");
    }
}

void SocialPanel::SetStatus(const std::string& text, const char* tone) {
    m_state.status = text;
    m_state.statusTone = tone != nullptr ? tone : "info";
    if (m_status != nullptr) {
        m_status->SetInnerRML(EscapeRml(text));
        m_status->SetClassNames("social-status status-" +
                                (tone != nullptr ? std::string(tone) : std::string("info")));
        SetVisible(m_status, !text.empty());
    }
}

void SocialPanel::AddEventListener(Rml::Element* element, const char* event,
                                   std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<EventFunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back(ListenerBinding{element, event, std::move(listener)});
}

}  // namespace zl::ui
