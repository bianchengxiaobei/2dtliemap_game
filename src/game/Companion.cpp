#include "game/Companion.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "core/Log.h"
#include "game/Missions.h"
#include "items/InventoryOps.h"
#include "items/ItemDef.h"
#include "items/ItemRegistry.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "companions";

bool ValidToken(std::string_view value, std::size_t maxBytes) {
    if (value.empty() || value.size() > maxBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}

bool ReadString(const nlohmann::json& node, const char* key,
                std::size_t maxBytes, std::string* out) {
    if (!node.contains(key) || !node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (value.empty() || value.size() > maxBytes) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadOptionalString(const nlohmann::json& node, const char* key,
                        std::size_t maxBytes, std::string* out) {
    if (!node.contains(key)) {
        return true;
    }
    if (!node[key].is_string()) {
        return false;
    }
    std::string value = node[key].get<std::string>();
    if (value.size() > maxBytes) {
        return false;
    }
    *out = std::move(value);
    return true;
}

bool ReadInt(const nlohmann::json& node, const char* key, int lo, int hi,
             int* out) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        return false;
    }
    std::int64_t value = 0;
    if (node[key].is_number_unsigned()) {
        const std::uint64_t raw = node[key].get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        value = static_cast<std::int64_t>(raw);
    } else {
        value = node[key].get<std::int64_t>();
    }
    if (value < lo || value > hi) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadFloat(const nlohmann::json& node, const char* key, float lo, float hi,
               float* out) {
    if (!node.contains(key) || !node[key].is_number()) {
        return false;
    }
    const double value = node[key].get<double>();
    if (!std::isfinite(value) || value < static_cast<double>(lo) ||
        value > static_cast<double>(hi)) {
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json* out,
                  std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "cannot read " + path.generic_string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try {
        *out = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = path.generic_string() + ": JSON parse failed: " + e.what();
        }
        return false;
    }
    return true;
}

bool ItemReferenceValid(const items::ItemRegistry* items,
                        const std::string& itemId) {
    return items == nullptr || items->Find(itemId) != nullptr;
}

bool ParseEquipment(const nlohmann::json& node, CompanionEquipmentDef* out,
                    const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "equipment must be an object";
        }
        return false;
    }
    CompanionEquipmentDef equipment;
    for (const auto& [key, value] : node.items()) {
        if (!value.is_string()) {
            if (error != nullptr) {
                *error = "equipment." + key + " must be a string";
            }
            return false;
        }
        const std::string item = value.get<std::string>();
        if (item.empty() || item.size() > kMaxCompanionIdBytes ||
            !ValidToken(item, kMaxCompanionIdBytes) ||
            !ItemReferenceValid(items, item)) {
            if (error != nullptr) {
                *error = "equipment." + key + " references an invalid item";
            }
            return false;
        }
        if (key == "primary") {
            equipment.primary = item;
        } else if (key == "body") {
            equipment.body = item;
        } else {
            if (error != nullptr) {
                *error = "unknown equipment slot '" + key + "'";
            }
            return false;
        }
    }
    *out = std::move(equipment);
    return true;
}

bool ParseItemStack(const nlohmann::json& node, CompanionItemStack* out,
                    std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "inventory entry must be an object";
        }
        return false;
    }
    CompanionItemStack stack;
    if (!ReadString(node, "item", kMaxCompanionIdBytes, &stack.item) ||
        !ValidToken(stack.item, kMaxCompanionIdBytes)) {
        if (error != nullptr) {
            *error = "inventory item is invalid";
        }
        return false;
    }
    if (node.contains("count") &&
        !ReadInt(node, "count", 1, 999, &stack.count)) {
        if (error != nullptr) {
            *error = "inventory count must be 1..999";
        }
        return false;
    }
    *out = std::move(stack);
    return true;
}

