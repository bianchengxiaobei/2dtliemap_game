#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "game/RegionalSiege.h"
#include "game/Weather.h"

namespace zl::ai {
class AiSystem;
class BoidsSystem;
class LodSystem;
class PathfindingService;
class PerceptionSystem;
class SpawnLodSystem;
class VirtualHordeStore;
class ZombieSpecialSystem;
}  // namespace zl::ai

namespace zl::characters {
class CombatSystem;
}  // namespace zl::characters

namespace zl::core {
class Config;
class EventBus;
class GameTime;
class TaskScheduler;
}  // namespace zl::core

namespace zl::ecs {
class EcsWorld;
class SpatialIndex;
enum class ZombieVariantKind : std::uint8_t;
}  // namespace zl::ecs

namespace zl::physics {
class PhysicsWorld;
}

namespace zl::items {
class ItemRegistry;
class LootTableRegistry;
}

namespace zl::save {
class ComponentRegistry;
class SaveManager;
class SaveService;
struct SavedEntity;
struct WorldManifest;
}  // namespace zl::save

namespace zl::world {
class ChunkManager;
class RegionStreaming;
class World;
class WorldGraph;
}  // namespace zl::world

namespace zl::game {

class AnimalRegistry;
class AnimalSystem;
class FireRegistry;
class FireSystem;
class RegionEventDirector;
class WorldEventDirector;
class SettlementNpcSystem;
struct CompanionConfig;
struct CompanionDef;
struct PoiConfig;
struct WorldEventConfig;
struct RegionEvent;
struct SettlementConfig;
struct StrategicConfig;
struct StrategicState;
struct StrategicTickInput;
struct StrategicTickReport;
struct TrapDefsConfig;

struct WorldEventSettlement {
    bool applied = false;
    bool alreadyApplied = false;
    bool itemDropped = false;
    int reputationDelta = 0;
    int reputationScore = 0;
    float xp = 0.0f;
    std::string item;
    int itemCount = 0;
    int pressureDelta = 0;
    float pressureBefore = 0.0f;
    float pressureAfter = 0.0f;
    std::string choiceId;
};

// M30-T7锛氫粠 config/game.json 鐨?combat.rules.* 璇诲彇鎴樻枟鍙傛暟锛?
// 缂哄け閿娇鐢?CombatRuleConfig 榛樿鍊硷紝杩斿洖鍓嶇粺涓€ Sanitize锛堝鎴风/鏈嶅姟鍣ㄥ叡鐢級銆?
characters::CombatRuleConfig LoadCombatRuleConfig(const core::Config& config);

// M13-T3锛氭父鎴忔ā鎷熺殑鍗曚竴缁勮鐐广€?
//
// GameApp锛堝甫绐楀彛锛変笌 ServerHost锛堟棤绐楀彛锛夐兘閫氳繃鏈被鎸佹湁鍚屼竴濂椾笘鐣屻€丒CS銆佺墿鐞嗐€?
// 瀵昏矾銆丄I 绯荤粺涓庡浐瀹氶摼銆傛覆鏌?UI/杈撳叆鍙€氳繃 GameContext 璇诲彇妯℃嫙鐘舵€侊紝涓嶅弽鍚戜緷璧栥€?
class WorldSimulation {
public:
    struct Options {
        core::Config* config = nullptr;
        core::EventBus* events = nullptr;
        core::GameTime* gameTime = nullptr;
        items::ItemRegistry* items = nullptr;
        const items::LootTableRegistry* lootTables = nullptr;
        const AnimalRegistry* animals = nullptr;
        const PoiConfig* pois = nullptr;
        const SettlementConfig* settlements = nullptr;
        const WorldEventConfig* worldEvents = nullptr;
        const StrategicConfig* strategic = nullptr;
        const CompanionConfig* companions = nullptr;
        const TrapDefsConfig* traps = nullptr;
        bool spawnAnimals = false;
        FireRegistry* fire = nullptr;
        core::TaskScheduler* tasks = nullptr;
        save::SaveManager* saveManager = nullptr;
        save::SaveService* saveService = nullptr;
        save::ComponentRegistry* saveComponents = nullptr;
        std::string* activeSaveId = nullptr;
        bool* activeSaveUseBackup = nullptr;
        std::string* saveWarning = nullptr;
        // Explicit map selection from WorldSelection; empty keeps the legacy
        // world.mapDir lookup for direct tests and older callers.
        std::string mapDir;
        glm::vec2 focus{70.0f, 18.0f};
    };

