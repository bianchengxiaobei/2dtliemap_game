#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/MissionComponents.h"

namespace zl::core {
class Config;
}

namespace zl::game {

enum class MissionKind : std::uint8_t {
    Once = 0,
    Daily = 1,
    Random = 2,
};

enum class MissionSource : std::uint8_t {
    Radio = 0,
    Note = 1,
    Npc = 2,
    Base = 3,
};

enum class MissionTaskKind : std::uint8_t {
    Generic = 0,
    Help = 1,
    Escort = 2,
    Clear = 3,
    Supply = 4,
};

enum class MissionObjectiveKind : std::uint8_t {
    CollectItem = 0,
    Interact = 1,
    Kill = 2,
    Loot = 3,
    Poi = 4,
    Reach = 5,
};

struct MissionObjectiveDef {
    MissionObjectiveKind kind = MissionObjectiveKind::CollectItem;
    std::string target;
    int count = 1;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 1.5f;
    std::string description;
};

struct MissionRewardDef {
    float xp = 0.0f;
    std::string skill = "survival";
    std::string item;
    int itemCount = 1;
    std::string extraItem;
    int extraItemCount = 1;
    std::string blueprint;
    std::string label;
};

struct MissionDef {
    std::string id;
    std::string name;
    std::string chain;
    int order = 1;
    MissionKind kind = MissionKind::Once;
    MissionSource source = MissionSource::Radio;
    MissionTaskKind task = MissionTaskKind::Generic;
    std::string prerequisite;
    int minDay = 1;
    float chance = 0.5f;
    bool autoActivate = true;
    int timeLimitDays = 0;
    int retryDays = 1;
    std::vector<MissionObjectiveDef> objectives;
    MissionRewardDef reward;
};

struct MissionConfig {
    bool enabled = false;
    std::uint32_t randomSeed = 20261024u;
    std::vector<MissionDef> defs;

    const MissionDef* Find(std::string_view id) const {
        for (const MissionDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

struct MissionMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
};

bool LoadMissionConfig(const core::Config& config, MissionConfig* out,
                       std::string* error);
bool MergeMissionFiles(const std::vector<std::string>& files,
                       const core::Config& config, MissionConfig* out,
                       MissionMergeStats* stats, std::string* error,
                       std::string* acceptedJson = nullptr);

const char* MissionKindName(MissionKind kind);
const char* MissionSourceName(MissionSource source);
const char* MissionSourceLabel(MissionSource source);
const char* MissionTaskKindName(MissionTaskKind kind);
const char* MissionTaskKindLabel(MissionTaskKind kind);
const char* MissionObjectiveKindName(MissionObjectiveKind kind);
const char* MissionRunStatusName(ecs::MissionRunStatus status);

const ecs::MissionRunState* FindMissionState(const ecs::MissionLog& log,
                                             std::string_view id);
ecs::MissionRunState* FindMissionState(ecs::MissionLog* log,
                                       std::string_view id);

int MissionObjectiveProgress(const MissionObjectiveDef& objective,
                             const ecs::MissionRunState* state,
                             std::size_t index);
bool IsMissionObjectiveComplete(const MissionObjectiveDef& objective,
                                const ecs::MissionRunState* state,
                                std::size_t index);
bool IsMissionComplete(const MissionDef& def,
                       const ecs::MissionRunState& state);

std::string MissionObjectiveLine(const MissionObjectiveDef& objective,
                                 const ecs::MissionRunState* state,
                                 std::size_t index);
std::string MissionGuideText(const MissionDef& def,
                             const ecs::MissionRunState& state,
                             std::string_view directionHint = {});
const char* MissionGuideTone(const MissionDef& def);
std::string MissionEventGuideText(const MissionDef& def,
                                  const ecs::MissionRunState& state,
                                  std::string_view directionHint = {});

struct MissionFocusCandidate {
    const MissionDef* def = nullptr;
    float distance = 0.0f;
};

// 焦点顺序：随机事件优先（当日时限），其次主线/支线，最后每日；同优先级按距离、链顺序与 ID。
int MissionFocusPriority(const MissionDef& def);
bool MissionFocusLess(const MissionFocusCandidate& a,
                      const MissionFocusCandidate& b);

bool MissionRandomRoll(std::uint32_t seed, int day, std::string_view id,
                       float chance);
bool MissionHasNextStep(const MissionConfig& config, const MissionDef& def);
bool MissionCanActivate(const MissionDef& def, const ecs::MissionLog& log,
                        int day);
int MissionDeadlineDay(const MissionDef& def,
                       const ecs::MissionRunState& state);
bool MissionIsExpired(const MissionDef& def,
                      const ecs::MissionRunState& state, int day);
bool AbandonMission(ecs::MissionLog* log, std::string_view id,
                    int day = 0);

}  // namespace zl::game
