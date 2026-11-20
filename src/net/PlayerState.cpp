#include "net/PlayerState.h"

#include <bit>
#include <cmath>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace zl::net {
namespace {

constexpr const char* kDefaultSkillIds[4] = {
    "melee", "building", "crafting", "survival"};

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool ValidPerkId(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (const char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

bool ValidProgression(const PlayerState& state) {
    if (state.progressionLevel < 1 || state.progressionLevel > 99 ||
        !std::isfinite(state.progressionXp) || state.progressionXp < 0.0f ||
        state.progressionXp > 1.0e9f || !std::isfinite(state.progressionXpNext) ||
        state.progressionXpNext < 0.0f || state.progressionXpNext > 1.0e9f ||
        state.perkPoints < 0 || state.perkPoints > 99 ||
        state.perks.size() > kMaxPlayerStatePerks ||
        state.perkChoices.size() > 3) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const std::string& perk : state.perks) {
        if (!ValidPerkId(perk) || !seen.insert(perk).second) {
            return false;
        }
    }
    seen.clear();
    for (const std::string& perk : state.perkChoices) {
        if (!ValidPerkId(perk) || !seen.insert(perk).second) {
            return false;
        }
    }
    for (const PlayerStateSkill& skill : state.skills) {
        if ((!skill.id.empty() && !ValidPerkId(skill.id)) ||
            skill.level < 0 || skill.level > 99 ||
            !std::isfinite(skill.xp) || skill.xp < 0.0f || skill.xp > 1.0e9f ||
            !std::isfinite(skill.xpNext) || skill.xpNext < 0.0f ||
            skill.xpNext > 1.0e9f) {
            return false;
        }
    }
    return true;
}

bool ValidInfection(const PlayerState& state) {
    if (state.infectionStage > 3 ||
        !std::isfinite(state.infectionProgress) ||
        state.infectionProgress < 0.0f || state.infectionProgress > 1.0f ||
        !std::isfinite(state.infectionTimeToNextStage) ||
        state.infectionTimeToNextStage < 0.0f ||
        state.infectionTimeToNextStage > 86400.0f ||
        !std::isfinite(state.infectionSlowStrength) ||
        state.infectionSlowStrength < 0.0f ||
        state.infectionSlowStrength > 1.0f ||
        !std::isfinite(state.infectionSlowRemaining) ||
        state.infectionSlowRemaining < 0.0f ||
        state.infectionSlowRemaining > 3600.0f ||
        !std::isfinite(state.infectionSlowDuration) ||
        state.infectionSlowDuration < 0.0f ||
        state.infectionSlowDuration > 3600.0f ||
        state.infectionSlowRemaining > state.infectionSlowDuration + 0.001f) {
        return false;
    }
    if (state.infectionStage == 0) {
        return state.infectionProgress <= 0.0001f &&
               state.infectionTimeToNextStage <= 0.001f &&
               state.infectionSlowStrength <= 0.0001f &&
               state.infectionSlowRemaining <= 0.0001f;
    }
    return true;
}

bool ValidClimate(const PlayerState& state) {
    return std::isfinite(state.climateBodyC) && state.climateBodyC >= 20.0f &&
           state.climateBodyC <= 45.0f && std::isfinite(state.climateWetness) &&
           state.climateWetness >= 0.0f && state.climateWetness <= 1.0f;
}

bool ValidSiege(const PlayerState& state) {
    const PlayerSiegeState& siege = state.siege;
    const auto validDirection = [](std::int8_t direction) {
        return direction >= -1 && direction <= 7;
    };
    const auto validCoordinate = [](float value) {
        return std::isfinite(value) && value >= -1.0e6f && value <= 1.0e6f;
    };
    return siege.phase <= 3 && validDirection(siege.primaryDirection) &&
           validDirection(siege.secondaryDirection) &&
           validDirection(siege.currentDirection) &&
           siege.waveIndex <= 99 && siege.waveCount <= 99 &&
           siege.waveIndex <= siege.waveCount &&
           siege.tone <= 2 && (!siege.breachValid || siege.breached) &&
           validCoordinate(siege.targetX) && validCoordinate(siege.targetY) &&
           validCoordinate(siege.breachX) && validCoordinate(siege.breachY) &&
           siege.status.size() <= kMaxPlayerStateSiegeBytes;
}

bool ValidPower(const PlayerState& state) {
    return std::isfinite(state.power.fuelMinutes) &&
           state.power.fuelMinutes >= 0.0f &&
           state.power.fuelMinutes <= 1.0e9f &&
           std::isfinite(state.power.load) && state.power.load >= 0.0f &&
           state.power.load <= 1.0e6f;
}

bool ValidRuntimeId(std::string_view id) {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    for (const char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

bool ValidPoiProgress(const PlayerState& state) {
    if (state.poiProgress.size() > kMaxPlayerStatePoiProgress) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const PlayerPoiProgress& poi : state.poiProgress) {
        if (!ValidRuntimeId(poi.id) || !seen.insert(poi.id).second ||
            poi.liveGuards < 0 || poi.liveGuards > 9999) {
            return false;
        }
    }
    return true;
}

bool ValidPoiStageProgress(const PlayerState& state) {
    if (state.poiStageProgress.size() > kMaxPlayerStatePoiStages) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const PlayerPoiStageProgress& poi : state.poiStageProgress) {
        if (!ValidRuntimeId(poi.id) || !seen.insert(poi.id).second ||
            poi.stageIndex > 8 || poi.stageCount > 8 ||
            poi.completedStageCount > 8 || poi.flags > 7 ||
            poi.currentStage.size() > 64 ||
            poi.chosenBranch.size() > 64 ||
            (!poi.currentStage.empty() &&
             !ValidRuntimeId(poi.currentStage)) ||
            (!poi.chosenBranch.empty() &&
             !ValidRuntimeId(poi.chosenBranch)) ||
            (poi.stageCount == 0 && !poi.currentStage.empty())) {
            return false;
        }
    }
    return true;
}

bool ValidMissionProgress(const PlayerState& state) {
    if (state.missions.size() > kMaxPlayerStateMissions ||
        state.missionLastDay < 0 || state.missionLastDay > 1000000 ||
        state.missionRandomDay < 0 || state.missionRandomDay > 1000000) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const PlayerMissionProgress& mission : state.missions) {
        if (!ValidRuntimeId(mission.id) || !seen.insert(mission.id).second ||
            mission.status > 2 || mission.day < 0 || mission.day > 1000000 ||
            mission.progress.size() > kMaxPlayerStateMissionObjectives) {
            return false;
        }
        for (const int value : mission.progress) {
            if (value < 0 || value > 999999) {
                return false;
            }
        }
    }
    return true;
}

bool ValidSettlementProgress(const PlayerState& state) {
    if (state.settlements.size() > kMaxPlayerStateSettlements ||
        state.settlementNoticeTone > 2 ||
        state.settlementNotice.size() >
            kMaxPlayerStateSettlementNoticeBytes) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const PlayerSettlementProgress& settlement : state.settlements) {
        if (!ValidRuntimeId(settlement.id) ||
            !seen.insert(settlement.id).second ||
            (settlement.fallen && settlement.raidActive) ||
            !std::isfinite(settlement.guardHealth) ||
            !std::isfinite(settlement.guardHealthMax) ||
            settlement.guardHealth < 0.0f ||
            settlement.guardHealth > 1.0e6f ||
            settlement.guardHealthMax < 0.0f ||
            settlement.guardHealthMax > 1.0e6f ||
            settlement.guardHealth > settlement.guardHealthMax + 0.01f) {
            return false;
        }
    }
    return true;
}

bool ValidSettlementReputation(const PlayerState& state) {
    if (state.settlementReputation.size() >
        kMaxPlayerStateSettlementReputation) {
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const PlayerSettlementReputation& entry :
         state.settlementReputation) {
        if (entry.id.empty() || entry.id.size() > 64 ||
            !seen.insert(entry.id).second || entry.score < -100 ||
            entry.score > 100) {
            return false;
        }
        for (const char c : entry.id) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '.')) {
                return false;
            }
        }
    }
    return true;
}

bool ValidNeeds(const PlayerState& state) {
    const PlayerNeedsState& needs = state.needs;
    if (!std::isfinite(needs.hunger) || !std::isfinite(needs.thirst) ||
        !std::isfinite(needs.fatigue) || needs.warning > 2) {
        return false;
    }
    if (!needs.valid) {
        return needs.hunger == 0.0f && needs.thirst == 0.0f &&
               needs.fatigue == 0.0f && needs.warning == 0;
    }
    return needs.hunger >= 0.0f && needs.hunger <= 1.0f &&
           needs.thirst >= 0.0f && needs.thirst <= 1.0f &&
           needs.fatigue >= 0.0f && needs.fatigue <= 1.0f;
}

