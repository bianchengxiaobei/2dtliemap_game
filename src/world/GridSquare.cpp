#include "world/GridSquare.h"

#include "core/Log.h"

namespace zl::world {
namespace {

std::size_t LayerIndex(SquareLayer layer) {
    const auto index = static_cast<std::size_t>(layer);
    ZL_ASSERT(index < kSquareLayerCount, "非法层枚举: {}", static_cast<int>(layer));
    return index;
}

}  // namespace

const std::vector<SquareObject>& GridSquare::Layer(SquareLayer layer) const {
    return m_layers[LayerIndex(layer)];
}

std::vector<SquareObject>& GridSquare::Layer(SquareLayer layer) {
    return m_layers[LayerIndex(layer)];
}

void GridSquare::AddObject(SquareLayer layer, SquareObject object) {
    Layer(layer).push_back(std::move(object));
}

void GridSquare::ClearLayer(SquareLayer layer) {
    Layer(layer).clear();
}

const SquareObject* GridSquare::Floor() const {
    const auto& floorLayer = Layer(SquareLayer::Floor);
    return floorLayer.empty() ? nullptr : &floorLayer.front();
}

void GridSquare::SetFloor(SquareObject floorObject) {
    auto& floorLayer = Layer(SquareLayer::Floor);
    floorLayer.clear();
    floorLayer.push_back(std::move(floorObject));
}

std::size_t GridSquare::ObjectCount() const {
    std::size_t total = 0;
    for (const auto& layer : m_layers) {
        total += layer.size();
    }
    return total;
}

void GridSquare::Clear() {
    for (auto& layer : m_layers) {
        layer.clear();
    }
    m_roomId = -1;
}

}  // namespace zl::world
