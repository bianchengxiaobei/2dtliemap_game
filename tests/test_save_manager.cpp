#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "items/ItemComponents.h"
#include "save/BinaryWriter.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/Thumbnail.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

using namespace zl;

namespace {

struct TempRoot {
    std::filesystem::path path;

    explicit TempRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
    }

    ~TempRoot() { std::filesystem::remove_all(path); }
};

save::ComponentRegistry MakeRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    return registry;
}

save::ThumbnailImage MakeThumbnail() {
    save::ThumbnailImage image;
    image.width = 4;
    image.height = 3;
    image.rgba.resize(4u * 3u * 4u);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i + 0] = static_cast<std::uint8_t>(i);
        image.rgba[i + 1] = static_cast<std::uint8_t>(i + 1);
        image.rgba[i + 2] = static_cast<std::uint8_t>(i + 2);
        image.rgba[i + 3] = 255;
    }
    return image;
}

save::EntitySnapshot MakePlayerSnapshot(save::ComponentRegistry& components,
                                        std::uint64_t* stableId,
                                        float health = 82.5f) {
    ecs::EcsWorld world;
    const ecs::Entity player = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{70.5f, 24.25f, 0});
    registry.emplace<ecs::Health>(player, ecs::Health{health, 100.0f});
    ecs::Container bag;
    bag.capacityKg = 15.0f;
    bag.items.push_back(ecs::ItemInstance{"bandage", 3, 0.75f});
    registry.emplace<ecs::Container>(player, std::move(bag));

    save::SavedEntity saved;
    std::string error;
    REQUIRE(save::CapturePlayer(world, components, &saved, &error));
    CHECK(error.empty());
    if (stableId != nullptr) {
        *stableId = saved.stableId;
    }
    save::EntitySnapshot snapshot;
    snapshot.entities.push_back(std::move(saved));
    return snapshot;
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.is_open());
    const std::streamoff end = file.tellg();
    REQUIRE(end >= 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    REQUIRE(static_cast<bool>(file));
    return bytes;
}

void WriteBytes(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.flush();
    REQUIRE(static_cast<bool>(file));
}

std::uint32_t ReadBigEndian32(const std::vector<std::uint8_t>& bytes,
                              std::size_t offset) {
    REQUIRE(offset + 4 <= bytes.size());
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

save::SaveManager::CreateOptions MakeCreateOptions(
    const save::EntitySnapshot& entities, std::uint64_t playerId) {
    save::SaveManager::CreateOptions options;
    options.name = "测试存档";
    options.mapId = "village_demo";
    options.players.push_back(save::SavePlayer{playerId, "Alice", true});
    options.thumbnail = MakeThumbnail();
    options.entities = &entities;
    return options;
}

ecs::Entity FindPlayer(ecs::EcsWorld& world) {
    ecs::Entity player = entt::null;
    world.Registry().view<ecs::PlayerTag>().each(
        [&](const ecs::Entity entity) {
            if (player == entt::null) {
                player = entity;
            }
        });
    return player;
}

float LoadedPlayerHealth(save::SaveManager& manager,
                         save::ComponentRegistry& components, std::string_view id,
                         save::SaveManager::LoadReport* report = nullptr) {
    save::EntitySnapshot entities;
    std::string error;
    const save::SaveManager::LoadResult result =
        manager.Load(id, components, &entities, nullptr, &error, report);
    REQUIRE(result == save::SaveManager::LoadResult::Ok);
    CHECK(error.empty());

    ecs::EcsWorld world;
    REQUIRE(save::LoadEntities(entities, world, components, nullptr, &error));
    const ecs::Entity player = FindPlayer(world);
    REQUIRE(player != ecs::EcsWorld::kNull);
    const auto* health = world.Registry().try_get<ecs::Health>(player);
    REQUIRE(health != nullptr);
    return health->current;
}

save::ChunkDiff MakeDirtDiff() {
    world::Chunk source(0, 0);
    source.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    source.CaptureBaseline();
    source.Square(1, 1, 0).SetFloor({"tile.floor.dirt", 0, 0});
    save::ChunkDiff diff;
    std::string error;
    REQUIRE(save::CaptureChunkDiff(source, &diff, &error));
    CHECK(error.empty());
    REQUIRE_FALSE(diff.squares.empty());
    return diff;
}

}  // namespace

