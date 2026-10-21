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