bool ParseRecruit(const nlohmann::json& node,
                  CompanionRecruitRequirement* out,
                  const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "recruit must be an object";
        }
        return false;
    }
    CompanionRecruitRequirement recruit;
    if (node.contains("minReputationTier")) {
        if (!node["minReputationTier"].is_string()) {
            if (error != nullptr) {
                *error = "recruit.minReputationTier must be a string";
            }
            return false;
        }
        const std::string tier = node["minReputationTier"].get<std::string>();
        if (!ParseSettlementReputationTier(tier, &recruit.minReputationTier)) {
            if (error != nullptr) {
                *error = "recruit.minReputationTier is invalid";
            }
            return false;
        }
    }
    if (node.contains("requiredMission") &&
        (!ReadOptionalString(node, "requiredMission", kMaxCompanionIdBytes,
                             &recruit.requiredMission) ||
         (!recruit.requiredMission.empty() &&
          !ValidToken(recruit.requiredMission, kMaxCompanionIdBytes)))) {
        if (error != nullptr) {
            *error = "recruit.requiredMission is invalid";
        }
        return false;
    }
    if (node.contains("requiredItem")) {
        if (!ReadString(node, "requiredItem", kMaxCompanionIdBytes,
                        &recruit.requiredItem) ||
            !ValidToken(recruit.requiredItem, kMaxCompanionIdBytes) ||
            !ItemReferenceValid(items, recruit.requiredItem)) {
            if (error != nullptr) {
                *error = "recruit.requiredItem is invalid";
            }
            return false;
        }
        recruit.requiredItemCount = 1;
        if (node.contains("requiredItemCount") &&
            !ReadInt(node, "requiredItemCount", 1, 999,
                     &recruit.requiredItemCount)) {
            if (error != nullptr) {
                *error = "recruit.requiredItemCount must be 1..999";
            }
            return false;
        }
    } else if (node.contains("requiredItemCount")) {
        if (error != nullptr) {
            *error = "recruit.requiredItemCount requires requiredItem";
        }
        return false;
    }
    *out = std::move(recruit);
    return true;
}

bool ParseQuestObjective(const nlohmann::json& node,
                         CompanionQuestObjectiveDef* out,
                         const items::ItemRegistry* items,
                         std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "personalQuest objective must be an object";
        }
        return false;
    }
    CompanionQuestObjectiveDef objective;
    std::string kind;
    if (!ReadString(node, "kind", kMaxCompanionIdBytes, &kind)) {
        if (error != nullptr) {
            *error = "personalQuest objective kind is required";
        }
        return false;
    }
    if (kind == "kill") {
        objective.kind = CompanionQuestObjectiveKind::Kill;
        objective.target = "any";
        if (node.contains("target")) {
            if (!ReadString(node, "target", kMaxCompanionQuestTargetBytes,
                            &objective.target) ||
                !ValidToken(objective.target,
                            kMaxCompanionQuestTargetBytes)) {
                if (error != nullptr) {
                    *error = "personalQuest kill target is invalid";
                }
                return false;
            }
        }
    } else if (kind == "gift") {
        objective.kind = CompanionQuestObjectiveKind::Gift;
        if (!ReadString(node, "target", kMaxCompanionQuestTargetBytes,
                        &objective.target) ||
            !ValidToken(objective.target, kMaxCompanionQuestTargetBytes) ||
            !ItemReferenceValid(items, objective.target)) {
            if (error != nullptr) {
                *error = "personalQuest gift target is invalid";
            }
            return false;
        }
    } else {
        if (error != nullptr) {
            *error = "personalQuest objective kind must be kill or gift";
        }
        return false;
    }
    if (node.contains("count") &&
        !ReadInt(node, "count", 1, 999, &objective.count)) {
        if (error != nullptr) {
            *error = "personalQuest objective count must be 1..999";
        }
        return false;
    }
    if (!ReadOptionalString(node, "description", kMaxCompanionQuestTextBytes,
                            &objective.description)) {
        if (error != nullptr) {
            *error = "personalQuest objective description is invalid";
        }
        return false;
    }
    *out = std::move(objective);
    return true;
}

