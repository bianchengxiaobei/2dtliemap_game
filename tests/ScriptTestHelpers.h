#pragma once

// M7 脚本测试共用夹具：临时 mod 根目录 + 日志捕获。
// （供 test_lua_runtime.cpp / test_event_bridge.cpp 等复用）

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/Log.h"
#include "script/IScriptRuntime.h"

namespace zltest {

// 测试进程独立的临时 mod 根目录（自动清理）
class TempModRoot {
public:
    TempModRoot() {
        std::random_device rd;
        root = std::filesystem::temp_directory_path() /
               ("zl_lua_test_" + std::to_string(rd()));
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
    }
    ~TempModRoot() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    zl::script::ModInfo MakeMod(const std::string& id, const std::string& source) {
        WriteLua(id, source);
        zl::script::ModInfo mod;
        mod.id = id;
        mod.name = id;
        mod.version = "1.0.0";
        mod.backend = "lua";
        mod.rootPath = (root / id).string();
        mod.mainScript = "scripts/main.lua";
        return mod;
    }

    void WriteLua(const std::string& id, const std::string& source) {
        const std::filesystem::path dir = root / id / "scripts";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream out(dir / "main.lua", std::ios::binary | std::ios::trunc);
        out << source;
    }

    std::filesystem::path root;
};

// 捕获游戏日志输出（验证 print/Log 重定向与脚本错误日志）
class LogCapture {
public:
    LogCapture() : m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_stream)) {
        zl::core::Log()->sinks().push_back(m_sink);
    }
    ~LogCapture() {
        auto& sinks = zl::core::Log()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    std::string Str() const { return m_stream.str(); }

private:
    std::ostringstream m_stream;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
};

}  // namespace zltest
