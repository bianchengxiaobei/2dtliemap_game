#include "net/PlayerIdentity.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <vector>

namespace zl::net {
namespace {

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end) {
        const char c = text[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++begin;
    }
    while (end > begin) {
        const char c = text[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

bool IsValidPlayerIdentity(std::string_view identity) {
    if (identity.empty() || identity.size() > kMaxPlayerIdentityBytes) {
        return false;
    }
    for (const unsigned char c : identity) {
        const bool alphaNumeric =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');
        if (!alphaNumeric && c != '_' && c != '-' && c != '.' && c != ':') {
            return false;
        }
    }
    return true;
}

std::string GeneratePlayerIdentity() {
    static std::atomic<std::uint64_t> counter{0};
    std::uint64_t seed = 0;
    try {
        std::random_device device;
        seed = (static_cast<std::uint64_t>(device()) << 32U) ^ device();
    } catch (...) {
        seed = 0;
    }
    seed ^= static_cast<std::uint64_t>(
                std::chrono::high_resolution_clock::now()
                    .time_since_epoch()
                    .count()) ^
            (++counter * 0x9E3779B97F4A7C15ull);

    std::mt19937_64 rng(seed);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string identity(32, '0');
    for (std::size_t i = 0; i < identity.size(); i += 16) {
        const std::uint64_t value = rng();
        for (std::size_t j = 0; j < 16 && i + j < identity.size(); ++j) {
            identity[i + j] = kHex[(value >> ((15 - j) * 4U)) & 0x0Fu];
        }
    }
    return identity;
}

std::string LoadOrCreatePlayerIdentity(const std::filesystem::path& path,
                                       std::string* error) {
    if (error != nullptr) {
        error->clear();
    }

    if (!path.empty()) {
        std::ifstream input(path, std::ios::binary);
        if (input) {
            std::string text((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
            const std::string existing = Trim(text);
            if (IsValidPlayerIdentity(existing)) {
                return existing;
            }
        }
    }

    const std::string generated = GeneratePlayerIdentity();
    if (path.empty()) {
        return generated;
    }

    std::error_code fsError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), fsError);
    }
    if (!fsError) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (output) {
            output << generated << '\n';
            output.flush();
            if (!output) {
                fsError = std::make_error_code(std::errc::io_error);
            }
        } else {
            fsError = std::make_error_code(std::errc::io_error);
        }
    }
    if (fsError && error != nullptr) {
        *error = "玩家身份写盘失败：" + fsError.message();
    }
    return generated;
}

}  // namespace zl::net