bool ParseQuestStage(const nlohmann::json& node, CompanionQuestStageDef* out,
                     const items::ItemRegistry* items, std::string* error) {
    if (!node.is_object()) {
        if (error != nullptr) {
            *error = "personalQuest stage must be an object";
        }
        return false;
    }
    CompanionQuestStageDef stage;
    if (!ReadString(node, "id", kMaxCompanionIdBytes, &stage.id) ||
        !ValidToken(stage.id, kMaxCompanionIdBytes)) {
        if (error != nullptr) {
            *error = "personalQuest stage id is invalid";
        }
        return false;
    }
    if (!ReadString(node, "name", kMaxCompanionNameBytes, &stage.name)) {
        if (error != nullptr) {
            *error = "personalQuest stage name is required";
        }
        return false;
    }
    if (!ReadOptionalString(node, "description", kMaxCompanionQuestTextBytes,
                            &stage.description) ||
        !ReadOptionalString(node, "completionText",
                            kMaxCompanionQuestTextBytes,
                            &stage.completionText)) {
        if (error != nullptr) {
            *error = "personalQuest stage text is invalid";
        }
        return false;
    }
    if (node.contains("minTrust") &&
        !ReadInt(node, "minTrust", 0, 100, &stage.minTrust)) {
        if (error != nullptr) {
            *error = "personalQuest minTrust must be 0..100";
        }
        return false;
    }
    if (!node.contains("objective") ||
        !ParseQuestObjective(node["objective"], &stage.objective, items,
                             error)) {
        if (error != nullptr && error->empty()) {
            *error = "personalQuest objective is required";
        }
        return false;
    }
    if (node.contains("rewardTrust") &&
        !ReadInt(node, "rewardTrust", 0, 100, &stage.rewardTrust)) {
        if (error != nullptr) {
            *error = "personalQuest rewardTrust must be 0..100";
        }
        return false;
    }
    if (node.contains("rewardTrait") &&
        (!ReadString(node, "rewardTrait", kMaxCompanionTraitBytes,
                     &stage.rewardTrait) ||
         !ValidToken(stage.rewardTrait, kMaxCompanionTraitBytes))) {
        if (error != nullptr) {
            *error = "personalQuest rewardTrait is invalid";
        }
        return false;
    }
    if (node.contains("rewardItem")) {
        if (!ReadString(node, "rewardItem", kMaxCompanionIdBytes,
                        &stage.rewardItem) ||
            !ValidToken(stage.rewardItem, kMaxCompanionIdBytes) ||
            !ItemReferenceValid(items, stage.rewardItem)) {
            if (error != nullptr) {
                *error = "personalQuest rewardItem is invalid";
            }
            return false;
        }
        stage.rewardItemCount = 1;
        if (node.contains("rewardItemCount") &&
            !ReadInt(node, "rewardItemCount", 1, 999,
                     &stage.rewardItemCount)) {
            if (error != nullptr) {
                *error = "personalQuest rewardItemCount must be 1..999";
            }
            return false;
        }
    } else if (node.contains("rewardItemCount")) {
        if (error != nullptr) {
            *error = "personalQuest rewardItemCount requires rewardItem";
        }
        return false;
    }
    if (node.contains("rewardEquip")) {
        if (!ReadString(node, "rewardEquip", 16, &stage.rewardEquipSlot) ||
            (stage.rewardEquipSlot != "primary" &&
             stage.rewardEquipSlot != "body")) {
            if (error != nullptr) {
                *error = "personalQuest rewardEquip must be primary or body";
            }
            return false;
        }
        if (stage.rewardItem.empty()) {
            if (error != nullptr) {
                *error = "personalQuest rewardEquip requires rewardItem";
            }
            return false;
        }
        if (items != nullptr) {
            const items::ItemDef* itemDef = items->Find(stage.rewardItem);
            const ecs::EquipSlot slot =
                stage.rewardEquipSlot == "primary"
                    ? ecs::EquipSlot::PrimaryHand
                    : ecs::EquipSlot::Body;
            if (itemDef == nullptr || !items::SlotAccepts(slot, itemDef->type)) {
                if (error != nullptr) {
                    *error = "personalQuest rewardEquip item type is invalid";
                }
                return false;
            }
        }
    }
    *out = std::move(stage);
    return true;
}

bool ParsePersonalQuest(const nlohmann::json& node, CompanionDef* out,
                        const items::ItemRegistry* items,
                        std::string* error) {
    if (!node.is_array() || node.size() > kMaxCompanionQuestStages) {
        if (error != nullptr) {
            *error = "personalQuest must contain at most 8 stages";
        }
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const nlohmann::json& entry : node) {
        CompanionQuestStageDef stage;
        if (!ParseQuestStage(entry, &stage, items, error)) {
            return false;
        }
        if (!seen.insert(stage.id).second) {
            if (error != nullptr) {
                *error = "personalQuest stage id is duplicated";
            }
            return false;
        }
        out->personalQuest.push_back(std::move(stage));
    }
    return true;
}

