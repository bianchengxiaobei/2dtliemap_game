#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <doctest/doctest.h>

#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveService.h"

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

}  // namespace

TEST_CASE("M10-T5 后台存档线程新建与更新") {
    TempRoot root("zl_save_service");
    save::ComponentRegistry components = MakeRegistry();
    save::SaveManager manager({root.path, "service-1.0"});
    REQUIRE(manager.EnsureRoot());
    save::SaveService service(manager);

    save::SaveRequest create;
    create.name = "后台存档";
    create.mapId = "village_demo";
    save::EntitySnapshot empty;
    create.entities = empty;
    std::string error;
    REQUIRE(service.Request(std::move(create), &error));
    CHECK(error.empty());
    service.WaitIdle();

    std::vector<save::SaveResult> results = service.PollResults();
    REQUIRE(results.size() == 1u);
    CHECK(results[0].ok);
    CHECK(results[0].created);
    CHECK_FALSE(results[0].id.empty());
    CHECK(results[0].error.empty());
    const std::string id = results[0].id;

    save::EntitySnapshot loaded;
    REQUIRE(manager.Load(id, components, &loaded, nullptr, &error) ==
            save::SaveManager::LoadResult::Ok);
    CHECK(loaded.entities.empty());

    save::SaveRequest update;
    update.id = id;
    update.entities = empty;
    REQUIRE(service.Request(std::move(update), &error));
    service.WaitIdle();
    results = service.PollResults();
    REQUIRE(results.size() == 1u);
    CHECK(results[0].ok);
    CHECK_FALSE(results[0].created);
    CHECK(results[0].id == id);
}

TEST_CASE("M10-T5 后台存档失败回传明确错误") {
    TempRoot root("zl_save_service_failure");
    std::filesystem::create_directories(root.path);
    const std::filesystem::path fileRoot = root.path / "not-a-directory";
    {
        std::ofstream file(fileRoot, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file << "x";
    }

    save::SaveManager manager({fileRoot, "service-1.0"});
    save::SaveService service(manager);
    save::SaveRequest request;
    request.name = "失败存档";
    const save::EntitySnapshot empty;
    request.entities = empty;
    std::string error;
    REQUIRE(service.Request(std::move(request), &error));
    service.WaitIdle();

    const std::vector<save::SaveResult> results = service.PollResults();
    REQUIRE(results.size() == 1u);
    CHECK_FALSE(results[0].ok);
    CHECK_FALSE(results[0].error.empty());
}
