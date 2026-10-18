#include "save/BinaryWriter.h"

#include <algorithm>
#include <limits>

namespace zl::save {
namespace {

void PutU16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void PutU32(std::uint8_t* out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu);
    }
}

void PutU64(std::uint8_t* out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu);
    }
}

}  // namespace

bool BinaryWriter::BeginSegment(std::uint32_t magic, std::uint16_t version,
                                std::uint16_t flags) {
    if (!Ok()) {
        return false;
    }
    if (m_segmentOpen) {
        return Fail("上一个段尚未结束");
    }
    if (version == 0) {
        return Fail("段版本不能为 0");
    }

    std::array<std::uint8_t, kSegmentHeaderSize> header{};
    PutU32(header.data(), magic);
    PutU16(header.data() + 4, version);
    PutU16(header.data() + 6, flags);
    PutU64(header.data() + 8, 0);

    m_segmentHeaderOffset = m_data.size();
    if (!WriteRaw(header.data(), header.size())) {
        return false;
    }
    m_segmentPayloadOffset = m_data.size();
    m_segmentOpen = true;
    return true;
}

bool BinaryWriter::EndSegment() {
    if (!Ok()) {
        return false;
    }
    if (!m_segmentOpen) {
        return Fail("没有打开的段");
    }

    const std::uint64_t payloadSize = m_data.size() - m_segmentPayloadOffset;
    if (payloadSize > kMaxSegmentPayloadBytes) {
        return Fail("段数据超过大小上限");
    }
    if (m_segmentHeaderOffset + kSegmentHeaderSize > m_data.size()) {
        return Fail("段头已损坏");
    }

    PutU64(m_data.data() + m_segmentHeaderOffset + 8, payloadSize);
    m_segmentOpen = false;
    return true;
}

bool BinaryWriter::WriteBytes(const void* data, std::size_t size) {
    if (!EnsureActive()) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        return Fail("写入数据指针为空");
    }

    const std::uint64_t used = m_data.size() - m_segmentPayloadOffset;
    if (used > kMaxSegmentPayloadBytes ||
        size > kMaxSegmentPayloadBytes - used) {
        return Fail("段数据超过大小上限");
    }
    return WriteRaw(data, size);
}

bool BinaryWriter::WriteString(std::string_view value, std::uint32_t maxBytes) {
    if (!EnsureActive()) {
        return false;
    }
    const std::uint32_t limit = std::min(maxBytes, kMaxStringBytes);
    if (value.size() > limit) {
        return Fail("字符串长度 " + std::to_string(value.size()) + " 超过上限 " +
                    std::to_string(limit));
    }
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Fail("字符串长度超过 32 位上限");
    }
    if (!Write(static_cast<std::uint32_t>(value.size()))) {
        return false;
    }
    return WriteBytes(value.data(), value.size());
}

void BinaryWriter::Clear() {
    m_data.clear();
    m_error.clear();
    m_segmentHeaderOffset = 0;
    m_segmentPayloadOffset = 0;
    m_segmentOpen = false;
}

bool BinaryWriter::EnsureActive() {
    if (!Ok()) {
        return false;
    }
    if (!m_segmentOpen) {
        return Fail("没有打开的段");
    }
    return true;
}

bool BinaryWriter::WriteRaw(const void* data, std::size_t size) {
    if (size == 0) {
        return true;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    try {
        m_data.insert(m_data.end(), bytes, bytes + size);
    } catch (const std::exception&) {
        return Fail("写入缓冲区失败（内存不足）");
    }
    return true;
}

bool BinaryWriter::Fail(std::string message) {
    if (m_error.empty()) {
        m_error = "偏移 " + std::to_string(m_data.size()) + ": " + std::move(message);
    }
    return false;
}

}  // namespace zl::save
