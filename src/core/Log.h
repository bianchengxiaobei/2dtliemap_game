#pragma once

#include <string_view>

#include <spdlog/spdlog.h>

// 日志使用规范（docs/02 §5.5）：
//   [时间][级别][模块] 消息；模块名与目录一致（core/render/world/...）。
//   ZL_INFO("render", "loaded {} textures", count);

namespace zl::core {

// 初始化日志：控制台彩色输出 + logs/ 下文件轮转；同时打印版本与构建时间
void InitLogging();

void ShutdownLogging();

// 运行时调整级别（配置 log.level）：trace/debug/info/warn/error/critical
void SetLogLevel(std::string_view level);

// 全局日志器（未初始化时为默认 logger）
spdlog::logger* Log();

}  // namespace zl::core

#if defined(NDEBUG)
#define ZL_DEBUG_BREAK() ((void)0)
#elif defined(_MSC_VER)
#define ZL_DEBUG_BREAK() __debugbreak()
#else
#include <csignal>
#define ZL_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

// 级别宏：module 固定为第一个格式参数，输出 "[模块] 消息"
#define ZL_TRACE(module, fmt, ...) SPDLOG_LOGGER_TRACE(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))
#define ZL_DEBUG(module, fmt, ...) SPDLOG_LOGGER_DEBUG(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))
#define ZL_INFO(module, fmt, ...) SPDLOG_LOGGER_INFO(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))
#define ZL_WARN(module, fmt, ...) SPDLOG_LOGGER_WARN(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))
#define ZL_ERROR(module, fmt, ...) SPDLOG_LOGGER_ERROR(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))
#define ZL_CRITICAL(module, fmt, ...) SPDLOG_LOGGER_CRITICAL(::zl::core::Log(), "[{}] " fmt, module __VA_OPT__(, __VA_ARGS__))

// 断言：Debug 下中断，Release 下记 critical 日志（docs/02 §5.2）
// 用法：ZL_ASSERT(ptr != nullptr);  ZL_ASSERT(x > 0, "x 不能为负: {}", x);
#define ZL_ASSERT(cond, ...)                                                                        \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            SPDLOG_LOGGER_CRITICAL(::zl::core::Log(),                                               \
                                   "断言失败 [{}]: `{}`" __VA_OPT__(" | " __VA_ARGS__), "core",     \
                                   #cond);                                                          \
            ZL_DEBUG_BREAK();                                                                       \
        }                                                                                           \
    } while (0)
