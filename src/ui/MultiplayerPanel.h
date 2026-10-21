#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/MultiplayerUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

class MultiplayerPanel {
public:
    static constexpr int kServerRows = 8;

    MultiplayerPanel();
    ~MultiplayerPanel();

    MultiplayerPanel(const MultiplayerPanel&) = delete;
    MultiplayerPanel& operator=(const MultiplayerPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetActionHandler(MultiplayerActionHandler handler);
    void SetState(const MultiplayerMenuState& state);
    void Show(const MultiplayerMenuState& state);
    void Hide();
    bool IsOpen() const { return m_open; }
    void TriggerBack();

    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddEventListener(Rml::Element* element, const char* event,
                          std::function<void(Rml::Event&)> action);
    void AddClickListener(const char* elementId, MultiplayerActionType action);
    void RefreshFields();
    void RefreshServers();
    void RefreshStatus();
    MultiplayerMenuState ReadState() const;
    void Trigger(MultiplayerActionType action);
    void FillServerFields(int index);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_nameInput = nullptr;
    Rml::Element* m_hostInput = nullptr;
    Rml::Element* m_portInput = nullptr;
    Rml::Element* m_hostPortInput = nullptr;
    Rml::Element* m_emptyElement = nullptr;
    Rml::Element* m_statusElement = nullptr;
    std::array<Rml::Element*, kServerRows> m_serverRows{};
    std::array<Rml::Element*, kServerRows> m_serverNames{};
    std::array<Rml::Element*, kServerRows> m_serverAddresses{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;

    MultiplayerActionHandler m_actionHandler;
    MultiplayerMenuState m_state;
    bool m_open = false;
};

}  // namespace zl::ui