bool ValidFood(const PlayerState& state) {
    const PlayerFoodState& food = state.food;
    if (!std::isfinite(food.nutrition) ||
        !std::isfinite(food.poisonRemaining) ||
        !std::isfinite(food.poisonDamagePerSecond)) {
        return false;
    }
    if (!food.valid) {
        return food.nutrition == 0.0f && food.poisonRemaining == 0.0f &&
               food.poisonDamagePerSecond == 0.0f;
    }
    return food.nutrition >= 0.0f && food.nutrition <= 1.0f &&
           food.poisonRemaining >= 0.0f &&
           food.poisonRemaining <= 86'400.0f &&
           food.poisonDamagePerSecond >= 0.0f &&
           food.poisonDamagePerSecond <= 100.0f;
}

bool ValidVehicle(const PlayerState& state) {
    const PlayerVehicleState& vehicle = state.vehicle;
    if (!std::isfinite(vehicle.fuel) || !std::isfinite(vehicle.condition)) {
        return false;
    }
    if (!vehicle.valid) {
        return vehicle.vehicleStableId == 0 && !vehicle.driving &&
               !vehicle.passenger && vehicle.fuel == 0.0f &&
               vehicle.condition == 0.0f;
    }
    return vehicle.vehicleStableId != 0 &&
           !(vehicle.driving && vehicle.passenger) &&
           vehicle.fuel >= 0.0f && vehicle.fuel <= 1.0f &&
           vehicle.condition >= 0.0f && vehicle.condition <= 1.0f &&
           vehicle.vehicleType <= 2 && std::isfinite(vehicle.heading);
}

bool ValidCombat(const PlayerState& state) {
    const PlayerCombatState& combat = state.combat;
    if (!std::isfinite(combat.stamina) || !std::isfinite(combat.staminaMax) ||
        !std::isfinite(combat.parryWindowLeft) ||
        !std::isfinite(combat.guardBreakLeft) ||
        !std::isfinite(combat.dodgeInvulnLeft) ||
        combat.stance > kPlayerCombatStanceDodge ||
        combat.parryWindowLeft < 0.0f || combat.parryWindowLeft > 60.0f ||
        combat.guardBreakLeft < 0.0f || combat.guardBreakLeft > 60.0f ||
        combat.dodgeInvulnLeft < 0.0f || combat.dodgeInvulnLeft > 60.0f) {
        return false;
    }
    if (!combat.valid) {
        return combat.stamina == 0.0f && combat.staminaMax == 0.0f &&
               combat.stance == kPlayerCombatStanceNone &&
               combat.parryWindowLeft == 0.0f &&
               combat.guardBreakLeft == 0.0f &&
               combat.dodgeInvulnLeft == 0.0f;
    }
    return combat.staminaMax > 0.0f && combat.staminaMax <= 1.0e6f &&
           combat.stamina >= 0.0f && combat.stamina <= combat.staminaMax;
}

bool ValidStealth(const PlayerState& state) {
    const PlayerStealthState& stealth = state.stealth;
    if (!std::isfinite(stealth.noiseRadius) ||
        !std::isfinite(stealth.visibilityScale)) {
        return false;
    }
    if (!stealth.valid) {
        return !stealth.crouched && stealth.noiseRadius == 2.5f &&
               stealth.visibilityScale == 1.0f;
    }
    return stealth.noiseRadius >= 0.0f && stealth.noiseRadius <= 10.0f &&
           stealth.visibilityScale >= 0.1f &&
           stealth.visibilityScale <= 1.0f;
}

bool ValidSleep(const PlayerState& state) {
    const PlayerSleepState& sleep = state.sleep;
    if (sleep.phase > kPlayerSleepPhaseSleeping || sleep.tone > 2 ||
        sleep.yesCount > 32 || sleep.eligibleCount > 32 ||
        sleep.yesCount > sleep.eligibleCount ||
        sleep.status.size() > kMaxPlayerStateSleepStatusBytes ||
        !std::isfinite(sleep.startMinutes) || sleep.startMinutes < 0.0f ||
        sleep.startMinutes > 1.0e7f ||
        !std::isfinite(sleep.targetMinutes) || sleep.targetMinutes < 0.0f ||
        sleep.targetMinutes > 1.0e7f ||
        !std::isfinite(sleep.voteSeconds) || sleep.voteSeconds < 0.0f ||
        sleep.voteSeconds > 3600.0f ||
        !std::isfinite(sleep.progress) || sleep.progress < 0.0f ||
        sleep.progress > 1.0f) {
        return false;
    }
    if (sleep.phase == kPlayerSleepPhaseNone) {
        return sleep.initiator == 0 && !sleep.voted &&
               sleep.yesCount == 0 && sleep.eligibleCount == 0 &&
               sleep.startMinutes == 0.0f && sleep.targetMinutes == 0.0f &&
               sleep.voteSeconds == 0.0f && sleep.progress == 0.0f;
    }
    if (sleep.initiator == 0 || sleep.eligibleCount == 0 ||
        !(sleep.targetMinutes > sleep.startMinutes)) {
        return false;
    }
    if (sleep.phase == kPlayerSleepPhaseVote) {
        return sleep.yesCount >= 1 && sleep.progress == 0.0f;
    }
    return sleep.voted && sleep.yesCount == sleep.eligibleCount &&
           sleep.voteSeconds == 0.0f;
}

bool ValidObjective(const PlayerState& state) {
    const PlayerObjectiveState& objective = state.objective;
    if (objective.step > 3) {
        return false;
    }
    return objective.valid || objective.step == 0;
}

bool ValidRangedState(const PlayerState& state) {
    for (const PlayerStateEquipment& slot : state.equipment) {
        if (slot.loadedAmmo < 0 || slot.loadedAmmo > 9999 ||
            (!slot.present && slot.loadedAmmo != 0)) {
            return false;
        }
    }
    const PlayerRangedState& ranged = state.ranged;
    if (!ranged.valid) {
        return ranged.loaded == 0 && ranged.magazine == 0 &&
               ranged.reserve == 0 && !ranged.reloading &&
               ranged.reloadElapsed == 0.0f &&
               ranged.reloadDuration == 0.0f && ranged.reloadAmount == 0 &&
               ranged.shotSerial == 0;
    }
    if (ranged.magazine < 1 || ranged.magazine > 9999 ||
        ranged.loaded < 0 || ranged.loaded > ranged.magazine ||
        ranged.reserve < 0 || ranged.reserve > 9999 ||
        ranged.reloadAmount < 0 || ranged.reloadAmount > ranged.magazine ||
        !std::isfinite(ranged.reloadElapsed) ||
        !std::isfinite(ranged.reloadDuration) ||
        ranged.reloadElapsed < 0.0f || ranged.reloadDuration < 0.0f ||
        ranged.reloadDuration > 3600.0f ||
        ranged.reloadElapsed > ranged.reloadDuration + 1e-4f) {
        return false;
    }
    if (ranged.reloading) {
        return ranged.reloadDuration > 0.0f;
    }
    return ranged.reloadElapsed == 0.0f && ranged.reloadDuration == 0.0f;
}

bool ValidSurvivalCoord(int value) {
    return value >= -100'000 && value <= 100'000;
}

