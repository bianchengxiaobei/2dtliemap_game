#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "save/BinaryFormat.h"

namespace zl::save {

// M10-T1：二进制写入器。
//
// 用法：
//   BinaryWriter writer;
//   writer.BeginSegment(kMagicWorld, kFormatVersion);
//   writer.Write(...);
//   writer.EndSegment();
//   SaveBytes(writer.Data());
//
// 每个段写入时先占位段头，EndSegment 回填长度；写入器只负责生成字节，
// 文件 IO/原子替换/后台写盘由 M10-T4/T5 的 SaveManager 负责。
class BinaryWriter {
public:
    bool BeginSegment(std::uint32_t magic, std::uint16_t version,
                      std::uint16_t flags = 0);
    bool EndSegment();

    bool Ok() const { return m_error.empty(); }
    const std::string& Error() const { return m_error; }

    bool WriteBytes(const void* data, std::size_t size);
    bool WriteBytes(std::span<const std::uint8_t> data) {
        return WriteBytes(data.data(), data.size());
    }
    bool WriteString(std::string_view value,
                     std::uint32_t maxBytes = kMaxStringBytes);

    template <typename T>
    bool Write(const T& value) {
        if (!EnsureActive()) {
            return false;
        }

        if constexpr (std::is_same_v<T, std::string> ||
                      std::is_same_v<T, std::string_view>) {
            return WriteString(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            const std::uint8_t byte = value ? 1u : 0u;
            return WriteBytes(&byte, sizeof(byte));
        } else if constexpr (std::is_enum_v<T>) {
            using Underlying = std::underlying_type_t<T>;
            return Write(static_cast<Underlying>(value));
        } else if constexpr (std::is_integral_v<T>) {
            static_assert(sizeof(T) <= 8, "BinaryWriter 只支持最长 64 位的整数");
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned raw = static_cast<Unsigned>(value);
            std::array<std::uint8_t, sizeof(T)> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((raw >> (i * 8)) & 0xFFu);
            }
            return WriteBytes(bytes.data(), bytes.size());
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                return Write(std::bit_cast<std::uint32_t>(value));
            } else if constexpr (sizeof(T) == 8) {
                return Write(std::bit_cast<std::uint64_t>(value));
            } else {
                static_assert(detail::AlwaysFalse<T>::value,
                              "BinaryWriter 只支持 float/double");
            }
        } else if constexpr (detail::IsStdVectorV<T>) {
            if (value.size() > kMaxContainerElements) {
                return Fail("容器元素数量超过上限");
            }
            if (!Write(static_cast<std::uint32_t>(value.size()))) {
                return false;
            }
            for (const auto& item : value) {
                if (!Write(item)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (detail::IsStdArrayV<T>) {
            for (const auto& item : value) {
                if (!Write(item)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (detail::IsStdOptionalV<T>) {
            if (!Write(value.has_value())) {
                return false;
            }
            return value ? Write(*value) : true;
        } else if constexpr (detail::IsStdMapV<T>) {
            if (value.size() > kMaxContainerElements) {
                return Fail("容器元素数量超过上限");
            }
            if (!Write(static_cast<std::uint32_t>(value.size()))) {
                return false;
            }
            for (const auto& [key, mapped] : value) {
                if (!Write(key) || !Write(mapped)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (detail::IsStdPairV<T>) {
            return Write(value.first) && Write(value.second);
        } else {
            static_assert(detail::AlwaysFalse<T>::value,
                          "BinaryWriter 不支持该类型；请显式拆字段后写入");
        }
    }

    std::span<const std::uint8_t> Data() const {
        return std::span<const std::uint8_t>(m_data.data(), m_data.size());
    }
    std::size_t Size() const { return m_data.size(); }
    void Clear();

private:
    bool EnsureActive();
    bool WriteRaw(const void* data, std::size_t size);
    bool Fail(std::string message);

    std::vector<std::uint8_t> m_data;
    std::string m_error;
    std::size_t m_segmentHeaderOffset = 0;
    std::size_t m_segmentPayloadOffset = 0;
    bool m_segmentOpen = false;
};

}  // namespace zl::save
