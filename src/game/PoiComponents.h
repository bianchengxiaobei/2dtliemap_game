#pragma once

#include <string>
#include <vector>

namespace zl::ecs {

// M19-F2：玩家已完成的 POI 任务 id。旧档缺少组件时按全部未完成处理。
struct PoiTasks {
    std::vector<std::string> completed;
};

}  // namespace zl::ecs
