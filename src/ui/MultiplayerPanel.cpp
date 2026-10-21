#include "ui/MultiplayerPanel.h"

#include <algorithm>
#include <cstdlib>
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
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(value); break;
        }
    }
    return out;
}

void SetText(Rml::Element* element, const std::string& text) {
    if (element != nullptr) {
        element->SetInnerRML(EscapeRml(text));
    }
}

void SetVisible(Rml::Element* element, bool visible) {
    if (element != nullptr) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

std::uint16_t ParsePort(const std::string& text) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < 0 || value > 65535) {
        return 0;
    }
    return static_cast<std::uint16_t>(value);
}

}  // namespace

MultiplayerPanel::MultiplayerPanel() = default;

MultiplayerPanel::~MultiplayerPanel() { Shutdown(); }

bool MultiplayerPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "联机菜单加载失败：{}", path);
        m_context = nullptr;
        return false;
    }
    m_nameInput = m_document->GetElementById("mp-name");
    m_hostInput = m_document->GetElementById("mp-host");
    m_portInput = m_document->GetElementById("mp-port");
    m_hostPortInput = m_document->GetElementById("mp-host-port");
    m_emptyElement = m_document->GetElementById("mp-server-empty");
    m_statusElement = m_document->GetElementById("mp-status");
    for (int i = 0; i < kServerRows; ++i) {
        const std::string suffix = std::to_string(i);
        m_serverRows[static_cast<std::size_t>(i)] =
            m_document->GetElementById("mp-server-" + suffix);
        m_serverNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("mp-server-name-" + suffix);
        m_serverAddresses[static_cast<std::size_t>(i)] =
            m_document->GetElementById("mp-server-addr-" + suffix);
        Rml::Element* row = m_serverRows[static_cast<std::size_t>(i)];
        if (row != nullptr) {
            AddEventListener(row, "click", [this, i](Rml::Event&) {
                FillServerFields(i);
            });
        }
    }

    AddClickListener("mp-join", MultiplayerActionType::Join);
    AddClickListener("mp-host-start", MultiplayerActionType::Host);
    AddClickListener("mp-add-server", MultiplayerActionType::AddServer);
    AddClickListener("mp-refresh", MultiplayerActionType::RefreshServers);
    AddClickListener("mp-back", MultiplayerActionType::Back);

    m_document->Hide();
    m_open = false;
    return true;
}

void MultiplayerPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event, binding.listener.get());
        }
    }
    m_listeners.clear();
    m_document = nullptr;
    m_context = nullptr;
    m_nameInput = nullptr;
    m_hostInput = nullptr;
    m_portInput = nullptr;
    m_hostPortInput = nullptr;
    m_emptyElement = nullptr;
    m_statusElement = nullptr;
    m_serverRows.fill(nullptr);
    m_serverNames.fill(nullptr);
    m_serverAddresses.fill(nullptr);
    m_actionHandler = nullptr;
    m_state = {};
    m_open = false;
}

void MultiplayerPanel::SetActionHandler(MultiplayerActionHandler handler) {
    m_actionHandler = std::move(handler);
}

void MultiplayerPanel::SetState(const MultiplayerMenuState& state) {
    m_state = state;
    if (m_document != nullptr) {
        RefreshFields();
        RefreshServers();
        RefreshStatus();
    }
}

void MultiplayerPanel::Show(const MultiplayerMenuState& state) {
    if (m_document == nullptr) {
        return;
    }
    SetState(state);
    m_document->Show();
    m_document->PullToFront();
    m_open = true;
}

void MultiplayerPanel::Hide() {
    if (m_context != nullptr) {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
    }
    if (m_document != nullptr) {
        m_document->Hide();
    }
    m_open = false;
}

void MultiplayerPanel::TriggerBack() { Trigger(MultiplayerActionType::Back); }

void MultiplayerPanel::RefreshFields() {
    auto setInput = [](Rml::Element* element, const std::string& text) {
        if (element != nullptr) {
            static_cast<Rml::ElementFormControlInput*>(element)->SetValue(text);
        }
    };
    setInput(m_nameInput, m_state.playerName);
    setInput(m_hostInput, m_state.joinHost);
    setInput(m_portInput, std::to_string(m_state.joinPort));
    setInput(m_hostPortInput, std::to_string(m_state.hostPort));
}

void MultiplayerPanel::RefreshServers() {
    for (int i = 0; i < kServerRows; ++i) {
        const bool visible = i < static_cast<int>(m_state.servers.size());
        SetVisible(m_serverRows[static_cast<std::size_t>(i)], visible);
        if (!visible) {
            continue;
        }
        const MultiplayerServerUi& server =
            m_state.servers[static_cast<std::size_t>(i)];
        SetText(m_serverNames[static_cast<std::size_t>(i)], server.name);
        SetText(m_serverAddresses[static_cast<std::size_t>(i)],
                server.host + ":" + std::to_string(server.port));
    }
    SetVisible(m_emptyElement, m_state.servers.empty());
}

void MultiplayerPanel::RefreshStatus() {
    if (m_statusElement == nullptr) {
        return;
    }
    m_statusElement->SetInnerRML(EscapeRml(m_state.status));
    m_statusElement->SetClassNames("mp-status status-" + m_state.statusTone);
    SetVisible(m_statusElement, !m_state.status.empty());
}

MultiplayerMenuState MultiplayerPanel::ReadState() const {
    MultiplayerMenuState state = m_state;
    if (m_nameInput != nullptr) {
        state.playerName =
            static_cast<Rml::ElementFormControlInput*>(m_nameInput)->GetValue();
    }
    if (m_hostInput != nullptr) {
        state.joinHost =
            static_cast<Rml::ElementFormControlInput*>(m_hostInput)->GetValue();
    }
    if (m_portInput != nullptr) {
        state.joinPort = ParsePort(
            static_cast<Rml::ElementFormControlInput*>(m_portInput)->GetValue());
    }
    if (m_hostPortInput != nullptr) {
        state.hostPort = ParsePort(
            static_cast<Rml::ElementFormControlInput*>(m_hostPortInput)->GetValue());
    }
    return state;
}

void MultiplayerPanel::Trigger(MultiplayerActionType action) {
    if (m_actionHandler) {
        const MultiplayerMenuState state = ReadState();
        m_state = state;
        m_actionHandler(action, state);
    }
}

void MultiplayerPanel::FillServerFields(int index) {
    if (index < 0 || index >= static_cast<int>(m_state.servers.size())) {
        return;
    }
    const MultiplayerServerUi& server =
        m_state.servers[static_cast<std::size_t>(index)];
    m_state.joinHost = server.host;
    m_state.joinPort = server.port;
    RefreshFields();
}

void MultiplayerPanel::AddClickListener(const char* elementId,
                                        MultiplayerActionType action) {
    if (m_document == nullptr) {
        return;
    }
    Rml::Element* element = m_document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "联机菜单元素不存在：{}", elementId);
        return;
    }
    AddEventListener(element, "click",
                     [this, action](Rml::Event&) { Trigger(action); });
}

void MultiplayerPanel::AddEventListener(Rml::Element* element, const char* event,
                                        std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<EventFunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back(ListenerBinding{element, event, std::move(listener)});
}

}  // namespace zl::ui
