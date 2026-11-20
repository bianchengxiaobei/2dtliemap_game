#include "ui/VehicleModsPanel.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <RmlUi/Core.h>

#include "core/Log.h"

namespace zl::ui {
namespace {

class FunctionListener final : public Rml::EventListener {
public:
    explicit FunctionListener(std::function<void(Rml::Event&)> action)
        : m_action(std::move(action)) {}

    void ProcessEvent(Rml::Event& event) override {
        if (m_action) {
            m_action(event);
        }
    }

private:
    std::function<void(Rml::Event&)> m_action;
};

std::string EscapeRml(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

void SetText(Rml::Element* element, const std::string& text) {
    if (element != nullptr) {
        element->SetInnerRML(EscapeRml(text));
    }
}

void SetVisible(Rml::Element* element, bool visible, const char* display) {
    if (element != nullptr) {
        element->SetProperty("display", visible ? display : "none");
    }
}

void SetEnabledLook(Rml::Element* element, bool enabled) {
    if (element == nullptr) {
        return;
    }
    element->SetProperty("opacity", enabled ? "1.0" : "0.45");
}

std::string ToneColor(const std::string& tone) {
    if (tone == "ok") {
        return "#9ccc65";
    }
    if (tone == "fail") {
        return "#ef5350";
    }
    if (tone == "warn") {
        return "#ffb74d";
    }
    return "#d7e3ef";
}

}  // namespace

VehicleModsPanel::VehicleModsPanel() = default;

VehicleModsPanel::~VehicleModsPanel() { Shutdown(); }

bool VehicleModsPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "车辆改装文档加载失败：{}", path);
        m_context = nullptr;
        return false;
    }
    m_overlay = m_document->GetElementById("vehicle-mods-overlay");
    m_subtitle = m_document->GetElementById("vehicle-mods-subtitle");
    m_summary = m_document->GetElementById("vehicle-mods-summary");
    m_status = m_document->GetElementById("vehicle-mods-status");
    m_progress = m_document->GetElementById("vehicle-mods-progress");
    m_progressFill = m_document->GetElementById("vehicle-mods-progress-fill");

    for (int i = 0; i < kVehicleModPanelSlots; ++i) {
        const std::string suffix = std::to_string(i);
        m_slotNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-slot-name-" + suffix);
        m_currents[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-current-" + suffix);
        m_candidates[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-candidate-" + suffix);
        m_materials[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-materials-" + suffix);
        m_durations[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-duration-" + suffix);
        m_cycles[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-cycle-" + suffix);
        m_installs[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-install-" + suffix);
        m_removes[static_cast<std::size_t>(i)] =
            m_document->GetElementById("vm-remove-" + suffix);

        const int slot = i;
        AddClickListener(m_cycles[static_cast<std::size_t>(i)],
                         [this, slot] {
                             if (m_state.slots[static_cast<std::size_t>(slot)]
                                     .candidateCount > 1) {
                                 Invoke({VehicleModPanelActionType::CycleCandidate,
                                         slot});
                             }
                         });
        AddClickListener(m_installs[static_cast<std::size_t>(i)],
                         [this, slot] {
                             if (m_state.slots[static_cast<std::size_t>(slot)]
                                     .canInstall) {
                                 Invoke({VehicleModPanelActionType::Install,
                                         slot});
                             }
                         });
        AddClickListener(m_removes[static_cast<std::size_t>(i)],
                         [this, slot] {
                             if (m_state.slots[static_cast<std::size_t>(slot)]
                                     .canRemove) {
                                 Invoke({VehicleModPanelActionType::Remove,
                                         slot});
                             }
                         });
    }

    AddClickListener(m_document->GetElementById("vehicle-mods-close"),
                     [this] { Invoke({VehicleModPanelActionType::Close, -1}); });

    Hide();
    return true;
}

void VehicleModsPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event,
                                                 binding.listener.get());
        }
    }
    m_listeners.clear();
    m_document = nullptr;
    m_context = nullptr;
    m_overlay = nullptr;
    m_subtitle = nullptr;
    m_summary = nullptr;
    m_status = nullptr;
    m_progress = nullptr;
    m_progressFill = nullptr;
    m_slotNames.fill(nullptr);
    m_currents.fill(nullptr);
    m_candidates.fill(nullptr);
    m_materials.fill(nullptr);
    m_durations.fill(nullptr);
    m_cycles.fill(nullptr);
    m_installs.fill(nullptr);
    m_removes.fill(nullptr);
    m_open = false;
}

void VehicleModsPanel::SetActionHandler(
    VehicleModPanelActionHandler handler) {
    m_actionHandler = std::move(handler);
}

void VehicleModsPanel::SetState(const VehicleModsUiState& state) {
    if (m_document == nullptr) {
        return;
    }
    m_state = state;
    SetText(m_subtitle, state.subtitle);
    SetText(m_summary, state.summary);
    SetText(m_status, state.status);
    if (m_status != nullptr) {
        m_status->SetProperty("color", ToneColor(state.statusTone));
    }
    SetVisible(m_progress, state.inProgress, "block");
    if (m_progressFill != nullptr) {
        const float percent =
            std::clamp(state.progress, 0.0f, 1.0f) * 100.0f;
        char width[32];
        std::snprintf(width, sizeof(width), "%.1f%%",
                      static_cast<double>(percent));
        m_progressFill->SetProperty("width", width);
    }

    for (int i = 0; i < kVehicleModPanelSlots; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const VehicleModSlotUi& slot = state.slots[index];
        std::string currentText =
            slot.occupied ? "当前：" + slot.currentName : "当前：空";
        if (slot.occupied && !slot.currentEffect.empty()) {
            currentText += " — " + slot.currentEffect;
        }
        SetText(m_slotNames[index], slot.slotName);
        SetText(m_currents[index], currentText);
        if (slot.currentDetail.empty()) {
            std::string candidateText =
                slot.hasCandidate ? "候选：" + slot.candidateName
                                  : "没有可用改装";
            if (slot.hasCandidate && slot.candidateCount > 1) {
                candidateText += "（" +
                                 std::to_string(slot.candidateIndex + 1) +
                                 "/" +
                                 std::to_string(slot.candidateCount) + "）";
            }
            if (slot.hasCandidate && !slot.candidateEffect.empty()) {
                candidateText += " — " + slot.candidateEffect;
            }
            SetText(m_candidates[index], candidateText);
            SetText(m_materials[index], slot.hasCandidate
                                           ? slot.materials
                                           : std::string());
            SetText(m_durations[index], slot.hasCandidate
                                           ? slot.duration
                                           : std::string());
        } else {
            SetText(m_candidates[index], "状态：" + slot.currentDetail);
            SetText(m_materials[index], std::string());
            SetText(m_durations[index], std::string());
        }
        SetVisible(m_cycles[index],
                   !state.readOnly && slot.candidateCount > 1,
                   "inline-block");
        SetVisible(m_installs[index],
                   !state.readOnly && slot.hasCandidate, "inline-block");
        SetVisible(m_removes[index],
                   !state.readOnly && slot.occupied, "inline-block");
        SetEnabledLook(m_installs[index], slot.canInstall);
        SetEnabledLook(m_removes[index], slot.canRemove);
    }

    if (state.open) {
        Show();
    } else {
        Hide();
    }
}

void VehicleModsPanel::Show() {
    if (m_document == nullptr) {
        return;
    }
    if (m_overlay != nullptr) {
        m_overlay->SetProperty("display", "block");
    }
    m_document->Show();
    m_document->PullToFront();
    m_open = true;
}

void VehicleModsPanel::Hide() {
    if (m_overlay != nullptr) {
        m_overlay->SetProperty("display", "none");
    }
    if (m_document != nullptr) {
        m_document->Hide();
    }
    m_open = false;
}

void VehicleModsPanel::AddClickListener(
    Rml::Element* element, std::function<void()> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<FunctionListener>(
        [action = std::move(action)](Rml::Event&) { action(); });
    element->AddEventListener("click", listener.get());
    m_listeners.push_back(
        ListenerBinding{element, "click", std::move(listener)});
}

void VehicleModsPanel::Invoke(const VehicleModPanelAction& action) {
    if (m_actionHandler) {
        m_actionHandler(action);
    }
}

}  // namespace zl::ui
