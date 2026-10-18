#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace zl::save {

// M10-T1：存档二进制格式的公共定义。
//
// 文件/段布局（全部小端）：
//   magic:u32 | version:u16 | flags:u16 | payloadSize:u64 | payload...
// magic 采用低字节在前的写法，因此 MakeMagic('Z','L','S','V') 在磁盘上就是 "ZLSV"。
inline constexpr std::uint16_t kFormatVersion = 1;
inline constexpr std::size_t kSegmentHeaderSize = 16;
inline constexpr std::uint64_t kMaxSegmentPayloadBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::uint32_t kMaxStringBytes = 16u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxContainerElements = 1'000'000u;

constexpr std::uint32_t MakeMagic(char a, char b, char c, char d) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

inline constexpr std::uint32_t kMagicSaveHeader = MakeMagic('Z', 'L', 'S', 'V');
inline constexpr std::uint32_t kMagicWorld = MakeMagic('Z', 'L', 'W', 'D');
inline constexpr std::uint32_t kMagicEntities = MakeMagic('Z', 'L', 'E', 'N');
inline constexpr std::uint32_t kMagicPlayer = MakeMagic('Z', 'L', 'P', 'L');
inline constexpr std::uint32_t kMagicChunkDiff = MakeMagic('Z', 'L', 'C', 'D');

inline std::string MagicToString(std::uint32_t magic) {
    std::string result(4, '?');
    for (std::size_t i = 0; i < result.size(); ++i) {
        const char c = static_cast<char>((magic >> (i * 8)) & 0xFFu);
        result[i] = (c >= 32 && c <= 126) ? c : '?';
    }
    return result;
}

// 段头。payloadSize 是段头之后的有效负载字节数，不含段头自身。
struct SegmentHeader {
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t payloadSize = 0;
};

namespace detail {

template <typename T>
struct IsStdVector : std::false_type {};
template <typename T, typename Alloc>
struct IsStdVector<std::vector<T, Alloc>> : std::true_type {};

template <typename T>
struct IsStdArray : std::false_type {};
template <typename T, std::size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type {};

template <typename T>
struct IsStdOptional : std::false_type {};
template <typename T>
struct IsStdOptional<std::optional<T>> : std::true_type {};

template <typename T>
struct IsStdMap : std::false_type {};
template <typename K, typename V, typename Compare, typename Alloc>
struct IsStdMap<std::map<K, V, Compare, Alloc>> : std::true_type {};

template <typename T>
struct IsStdPair : std::false_type {};
template <typename A, typename B>
struct IsStdPair<std::pair<A, B>> : std::true_type {};

template <typename T>
inline constexpr bool IsStdVectorV = IsStdVector<T>::value;
template <typename T>
inline constexpr bool IsStdArrayV = IsStdArray<T>::value;
template <typename T>
inline constexpr bool IsStdOptionalV = IsStdOptional<T>::value;
template <typename T>
inline constexpr bool IsStdMapV = IsStdMap<T>::value;
template <typename T>
inline constexpr bool IsStdPairV = IsStdPair<T>::value;

template <typename T>
struct AlwaysFalse : std::false_type {};

}  // namespace detail

}  // namespace zl::save
