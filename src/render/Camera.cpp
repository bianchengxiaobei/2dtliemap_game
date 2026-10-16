#include "render/Camera.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace zl::render {

void Camera::SetViewport(int width, int height) {
    m_viewportW = std::max(width, 1);
    m_viewportH = std::max(height, 1);
    ClampCenter();
}

void Camera::SetBounds(const glm::vec2& minPx, const glm::vec2& maxPx) {
    m_boundsMin = minPx;
    m_boundsMax = maxPx;
    m_hasBounds = true;
    ClampCenter();
}

void Camera::SetCenter(const glm::vec2& centerPx) {
    m_center = centerPx;
    ClampCenter();
}

void Camera::PanBy(const glm::vec2& deltaPx) {
    m_center += deltaPx;
    ClampCenter();
}

void Camera::ZoomStep(int steps) {
    if (steps == 0) {
        return;
    }
    const int next = std::clamp(m_zoomLevel + steps, 0, kZoomLevelCount - 1);
    if (next == m_zoomLevel) {
        return;
    }
    // 保持屏幕中心不动：中心点本就不随缩放移动（世界像素坐标），仅夹取范围变化
    m_zoomLevel = next;
    m_zoom = kZoomLevels[m_zoomLevel];
    ClampCenter();
}

void Camera::ClampCenter() {
    if (!m_hasBounds) {
        return;
    }
    const glm::vec2 half = VisibleSize() * 0.5f;
    // 可视范围比边界还大时：贴中
    const float minX = m_boundsMin.x + half.x;
    const float maxX = m_boundsMax.x - half.x;
    if (minX <= maxX) {
        m_center.x = std::clamp(m_center.x, minX, maxX);
    } else {
        m_center.x = (m_boundsMin.x + m_boundsMax.x) * 0.5f;
    }
    const float minY = m_boundsMin.y + half.y;
    const float maxY = m_boundsMax.y - half.y;
    if (minY <= maxY) {
        m_center.y = std::clamp(m_center.y, minY, maxY);
    } else {
        m_center.y = (m_boundsMin.y + m_boundsMax.y) * 0.5f;
    }
}

glm::vec2 Camera::ScreenToWorld(const glm::vec2& screenPx) const {
    const glm::vec2 viewportCenter(static_cast<float>(m_viewportW) * 0.5f,
                                   static_cast<float>(m_viewportH) * 0.5f);
    return m_center + (screenPx - viewportCenter) / m_zoom;
}

glm::vec2 Camera::WorldToScreen(const glm::vec2& worldPx) const {
    const glm::vec2 viewportCenter(static_cast<float>(m_viewportW) * 0.5f,
                                   static_cast<float>(m_viewportH) * 0.5f);
    return (worldPx - m_center) * m_zoom + viewportCenter;
}

glm::mat4 Camera::ViewMatrix() const {
    glm::mat4 view(1.0f);
    view[3][0] = -m_center.x;
    view[3][1] = -m_center.y;
    return view;
}

glm::mat4 Camera::ProjMatrix() const {
    const float halfW = static_cast<float>(m_viewportW) * 0.5f / m_zoom;
    const float halfH = static_cast<float>(m_viewportH) * 0.5f / m_zoom;
    // 相机平移只由 ViewMatrix 负责：正交投影必须以「相机为原点」，
    // 否则相机偏移被应用两次（世界是周期性格子时会掩盖该问题）。
    // 注意：bottom/top 互换实现 Y 轴翻转（世界 Y 向下）。
    return glm::ortho(-halfW, halfW, halfH, -halfH, -1000.0f, 1000.0f);
}

}  // namespace zl::render