bool ParseCompanion(const nlohmann::json& entry, CompanionDef* out,
                    const items::ItemRegistry* items, std::string* error) {
    if (!entry.is_object()) {
        if (error != nullptr) {
            *error = "companion entry must be an object";
        }
        return false;
    }
    CompanionDef def;
    if (!ReadString(entry, "id", kMaxCompanionIdBytes, &def.id) ||
        !ValidToken(def.id, kMaxCompanionIdBytes)) {
        if (error != nullptr) {
            *error = "companion id is invalid";
        }
        return false;
    }
    if (!ReadString(entry, "name", kMaxCompanionNameBytes, &def.name)) {
        if (error != nullptr) {
            *error = "companion name is required";
        }
        return false;
    }
    if (!ReadOptionalString(entry, "background", kMaxCompanionTextBytes,
                            &def.background)) {
        if (error != nullptr) {
            *error = "companion background is invalid";
        }
        return false;
    }
    if (entry.contains("baseHealth") &&
        !ReadFloat(entry, "baseHealth", 1.0f,
                   static_cast<float>(kCompanionMaxHealth), &def.baseHealth)) {
        if (error != nullptr) {
            *error = "companion baseHealth must be 1..1000";
        }
        return false;
    }
    if (entry.contains("inventoryCapacityKg") &&
        !ReadFloat(entry, "inventoryCapacityKg", 1.0f,
                   kCompanionMaxInventoryKg, &def.inventoryCapacityKg)) {
        if (error != nullptr) {
            *error = "companion inventoryCapacityKg must be 1..200";
        }
        return false;
    }
    if (entry.contains("startingLoyalty") &&
        !ReadInt(entry, "startingLoyalty", 0, 100, &def.startingLoyalty)) {
        if (error != nullptr) {
            *error = "companion startingLoyalty must be 0..100";
        }
        return false;
    }
    if (entry.contains("equipment")) {
        std::string equipmentError;
        if (!ParseEquipment(entry["equipment"], &def.equipment, items,
                            &equipmentError)) {
            if (error != nullptr) {
                *error = "companion '" + def.id + "': " + equipmentError;
            }
            return false;
        }
    }
    if (entry.contains("inventory")) {
        const nlohmann::json& inventory = entry["inventory"];
        if (!inventory.is_array() ||
            inventory.size() > kMaxCompanionInventoryEntries) {
            if (error != nullptr) {
                *error = "companion inventory must contain at most 16 entries";
            }
            return false;
        }
        std::unordered_set<std::string> seenItems;
        for (const nlohmann::json& itemNode : inventory) {
            CompanionItemStack stack;
            std::string itemError;
            if (!ParseItemStack(itemNode, &stack, &itemError)) {
                if (error != nullptr) {
                    *error = "companion '" + def.id + "': " + itemError;
                }
                return false;
            }
            if (!ItemReferenceValid(items, stack.item)) {
                if (error != nullptr) {
                    *error = "companion '" + def.id +
                             "' inventory references an unknown item";
                }
                return false;
            }
            if (!seenItems.insert(stack.item).second) {
                if (error != nullptr) {
                    *error = "companion '" + def.id +
                             "' inventory has duplicate item";
                }
                return false;
            }
            def.inventory.push_back(std::move(stack));
        }
    }
    if (entry.contains("traits")) {
        const nlohmann::json& traits = entry["traits"];
        if (!traits.is_array() || traits.size() > kMaxCompanionTraits) {
            if (error != nullptr) {
                *error = "companion traits must contain at most 8 entries";
            }
            return false;
        }
        std::unordered_set<std::string> seenTraits;
        for (const nlohmann::json& traitNode : traits) {
            if (!traitNode.is_string()) {
                if (error != nullptr) {
                    *error = "companion trait must be a string";
                }
                return false;
            }
            const std::string trait = traitNode.get<std::string>();
            if (!ValidToken(trait, kMaxCompanionTraitBytes)) {
                if (error != nullptr) {
                    *error = "companion trait is invalid";
                }
                return false;
            }
            if (!seenTraits.insert(trait).second) {
                if (error != nullptr) {
                    *error = "companion trait is duplicated";
                }
                return false;
            }
            def.traits.push_back(trait);
        }
    }
    if (entry.contains("personalQuest")) {
        std::string questError;
        if (!ParsePersonalQuest(entry["personalQuest"], &def, items,
                                &questError)) {
            if (error != nullptr) {
                *error = "companion '" + def.id + "': " + questError;
            }
            return false;
        }
    }
    if (entry.contains("recruit")) {
        std::string recruitError;
        if (!ParseRecruit(entry["recruit"], &def.recruit, items,
                          &recruitError)) {
            if (error != nullptr) {
                *error = "companion '" + def.id + "': " + recruitError;
            }
            return false;
        }
    }
    def.dialogueProfile = def.id;
    if (entry.contains("dialogueProfile") &&
        (!ReadString(entry, "dialogueProfile", kMaxCompanionIdBytes,
                     &def.dialogueProfile) ||
         !ValidToken(def.dialogueProfile, kMaxCompanionIdBytes))) {
        if (error != nullptr) {
            *error = "companion dialogueProfile is invalid";
        }
        return false;
    }
    *out = std::move(def);
    return true;
}

