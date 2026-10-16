#pragma once

#include <cstdint>
#include <vector>

namespace zl::ai {

// M5-T5：虚拟僵尸群落 —— 离屏僵尸的"统计表示"，**不入 ECS registry**。
// 一个群落 = 一群共享统计属性的僵尸（中心/数量/散布/漂移速度）；
// 每 tick 只做群体级聚合推进（O(群落数)），不产生实体、不跑感知/AI/物理。
// M5-T7 的 SpawnLodSystem 将消费它：焦点接近时 TakeZombies 扣减 →
// 实体化入 registry；远离时回收批量并回填群落。
struct VirtualHorde {
    std::uint64_t id = 0;
    float centerX = 0.0f;   // 群落中心（世界格）
    float centerY = 0.0f;
    float spread = 6.0f;    // 分布半径（格；统计展示用，个体不单独模拟）
    int count = 0;          // 群落内僵尸数
    float speed = 1.2f;     // 漂移速度上限（格/秒）
};

class VirtualHordeStore {
public:
    struct Stats {
        int hordes = 0;             // 群落数
        int zombies = 0;            // 虚拟僵尸总数
        float nearestDist = -1.0f;  // 距参考点最近的群落距离（格；无群落 = -1）
    };

    std::uint64_t AddHorde(float cx, float cy, int count, float spread = 6.0f);
    bool RemoveHorde(std::uint64_t id);
    // 实体化扣减（T7 用）：返回实际扣减数；扣空自动移除群落
    int TakeZombies(std::uint64_t id, int n);

    // 聚合推进：所有群落朝吸引点漂移（群体级，无个体模拟），到 stopDistance 内停住
    void Update(float dt, float attractX, float attractY, float stopDistance);

    Stats GetStats(float fromX, float fromY) const;
    const std::vector<VirtualHorde>& Hordes() const { return m_hordes; }
    void Clear();

private:
    std::vector<VirtualHorde> m_hordes;
    std::uint64_t m_nextId = 1;
};

}  // namespace zl::ai
