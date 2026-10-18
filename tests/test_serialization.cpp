#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace {

using namespace zl::save;

constexpr std::uint32_t kTestMagic = MakeMagic('Z', 'L', 'T', 'S');
constexpr std::uint32_t kUnknownMagic = MakeMagic('U', 'N', 'K', 'N');

enum class TestColor : std::uint16_t {
    Red = 1,
    Blue = 500,
};

std::vector<std::uint8_t> CopyBytes(std::span<const std::uint8_t> data) {
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

}  // namespace

TEST_CASE("M10-T1 基本类型、字符串与容器往返") {
    BinaryWriter writer;
    REQUIRE(writer.BeginSegment(kTestMagic, kFormatVersion));

    const std::uint8_t u8 = 0x12;
    const std::int8_t i8 = -7;
    const std::uint16_t u16 = 0xBEEF;
    const std::int16_t i16 = -12345;
    const std::uint32_t u32 = 0xDEADBEEF;
    const std::int32_t i32 = -123456789;
    const std::uint64_t u64 = 0x0123456789ABCDEFull;
    const std::int64_t i64 = -9000000000000000000ll;
    const float f32 = 3.5f;
    const double f64 = -1234.25;
    const bool yes = true;
    const bool no = false;
    const TestColor color = TestColor::Blue;
    const std::string text = "你好，ZomboidLike";
    const std::vector<std::int32_t> numbers{1, -2, 3, -4};
    const std::vector<std::vector<std::string>> nested{{"a", ""}, {"中文", "b"}};
    const std::map<std::string, std::int32_t> table{{"alpha", 1}, {"beta", 2}};
    const std::optional<std::string> hasValue = std::string("optional");
    const std::optional<std::string> emptyValue;
    const std::array<std::uint16_t, 3> fixed{10, 20, 30};
    const std::pair<std::uint32_t, std::string> pair{7, "pair"};

    CHECK(writer.Write(u8));
    CHECK(writer.Write(i8));
    CHECK(writer.Write(u16));
    CHECK(writer.Write(i16));
    CHECK(writer.Write(u32));
    CHECK(writer.Write(i32));
    CHECK(writer.Write(u64));
    CHECK(writer.Write(i64));
    CHECK(writer.Write(f32));
    CHECK(writer.Write(f64));
    CHECK(writer.Write(yes));
    CHECK(writer.Write(no));
    CHECK(writer.Write(color));
    CHECK(writer.Write(text));
    CHECK(writer.Write(numbers));
    CHECK(writer.Write(nested));
    CHECK(writer.Write(table));
    CHECK(writer.Write(hasValue));
    CHECK(writer.Write(emptyValue));
    CHECK(writer.Write(fixed));
    CHECK(writer.Write(pair));

    const std::array<std::uint8_t, 4> raw{0x00, 0x01, 0xFE, 0xFF};
    CHECK(writer.WriteBytes(raw.data(), raw.size()));
    REQUIRE(writer.EndSegment());
    REQUIRE(writer.Ok());

    const auto bytes = writer.Data();
    CHECK(bytes.size() > kSegmentHeaderSize);
    CHECK(bytes[0] == 'Z');
    CHECK(bytes[1] == 'L');
    CHECK(bytes[2] == 'T');
    CHECK(bytes[3] == 'S');

    BinaryReader reader(bytes);
    SegmentHeader header;
    REQUIRE(reader.BeginSegment(header, kTestMagic, kFormatVersion));
    CHECK(header.magic == kTestMagic);
    CHECK(header.version == kFormatVersion);
    CHECK(header.flags == 0);
    CHECK(header.payloadSize == bytes.size() - kSegmentHeaderSize);

    std::uint8_t readU8 = 0;
    std::int8_t readI8 = 0;
    std::uint16_t readU16 = 0;
    std::int16_t readI16 = 0;
    std::uint32_t readU32 = 0;
    std::int32_t readI32 = 0;
    std::uint64_t readU64 = 0;
    std::int64_t readI64 = 0;
    float readF32 = 0.0f;
    double readF64 = 0.0;
    bool readYes = false;
    bool readNo = true;
    TestColor readColor = TestColor::Red;
    std::string readText;
    std::vector<std::int32_t> readNumbers;
    std::vector<std::vector<std::string>> readNested;
    std::map<std::string, std::int32_t> readTable;
    std::optional<std::string> readHasValue;
    std::optional<std::string> readEmptyValue = std::string("not empty");
    std::array<std::uint16_t, 3> readFixed{};
    std::pair<std::uint32_t, std::string> readPair;
    std::array<std::uint8_t, 4> readRaw{};

    REQUIRE(reader.Read(readU8));
    REQUIRE(reader.Read(readI8));
    REQUIRE(reader.Read(readU16));
    REQUIRE(reader.Read(readI16));
    REQUIRE(reader.Read(readU32));
    REQUIRE(reader.Read(readI32));
    REQUIRE(reader.Read(readU64));
    REQUIRE(reader.Read(readI64));
    REQUIRE(reader.Read(readF32));
    REQUIRE(reader.Read(readF64));
    REQUIRE(reader.Read(readYes));
    REQUIRE(reader.Read(readNo));
    REQUIRE(reader.Read(readColor));
    REQUIRE(reader.Read(readText));
    REQUIRE(reader.Read(readNumbers));
    REQUIRE(reader.Read(readNested));
    REQUIRE(reader.Read(readTable));
    REQUIRE(reader.Read(readHasValue));
    REQUIRE(reader.Read(readEmptyValue));
    REQUIRE(reader.Read(readFixed));
    REQUIRE(reader.Read(readPair));
    REQUIRE(reader.ReadBytes(readRaw.data(), readRaw.size()));
    REQUIRE(reader.EndSegment());
    REQUIRE(reader.Ok());

    CHECK(readU8 == u8);
    CHECK(readI8 == i8);
    CHECK(readU16 == u16);
    CHECK(readI16 == i16);
    CHECK(readU32 == u32);
    CHECK(readI32 == i32);
    CHECK(readU64 == u64);
    CHECK(readI64 == i64);
    CHECK(std::bit_cast<std::uint32_t>(readF32) == std::bit_cast<std::uint32_t>(f32));
    CHECK(std::bit_cast<std::uint64_t>(readF64) == std::bit_cast<std::uint64_t>(f64));
    CHECK(readYes == yes);
    CHECK(readNo == no);
    CHECK(readColor == color);
    CHECK(readText == text);
    CHECK(readNumbers == numbers);
    CHECK(readNested == nested);
    CHECK(readTable == table);
    REQUIRE(readHasValue.has_value());
    CHECK(*readHasValue == "optional");
    CHECK_FALSE(readEmptyValue.has_value());
    CHECK(readFixed == fixed);
    CHECK(readPair == pair);
    CHECK(readRaw == raw);
}

