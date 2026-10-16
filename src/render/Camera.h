#pragma once

#include <glm/glm.hpp>

namespace zl::render {

// M2-T5：2D 等距相机。
// - 平移：WASD / 鼠标拖拽（由调用方换算成世界像素增量后 PanBy）；
// - 缩放：离散挡位 0.5x~3x（滚轮切换）；
// - 边界夹取：SetBounds 后中心点被限制在可视范围不越界；
// - 提供 世界像素 ↔ 屏幕像素 换算与 bgfx view/proj 矩阵。
class Camera {
public:
    static constexpr int kZoomLevelCount = 6;
    static constexpr float kZoomLevels[kZoomLevelCount] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
    static constexpr int kDefaultZoomLevel = 2;  // 1.0x

    void SetViewport(int width, int height);
    void SetBounds(const glm::vec2& minPx, const glm::vec2& maxPx);  // 世界像素范围（含留白）
    void ClearBounds() { m_hasBounds = false; }

    void SetCenter(const glm::vec2& centerPx);
    const glm::vec2& Center() const { return m_center; }

    // 相机平移（世界像素增量）
    void PanBy(const glm::vec2& deltaPx);

    // 缩放挡位：steps=+1 放大一级，-1 缩小一级
    void ZoomStep(int steps);
    float Zoom() const { return m_zoom; }
    int ZoomLevelIndex() const { return m_zoomLevel; }

    glm::vec2 ScreenToWorld(const glm::vec2& screenPx) const;
    glm::vec2 WorldToScreen(const glm::vec2& worldPx) const;

    glm::mat4 ViewMatrix() const;
    glm::mat4 ProjMatrix() const;

    // 当前可视世界范围尺寸（像素）
    glm::vec2 VisibleSize() const {
        return {static_cast<float>(m_viewportW) / m_zoom, static_cast<float>(m_viewportH) / m_zoom};
    }

private:
    void ClampCenter();

    glm::vec2 m_center{0.0f, 0.0f};
    float m_zoom = kZoomLevels[kDefaultZoomLevel];
    int m_zoomLevel = kDefaultZoomLevel;
    int m_viewportW = 1280;
    int m_viewportH = 720;
    glm::vec2 m_boundsMin{0.0f, 0.0f};
    glm::vec2 m_boundsMax{0.0f, 0.0f};
    bool m_hasBounds = false;
};

}  // namespace zl::render
