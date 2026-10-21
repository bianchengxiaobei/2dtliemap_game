#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

inline constexpr std::uint32_t kFrameMagic = 0x31464C5Au;
inline constexpr std::size_t kFrameHeaderSize = 8;
inline constexpr std::size_t kMaxFramePayload = 64 * 1024;

std::vector<std::uint8_t> EncodeFrame(std::span<const std::uint8_t> payload,
                                      std::string* error = nullptr);

class FrameDecoder {
public:
    bool Push(std::span<const std::uint8_t> bytes, std::string* error = nullptr);
    bool Pop(std::vector<std::uint8_t>* frame);
    std::size_t BufferedBytes() const;
    std::size_t PendingFrames() const { return m_frames.size(); }
    void Clear();

private:
    std::vector<std::uint8_t> m_buffer;
    std::size_t m_offset = 0;
    std::deque<std::vector<std::uint8_t>> m_frames;
};

}  // namespace zl::net