TEST_CASE("M10-T1 多段、未知段跳过与长度回填") {
    BinaryWriter writer;
    REQUIRE(writer.BeginSegment(kMagicWorld, kFormatVersion));
    CHECK(writer.Write(std::uint32_t{42}));
    REQUIRE(writer.EndSegment());

    REQUIRE(writer.BeginSegment(kUnknownMagic, 3, 0x1234));
    CHECK(writer.WriteString("skip-me"));
    REQUIRE(writer.EndSegment());

    REQUIRE(writer.BeginSegment(kMagicEntities, kFormatVersion));
    CHECK(writer.Write(true));
    REQUIRE(writer.EndSegment());

    REQUIRE(writer.BeginSegment(kMagicPlayer, kFormatVersion));
    REQUIRE(writer.EndSegment());

    BinaryReader reader(writer.Data());
    SegmentHeader header;
    REQUIRE(reader.BeginSegment(header));
    CHECK(header.magic == kMagicWorld);
    CHECK(header.payloadSize == 4);
    std::uint32_t value = 0;
    REQUIRE(reader.Read(value));
    CHECK(value == 42);
    REQUIRE(reader.EndSegment());

    REQUIRE(reader.BeginSegment(header));
    CHECK(header.magic == kUnknownMagic);
    CHECK(header.version == 3);
    CHECK(header.flags == 0x1234);
    CHECK(header.payloadSize == 4 + 7);
    REQUIRE(reader.SkipSegment());

    REQUIRE(reader.BeginSegment(header, kMagicEntities, kFormatVersion));
    bool flag = false;
    REQUIRE(reader.Read(flag));
    CHECK(flag);
    REQUIRE(reader.EndSegment());

    REQUIRE(reader.BeginSegment(header, kMagicPlayer, kFormatVersion));
    CHECK(header.payloadSize == 0);
    CHECK(reader.SegmentBytes().empty());
    REQUIRE(reader.EndSegment());

    CHECK(reader.Offset() == writer.Size());
    CHECK(reader.AtEnd());
    CHECK_FALSE(reader.BeginSegment(header));  // 正常读完不应被当成损坏
    CHECK(reader.Ok());
    CHECK(reader.Error().empty());
}

TEST_CASE("M10-T1 版本与 magic 校验") {
    BinaryWriter writer;
    REQUIRE(writer.BeginSegment(kMagicWorld, 2));
    CHECK(writer.Write(std::uint32_t{1}));
    REQUIRE(writer.EndSegment());

    BinaryReader exact(writer.Data());
    SegmentHeader header;
    CHECK_FALSE(exact.BeginSegment(header, kMagicWorld, 1));
    CHECK(exact.Error().find("版本") != std::string::npos);
    CHECK(exact.Error().find("2") != std::string::npos);

    BinaryReader range(writer.Data());
    REQUIRE(range.BeginSegment(header, kMagicWorld, 1, 2));
    std::uint32_t rangeValue = 0;
    REQUIRE(range.Read(rangeValue));
    REQUIRE(range.EndSegment());

    BinaryReader magicMismatch(writer.Data());
    CHECK_FALSE(magicMismatch.BeginSegment(header, kMagicEntities, 2));
    CHECK(magicMismatch.Error().find("标识") != std::string::npos);

    BinaryWriter invalid;
    CHECK_FALSE(invalid.BeginSegment(kMagicWorld, 0));
    CHECK(invalid.Error().find("版本") != std::string::npos);
}

