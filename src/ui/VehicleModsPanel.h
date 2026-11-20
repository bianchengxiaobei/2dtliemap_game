#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/VehicleModsUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

class VehicleModsPanel {
public:
    VehicleModsPanel();
    ~VehicleModsPanel();

    VehicleModsPanel(const VehicleModsPanel&) = delete;
    VehicleModsPanel& operator=(const VehicleModsPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetActionHandler(VehicleModPanelActionHandler handler);
    void SetState(const VehicleModsUiState& state);

    void Show();
    void Hide();
    bool IsOpen() const { return m_open; }
    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddClickListener(Rml::Element* element, std::function<void()> action);
    void Invoke(const VehicleModPanelAction& action);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_overlay = nullptr;
    Rml::Element* m_subtitle = nullptr;
    Rml::Element* m_summary = nullptr;
    Rml::Element* m_status = nullptr;
    Rml::Element* m_progress = nullptr;
    Rml::Element* m_progressFill = nullptr;
    std::array<Rml::Element*, kVehicleModPanelSlots> m_slotNames{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_currents{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_candidates{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_materials{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_durations{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_cycles{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_installs{};
    std::array<Rml::Element*, kVehicleModPanelSlots> m_removes{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;
    VehicleModPanelActionHandler m_actionHandler;
    VehicleModsUiState m_state;
    bool m_open = false;
};

}  // namespace zl::ui