bool AppendCompanionDocument(const nlohmann::json& doc,
                             const std::string& source, CompanionConfig* out,
                             CompanionMergeStats* stats, bool allowEmpty,
                             const items::ItemRegistry* items,
                             std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "companion config output is null";
        }
        return false;
    }
    if (!doc.is_object()) {
        if (error != nullptr) {
            *error = source + ": root must be an object";
        }
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "companions-v1") {
        if (error != nullptr) {
            *error = source + ": format must be companions-v1";
        }
        return false;
    }
    if (!doc.contains("defs") || !doc["defs"].is_array()) {
        if (error != nullptr) {
            *error = source + ": defs must be an array";
        }
        return false;
    }
    if (doc["defs"].empty()) {
        if (!allowEmpty) {
            if (error != nullptr) {
                *error = source + ": defs must not be empty";
            }
            return false;
        }
        return true;
    }

    std::vector<CompanionDef> parsed;
    int skipped = 0;
    std::string skippedReason;
    for (const nlohmann::json& entry : doc["defs"]) {
        if (out->defs.size() + parsed.size() >= kMaxCompanionDefs) {
            skippedReason = "companion count exceeds limit";
            ++skipped;
            break;
        }
        CompanionDef def;
        std::string defError;
        if (!ParseCompanion(entry, &def, items, &defError)) {
            ZL_WARN(kLogModule, "{}: {}", source, defError);
            skippedReason = defError;
            ++skipped;
            continue;
        }
        if (out->Find(def.id) != nullptr ||
            std::any_of(parsed.begin(), parsed.end(),
                        [&](const CompanionDef& existing) {
                            return existing.id == def.id;
                        })) {
            ZL_WARN(kLogModule, "{}: duplicate companion id '{}'", source,
                    def.id);
            skippedReason = "duplicate companion id '" + def.id + "'";
            ++skipped;
            continue;
        }
        parsed.push_back(std::move(def));
    }

    if (parsed.empty()) {
        if (error != nullptr) {
            *error = source + ": " +
                     (skippedReason.empty() ? "no valid companion definitions"
                                            : skippedReason);
        }
        return false;
    }

    const int added = static_cast<int>(parsed.size());
    out->defs.insert(out->defs.end(),
                     std::make_move_iterator(parsed.begin()),
                     std::make_move_iterator(parsed.end()));
    if (stats != nullptr) {
        stats->defsAdded += added;
        stats->defsSkipped += skipped;
    }
    return true;
}

bool ValidateItemInstance(const ecs::ItemInstance& item,
                          const items::ItemRegistry* registry,
                          std::string* error) {
    if (item.defId.empty() || item.defId.size() > kMaxCompanionIdBytes ||
        !ValidToken(item.defId, kMaxCompanionIdBytes) || item.count < 1 ||
        item.count > 9999 || !std::isfinite(item.condition) ||
        item.condition < 0.0f || item.condition > 1.0f ||
        item.loadedAmmo < 0 || item.loadedAmmo > 200) {
        if (error != nullptr) {
            *error = "companion item instance is invalid";
        }
        return false;
    }
    if (registry != nullptr) {
        const items::ItemDef* def = registry->Find(item.defId);
        if (def == nullptr || item.count > def->maxStack) {
            if (error != nullptr) {
                *error = "companion item '" + item.defId +
                         "' is unknown or exceeds maxStack";
            }
            return false;
        }
    }
    return true;
}

}  // namespace

