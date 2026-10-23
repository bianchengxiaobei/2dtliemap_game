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
find_package(RmlUi CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(kcp CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)

if (ZL_ENABLE_PYTHON)
    # Python 脚本后端依赖（M8-T1）：嵌入式 CPython + pybind11。
    # 说明（M8-T2）：Debug 构建同样链接 release 版 python3xx.lib——vcpkg 的 python3
    # debug 树只有 _d.lib，没有对应的运行时/标准库；统一用 release CPython（含标准库，
    # 位于 <vcpkg_installed>/<triplet>/tools/python3）。
    find_package(Python3 COMPONENTS Development.Embed REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)
    set_target_properties(Python3::Python PROPERTIES MAP_IMPORTED_CONFIG_DEBUG RELEASE)
    set(ZL_PYTHON_HOME_DEFAULT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3")
    message(STATUS "ZL Python home: ${ZL_PYTHON_HOME_DEFAULT}")
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
    src/audio/AudioEngine.cpp
    src/audio/SoundManager.cpp
    src/characters/systems/ActionPresentationSystem.cpp
    src/characters/systems/AnimationSystem.cpp
    src/characters/systems/CombatSystem.cpp
    src/characters/systems/DeathSystem.cpp
    src/characters/systems/InputSystem.cpp
    src/characters/systems/MovementSystem.cpp
    src/characters/systems/NeedsSystem.cpp
    src/core/AppState.cpp
    src/core/Config.cpp
    src/core/EventBus.cpp
    src/core/GameLoop.cpp
    src/core/GameTime.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/SoundPropagation.cpp
    src/core/TaskScheduler.cpp
    src/ecs/EcsWorld.cpp
    src/ecs/SpatialIndex.cpp
    src/ecs/SystemScheduler.cpp
    src/game/AnimationManifest.cpp
    src/game/AssetProvider.cpp
    src/game/EmbeddedServer.cpp
    src/game/EventProbe.cpp
    src/game/EntityReplication.cpp
    src/game/GameApp.cpp
    src/game/GroundItems.cpp
    src/game/LootContainers.cpp
    src/game/NetworkClient.cpp
    src/game/PlayerInventoryService.cpp
    src/game/ServerList.cpp
    src/game/RenderCollectSystem.cpp
    src/game/WorldSimulation.cpp
    src/game/ZombieFactory.cpp
    src/game/states/MainMenuState.cpp
    src/game/states/RenderDemoState.cpp
    src/game/states/SandboxState.cpp
    src/game/states/WorldDemoState.cpp
    src/items/Crafting.cpp
    src/items/ItemDef.cpp
    src/items/ItemRegistry.cpp
    src/items/InventoryOps.cpp
    src/items/LootTable.cpp
    src/items/RecipeRegistry.cpp
    src/items/Repair.cpp
    src/net/Command.cpp
    src/net/CommandCodec.cpp
    src/net/CommandPipeline.cpp
    src/net/Loopback.cpp
    src/net/FrameCodec.cpp
    src/net/Kcp.cpp
    src/net/SessionProtocol.cpp
    src/net/KcpSession.cpp
    src/net/PlayerAssignment.cpp
    src/net/PlayerIdentity.cpp
    src/net/PlayerState.cpp
    src/net/SocialProtocol.cpp
    src/net/Replication.cpp
    src/platform/KeyNames.cpp
    src/platform/Window.cpp
    src/server/PlayerProfile.cpp
    src/server/ServerHost.cpp
    src/physics/PhysicsSyncSystem.cpp
    src/physics/PhysicsWorld.cpp
    src/render/Atlas.cpp
    src/render/Camera.cpp
    src/render/IsoMath.cpp
    src/render/Renderer.cpp
    src/render/Screenshot.cpp
    src/render/SpriteBatch.cpp
    src/render/Texture2D.cpp
    src/save/AtomicFile.cpp
    src/save/BinaryReader.cpp
    src/save/BinaryWriter.cpp
    src/save/ChunkDiff.cpp
    src/save/ChunkDiffStore.cpp
    src/save/ComponentRegistry.cpp
    src/save/EntityStore.cpp
    src/save/SaveManager.cpp
    src/save/SaveService.cpp
    src/save/SqliteMeta.cpp
    src/save/Thumbnail.cpp
    src/ui/InventoryPanel.cpp
    src/ui/MenuPanel.cpp
    src/ui/MultiplayerPanel.cpp
    src/ui/SocialPanel.cpp
    src/ui/RmlBgfxRenderInterface.cpp
    src/ui/RmlSystemInterface.cpp
    src/ui/ScriptUiHost.cpp
    src/ui/UiInput.cpp
    src/ui/UiSystem.cpp
    src/script/EventBridge.cpp
    src/script/LuaRuntime.cpp
    src/script/ModLoader.cpp
    src/script/ModWatcher.cpp
    src/script/PythonRuntime.cpp
    src/script/PythonSupport.cpp
    src/script/ScriptApi.cpp
    src/script/ScriptManager.cpp
    src/script/ScriptUiApi.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
)

target_include_directories(ZomboidLike PRIVATE src ${BULLET_INCLUDE_DIRS} ${MINIAUDIO_INCLUDE_DIR})

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
    RmlUi::RmlUi
    unofficial::sqlite3::sqlite3
    kcp::kcp
    ws2_32
)

