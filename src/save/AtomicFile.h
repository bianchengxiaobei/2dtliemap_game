#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace zl::save {

// M10-T5：单文件原子写（临时文件 + 同目录改名）。
// 成功返回 true；失败时目标文件保持原内容（若存在），临时文件被清理。
bool WriteFileAtomic(const std::filesystem::path& path,
                     std::span<const std::uint8_t> bytes,
                     std::string* error = nullptr);

}  // namespace zl::save
