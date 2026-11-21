#include "game/GameApp.h"
#include "server/ServerHost.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(buffer.data()).parent_path();
#else
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return executable.parent_path();
#endif
}

void UseProjectRootWorkingDirectory() {
    std::filesystem::path directory = ExecutableDirectory();
    std::error_code error;
    for (int depth = 0; depth < 4 && !directory.empty(); ++depth) {
        if (std::filesystem::exists(directory / "assets" / "asset_manifest.json",
                                    error) &&
            std::filesystem::exists(directory / "config" / "game.json", error)) {
#if defined(_WIN32)
            SetCurrentDirectoryW(directory.c_str());
#else
            std::filesystem::current_path(directory, error);
#endif
            return;
        }
        directory = directory.parent_path();
    }
}

}  // namespace

int main(int argc, char** argv) {
    UseProjectRootWorkingDirectory();
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            return zl::server::ServerHost::Run(argc, argv);
        }
    }
    return zl::game::GameApp::Run(argc, argv);
}
