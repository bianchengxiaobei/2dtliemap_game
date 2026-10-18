#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "save/SaveTypes.h"

namespace zl::save {

bool EncodePng(const ThumbnailImage& image, std::vector<std::uint8_t>* out,
               std::string* error = nullptr);

bool WritePng(const std::filesystem::path& path, const ThumbnailImage& image,
              std::string* error = nullptr);

}  // namespace zl::save
