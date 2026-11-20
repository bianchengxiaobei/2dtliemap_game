#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/EcsWorld.h"
#include "game/Settlement.h"
#include "items/ItemComponents.h"

namespace zl::core {
class Config;
}

namespace zl::ecs {
struct Health;
struct MissionLog;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::game {

inline constexpr std::size_t kMaxCompanionDefs = 32;
inline constexpr std::size_t kMaxCompanionInventoryEntries = 16;
inline constexpr std::size_t kMaxCompanionTraits = 8;
inline constexpr std::size_t kMaxCompanionIdBytes = 64;
inline constexpr std::size_t kMaxCompanionNameBytes = 128;
inline constexpr std::size_t kMaxCompanionTextBytes = 512;
inline constexpr std::size_t kMaxCompanionTraitBytes = 32;
inline constexpr std::size_t kMaxCompanionQuestStages = 8;
inline constexpr std::size_t kMaxCompanionQuestTextBytes = 512;
inline constexpr std::size_t kMaxCompanionQuestTargetBytes = 64;
inline constexpr int kCompanionMaxHealth = 1000;
inline constexpr float kCompanionMaxInventoryKg = 200.0f;
inline constexpr int kCompanionGiftTrust = 2;

// M33-F3-T1：companions-v1 数据契约。装备只放现有 Equipment 的两个槽位，
// 背包条目引用现有 item id；特长/对话/招募条件先作为数据，由后续 T 接线。
struct CompanionEquipmentDef {
    std::string primary;
    std::string body;
};

struct CompanionItemStack {
    std::string item;
    int count = 1;
};

struct CompanionRecruitRequirement {
    SettlementReputationTier minReputationTier =
        SettlementReputationTier::Friendly;
    std::string requiredMission;
    std::string requiredItem;
    int requiredItemCount = 0;
};

enum class CompanionTrustTier : std::uint8_t {
    Wary = 0,
    Steady = 1,
    Trusted = 2,
    Bonded = 3,
};

enum class CompanionQuestObjectiveKind : std::uint8_t {
    Kill = 0,
    Gift = 1,
};

enum class CompanionQuestStatus : std::uint8_t {
    Locked = 0,
    Available = 1,
    Active = 2,
    Completed = 3,
    Failed = 4,
};

struct CompanionQuestObjectiveDef {
    CompanionQuestObjectiveKind kind = CompanionQuestObjectiveKind::Kill;
    std::string target = "any";
    int count = 1;
    std::string description;
};

struct CompanionQuestStageDef {
    std::string id;
    std::string name;
    std::string description;
    int minTrust = 0;
    CompanionQuestObjectiveDef objective;
    int rewardTrust = 0;
    std::string rewardTrait;
    std::string rewardItem;
    int rewardItemCount = 0;
    std::string rewardEquipSlot;
    std::string completionText;
};

struct CompanionDef {
    std::string id;
    std::string name;
    std::string background;
    float baseHealth = 100.0f;
    CompanionEquipmentDef equipment;
    float inventoryCapacityKg = 12.0f;
    std::vector<CompanionItemStack> inventory;
    std::vector<std::string> traits;
    CompanionRecruitRequirement recruit;
    std::string dialogueProfile;
    int startingLoyalty = 50;
    std::vector<CompanionQuestStageDef> personalQuest;
};

struct CompanionConfig {
    bool enabled = false;
    std::vector<CompanionDef> defs;

