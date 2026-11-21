#include <string>

#include "doctest/doctest.h"
#include "game/ActionHints.h"
#include "game/Traps.h"
#include "ui/HudState.h"

TEST_CASE("M34-F3-T6：陷阱构筑摘要与效果文案") {
    zl::game::TrapDef bear;
    bear.id = "bear_trap";
    bear.name = "捕兽夹";
    bear.damage = 55.0f;
    bear.staggerSeconds = 2.0f;
    bear.uses = 4;
    bear.cooldownSeconds = 2.0f;
    CHECK(zl::game::TrapEffectText(bear) == "55 伤害 · 硬直 2 秒");
    CHECK(zl::game::TrapBuildSummary(bear) ==
          "55 伤害 · 硬直 2 秒 · 4 次/冷却 2s");

    zl::game::TrapDef tripwire;
    tripwire.id = "tripwire_alarm";
    tripwire.name = "绊线警报";
    tripwire.noiseRadius = 18.0f;
    tripwire.uses = 8;
    tripwire.cooldownSeconds = 1.0f;
    CHECK(zl::game::TrapEffectText(tripwire) == "噪声 18 格");
    CHECK(zl::game::TrapBuildSummary(tripwire) ==
          "噪声 18 格 · 8 次/冷却 1s");

    zl::game::TrapDef fuel;
    fuel.id = "fuel_trap";
    fuel.name = "燃油陷阱";
    fuel.uses = 1;
    fuel.fire = zl::game::TrapFireDef{1.5f, 45.0f};
    CHECK(zl::game::TrapEffectText(fuel) == "火焰 1.5 格 · 45 分钟");
    CHECK(zl::game::TrapBuildSummary(fuel) ==
          "火焰 1.5 格 · 45 分钟 · 1 次");
}

TEST_CASE("M34-F3-T6：陷阱 HUD、操作提示与 owner 只读") {
    zl::ui::HudState hud;
    hud.trapValid = true;
    hud.trapName = "捕兽夹";
    hud.trapArmed = true;
    hud.trapUses = 4;
    hud.trapUsesMax = 4;
    hud.trapTone = "info";
    std::string tone;
    CHECK(zl::ui::TrapStatusText(hud, &tone) ==
          "捕兽夹 · 4/4 次 · 已布防");
    CHECK(tone == "ok");

    hud.trapCooldownRemaining = 1.2f;
    hud.trapCooldownDuration = 2.0f;
    CHECK(zl::ui::TrapStatusText(hud, &tone) ==
          "捕兽夹 · 4/4 次 · 已布防 · 冷却 1.2s/2.0s");
    CHECK(tone == "warn");

    hud.trapCooldownRemaining = 0.0f;
    hud.trapArmed = false;
    hud.trapArming = true;
    hud.trapArmRemaining = 0.7f;
    CHECK(zl::ui::TrapStatusText(hud, &tone) ==
          "捕兽夹 · 4/4 次 · 布防中 0.7s");
    CHECK(tone == "warn");

    hud.trapArming = false;
    hud.trapArmRemaining = 0.0f;
    hud.trapReadOnly = true;
    CHECK(zl::ui::TrapStatusText(hud, &tone) ==
          "捕兽夹 · 4/4 次 · 已解除 · 仅所有者可操作");
    CHECK(tone == "warn");

    hud.trapUses = 0;
    hud.trapReadOnly = false;
    CHECK(zl::ui::TrapStatusText(hud, &tone) ==
          "捕兽夹 · 0/4 次 · 已耗尽 · 需拆除重建");
    CHECK(tone == "fail");
    CHECK(zl::ui::TrapStatusText(zl::ui::HudState{}, &tone).empty());

    zl::game::TrapHintState hint;
    hint.name = "捕兽夹";
    hint.armed = true;
    hint.uses = 4;
    hint.usesMax = 4;
    CHECK(zl::game::TrapHintText(hint, &tone) ==
          "捕兽夹 · 4/4 次 · 已布防 · E 解除 · X 拆除（建造模式）");
    CHECK(tone == "info");

    hint.armed = false;
    hint.arming = true;
    hint.armRemaining = 0.7f;
    CHECK(zl::game::TrapHintText(hint, &tone) ==
          "捕兽夹 · 4/4 次 · 布防中 0.7s · E 立即布防 · X 拆除（建造模式）");
    CHECK(tone == "warn");

    hint.arming = false;
    hint.readOnly = true;
    CHECK(zl::game::TrapHintText(hint, &tone) ==
          "捕兽夹 · 4/4 次 · 已解除 · 仅所有者可操作");
    CHECK(tone == "warn");

    hint.readOnly = false;
    hint.uses = 0;
    CHECK(zl::game::TrapHintText(hint, &tone) ==
          "捕兽夹 · 0/4 次 · 已耗尽 · 需拆除重建");
    CHECK(tone == "fail");

    CHECK(zl::ui::HudTrapStatusSuppressed(true, false, false, false, false));
    CHECK(zl::ui::HudTrapStatusSuppressed(false, true, false, false, false));
    CHECK(zl::ui::HudTrapStatusSuppressed(false, false, true, false, false));
    CHECK(zl::ui::HudTrapStatusSuppressed(false, false, false, true, false));
    CHECK(zl::ui::HudTrapStatusSuppressed(false, false, false, false, true));
    CHECK_FALSE(
        zl::ui::HudTrapStatusSuppressed(false, false, false, false, false));
}
