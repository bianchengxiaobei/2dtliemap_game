cmake_minimum_required(VERSION 3.24)

# Python 脚本后端开关（M8）；必须在 project() 之前设置，供 vcpkg manifest 特性选择
option(ZL_ENABLE_PYTHON "启用 Python 脚本后端（M8，默认关闭）" OFF)
if (ZL_ENABLE_PYTHON)
    set(VCPKG_MANIFEST_FEATURES "python")
endif()

project(ZomboidLike VERSION 0.2.0 LANGUAGES CXX)

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
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(doctest CONFIG REQUIRED)

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
    src/game/AssetProvider.cpp
    src/game/EventProbe.cpp
    src/game/GameApp.cpp
    src/game/states/RenderDemoState.cpp
    src/game/states/SandboxState.cpp
    src/game/states/WorldDemoState.cpp
    src/platform/KeyNames.cpp
    src/platform/Window.cpp
    src/render/Atlas.cpp
    src/render/Camera.cpp
    src/render/IsoMath.cpp
    src/render/Renderer.cpp
    src/render/SpriteBatch.cpp
    src/render/Texture2D.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
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
    glm::glm
)

target_include_directories(ZomboidLike PRIVATE ${Stb_INCLUDE_DIR})

# ---- 单元测试（docs/02 §8：坐标换算等必须先有测试） ----
enable_testing()

add_executable(ZomboidLikeTests
    tests/test_main.cpp
    tests/test_isomath.cpp
    tests/test_maploader.cpp
    tests/test_chunkmanager.cpp
    tests/test_world.cpp
    src/core/Log.cpp
    src/core/TaskScheduler.cpp
    src/render/IsoMath.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
)

target_include_directories(ZomboidLikeTests PRIVATE src)

# 测试需要访问仓库内资源（如 assets/maps/village_demo）
target_compile_definitions(ZomboidLikeTests PRIVATE ZL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(ZomboidLikeTests PRIVATE
    doctest::doctest
    glm::glm
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

add_test(NAME ZomboidLikeTests COMMAND ZomboidLikeTests)
