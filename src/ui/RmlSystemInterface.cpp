#include "ui/RmlSystemInterface.h"

#include <filesystem>

#include "core/Log.h"

namespace zl::ui {

RmlSystemInterface::RmlSystemInterface() : m_start(std::chrono::steady_clock::now()) {}

double RmlSystemInterface::GetElapsedTime() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - m_start).count();
}

bool RmlSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            ZL_ERROR("ui", "{}", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            ZL_WARN("ui", "{}", message.c_str());
            break;
        case Rml::Log::LT_INFO:
            ZL_INFO("ui", "{}", message.c_str());
            break;
        case Rml::Log::LT_DEBUG:
        default:
            ZL_DEBUG("ui", "{}", message.c_str());
            break;
    }
    return true;
}

void RmlSystemInterface::JoinPath(Rml::String& translated_path, const Rml::String& document_path,
                                  const Rml::String& path) {
    if (path.empty() || document_path.empty() || path[0] == '/' || path[0] == '\\' ||
        (path.size() > 1 && path[1] == ':')) {
        translated_path = path;
        return;
    }
    const auto slash = document_path.find_last_of("/\\");
    if (slash == Rml::String::npos) {
        translated_path = path;
        return;
    }
    const Rml::String joined = document_path.substr(0, slash + 1) + path;
    translated_path = std::filesystem::path(joined).lexically_normal().generic_string();
}

}  // namespace zl::ui
