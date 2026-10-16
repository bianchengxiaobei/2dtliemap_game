#pragma once

#include <cstddef>

#include <entt/entt.hpp>

namespace zl::ecs {

using Entity = entt::entity;

// entt::entity 在 unordered 容器里的哈希（scoped enum 没有 std::hash 特化）
struct EntityHash {
    std::size_t operator()(Entity e) const noexcept { return static_cast<std::size_t>(e); }
};

}  // namespace zl::ecs
