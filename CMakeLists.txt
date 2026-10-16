cmake_minimum_required(VERSION 3.24)

# Python 脚本后端开关（M8）；必须在 project() 之前设置，供 vcpkg manifest 特性选择
option(ZL_ENABLE_PYTHON "启用 Python 脚本后端（M8，默认关闭）" OFF)
if (ZL_ENABLE_PYTHON)
    set(VCPKG_MANIFEST_FEATURES "python")
endif()

project(ZomboidLike VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

find_package(glfw3 CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(ZomboidLike
    src/main.cpp
    src/core/AppState.cpp
    src/core/Config.cpp
    src/core/EventBus.cpp
    src/core/GameLoop.cpp
    src/core/GameTime.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/TaskScheduler.cpp
    src/game/EventProbe.cpp
    src/game/GameApp.cpp
    src/game/states/SandboxState.cpp
    src/platform/KeyNames.cpp
    src/platform/Window.cpp
)

target_include_directories(ZomboidLike PRIVATE src)

target_compile_definitions(ZomboidLike PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_link_libraries(ZomboidLike PRIVATE
    glfw
    bgfx::bgfx
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
