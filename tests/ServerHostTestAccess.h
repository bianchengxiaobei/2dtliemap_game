#pragma once

#include <string>
#include <utility>

#include "ecs/components/PhysicsComponents.h"
#include "game/VehicleComponents.h"
#include "net/Command.h"
#include "save/SaveManager.h"
#include "server/ServerHost.h"

namespace zl::server {

// Shared test-only access to ServerHost internals for vehicle, command, save
// and fixture setup. ServerHost declares this struct as a friend.
struct ServerHostTestAccess {
    static bool TeleportVehicle(ServerHost& host, std::uint64_t stableId,
                                float x, float y, int z = 0) {
        ServerHost::ServerVehicleRuntime* vehicle =
            host.FindServerVehicle(stableId);
        if (vehicle == nullptr || vehicle->entity == entt::null) {
            return false;
        }
        vehicle->motion.x = x;
        vehicle->motion.y = y;
        vehicle->motion.z = z;
        vehicle->motion.vx = 0.0f;
        vehicle->motion.vy = 0.0f;
        vehicle->motion.crashLatched = false;
        host.SyncServerVehicleToEcs(*vehicle);
        host.SyncServerVehiclePassengers(*vehicle);
        return true;
    }

    static bool SetVehicleVelocity(ServerHost& host, std::uint64_t stableId,
                                   float vx, float vy) {
        ServerHost::ServerVehicleRuntime* vehicle =
            host.FindServerVehicle(stableId);
        if (vehicle == nullptr || vehicle->entity == entt::null) {
            return false;
        }
        vehicle->motion.vx = vx;
        vehicle->motion.vy = vy;
        host.SyncServerVehicleToEcs(*vehicle);
        return true;
    }

    static zl::ecs::Position VehiclePosition(ServerHost& host,
                                             std::uint64_t stableId) {
        const ServerHost::ServerVehicleRuntime* vehicle =
            host.FindServerVehicle(stableId);
        if (vehicle == nullptr) {
            return {};
        }
        return {vehicle->motion.x, vehicle->motion.y, vehicle->motion.z};
    }

    static zl::game::VehicleVisualDirection VehicleVisualDirection(
        ServerHost& host, std::uint64_t stableId) {
        const ServerHost::ServerVehicleRuntime* vehicle =
            host.FindServerVehicle(stableId);
        return vehicle != nullptr ? vehicle->visualDirection
                                  : zl::game::VehicleVisualDirection::South;
    }

    static zl::game::VehicleMotion VehicleMotion(
        ServerHost& host, std::uint64_t stableId) {
        const ServerHost::ServerVehicleRuntime* vehicle =
            host.FindServerVehicle(stableId);
        return vehicle != nullptr ? vehicle->motion : zl::game::VehicleMotion{};
    }

    static bool ExecuteCommand(ServerHost& host,
                               std::uint64_t playerStableId,
                               const zl::net::Command& command) {
        for (auto& [sessionId, session] : host.m_sessions) {
            if (session.playerStableId != playerStableId) {
                continue;
            }
            std::string error;
            return host.ExecuteSessionCommand(sessionId, session, command,
                                              &error);
        }
        return false;
    }

    static void SetActiveSaveId(ServerHost& host, std::string saveId) {
        host.m_activeSaveId = std::move(saveId);
    }

    static bool SaveWorld(ServerHost& host, const char* reason) {
        return host.SaveWorld(reason);
    }

    static void SaveProfiles(ServerHost& host, const char* reason) {
        host.SavePlayerProfiles(reason);
    }

    static void SetSaveWorldOnExit(ServerHost& host, bool value) {
        host.m_saveWorldOnExit = value;
    }

    static bool CreateSave(ServerHost& host, std::string mapId,
                           std::string* outId, std::string* error) {
        if (host.m_saveManager == nullptr) {
            if (error != nullptr) {
                *error = "save manager is not initialized";
            }
            return false;
        }
        save::SaveManager::CreateOptions options;
        options.name = "throw_fixture";
        options.mapId = std::move(mapId);
        options.gameMinutes = 0.0;
        return host.m_saveManager->Create(options, outId, error);
    }
};

}  // namespace zl::server
