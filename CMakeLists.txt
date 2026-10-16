cmake_minimum_required(VERSION 3.24)

# Python 脚本后端开关（M8）；必须在 project() 之前设置，供 vcpkg manifest 特性选择
option(ZL_ENABLE_PYTHON "启用 Python 脚本后端（M8，默认关闭）" OFF)
if (ZL_ENABLE_PYTHON)
    set(VCPKG_MANIFEST_FEATURES "python")
endif()

project(ZomboidLike VERSION 0.3.0 LANGUAGES CXX)

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
find_package(EnTT CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

if (ZL_ENABLE_PYTHON)
    # Python 脚本后端依赖（M8-T1）：嵌入式 CPython + pybind11
    find_package(Python3 COMPONENTS Development.Embed REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)
endif()

add_executable(ZomboidLike
    src/main.cpp
    src/ai/AStar.cpp
    src/ai/Passability.cpp
    src/ai/PathfindingService.cpp
    src/ai/VirtualHorde.cpp
    src/ai/systems/AiSystem.cpp
    src/ai/systems/BoidsSystem.cpp
    src/ai/systems/LodSystem.cpp
    src/ai/systems/PathFollowSystem.cpp
    src/ai/systems/PerceptionSystem.cpp
    src/ai/systems/SpawnLodSystem.cpp
    src/characters/systems/AnimationSystem.cpp
    src/characters/systems/CombatSystem.cpp
    src/characters/systems/DeathSystem.cpp
    src/characters/systems/InputSystem.cpp
    src/characters/systems/MovementSystem.cpp
    src/core/AppState.cpp
    src/core/Config.cpp
    src/core/EventBus.cpp
    src/core/GameLoop.cpp
    src/core/GameTime.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/TaskScheduler.cpp
    src/ecs/EcsWorld.cpp
    src/ecs/SpatialIndex.cpp
    src/ecs/SystemScheduler.cpp
    src/game/AssetProvider.cpp
    src/game/EventProbe.cpp
    src/game/GameApp.cpp
    src/game/GroundItems.cpp
    src/game/RenderCollectSystem.cpp
    src/game/ZombieFactory.cpp
    src/game/states/RenderDemoState.cpp
    src/game/states/SandboxState.cpp
    src/game/states/WorldDemoState.cpp
    src/items/ItemDef.cpp
    src/items/ItemRegistry.cpp
    src/items/InventoryOps.cpp
    src/platform/KeyNames.cpp
    src/platform/Window.cpp
    src/physics/PhysicsSyncSystem.cpp
    src/physics/PhysicsWorld.cpp
    src/render/Atlas.cpp
    src/render/Camera.cpp
    src/render/IsoMath.cpp
    src/render/Renderer.cpp
    src/render/SpriteBatch.cpp
    src/render/Texture2D.cpp
    src/script/EventBridge.cpp
    src/script/LuaRuntime.cpp
    src/script/ModLoader.cpp
    src/script/ModWatcher.cpp
    src/script/PythonSupport.cpp
    src/script/ScriptApi.cpp
    src/script/ScriptManager.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
)

target_include_directories(ZomboidLike PRIVATE src ${BULLET_INCLUDE_DIRS})

target_compile_definitions(ZomboidLike PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_link_libraries(ZomboidLike PRIVATE
    glfw
    bgfx::bgfx
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    glm::glm
    EnTT::EnTT
    sol2::sol2
    ${LUA_LIBRARIES}
    ${BULLET_LIBRARIES}
)

if (WIN32)
    target_link_libraries(ZomboidLike PRIVATE psapi)  # 工作集内存采样（M3-T7 压测）
endif()

if (ZL_ENABLE_PYTHON)
    target_compile_definitions(ZomboidLike PRIVATE ZL_ENABLE_PYTHON=1)
    target_link_libraries(ZomboidLike PRIVATE pybind11::embed)
endif()

target_include_directories(ZomboidLike PRIVATE ${Stb_INCLUDE_DIR})

# ---- 单元测试（docs/02 §8：坐标换算等必须先有测试） ----
enable_testing()

add_executable(ZomboidLikeTests
    tests/test_main.cpp
    tests/test_isomath.cpp
    tests/test_maploader.cpp
    tests/test_chunkmanager.cpp
    tests/test_ecs.cpp
    tests/test_spatialindex.cpp
    tests/test_physics.cpp
    tests/test_player_control.cpp
    tests/test_camera_follow.cpp
    tests/test_pathfinding.cpp
    tests/test_death.cpp
    tests/test_zombie_ai.cpp
    tests/test_perception.cpp
    tests/test_boids.cpp
    tests/test_combat.cpp
    tests/test_spawn_lod.cpp
    tests/test_items.cpp
    tests/test_inventory.cpp
    tests/test_ground_items.cpp
    tests/test_script.cpp
    tests/test_lua_runtime.cpp
    tests/test_script_api.cpp
    tests/test_event_bridge.cpp
    tests/test_mod_loader.cpp
    tests/test_hot_reload.cpp
    tests/test_python_backend.cpp
    tests/test_world.cpp
    src/ai/AStar.cpp
    src/ai/Passability.cpp
    src/ai/PathfindingService.cpp
    src/ai/VirtualHorde.cpp
    src/ai/systems/AiSystem.cpp
    src/ai/systems/BoidsSystem.cpp
    src/ai/systems/LodSystem.cpp
    src/ai/systems/PathFollowSystem.cpp
    src/ai/systems/PerceptionSystem.cpp
    src/ai/systems/SpawnLodSystem.cpp
    src/characters/systems/AnimationSystem.cpp
    src/characters/systems/CombatSystem.cpp
    src/characters/systems/DeathSystem.cpp
    src/characters/systems/InputSystem.cpp
    src/characters/systems/MovementSystem.cpp
    src/core/EventBus.cpp
    src/core/GameLoop.cpp
    src/core/GameTime.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/TaskScheduler.cpp
    src/ecs/EcsWorld.cpp
    src/ecs/SpatialIndex.cpp
    src/ecs/SystemScheduler.cpp
    src/game/GroundItems.cpp
    src/game/ZombieFactory.cpp
    src/items/ItemDef.cpp
    src/items/ItemRegistry.cpp
    src/items/InventoryOps.cpp
    src/physics/PhysicsSyncSystem.cpp
    src/physics/PhysicsWorld.cpp
    src/render/Camera.cpp
    src/render/IsoMath.cpp
    src/script/EventBridge.cpp
    src/script/LuaRuntime.cpp
    src/script/ModLoader.cpp
    src/script/ModWatcher.cpp
    src/script/PythonSupport.cpp
    src/script/ScriptApi.cpp
    src/script/ScriptManager.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
)

target_include_directories(ZomboidLikeTests PRIVATE src ${BULLET_INCLUDE_DIRS})

# 测试需要访问仓库内资源（如 assets/maps/village_demo）
target_compile_definitions(ZomboidLikeTests PRIVATE ZL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(ZomboidLikeTests PRIVATE
    doctest::doctest
    glm::glm
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    sol2::sol2
    ${LUA_LIBRARIES}
    ${BULLET_LIBRARIES}
)

if (ZL_ENABLE_PYTHON)
    target_compile_definitions(ZomboidLikeTests PRIVATE ZL_ENABLE_PYTHON=1)
    target_link_libraries(ZomboidLikeTests PRIVATE pybind11::embed)
endif()

add_test(NAME ZomboidLikeTests COMMAND ZomboidLikeTests)