bool IsValidCompanionToken(std::string_view value) {
    return ValidToken(value, kMaxCompanionIdBytes);
}

const CompanionDef* FindSettlementCompanion(
    const CompanionConfig& companions, const SettlementDef& settlement) {
    if (!companions.enabled ||
        !HasSettlementService(settlement, SettlementService::Recruit)) {
        return nullptr;
    }
    for (const CompanionDef& def : companions.defs) {
        if (def.recruit.requiredMission.empty() ||
            std::find(settlement.missionPool.begin(),
                      settlement.missionPool.end(),
                      def.recruit.requiredMission) !=
                settlement.missionPool.end()) {
            return &def;
        }
    }
    return nullptr;
}

CompanionRecruitCheck EvaluateCompanionRecruitment(
    const CompanionConfig& companions, const SettlementDef& settlement,
    const SettlementReputationConfig& reputationConfig, int reputation,
    const ecs::MissionLog* missionLog, const ecs::Container* inventory,
    bool alreadyRecruited) {
    CompanionRecruitCheck check;
    check.reputation = reputation;
    if (!HasSettlementService(settlement, SettlementService::Recruit) ||
        !SettlementCanUseService(settlement, reputationConfig,
                                 SettlementService::Recruit, reputation)) {
        check.status = CompanionRecruitStatus::ServiceUnavailable;
        return check;
    }
    if (alreadyRecruited) {
        check.status = CompanionRecruitStatus::AlreadyRecruited;
        return check;
    }
    check.def = FindSettlementCompanion(companions, settlement);
    if (check.def == nullptr) {
        check.status = CompanionRecruitStatus::NoCompanion;
        return check;
    }
    const SettlementReputationTierDef* currentTier =
        SettlementReputationTierForScore(reputationConfig, reputation);
    const SettlementReputationTierDef* requiredTier =
        FindSettlementReputationTier(reputationConfig,
                                     check.def->recruit.minReputationTier);
    if (currentTier == nullptr || requiredTier == nullptr ||
        static_cast<std::uint8_t>(currentTier->tier) <
            static_cast<std::uint8_t>(requiredTier->tier)) {
        check.status = CompanionRecruitStatus::ReputationTooLow;
        return check;
    }
    if (!check.def->recruit.requiredMission.empty()) {
        const ecs::MissionRunState* state =
            missionLog != nullptr
                ? FindMissionState(*missionLog,
                                   check.def->recruit.requiredMission)
                : nullptr;
        if (state == nullptr ||
            state->status != ecs::MissionRunStatus::Completed) {
            check.status = CompanionRecruitStatus::MissionRequired;
            return check;
        }
    }
    if (!check.def->recruit.requiredItem.empty() &&
        check.def->recruit.requiredItemCount > 0) {
        check.itemCount =
            inventory != nullptr
                ? items::CountOf(*inventory,
                                 check.def->recruit.requiredItem)
                : 0;
        if (check.itemCount < check.def->recruit.requiredItemCount) {
            check.status = CompanionRecruitStatus::ItemRequired;
            return check;
        }
    }
    check.status = CompanionRecruitStatus::Ready;
    return check;
}

const char* CompanionRecruitStatusText(CompanionRecruitStatus status) {
    switch (status) {
        case CompanionRecruitStatus::Ready:
            return "可以招募";
        case CompanionRecruitStatus::NoCompanion:
            return "该聚落暂无可招募的同伴";
        case CompanionRecruitStatus::ServiceUnavailable:
            return "该聚落不提供招募，或当前声望敌对";
        case CompanionRecruitStatus::ReputationTooLow:
            return "聚落声望不足";
        case CompanionRecruitStatus::MissionRequired:
            return "尚未完成前置委托";
        case CompanionRecruitStatus::ItemRequired:
            return "招募物资不足";
        case CompanionRecruitStatus::AlreadyRecruited:
            return "你已经有一名同伴";
    }
    return "无法招募";
}

