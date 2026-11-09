#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
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
}

namespace zl::save {
class ComponentRegistry;
class SaveManager;
struct SavedEntity;
}  // namespace zl::save

namespace zl::world {
class ChunkManager;
class World;
}  // namespace zl::world

namespace zl::game {

class AnimalRegistry;
class AnimalSystem;
class FireRegistry;
class FireSystem;
class SettlementNpcSystem;
struct PoiConfig;
struct SettlementConfig;

// M30-T7：从 config/game.json 的 combat.rules.* 读取战斗参数；
// 缺失键使用 CombatRuleConfig 默认值，返回前统一 Sanitize（客户端/服务器共用）。
characters::CombatRuleConfig LoadCombatRuleConfig(const core::Config& config);

// M13-T3：游戏模拟的单一组装点。
//
// GameApp（带窗口）与 ServerHost（无窗口）都通过本类持有同一套世界、ECS、物理、
// 寻路、AI 系统与固定链。渲染/UI/输入只通过 GameContext 读取模拟状态，不反向依赖。
class WorldSimulation {
public:
    struct Options {
        core::Config* config = nullptr;
        core::EventBus* events = nullptr;
        core::GameTime* gameTime = nullptr;
        items::ItemRegistry* items = nullptr;
        const AnimalRegistry* animals = nullptr;
        const PoiConfig* pois = nullptr;
        const SettlementConfig* settlements = nullptr;
        bool spawnAnimals = false;
        FireRegistry* fire = nullptr;
        core::TaskScheduler* tasks = nullptr;
        save::SaveManager* saveManager = nullptr;
        save::ComponentRegistry* saveComponents = nullptr;
        std::string* activeSaveId = nullptr;
        bool* activeSaveUseBackup = nullptr;
        std::string* saveWarning = nullptr;
        glm::vec2 focus{70.0f, 18.0f};
    };

    explicit WorldSimulation(Options options);
    ~WorldSimulation();

    WorldSimulation(const WorldSimulation&) = delete;
    WorldSimulation& operator=(const WorldSimulation&) = delete;

    // 加载地图元数据并创建世界/物理/寻路/系统；不创建窗口、不初始化渲染。
    bool Init(std::string* error = nullptr);
    void Shutdown();

    // 固定 tick 使用的焦点（单机=相机中心，服务器=玩家附近）；影响 LOD/刷怪。
    void SetFocus(float gridX, float gridY);
    glm::vec2 Focus() const;

    // chunk 流式加载：Update 派发请求，ProcessResults 在主线程挂接/卸载。
    void Stream();

    // 固定链推进；不包含 chunk 流式加载（由 Stream 负责）。
    void Tick(float dt);
    void Flush();

    // 在目标格附近等待 chunk 就绪并生成本地玩家（已存在则直接返回）。
    ecs::Entity EnsureLocalPlayer(float targetX, float targetY, int searchRadius = 24);

    // M13-T5：为网络会话生成一个服务器权威玩家。每个 PlayerTag 可拥有独立 MoveInput；
    // 服务器命令按 StableId 找到对应实体。chunk 未就绪时返回 entt::null，由调用方重试。
    // M13-T8：stableId 非 0 时使用服务器档案分配的持久 StableId。
    ecs::Entity SpawnNetworkPlayer(float targetX, float targetY, int searchRadius = 24,
                                   std::uint64_t stableId = 0);

    // M13-T8：从玩家档案快照恢复同一 StableId 的实体（保留位置/背包/装备/生命）。
    // 不做“最近可行走格”搜索，保存时的位置应有效；读档后的 chunk 流式加载由调用方负责。
    ecs::Entity RestoreNetworkPlayer(const save::SavedEntity& snapshot, bool resetHealth,
                                     std::string* error = nullptr);

    // M13-T8：死亡后在指定出生点附近复活；chunk 未就绪/位置不可用时返回 null 供调用方重试。
    ecs::Entity RespawnNetworkPlayer(const save::SavedEntity& snapshot, float targetX,
                                     float targetY, int searchRadius, bool resetHealth,
                                     std::string* error = nullptr);

    // 测试/工具：在指定格生成一个游荡者（不等待 chunk；由调用方保证位置有效）。
    // M24-T4：stableId 非 0 时按 StableId 幂等生成，供随机事件/存档去重。
    ecs::Entity SpawnZombieAt(float x, float y,
                              std::uint64_t stableId = 0);
    // M19-F2：POI 守军等需要指定变体时使用；仍由调用方保证位置可走。
    ecs::Entity SpawnZombieAt(float x, float y, ecs::ZombieVariantKind variant,
                              std::uint64_t stableId = 0);

    // 服务器会话清理：销毁玩家/任意实体及物理体；会延迟到下一次 Flush 回收。
    void DespawnEntity(ecs::Entity entity);

    // 演示人口：实体僵尸 + 虚拟群落，数量由 spawner.demo* 配置；读档时不要调用。
    void SpawnDemoPopulation();

    // 清空实体并读取存档实体快照；要求 SaveManager/ComponentRegistry 已初始化。
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
    // M30-T4：取走本帧累积的格挡/弹反/破防/闪避反馈，供表现层消费。
    std::vector<characters::CombatSystem::BlockFeedback> TakeBlockFeedbacks();
    // M30-T5：取走本帧累积的真实命中/破防反馈。
    std::vector<characters::CombatSystem::HitFeedback> TakeHitFeedbacks();
    SettlementNpcSystem* SettlementNpcs();
    FireSystem* Fire();
    AnimalSystem* Animals();

    // M20-F2：当前天气由游戏时间确定性采样，可随时供 HUD/脚本读取。
    WeatherState CurrentWeather() const;
    // M24-T4：寒潮事件临时覆盖天气；只存在运行时，不写存档。
    void SetWeatherOverride(WeatherKind kind, float durationMinutes);
    void ClearWeatherOverride();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::game