TEST_CASE("M10-T4 PNG 缩略图编码与非法输入") {
    const save::ThumbnailImage image = MakeThumbnail();
    std::string error;
    std::vector<std::uint8_t> png;
    REQUIRE(save::EncodePng(image, &png, &error));
    CHECK(error.empty());
    REQUIRE(png.size() > 24u);
    CHECK(png[0] == 0x89u);
    CHECK(png[1] == 'P');
    CHECK(png[2] == 'N');
    CHECK(png[3] == 'G');
    CHECK(ReadBigEndian32(png, 16) == 4u);
    CHECK(ReadBigEndian32(png, 20) == 3u);

    save::ThumbnailImage invalid;
    invalid.width = 2;
    invalid.height = 2;
    CHECK_FALSE(save::EncodePng(invalid, &png, &error));
    CHECK_FALSE(error.empty());

    TempRoot root("zl_save_thumbnail_test");
    std::filesystem::create_directories(root.path);
    const std::filesystem::path path = root.path / "thumb.png";
    REQUIRE(save::WritePng(path, image, &error));
    CHECK(std::filesystem::exists(path));
    CHECK(ReadBytes(path) == png);
}

TEST_CASE("M10-T4 SaveManager 创建、列表、加载与删除") {
    TempRoot root("zl_save_manager_roundtrip");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    const save::EntitySnapshot entities =
        MakePlayerSnapshot(components, &playerId);
    REQUIRE(playerId != 0);

    save::SaveManager manager({root.path, "manager-1.0"});
    CHECK(manager.Root() == root.path);
    CHECK(manager.EnsureRoot());
    CHECK_FALSE(manager.List(nullptr));

    std::string id;
    std::string error;
    const save::SaveManager::CreateOptions options =
        MakeCreateOptions(entities, playerId);
    REQUIRE(manager.Create(options, &id, &error));
    CHECK(error.empty());
    CHECK_FALSE(id.empty());
    CHECK(manager.Exists(id));
    CHECK(std::filesystem::exists(manager.SavePath(id)));
    CHECK(std::filesystem::exists(manager.WorldDir(id)));
    CHECK(std::filesystem::exists(manager.MetaPath(id)));
    CHECK(std::filesystem::exists(manager.HeaderPath(id)));
    CHECK(std::filesystem::exists(manager.EntitiesPath(id)));
    CHECK(std::filesystem::exists(manager.ThumbnailPath(id)));
    CHECK(std::filesystem::file_size(manager.MetaPath(id)) > 0u);

    std::vector<save::SaveMetadata> list;
    REQUIRE(manager.List(&list, &error));
    REQUIRE(list.size() == 1u);
    CHECK(list[0].id == id);
    CHECK(list[0].name == "测试存档");
    CHECK(list[0].mapId == "village_demo");
    CHECK(list[0].gameVersion == "manager-1.0");
    CHECK(list[0].saveVersion == save::kSaveFormatVersion);
    CHECK(list[0].thumbnail == save::kSaveThumbnailFile);
    REQUIRE(list[0].players.size() == 1u);
    CHECK(list[0].players[0].stableId == playerId);
    CHECK(list[0].players[0].name == "Alice");
    CHECK(list[0].players[0].local);

    save::SaveHeader header;
    REQUIRE(manager.ReadHeader(id, &header, &error) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(header.version == save::kSaveFormatVersion);
    CHECK(header.gameVersion == "manager-1.0");
    CHECK(header.mapId == "village_demo");
    CHECK(header.createdAt == list[0].createdAt);

    save::EntitySnapshot loadedEntities;
    save::SaveMetadata loadedMetadata;
    REQUIRE(manager.Load(id, components, &loadedEntities, &loadedMetadata,
                         &error) == save::SaveManager::LoadResult::Ok);
    CHECK(error.empty());
    REQUIRE(loadedEntities.entities.size() == 1u);
    CHECK(loadedEntities.entities[0].stableId == playerId);
    CHECK(loadedMetadata.name == "测试存档");

    const std::vector<std::uint8_t> png = ReadBytes(manager.ThumbnailPath(id));
    CHECK(ReadBigEndian32(png, 16) == 4u);
    CHECK(ReadBigEndian32(png, 20) == 3u);

    REQUIRE(manager.Delete(id, &error));
    CHECK(error.empty());
    CHECK_FALSE(manager.Exists(id));
    CHECK_FALSE(std::filesystem::exists(manager.SavePath(id)));
    REQUIRE(manager.List(&list, &error));
    CHECK(list.empty());
    CHECK(manager.Load(id, components, &loadedEntities, nullptr, &error) ==
          save::SaveManager::LoadResult::NotFound);
}

TEST_CASE("M10-T4 SaveManager 更新实体与缩略图") {
    TempRoot root("zl_save_manager_update");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    save::EntitySnapshot entities =
        MakePlayerSnapshot(components, &playerId);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    std::string id;
    std::string error;
    REQUIRE(manager.Create(MakeCreateOptions(entities, playerId), &id, &error));

    save::ThumbnailImage replacement = MakeThumbnail();
    replacement.width = 2;
    replacement.height = 2;
    replacement.rgba.assign(2u * 2u * 4u, 0x7Fu);
    REQUIRE(manager.Save(id, entities, &replacement, &error));
    CHECK(error.empty());

    save::SaveMetadata metadata;
    REQUIRE(manager.ReadMetadata(id, &metadata, &error) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(metadata.thumbnail == save::kSaveThumbnailFile);
    const auto png = ReadBytes(manager.ThumbnailPath(id));
    CHECK(ReadBigEndian32(png, 16) == 2u);
    CHECK(ReadBigEndian32(png, 20) == 2u);

    save::EntitySnapshot loaded;
    REQUIRE(manager.Load(id, components, &loaded, nullptr, &error) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(loaded.entities.size() == 1u);
}

TEST_CASE("M10-T4 SaveManager 保存与加载 chunk diff") {
    TempRoot root("zl_save_manager_chunk");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    save::EntitySnapshot entities =
        MakePlayerSnapshot(components, &playerId);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    std::string id;
    std::string error;
    REQUIRE(manager.Create(MakeCreateOptions(entities, playerId), &id, &error));

    world::Chunk source(0, 0);
    source.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    source.CaptureBaseline();
    source.Square(1, 1, 0).SetFloor({"tile.floor.dirt", 0, 0});
    REQUIRE(manager.SaveChunk(id, source, &error));
    CHECK(std::filesystem::exists(manager.WorldDir(id) / "chunk_0_0.bin"));

    world::Chunk target(0, 0);
    target.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    target.CaptureBaseline();
    REQUIRE(manager.LoadChunk(id, target, &error) ==
            save::SaveManager::LoadResult::Ok);
    const world::GridSquare& square = target.Square(1, 1, 0);
    REQUIRE(square.Floor() != nullptr);
    CHECK(square.Floor()->sprite == "tile.floor.dirt");

    world::Chunk clean(1, 1);
    clean.CaptureBaseline();
    CHECK(manager.LoadChunk(id, clean, &error) ==
          save::SaveManager::LoadResult::NotFound);
}

TEST_CASE("M10-T4 SaveManager 拒绝非法输入并识别损坏/版本") {
    TempRoot root("zl_save_manager_errors");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    save::EntitySnapshot entities =
        MakePlayerSnapshot(components, &playerId);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    std::string error;

    save::SaveManager::CreateOptions options =
        MakeCreateOptions(entities, playerId);
    options.name.clear();
    std::string id;
    CHECK_FALSE(manager.Create(options, &id, &error));

    options = MakeCreateOptions(entities, playerId);
    options.thumbnail.width = 3;
    options.thumbnail.height = 3;
    options.thumbnail.rgba.clear();
    CHECK_FALSE(manager.Create(options, &id, &error));

    options = MakeCreateOptions(entities, playerId);
    REQUIRE(manager.Create(options, &id, &error));
    CHECK_FALSE(manager.Delete("../escape", &error));
    CHECK_FALSE(manager.Delete("not-present", &error));
    CHECK(manager.Load("../escape", components, &entities, nullptr, &error) ==
          save::SaveManager::LoadResult::NotFound);

    WriteBytes(manager.EntitiesPath(id),
               std::vector<std::uint8_t>{'b', 'a', 'd'});
    CHECK(manager.Load(id, components, &entities, nullptr, &error) ==
          save::SaveManager::LoadResult::Corrupt);
    CHECK_FALSE(error.empty());

    save::BinaryWriter versionWriter;
    REQUIRE(versionWriter.BeginSegment(save::kMagicSaveHeader, 2));
    REQUIRE(versionWriter.WriteString("future"));
    REQUIRE(versionWriter.WriteString("village_demo"));
    REQUIRE(versionWriter.Write(std::int64_t{1}));
    REQUIRE(versionWriter.EndSegment());
    const auto versionBytes = versionWriter.Data();
    WriteBytes(manager.HeaderPath(id),
               std::vector<std::uint8_t>(versionBytes.begin(),
                                         versionBytes.end()));
    CHECK(manager.Load(id, components, &entities, nullptr, &error) ==
          save::SaveManager::LoadResult::Incompatible);
    CHECK(error.find("版本") != std::string::npos);

    WriteBytes(manager.MetaPath(id),
               std::vector<std::uint8_t>{'n', 'o', 't', 's', 'q', 'l'});
    save::SaveMetadata corruptedMetadata;
    CHECK(manager.ReadMetadata(id, &corruptedMetadata, &error) ==
          save::SaveManager::LoadResult::Corrupt);
    CHECK(manager.Load(id, components, &entities, nullptr, &error) ==
          save::SaveManager::LoadResult::Corrupt);

    std::filesystem::create_directories(root.path / "junk");
    std::vector<save::SaveMetadata> list;
    REQUIRE(manager.List(&list, &error));
    CHECK(list.empty());
}

TEST_CASE("M10-T5 存档原子提交与损坏回退备份") {
    TempRoot root("zl_save_manager_backup");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    const save::EntitySnapshot first =
        MakePlayerSnapshot(components, &playerId, 82.5f);
    const save::EntitySnapshot second =
        MakePlayerSnapshot(components, &playerId, 41.0f);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    std::string id;
    std::string error;
    REQUIRE(manager.Create(MakeCreateOptions(first, playerId), &id, &error));
    CHECK_FALSE(std::filesystem::exists(manager.BackupPath(id)));

    REQUIRE(manager.Save(id, second, nullptr, &error));
    REQUIRE(std::filesystem::is_directory(manager.BackupPath(id)));
    CHECK(LoadedPlayerHealth(manager, components, id) == doctest::Approx(41.0f));

    for (const auto& entry : std::filesystem::directory_iterator(root.path)) {
        const std::string name = entry.path().filename().string();
        CHECK_FALSE(name.starts_with(".tmp-"));
    }

    WriteBytes(manager.EntitiesPath(id),
               std::vector<std::uint8_t>{'b', 'a', 'd'});
    save::SaveManager::LoadReport report;
    CHECK(LoadedPlayerHealth(manager, components, id, &report) ==
          doctest::Approx(82.5f));
    CHECK(report.usedBackup);
    CHECK_FALSE(report.warning.empty());

    WriteBytes(manager.MetaPath(id),
               std::vector<std::uint8_t>{'n', 'o', 't', 's', 'q', 'l'});
    std::vector<save::SaveMetadata> list;
    REQUIRE(manager.List(&list, &error));
    REQUIRE(list.size() == 1u);
    CHECK(list[0].id == id);
    CHECK(LoadedPlayerHealth(manager, components, id) == doctest::Approx(82.5f));

    REQUIRE(manager.Delete(id, &error));
    CHECK_FALSE(std::filesystem::exists(manager.SavePath(id)));
    CHECK_FALSE(std::filesystem::exists(manager.BackupPath(id)));
}

TEST_CASE("M10-T5 chunk 损坏回退备份") {
    TempRoot root("zl_save_manager_chunk_backup");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    const save::EntitySnapshot first =
        MakePlayerSnapshot(components, &playerId);
    const save::EntitySnapshot second =
        MakePlayerSnapshot(components, &playerId, 60.0f);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    save::SaveManager::CreateOptions options = MakeCreateOptions(first, playerId);
    options.chunks.push_back(MakeDirtDiff());
    std::string id;
    std::string error;
    REQUIRE(manager.Create(options, &id, &error));
    REQUIRE(std::filesystem::exists(manager.WorldDir(id) / "chunk_0_0.bin"));

    REQUIRE(manager.Save(id, second, nullptr, &error));
    REQUIRE(std::filesystem::exists(
        manager.BackupPath(id) / "world" / "chunk_0_0.bin"));

    WriteBytes(manager.WorldDir(id) / "chunk_0_0.bin",
               std::vector<std::uint8_t>{'b', 'a', 'd'});
    world::Chunk target(0, 0);
    target.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    target.CaptureBaseline();
    save::SaveManager::LoadReport report;
    CHECK(manager.LoadChunk(id, target, &error, &report) ==
          save::SaveManager::LoadResult::Ok);
    CHECK(error.empty());
    CHECK(report.usedBackup);
    CHECK_FALSE(report.warning.empty());
    const world::GridSquare& square = target.Square(1, 1, 0);
    REQUIRE(square.Floor() != nullptr);
    CHECK(square.Floor()->sprite == "tile.floor.dirt");

    world::Chunk backupTarget(0, 0);
    backupTarget.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    backupTarget.CaptureBaseline();
    CHECK(manager.LoadChunkFromBackup(id, backupTarget, &error) ==
          save::SaveManager::LoadResult::Ok);
    const world::GridSquare& backupSquare = backupTarget.Square(1, 1, 0);
    REQUIRE(backupSquare.Floor() != nullptr);
    CHECK(backupSquare.Floor()->sprite == "tile.floor.dirt");
}

TEST_CASE("M10-T5 SaveOptions 后台更新实体与 chunk") {
    TempRoot root("zl_save_manager_save_options");
    save::ComponentRegistry components = MakeRegistry();
    std::uint64_t playerId = 0;
    const save::EntitySnapshot first =
        MakePlayerSnapshot(components, &playerId);
    const save::EntitySnapshot second =
        MakePlayerSnapshot(components, &playerId, 55.0f);

    save::SaveManager manager({root.path, "manager-1.0"});
    REQUIRE(manager.EnsureRoot());
    std::string id;
    std::string error;
    REQUIRE(manager.Create(MakeCreateOptions(first, playerId), &id, &error));

    std::vector<save::ChunkDiff> chunks;
    chunks.push_back(MakeDirtDiff());
    save::SaveManager::SaveOptions options;
    options.entities = &second;
    options.chunks = &chunks;
    REQUIRE(manager.Save(id, options, &error));
    CHECK(LoadedPlayerHealth(manager, components, id) == doctest::Approx(55.0f));

    world::Chunk target(0, 0);
    target.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
    target.CaptureBaseline();
    REQUIRE(manager.LoadChunk(id, target, &error) ==
            save::SaveManager::LoadResult::Ok);
    const world::GridSquare& square = target.Square(1, 1, 0);
    REQUIRE(square.Floor() != nullptr);
    CHECK(square.Floor()->sprite == "tile.floor.dirt");
}
