#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zl::core {
class Config;
}

namespace zl::game {

// M19-F2：小型 POI 数据与纯规则。地图布局在 tools/gen_map.py；
// 运行时配置在 config/game.json 的 pois.defs，二者坐标由 Python 数据测试交叉校验。
enum class PoiGuardKind : std::uint8_t {
    Normal = 0,
    Fast = 1,
    Strong = 2,
    Mixed = 3,
    Armored = 4,  // M23-F6-T3：重甲军械守卫
};

struct PoiContainerDef {
    float x = 0.0f;
    float y = 0.0f;
    std::string tableId;
    std::string sprite = "object.crate";
};

struct PoiEnemyDef {
    int count = 0;
    float radius = 8.0f;
    PoiGuardKind kind = PoiGuardKind::Mixed;
    int armoredCount = 0;  // 前 N 个守军替换为重甲变体（不超过 count）
};

struct PoiTaskDef {
    std::string description;
    std::string targetTable;
    float rewardXp = 0.0f;
    std::string rewardItem;
    int rewardCount = 1;
};

struct PoiDef {
    std::string id;
    std::string name;
    int threat = 1;
    float markerX = 0.0f;
    float markerY = 0.0f;
    std::vector<PoiContainerDef> containers;
    PoiEnemyDef enemies;
    PoiTaskDef task;
};

struct PoiConfig {
    bool enabled = false;
    float guideRange = 120.0f;
    std::vector<PoiDef> defs;

    const PoiDef* Find(std::string_view id) const {
        for (const PoiDef& def : defs) {
            if (def.id == id) {
                return &def;
            }
        }
        return nullptr;
    }
};

// 解析并校验 pois 段；结构性错误失败，单条非法定义跳过并告警。
bool LoadPoiConfig(const core::Config& config, PoiConfig* out, std::string* error);

struct PoiMergeStats {
    int filesLoaded = 0;
    int defsAdded = 0;
    int defsSkipped = 0;
};

bool MergePoiFile(const std::filesystem::path& path, PoiConfig* out,
                  PoiMergeStats* stats = nullptr, std::string* error = nullptr);
bool MergePoiJson(std::string_view json, const std::string& source,
                  PoiConfig* out, PoiMergeStats* stats = nullptr,
                  std::string* error = nullptr);

const char* PoiGuardKindName(PoiGuardKind kind);
PoiGuardKind PoiGuardKindForSpawn(const PoiEnemyDef& enemies, int index);
const char* PoiThreatLabel(int threat);

// 玩家到 POI 的方向（格坐标；y 向下为南）。
std::string PoiDirectionLabel(float dx, float dy);

// 当前任务提示；targetSearched 与剩余守军共同决定文案。
std::string PoiTaskText(const PoiDef& def, bool targetSearched, int liveGuards);

// HUD 指引：POI 名称、威胁、方向/距离、任务进度。
std::string PoiGuideText(const PoiDef& def, float dx, float dy, float distance,
                         bool targetSearched, int liveGuards);
const char* PoiGuideTone(const PoiDef& def, bool targetSearched, int liveGuards);

bool IsPoiCompleted(const std::vector<std::string>& completed, std::string_view id);
bool MarkPoiCompleted(std::vector<std::string>* completed, std::string id);

// 任务完成条件：目标物资已搜索，且该 POI 的守军全部清除。
bool ShouldCompletePoiTask(const PoiDef& def, bool targetSearched, int liveGuards);

}  // namespace zl::game
