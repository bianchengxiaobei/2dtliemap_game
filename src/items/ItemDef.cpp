#include "items/ItemDef.h"

namespace zl::items {

const char* ItemTypeToString(ItemType type) {
    switch (type) {
        case ItemType::Generic:
            return "generic";
        case ItemType::Weapon:
            return "weapon";
        case ItemType::Consumable:
            return "consumable";
        case ItemType::Clothing:
            return "clothing";
        case ItemType::Container:
            return "container";
    }
    return "generic";
}

bool ParseItemType(std::string_view text, ItemType* out) {
    if (out == nullptr) {
        return false;
    }
    if (text == "generic") {
        *out = ItemType::Generic;
        return true;
    }
    if (text == "weapon") {
        *out = ItemType::Weapon;
        return true;
    }
    if (text == "consumable") {
        *out = ItemType::Consumable;
        return true;
    }
    if (text == "clothing") {
        *out = ItemType::Clothing;
        return true;
    }
    if (text == "container") {
        *out = ItemType::Container;
        return true;
    }
    return false;
}

}  // namespace zl::items
