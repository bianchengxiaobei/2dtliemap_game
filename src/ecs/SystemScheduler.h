#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zl::ecs {

class EcsWorld;

// M4-T1：系统调度器（docs/02 §5.6）。
//
// 规则：
//  - **更新顺序 = 注册顺序**；全部系统在一处集中注册（GameApp::Init），
//    依赖关系写在注册点（docs/00 §6.1 固定链）：
//      InputSystem → AiSystem → PathFollowSystem → MovementSystem → PhysicsSyncSystem
//      → PerceptionSystem → CombatSystem → SpawnLodSystem → RenderCollectSystem
//  - 每个系统签名统一 `void Update(EcsWorld& world, float dt)`；实现类放各自模块
//    （characters/systems/…），注册处用 lambda 包住类实例的 Update。
//  - 每个系统记录 lastMs / avgMs（指数滑动平均），供 perf 日志与调试开关。
class SystemScheduler {
public:
    using SystemFn = std::function<void(EcsWorld&, float)>;

    // 注册（名字重复会告警并以后注册者为准，顺序不动）
    void AddSystem(std::string name, SystemFn fn);

    // 按名字开关（调试/性能对比用；顺序不变）
    void SetEnabled(std::string_view name, bool enabled);
    bool IsEnabled(std::string_view name) const;

    // 依注册顺序执行；world + dt 原样传入
    void Run(EcsWorld& world, float dt);

    void Clear();

    struct Entry {
        std::string name;
        SystemFn fn;
        bool enabled = true;
        double lastMs = 0.0;  // 上帧耗时
        double avgMs = 0.0;   // 指数滑动平均（0.9/0.1）
    };
    const std::vector<Entry>& Entries() const { return m_entries; }
    std::size_t Count() const { return m_entries.size(); }

private:
    std::vector<Entry> m_entries;
    std::unordered_map<std::string, std::size_t> m_index;  // name → entries 下标
};

}  // namespace zl::ecs
