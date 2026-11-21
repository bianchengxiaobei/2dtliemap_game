#include <string>

#include "doctest/doctest.h"

#include "core/Config.h"
#include "game/ActionHints.h"
#include "game/Throwing.h"

using namespace zl;

using zl::game::ActionHintState;
using zl::game::ActionHintText;
using zl::game::EvaluateThrowAim;
using zl::game::LoadThrowingConfig;
using zl::game::ResolveThrowAimAction;
using zl::game::ResolveThrowAimSource;
using zl::game::SanitizeThrowingConfig;
using zl::game::ThrowAimAction;
using zl::game::ThrowAimBlockReason;
using zl::game::ThrowAimRequest;
using zl::game::ThrowAimSource;
using zl::game::ThrowingConfig;

namespace {

ThrowAimRequest ReadyRequest() {
    ThrowAimRequest request;
    request.hasThrowable = true;
    request.targetValid = true;
    request.playerX = 10.0f;
    request.playerY = 10.0f;
    request.targetX = 16.0f;
    request.targetY = 10.0f;
    request.itemRange = 8.0f;
    request.config = ThrowingConfig{0.7f, 12.0f, 1.0f};
    return request;
}

}  // namespace

TEST_CASE("M34-F2-T2 投掷物优先于枪械且主手优先") {
    CHECK(ResolveThrowAimSource(true, true) == ThrowAimSource::PrimaryHand);
    CHECK(ResolveThrowAimSource(false, true) == ThrowAimSource::Hotbar);
    CHECK(ResolveThrowAimSource(false, false) == ThrowAimSource::None);

    CHECK(ResolveThrowAimAction(true, false, true) == ThrowAimAction::Throwable);
    CHECK(ResolveThrowAimAction(false, true, true) == ThrowAimAction::Throwable);
    CHECK(ResolveThrowAimAction(false, false, true) == ThrowAimAction::Ranged);
    CHECK(ResolveThrowAimAction(false, false, false) == ThrowAimAction::None);
}

TEST_CASE("M34-F2-T2 射程夹取、最小距离与无效落点") {
    ThrowAimRequest request = ReadyRequest();
    auto result = EvaluateThrowAim(request);
    CHECK(result.valid);
    CHECK_FALSE(result.clamped);
    CHECK(result.targetX == doctest::Approx(16.0f));
    CHECK(result.targetY == doctest::Approx(10.0f));
    CHECK(result.distance == doctest::Approx(6.0f));
    CHECK(result.range == doctest::Approx(8.0f));
    CHECK(result.directionX == doctest::Approx(1.0f));
    CHECK(result.directionY == doctest::Approx(0.0f));

    request.targetX = 30.0f;
    result = EvaluateThrowAim(request);
    CHECK(result.valid);
    CHECK(result.clamped);
    CHECK(result.distance == doctest::Approx(8.0f));
    CHECK(result.targetX == doctest::Approx(18.0f));
    CHECK(result.targetY == doctest::Approx(10.0f));

    request.itemRange = 16.0f;
    result = EvaluateThrowAim(request);
    CHECK(result.valid);
    CHECK(result.clamped);
    CHECK(result.distance == doctest::Approx(12.0f));
    CHECK(result.targetX == doctest::Approx(22.0f));

    request = ReadyRequest();
    request.targetX = 10.5f;
    result = EvaluateThrowAim(request);
    CHECK_FALSE(result.valid);
    CHECK(result.block == ThrowAimBlockReason::TooClose);

    request = ReadyRequest();
    request.targetValid = false;
    result = EvaluateThrowAim(request);
    CHECK_FALSE(result.valid);
    CHECK(result.block == ThrowAimBlockReason::InvalidTarget);
}

