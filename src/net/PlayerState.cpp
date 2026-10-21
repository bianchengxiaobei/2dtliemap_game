#include "net/PlayerState.h"

#include <bit>
#include <cmath>
#include <string_view>
#include <utility>

namespace zl::net {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool ValidItem(const PlayerStateItem& item, std::string* reason) {
    if (item.defId.empty() ||
        item.defId.size() > kMaxPlayerStateStringBytes) {
        SetError(reason, "玩家状态物品 defId 非法");
        return false;
    }
    if (item.count < 1 || item.count > 9999) {
        SetError(reason, "玩家状态物品数量非法");
        return false;
    }
    if (!std::isfinite(item.condition) || item.condition < 0.0f ||
        item.condition > 1.0f) {
        SetError(reason, "玩家状态物品耐久非法");
        return false;
    }
    return true;
}

class Writer {
public:
    void U8(std::uint8_t value) { m_bytes.push_back(value); }
    void U16(std::uint16_t value) {
        U8(static_cast<std::uint8_t>(value & 0xFFu));
        U8(static_cast<std::uint8_t>((value >> 8U) & 0xFFu));
    }
    void U32(std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            U8(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
        }
    }
    void U64(std::uint64_t value) {
        for (std::size_t i = 0; i < 8; ++i) {
            U8(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
        }
    }
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }
    bool String(std::string_view value, std::size_t maxBytes, bool allowEmpty,
                std::string* error) {
        if (value.size() > maxBytes || value.size() > 0xFFFFu ||
            (!allowEmpty && value.empty())) {
            SetError(error, "玩家状态字符串长度非法");
            return false;
        }
        U16(static_cast<std::uint16_t>(value.size()));
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
        return true;
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U8(std::uint8_t* value) {
        if (!Has(1)) {
            return false;
        }
        *value = m_bytes[m_offset++];
        return true;
    }
    bool U16(std::uint16_t* value) {
        if (!Has(2)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint16_t>(m_bytes[m_offset + 1]) << 8U);
        m_offset += 2;
        return true;
    }
    bool U32(std::uint32_t* value) {
        if (!Has(4)) {
            return false;
        }
        *value = static_cast<std::uint32_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 1]) << 8U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 2]) << 16U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 3]) << 24U);
        m_offset += 4;
        return true;
    }
    bool U64(std::uint64_t* value) {
        if (!Has(8)) {
            return false;
        }
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            result |= static_cast<std::uint64_t>(m_bytes[m_offset + i]) << (i * 8U);
        }
        *value = result;
        m_offset += 8;
        return true;
    }
    bool F32(float* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = std::bit_cast<float>(raw);
        return true;
    }
    bool I32(std::int32_t* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = static_cast<std::int32_t>(raw);
        return true;
    }
    bool String(std::string* value, std::size_t maxBytes, bool allowEmpty) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || (!allowEmpty && size == 0) ||
            !Has(size)) {
            return false;
        }
        value->assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                      m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size));
        m_offset += size;
        return true;
    }
    bool AtEnd() const { return m_offset == m_bytes.size(); }

private:
    bool Has(std::size_t count) const {
        return count <= m_bytes.size() - m_offset;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

void WriteItem(Writer& writer, const PlayerStateItem& item, std::string* error,
               bool* ok) {
    if (!*ok) {
        return;
    }
    std::string reason;
    if (!ValidItem(item, &reason)) {
        SetError(error, reason);
        *ok = false;
        return;
    }
    if (!writer.String(item.defId, kMaxPlayerStateStringBytes, false, error)) {
        *ok = false;
        return;
    }
    writer.U16(static_cast<std::uint16_t>(item.count));
    writer.F32(item.condition);
}

bool ReadItem(Reader& reader, PlayerStateItem* item) {
    if (!reader.String(&item->defId, kMaxPlayerStateStringBytes, false)) {
        return false;
    }
    std::uint16_t count = 0;
    if (!reader.U16(&count) || !reader.F32(&item->condition)) {
        return false;
    }
    item->count = static_cast<int>(count);
    std::string reason;
    return ValidItem(*item, &reason);
}

}  // namespace

