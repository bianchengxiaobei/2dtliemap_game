#pragma once

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

class ActionPresentationSystem {
public:
    void Update(ecs::EcsWorld& world, float dt) const;
};

}  // namespace zl::characters