    explicit WorldSimulation(Options options);
    ~WorldSimulation();

    WorldSimulation(const WorldSimulation&) = delete;
    WorldSimulation& operator=(const WorldSimulation&) = delete;

    // 鍔犺浇鍦板浘鍏冩暟鎹苟鍒涘缓涓栫晫/鐗╃悊/瀵昏矾/绯荤粺锛涗笉鍒涘缓绐楀彛銆佷笉鍒濆鍖栨覆鏌撱€?
    bool Init(std::string* error = nullptr);
    void Shutdown();
    void SetTrapDefs(const TrapDefsConfig* traps);

    // 鍥哄畾 tick 浣跨敤鐨勭劍鐐癸紙鍗曟満=鐩告満涓績锛屾湇鍔″櫒=鐜╁闄勮繎锛夛紱褰卞搷 LOD/鍒锋€€?
    void SetFocus(float gridX, float gridY);
    // M31-T8: server interest union. The first focus remains the primary focus
    // for spawn/AI systems; every focus contributes to chunks and region phases.
    void SetFocuses(const std::vector<glm::vec2>& focuses);
    glm::vec2 Focus() const;

    // chunk 娴佸紡鍔犺浇锛歎pdate 娲惧彂璇锋眰锛孭rocessResults 鍦ㄤ富绾跨▼鎸傛帴/鍗歌浇銆?
    void Stream();
    // M31-T3: streaming focus is the rendered camera (single player) or the
    // player (server). Velocity is grid/sec and drives the look-ahead window.
    void StreamAt(float focusX, float focusY, float velocityX = 0.0f,
                  float velocityY = 0.0f);
    void StreamAtFocuses(const std::vector<glm::vec2>& focuses);
    world::RegionStreaming* Regions();
    // M31-T5: road/region/density graph. Null for legacy maps without world.json.
    world::WorldGraph* Graph();
    // M31-T6: regional event state; null for legacy maps.
    RegionEventDirector* RegionEvents();
    // M33-F2-T2: interactive world event state; null when disabled/legacy maps.
    WorldEventDirector* WorldEvents();
    const WorldEventDirector* WorldEvents() const;
    bool ChooseWorldEvent(std::string_view eventId, std::string_view choiceId,
                          ecs::Entity player,
                          WorldEventSettlement* out = nullptr,
                          std::string* error = nullptr);
    bool SettleWorldEvent(std::string_view eventId, ecs::Entity player,
                          WorldEventSettlement* out = nullptr,
                          std::string* error = nullptr);
    // M32-T1: persistent strategic state; null when strategic.json is absent
    // or the current map has no matching regions.
    StrategicState* Strategic();
    const StrategicState* Strategic() const;
    const StrategicConfig* StrategicSettings() const;
    bool TickStrategic(const StrategicTickInput& input,
                       StrategicTickReport* report = nullptr,
                       std::string* error = nullptr);
    // M32-T4：玩家在场时由战略 Active 启动；同一时间只允许一个详细围攻。
    bool StartRegionalSiege(std::string_view regionId);
    // M32-T5：失守区域完成清理核心/修复设施后，玩家在锚点启动反攻波次。
    bool StartRegionalReclaim(std::string_view regionId);
    bool ClearRegionalRecoveryCore(std::string_view regionId,
                                   std::string* error = nullptr);
    bool RepairRegionalRecoveryFacility(std::string_view regionId,
                                        std::string* error = nullptr);
    // M32-T6：护送/补给等任务完成时结算战略奖励，幂等。
    bool GrantStrategicMissionReward(std::string_view missionId,
                                     std::string* error = nullptr);
    // M32-T7：联机“支援/捐献”只转移战略库存，不生成商队实体。
    // 服务器必须先校验玩家在目标区域；本接口只校验资源和区域上限。
    bool DonateStrategicSupply(std::string_view regionId, int amount,
                               std::uint64_t donorStableId,
                               std::string* error = nullptr,
                               int* deliveredOut = nullptr);
    // 玩家离开区域会中止详细战斗并清理围攻实体；战略 Active 保留，
    // 由 autoResolveGraceMinutes 回落自动结算。
    void UpdateRegionalSiege(float dt, bool paused,
                             std::string_view playerRegion);
    const RegionalSiegeSnapshot& RegionalSiegeStatus() const;
    const RegionalSiegeResult& RegionalSiegeLastResult() const;
    bool CaptureStrategicToManifest(save::WorldManifest* manifest,
                                    std::string* error = nullptr) const;
    bool RestoreStrategicFromManifest(const save::WorldManifest& manifest,
                                      std::string* error = nullptr);
    // M33-F2-T5: persist interactive event state and idempotent outcome keys.
    bool CaptureWorldEventsToManifest(save::WorldManifest* manifest,
                                      std::string* error = nullptr) const;
    bool RestoreWorldEventsFromManifest(const save::WorldManifest& manifest,
                                        std::string* error = nullptr);

