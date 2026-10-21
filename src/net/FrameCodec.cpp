#include "net/FrameCodec.h"

#include <algorithm>
#include <cstring>

namespace zl::net {
namespace {

std::uint32_t ReadU32Le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void WriteU32Le(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
    data[2] = static_cast<std::uint8_t>(value >> 16U);
    data[3] = static_cast<std::uint8_t>(value >> 24U);
}

}  // namespace

std::vector<std::uint8_t> EncodeFrame(std::span<const std::uint8_t> payload,
                                      std::string* error) {
    if (payload.size() > kMaxFramePayload) {
        if (error != nullptr) {
            *error = "帧载荷超过 64KB 上限";
        }
        return {};
    }

    std::vector<std::uint8_t> bytes(kFrameHeaderSize + payload.size());
    WriteU32Le(bytes.data(), kFrameMagic);
    WriteU32Le(bytes.data() + 4, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        std::memcpy(bytes.data() + kFrameHeaderSize, payload.data(), payload.size());
    }
    return bytes;
}

bool FrameDecoder::Push(std::span<const std::uint8_t> bytes, std::string* error) {
    if (bytes.empty()) {
        return true;
    }

    if (m_offset > 0) {
        m_buffer.erase(m_buffer.begin(),
                       m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
        m_offset = 0;
    }
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());

    while (m_buffer.size() - m_offset >= kFrameHeaderSize) {
        const std::uint8_t* header = m_buffer.data() + m_offset;
        const std::uint32_t magic = ReadU32Le(header);
        if (magic != kFrameMagic) {
            if (error != nullptr) {
                *error = "帧 magic 非法";
            }
            Clear();
            return false;
        }

        const std::size_t payloadSize = ReadU32Le(header + 4);
        if (payloadSize > kMaxFramePayload) {
            if (error != nullptr) {
                *error = "帧长度超过 64KB 上限";
            }
            Clear();
            return false;
        }
        if (m_buffer.size() - m_offset < kFrameHeaderSize + payloadSize) {
            break;
        }

        const auto begin = m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset + kFrameHeaderSize);
        const auto end = begin + static_cast<std::ptrdiff_t>(payloadSize);
        m_frames.emplace_back(begin, end);
        m_offset += kFrameHeaderSize + payloadSize;
    }

    if (m_offset >= m_buffer.size()) {
        m_buffer.clear();
        m_offset = 0;
    } else if (m_offset > 4096 && m_offset * 2 >= m_buffer.size()) {
        m_buffer.erase(m_buffer.begin(),
                       m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
        m_offset = 0;
    }
    return true;
}

bool FrameDecoder::Pop(std::vector<std::uint8_t>* frame) {
    if (m_frames.empty() || frame == nullptr) {
        return false;
    }
    *frame = std::move(m_frames.front());
    m_frames.pop_front();
    return true;
}

std::size_t FrameDecoder::BufferedBytes() const {
    return m_buffer.size() - m_offset;
}

void FrameDecoder::Clear() {
    m_buffer.clear();
    m_offset = 0;
    m_frames.clear();
}

}  // namespace zl::net