if (WIN32)
    target_link_libraries(ZomboidLike PRIVATE psapi)  # 工作集内存采样（M3-T7 压测）
endif()

if (ZL_ENABLE_PYTHON)
    target_compile_definitions(ZomboidLike PRIVATE ZL_ENABLE_PYTHON=1
        ZL_PYTHON_HOME_DEFAULT="${ZL_PYTHON_HOME_DEFAULT}")
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
    tests/test_needs.cpp
    tests/test_animation_v2.cpp
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
    tests/test_recipes.cpp
    tests/test_ground_items.cpp
    tests/test_loot.cpp
    tests/test_objective.cpp
    tests/test_zombie_variants.cpp
    tests/test_script.cpp
    tests/test_lua_runtime.cpp
    tests/test_script_api.cpp
    tests/test_event_bridge.cpp
    tests/test_mod_loader.cpp
    tests/test_hot_reload.cpp
    tests/test_python_backend.cpp
    tests/test_python_runtime.cpp
    tests/test_python_api.cpp
    tests/test_ui.cpp
    tests/test_script_ui.cpp
    tests/test_config.cpp
    tests/test_serialization.cpp
    tests/test_chunk_diff.cpp
    tests/test_entity_save.cpp
    tests/test_save_manager.cpp
    tests/test_save_service.cpp
    tests/test_audio.cpp
    tests/test_screenshot.cpp
    tests/test_world.cpp
    tests/test_net.cpp
    tests/test_kcp.cpp
    tests/test_kcp_session.cpp
    tests/test_server_host.cpp
    tests/test_player_profile.cpp
    tests/test_server_security.cpp
    tests/test_replication.cpp
    tests/test_network_integration.cpp
    tests/test_player_state.cpp
    tests/test_social_protocol.cpp
    tests/test_server_list.cpp
    tests/test_server_social.cpp
    tests/test_inventory_service.cpp
    tests/test_network_inventory.cpp
    tests/test_network_persistence.cpp
    tests/test_embedded_server.cpp
    src/core/Config.cpp
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
    src/audio/AudioEngine.cpp
    src/audio/SoundManager.cpp
    src/characters/systems/ActionPresentationSystem.cpp
    src/characters/systems/AnimationSystem.cpp
    src/characters/systems/CombatSystem.cpp
    src/characters/systems/DeathSystem.cpp
    src/characters/systems/InputSystem.cpp
    src/characters/systems/MovementSystem.cpp
    src/characters/systems/NeedsSystem.cpp
    src/core/EventBus.cpp
    src/core/GameLoop.cpp
    src/core/GameTime.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/SoundPropagation.cpp
    src/core/TaskScheduler.cpp
    src/ecs/EcsWorld.cpp
    src/ecs/SpatialIndex.cpp
    src/ecs/SystemScheduler.cpp
    src/game/AnimationManifest.cpp
    src/game/EntityReplication.cpp
    src/game/GroundItems.cpp
    src/game/LootContainers.cpp
    src/game/EmbeddedServer.cpp
    src/game/NetworkClient.cpp
    src/game/PlayerInventoryService.cpp
    src/game/ServerList.cpp
    src/game/WorldSimulation.cpp
    src/game/ZombieFactory.cpp
    src/items/Crafting.cpp
    src/items/ItemDef.cpp
    src/items/ItemRegistry.cpp
    src/items/InventoryOps.cpp
    src/items/LootTable.cpp
    src/items/RecipeRegistry.cpp
    src/items/Repair.cpp
    src/net/Command.cpp
    src/net/CommandCodec.cpp
    src/net/CommandPipeline.cpp
    src/net/Loopback.cpp
    src/net/FrameCodec.cpp
    src/net/Kcp.cpp
    src/net/SessionProtocol.cpp
    src/net/KcpSession.cpp
    src/net/PlayerAssignment.cpp
    src/net/PlayerIdentity.cpp
    src/net/PlayerState.cpp
    src/net/SocialProtocol.cpp
    src/net/Replication.cpp
    src/platform/KeyNames.cpp
    src/physics/PhysicsSyncSystem.cpp
    src/server/PlayerProfile.cpp
    src/server/ServerHost.cpp
    src/physics/PhysicsWorld.cpp
    src/render/Camera.cpp
    src/render/IsoMath.cpp
    src/render/Screenshot.cpp
    src/save/AtomicFile.cpp
    src/save/BinaryReader.cpp
    src/save/BinaryWriter.cpp
    src/save/ChunkDiff.cpp
    src/save/ChunkDiffStore.cpp
    src/save/ComponentRegistry.cpp
    src/save/EntityStore.cpp
    src/save/SaveManager.cpp
    src/save/SaveService.cpp
    src/save/SqliteMeta.cpp
    src/save/Thumbnail.cpp
    src/ui/InventoryPanel.cpp
    src/ui/MenuPanel.cpp
    src/ui/MultiplayerPanel.cpp
    src/ui/SocialPanel.cpp
    src/ui/ScriptUiHost.cpp
    src/ui/UiInput.cpp
    src/script/EventBridge.cpp
    src/script/LuaRuntime.cpp
    src/script/ModLoader.cpp
    src/script/ModWatcher.cpp
    src/script/PythonRuntime.cpp
    src/script/PythonSupport.cpp
    src/script/ScriptApi.cpp
    src/script/ScriptManager.cpp
    src/script/ScriptUiApi.cpp
    src/world/Cell.cpp
    src/world/Chunk.cpp
    src/world/ChunkData.cpp
    src/world/ChunkManager.cpp
    src/world/GridSquare.cpp
    src/world/MapLoader.cpp
    src/world/World.cpp
)