    // 鍥哄畾閾炬帹杩涳紱涓嶅寘鍚?chunk 娴佸紡鍔犺浇锛堢敱 Stream 璐熻矗锛夈€?
    void Tick(float dt);
    void Flush();

    // 鍦ㄧ洰鏍囨牸闄勮繎绛夊緟 chunk 灏辩华骞剁敓鎴愭湰鍦扮帺瀹讹紙宸插瓨鍦ㄥ垯鐩存帴杩斿洖锛夈€?
    ecs::Entity EnsureLocalPlayer(float targetX, float targetY, int searchRadius = 24);

    // M13-T5锛氫负缃戠粶浼氳瘽鐢熸垚涓€涓湇鍔″櫒鏉冨▉鐜╁銆傛瘡涓?PlayerTag 鍙嫢鏈夌嫭绔?MoveInput锛?
    // 鏈嶅姟鍣ㄥ懡浠ゆ寜 StableId 鎵惧埌瀵瑰簲瀹炰綋銆俢hunk 鏈氨缁椂杩斿洖 entt::null锛岀敱璋冪敤鏂归噸璇曘€?
    // M13-T8锛歴tableId 闈?0 鏃朵娇鐢ㄦ湇鍔″櫒妗ｆ鍒嗛厤鐨勬寔涔?StableId銆?
    ecs::Entity SpawnNetworkPlayer(float targetX, float targetY, int searchRadius = 24,
                                   std::uint64_t stableId = 0);

    // M13-T8锛氫粠鐜╁妗ｆ蹇収鎭㈠鍚屼竴 StableId 鐨勫疄浣擄紙淇濈暀浣嶇疆/鑳屽寘/瑁呭/鐢熷懡锛夈€?
    // 涓嶅仛鈥滄渶杩戝彲琛岃蛋鏍尖€濇悳绱紝淇濆瓨鏃剁殑浣嶇疆搴旀湁鏁堬紱璇绘。鍚庣殑 chunk 娴佸紡鍔犺浇鐢辫皟鐢ㄦ柟璐熻矗銆?
    ecs::Entity RestoreNetworkPlayer(const save::SavedEntity& snapshot, bool resetHealth,
                                     std::string* error = nullptr);

    // M13-T8锛氭浜″悗鍦ㄦ寚瀹氬嚭鐢熺偣闄勮繎澶嶆椿锛沜hunk 鏈氨缁?浣嶇疆涓嶅彲鐢ㄦ椂杩斿洖 null 渚涜皟鐢ㄦ柟閲嶈瘯銆?
    ecs::Entity RespawnNetworkPlayer(const save::SavedEntity& snapshot, float targetX,
                                     float targetY, int searchRadius, bool resetHealth,
                                     std::string* error = nullptr);

