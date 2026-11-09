#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zl::core {
class Config;
}

namespace zl::game {

inline constexpr std::size_t kSettlementMerchantStockCount = 5;
inline constexpr std::size_t kMaxSettlementNpcPoints = 16;
inline constexpr std::size_t kMaxSettlementMissionPool = 32;

// M23-F4-T1：最小幸存者聚落数据。地图/POI/商人/任务仍使用现有系统，
// 本层只负责聚落边界、NPC 点位与交易/任务池的数据契约。
enum class SettlementNpcKind : std::uint8_t {
    Merchant = 0,
    Guard = 1,
    Requester = 2,
};

// M23-F4-T5：夜袭时守卫的行为策略。Defend 会主动迎战，Retreat 与平民一起撤到中心。
enum class SettlementGuardBehavior : std::uint8_t {
    Defend = 0,
    Retreat = 1,
};

struct SettlementNpcPoint {
    std::string id;
    SettlementNpcKind kind = SettlementNpcKind::Guard;
    float x = 0.0f;
    float y = 0.0f;
    float wanderRadius = 2.0f;
};

// M23-F4-T5：聚落夜袭参数。默认关闭，避免旧数据/旧存档在夜里意外触发；
// 基础数据显式开启。所有战斗数值仍走数据，不写死在系统里。
struct SettlementDefenseConfig {
    bool enabled = false;
    int minDay = 1;
    int nightStartHour = 22;
    int nightEndHour = 5;
    float activationRadius = 24.0f;
    float spawnRadius = 13.0f;
    int baseRaiders = 2;
    int raidersPerNight = 1;
    int maxRaiders = 4;
    SettlementGuardBehavior guardBehavior = SettlementGuardBehavior::Defend;
    float guardHealth = 120.0f;
    float guardDamage = 14.0f;
    float guardRange = 1.5f;
    float guardAttackSeconds = 1.0f;
    float raiderHealth = 50.0f;
    float raiderDamage = 5.0f;
    float raiderRange = 1.2f;
    float raiderAttackSeconds = 1.5f;
    float raiderSpeed = 2.2f;
};

struct SettlementDef {
    std::string id;
    std::string name;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float safeRadius = 8.0f;
    std::vector<SettlementNpcPoint> npcPoints;
    std::array<int, kSettlementMerchantStockCount> merchantStock{{4, 2, 1, 2, 1}};
    int restockDays = 1;
    std::vector<std::string> missionPool;
    SettlementDefenseConfig defense;

    const SettlementNpcPoint* FindNpc(SettlementNpcKind kind) const {
        for (const SettlementNpcPoint& point : npcPoints) {
            if (point.kind == kind) {
                return &point;
            }
        }
        return nullptr;
    }
};

struct SettlementConfig {
    bool enabled = false;
    std::vector<SettlementDef> defs;

    const SettlementDef* Find(std::string_view id) const {
        for (const SettlementDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

// 读取 config/game.json 的 settlements.file；缺失/非法时返回 false。
bool LoadSettlementConfig(const core::Config& config, SettlementConfig* out,
                          std::string* error);

struct SettlementMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

// mod content/settlements.json 只追加新 id；单条非法定义跳过。
bool MergeSettlementFile(const std::filesystem::path& path,
                         SettlementConfig* out,
                         SettlementMergeStats* stats = nullptr,
                         std::string* error = nullptr);
bool MergeSettlementJson(std::string_view json, const std::string& source,
                         SettlementConfig* out,
                         SettlementMergeStats* stats = nullptr,
                         std::string* error = nullptr);

const char* SettlementNpcKindName(SettlementNpcKind kind);
bool ParseSettlementNpcKind(std::string_view value, SettlementNpcKind* out);
const char* SettlementGuardBehaviorName(SettlementGuardBehavior behavior);
bool ParseSettlementGuardBehavior(std::string_view value,
                                  SettlementGuardBehavior* out);
const SettlementNpcPoint* FindSettlementNpc(const SettlementDef& def,
                                            SettlementNpcKind kind);
const SettlementDef* PrimarySettlement(const SettlementConfig& config);
const SettlementDef* FindSettlementContaining(const SettlementConfig& config,
                                              float x, float y);
bool IsInsideSettlement(const SettlementDef& def, float x, float y);

}  // namespace zl::game
