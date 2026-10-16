#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "items/ItemDef.h"

namespace zl::items {

// 物品定义注册表（M6-T1）：启动时加载 JSON v1（根对象 format="items-v1"）。
// 严格校验：未知字段 / 类型错误 / 取值越界 / 重复 id 均报错，
// 错误信息定位到 行（语法错误精确到列）；任一错误整体不加载（不部分生效）。
// 加载失败不阻断启动：游戏以空注册表继续（调用方决定是否降级）。
class ItemRegistry {
public:
    // 从文件加载；失败返回 false 并写 *error（同时打日志），注册表保持原内容
    bool LoadFile(const std::filesystem::path& path, std::string* error = nullptr);

    // 从字符串加载（供测试与后续 mod 热重载）；sourceName 作为错误前缀
    bool LoadFromString(std::string_view text, const std::string& sourceName,
                        std::string* error = nullptr);

    // 按 id 查定义；不存在返回 nullptr（指针在下次成功加载前稳定）
    const ItemDef* Find(std::string_view id) const;

    const std::vector<ItemDef>& All() const { return m_defs; }
    std::size_t Size() const { return m_defs.size(); }

private:
    std::vector<ItemDef> m_defs;
    std::unordered_map<std::string, std::size_t> m_byId;
};

}  // namespace zl::items