target_include_directories(ZomboidLikeTests PRIVATE src ${BULLET_INCLUDE_DIRS} ${Stb_INCLUDE_DIR} ${MINIAUDIO_INCLUDE_DIR})

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
    RmlUi::RmlUi
    unofficial::sqlite3::sqlite3
    kcp::kcp
    ws2_32
    glfw
)

if (ZL_ENABLE_PYTHON)
    target_compile_definitions(ZomboidLikeTests PRIVATE ZL_ENABLE_PYTHON=1
        ZL_PYTHON_HOME_DEFAULT="${ZL_PYTHON_HOME_DEFAULT}")
    target_link_libraries(ZomboidLikeTests PRIVATE pybind11::embed)
endif()

if (ZL_ENABLE_PYTHON)
    # vcpkg 的 python3 运行时 DLL 只放在 tools/python3（不在 bin 目录，applocal 不会拷）；
    # 开发构建把它复制到 exe 旁边，保证 OS 能加载 python3xx.dll。
    file(GLOB ZL_PYTHON_RUNTIME_DLLS "${ZL_PYTHON_HOME_DEFAULT}/python*.dll")
    if (EXISTS "${ZL_PYTHON_HOME_DEFAULT}/z.dll")  # python314.dll 的 zlib 依赖（vcpkg 自带）
        list(APPEND ZL_PYTHON_RUNTIME_DLLS "${ZL_PYTHON_HOME_DEFAULT}/z.dll")
    endif()
    foreach(zl_target ZomboidLike ZomboidLikeTests)
        add_custom_command(TARGET ${zl_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZL_PYTHON_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:${zl_target}>)
    endforeach()
endif()

add_test(NAME ZomboidLikeTests COMMAND ZomboidLikeTests)