    // 娴嬭瘯/宸ュ叿锛氬湪鎸囧畾鏍肩敓鎴愪竴涓父鑽¤€咃紙涓嶇瓑寰?chunk锛涚敱璋冪敤鏂逛繚璇佷綅缃湁鏁堬級銆?
    // M24-T4锛歴tableId 闈?0 鏃舵寜 StableId 骞傜瓑鐢熸垚锛屼緵闅忔満浜嬩欢/瀛樻。鍘婚噸銆?
    ecs::Entity SpawnZombieAt(float x, float y,
                              std::uint64_t stableId = 0);
    // M19-F2锛歅OI 瀹堝啗绛夐渶瑕佹寚瀹氬彉浣撴椂浣跨敤锛涗粛鐢辫皟鐢ㄦ柟淇濊瘉浣嶇疆鍙蛋銆?
    ecs::Entity SpawnZombieAt(float x, float y, ecs::ZombieVariantKind variant,
                              std::uint64_t stableId = 0);

    ecs::Entity SpawnCompanionAt(const CompanionDef& def,
                                 std::uint64_t ownerStableId,
                                 std::string_view homeSettlementId,
                                 float targetX, float targetY,
                                 int searchRadius = 4);
    bool TeleportCompanion(ecs::Entity companion, float targetX, float targetY,
                           int searchRadius = 4);
    bool RescueCompanion(ecs::Entity companion, ecs::Entity rescuer,
                         std::string* error = nullptr);

    // 鏈嶅姟鍣ㄤ細璇濇竻鐞嗭細閿€姣佺帺瀹?浠绘剰瀹炰綋鍙婄墿鐞嗕綋锛涗細寤惰繜鍒颁笅涓€娆?Flush 鍥炴敹銆?
    void DespawnEntity(ecs::Entity entity);

    // 婕旂ず浜哄彛锛氬疄浣撳兊灏?+ 铏氭嫙缇よ惤锛屾暟閲忕敱 spawner.demo* 閰嶇疆锛涜妗ｆ椂涓嶈璋冪敤銆?
    void SpawnDemoPopulation();

    // 娓呯┖瀹炰綋骞惰鍙栧瓨妗ｅ疄浣撳揩鐓э紱瑕佹眰 SaveManager/ComponentRegistry 宸插垵濮嬪寲銆?
    bool LoadSave(const std::string& saveId, std::string* error = nullptr);

    ecs::Entity Player() const;
    ecs::EcsWorld& Ecs();
    ecs::SpatialIndex& Spatial();
    world::World& World();
    world::ChunkManager& Chunks();
    physics::PhysicsWorld& Physics();
    ai::PathfindingService* Pathfinding();
    ai::AiSystem* Ai();
    ai::PerceptionSystem* Perception();
    ai::LodSystem* Lod();
    ai::BoidsSystem* Boids();
    ai::VirtualHordeStore* VirtualHordes();
    ai::SpawnLodSystem* SpawnLod();
    ai::ZombieSpecialSystem* ZombieSpecial();
    characters::CombatSystem* Combat();
    // M30-T4锛氬彇璧版湰甯х疮绉殑鏍兼尅/寮瑰弽/鐮撮槻/闂伩鍙嶉锛屼緵琛ㄧ幇灞傛秷璐广€?
    std::vector<characters::CombatSystem::BlockFeedback> TakeBlockFeedbacks();
    // M30-T5锛氬彇璧版湰甯х疮绉殑鐪熷疄鍛戒腑/鐮撮槻鍙嶉銆?
    std::vector<characters::CombatSystem::HitFeedback> TakeHitFeedbacks();
    SettlementNpcSystem* SettlementNpcs();
    FireSystem* Fire();
    AnimalSystem* Animals();

    // M20-F2锛氬綋鍓嶅ぉ姘旂敱娓告垙鏃堕棿纭畾鎬ч噰鏍凤紝鍙殢鏃朵緵 HUD/鑴氭湰璇诲彇銆?
    WeatherState CurrentWeather() const;
    // M24-T4锛氬瘨娼簨浠朵复鏃惰鐩栧ぉ姘旓紱鍙瓨鍦ㄨ繍琛屾椂锛屼笉鍐欏瓨妗ｃ€?
    void SetWeatherOverride(WeatherKind kind, float durationMinutes);
    void ClearWeatherOverride();

private:
    int SpawnRegionEventContent(const RegionEvent& event);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::game