std::vector<std::uint8_t> EncodePlayerState(const PlayerState& state,
                                            std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (state.playerStableId == 0) {
        SetError(error, "玩家状态缺少 playerStableId");
        return {};
    }
    if (state.bag.size() > kMaxPlayerStateBagItems) {
        SetError(error, "玩家状态背包槽过多");
        return {};
    }
    if (state.knownRecipes.size() > kMaxPlayerStateKnownRecipes) {
        SetError(error, "玩家状态已学配方过多");
        return {};
    }
    if (state.craftQueue.size() > kMaxPlayerStateCraftQueue) {
        SetError(error, "玩家状态制作队列过长");
        return {};
    }
    if (!std::isfinite(state.capacityKg) || state.capacityKg <= 0.0f ||
        state.capacityKg > 1'000'000.0f) {
        SetError(error, "玩家状态背包容量非法");
        return {};
    }
    if (!std::isfinite(state.health) || !std::isfinite(state.healthMax) ||
        !std::isfinite(state.respawnSeconds) || state.healthMax <= 0.0f ||
        state.health < 0.0f || state.health > state.healthMax ||
        state.respawnSeconds < 0.0f || state.respawnSeconds > 86'400.0f ||
        (!state.alive && state.health > 0.0f)) {
        SetError(error, "玩家状态生命字段非法");
        return {};
    }
    if (!std::isfinite(state.craftingElapsed) ||
        !std::isfinite(state.craftingDuration) ||
        !std::isfinite(state.craftingToolEfficiency) ||
        state.craftingElapsed < 0.0f || state.craftingDuration < 0.0f ||
        state.craftingToolEfficiency < 0.0f ||
        state.craftingToolEfficiency > 1.0f || state.batchTotal < 0 ||
        state.batchTotal > 99 || state.batchCompleted < 0 ||
        state.batchCompleted > state.batchTotal) {
        SetError(error, "玩家状态制作字段非法");
        return {};
    }
    if (state.craftingActive &&
        (state.craftingRecipeId.empty() || state.craftingDuration <= 0.0f ||
         state.craftingElapsed > state.craftingDuration + 1e-4f ||
         state.batchTotal < 1)) {
        SetError(error, "玩家状态活跃制作字段非法");
        return {};
    }
    if (state.statusTone > 2 || state.status.size() > kMaxPlayerStateStatusBytes) {
        SetError(error, "玩家状态提示字段非法");
        return {};
    }

    Writer writer;
    writer.U32(kPlayerStateMagic);
    writer.U16(kPlayerStateVersion);
    writer.U16(0);
    writer.U64(state.playerStableId);
    writer.U64(state.serverTick);
    writer.F32(state.capacityKg);
    writer.F32(state.health);
    writer.F32(state.healthMax);
    writer.U8(state.alive ? 1 : 0);
    writer.F32(state.respawnSeconds);

    bool ok = true;
    writer.U16(static_cast<std::uint16_t>(state.bag.size()));
    for (const PlayerStateItem& item : state.bag) {
        WriteItem(writer, item, error, &ok);
    }
    if (!ok) {
        return {};
    }

    for (const PlayerStateEquipment& slot : state.equipment) {
        writer.U8(slot.present ? 1 : 0);
        if (slot.present) {
            WriteItem(writer, slot.item, error, &ok);
            if (!ok) {
                return {};
            }
        }
    }

    writer.U16(static_cast<std::uint16_t>(state.knownRecipes.size()));
    for (const std::string& recipeId : state.knownRecipes) {
        if (!writer.String(recipeId, kMaxPlayerStateStringBytes, false, error)) {
            return {};
        }
    }

    writer.U8(state.craftingActive ? 1 : 0);
    if (state.craftingActive) {
        if (!writer.String(state.craftingRecipeId, kMaxPlayerStateStringBytes,
                           false, error)) {
            return {};
        }
        writer.F32(state.craftingElapsed);
        writer.F32(state.craftingDuration);
        writer.F32(state.craftingToolEfficiency);
        writer.I32(state.batchTotal);
        writer.I32(state.batchCompleted);
    }
    writer.U16(static_cast<std::uint16_t>(state.craftQueue.size()));
    for (const PlayerStateCraftQueue& entry : state.craftQueue) {
        if (entry.count < 1 || entry.count > 99 ||
            !writer.String(entry.recipeId, kMaxPlayerStateStringBytes, false,
                           error)) {
            if (error != nullptr && error->empty()) {
                *error = "玩家状态制作队列项非法";
            }
            return {};
        }
        writer.I32(entry.count);
    }

    writer.U32(state.statusSequence);
    writer.U8(state.statusTone);
    if (!writer.String(state.status, kMaxPlayerStateStatusBytes, true, error)) {
        return {};
    }
    if (writer.Bytes().size() > kMaxPlayerStateBytes) {
        SetError(error, "玩家状态超过单包字节上限");
        return {};
    }
    return writer.Bytes();
}

