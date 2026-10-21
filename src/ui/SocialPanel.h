#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ui/SocialUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

class SocialPanel {
public:
    static constexpr int kPlayerRows = 8;
    static constexpr int kChatRows = 8;

    SocialPanel();
    ~SocialPanel();

    SocialPanel(const SocialPanel&) = delete;
    SocialPanel& operator=(const SocialPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetSendHandler(SocialChatHandler handler);
    void SetSuppressed(bool suppressed);
    void SetState(const SocialUiState& state);
    bool IsVisible() const { return m_state.visible; }
    bool IsChatOpen() const { return m_chatOpen; }

    void OpenChat();
    void CloseChat();
    bool SubmitChat();

    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddEventListener(Rml::Element* element, const char* event,
                          std::function<void(Rml::Event&)> action);
    void RefreshPlayers();
    void RefreshChat();
    void RefreshVisibility();
    void SetStatus(const std::string& text, const char* tone);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_root = nullptr;
    Rml::Element* m_playersCard = nullptr;
    Rml::Element* m_playerCount = nullptr;
    Rml::Element* m_chatCard = nullptr;
    Rml::Element* m_chatLog = nullptr;
    Rml::Element* m_chatInputRow = nullptr;
    Rml::Element* m_chatInput = nullptr;
    Rml::Element* m_chatHint = nullptr;
    Rml::Element* m_status = nullptr;
    std::array<Rml::Element*, kPlayerRows> m_playerRows{};
    std::array<Rml::Element*, kPlayerRows> m_playerNames{};
    std::array<Rml::Element*, kChatRows> m_chatLines{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;

    SocialChatHandler m_sendHandler;
    SocialUiState m_state;
    bool m_chatOpen = false;
    bool m_suppressed = false;
};

}  // namespace zl::ui
