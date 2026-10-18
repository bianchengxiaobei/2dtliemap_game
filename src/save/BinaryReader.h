#pragma once

#include <algorithm>
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

// M10-T1：二进制读取器。
//
// 读取器绑定一段只读字节（通常来自文件），按段顺序解析：
//   SegmentHeader header;
//   while (!reader.AtEnd() && reader.BeginSegment(header)) {
//       if (header.magic == kMagicWorld) { ...; reader.EndSegment(); }
//       else { reader.SkipSegment(); }  // 未知段可跳过
//   }
//   if (!reader.Ok()) { /* 报告 reader.Error() */ }
// 所有读取失败都设置 Error()，不会越界访问；损坏数据由调用方决定是否删档/回退备份。
class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::uint8_t> data) : m_data(data) {}

    // 读取任意段。成功后必须调用 EndSegment 或 SkipSegment。
    bool BeginSegment(SegmentHeader& header);
    bool BeginSegment(SegmentHeader& header, std::uint32_t expectedMagic,
                      std::uint16_t expectedVersion);
    bool BeginSegment(SegmentHeader& header, std::uint32_t expectedMagic,
                      std::uint16_t minVersion, std::uint16_t maxVersion);
    // 使用内部 Header() 的便捷版本。
    bool BeginSegment(std::uint32_t expectedMagic, std::uint16_t expectedVersion);

    // 要求当前段恰好读完；有剩余字节视为格式错误（防止段长得不合理却静默通过）。
    bool EndSegment();
    // 跳过未知段（不校验内容），之后可继续读下一段。
    bool SkipSegment();
    bool Skip(std::size_t bytes);

    bool ReadBytes(void* data, std::size_t size);
    bool ReadString(std::string& value,
                    std::uint32_t maxBytes = kMaxStringBytes);

    template <typename T>
    bool Read(T& value) {
        if (!EnsureActive()) {
            return false;
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return ReadString(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t byte = 0;
            if (!ReadBytes(&byte, sizeof(byte))) {
                return false;
            }
            if (byte > 1) {
                return Fail("布尔值非法: " + std::to_string(byte));
            }
            value = (byte != 0);
            return true;
        } else if constexpr (std::is_enum_v<T>) {
            using Underlying = std::underlying_type_t<T>;
            Underlying raw{};
            if (!Read(raw)) {
                return false;
            }
            value = static_cast<T>(raw);
            return true;
        } else if constexpr (std::is_integral_v<T>) {
            static_assert(sizeof(T) <= 8, "BinaryReader 只支持最长 64 位的整数");
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned raw = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                std::uint8_t byte = 0;
                if (!ReadBytes(&byte, sizeof(byte))) {
                    return false;
                }
                raw |= static_cast<Unsigned>(byte) << (i * 8);
            }
            value = std::bit_cast<T>(raw);
            return true;
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                std::uint32_t raw = 0;
                if (!Read(raw)) {
                    return false;
                }
                value = std::bit_cast<T>(raw);
                return true;
            } else if constexpr (sizeof(T) == 8) {
                std::uint64_t raw = 0;
                if (!Read(raw)) {
                    return false;
                }
                value = std::bit_cast<T>(raw);
                return true;
            } else {
                static_assert(detail::AlwaysFalse<T>::value,
                              "BinaryReader 只支持 float/double");
            }
        } else if constexpr (detail::IsStdVectorV<T>) {
            std::uint32_t count = 0;
            if (!Read(count)) {
                return false;
            }
            if (count > kMaxContainerElements) {
                return Fail("容器元素数量 " + std::to_string(count) + " 超过上限");
            }
            if (count > m_segmentEnd - m_cursor) {
                return Fail("容器元素数量 " + std::to_string(count) +
                            " 超过当前段剩余数据");
            }
            try {
                value.clear();
                value.reserve(std::min<std::uint32_t>(count, 4096));
                for (std::uint32_t i = 0; i < count; ++i) {
                    typename T::value_type item{};
                    if (!Read(item)) {
                        return false;
                    }
                    value.push_back(std::move(item));
                }
            } catch (const std::exception&) {
                return Fail("读取容器失败（内存不足）");
            }
            return true;
        } else if constexpr (detail::IsStdArrayV<T>) {
            for (auto& item : value) {
                if (!Read(item)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (detail::IsStdOptionalV<T>) {
            bool hasValue = false;
            if (!Read(hasValue)) {
                return false;
            }
            if (!hasValue) {
                value.reset();
                return true;
            }
            value.emplace();
            return Read(*value);
        } else if constexpr (detail::IsStdMapV<T>) {
            std::uint32_t count = 0;
            if (!Read(count)) {
                return false;
            }
            if (count > kMaxContainerElements) {
                return Fail("容器元素数量 " + std::to_string(count) + " 超过上限");
            }
            if (count > m_segmentEnd - m_cursor) {
                return Fail("容器元素数量 " + std::to_string(count) +
                            " 超过当前段剩余数据");
            }
            try {
                value.clear();
                for (std::uint32_t i = 0; i < count; ++i) {
                    typename T::key_type key{};
                    typename T::mapped_type mapped{};
                    if (!Read(key) || !Read(mapped)) {
                        return false;
                    }
                    value.emplace(std::move(key), std::move(mapped));
                }
            } catch (const std::exception&) {
                return Fail("读取映射失败（内存不足或键冲突）");
            }
            return true;
        } else if constexpr (detail::IsStdPairV<T>) {
            return Read(value.first) && Read(value.second);
        } else {
            static_assert(detail::AlwaysFalse<T>::value,
                          "BinaryReader 不支持该类型；请显式拆字段后读取");
        }
    }

    const SegmentHeader& Header() const { return m_header; }
    std::span<const std::uint8_t> SegmentBytes() const {
        if (!m_haveSegment) {
            return {};
        }
        return m_data.subspan(m_payloadOffset, m_segmentEnd - m_payloadOffset);
    }

    std::size_t Offset() const { return m_cursor; }
    std::size_t Size() const { return m_data.size(); }
    // 当前没有打开段且已到字节末尾：顶层循环用它区分“正常结束”和“段头截断”。
    bool AtEnd() const { return !m_segmentOpen && m_cursor >= m_data.size(); }
    std::size_t RemainingInSegment() const {
        return m_segmentOpen ? (m_segmentEnd - m_cursor) : 0;
    }
    bool Ok() const { return m_error.empty(); }
    const std::string& Error() const { return m_error; }

private:
    bool EnsureActive();
    bool ReadRaw(void* data, std::size_t size);
    bool Fail(std::string message);

    std::span<const std::uint8_t> m_data;
    std::size_t m_cursor = 0;
    std::size_t m_payloadOffset = 0;
    std::size_t m_segmentEnd = 0;
    bool m_segmentOpen = false;
    bool m_haveSegment = false;
    SegmentHeader m_header;
    std::string m_error;
};

}  // namespace zl::save
