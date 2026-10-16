#include "core/Log.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>
#include <memory>
#include <vector>

namespace zl::core {
namespace {

constexpr const char* kVersion = "0.1.0";
constexpr const char* kLogDir = "logs";
constexpr const char* kLogFile = "logs/zomboidlike.log";
constexpr std::size_t kLogMaxBytes = 5 * 1024 * 1024;
constexpr std::size_t kLogMaxFiles = 3;

std::shared_ptr<spdlog::logger> s_logger;

}  // namespace

void InitLogging() {
    if (s_logger) {
        return;
    }

#ifdef _WIN32
    // 控制台切到 UTF-8 码页，保证中文日志不乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::error_code ec;
    std::filesystem::create_directories(kLogDir, ec);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%H:%M:%S][%^%l%$] %v");

    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        kLogFile, kLogMaxBytes, kLogMaxFiles, false);
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");

    s_logger = std::make_shared<spdlog::logger>(
        "zl", spdlog::sinks_init_list{consoleSink, fileSink});
#ifdef NDEBUG
    s_logger->set_level(spdlog::level::info);
    s_logger->flush_on(spdlog::level::warn);
#else
    // Debug 构建每次 debug 级日志即落盘：开发期异常退出也能看到日志
    s_logger->set_level(spdlog::level::trace);
    s_logger->flush_on(spdlog::level::debug);
#endif

    ZL_INFO("core", "ZomboidLike v{} | 构建时间 {} {}", kVersion, __DATE__, __TIME__);
}

void ShutdownLogging() {
    if (s_logger) {
        s_logger->flush();
        s_logger.reset();
    }
    spdlog::shutdown();
}

void SetLogLevel(std::string_view level) {
    const auto lvl = spdlog::level::from_str(std::string(level));
    if (s_logger) {
        s_logger->set_level(lvl);
    }
    spdlog::set_level(lvl);
}

spdlog::logger* Log() {
    if (s_logger) {
        return s_logger.get();
    }
    return spdlog::default_logger_raw();
}

}  // namespace zl::core
