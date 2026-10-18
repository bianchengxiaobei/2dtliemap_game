#include "save/AtomicFile.h"

#include <atomic>
#include <fstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zl::save {
namespace {

std::atomic<std::uint64_t> g_tempCounter{1};

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

}  // namespace

bool WriteFileAtomic(const std::filesystem::path& path,
                     std::span<const std::uint8_t> bytes, std::string* error) {
    if (path.empty()) {
        return Fail(error, "原子写路径为空");
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Fail(error, "创建目录失败: " + path.parent_path().string() +
                                   "（" + ec.message() + "）");
        }
    }

    std::filesystem::path temp = path;
    temp += ".tmp-" + std::to_string(g_tempCounter.fetch_add(1));

    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Fail(error, "无法写入临时文件: " + temp.string());
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    file.flush();
    if (!file) {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return Fail(error, "写入临时文件失败: " + temp.string());
    }
    file.close();

#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return Fail(error, "原子替换文件失败: " + path.string() + "（" +
                               std::system_category().message(static_cast<int>(code)) +
                               "）");
    }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return Fail(error, "原子替换文件失败: " + path.string() + "（" +
                               ec.message() + "）");
    }
#endif

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace zl::save
