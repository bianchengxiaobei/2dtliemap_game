#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Constants.h"

namespace zl::world {

// M3-T1：格内分层（docs/00 §4.2），枚举值即绘制顺序（小→大先画）。
enum class SquareLayer : int {
    Floor = 0,      // 地面/地形（每格必有一块）
    Wall = 1,       // 墙
    Furniture = 2,  // 家具/大型物体
    Item = 3,       // 容器与地面物品
    Mark = 4,       // 临时标记（血迹/脚印，不随地图母文件存档）
};
inline constexpr int kSquareLayerCount = 5;

// 格内静态对象：M3 只含"渲染引用"（逻辑名由 AssetProvider 解析）；
// M4 起在此扩展碰撞/物品/脚本句柄等（保持纯数据）。
struct SquareObject {
    std::string sprite;   // 逻辑名，如 "tile.floor.grass"、"wall.brick"
    int16_t offsetX = 0;  // 相对格中心（地面接触点）的附加像素偏移
    int16_t offsetY = 0;  // 供大尺寸精灵微调锚点，默认 0
    // 阻挡移动（M4 起）：物理静态碰撞体（M4-T3）与 A* passability（M4-T6）共用；
    // 由地图数据显式标注（floor/草丛等装饰不标）
    bool blocksMovement = false;
};

// 单格。动态实体（角色/僵尸/抛掷物）不挂在格上，由 ECS 统一管理（docs/00 §4.2）。
class GridSquare {
public:
    GridSquare() = default;
    GridSquare(int x, int y, int z)
        : m_x(x), m_y(y), m_z(static_cast<int8_t>(z)) {}

    int X() const { return m_x; }
    int Y() const { return m_y; }
    int Z() const { return m_z; }
    void SetCoords(int x, int y, int z) {
        m_x = x;
        m_y = y;
        m_z = static_cast<int8_t>(z);
    }

    // ---- 分层对象 ----
    const std::vector<SquareObject>& Layer(SquareLayer layer) const;
    std::vector<SquareObject>& Layer(SquareLayer layer);

    void AddObject(SquareLayer layer, SquareObject object);
    void ClearLayer(SquareLayer layer);

    // 地面便捷操作（99% 的格都有地面；重复 Set 为覆盖语义，便于 chunk 重载）
    bool HasFloor() const { return !Layer(SquareLayer::Floor).empty(); }
    const SquareObject* Floor() const;  // 无地面返回 nullptr
    void SetFloor(SquareObject floorObject);

    // ---- 汇总 ----
    std::size_t ObjectCount() const;  // 全部层对象总数
    bool IsEmpty() const { return ObjectCount() == 0; }
    void Clear();  // 清空全部层（chunk 卸载复用）

    // 房间 ID（M3-T2 生成器分配；-1 = 室外）
    int RoomId() const { return m_roomId; }
    void SetRoomId(int roomId) { m_roomId = static_cast<int16_t>(roomId); }

private:
    std::array<std::vector<SquareObject>, kSquareLayerCount> m_layers;
    int32_t m_x = 0;
    int32_t m_y = 0;
    int8_t m_z = 0;         // [0, kMaxZ)
    int16_t m_roomId = -1;  // -1 = 室外；房间数上限 32767（对齐 PZ roomID）
};

}  // namespace zl::world