bool ValidSurvival(const PlayerState& state) {
    if (state.forageNodes.size() > kMaxPlayerStateForageNodes ||
        state.fishingSpots.size() > kMaxPlayerStateFishingSpots) {
        return false;
    }
    for (const PlayerForageNodeState& node : state.forageNodes) {
        if (!ValidRuntimeId(node.zoneId) || !ValidSurvivalCoord(node.x) ||
            !ValidSurvivalCoord(node.y) || !ValidSurvivalCoord(node.z) ||
            !std::isfinite(node.respawnMinutes) ||
            node.respawnMinutes < 0.0f || node.respawnMinutes > 1.0e7f ||
            !std::isfinite(node.seconds) || node.seconds < 0.0f ||
            node.seconds > 30.0f) {
            return false;
        }
    }
    for (const PlayerFishingSpotState& spot : state.fishingSpots) {
        if (!ValidRuntimeId(spot.spotId) || !ValidSurvivalCoord(spot.x) ||
            !ValidSurvivalCoord(spot.y) || !ValidSurvivalCoord(spot.z) ||
            !ValidSurvivalCoord(spot.waterX) ||
            !ValidSurvivalCoord(spot.waterY) ||
            !ValidSurvivalCoord(spot.waterZ) ||
            !std::isfinite(spot.seconds) || spot.seconds < 0.0f ||
            spot.seconds > 60.0f) {
            return false;
        }
    }
    const PlayerSurvivalState& survival = state.survival;
    if (survival.mode > kPlayerSurvivalModeFishing ||
        survival.phase > kPlayerSurvivalPhaseBiting ||
        !ValidSurvivalCoord(survival.x) || !ValidSurvivalCoord(survival.y) ||
        !ValidSurvivalCoord(survival.z) ||
        !ValidSurvivalCoord(survival.waterX) ||
        !ValidSurvivalCoord(survival.waterY) ||
        !ValidSurvivalCoord(survival.waterZ) ||
        !std::isfinite(survival.progress) || survival.progress < 0.0f ||
        survival.progress > 1.0f || !std::isfinite(survival.duration) ||
        survival.duration < 0.0f || survival.duration > 60.0f ||
        !std::isfinite(survival.biteRemaining) ||
        survival.biteRemaining < 0.0f || survival.biteRemaining > 60.0f ||
        survival.name.size() > kMaxPlayerStateStringBytes ||
        survival.zoneId.size() > kMaxPlayerStateStringBytes ||
        survival.tableId.size() > kMaxPlayerStateStringBytes ||
        survival.spotId.size() > kMaxPlayerStateStringBytes) {
        return false;
    }
    if (survival.mode == kPlayerSurvivalModeNone) {
        return survival.phase == 0 && survival.name.empty() &&
               survival.zoneId.empty() && survival.tableId.empty() &&
               survival.spotId.empty() && survival.x == 0 &&
               survival.y == 0 && survival.z == 0 && survival.waterX == 0 &&
               survival.waterY == 0 && survival.waterZ == 0 &&
               survival.progress == 0.0f && survival.duration == 0.0f &&
               survival.biteRemaining == 0.0f;
    }
    if (survival.phase == 0 || survival.name.empty() ||
        !(survival.duration > 0.0f)) {
        return false;
    }
    if (survival.mode == kPlayerSurvivalModeForage) {
        return survival.phase == kPlayerSurvivalPhaseActive &&
               ValidRuntimeId(survival.zoneId) &&
               ValidRuntimeId(survival.tableId) && survival.spotId.empty() &&
               survival.biteRemaining == 0.0f;
    }
    if (!ValidRuntimeId(survival.spotId) || !survival.zoneId.empty() ||
        !ValidRuntimeId(survival.tableId)) {
        return false;
    }
    return survival.phase == kPlayerSurvivalPhaseActive
               ? survival.biteRemaining == 0.0f
               : survival.biteRemaining >= 0.0f;
}

bool ValidAnimalHarvest(const PlayerState& state) {
    const PlayerAnimalHarvestState& harvest = state.animalHarvest;
    if (!std::isfinite(harvest.progress) || harvest.progress < 0.0f ||
        harvest.progress > 1.0f || !std::isfinite(harvest.duration) ||
        harvest.duration < 0.0f || harvest.duration > 30.0f ||
        !ValidSurvivalCoord(harvest.x) || !ValidSurvivalCoord(harvest.y) ||
        !ValidSurvivalCoord(harvest.z) ||
        harvest.speciesId.size() > kMaxPlayerStateStringBytes ||
        harvest.name.size() > kMaxPlayerStateStringBytes) {
        return false;
    }
    if (!harvest.active) {
        return harvest.corpseStableId == 0 && harvest.speciesId.empty() &&
               harvest.name.empty() && harvest.x == 0 && harvest.y == 0 &&
               harvest.z == 0 && harvest.progress == 0.0f &&
               harvest.duration == 0.0f;
    }
    return harvest.corpseStableId != 0 &&
           !harvest.speciesId.empty() && !harvest.name.empty() &&
           harvest.duration > 0.0f;
}

bool ValidItem(const PlayerStateItem& item, std::string* reason) {
    if (item.defId.empty() ||
        item.defId.size() > kMaxPlayerStateStringBytes) {
        SetError(reason, "玩家状态物品 defId 非法");
        return false;
    }
    if (item.count < 1 || item.count > 9999) {
        SetError(reason, "玩家状态物品数量非法");
        return false;
    }
    if (!std::isfinite(item.condition) || item.condition < 0.0f ||
        item.condition > 1.0f) {
        SetError(reason, "玩家状态物品耐久非法");
        return false;
    }
    return true;
}

class Writer {
public:
    void U8(std::uint8_t value) { m_bytes.push_back(value); }
    void U16(std::uint16_t value) {
        U8(static_cast<std::uint8_t>(value & 0xFFu));
        U8(static_cast<std::uint8_t>((value >> 8U) & 0xFFu));
    }
    void U32(std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            U8(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
        }
    }
    void U64(std::uint64_t value) {
        for (std::size_t i = 0; i < 8; ++i) {
            U8(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
        }
    }
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }
    bool String(std::string_view value, std::size_t maxBytes, bool allowEmpty,
                std::string* error) {
        if (value.size() > maxBytes || value.size() > 0xFFFFu ||
            (!allowEmpty && value.empty())) {
            SetError(error, "玩家状态字符串长度非法");
            return false;
        }
        U16(static_cast<std::uint16_t>(value.size()));
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
        return true;
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U8(std::uint8_t* value) {
        if (!Has(1)) {
            return false;
        }
        *value = m_bytes[m_offset++];
        return true;
    }
    bool U16(std::uint16_t* value) {
        if (!Has(2)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint16_t>(m_bytes[m_offset + 1]) << 8U);
        m_offset += 2;
        return true;
    }
    bool U32(std::uint32_t* value) {
        if (!Has(4)) {
            return false;
        }
        *value = static_cast<std::uint32_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 1]) << 8U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 2]) << 16U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 3]) << 24U);
        m_offset += 4;
        return true;
    }
    bool U64(std::uint64_t* value) {
        if (!Has(8)) {
            return false;
        }
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i]) << (i * 8U);
        }
        *value = result;
        m_offset += 8;
        return true;
    }
    bool F32(float* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = std::bit_cast<float>(raw);
        return true;
    }
    bool I32(std::int32_t* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = static_cast<std::int32_t>(raw);
        return true;
    }
    bool String(std::string* value, std::size_t maxBytes, bool allowEmpty) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || (!allowEmpty && size == 0) ||
            !Has(size)) {
            return false;
        }
        value->assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                      m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size));
        m_offset += size;
        return true;
    }
    bool AtEnd() const { return m_offset == m_bytes.size(); }

private:
    bool Has(std::size_t count) const {
        return count <= m_bytes.size() - m_offset;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

void WriteItem(Writer& writer, const PlayerStateItem& item, std::string* error,
               bool* ok) {
    if (!*ok) {
        return;
    }
    std::string reason;
    if (!ValidItem(item, &reason)) {
        SetError(error, reason);
        *ok = false;
        return;
    }
    if (!writer.String(item.defId, kMaxPlayerStateStringBytes, false, error)) {
        *ok = false;
        return;
    }
    writer.U16(static_cast<std::uint16_t>(item.count));
    writer.F32(item.condition);
}

bool ReadItem(Reader& reader, PlayerStateItem* item) {
    if (!reader.String(&item->defId, kMaxPlayerStateStringBytes, false)) {
        return false;
    }
    std::uint16_t count = 0;
    if (!reader.U16(&count) || !reader.F32(&item->condition)) {
        return false;
    }
    item->count = static_cast<int>(count);
    std::string reason;
    return ValidItem(*item, &reason);
}

}  // namespace