TEST_CASE("M34-F2-T2 UI/建造/背包/载具拦截与 V 投掷提示") {
    ThrowAimRequest request = ReadyRequest();
    request.uiPointerOver = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::UiPointer);

    request = ReadyRequest();
    request.buildMode = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::BuildMode);

    request = ReadyRequest();
    request.inventoryOpen = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::InventoryOpen);

    request = ReadyRequest();
    request.menuOpen = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::MenuOpen);

    request = ReadyRequest();
    request.modalOpen = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::OtherUi);

    request = ReadyRequest();
    request.dead = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::Dead);

    request = ReadyRequest();
    request.inVehicle = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::InVehicle);

    request = ReadyRequest();
    request.gathering = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::Gathering);

    request = ReadyRequest();
    request.hasThrowable = false;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::NoThrowable);

    request = ReadyRequest();
    request.uiPointerOver = true;
    request.buildMode = true;
    CHECK(EvaluateThrowAim(request).block == ThrowAimBlockReason::UiPointer);

    ActionHintState hint;
    hint.hasThrowable = true;
    hint.throwableName = "石块";
    std::string tone;
    std::string text = ActionHintText(hint, &tone);
    CHECK(text.find("V 投掷：石块") != std::string::npos);

    hint.throwAiming = true;
    hint.throwDistance = 7.5f;
    hint.throwClamped = true;
    hint.throwWallClipped = true;
    text = ActionHintText(hint, &tone);
    CHECK(text.find("松开 V 投掷") != std::string::npos);
    CHECK(text.find("石块") != std::string::npos);
    CHECK(text.find("7.5") != std::string::npos);
    CHECK(text.find("最大射程") != std::string::npos);
    CHECK(text.find("墙前落点") != std::string::npos);
}

TEST_CASE("M34-F2-T2 throwing 配置读取、默认值与非法回退") {
    core::Config shipped;
    REQUIRE_MESSAGE(shipped.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"),
                    "config/game.json 加载失败");
    const ThrowingConfig loaded = LoadThrowingConfig(shipped);
    CHECK(loaded.cooldownSeconds == doctest::Approx(0.7f));
    CHECK(loaded.maxRange == doctest::Approx(12.0f));
    CHECK(loaded.minRange == doctest::Approx(1.0f));

    core::Config custom;
    custom.Set("throwing.cooldownSeconds", 1.25);
    custom.Set("throwing.maxRange", 9.5);
    custom.Set("throwing.minRange", 1.5);
    const ThrowingConfig customRules = LoadThrowingConfig(custom);
    CHECK(customRules.cooldownSeconds == doctest::Approx(1.25f));
    CHECK(customRules.maxRange == doctest::Approx(9.5f));
    CHECK(customRules.minRange == doctest::Approx(1.5f));

    core::Config legacy;
    const ThrowingConfig legacyRules = LoadThrowingConfig(legacy);
    CHECK(legacyRules.cooldownSeconds ==
          doctest::Approx(ThrowingConfig{}.cooldownSeconds));
    CHECK(legacyRules.maxRange == doctest::Approx(ThrowingConfig{}.maxRange));
    CHECK(legacyRules.minRange == doctest::Approx(ThrowingConfig{}.minRange));

    core::Config invalid;
    invalid.Set("throwing.cooldownSeconds", -2.0);
    invalid.Set("throwing.maxRange", 0.0);
    invalid.Set("throwing.minRange", 99.0);
    const ThrowingConfig invalidRules = LoadThrowingConfig(invalid);
    CHECK(invalidRules.cooldownSeconds ==
          doctest::Approx(ThrowingConfig{}.cooldownSeconds));
    CHECK(invalidRules.maxRange == doctest::Approx(ThrowingConfig{}.maxRange));
    CHECK(invalidRules.minRange == doctest::Approx(ThrowingConfig{}.minRange));

    ThrowingConfig inverted;
    inverted.minRange = 8.0f;
    inverted.maxRange = 4.0f;
    const ThrowingConfig safe = SanitizeThrowingConfig(inverted);
    CHECK(safe.minRange == doctest::Approx(ThrowingConfig{}.minRange));
    CHECK(safe.maxRange == doctest::Approx(ThrowingConfig{}.maxRange));
}