    const CompanionDef* Find(std::string_view id) const {
        for (const CompanionDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

struct CompanionMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

bool LoadCompanionConfig(const core::Config& config, CompanionConfig* out,
                         std::string* error,
                         const items::ItemRegistry* items = nullptr);
bool MergeCompanionFile(const std::filesystem::path& path,
                        CompanionConfig* out,
                        CompanionMergeStats* stats = nullptr,
                        std::string* error = nullptr,
                        const items::ItemRegistry* items = nullptr);
bool MergeCompanionJson(std::string_view json, const std::string& source,
                        CompanionConfig* out,
                        CompanionMergeStats* stats = nullptr,
                        std::string* error = nullptr,
                        const items::ItemRegistry* items = nullptr);

bool IsValidCompanionToken(std::string_view value);

enum class CompanionRecruitStatus : std::uint8_t {
    Ready = 0,
    NoCompanion = 1,
    ServiceUnavailable = 2,
    ReputationTooLow = 3,
    MissionRequired = 4,
    ItemRequired = 5,
    AlreadyRecruited = 6,
};

struct CompanionRecruitCheck {
    CompanionRecruitStatus status = CompanionRecruitStatus::NoCompanion;
    const CompanionDef* def = nullptr;
    int reputation = 0;
    int itemCount = 0;
};

const CompanionDef* FindSettlementCompanion(
    const CompanionConfig& companions, const SettlementDef& settlement);
CompanionRecruitCheck EvaluateCompanionRecruitment(
    const CompanionConfig& companions, const SettlementDef& settlement,
    const SettlementReputationConfig& reputationConfig, int reputation,
    const ecs::MissionLog* missionLog, const ecs::Container* inventory,
    bool alreadyRecruited);
const char* CompanionRecruitStatusText(CompanionRecruitStatus status);
bool ConsumeCompanionRecruitItems(ecs::Container* inventory,
                                  const CompanionDef& def);
ecs::Entity FindOwnedCompanion(ecs::EcsWorld& world,
                               std::uint64_t ownerStableId);

enum class CompanionCommand : std::uint8_t {
    Follow = 0,
    Stay = 1,
    Retreat = 2,
};

const char* CompanionCommandName(CompanionCommand command);
bool ParseCompanionCommand(std::string_view value, CompanionCommand* out);

// 同伴实体的身份部分。生命仍使用通用 ecs::Health，避免伤害/死亡系统出现两个真值源。
struct Companion {
    std::string defId;
    std::uint64_t ownerStableId = 0;
    std::string homeSettlementId;
};

// 同伴的运行时状态。inventory/equipment 直接复用玩家同款组件，便于 T4 共用
// 拾取、装备和存档规则；倒地/死亡阈值由 T4 驱动。
struct CompanionState {
    int loyalty = 50;
    CompanionCommand command = CompanionCommand::Follow;
    bool standby = false;
    bool downed = false;
    bool dead = false;
    float downedRemaining = 0.0f;
    CompanionQuestStatus personalStatus = CompanionQuestStatus::Locked;
    int personalStage = 0;
    int personalProgress = 0;
    std::uint32_t personalRewardMask = 0;
    std::vector<std::string> extraTraits;
    ecs::Container inventory;
    ecs::Equipment equipment;
};

CompanionTrustTier CompanionTrustTierForScore(int score);
const char* CompanionTrustTierName(CompanionTrustTier tier);
const char* CompanionTrustTierLabel(CompanionTrustTier tier);
int AddCompanionTrust(CompanionState* state, int delta);

const CompanionQuestStageDef* CurrentCompanionQuestStage(
    const CompanionDef& def, const CompanionState& state);
CompanionQuestStatus CompanionQuestStatusFor(const CompanionDef& def,
                                             const CompanionState& state);
void RefreshCompanionQuestState(const CompanionDef& def,
                                CompanionState* state);
bool StartCompanionQuest(const CompanionDef& def, CompanionState* state,
                         std::string* error = nullptr);
bool FailCompanionQuest(const CompanionDef& def, CompanionState* state,
                        std::string* error = nullptr);
bool RecordCompanionQuestEvent(const CompanionDef& def, CompanionState* state,
                               CompanionQuestObjectiveKind kind,
                               std::string_view target, int amount,
                               const items::ItemRegistry* items = nullptr,
                               std::string* error = nullptr);
bool GiftToCompanion(ecs::EcsWorld& world, ecs::Entity companion,
                     const CompanionDef& def, const items::ItemRegistry& items,
                     ecs::Container* donor, std::size_t index, int count,
                     std::string* error = nullptr);

bool ValidateCompanionComponents(const Companion& companion,
                                 const CompanionState& state,
                                 const ecs::Health& health,
                                 const items::ItemRegistry* items,
                                 std::string* error = nullptr);

}  // namespace zl::game
