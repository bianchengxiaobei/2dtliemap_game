#include "net/Command.h"

namespace zl::net {

const char* CommandTypeName(CommandType type) {
    switch (type) {
        case CommandType::Move:
            return "Move";
        case CommandType::MoveTo:
            return "MoveTo";
        case CommandType::Attack:
            return "Attack";
        case CommandType::PickupNearest:
            return "PickupNearest";
        case CommandType::PickupEntity:
            return "PickupEntity";
        case CommandType::UseBag:
            return "UseBag";
        case CommandType::EquipBag:
            return "EquipBag";
        case CommandType::Unequip:
            return "Unequip";
        case CommandType::DropBag:
            return "DropBag";
        case CommandType::DropEquipment:
            return "DropEquipment";
        case CommandType::InventoryAction:
            return "InventoryAction";
        case CommandType::Gunshot:
            return "Gunshot";
        case CommandType::SelectHotbar:
            return "SelectHotbar";
        case CommandType::FireProjectile:
            return "FireProjectile";
        case CommandType::ChoosePerk:
            return "ChoosePerk";
        case CommandType::PlaceStructure:
            return "PlaceStructure";
        case CommandType::DemolishStructure:
            return "DemolishStructure";
        case CommandType::RepairStructure:
            return "RepairStructure";
        case CommandType::ToggleDoor:
            return "ToggleDoor";
        case CommandType::InteractFacility:
            return "InteractFacility";
        case CommandType::TradeOffer:
            return "TradeOffer";
        case CommandType::SearchLootContainer:
            return "SearchLootContainer";
        case CommandType::AbandonMission:
            return "AbandonMission";
        case CommandType::InteractRadio:
            return "InteractRadio";
        case CommandType::SleepRequest:
            return "SleepRequest";
        case CommandType::SleepVote:
            return "SleepVote";
        case CommandType::SleepCancel:
            return "SleepCancel";
        case CommandType::FireRanged:
            return "FireRanged";
        case CommandType::ReloadRanged:
            return "ReloadRanged";
        case CommandType::ForageStart:
            return "ForageStart";
        case CommandType::ForageCancel:
            return "ForageCancel";
        case CommandType::FishingStart:
            return "FishingStart";
        case CommandType::FishingReel:
            return "FishingReel";
        case CommandType::FishingCancel:
            return "FishingCancel";
        case CommandType::IgniteFire:
            return "IgniteFire";
        case CommandType::ExtinguishFire:
            return "ExtinguishFire";
        case CommandType::AnimalHarvestStart:
            return "AnimalHarvestStart";
        case CommandType::AnimalHarvestCancel:
            return "AnimalHarvestCancel";
        case CommandType::AcceptSettlementMission:
            return "AcceptSettlementMission";
        case CommandType::EnterVehicle:
            return "EnterVehicle";
        case CommandType::ExitVehicle:
            return "ExitVehicle";
        case CommandType::ServiceVehicle:
            return "ServiceVehicle";
        case CommandType::ToggleHeadlights:
            return "ToggleHeadlights";
        case CommandType::HonkHorn:
            return "HonkHorn";
        case CommandType::EnterVehiclePassenger:
            return "EnterVehiclePassenger";
        case CommandType::TakeDriverSeat:
            return "TakeDriverSeat";
        case CommandType::GiveDriverSeat:
            return "GiveDriverSeat";
        case CommandType::Block:
            return "Block";
        case CommandType::Dodge:
            return "Dodge";
        case CommandType::ChooseWorldEvent:
            return "ChooseWorldEvent";
    }
    return "Unknown";
}

const char* TransportModeName(TransportMode mode) {
    switch (mode) {
        case TransportMode::Direct:
            return "direct";
        case TransportMode::Loopback:
            return "loopback";
    }
    return "unknown";
}

}  // namespace zl::net
