#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "net/PlayerState.h"
#include "save/EntityStore.h"

namespace zl::server {

// M13-T8：跨连接/跨服务器重启的玩家档案。
//
// identity 是客户端持久身份；stableId 由服务器档案分配并永久复用。
// entity 保存完整玩家实体快照（位置/生命/背包/装备/配方/制作），
// state 用于玩家离线/死亡期间继续向客户端推送只读状态。
struct PlayerProfile {
    std::string identity;
    std::string name;
    std::uint64_t stableId = 0;
    bool alive = true;
    float respawnSeconds = 0.0f;
    bool hasSnapshot = false;
    save::SavedEntity entity;
    net::PlayerState state;
};

class PlayerProfileStore {
public:
    struct Options {
        std::filesystem::path path;
    };

    explicit PlayerProfileStore(Options options);

    const std::filesystem::path& Path() const { return m_path; }
    std::size_t Size() const { return m_profiles.size(); }

    bool Load(std::string* error = nullptr);
    bool Save(std::string* error = nullptr);

    PlayerProfile* Find(std::string_view identity);
    const PlayerProfile* Find(std::string_view identity) const;

    // 新身份分配高位 StableId；调用方负责填写 name/state 初始值。
    PlayerProfile& Create(std::string identity, std::string name);
    std::uint64_t AllocateStableId();

    const std::map<std::string, PlayerProfile>& Profiles() const {
        return m_profiles;
    }

private:
    std::filesystem::path m_path;
    std::uint64_t m_nextStableId = 0;
    std::map<std::string, PlayerProfile> m_profiles;
};

}  // namespace zl::server
