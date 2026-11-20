#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace zl::ui {

inline constexpr int kVehicleModPanelSlots = 5;

struct VehicleModSlotUi {
    bool occupied = false;
    bool hasCandidate = false;
    bool canInstall = false;
    bool canRemove = false;
    int candidateIndex = 0;
    int candidateCount = 0;
    std::string slotName;
    std::string currentName;
    std::string currentDetail;
    std::string currentEffect;
    std::string candidateName;
    std::string candidateDetail;
    std::string candidateEffect;
    std::string materials;
    std::string duration;
};

struct VehicleModsUiState {
    bool open = false;
    bool valid = false;
    bool readOnly = false;
    bool inProgress = false;
    float progress = 0.0f;
    std::string title = "车辆改装";
    std::string subtitle;
    std::string summary;
    std::string status;
    std::string statusTone = "info";
    std::array<VehicleModSlotUi, kVehicleModPanelSlots> slots{};
};

enum class VehicleModPanelActionType : std::uint8_t {
    Install = 0,
    Remove,
    CycleCandidate,
    Close,
};

struct VehicleModPanelAction {
    VehicleModPanelActionType type = VehicleModPanelActionType::Close;
    int slot = -1;
};

using VehicleModPanelActionHandler =
    std::function<void(const VehicleModPanelAction&)>;

}  // namespace zl::ui