bool DecodePlayerState(std::span<const std::uint8_t> bytes, PlayerState* state,
                       std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (state == nullptr) {
        SetError(error, "玩家状态输出为空");
        return false;
    }
    if (bytes.size() < kPlayerStateHeaderSize || bytes.size() > kMaxPlayerStateBytes) {
        SetError(error, "玩家状态消息长度非法");
        return false;
    }
    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    if (!reader.U32(&magic) || magic != kPlayerStateMagic) {
        SetError(error, "玩家状态 magic 不匹配");
        return false;
    }
    if (!reader.U16(&version) || version != kPlayerStateVersion) {
        SetError(error, "玩家状态协议版本不支持");
        return false;
    }
    if (!reader.U16(&reserved) || reserved != 0) {
        SetError(error, "玩家状态保留字段非 0");
        return false;
    }

    PlayerState decoded;
    if (!reader.U64(&decoded.playerStableId) || decoded.playerStableId == 0 ||
        !reader.U64(&decoded.serverTick) || !reader.F32(&decoded.capacityKg) ||
        !std::isfinite(decoded.capacityKg) || decoded.capacityKg <= 0.0f ||
        decoded.capacityKg > 1'000'000.0f || !reader.F32(&decoded.health) ||
        !reader.F32(&decoded.healthMax)) {
        SetError(error, "玩家状态基础字段非法");
        return false;
    }
    std::uint8_t alive = 0;
    if (!reader.U8(&alive) || alive > 1 || !reader.F32(&decoded.respawnSeconds)) {
        SetError(error, "玩家状态存活/复活字段非法");
        return false;
    }
    decoded.alive = alive != 0;
    if (!std::isfinite(decoded.health) || !std::isfinite(decoded.healthMax) ||
        !std::isfinite(decoded.respawnSeconds) || decoded.healthMax <= 0.0f ||
        decoded.health < 0.0f || decoded.health > decoded.healthMax ||
        decoded.respawnSeconds < 0.0f || decoded.respawnSeconds > 86'400.0f ||
        (!decoded.alive && decoded.health > 0.0f)) {
        SetError(error, "玩家状态生命字段非法");
        return false;
    }

    std::uint16_t bagCount = 0;
    if (!reader.U16(&bagCount) || bagCount > kMaxPlayerStateBagItems) {
        SetError(error, "玩家状态背包数量非法");
        return false;
    }
    decoded.bag.resize(bagCount);
    for (PlayerStateItem& item : decoded.bag) {
        if (!ReadItem(reader, &item)) {
            SetError(error, "玩家状态背包物品非法");
            return false;
        }
    }

    for (PlayerStateEquipment& slot : decoded.equipment) {
        std::uint8_t present = 0;
        if (!reader.U8(&present) || present > 1) {
            SetError(error, "玩家状态装备槽非法");
            return false;
        }
        slot.present = present != 0;
        if (slot.present && !ReadItem(reader, &slot.item)) {
            SetError(error, "玩家状态装备物品非法");
            return false;
        }
    }

    std::uint16_t knownCount = 0;
    if (!reader.U16(&knownCount) || knownCount > kMaxPlayerStateKnownRecipes) {
        SetError(error, "玩家状态配方数量非法");
        return false;
    }
    decoded.knownRecipes.resize(knownCount);
    for (std::string& recipeId : decoded.knownRecipes) {
        if (!reader.String(&recipeId, kMaxPlayerStateStringBytes, false)) {
            SetError(error, "玩家状态配方 id 非法");
            return false;
        }
    }

    std::uint8_t craftingActive = 0;
    if (!reader.U8(&craftingActive) || craftingActive > 1) {
        SetError(error, "玩家状态制作标记非法");
        return false;
    }
    decoded.craftingActive = craftingActive != 0;
    if (decoded.craftingActive) {
        std::int32_t batchTotal = 0;
        std::int32_t batchCompleted = 0;
        if (!reader.String(&decoded.craftingRecipeId, kMaxPlayerStateStringBytes,
                           false) ||
            !reader.F32(&decoded.craftingElapsed) ||
            !reader.F32(&decoded.craftingDuration) ||
            !reader.F32(&decoded.craftingToolEfficiency) ||
            !reader.I32(&batchTotal) || !reader.I32(&batchCompleted)) {
            SetError(error, "玩家状态制作字段非法");
            return false;
        }
        decoded.batchTotal = batchTotal;
        decoded.batchCompleted = batchCompleted;
        if (!std::isfinite(decoded.craftingElapsed) ||
            !std::isfinite(decoded.craftingDuration) ||
            !std::isfinite(decoded.craftingToolEfficiency) ||
            decoded.craftingElapsed < 0.0f || decoded.craftingDuration <= 0.0f ||
            decoded.craftingElapsed > decoded.craftingDuration + 1e-4f ||
            decoded.craftingToolEfficiency < 0.0f ||
            decoded.craftingToolEfficiency > 1.0f || decoded.batchTotal < 1 ||
            decoded.batchTotal > 99 || decoded.batchCompleted < 0 ||
            decoded.batchCompleted > decoded.batchTotal) {
            SetError(error, "玩家状态制作数值非法");
            return false;
        }
    }

    std::uint16_t queueCount = 0;
    if (!reader.U16(&queueCount) || queueCount > kMaxPlayerStateCraftQueue) {
        SetError(error, "玩家状态制作队列数量非法");
        return false;
    }
    decoded.craftQueue.resize(queueCount);
    for (PlayerStateCraftQueue& entry : decoded.craftQueue) {
        std::int32_t count = 0;
        if (!reader.String(&entry.recipeId, kMaxPlayerStateStringBytes, false) ||
            !reader.I32(&count) || count < 1 || count > 99) {
            SetError(error, "玩家状态制作队列项非法");
            return false;
        }
        entry.count = count;
    }

    std::uint8_t tone = 0;
    if (!reader.U32(&decoded.statusSequence) || !reader.U8(&tone) || tone > 2 ||
        !reader.String(&decoded.status, kMaxPlayerStateStatusBytes, true) ||
        !reader.AtEnd()) {
        SetError(error, "玩家状态提示字段非法或存在尾随数据");
        return false;
    }
    decoded.statusTone = tone;
    *state = std::move(decoded);
    return true;
}

}  // namespace zl::net
