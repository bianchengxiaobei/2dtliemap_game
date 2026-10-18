#include "save/BinaryReader.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace zl::save {
namespace {

std::uint16_t GetU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t GetU32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(data[i]) << (i * 8);
    }
    return value;
}

std::uint64_t GetU64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
    }
    return value;
}

}  // namespace

bool BinaryReader::BeginSegment(SegmentHeader& header) {
    if (!Ok()) {
        return false;
    }
    if (m_segmentOpen) {
        return Fail("上一个段尚未结束");
    }
    if (m_cursor >= m_data.size()) {
        return false;  // 正常读完，不设置错误
    }

    std::array<std::uint8_t, kSegmentHeaderSize> raw{};
    if (!ReadRaw(raw.data(), raw.size())) {
        return false;
    }

    SegmentHeader parsed;
    parsed.magic = GetU32(raw.data());
    parsed.version = GetU16(raw.data() + 4);
    parsed.flags = GetU16(raw.data() + 6);
    parsed.payloadSize = GetU64(raw.data() + 8);
    header = parsed;
    m_header = parsed;

    if (parsed.version == 0) {
        return Fail("段版本 0 无效");
    }
    if (parsed.payloadSize > kMaxSegmentPayloadBytes) {
        return Fail("段数据长度 " + std::to_string(parsed.payloadSize) +
                    " 超过上限");
    }
    if (parsed.payloadSize > m_data.size() - m_cursor) {
        return Fail("段数据截断: 需要 " + std::to_string(parsed.payloadSize) +
                    " 字节，剩余 " + std::to_string(m_data.size() - m_cursor));
    }

    m_payloadOffset = m_cursor;
    m_segmentEnd = m_cursor + static_cast<std::size_t>(parsed.payloadSize);
    m_segmentOpen = true;
    m_haveSegment = true;
    return true;
}

bool BinaryReader::BeginSegment(SegmentHeader& header,
                                std::uint32_t expectedMagic,
                                std::uint16_t expectedVersion) {
    return BeginSegment(header, expectedMagic, expectedVersion, expectedVersion);
}

bool BinaryReader::BeginSegment(SegmentHeader& header,
                                std::uint32_t expectedMagic,
                                std::uint16_t minVersion,
                                std::uint16_t maxVersion) {
    if (!BeginSegment(header)) {
        return false;
    }
    if (header.magic != expectedMagic) {
        return Fail("段标识不匹配: 读取到 \"" + MagicToString(header.magic) +
                    "\"，期望 \"" + MagicToString(expectedMagic) + "\"");
    }
    if (header.version < minVersion || header.version > maxVersion) {
        std::string expected;
        if (minVersion == maxVersion) {
            expected = std::to_string(minVersion);
        } else {
            expected = std::to_string(minVersion) + "–" +
                       std::to_string(maxVersion);
        }
        return Fail("段版本不兼容: 读取到 " + std::to_string(header.version) +
                    "，支持版本 " + expected);
    }
    return true;
}

bool BinaryReader::BeginSegment(std::uint32_t expectedMagic,
                                std::uint16_t expectedVersion) {
    return BeginSegment(m_header, expectedMagic, expectedVersion);
}

bool BinaryReader::EndSegment() {
    if (!EnsureActive()) {
        return false;
    }
    if (m_cursor != m_segmentEnd) {
        return Fail("段尾还有 " + std::to_string(m_segmentEnd - m_cursor) +
                    " 字节未读取");
    }
    m_segmentOpen = false;
    return true;
}

bool BinaryReader::SkipSegment() {
    if (!EnsureActive()) {
        return false;
    }
    m_cursor = m_segmentEnd;
    m_segmentOpen = false;
    return true;
}

bool BinaryReader::Skip(std::size_t bytes) {
    if (!EnsureActive()) {
        return false;
    }
    if (bytes > m_segmentEnd - m_cursor) {
        return Fail("跳过 " + std::to_string(bytes) + " 字节超出当前段");
    }
    m_cursor += bytes;
    return true;
}

bool BinaryReader::ReadBytes(void* data, std::size_t size) {
    if (!EnsureActive()) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        return Fail("读取数据指针为空");
    }
    if (size > m_segmentEnd - m_cursor) {
        return Fail("段数据截断: 需要 " + std::to_string(size) +
                    " 字节，剩余 " + std::to_string(m_segmentEnd - m_cursor));
    }
    return ReadRaw(data, size);
}

bool BinaryReader::ReadString(std::string& value, std::uint32_t maxBytes) {
    if (!EnsureActive()) {
        return false;
    }

    std::uint32_t length = 0;
    if (!Read(length)) {
        return false;
    }
    const std::uint32_t limit = std::min(maxBytes, kMaxStringBytes);
    if (length > limit) {
        return Fail("字符串长度 " + std::to_string(length) + " 超过上限 " +
                    std::to_string(limit));
    }
    if (length > m_segmentEnd - m_cursor) {
        return Fail("字符串数据截断: 需要 " + std::to_string(length) +
                    " 字节，剩余 " + std::to_string(m_segmentEnd - m_cursor));
    }

    try {
        value.assign(reinterpret_cast<const char*>(m_data.data() + m_cursor),
                     length);
    } catch (const std::exception&) {
        return Fail("读取字符串失败（内存不足）");
    }
    m_cursor += length;
    return true;
}

bool BinaryReader::EnsureActive() {
    if (!Ok()) {
        return false;
    }
    if (!m_segmentOpen) {
        return Fail("没有打开的段");
    }
    return true;
}

bool BinaryReader::ReadRaw(void* data, std::size_t size) {
    if (size > m_data.size() - m_cursor) {
        return Fail("数据截断: 需要 " + std::to_string(size) + " 字节，剩余 " +
                    std::to_string(m_data.size() - m_cursor));
    }
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        return Fail("读取数据指针为空");
    }
    std::memcpy(data, m_data.data() + m_cursor, size);
    m_cursor += size;
    return true;
}

bool BinaryReader::Fail(std::string message) {
    if (m_error.empty()) {
        m_error = "偏移 " + std::to_string(m_cursor) + ": " + std::move(message);
    }
    return false;
}

}  // namespace zl::save