TEST_CASE("M10-T1 截断与损坏数据报告错误而不越界") {
    BinaryWriter writer;
    REQUIRE(writer.BeginSegment(kTestMagic, kFormatVersion));
    CHECK(writer.Write(std::uint32_t{0x11223344}));
    REQUIRE(writer.EndSegment());
    const auto full = CopyBytes(writer.Data());

    SUBCASE("段数据截断") {
        auto truncated = full;
        truncated.pop_back();
        BinaryReader reader(truncated);
        SegmentHeader header;
        CHECK_FALSE(reader.BeginSegment(header));
        CHECK(reader.Error().find("截断") != std::string::npos);
    }

    SUBCASE("段头截断") {
        BinaryReader reader(std::span<const std::uint8_t>(full.data(), 8));
        SegmentHeader header;
        CHECK_FALSE(reader.BeginSegment(header));
        CHECK(reader.Error().find("截断") != std::string::npos);
    }

    SUBCASE("字符串长度超过剩余数据") {
        BinaryWriter bad;
        REQUIRE(bad.BeginSegment(kTestMagic, kFormatVersion));
        CHECK(bad.Write(std::uint32_t{100}));
        REQUIRE(bad.EndSegment());
        BinaryReader reader(bad.Data());
        SegmentHeader header;
        REQUIRE(reader.BeginSegment(header, kTestMagic, kFormatVersion));
        std::string text;
        CHECK_FALSE(reader.Read(text));
        CHECK(reader.Error().find("字符串") != std::string::npos);
    }

    SUBCASE("非法 bool") {
        BinaryWriter bad;
        REQUIRE(bad.BeginSegment(kTestMagic, kFormatVersion));
        CHECK(bad.Write(std::uint8_t{2}));
        REQUIRE(bad.EndSegment());
        BinaryReader reader(bad.Data());
        SegmentHeader header;
        REQUIRE(reader.BeginSegment(header, kTestMagic, kFormatVersion));
        bool value = false;
        CHECK_FALSE(reader.Read(value));
        CHECK(reader.Error().find("布尔") != std::string::npos);
    }

    SUBCASE("容器数量超限") {
        BinaryWriter bad;
        REQUIRE(bad.BeginSegment(kTestMagic, kFormatVersion));
        CHECK(bad.Write(static_cast<std::uint32_t>(kMaxContainerElements + 1)));
        REQUIRE(bad.EndSegment());
        BinaryReader reader(bad.Data());
        SegmentHeader header;
        REQUIRE(reader.BeginSegment(header, kTestMagic, kFormatVersion));
        std::vector<std::uint8_t> values;
        CHECK_FALSE(reader.Read(values));
        CHECK(reader.Error().find("上限") != std::string::npos);
    }

    SUBCASE("段尾有多余字节") {
        BinaryReader reader(full);
        SegmentHeader header;
        REQUIRE(reader.BeginSegment(header, kTestMagic, kFormatVersion));
        std::uint8_t first = 0;
        REQUIRE(reader.Read(first));
        CHECK_FALSE(reader.EndSegment());
        CHECK(reader.Error().find("段尾") != std::string::npos);
    }
}

TEST_CASE("M10-T1 写入/读取 API 的状态错误") {
    BinaryWriter writer;
    CHECK_FALSE(writer.Write(std::uint32_t{1}));
    CHECK(writer.Error().find("段") != std::string::npos);
    CHECK_FALSE(writer.EndSegment());

    writer.Clear();
    REQUIRE(writer.BeginSegment(kTestMagic, kFormatVersion));
    CHECK_FALSE(writer.BeginSegment(kTestMagic, kFormatVersion));
    CHECK(writer.Error().find("尚未结束") != std::string::npos);
    CHECK_FALSE(writer.EndSegment());
    writer.Clear();
    REQUIRE(writer.BeginSegment(kTestMagic, kFormatVersion));
    REQUIRE(writer.EndSegment());
    writer.Clear();

    BinaryWriter tooLong;
    REQUIRE(tooLong.BeginSegment(kTestMagic, kFormatVersion));
    CHECK_FALSE(tooLong.WriteString("abc", 2));
    CHECK(tooLong.Error().find("字符串") != std::string::npos);

    BinaryReader empty{std::span<const std::uint8_t>()};
    std::uint32_t value = 0;
    CHECK_FALSE(empty.Read(value));
    CHECK(empty.Error().find("段") != std::string::npos);

    BinaryReader cleanEmpty{std::span<const std::uint8_t>()};
    SegmentHeader emptyHeader;
    CHECK(cleanEmpty.AtEnd());
    CHECK_FALSE(cleanEmpty.BeginSegment(emptyHeader));
    CHECK(cleanEmpty.Ok());
}
