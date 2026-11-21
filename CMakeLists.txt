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
    add_compile_options(/utf-8 /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /bigobj)
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
    add_library(ZL_PYTHON_EMBED INTERFACE)
    set(ZL_PYTHON_EMBED_LIBRARY "${Python3_LIBRARY_RELEASE}")
    if (NOT ZL_PYTHON_EMBED_LIBRARY)
        set(ZL_PYTHON_EMBED_LIBRARY "${Python3_LIBRARIES}")
    endif()
    target_link_libraries(ZL_PYTHON_EMBED INTERFACE
        pybind11::headers
        ${ZL_PYTHON_EMBED_LIBRARY}
    )
    target_include_directories(ZL_PYTHON_EMBED INTERFACE ${Python3_INCLUDE_DIRS})
    set(ZL_PYTHON_HOME_DEFAULT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3")
    message(STATUS "ZL Python home: ${ZL_PYTHON_HOME_DEFAULT}")
endif()

set(ZL_CORE_SOURCES
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
    src/ai/systems/ZombieSpecialSystem.cpp
    src/audio/AudioEngine.cpp
    src/audio/SoundManager.cpp
    src/characters/systems/ActionPresentationSystem.cpp
    src/characters/systems/AnimationSystem.cpp
    src/characters/systems/CombatSystem.cpp
    src/characters/systems/CombatRules.cpp
    src/characters/systems/StealthRules.cpp
    src/characters/systems/WoundSystem.cpp
    src/characters/systems/PainSystem.cpp
    src/characters/systems/InfectionSystem.cpp
    src/characters/systems/DeathSystem.cpp
    src/characters/systems/InputSystem.cpp
    src/characters/systems/MovementSystem.cpp
    src/characters/systems/NeedsSystem.cpp
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
    src/game/Animals.cpp
    src/game/AnimalSystem.cpp
    src/game/AnimalHarvest.cpp
    src/game/AssetManifest.cpp
    src/game/BuildingRules.cpp
    src/game/Climate.cpp
    src/game/Companion.cpp
    src/game/CompanionInventory.cpp
    src/game/CompanionSystem.cpp
    src/game/CompanionTrust.cpp
    src/game/CompanionUi.cpp
    src/game/ContentHash.cpp
    src/game/ContentReload.cpp
    src/game/Dialogue.cpp
    src/game/Crops.cpp
    src/game/Fire.cpp
    src/game/FireSystem.cpp
    src/game/ModContent.cpp
    src/game/EmbeddedServer.cpp
    src/game/Forage.cpp
    src/game/ForageRuntime.cpp
    src/game/FishingRuntime.cpp
    src/game/FoodSafety.cpp
    src/game/EntityReplication.cpp
    src/game/GroundItems.cpp
    src/game/LootContainers.cpp
    src/game/Missions.cpp
    src/game/Homestead.cpp
    src/game/PlayerCommand.cpp
    src/game/PlayerStorage.cpp
    src/game/Poi.cpp
    src/game/Sleeping.cpp
    src/game/Settlement.cpp
    src/game/SettlementNpc.cpp
    src/game/SettlementReputation.cpp
    src/game/StealthConfig.cpp
    src/game/Throwing.cpp
    src/game/TrapSystem.cpp
    src/game/Traps.cpp
    src/game/StrategicState.cpp
    src/game/StrategicPersistence.cpp
    src/game/StrategicDirector.cpp
    src/game/StrategicMap.cpp
    src/game/RegionalSiege.cpp
    src/game/NetworkClient.cpp
    src/game/PlayerCombat.cpp
    src/game/PlayerInventoryService.cpp
    src/game/ProgressionRules.cpp
    src/game/RandomEvents.cpp
    src/game/RegionEventDirector.cpp
    src/game/WorldMap.cpp
    src/game/ServerList.cpp
    src/game/Weather.cpp
    src/game/VehicleMods.cpp
    src/game/WorldEvent.cpp
    src/game/WorldEventDirector.cpp
    src/game/WorldEventRuntime.cpp
    src/game/WorldSimulation.cpp
    src/game/ZombieFactory.cpp
    src/items/Crafting.cpp
    src/items/ItemDef.cpp
    src/items/ItemRegistry.cpp
    src/items/InventoryOps.cpp
    src/items/LootTable.cpp
    src/items/RecipeRegistry.cpp
    src/items/Repair.cpp
    src/items/RangedWeapon.cpp
    src/items/Spoilage.cpp
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
    src/net/StrategicProtocol.cpp
    src/net/WorldEventProtocol.cpp
    src/net/WorldSound.cpp
    src/net/Replication.cpp
    src/platform/KeyNames.cpp
    src/server/PlayerProfile.cpp
    src/server/ServerHost.cpp
    src/physics/PhysicsSyncSystem.cpp
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
    src/save/RegionEntityBuckets.cpp
    src/save/SaveManager.cpp
    src/save/WorldManifest.cpp
    src/save/SaveService.cpp
    src/save/SqliteMeta.cpp
    src/save/Thumbnail.cpp
    src/ui/InventoryPanel.cpp
    src/ui/MenuPanel.cpp
    src/ui/MultiplayerPanel.cpp
    src/ui/SocialPanel.cpp
    src/ui/VehicleModsPanel.cpp
    src/ui/ScriptUiHost.cpp
    src/ui/UiInput.cpp
    src/ui/WorldEventUi.cpp
    src/ui/WorldMapPanel.cpp
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
    src/world/WorldDefinition.cpp
    src/world/WorldGraph.cpp
    src/world/WorldSelection.cpp
    src/world/RegionStreaming.cpp
    src/world/World.cpp
)

add_library(ZomboidLikeCore STATIC ${ZL_CORE_SOURCES})

target_include_directories(ZomboidLikeCore PUBLIC
    src
    ${BULLET_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
    ${MINIAUDIO_INCLUDE_DIR}
    ${LUA_INCLUDE_DIR}
)

target_compile_definitions(ZomboidLikeCore PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_link_libraries(ZomboidLikeCore PUBLIC
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
)

if (WIN32)
    target_link_libraries(ZomboidLikeCore PUBLIC psapi ws2_32)
endif()

if (ZL_ENABLE_PYTHON)
    target_compile_definitions(ZomboidLikeCore PUBLIC ZL_ENABLE_PYTHON=1
        ZL_PYTHON_HOME_DEFAULT="${ZL_PYTHON_HOME_DEFAULT}")
    target_link_libraries(ZomboidLikeCore PUBLIC ZL_PYTHON_EMBED)
endif()

target_precompile_headers(ZomboidLikeCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ZomboidLikePch.h
)

add_executable(ZomboidLike
    src/main.cpp
    src/core/AppState.cpp
    src/game/AssetProvider.cpp
    src/game/EventProbe.cpp
    src/game/GameApp.cpp
    src/game/RenderCollectSystem.cpp
    src/game/states/MainMenuState.cpp
    src/game/states/RenderDemoState.cpp
    src/game/states/SandboxState.cpp
    src/game/states/WorldDemoState.cpp
    src/platform/Window.cpp
    src/render/Atlas.cpp
    src/render/Renderer.cpp
    src/render/SpriteBatch.cpp
    src/render/Texture2D.cpp
    src/ui/RmlBgfxRenderInterface.cpp
    src/ui/RmlSystemInterface.cpp
    src/ui/UiSystem.cpp
)

target_compile_definitions(ZomboidLike PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_link_libraries(ZomboidLike PRIVATE ZomboidLikeCore)

target_precompile_headers(ZomboidLike PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ZomboidLikePch.h
)

# ---- 单元测试（docs/02 §8：坐标换算等必须先有测试） ----
enable_testing()

add_executable(ZomboidLikeTests
    tests/test_main.cpp
    tests/test_isomath.cpp
    tests/test_maploader.cpp
    tests/test_world_definition.cpp
    tests/test_chunkmanager.cpp
    tests/test_ecs.cpp
    tests/test_spatialindex.cpp
    tests/test_physics.cpp
    tests/test_player_control.cpp
    tests/test_needs.cpp
    tests/test_animation_v2.cpp
    tests/test_camera_follow.cpp
    tests/test_vehicle.cpp
    tests/test_vehicle_mods.cpp
    tests/test_vehicle_mods_persistence.cpp
    tests/test_vehicle_mod_effects.cpp
    tests/test_vehicle_mod_balance.cpp
    tests/test_vehicle_mod_network.cpp
    tests/test_pathfinding.cpp
    tests/test_death.cpp
    tests/test_zombie_ai.cpp
    tests/test_perception.cpp
    tests/test_boids.cpp
    tests/test_combat.cpp
    tests/test_combat_rules.cpp
    tests/test_stealth_rules.cpp
    tests/test_combat_m30.cpp
    tests/test_combat_presentation.cpp
    tests/test_player_combat.cpp
    tests/test_animals.cpp
    tests/test_animal_spawn.cpp
    tests/test_animal_harvest.cpp
    tests/test_spawn_lod.cpp
    tests/test_items.cpp
    tests/test_traps.cpp
    tests/test_trap_persistence.cpp
    tests/test_trap_ui.cpp
    tests/test_throwing.cpp
    tests/test_throw_aim.cpp
    tests/test_throw_flight.cpp
    tests/test_throw_fire.cpp
    tests/test_throw_network.cpp
    tests/test_throw_regression.cpp
    tests/test_inventory.cpp
    tests/test_ranged_weapon.cpp
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
    tests/test_mod_content.cpp
    tests/test_content_reload.cpp
    tests/test_mod_assets.cpp
    tests/test_content_hash.cpp
    tests/test_crops.cpp
    tests/test_hot_reload.cpp
    tests/test_python_backend.cpp
    tests/test_python_runtime.cpp
    tests/test_python_api.cpp
    tests/test_ui.cpp
    tests/test_script_ui.cpp
    tests/test_example_gameplay.cpp
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
    tests/test_trap_network.cpp
    tests/test_network_persistence.cpp
    tests/test_network_world_persistence.cpp
    tests/test_m33_f5_t7.cpp
    tests/test_embedded_server.cpp
    tests/test_special_zombies.cpp
    tests/test_armor.cpp
    tests/test_infection.cpp
    tests/test_weather.cpp
    tests/test_progression.cpp
    tests/test_poi.cpp
    tests/test_settlements.cpp
    tests/test_settlement_npc.cpp
    tests/test_settlement_defense.cpp
    tests/test_settlement_network.cpp
    tests/test_settlement_integration.cpp
    tests/test_companions.cpp
    tests/test_companion_ai.cpp
    tests/test_companion_state.cpp
    tests/test_companion_trust.cpp
    tests/test_companion_balance.cpp
    tests/test_companion_ui.cpp
    tests/test_companion_network.cpp
    tests/test_companion_persistence.cpp
    tests/test_world_events.cpp
    tests/test_world_event_director.cpp
    tests/test_world_event_runtime.cpp
    tests/test_dialogue.cpp
    tests/test_forage.cpp
    tests/test_fire.cpp
    tests/test_missions.cpp
    tests/test_random_events.cpp
    tests/test_storage.cpp
    tests/test_food.cpp
    tests/test_sleep.cpp
    tests/test_building.cpp
    tests/test_base_siege.cpp
    tests/test_base_hud.cpp
    tests/test_homestead.cpp
    tests/test_world_generator.cpp
    tests/test_region_streaming.cpp
    tests/test_world_manifest.cpp
    tests/test_world_graph.cpp
    tests/test_world_map.cpp
    tests/test_world_event_ui.cpp
    tests/test_world_event_network.cpp
    tests/test_region_events.cpp
    tests/test_region_network.cpp
    tests/test_strategic_state.cpp
    tests/test_strategic_map.cpp
    tests/test_strategic_network.cpp
    tests/test_regional_siege.cpp
    tests/test_regional_siege_integration.cpp
    tests/test_m31_stress.cpp
    tests/test_m31_vertical_slice.cpp
)

target_compile_definitions(ZomboidLikeTests PRIVATE
    ZL_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(ZomboidLikeTests PRIVATE
    ZomboidLikeCore
    doctest::doctest
)

target_precompile_headers(ZomboidLikeTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ZomboidLikePch.h
)

set(ZL_FAST_TEST_SOURCES
    tests/test_main.cpp
    tests/test_throwing.cpp
    tests/test_throw_aim.cpp
    tests/test_throw_flight.cpp
    tests/test_throw_fire.cpp
    tests/test_throw_network.cpp
    tests/test_throw_regression.cpp
)

add_executable(ZomboidLikeTestsFast EXCLUDE_FROM_ALL ${ZL_FAST_TEST_SOURCES})

target_compile_definitions(ZomboidLikeTestsFast PRIVATE
    ZL_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(ZomboidLikeTestsFast PRIVATE
    ZomboidLikeCore
    doctest::doctest
)

target_precompile_headers(ZomboidLikeTestsFast PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ZomboidLikePch.h
)

if (ZL_ENABLE_PYTHON)
    # vcpkg 的 python3 运行时 DLL 只放在 tools/python3（不在 bin 目录，applocal 不会拷）；
    # 开发构建把它复制到 exe 旁边，保证 OS 能加载 python3xx.dll。
    file(GLOB ZL_PYTHON_RUNTIME_DLLS "${ZL_PYTHON_HOME_DEFAULT}/python*.dll")
    if (EXISTS "${ZL_PYTHON_HOME_DEFAULT}/z.dll")  # python314.dll 的 zlib 依赖（vcpkg 自带）
        list(APPEND ZL_PYTHON_RUNTIME_DLLS "${ZL_PYTHON_HOME_DEFAULT}/z.dll")
    endif()
    foreach(zl_target ZomboidLike ZomboidLikeTests ZomboidLikeTestsFast)
        add_custom_command(TARGET ${zl_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZL_PYTHON_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:${zl_target}>)
    endforeach()
endif()

add_test(NAME ZomboidLikeTests COMMAND ZomboidLikeTests)
