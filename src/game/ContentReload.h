#pragma once

#include <memory>
#include <string>

#include "game/Animals.h"
#include "game/Crops.h"
#include "game/Forage.h"
#include "game/ModContent.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"

namespace zl::core {
class Config;
}

namespace zl::game {

struct ContentBundle {
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    ModContentResult modContent;
    items::LootTableRegistry lootTables;
    ForageRegistry forage;
    AnimalRegistry animals;
    CropRegistry crops;
    PoiConfig poiConfig;
    MissionConfig missionConfig;
    std::string missionSourceJson;
    SettlementConfig settlements;
};

struct ContentBundleResult {
    std::unique_ptr<ContentBundle> bundle;
    std::string error;

    bool Ok() const { return bundle != nullptr; }
};

ContentBundleResult LoadContentBundle(const core::Config& config);

inline constexpr char kContentReloadNetworkBlockedMessage[] =
    "联机中不支持 mod 内容热重载；请重启服务器和所有客户端后重连";
inline constexpr char kContentReloadDynamicBlockedMessage[] =
    "脚本已动态注册内容，需重启游戏后应用 mod 内容变更";
inline constexpr char kDynamicContentSinglePlayerOnlyMessage[] =
    "动态内容仅支持单机；请移除动态注册后重启";

// 单机且无脚本动态内容时允许热重载；联机必须重启服务器和全部客户端重连。
std::string ContentReloadBlockedReason(bool allowDynamicContent,
                                       bool networkMode,
                                       int dynamicContentCount);

// 脚本动态内容只允许在单机注册；联机入口会直接拒绝连接/主机启动。
std::string DynamicContentBlockedReason(bool allowDynamicContent);

}  // namespace zl::game
