#include "server/PlayerProfile.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "net/PlayerIdentity.h"
#include "net/SessionProtocol.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::server {
namespace {

constexpr std::uint32_t kProfileMagic = save::MakeMagic('Z', 'L', 'P', 'F');
constexpr std::uint16_t kProfileVersion = 1;
constexpr std::size_t kMaxProfiles = 4096;
constexpr std::size_t kMaxProfileFileBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kFirstProfileStableId =
    ecs::EcsWorld::kExternalStableIdStart + 1ull;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool ReadFile(const std::filesystem::path& path, std::vector<std::uint8_t>* out,
              std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "玩家档案文件读取失败：" + path.string());
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxProfileFileBytes) {
        SetError(error, "玩家档案文件大小非法");
        return false;
    }
    input.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(size), 0);
    if (size > 0 &&
        !input.read(reinterpret_cast<char*>(out->data()),
                    static_cast<std::streamsize>(size))) {
        SetError(error, "玩家档案文件读取中断");
        return false;
    }
    return true;
}

bool WriteFileAtomic(const std::filesystem::path& path,
                     std::span<const std::uint8_t> bytes, std::string* error) {
    std::error_code fsError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), fsError);
    }
    if (fsError) {
        SetError(error, "玩家档案目录创建失败：" + fsError.message());
        return false;
    }

    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()))) {
            SetError(error, "玩家档案写入失败：" + temp.string());
            return false;
        }
        output.flush();
        if (!output) {
            SetError(error, "玩家档案刷盘失败：" + temp.string());
            return false;
        }
    }

    std::filesystem::remove(path, fsError);
    fsError.clear();
    std::filesystem::rename(temp, path, fsError);
    if (fsError) {
        SetError(error, "玩家档案替换失败：" + fsError.message());
        return false;
    }
    return true;
}

bool ReadBytes(save::BinaryReader& reader, std::uint32_t maxSize,
               std::vector<std::uint8_t>* out, std::string* error) {
    std::uint32_t size = 0;
    if (!reader.Read(size) || size > maxSize) {
        SetError(error, "玩家档案字段长度非法");
        return false;
    }
    out->assign(size, 0);
    if (size > 0 && !reader.ReadBytes(out->data(), size)) {
        SetError(error, "玩家档案字段截断");
        return false;
    }
    return true;
}

}  // namespace

PlayerProfileStore::PlayerProfileStore(Options options)
    : m_path(std::move(options.path)),
      m_nextStableId(kFirstProfileStableId) {}

PlayerProfile* PlayerProfileStore::Find(std::string_view identity) {
    const auto it = m_profiles.find(std::string(identity));
    return it == m_profiles.end() ? nullptr : &it->second;
}

const PlayerProfile* PlayerProfileStore::Find(std::string_view identity) const {
    const auto it = m_profiles.find(std::string(identity));
    return it == m_profiles.end() ? nullptr : &it->second;
}

std::uint64_t PlayerProfileStore::AllocateStableId() {
    if (m_nextStableId == 0 ||
        m_nextStableId == std::numeric_limits<std::uint64_t>::max()) {
        return 0;
    }
    const std::uint64_t id = m_nextStableId++;
    if (m_nextStableId == std::numeric_limits<std::uint64_t>::max()) {
        m_nextStableId = 0;
    }
    return id;
}

PlayerProfile& PlayerProfileStore::Create(std::string identity, std::string name) {
    if (const auto found = m_profiles.find(identity); found != m_profiles.end()) {
        return found->second;
    }
    PlayerProfile profile;
    profile.identity = std::move(identity);
    profile.name = std::move(name);
    profile.stableId = AllocateStableId();
    if (profile.name.empty()) {
        profile.name = "Player";
    }
    profile.state.playerStableId = profile.stableId;
    profile.state.capacityKg = 20.0f;
    profile.state.health = 100.0f;
    profile.state.healthMax = 100.0f;
    profile.state.alive = true;
    profile.state.respawnSeconds = 0.0f;
    const auto [it, inserted] = m_profiles.emplace(profile.identity, std::move(profile));
    static_cast<void>(inserted);
    return it->second;
}

