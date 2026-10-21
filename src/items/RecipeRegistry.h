#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "items/ItemRegistry.h"
#include "items/RecipeDef.h"

namespace zl::items {

// 配方定义注册表（recipes-v2，兼容 recipes-v1）：启动时加载并与 ItemRegistry
// 交叉校验。v2 增加可选的 tools（工具不消耗）与 time（制作秒数）；任一错误整体
// 不生效，加载失败不阻断启动，游戏以空配方表继续。
class RecipeRegistry {
public:
    bool LoadFile(const std::filesystem::path& path, const ItemRegistry& items,
                  std::string* error = nullptr);

    bool LoadFromString(std::string_view text, const std::string& source,
                        const ItemRegistry& items, std::string* error = nullptr);

    const RecipeDef* Find(std::string_view id) const;
    const RecipeDef* At(std::size_t index) const;
    const std::vector<RecipeDef>& All() const { return m_defs; }
    std::size_t Size() const { return m_defs.size(); }

private:
    std::vector<RecipeDef> m_defs;
    std::unordered_map<std::string, std::size_t> m_byId;
};

}  // namespace zl::items