bool ConsumeCompanionRecruitItems(ecs::Container* inventory,
                                  const CompanionDef& def) {
    if (def.recruit.requiredItem.empty() ||
        def.recruit.requiredItemCount <= 0) {
        return true;
    }
    if (inventory == nullptr ||
        items::CountOf(*inventory, def.recruit.requiredItem) <
            def.recruit.requiredItemCount) {
        return false;
    }
    return items::RemoveItem(*inventory, def.recruit.requiredItem,
                             def.recruit.requiredItemCount) ==
           def.recruit.requiredItemCount;
}

ecs::Entity FindOwnedCompanion(ecs::EcsWorld& world,
                               std::uint64_t ownerStableId) {
    if (ownerStableId == 0) {
        return ecs::EcsWorld::kNull;
    }
    ecs::Entity found = ecs::EcsWorld::kNull;
    world.Registry().view<const Companion>().each(
        [&](const ecs::Entity entity, const Companion& companion) {
            if (found == ecs::EcsWorld::kNull &&
                companion.ownerStableId == ownerStableId) {
                found = entity;
            }
        });
    return found;
}

const char* CompanionCommandName(CompanionCommand command) {
    switch (command) {
        case CompanionCommand::Follow:
            return "follow";
        case CompanionCommand::Stay:
            return "stay";
        case CompanionCommand::Retreat:
            return "retreat";
    }
    return "follow";
}

bool ParseCompanionCommand(std::string_view value, CompanionCommand* out) {
    if (out == nullptr) {
        return false;
    }
    if (value == "follow") {
        *out = CompanionCommand::Follow;
        return true;
    }
    if (value == "stay") {
        *out = CompanionCommand::Stay;
        return true;
    }
    if (value == "retreat") {
        *out = CompanionCommand::Retreat;
        return true;
    }
    return false;
}

bool ValidateCompanionComponents(const Companion& companion,
                                 const CompanionState& state,
                                 const ecs::Health& health,
                                 const items::ItemRegistry* items,
                                 std::string* error) {
    if (!ValidToken(companion.defId, kMaxCompanionIdBytes)) {
        if (error != nullptr) {
            *error = "companion defId is invalid";
        }
        return false;
    }
    if (companion.ownerStableId == 0) {
        if (error != nullptr) {
            *error = "companion ownerStableId is zero";
        }
        return false;
    }
    if (!ValidToken(companion.homeSettlementId, kMaxCompanionIdBytes)) {
        if (error != nullptr) {
            *error = "companion homeSettlementId is invalid";
        }
        return false;
    }
    if (!std::isfinite(health.max) || health.max <= 0.0f ||
        health.max > static_cast<float>(kCompanionMaxHealth) ||
        !std::isfinite(health.current) || health.current < 0.0f ||
        health.current > health.max) {
        if (error != nullptr) {
            *error = "companion health is invalid";
        }
        return false;
    }
    if (state.loyalty < 0 || state.loyalty > 100) {
        if (error != nullptr) {
            *error = "companion loyalty must be 0..100";
        }
        return false;
    }
    if (static_cast<std::uint8_t>(state.command) > 2u) {
        if (error != nullptr) {
            *error = "companion command is invalid";
        }
        return false;
    }
    if (state.downed && state.dead) {
        if (error != nullptr) {
            *error = "companion cannot be downed and dead at the same time";
        }
        return false;
    }
    if (!std::isfinite(state.downedRemaining) ||
        state.downedRemaining < 0.0f || state.downedRemaining > 300.0f) {
        if (error != nullptr) {
            *error = "companion downedRemaining is invalid";
        }
        return false;
    }
    if (!state.downed && state.downedRemaining > 0.0f) {
        if (error != nullptr) {
            *error = "companion downedRemaining requires downed";
        }
        return false;
    }
    if (static_cast<std::uint8_t>(state.personalStatus) > 4u ||
        state.personalStage < 0 ||
        state.personalStage > static_cast<int>(kMaxCompanionQuestStages) ||
        state.personalProgress < 0 || state.personalProgress > 1000000 ||
        state.personalRewardMask > 0xFFu ||
        state.extraTraits.size() > kMaxCompanionTraits) {
        if (error != nullptr) {
            *error = "companion personal quest state is invalid";
        }
        return false;
    }
    for (std::size_t i = 0; i < state.extraTraits.size(); ++i) {
        if (!ValidToken(state.extraTraits[i], kMaxCompanionTraitBytes)) {
            if (error != nullptr) {
                *error = "companion extra trait is invalid";
            }
            return false;
        }
        if (std::find(state.extraTraits.begin(),
                      state.extraTraits.begin() +
                          static_cast<std::ptrdiff_t>(i),
                      state.extraTraits[i]) !=
            state.extraTraits.begin() +
                static_cast<std::ptrdiff_t>(i)) {
            if (error != nullptr) {
                *error = "companion extra trait is duplicated";
            }
            return false;
        }
    }
    if (!std::isfinite(state.inventory.capacityKg) ||
        state.inventory.capacityKg < 0.0f ||
        state.inventory.capacityKg > kCompanionMaxInventoryKg ||
        state.inventory.items.size() > kMaxCompanionInventoryEntries) {
        if (error != nullptr) {
            *error = "companion inventory is invalid";
        }
        return false;
    }
    for (const ecs::ItemInstance& item : state.inventory.items) {
        if (!ValidateItemInstance(item, items, error)) {
            return false;
        }
    }
    if (state.equipment.slots.size() != ecs::kEquipSlotCount) {
        if (error != nullptr) {
            *error = "companion equipment slot count is invalid";
        }
        return false;
    }
    for (const std::optional<ecs::ItemInstance>& item :
         state.equipment.slots) {
        if (item.has_value() && !ValidateItemInstance(*item, items, error)) {
            return false;
        }
    }
    return true;
}