std::vector<std::uint8_t> EncodePlayerState(const PlayerState& state,
                                            std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (state.playerStableId == 0) {
        SetError(error, "玩家状态缺少 playerStableId");
        return {};
    }
    if (state.bag.size() > kMaxPlayerStateBagItems) {
        SetError(error, "玩家状态背包槽过多");
        return {};
    }
    if (state.knownRecipes.size() > kMaxPlayerStateKnownRecipes) {
        SetError(error, "玩家状态已学配方过多");
        return {};
    }
    if (state.craftQueue.size() > kMaxPlayerStateCraftQueue) {
        SetError(error, "玩家状态制作队列过长");
        return {};
    }
    if (!std::isfinite(state.capacityKg) || state.capacityKg <= 0.0f ||
        state.capacityKg > 1'000'000.0f) {
        SetError(error, "玩家状态背包容量非法");
        return {};
    }
    if (!std::isfinite(state.health) || !std::isfinite(state.healthMax) ||
        !std::isfinite(state.respawnSeconds) || state.healthMax <= 0.0f ||
        state.health < 0.0f || state.health > state.healthMax ||
        state.respawnSeconds < 0.0f || state.respawnSeconds > 86'400.0f ||
        (!state.alive && state.health > 0.0f)) {
        SetError(error, "玩家状态生命字段非法");
        return {};
    }
    if (!std::isfinite(state.craftingElapsed) ||
        !std::isfinite(state.craftingDuration) ||
        !std::isfinite(state.craftingToolEfficiency) ||
        state.craftingElapsed < 0.0f || state.craftingDuration < 0.0f ||
        state.craftingToolEfficiency < 0.0f ||
        state.craftingToolEfficiency > 1.0f || state.batchTotal < 0 ||
        state.batchTotal > 99 || state.batchCompleted < 0 ||
        state.batchCompleted > state.batchTotal) {
        SetError(error, "玩家状态制作字段非法");
        return {};
    }
    if (state.craftingActive &&
        (state.craftingRecipeId.empty() || state.craftingDuration <= 0.0f ||
         state.craftingElapsed > state.craftingDuration + 1e-4f ||
         state.batchTotal < 1)) {
        SetError(error, "玩家状态活跃制作字段非法");
        return {};
    }
    if (state.statusTone > 2 || state.status.size() > kMaxPlayerStateStatusBytes) {
        SetError(error, "玩家状态提示字段非法");
        return {};
    }
    if (!ValidProgression(state)) {
        SetError(error, "玩家状态成长字段非法");
        return {};
    }
    if (!ValidInfection(state)) {
        SetError(error, "玩家状态感染字段非法");
        return {};
    }
    if (!ValidClimate(state)) {
        SetError(error, "玩家状态气候字段非法");
        return {};
    }
    if (!ValidSiege(state)) {
        SetError(error, "玩家状态守夜字段非法");
        return {};
    }
    if (!ValidPower(state)) {
        SetError(error, "玩家状态电力字段非法");
        return {};
    }
    if (!ValidPoiProgress(state)) {
        SetError(error, "玩家状态 POI 字段非法");
        return {};
    }
    if (!ValidPoiStageProgress(state)) {
        SetError(error, "玩家状态 POI 阶段字段非法");
        return {};
    }
    if (!ValidMissionProgress(state)) {
        SetError(error, "玩家状态任务字段非法");
        return {};
    }
    if (!ValidSettlementProgress(state)) {
        SetError(error, "玩家状态聚落字段非法");
        return {};
    }
    if (!ValidSettlementReputation(state)) {
        SetError(error, "玩家状态聚落声望字段非法");
        return {};
    }
    if (!ValidNeeds(state)) {
        SetError(error, "玩家状态需求字段非法");
        return {};
    }
    if (!ValidSleep(state)) {
        SetError(error, "玩家状态睡眠字段非法");
        return {};
    }
    if (!ValidObjective(state)) {
        SetError(error, "玩家状态主目标字段非法");
        return {};
    }
    if (!ValidRangedState(state)) {
        SetError(error, "玩家状态远程武器字段非法");
        return {};
    }
    if (!ValidSurvival(state)) {
        SetError(error, "玩家状态采集/钓鱼字段非法");
        return {};
    }
    if (!ValidAnimalHarvest(state)) {
        SetError(error, "玩家状态动物剥取字段非法");
        return {};
    }
    if (!ValidFood(state)) {
        SetError(error, "玩家状态食物字段非法");
        return {};
    }
    if (!ValidVehicle(state)) {
        SetError(error, "玩家状态载具字段非法");
        return {};
    }
    if (!ValidCombat(state)) {
        SetError(error, "玩家状态战斗字段非法");
        return {};
    }
    if (!ValidStealth(state)) {
        SetError(error, "玩家状态潜行字段非法");
        return {};
    }

    Writer writer;
    writer.U32(kPlayerStateMagic);
    writer.U16(kPlayerStateVersion);
    writer.U16(0);
    writer.U64(state.playerStableId);
    writer.U64(state.serverTick);
    writer.F32(state.capacityKg);
    writer.F32(state.health);
    writer.F32(state.healthMax);
    writer.U8(state.alive ? 1 : 0);
    writer.F32(state.respawnSeconds);

    bool ok = true;
    writer.U16(static_cast<std::uint16_t>(state.bag.size()));
    for (const PlayerStateItem& item : state.bag) {
        WriteItem(writer, item, error, &ok);
    }
    if (!ok) {
        return {};
    }

    for (const PlayerStateEquipment& slot : state.equipment) {
        writer.U8(slot.present ? 1 : 0);
        if (slot.present) {
            WriteItem(writer, slot.item, error, &ok);
            if (!ok) {
                return {};
            }
        }
    }

    writer.U16(static_cast<std::uint16_t>(state.knownRecipes.size()));
    for (const std::string& recipeId : state.knownRecipes) {
        if (!writer.String(recipeId, kMaxPlayerStateStringBytes, false, error)) {
            return {};
        }
    }

    writer.U8(state.craftingActive ? 1 : 0);
    if (state.craftingActive) {
        if (!writer.String(state.craftingRecipeId, kMaxPlayerStateStringBytes,
                           false, error)) {
            return {};
        }
        writer.F32(state.craftingElapsed);
        writer.F32(state.craftingDuration);
        writer.F32(state.craftingToolEfficiency);
        writer.I32(state.batchTotal);
        writer.I32(state.batchCompleted);
    }
    writer.U16(static_cast<std::uint16_t>(state.craftQueue.size()));
    for (const PlayerStateCraftQueue& entry : state.craftQueue) {
        if (entry.count < 1 || entry.count > 99 ||
            !writer.String(entry.recipeId, kMaxPlayerStateStringBytes, false,
                           error)) {
            if (error != nullptr && error->empty()) {
                *error = "玩家状态制作队列项非法";
            }
            return {};
        }
        writer.I32(entry.count);
    }

    writer.U32(state.statusSequence);
    writer.U8(state.statusTone);
    if (!writer.String(state.status, kMaxPlayerStateStatusBytes, true, error)) {
        return {};
    }

    writer.I32(state.progressionLevel);
    writer.F32(state.progressionXp);
    writer.F32(state.progressionXpNext);
    writer.I32(state.perkPoints);
    for (std::size_t i = 0; i < state.skills.size(); ++i) {
        const PlayerStateSkill& skill = state.skills[i];
        const std::string id =
            skill.id.empty() ? std::string(kDefaultSkillIds[i]) : skill.id;
        if (!writer.String(id, 64, false, error)) {
            return {};
        }
        writer.I32(skill.level);
        writer.F32(skill.xp);
        writer.F32(skill.xpNext);
    }
    writer.U16(static_cast<std::uint16_t>(state.perks.size()));
    for (const std::string& perk : state.perks) {
        if (!writer.String(perk, 64, false, error)) {
            return {};
        }
    }
    writer.U16(static_cast<std::uint16_t>(state.perkChoices.size()));
    for (const std::string& perk : state.perkChoices) {
        if (!writer.String(perk, 64, false, error)) {
            return {};
        }
    }

    writer.U8(state.infectionStage);
    writer.F32(state.infectionProgress);
    writer.F32(state.infectionTimeToNextStage);
    writer.F32(state.infectionSlowStrength);
    writer.F32(state.infectionSlowRemaining);
    writer.F32(state.infectionSlowDuration);
    writer.F32(state.climateBodyC);
    writer.F32(state.climateWetness);
    writer.U8(state.siege.phase);
    writer.U8(static_cast<std::uint8_t>(state.siege.primaryDirection + 1));
    writer.U8(static_cast<std::uint8_t>(state.siege.secondaryDirection + 1));
    writer.U8(static_cast<std::uint8_t>(state.siege.currentDirection + 1));
    writer.U8(state.siege.breached ? 1 : 0);
    writer.U8(state.siege.waveIndex);
    writer.U8(state.siege.waveCount);
    writer.U8(state.siege.tone);
    if (!writer.String(state.siege.status, kMaxPlayerStateSiegeBytes, true,
                       error)) {
        return {};
    }
    writer.F32(state.power.fuelMinutes);
    writer.F32(state.power.load);
    writer.U8(state.power.lowFuel ? 1 : 0);
    writer.U8(state.power.outage ? 1 : 0);
    writer.U8(state.siege.targetValid ? 1 : 0);
    writer.F32(state.siege.targetX);
    writer.F32(state.siege.targetY);
    writer.U8(state.siege.breachValid ? 1 : 0);
    writer.F32(state.siege.breachX);
    writer.F32(state.siege.breachY);

    writer.U16(static_cast<std::uint16_t>(state.poiProgress.size()));
    for (const PlayerPoiProgress& poi : state.poiProgress) {
        if (!writer.String(poi.id, 128, false, error)) {
            return {};
        }
        writer.U8(poi.completed ? 1 : 0);
        writer.U8(poi.targetSearched ? 1 : 0);
        writer.I32(poi.liveGuards);
    }
    writer.U16(static_cast<std::uint16_t>(state.missions.size()));
    for (const PlayerMissionProgress& mission : state.missions) {
        if (!writer.String(mission.id, 128, false, error)) {
            return {};
        }
        writer.U16(static_cast<std::uint16_t>(mission.progress.size()));
        for (const int value : mission.progress) {
            writer.I32(value);
        }
        writer.U8(mission.status);
        writer.I32(mission.day);
        writer.U8(mission.rewardClaimed ? 1 : 0);
    }
    writer.I32(state.missionLastDay);
    writer.I32(state.missionRandomDay);

    writer.U8(state.needs.valid ? 1 : 0);
    writer.F32(state.needs.hunger);
    writer.F32(state.needs.thirst);
    writer.F32(state.needs.fatigue);
    writer.U8(state.needs.warning);

    writer.U8(state.sleep.phase);
    writer.U8(state.sleep.tone);
    writer.U8(state.sleep.voted ? 1 : 0);
    writer.U8(state.sleep.yesCount);
    writer.U8(state.sleep.eligibleCount);
    writer.U64(state.sleep.initiator);
    writer.F32(state.sleep.startMinutes);
    writer.F32(state.sleep.targetMinutes);
    writer.F32(state.sleep.voteSeconds);
    writer.F32(state.sleep.progress);
    if (!writer.String(state.sleep.status, kMaxPlayerStateSleepStatusBytes,
                       true, error)) {
        return {};
    }
    writer.U8(state.objective.valid ? 1 : 0);
    writer.U8(state.objective.step);

    for (const PlayerStateEquipment& slot : state.equipment) {
        writer.U16(static_cast<std::uint16_t>(slot.loadedAmmo));
    }
    writer.U8(state.ranged.valid ? 1 : 0);
    writer.U16(static_cast<std::uint16_t>(state.ranged.loaded));
    writer.U16(static_cast<std::uint16_t>(state.ranged.magazine));
    writer.U16(static_cast<std::uint16_t>(state.ranged.reserve));
    writer.U8(state.ranged.reloading ? 1 : 0);
    writer.F32(state.ranged.reloadElapsed);
    writer.F32(state.ranged.reloadDuration);
    writer.U16(static_cast<std::uint16_t>(state.ranged.reloadAmount));
    writer.U32(state.ranged.shotSerial);

    writer.U8(state.survival.mode);
    writer.U8(state.survival.phase);
    if (!writer.String(state.survival.name, kMaxPlayerStateStringBytes, true,
                       error) ||
        !writer.String(state.survival.zoneId, kMaxPlayerStateStringBytes, true,
                       error) ||
        !writer.String(state.survival.tableId, kMaxPlayerStateStringBytes, true,
                       error) ||
        !writer.String(state.survival.spotId, kMaxPlayerStateStringBytes, true,
                       error)) {
        return {};
    }
    writer.I32(state.survival.x);
    writer.I32(state.survival.y);
    writer.I32(state.survival.z);
    writer.I32(state.survival.waterX);
    writer.I32(state.survival.waterY);
    writer.I32(state.survival.waterZ);
    writer.F32(state.survival.progress);
    writer.F32(state.survival.duration);
    writer.F32(state.survival.biteRemaining);
    writer.U16(static_cast<std::uint16_t>(state.forageNodes.size()));
    for (const PlayerForageNodeState& node : state.forageNodes) {
        if (!writer.String(node.zoneId, kMaxPlayerStateStringBytes, false,
                           error)) {
            return {};
        }
        writer.I32(node.x);
        writer.I32(node.y);
        writer.I32(node.z);
        writer.U8(node.ready ? 1 : 0);
        writer.F32(node.respawnMinutes);
        writer.F32(node.seconds);
    }
    writer.U16(static_cast<std::uint16_t>(state.fishingSpots.size()));
    for (const PlayerFishingSpotState& spot : state.fishingSpots) {
        if (!writer.String(spot.spotId, kMaxPlayerStateStringBytes, false,
                           error)) {
            return {};
        }
        writer.I32(spot.x);
        writer.I32(spot.y);
        writer.I32(spot.z);
        writer.I32(spot.waterX);
        writer.I32(spot.waterY);
        writer.I32(spot.waterZ);
        writer.F32(spot.seconds);
    }

    writer.U8(state.animalHarvest.active ? 1 : 0);
    writer.U64(state.animalHarvest.corpseStableId);
    if (!writer.String(state.animalHarvest.speciesId,
                       kMaxPlayerStateStringBytes, true, error) ||
        !writer.String(state.animalHarvest.name, kMaxPlayerStateStringBytes,
                       true, error)) {
        return {};
    }
    writer.I32(state.animalHarvest.x);
    writer.I32(state.animalHarvest.y);
    writer.I32(state.animalHarvest.z);
    writer.F32(state.animalHarvest.progress);
    writer.F32(state.animalHarvest.duration);

    writer.U8(state.food.valid ? 1 : 0);
    writer.F32(state.food.nutrition);
    writer.F32(state.food.poisonRemaining);
    writer.F32(state.food.poisonDamagePerSecond);

    writer.U16(static_cast<std::uint16_t>(state.settlements.size()));
    for (const PlayerSettlementProgress& settlement : state.settlements) {
        if (!writer.String(settlement.id, 128, false, error)) {
            return {};
        }
        std::uint8_t flags = 0;
        if (settlement.fallen) {
            flags |= 1u << 0;
        }
        if (settlement.raidActive) {
            flags |= 1u << 1;
        }
        writer.U8(flags);
        writer.F32(settlement.guardHealth);
        writer.F32(settlement.guardHealthMax);
    }
    writer.U32(state.settlementNoticeSerial);
    writer.U8(state.settlementNoticeTone);
    if (!writer.String(state.settlementNotice,
                       kMaxPlayerStateSettlementNoticeBytes, true, error)) {
        return {};
    }
    writer.U8(state.vehicle.valid ? 1 : 0);
    writer.U64(state.vehicle.vehicleStableId);
    writer.U8(state.vehicle.driving ? 1 : 0);
    writer.U8(state.vehicle.passenger ? 1 : 0);
    writer.F32(state.vehicle.fuel);
    writer.F32(state.vehicle.condition);
    writer.U8(state.vehicle.vehicleType);
    writer.F32(state.vehicle.heading);

    writer.U8(state.combat.valid ? 1 : 0);
    writer.F32(state.combat.stamina);
    writer.F32(state.combat.staminaMax);
    writer.U8(state.combat.stance);
    writer.F32(state.combat.parryWindowLeft);
    writer.F32(state.combat.guardBreakLeft);
    writer.F32(state.combat.dodgeInvulnLeft);

    writer.U16(static_cast<std::uint16_t>(state.settlementReputation.size()));
    for (const PlayerSettlementReputation& entry :
         state.settlementReputation) {
        if (!writer.String(entry.id, 64, false, error)) {
            return {};
        }
        writer.I32(entry.score);
    }

    writer.U16(static_cast<std::uint16_t>(state.poiStageProgress.size()));
    for (const PlayerPoiStageProgress& poi : state.poiStageProgress) {
        if (!writer.String(poi.id, 64, false, error)) {
            return {};
        }
        writer.U8(poi.stageIndex);
        writer.U8(poi.stageCount);
        writer.U8(poi.completedStageCount);
        writer.U8(poi.flags);
        if (!writer.String(poi.currentStage, 64, true, error) ||
            !writer.String(poi.chosenBranch, 64, true, error)) {
            return {};
        }
    }

    writer.U8(state.stealth.valid ? 1 : 0);
    writer.U8(state.stealth.crouched ? 1 : 0);
    writer.F32(state.stealth.noiseRadius);
    writer.F32(state.stealth.visibilityScale);

    if (writer.Bytes().size() > kMaxPlayerStateBytes) {
        SetError(error, "玩家状态超过单包字节上限");
        return {};
    }
    return writer.Bytes();
}

