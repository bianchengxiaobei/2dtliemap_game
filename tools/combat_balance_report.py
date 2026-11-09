#!/usr/bin/env python3
"""生成 M30 逐武器战斗平衡基线（Markdown / JSON）。"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "config" / "game.json"
DEFAULT_ITEMS = ROOT / "assets" / "items.json"
DEFAULT_MARKDOWN = ROOT / "docs" / "15-M30战斗平衡基线.md"
DEFAULT_JSON = ROOT / "build" / "combat_balance_report.json"
ATTACK_PRESENTATION_SECONDS = 0.28
EPSILON = 1.0e-9


def _number(raw: Any, fallback: float) -> float:
    if isinstance(raw, bool) or not isinstance(raw, (int, float)):
        return fallback
    value = float(raw)
    return value if math.isfinite(value) else fallback


def _valid(raw: Any, fallback: float, minimum: float, maximum: float) -> float:
    value = _number(raw, fallback)
    return value if minimum <= value <= maximum else fallback


def _positive_or(raw: Any, fallback: float) -> float:
    value = _number(raw, 0.0)
    return value if value > 0.0 else fallback


def _clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def _ceil_hits(value: float) -> int:
    return max(1, int(math.ceil(value - EPSILON)))


def _light_stamina_for_weight(weight: float) -> float:
    return _clamp(8.0 + weight * 2.0, 6.0, 18.0)


def _light_windup_for_weight(weight: float) -> float:
    return _clamp(0.04 + weight * 0.01, 0.0, 0.12)


def _light_poise_for_weight(weight: float) -> float:
    return _clamp(6.0 + weight * 1.5, 6.0, 12.0)


def _heavy_windup_for_weight(weight: float) -> float:
    return _clamp(0.26 + weight * 0.018, 0.30, 0.45)


@dataclass(frozen=True)
class Rules:
    stamina_max: float = 100.0
    stamina_exhausted_threshold: float = 5.0
    heavy_damage_scale: float = 1.65
    heavy_cooldown_scale: float = 1.55
    heavy_range_bonus: float = 0.10
    heavy_arc_scale: float = 0.90
    heavy_knockback_scale: float = 1.25
    heavy_stagger_scale: float = 1.25
    heavy_wear_scale: float = 1.25
    heavy_stamina_scale: float = 1.80
    heavy_poise_scale: float = 2.50
    block_damage_scale: float = 0.35
    block_base_stamina_cost: float = 8.0
    block_stamina_per_weight: float = 3.0
    block_min_stamina_cost: float = 10.0
    block_max_stamina_cost: float = 30.0
    poise_max_normal: float = 20.0
    poise_max_fast: float = 14.0
    poise_max_strong: float = 32.0
    poise_max_armored: float = 55.0
    armored_stagger_poise_threshold: float = 16.0

    @classmethod
    def from_mapping(cls, raw: Any) -> "Rules":
        source = raw if isinstance(raw, dict) else {}
        defaults = cls()
        return cls(
            stamina_max=_valid(source.get("staminaMax"), defaults.stamina_max, 1.0, 1000.0),
            stamina_exhausted_threshold=_valid(
                source.get("staminaExhaustedThreshold"),
                defaults.stamina_exhausted_threshold,
                0.0,
                1000.0,
            ),
            heavy_damage_scale=_valid(
                source.get("heavyDamageScale"), defaults.heavy_damage_scale, 0.1, 10.0
            ),
            heavy_cooldown_scale=_valid(
                source.get("heavyCooldownScale"), defaults.heavy_cooldown_scale, 0.1, 10.0
            ),
            heavy_range_bonus=_valid(
                source.get("heavyRangeBonus"), defaults.heavy_range_bonus, 0.0, 10.0
            ),
            heavy_arc_scale=_valid(
                source.get("heavyArcScale"), defaults.heavy_arc_scale, 0.05, 2.0
            ),
            heavy_knockback_scale=_valid(
                source.get("heavyKnockbackScale"), defaults.heavy_knockback_scale, 0.1, 10.0
            ),
            heavy_stagger_scale=_valid(
                source.get("heavyStaggerScale"), defaults.heavy_stagger_scale, 0.1, 10.0
            ),
            heavy_wear_scale=_valid(
                source.get("heavyWearScale"), defaults.heavy_wear_scale, 0.1, 10.0
            ),
            heavy_stamina_scale=_valid(
                source.get("heavyStaminaScale"), defaults.heavy_stamina_scale, 0.1, 10.0
            ),
            heavy_poise_scale=_valid(
                source.get("heavyPoiseScale"), defaults.heavy_poise_scale, 0.1, 10.0
            ),
            block_damage_scale=_valid(
                source.get("blockDamageScale"), defaults.block_damage_scale, 0.0, 1.0
            ),
            block_base_stamina_cost=_valid(
                source.get("blockBaseStaminaCost"),
                defaults.block_base_stamina_cost,
                0.0,
                1000.0,
            ),
            block_stamina_per_weight=_valid(
                source.get("blockStaminaPerWeight"),
                defaults.block_stamina_per_weight,
                0.0,
                1000.0,
            ),
            block_min_stamina_cost=_valid(
                source.get("blockMinStaminaCost"),
                defaults.block_min_stamina_cost,
                0.0,
                1000.0,
            ),
            block_max_stamina_cost=_valid(
                source.get("blockMaxStaminaCost"),
                defaults.block_max_stamina_cost,
                0.0,
                1000.0,
            ),
            poise_max_normal=_valid(
                source.get("poiseMaxNormal"), defaults.poise_max_normal, 1.0, 1000.0
            ),
            poise_max_fast=_valid(
                source.get("poiseMaxFast"), defaults.poise_max_fast, 1.0, 1000.0
            ),
            poise_max_strong=_valid(
                source.get("poiseMaxStrong"), defaults.poise_max_strong, 1.0, 1000.0
            ),
            poise_max_armored=_valid(
                source.get("poiseMaxArmored"), defaults.poise_max_armored, 1.0, 1000.0
            ),
            armored_stagger_poise_threshold=_valid(
                source.get("armoredStaggerPoiseThreshold"),
                defaults.armored_stagger_poise_threshold,
                0.0,
                1000.0,
            ),
        )


@dataclass(frozen=True)
class BaseAttack:
    damage: float = 40.0
    range: float = 1.7
    arc: float = 100.0
    cooldown: float = 0.5
    knockback: float = 3.0
    stagger: float = 0.35
    wear: float = 0.0

    @classmethod
    def from_mapping(cls, raw: Any) -> "BaseAttack":
        source = raw if isinstance(raw, dict) else {}
        defaults = cls()
        return cls(
            damage=_number(source.get("playerDamage"), defaults.damage),
            range=_number(source.get("playerRange"), defaults.range),
            arc=_number(source.get("playerArcDegrees"), defaults.arc),
            cooldown=_number(source.get("playerCooldownSeconds"), defaults.cooldown),
            knockback=_number(source.get("playerKnockback"), defaults.knockback),
            stagger=_number(source.get("playerStaggerSeconds"), defaults.stagger),
            wear=_number(source.get("playerWearPerHit"), defaults.wear),
        )


@dataclass(frozen=True)
class Attack:
    damage: float
    range: float
    arc: float
    cooldown: float
    knockback: float
    stagger: float
    wear: float
    stamina: float
    windup: float
    poise: float


@dataclass(frozen=True)
class Weapon:
    id: str
    name: str
    weight: float
    light: Attack
    heavy: Attack
    heavy_explicit: bool
    block_reduction: float
    block_stamina: float

    @property
    def block_damage_scale(self) -> float:
        return 1.0 - self.block_reduction


VARIANT_ORDER = ("普通", "快速", "强壮", "重甲")


def _variant_profiles(rules: Rules) -> dict[str, dict[str, float]]:
    return {
        "普通": {"health": 100.0, "poise": rules.poise_max_normal, "damage_reduction": 0.0},
        "快速": {"health": 70.0, "poise": rules.poise_max_fast, "damage_reduction": 0.0},
        "强壮": {"health": 170.0, "poise": rules.poise_max_strong, "damage_reduction": 0.0},
        "重甲": {
            "health": 200.0,
            "poise": rules.poise_max_armored,
            "damage_reduction": 0.35,
        },
    }


def _derive_heavy(light: Attack, weight: float, rules: Rules) -> Attack:
    return Attack(
        damage=light.damage * rules.heavy_damage_scale,
        range=light.range + rules.heavy_range_bonus,
        arc=light.arc * rules.heavy_arc_scale,
        cooldown=light.cooldown * rules.heavy_cooldown_scale,
        knockback=light.knockback * rules.heavy_knockback_scale,
        stagger=light.stagger * rules.heavy_stagger_scale,
        wear=light.wear * rules.heavy_wear_scale,
        stamina=_clamp(light.stamina * rules.heavy_stamina_scale, 8.0, 32.0),
        windup=_heavy_windup_for_weight(weight),
        poise=_clamp(light.poise * rules.heavy_poise_scale, 18.0, 45.0),
    )


def _resolve_attack_fields(source: dict[str, Any], fallback: Attack) -> Attack:
    return Attack(
        damage=_number(source.get("damage"), fallback.damage),
        range=_number(source.get("range"), fallback.range),
        arc=_number(source.get("arc"), fallback.arc),
        cooldown=_number(source.get("cooldown"), fallback.cooldown),
        knockback=_number(source.get("knockback"), fallback.knockback),
        stagger=_number(source.get("stagger"), fallback.stagger),
        wear=_number(source.get("wear"), fallback.wear),
        stamina=_positive_or(source.get("stamina"), fallback.stamina),
        windup=_positive_or(source.get("windup"), fallback.windup),
        poise=_positive_or(source.get("poise"), fallback.poise),
    )


def _resolve_weapon(item: dict[str, Any], base: BaseAttack, rules: Rules) -> Weapon | None:
    combat = item.get("combat")
    if not isinstance(combat, dict):
        return None
    weight = _number(item.get("weight"), 0.0)
    light_start = Attack(
        damage=base.damage,
        range=base.range,
        arc=base.arc,
        cooldown=base.cooldown,
        knockback=base.knockback,
        stagger=base.stagger,
        wear=base.wear,
        stamina=_light_stamina_for_weight(weight),
        windup=_light_windup_for_weight(weight),
        poise=_light_poise_for_weight(weight),
    )
    light = _resolve_attack_fields(combat, light_start)
    derived_heavy = _derive_heavy(light, weight, rules)
    heavy = derived_heavy
    heavy_data = combat.get("heavy")
    heavy_explicit = isinstance(heavy_data, dict)
    if heavy_explicit:
        heavy = _resolve_attack_fields(heavy_data, derived_heavy)
        heavy = Attack(
            damage=heavy.damage,
            range=heavy.range,
            arc=heavy.arc,
            cooldown=heavy.cooldown,
            knockback=heavy.knockback,
            stagger=heavy.stagger,
            wear=heavy.wear,
            stamina=_positive_or(heavy_data.get("stamina"), derived_heavy.stamina),
            windup=_positive_or(heavy_data.get("windup"), derived_heavy.windup),
            poise=_positive_or(heavy_data.get("poise"), derived_heavy.poise),
        )

    raw_reduction = combat.get("blockReduction")
    if isinstance(raw_reduction, (int, float)) and not isinstance(raw_reduction, bool):
        reduction = _clamp(float(raw_reduction), 0.0, 0.95) if math.isfinite(float(raw_reduction)) else None
    else:
        reduction = None
    block_reduction = (1.0 - rules.block_damage_scale) if reduction is None else reduction

    raw_block_stamina = combat.get("blockStamina")
    if (
        isinstance(raw_block_stamina, (int, float))
        and not isinstance(raw_block_stamina, bool)
        and math.isfinite(float(raw_block_stamina))
        and float(raw_block_stamina) >= 0.0
    ):
        block_stamina = _clamp(float(raw_block_stamina), 0.0, 1000.0)
    else:
        block_stamina = _clamp(
            rules.block_base_stamina_cost + weight * rules.block_stamina_per_weight,
            rules.block_min_stamina_cost,
            rules.block_max_stamina_cost,
        )

    return Weapon(
        id=str(item.get("id", "")),
        name=str(item.get("name", item.get("id", ""))),
        weight=weight,
        light=light,
        heavy=heavy,
        heavy_explicit=heavy_explicit,
        block_reduction=block_reduction,
        block_stamina=block_stamina,
    )


def _cycle_seconds(attack: Attack) -> float:
    return max(attack.cooldown, attack.windup + ATTACK_PRESENTATION_SECONDS)


def _attacks_before_exhaust(cost: float, rules: Rules) -> int:
    if cost <= 0.0:
        return 0
    current = rules.stamina_max
    count = 0
    while (
        current > rules.stamina_exhausted_threshold + EPSILON
        and current + EPSILON >= cost
    ):
        current -= cost
        count += 1
        if count > 100000:
            break
    return count


def _attack_metrics(attack: Attack, rules: Rules, variants: dict[str, dict[str, float]]) -> dict[str, Any]:
    cycle = _cycle_seconds(attack)
    attacks = _attacks_before_exhaust(attack.stamina, rules)
    exhaust_time = attack.windup + max(0, attacks - 1) * cycle if attacks else 0.0
    variant_metrics: dict[str, Any] = {}
    for label in VARIANT_ORDER:
        profile = variants[label]
        per_hit = attack.damage * (1.0 - profile["damage_reduction"])
        kills = _ceil_hits(profile["health"] / per_hit) if per_hit > 0.0 else 0
        poise_breaks = _ceil_hits(profile["poise"] / attack.poise) if attack.poise > 0.0 else 0
        variant_metrics[label] = {
            "hits_to_kill": kills,
            "ttk": attack.windup + max(0, kills - 1) * cycle if kills else None,
            "poise_break_hits": poise_breaks,
            "poise_break_time": attack.windup + max(0, poise_breaks - 1) * cycle if poise_breaks else None,
        }
    return {
        "cycle": cycle,
        "dps": attack.damage / cycle if cycle > 0.0 else 0.0,
        "damage_per_stamina": attack.damage / attack.stamina if attack.stamina > 0.0 else 0.0,
        "poise_per_second": attack.poise / cycle if cycle > 0.0 else 0.0,
        "attacks_before_exhaust": attacks,
        "exhaust_time": exhaust_time,
        "variants": variant_metrics,
    }


def _weapon_report(weapon: Weapon, rules: Rules, variants: dict[str, dict[str, float]]) -> dict[str, Any]:
    light_metrics = _attack_metrics(weapon.light, rules, variants)
    heavy_metrics = _attack_metrics(weapon.heavy, rules, variants)
    return {
        "id": weapon.id,
        "name": weapon.name,
        "weight": weapon.weight,
        "heavy_explicit": weapon.heavy_explicit,
        "light": asdict(weapon.light),
        "heavy": asdict(weapon.heavy),
        "block_damage_scale": weapon.block_damage_scale,
        "block_stamina": weapon.block_stamina,
        "metrics": {
            "light": light_metrics,
            "heavy": heavy_metrics,
            "heavy_dps_ratio": heavy_metrics["dps"] / light_metrics["dps"],
        },
    }


def _findings(rows: list[dict[str, Any]], rules: Rules) -> list[str]:
    top_light = max(rows, key=lambda row: row["metrics"]["light"]["dps"])
    top_heavy = max(rows, key=lambda row: row["metrics"]["heavy"]["dps"])
    top_efficiency = max(rows, key=lambda row: row["metrics"]["light"]["damage_per_stamina"])
    low_ratio = sorted(
        (row for row in rows if row["metrics"]["heavy_dps_ratio"] < 0.98),
        key=lambda row: row["metrics"]["heavy_dps_ratio"],
    )
    findings = [
        "轻击 DPS 最高："
        f"{top_light['name']} {top_light['metrics']['light']['dps']:.2f}；"
        f"重击 DPS 最高：{top_heavy['name']} {top_heavy['metrics']['heavy']['dps']:.2f}。",
        "轻击伤害/体力最高："
        f"{top_efficiency['name']} {top_efficiency['metrics']['light']['damage_per_stamina']:.2f}。",
    ]
    if low_ratio:
        detail = "、".join(
            f"{row['name']} {row['metrics']['heavy_dps_ratio']:.2f}" for row in low_ratio
        )
        findings.append(f"重击 DPS 低于轻击（换取破防/硬直）：{detail}。")
    else:
        findings.append("所有武器重击 DPS 均不低于轻击。")
    findings.append(
        "轻击硬直上限 "
        f"{max(row['light']['poise'] for row in rows):.2f}，"
        f"重击硬直下限 {min(row['heavy']['poise'] for row in rows):.2f}；"
        f"重甲轻击基础硬直阈值 {rules.armored_stagger_poise_threshold:.2f}。"
    )
    return findings


def _display_path(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def build_report(config_path: Path = DEFAULT_CONFIG, items_path: Path = DEFAULT_ITEMS) -> dict[str, Any]:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    items = json.loads(items_path.read_text(encoding="utf-8"))["items"]
    combat = config.get("combat", {})
    rules = Rules.from_mapping(combat.get("rules"))
    base = BaseAttack.from_mapping(combat)
    variants = _variant_profiles(rules)
    weapons = []
    for item in items:
        if item.get("type") != "weapon":
            continue
        weapon = _resolve_weapon(item, base, rules)
        if weapon is not None:
            weapons.append(weapon)
    rows = [_weapon_report(weapon, rules, variants) for weapon in weapons]
    if not rows:
        raise ValueError("assets/items.json 中没有可解析的近战武器")
    findings = _findings(rows, rules)
    return {
        "format": "m30-combat-balance-v1",
        "sources": {
            "config": _display_path(config_path),
            "items": _display_path(items_path),
        },
        "rules": asdict(rules),
        "variants": variants,
        "weapons": rows,
        "findings": findings,
    }


def _fmt_ttk(entry: dict[str, Any]) -> str:
    if not entry["hits_to_kill"]:
        return "—"
    return f"{entry['hits_to_kill']}击/{entry['ttk']:.2f}s"


def _fmt_break(entry: dict[str, Any]) -> str:
    if not entry["poise_break_hits"]:
        return "—"
    return f"{entry['poise_break_hits']}击/{entry['poise_break_time']:.2f}s"


def render_markdown(report: dict[str, Any]) -> str:
    rules = report["rules"]
    rows = report["weapons"]
    lines = [
        "# M30 逐武器战斗平衡基线",
        "",
        "> 由 `tools/combat_balance_report.py` 生成；数据源 "
        f"`{report['sources']['config']}`、`{report['sources']['items']}`。",
        "> 重新生成：`py -3 tools/combat_balance_report.py`。",
        "",
        "## 规则快照",
        "",
        f"- 体力：上限 {rules['stamina_max']:.0f}，耗尽阈值 "
        f"{rules['stamina_exhausted_threshold']:.0f}；重击伤害 ×{rules['heavy_damage_scale']:.2f}、"
        f"冷却 ×{rules['heavy_cooldown_scale']:.2f}、体力 ×{rules['heavy_stamina_scale']:.2f}、"
        f"硬直条 ×{rules['heavy_poise_scale']:.2f}。",
        f"- 硬直条：普通 {rules['poise_max_normal']:.0f} / 快速 {rules['poise_max_fast']:.0f} / "
        f"强壮 {rules['poise_max_strong']:.0f} / 重甲 {rules['poise_max_armored']:.0f}；"
        f"重甲轻击基础硬直阈值 {rules['armored_stagger_poise_threshold']:.0f}。",
        f"- 格挡：默认承伤 ×{rules['block_damage_scale']:.2f}；未配置武器体力 = "
        f"clamp({rules['block_base_stamina_cost']:.0f} + "
        f"{rules['block_stamina_per_weight']:.0f}×重量, "
        f"{rules['block_min_stamina_cost']:.0f}, {rules['block_max_stamina_cost']:.0f})。",
        "- 计量：攻击周期 = max(冷却, 前摇 + 0.28s)；TTK 从起手到第 N 次命中；不含移动、未命中、"
        "成长/疼痛/疲劳修正。",
        "",
        "## 1. 攻击参数",
        "",
        "| 武器 | 重量 | 轻伤害 | 轻范围 | 轻扇形 | 轻冷却 | 轻前摇 | 轻体力 | 轻硬直条 | 重伤害 | 重范围 | 重扇形 | 重冷却 | 重前摇 | 重体力 | 重硬直条 | 重击来源 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        light = row["light"]
        heavy = row["heavy"]
        source = "显式" if row["heavy_explicit"] else "派生"
        lines.append(
            f"| {row['name']} | {row['weight']:.2f} | {light['damage']:.0f} | "
            f"{light['range']:.2f} | {light['arc']:.0f} | {light['cooldown']:.2f} | "
            f"{light['windup']:.2f} | {light['stamina']:.2f} | {light['poise']:.2f} | "
            f"{heavy['damage']:.0f} | {heavy['range']:.2f} | {heavy['arc']:.0f} | "
            f"{heavy['cooldown']:.2f} | {heavy['windup']:.2f} | {heavy['stamina']:.2f} | "
            f"{heavy['poise']:.2f} | {source} |"
        )
    lines.extend(
        [
            "",
            "## 2. 输出与续航",
            "",
            "| 武器 | 轻 DPS | 重 DPS | 重/轻 DPS | 轻伤/体力 | 重伤/体力 | 满体力轻击 | 轻击耗尽 | 格挡承伤 | 格挡体力 |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in rows:
        light = row["metrics"]["light"]
        heavy = row["metrics"]["heavy"]
        lines.append(
            f"| {row['name']} | {light['dps']:.2f} | {heavy['dps']:.2f} | "
            f"{row['metrics']['heavy_dps_ratio']:.2f} | {light['damage_per_stamina']:.2f} | "
            f"{heavy['damage_per_stamina']:.2f} | {light['attacks_before_exhaust']} | "
            f"{light['exhaust_time']:.2f}s | {row['block_damage_scale']:.2f}× | "
            f"{row['block_stamina']:.2f} |"
        )
    lines.extend(
        [
            "",
            "## 3. 对僵尸变体",
            "",
            "| 武器 | 普通轻 TTK | 快速轻 TTK | 强壮轻 TTK | 重甲轻 TTK | 重甲重 TTK | 普通轻破防 | 快速轻破防 | 强壮轻破防 | 重甲轻破防 | 重甲重破防 |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in rows:
        light_variants = row["metrics"]["light"]["variants"]
        heavy_variants = row["metrics"]["heavy"]["variants"]
        lines.append(
            f"| {row['name']} | {_fmt_ttk(light_variants['普通'])} | "
            f"{_fmt_ttk(light_variants['快速'])} | {_fmt_ttk(light_variants['强壮'])} | "
            f"{_fmt_ttk(light_variants['重甲'])} | {_fmt_ttk(heavy_variants['重甲'])} | "
            f"{_fmt_break(light_variants['普通'])} | {_fmt_break(light_variants['快速'])} | "
            f"{_fmt_break(light_variants['强壮'])} | {_fmt_break(light_variants['重甲'])} | "
            f"{_fmt_break(heavy_variants['重甲'])} |"
        )
    lines.extend(
        [
            "",
            "## 4. 自动读数",
            "",
        ]
    )
    lines.extend(f"- {item}" for item in report["findings"])
    lines.extend(
        [
            "",
            "## 5. 读表边界",
            "",
            "- 重甲减伤按 35% 计入 TTK；重甲轻击在硬直条归零前不会产生基础硬直，表中“重甲轻破防”"
            "是硬直条归零的长硬直。",
            "- 重击来源为“显式”时使用物品 `combat.heavy` 覆盖；为“派生”时使用 `combat.rules.heavy*` "
            "与武器重量派生。",
            "- 本表是数据基线，不替代自然战斗试玩；格挡/弹反/闪避与联机权威仍以 `docs/14` 和烟测为准。",
            "",
        ]
    )
    return "\n".join(lines)


def _write_json(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _write_markdown(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="生成 M30 逐武器平衡基线")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--items", type=Path, default=DEFAULT_ITEMS)
    parser.add_argument("--markdown", type=Path, default=DEFAULT_MARKDOWN)
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--stdout", action="store_true")
    args = parser.parse_args(argv)
    report = build_report(args.config, args.items)
    text = render_markdown(report)
    if args.markdown is not None:
        _write_markdown(args.markdown, text)
    if args.json is not None:
        _write_json(args.json, report)
    if args.stdout or (args.markdown is None and args.json is None):
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