bool PlayerProfileStore::Load(std::string* error) {
    m_profiles.clear();
    m_nextStableId = kFirstProfileStableId;
    std::error_code fsError;
    if (m_path.empty() || !std::filesystem::exists(m_path, fsError)) {
        return true;
    }
    if (fsError) {
        SetError(error, "玩家档案路径检查失败：" + fsError.message());
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFile(m_path, &bytes, error)) {
        return false;
    }
    save::BinaryReader reader(bytes);
    save::SegmentHeader header;
    if (!reader.BeginSegment(header, kProfileMagic, kProfileVersion)) {
        SetError(error, "玩家档案头无效：" + reader.Error());
        return false;
    }
    std::uint64_t nextStableId = 0;
    std::uint32_t count = 0;
    if (!reader.Read(nextStableId) || !reader.Read(count) ||
        count > kMaxProfiles) {
        SetError(error, "玩家档案字段非法");
        return false;
    }

    std::map<std::string, PlayerProfile> profiles;
    for (std::uint32_t i = 0; i < count; ++i) {
        PlayerProfile profile;
        std::uint32_t stateSize = 0;
        if (!reader.ReadString(profile.identity, net::kMaxPlayerIdentityBytes) ||
            !reader.ReadString(profile.name, net::kMaxSessionName) ||
            !reader.Read(profile.stableId) || !reader.Read(profile.alive) ||
            !reader.Read(profile.respawnSeconds) || !reader.Read(stateSize)) {
            SetError(error, "玩家档案记录截断");
            return false;
        }
        if (!net::IsValidPlayerIdentity(profile.identity) ||
            profile.stableId < ecs::EcsWorld::kExternalStableIdStart ||
            profile.stableId >= std::numeric_limits<std::uint64_t>::max() ||
            !std::isfinite(profile.respawnSeconds) || profile.respawnSeconds < 0.0f ||
            profile.respawnSeconds > 86'400.0f ||
            profiles.contains(profile.identity)) {
            SetError(error, "玩家档案身份/StableId/复活字段非法");
            return false;
        }
        if (stateSize == 0 || stateSize > net::kMaxPlayerStateBytes) {
            SetError(error, "玩家档案状态长度非法");
            return false;
        }
        std::vector<std::uint8_t> stateBytes(stateSize, 0);
        if (!reader.ReadBytes(stateBytes.data(), stateBytes.size())) {
            SetError(error, "玩家档案状态截断");
            return false;
        }
        std::string stateError;
        if (!net::DecodePlayerState(stateBytes, &profile.state, &stateError) ||
            profile.state.playerStableId != profile.stableId) {
            SetError(error, "玩家档案状态无效：" + stateError);
            return false;
        }
        if (profile.alive != profile.state.alive ||
            (!profile.state.alive && profile.state.health > 0.0f)) {
            SetError(error, "玩家死亡/存活状态非法");
            return false;
        }
        std::uint8_t hasSnapshot = 0;
        if (!reader.Read(hasSnapshot) || hasSnapshot > 1) {
            SetError(error, "玩家档案快照标记非法");
            return false;
        }
        profile.hasSnapshot = hasSnapshot != 0;
        if (profile.hasSnapshot) {
            std::vector<std::uint8_t> snapshotBytes;
            if (!ReadBytes(reader, static_cast<std::uint32_t>(kMaxProfileFileBytes),
                           &snapshotBytes, error)) {
                return false;
            }
            save::EntitySnapshot snapshot;
            std::string snapshotError;
            if (!save::DecodeEntitySnapshot(snapshotBytes, &snapshot, &snapshotError) ||
                snapshot.entities.size() != 1 ||
                snapshot.entities[0].stableId != profile.stableId) {
                SetError(error, "玩家档案实体快照无效：" + snapshotError);
                return false;
            }
            profile.entity = std::move(snapshot.entities[0]);
        }
        if (profile.name.empty()) {
            profile.name = "Player";
        }
        profiles.emplace(profile.identity, std::move(profile));
    }
    if (!reader.EndSegment() || !reader.AtEnd()) {
        SetError(error, "玩家档案尾随数据或段损坏");
        return false;
    }
    if (nextStableId < kFirstProfileStableId) {
        nextStableId = kFirstProfileStableId;
    }
    for (const auto& [identity, profile] : profiles) {
        static_cast<void>(identity);
        if (profile.stableId >= nextStableId) {
            nextStableId = profile.stableId + 1;
        }
    }
    if (nextStableId == 0 || nextStableId == std::numeric_limits<std::uint64_t>::max()) {
        SetError(error, "玩家档案 StableId 分配游标耗尽");
        return false;
    }
    m_profiles = std::move(profiles);
    m_nextStableId = nextStableId;
    return true;
}

bool PlayerProfileStore::Save(std::string* error) {
    if (m_path.empty()) {
        SetError(error, "玩家档案路径为空");
        return false;
    }

    save::BinaryWriter writer;
    if (!writer.BeginSegment(kProfileMagic, kProfileVersion) ||
        !writer.Write(m_nextStableId) ||
        !writer.Write(static_cast<std::uint32_t>(m_profiles.size()))) {
        SetError(error, "玩家档案编码失败：" + writer.Error());
        return false;
    }
    for (const auto& [identity, profile] : m_profiles) {
        net::PlayerState state = profile.state;
        state.playerStableId = profile.stableId;
        state.alive = profile.alive;
        state.respawnSeconds = std::clamp(profile.respawnSeconds, 0.0f, 86'400.0f);
        if (!state.alive) {
            state.health = 0.0f;
        }
        state.status.clear();
        state.statusSequence = 0;
        const std::vector<std::uint8_t> stateBytes =
            net::EncodePlayerState(state, nullptr);
        if (stateBytes.empty() ||
            !writer.WriteString(identity, net::kMaxPlayerIdentityBytes) ||
            !writer.WriteString(profile.name, net::kMaxSessionName) ||
            !writer.Write(profile.stableId) || !writer.Write(profile.alive) ||
            !writer.Write(profile.respawnSeconds) ||
            !writer.Write(static_cast<std::uint32_t>(stateBytes.size())) ||
            !writer.WriteBytes(stateBytes)) {
            SetError(error, "玩家档案记录编码失败：" + writer.Error());
            return false;
        }

        std::vector<std::uint8_t> snapshotBytes;
        if (profile.hasSnapshot) {
            save::EntitySnapshot snapshot;
            snapshot.entities.push_back(profile.entity);
            snapshotBytes = save::EncodeEntitySnapshot(snapshot, nullptr);
            if (snapshotBytes.empty()) {
                SetError(error, "玩家档案实体快照编码失败：" + identity);
                return false;
            }
        }
        if (!writer.Write(profile.hasSnapshot) ||
            (profile.hasSnapshot &&
             (!writer.Write(static_cast<std::uint32_t>(snapshotBytes.size())) ||
              !writer.WriteBytes(snapshotBytes)))) {
            SetError(error, "玩家档案快照编码失败：" + writer.Error());
            return false;
        }
    }
    if (!writer.EndSegment()) {
        SetError(error, "玩家档案收尾失败：" + writer.Error());
        return false;
    }
    const auto bytes = writer.Data();
    return WriteFileAtomic(m_path, bytes, error);
}

}  // namespace zl::server
