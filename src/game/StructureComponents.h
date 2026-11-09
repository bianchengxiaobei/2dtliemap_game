#pragma once

#include <cstdint>

namespace zl::ecs {

// X-F1 阶段 2b：玩家建造的设施实体归属。地图结构把 ownerStableId 存在
// SquareObject 中；仓库/农田/发电机/冷藏柜/陷阱等 ECS 实体用本组件保存，
// 并随实体存档恢复。0 = 无主/旧档设施。
struct StructureOwner {
    std::uint64_t ownerStableId = 0;
};

}  // namespace zl::ecs