bool LoadCompanionConfig(const core::Config& config, CompanionConfig* out,
                         std::string* error,
                         const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "companion config output is null";
        }
        return false;
    }
    CompanionConfig result;
    const nlohmann::json& root = config.Raw();
    if (!root.is_object()) {
        *out = std::move(result);
        return true;
    }
    const auto nodeIt = root.find("companions");
    if (nodeIt == root.end() || !nodeIt->is_object()) {
        if (nodeIt != root.end()) {
            if (error != nullptr) {
                *error = "companions must be an object";
            }
            return false;
        }
        *out = std::move(result);
        return true;
    }
    const nlohmann::json& node = *nodeIt;
    if (node.contains("enabled") && !node["enabled"].is_boolean()) {
        if (error != nullptr) {
            *error = "companions.enabled must be a boolean";
        }
        return false;
    }
    result.enabled = node.value("enabled", false);
    if (!result.enabled) {
        *out = std::move(result);
        return true;
    }
    std::string path = "assets/companions.json";
    if (node.contains("file")) {
        if (!node["file"].is_string() ||
            node["file"].get<std::string>().empty()) {
            if (error != nullptr) {
                *error = "companions.file must be a non-empty string";
            }
            return false;
        }
        path = node["file"].get<std::string>();
    }
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    if (!AppendCompanionDocument(doc, path, &result, nullptr, true, items,
                                 error)) {
        return false;
    }
    if (result.defs.empty()) {
        ZL_WARN(kLogModule, "{}: no companion definitions", path);
    }
    *out = std::move(result);
    return true;
}

bool MergeCompanionJson(std::string_view json, const std::string& source,
                        CompanionConfig* out, CompanionMergeStats* stats,
                        std::string* error,
                        const items::ItemRegistry* items) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "companion config output is null";
        }
        return false;
    }
    if (!out->enabled) {
        return true;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = source + ": JSON parse failed: " + e.what();
        }
        return false;
    }
    if (!AppendCompanionDocument(doc, source, out, stats, false, items,
                                 error)) {
        return false;
    }
    if (stats != nullptr) {
        ++stats->filesLoaded;
    }
    return true;
}

bool MergeCompanionFile(const std::filesystem::path& path,
                        CompanionConfig* out, CompanionMergeStats* stats,
                        std::string* error,
                        const items::ItemRegistry* items) {
    nlohmann::json doc;
    if (!ReadJsonFile(path, &doc, error)) {
        return false;
    }
    return MergeCompanionJson(doc.dump(), path.generic_string(), out, stats,
                              error, items);
}

}  // namespace zl::game
