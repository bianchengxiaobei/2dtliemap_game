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

// 配方定义注册表（recipes-v4，兼容 v1~v3）：启动时加载并与 ItemRegistry 交叉校验。
// v2 增加可选 tools/time；v3 增加 station；v4 增加 category/cookware/fuel。
// 任一错误整体不生效，加载失败不阻断启动，游戏以空配方表继续。
class RecipeRegistry {
public:
    bool LoadFile(const std::filesystem::path& path, const ItemRegistry& items,
                  std::string* error = nullptr);

    bool LoadFromString(std::string_view text, const std::string& source,
                        const ItemRegistry& items, std::string* error = nullptr);

    bool MergeFromString(std::string_view text, const std::string& source,
                         const ItemRegistry& items, std::size_t* added = nullptr,
                         std::string* error = nullptr);

    const RecipeDef* Find(std::string_view id) const;
    const RecipeDef* At(std::size_t index) const;
    const std::vector<RecipeDef>& All() const { return m_defs; }
    std::size_t Size() const { return m_defs.size(); }

private:
    std::vector<RecipeDef> m_defs;
    std::unordered_map<std::string, std::size_t> m_byId;
    std::string m_sourceText;
};

}  // namespace zl::items