bool DecodePlayerState(std::span<const std::uint8_t> bytes, PlayerState* state,
                       std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (state == nullptr) {
        SetError(error, "玩家状态输出为空");
        return false;
    }
    if (bytes.size() < kPlayerStateHeaderSize || bytes.size() > kMaxPlayerStateBytes) {
        SetError(error, "玩家状态消息长度非法");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    if (!reader.U32(&magic) || magic != kPlayerStateMagic) {
        SetError(error, "玩家状态 magic 不匹配");
        return false;
    }
    if (!reader.U16(&version) || version < 2 ||
        version > kPlayerStateVersion) {
        SetError(error, "玩家状态协议版本不支持");
        return false;
    }
    if (!reader.U16(&reserved) || reserved != 0) {
        SetError(error, "玩家状态保留字段非 0");
        return false;
    }

    PlayerState decoded;
    for (std::size_t i = 0; i < decoded.skills.size(); ++i) {
        decoded.skills[i].id = kDefaultSkillIds[i];
    }
    if (!reader.U64(&decoded.playerStableId) || decoded.playerStableId == 0 ||
        !reader.U64(&decoded.serverTick) || !reader.F32(&decoded.capacityKg) ||
        !std::isfinite(decoded.capacityKg) || decoded.capacityKg <= 0.0f ||
        decoded.capacityKg > 1'000'000.0f || !reader.F32(&decoded.health) ||
        !reader.F32(&decoded.healthMax)) {
        SetError(error, "玩家状态基础字段非法");
        return false;
    }
    std::uint8_t alive = 0;
    if (!reader.U8(&alive) || alive > 1 || !reader.F32(&decoded.respawnSeconds)) {
        SetError(error, "玩家状态存活/复活字段非法");
        return false;
    }
    decoded.alive = alive != 0;
    if (!std::isfinite(decoded.health) || !std::isfinite(decoded.healthMax) ||
        !std::isfinite(decoded.respawnSeconds) || decoded.healthMax <= 0.0f ||
        decoded.health < 0.0f || decoded.health > decoded.healthMax ||
        decoded.respawnSeconds < 0.0f || decoded.respawnSeconds > 86'400.0f ||
        (!decoded.alive && decoded.health > 0.0f)) {
        SetError(error, "玩家状态生命字段非法");
        return false;
    }

    std::uint16_t bagCount = 0;
    if (!reader.U16(&bagCount) || bagCount > kMaxPlayerStateBagItems) {
        SetError(error, "玩家状态背包数量非法");
        return false;
    }
    decoded.bag.resize(bagCount);
    for (PlayerStateItem& item : decoded.bag) {
        if (!ReadItem(reader, &item)) {
            SetError(error, "玩家状态背包物品非法");
            return false;
        }
    }

    for (PlayerStateEquipment& slot : decoded.equipment) {
        std::uint8_t present = 0;
        if (!reader.U8(&present) || present > 1) {
            SetError(error, "玩家状态装备槽非法");
            return false;
        }
        slot.present = present != 0;
        if (slot.present && !ReadItem(reader, &slot.item)) {
            SetError(error, "玩家状态装备物品非法");
            return false;
        }
    }

    std::uint16_t knownCount = 0;
    if (!reader.U16(&knownCount) || knownCount > kMaxPlayerStateKnownRecipes) {
        SetError(error, "玩家状态配方数量非法");
        return false;
    }
    decoded.knownRecipes.resize(knownCount);
    for (std::string& recipeId : decoded.knownRecipes) {
        if (!reader.String(&recipeId, kMaxPlayerStateStringBytes, false)) {
            SetError(error, "玩家状态配方 id 非法");
            return false;
        }
    }

    std::uint8_t craftingActive = 0;
    if (!reader.U8(&craftingActive) || craftingActive > 1) {
        SetError(error, "玩家状态制作标记非法");
        return false;
    }
    decoded.craftingActive = craftingActive != 0;
    if (decoded.craftingActive) {
        std::int32_t batchTotal = 0;
        std::int32_t batchCompleted = 0;
        if (!reader.String(&decoded.craftingRecipeId, kMaxPlayerStateStringBytes,
                           false) ||
            !reader.F32(&decoded.craftingElapsed) ||
            !reader.F32(&decoded.craftingDuration) ||
            !reader.F32(&decoded.craftingToolEfficiency) ||
            !reader.I32(&batchTotal) || !reader.I32(&batchCompleted)) {
            SetError(error, "玩家状态制作字段非法");
            return false;
        }
        decoded.batchTotal = batchTotal;
        decoded.batchCompleted = batchCompleted;
        if (!std::isfinite(decoded.craftingElapsed) ||
            !std::isfinite(decoded.craftingDuration) ||
            !std::isfinite(decoded.craftingToolEfficiency) ||
            decoded.craftingElapsed < 0.0f || decoded.craftingDuration <= 0.0f ||
            decoded.craftingElapsed > decoded.craftingDuration + 1e-4f ||
            decoded.craftingToolEfficiency < 0.0f ||
            decoded.craftingToolEfficiency > 1.0f || decoded.batchTotal < 1 ||
            decoded.batchTotal > 99 || decoded.batchCompleted < 0 ||
            decoded.batchCompleted > decoded.batchTotal) {
            SetError(error, "玩家状态制作数值非法");
            return false;
        }
    }

    std::uint16_t queueCount = 0;
    if (!reader.U16(&queueCount) || queueCount > kMaxPlayerStateCraftQueue) {
        SetError(error, "玩家状态制作队列数量非法");
        return false;
    }
    decoded.craftQueue.resize(queueCount);
    for (PlayerStateCraftQueue& entry : decoded.craftQueue) {
        std::int32_t count = 0;
        if (!reader.String(&entry.recipeId, kMaxPlayerStateStringBytes, false) ||
            !reader.I32(&count) || count < 1 || count > 99) {
            SetError(error, "玩家状态制作队列项非法");
            return false;
        }
        entry.count = count;
    }

    std::uint8_t tone = 0;
    if (!reader.U32(&decoded.statusSequence) || !reader.U8(&tone) || tone > 2 ||
        !reader.String(&decoded.status, kMaxPlayerStateStatusBytes, true)) {
        SetError(error, "玩家状态提示字段非法");
        return false;
    }
    decoded.statusTone = tone;

    if (version >= 3) {
        std::int32_t level = 0;
        std::int32_t perkPoints = 0;
        if (!reader.I32(&level) || !reader.F32(&decoded.progressionXp) ||
            !reader.F32(&decoded.progressionXpNext) ||
            !reader.I32(&perkPoints) || level < 1 || level > 99 ||
            perkPoints < 0 || perkPoints > 99) {
            SetError(error, "玩家状态成长字段非法");
            return false;
        }
        decoded.progressionLevel = level;
        decoded.perkPoints = perkPoints;
        for (PlayerStateSkill& skill : decoded.skills) {
            std::int32_t skillLevel = 0;
            if (!reader.String(&skill.id, 64, false) ||
                !reader.I32(&skillLevel) || !reader.F32(&skill.xp) ||
                !reader.F32(&skill.xpNext) || skillLevel < 0 || skillLevel > 99 ||
                !ValidPerkId(skill.id)) {
                SetError(error, "玩家状态技能字段非法");
                return false;
            }
            skill.level = skillLevel;
        }
        std::uint16_t perkCount = 0;
        if (!reader.U16(&perkCount) || perkCount > kMaxPlayerStatePerks) {
            SetError(error, "玩家状态 Perk 数量非法");
            return false;
        }
        decoded.perks.resize(perkCount);
        for (std::string& perk : decoded.perks) {
            if (!reader.String(&perk, 64, false)) {
                SetError(error, "玩家状态 Perk 非法");
                return false;
            }
        }
        std::uint16_t choiceCount = 0;
        if (!reader.U16(&choiceCount) || choiceCount > 3) {
            SetError(error, "玩家状态 Perk 选项数量非法");
            return false;
        }
        decoded.perkChoices.resize(choiceCount);
        for (std::string& perk : decoded.perkChoices) {
            if (!reader.String(&perk, 64, false)) {
                SetError(error, "玩家状态 Perk 选项非法");
                return false;
            }
        }
        if (!ValidProgression(decoded)) {
            SetError(error, "玩家状态成长数值非法");
            return false;
        }
    }
    if (version >= 4) {
        if (!reader.U8(&decoded.infectionStage) ||
            !reader.F32(&decoded.infectionProgress) ||
            !reader.F32(&decoded.infectionTimeToNextStage) ||
            !reader.F32(&decoded.infectionSlowStrength) ||
            !reader.F32(&decoded.infectionSlowRemaining) ||
            !reader.F32(&decoded.infectionSlowDuration) ||
            !ValidInfection(decoded)) {
            SetError(error, "玩家状态感染字段非法");
            return false;
        }
    }
    if (version >= 5) {
        if (!reader.F32(&decoded.climateBodyC) ||
            !reader.F32(&decoded.climateWetness) || !ValidClimate(decoded)) {
            SetError(error, "玩家状态气候字段非法");
            return false;
        }
    }
    if (version >= 6) {
        std::uint8_t breached = 0;
        const auto readDirection = [&reader](std::int8_t* out) {
            std::uint8_t raw = 0;
            if (!reader.U8(&raw) || raw > 8) {
                return false;
            }
            *out = static_cast<std::int8_t>(raw) - 1;
            return true;
        };
        if (!reader.U8(&decoded.siege.phase) ||
            decoded.siege.phase > 3 ||
            !readDirection(&decoded.siege.primaryDirection) ||
            !readDirection(&decoded.siege.secondaryDirection) ||
            !readDirection(&decoded.siege.currentDirection) ||
            !reader.U8(&breached) || breached > 1 ||
            !reader.U8(&decoded.siege.waveIndex) ||
            !reader.U8(&decoded.siege.waveCount) ||
            !reader.U8(&decoded.siege.tone) || decoded.siege.tone > 2 ||
            !reader.String(&decoded.siege.status, kMaxPlayerStateSiegeBytes,
                           true) ||
            !ValidSiege(decoded)) {
            SetError(error, "玩家状态守夜字段非法");
            return false;
        }
        decoded.siege.breached = breached != 0;
    }
    if (version >= 7) {
        std::uint8_t lowFuel = 0;
        std::uint8_t outage = 0;
        if (!reader.F32(&decoded.power.fuelMinutes) ||
            !reader.F32(&decoded.power.load) || !reader.U8(&lowFuel) ||
            lowFuel > 1 || !reader.U8(&outage) || outage > 1 ||
            !ValidPower(decoded)) {
            SetError(error, "玩家状态电力字段非法");
            return false;
        }
        decoded.power.lowFuel = lowFuel != 0;
        decoded.power.outage = outage != 0;
    }
    if (version >= 8) {
        std::uint8_t targetValid = 0;
        std::uint8_t breachValid = 0;
        if (!reader.U8(&targetValid) || targetValid > 1 ||
            !reader.F32(&decoded.siege.targetX) ||
            !reader.F32(&decoded.siege.targetY) ||
            !reader.U8(&breachValid) || breachValid > 1 ||
            !reader.F32(&decoded.siege.breachX) ||
            !reader.F32(&decoded.siege.breachY) || !ValidSiege(decoded)) {
            SetError(error, "玩家状态破口坐标字段非法");
            return false;
        }
        decoded.siege.targetValid = targetValid != 0;
        decoded.siege.breachValid = breachValid != 0;
    }
    if (version >= 9) {
        std::uint16_t poiCount = 0;
        if (!reader.U16(&poiCount) ||
            poiCount > kMaxPlayerStatePoiProgress) {
            SetError(error, "玩家状态 POI 数量非法");
            return false;
        }
        decoded.poiProgress.resize(poiCount);
        for (PlayerPoiProgress& poi : decoded.poiProgress) {
            std::uint8_t completed = 0;
            std::uint8_t searched = 0;
            std::int32_t guards = 0;
            if (!reader.String(&poi.id, 128, false) ||
                !reader.U8(&completed) || completed > 1 ||
                !reader.U8(&searched) || searched > 1 ||
                !reader.I32(&guards) || guards < 0 || guards > 9999) {
                SetError(error, "玩家状态 POI 字段非法");
                return false;
            }
            poi.completed = completed != 0;
            poi.targetSearched = searched != 0;
            poi.liveGuards = guards;
        }
        std::uint16_t missionCount = 0;
        if (!reader.U16(&missionCount) ||
            missionCount > kMaxPlayerStateMissions) {
            SetError(error, "玩家状态任务数量非法");
            return false;
        }
        decoded.missions.resize(missionCount);
        for (PlayerMissionProgress& mission : decoded.missions) {
            std::uint16_t progressCount = 0;
            std::uint8_t status = 0;
            std::int32_t day = 0;
            std::uint8_t claimed = 0;
            if (!reader.String(&mission.id, 128, false) ||
                !reader.U16(&progressCount) ||
                progressCount > kMaxPlayerStateMissionObjectives) {
                SetError(error, "玩家状态任务字段非法");
                return false;
            }
            mission.progress.resize(progressCount);
            for (int& value : mission.progress) {
                std::int32_t raw = 0;
                if (!reader.I32(&raw) || raw < 0 || raw > 999999) {
                    SetError(error, "玩家状态任务进度非法");
                    return false;
                }
                value = raw;
            }
            if (!reader.U8(&status) || status > 2 ||
                !reader.I32(&day) || day < 0 || day > 1000000 ||
                !reader.U8(&claimed) || claimed > 1) {
                SetError(error, "玩家状态任务字段非法");
                return false;
            }
            mission.status = status;
            mission.day = day;
            mission.rewardClaimed = claimed != 0;
        }
        std::int32_t lastDay = 0;
        std::int32_t randomDay = 0;
        if (!reader.I32(&lastDay) || lastDay < 0 || lastDay > 1000000 ||
            !reader.I32(&randomDay) || randomDay < 0 ||
            randomDay > 1000000) {
            SetError(error, "玩家状态任务日期非法");
            return false;
        }
        decoded.missionLastDay = lastDay;
        decoded.missionRandomDay = randomDay;
        if (!ValidPoiProgress(decoded) || !ValidMissionProgress(decoded)) {
            SetError(error, "玩家状态 POI/任务数值非法");
            return false;
        }
    }
    if (version >= 10) {
        std::uint8_t needsValid = 0;
        if (!reader.U8(&needsValid) || needsValid > 1 ||
            !reader.F32(&decoded.needs.hunger) ||
            !reader.F32(&decoded.needs.thirst) ||
            !reader.F32(&decoded.needs.fatigue) ||
            !reader.U8(&decoded.needs.warning) ||
            decoded.needs.warning > 2) {
            SetError(error, "玩家状态需求字段非法");
            return false;
        }
        decoded.needs.valid = needsValid != 0;
        if (!ValidNeeds(decoded)) {
            SetError(error, "玩家状态需求字段非法");
            return false;
        }
        std::uint8_t voted = 0;
        if (!reader.U8(&decoded.sleep.phase) || decoded.sleep.phase > 2 ||
            !reader.U8(&decoded.sleep.tone) || decoded.sleep.tone > 2 ||
            !reader.U8(&voted) || voted > 1 ||
            !reader.U8(&decoded.sleep.yesCount) ||
            !reader.U8(&decoded.sleep.eligibleCount) ||
            !reader.U64(&decoded.sleep.initiator) ||
            !reader.F32(&decoded.sleep.startMinutes) ||
            !reader.F32(&decoded.sleep.targetMinutes) ||
            !reader.F32(&decoded.sleep.voteSeconds) ||
            !reader.F32(&decoded.sleep.progress) ||
            !reader.String(&decoded.sleep.status,
                           kMaxPlayerStateSleepStatusBytes, true)) {
            SetError(error, "玩家状态睡眠字段非法");
            return false;
        }
        decoded.sleep.voted = voted != 0;
        if (!ValidSleep(decoded)) {
            SetError(error, "玩家状态睡眠字段非法");
            return false;
        }
    }
    if (version >= 11) {
        std::uint8_t valid = 0;
        if (!reader.U8(&valid) || valid > 1 ||
            !reader.U8(&decoded.objective.step)) {
            SetError(error, "玩家状态主目标字段非法");
            return false;
        }
        decoded.objective.valid = valid != 0;
        if (!ValidObjective(decoded)) {
            SetError(error, "玩家状态主目标字段非法");
            return false;
        }
    }
    if (version >= 12) {
        for (PlayerStateEquipment& slot : decoded.equipment) {
            std::uint16_t loadedAmmo = 0;
            if (!reader.U16(&loadedAmmo)) {
                SetError(error, "玩家状态弹匣字段非法");
                return false;
            }
            slot.loadedAmmo = static_cast<int>(loadedAmmo);
        }
        std::uint8_t rangedValid = 0;
        std::uint8_t reloading = 0;
        std::uint16_t loaded = 0;
        std::uint16_t magazine = 0;
        std::uint16_t reserve = 0;
        std::uint16_t reloadAmount = 0;
        if (!reader.U8(&rangedValid) || rangedValid > 1 ||
            !reader.U16(&loaded) || !reader.U16(&magazine) ||
            !reader.U16(&reserve) || !reader.U8(&reloading) ||
            reloading > 1 || !reader.F32(&decoded.ranged.reloadElapsed) ||
            !reader.F32(&decoded.ranged.reloadDuration) ||
            !reader.U16(&reloadAmount) ||
            !reader.U32(&decoded.ranged.shotSerial)) {
            SetError(error, "玩家状态远程武器字段非法");
            return false;
        }
        decoded.ranged.valid = rangedValid != 0;
        decoded.ranged.loaded = static_cast<int>(loaded);
        decoded.ranged.magazine = static_cast<int>(magazine);
        decoded.ranged.reserve = static_cast<int>(reserve);
        decoded.ranged.reloading = reloading != 0;
        decoded.ranged.reloadAmount = static_cast<int>(reloadAmount);
        if (!ValidRangedState(decoded)) {
            SetError(error, "玩家状态远程武器数值非法");
            return false;
        }
    }
    if (version >= 13) {
        if (!reader.U8(&decoded.survival.mode) ||
            !reader.U8(&decoded.survival.phase) ||
            !reader.String(&decoded.survival.name,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.String(&decoded.survival.zoneId,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.String(&decoded.survival.tableId,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.String(&decoded.survival.spotId,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.I32(&decoded.survival.x) ||
            !reader.I32(&decoded.survival.y) ||
            !reader.I32(&decoded.survival.z) ||
            !reader.I32(&decoded.survival.waterX) ||
            !reader.I32(&decoded.survival.waterY) ||
            !reader.I32(&decoded.survival.waterZ) ||
            !reader.F32(&decoded.survival.progress) ||
            !reader.F32(&decoded.survival.duration) ||
            !reader.F32(&decoded.survival.biteRemaining)) {
            SetError(error, "玩家状态采集/钓鱼动作字段非法");
            return false;
        }
        std::uint16_t nodeCount = 0;
        if (!reader.U16(&nodeCount) ||
            nodeCount > kMaxPlayerStateForageNodes) {
            SetError(error, "玩家状态觅食节点数量非法");
            return false;
        }
        decoded.forageNodes.clear();
        decoded.forageNodes.reserve(nodeCount);
        for (std::uint16_t i = 0; i < nodeCount; ++i) {
            PlayerForageNodeState node;
            std::uint8_t ready = 0;
            if (!reader.String(&node.zoneId, kMaxPlayerStateStringBytes, false) ||
                !reader.I32(&node.x) || !reader.I32(&node.y) ||
                !reader.I32(&node.z) || !reader.U8(&ready) || ready > 1 ||
                !reader.F32(&node.respawnMinutes) ||
                !reader.F32(&node.seconds)) {
                SetError(error, "玩家状态觅食节点字段非法");
                return false;
            }
            node.ready = ready != 0;
            decoded.forageNodes.push_back(std::move(node));
        }
        std::uint16_t spotCount = 0;
        if (!reader.U16(&spotCount) ||
            spotCount > kMaxPlayerStateFishingSpots) {
            SetError(error, "玩家状态钓鱼点数量非法");
            return false;
        }
        decoded.fishingSpots.clear();
        decoded.fishingSpots.reserve(spotCount);
        for (std::uint16_t i = 0; i < spotCount; ++i) {
            PlayerFishingSpotState spot;
            if (!reader.String(&spot.spotId, kMaxPlayerStateStringBytes, false) ||
                !reader.I32(&spot.x) || !reader.I32(&spot.y) ||
                !reader.I32(&spot.z) || !reader.I32(&spot.waterX) ||
                !reader.I32(&spot.waterY) || !reader.I32(&spot.waterZ) ||
                !reader.F32(&spot.seconds)) {
                SetError(error, "玩家状态钓鱼点字段非法");
                return false;
            }
            decoded.fishingSpots.push_back(std::move(spot));
        }
        if (!ValidSurvival(decoded)) {
            SetError(error, "玩家状态采集/钓鱼数值非法");
            return false;
        }
    }
    if (version >= 14) {
        std::uint8_t active = 0;
        if (!reader.U8(&active) || active > 1 ||
            !reader.U64(&decoded.animalHarvest.corpseStableId) ||
            !reader.String(&decoded.animalHarvest.speciesId,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.String(&decoded.animalHarvest.name,
                           kMaxPlayerStateStringBytes, true) ||
            !reader.I32(&decoded.animalHarvest.x) ||
            !reader.I32(&decoded.animalHarvest.y) ||
            !reader.I32(&decoded.animalHarvest.z) ||
            !reader.F32(&decoded.animalHarvest.progress) ||
            !reader.F32(&decoded.animalHarvest.duration)) {
            SetError(error, "玩家状态动物剥取字段非法");
            return false;
        }
        decoded.animalHarvest.active = active != 0;
        if (!ValidAnimalHarvest(decoded)) {
            SetError(error, "玩家状态动物剥取数值非法");
            return false;
        }
    }
    if (version >= 15) {
        std::uint8_t valid = 0;
        if (!reader.U8(&valid) || valid > 1 ||
            !reader.F32(&decoded.food.nutrition) ||
            !reader.F32(&decoded.food.poisonRemaining) ||
            !reader.F32(&decoded.food.poisonDamagePerSecond)) {
            SetError(error, "玩家状态食物字段非法");
            return false;
        }
        decoded.food.valid = valid != 0;
        if (!ValidFood(decoded)) {
            SetError(error, "玩家状态食物数值非法");
            return false;
        }
    }
    if (version >= 16) {
        std::uint16_t count = 0;
        if (!reader.U16(&count) || count > kMaxPlayerStateSettlements) {
            SetError(error, "玩家状态聚落数量非法");
            return false;
        }
        decoded.settlements.resize(count);
        for (PlayerSettlementProgress& settlement : decoded.settlements) {
            std::uint8_t flags = 0;
            if (!reader.String(&settlement.id, 128, false) ||
                !reader.U8(&flags) || flags > 3 ||
                !reader.F32(&settlement.guardHealth) ||
                !reader.F32(&settlement.guardHealthMax)) {
                SetError(error, "玩家状态聚落字段非法");
                return false;
            }
            settlement.fallen = (flags & 1u) != 0;
            settlement.raidActive = (flags & 2u) != 0;
        }
        if (!reader.U32(&decoded.settlementNoticeSerial) ||
            !reader.U8(&decoded.settlementNoticeTone) ||
            !reader.String(&decoded.settlementNotice,
                           kMaxPlayerStateSettlementNoticeBytes, true)) {
            SetError(error, "玩家状态聚落提示非法");
            return false;
        }
        if (!ValidSettlementProgress(decoded)) {
            SetError(error, "玩家状态聚落数值非法");
            return false;
        }
    }
    if (version >= 17) {
        std::uint8_t valid = 0;
        std::uint8_t driving = 0;
        std::uint8_t passenger = 0;
        if (!reader.U8(&valid) || valid > 1 ||
            !reader.U64(&decoded.vehicle.vehicleStableId) ||
            !reader.U8(&driving) || driving > 1 ||
            (version >= 18 &&
             (!reader.U8(&passenger) || passenger > 1)) ||
            !reader.F32(&decoded.vehicle.fuel) ||
            !reader.F32(&decoded.vehicle.condition) ||
            (version >= 19 &&
             (!reader.U8(&decoded.vehicle.vehicleType) ||
              !reader.F32(&decoded.vehicle.heading)))) {
            SetError(error, "玩家状态载具字段非法");
            return false;
        }
        if (version < 19) {
            decoded.vehicle.vehicleType = 1;
            decoded.vehicle.heading = 1.5707963267948966f;
        }
        decoded.vehicle.valid = valid != 0;
        decoded.vehicle.driving = driving != 0;
        decoded.vehicle.passenger = passenger != 0;
        if (!ValidVehicle(decoded)) {
            SetError(error, "玩家状态载具数值非法");
            return false;
        }
    }
    if (version >= 20) {
        std::uint8_t valid = 0;
        if (!reader.U8(&valid) || valid > 1 ||
            !reader.F32(&decoded.combat.stamina) ||
            !reader.F32(&decoded.combat.staminaMax) ||
            !reader.U8(&decoded.combat.stance) ||
            !reader.F32(&decoded.combat.parryWindowLeft) ||
            !reader.F32(&decoded.combat.guardBreakLeft) ||
            !reader.F32(&decoded.combat.dodgeInvulnLeft)) {
            SetError(error, "玩家状态战斗字段非法");
            return false;
        }
        decoded.combat.valid = valid != 0;
        if (!ValidCombat(decoded)) {
            SetError(error, "玩家状态战斗数值非法");
            return false;
        }
    }
    if (version >= 21) {
        std::uint16_t count = 0;
        if (!reader.U16(&count) ||
            count > kMaxPlayerStateSettlementReputation) {
            SetError(error, "玩家状态聚落声望数量非法");
            return false;
        }
        decoded.settlementReputation.resize(count);
        for (PlayerSettlementReputation& entry :
             decoded.settlementReputation) {
            std::int32_t score = 0;
            if (!reader.String(&entry.id, 64, false) || !reader.I32(&score)) {
                SetError(error, "玩家状态聚落声望字段非法");
                return false;
            }
            entry.score = static_cast<int>(score);
        }
        if (!ValidSettlementReputation(decoded)) {
            SetError(error, "玩家状态聚落声望数值非法");
            return false;
        }
    }
    if (version >= 22) {
        std::uint16_t count = 0;
        if (!reader.U16(&count) || count > kMaxPlayerStatePoiStages) {
            SetError(error, "玩家状态 POI 阶段数量非法");
            return false;
        }
        decoded.poiStageProgress.resize(count);
        for (PlayerPoiStageProgress& poi : decoded.poiStageProgress) {
            if (!reader.String(&poi.id, 64, false) ||
                !reader.U8(&poi.stageIndex) ||
                !reader.U8(&poi.stageCount) ||
                !reader.U8(&poi.completedStageCount) ||
                !reader.U8(&poi.flags) ||
                !reader.String(&poi.currentStage, 64, true) ||
                !reader.String(&poi.chosenBranch, 64, true)) {
                SetError(error, "玩家状态 POI 阶段字段非法");
                return false;
            }
        }
        if (!ValidPoiStageProgress(decoded)) {
            SetError(error, "玩家状态 POI 阶段数值非法");
            return false;
        }
    }
    if (version >= 23) {
        std::uint8_t valid = 0;
        std::uint8_t crouched = 0;
        if (!reader.U8(&valid) || valid > 1 || !reader.U8(&crouched) ||
            crouched > 1 || !reader.F32(&decoded.stealth.noiseRadius) ||
            !reader.F32(&decoded.stealth.visibilityScale)) {
            SetError(error, "玩家状态潜行字段非法");
            return false;
        }
        decoded.stealth.valid = valid != 0;
        decoded.stealth.crouched = crouched != 0;
        if (!ValidStealth(decoded)) {
            SetError(error, "玩家状态潜行数值非法");
            return false;
        }
    }
    if (!reader.AtEnd()) {
        SetError(error, "玩家状态存在尾随数据");
        return false;
    }
    *state = std::move(decoded);
    return true;
}

}  // namespace zl::net
