# 内置 HUD 的脚本控制器：与 Lua 版共用 ui/hud.rml。
# 正常路径下由脚本加载/显示/更新/卸载；C++ 只保留 native fallback。

import math

DOC = "hud"
GROUP = "hud"


def set_bar(element, text_element, value, maximum, valid):
    ratio = 0.0
    if valid and maximum > 0.0:
        ratio = max(0.0, min(1.0, value / maximum))
    UI.SetProperty(DOC, element, "width", "%.1f%%" % (ratio * 100.0))
    if valid:
        UI.SetText(DOC, text_element, "%d" % math.floor(value + 0.5))
    else:
        UI.SetText(DOC, text_element, "--")
    return ratio


def refresh_mod_fields(s):
    fields = s.get("modFields", []) or []
    any_visible = False
    for i in range(8):
        field = fields[i] if i < len(fields) else None
        text = field.get("text", "") if field else ""
        label = field.get("label", "") if field else ""
        tone = field.get("tone", "info") if field else "info"
        row = "mod-field-%d" % i
        visible = text != ""
        UI.SetProperty(DOC, row, "display", "block" if visible else "none")
        UI.SetText(DOC, "mod-field-label-%d" % i, label)
        UI.SetText(DOC, "mod-field-value-%d" % i, text)
        UI.SetClass(DOC, row, "status-warn", visible and tone == "warn")
        UI.SetClass(DOC, row, "status-ok", visible and tone == "ok")
        UI.SetClass(DOC, row, "status-fail", visible and tone == "fail")
        if visible:
            any_visible = True
    UI.SetProperty(DOC, "mod-fields", "display",
                   "block" if any_visible else "none")


def refresh_world_event(s):
    visible = s.get("worldEventVisible", False)
    tone = s.get("worldEventTone", "info")
    UI.SetProperty(DOC, "world-event-panel", "display",
                   "block" if visible else "none")
    UI.SetClass(DOC, "world-event-panel", "status-warn", tone == "warn")
    UI.SetClass(DOC, "world-event-panel", "status-ok", tone == "ok")
    UI.SetClass(DOC, "world-event-panel", "status-fail", tone == "fail")
    if not visible:
        return
    UI.SetText(DOC, "world-event-meta", s.get("worldEventMeta", ""))
    UI.SetText(DOC, "world-event-title", s.get("worldEventName", ""))
    UI.SetText(DOC, "world-event-stage", s.get("worldEventStage", ""))
    hint = s.get("worldEventHint", "")
    UI.SetText(DOC, "world-event-hint", hint)
    UI.SetProperty(DOC, "world-event-hint", "display",
                   "block" if hint != "" else "none")
    result = s.get("worldEventResult", "")
    result_tone = s.get("worldEventResultTone", "ok")
    UI.SetText(DOC, "world-event-result", result)
    UI.SetProperty(DOC, "world-event-result", "display",
                   "block" if result != "" else "none")
    UI.SetClass(DOC, "world-event-result", "status-ok", result_tone == "ok")
    UI.SetClass(DOC, "world-event-result", "status-fail", result_tone == "fail")
    options = s.get("worldEventOptions", []) or []
    option_count = s.get("worldEventOptionCount", 0)
    can_choose = s.get("worldEventCanChoose", False)
    choice_open = s.get("worldEventChoiceOpen", False)
    any_option = can_choose and option_count > 0
    UI.SetProperty(DOC, "world-event-options", "display",
                   "block" if any_option else "none")
    for i in range(8):
        option = options[i] if i < len(options) else ""
        row = "world-event-option-%d" % i
        show = any_option and i < option_count
        UI.SetProperty(DOC, row, "display", "block" if show else "none")
        if show:
            UI.SetText(DOC, row, option)
            UI.SetClass(DOC, row, "active", choice_open)
    logs = s.get("worldEventLog", []) or []
    log_count = s.get("worldEventLogCount", 0)
    UI.SetProperty(DOC, "world-event-log", "display",
                   "block" if log_count > 0 else "none")
    for i in range(6):
        line = logs[i] if i < len(logs) else ""
        row = "world-event-log-%d" % i
        show = i < log_count
        UI.SetProperty(DOC, row, "display", "block" if show else "none")
        if show:
            UI.SetText(DOC, row, line)


def refresh():
    s = Hud.GetState()
    if not s["active"]:
        if UI.IsLoaded(DOC):
            UI.CloseDocument(DOC, True)
        return

    if not UI.IsLoaded(DOC):
        if not UI.LoadDocument(DOC, "ui/hud.rml", False, False, GROUP):
            Log.Error("core HUD 加载失败：ui/hud.rml")
            return
    if not Game.IsMenuOpen() and not Game.IsInventoryOpen():
        UI.Show(DOC, True)

    UI.SetText(DOC, "hud-day", s["day"])
    UI.SetText(DOC, "hud-time", s["time"])
    UI.SetText(DOC, "hud-speed", s["speed"])
    UI.SetClass(DOC, "hud-speed", "paused", s["speed"] == "暂停")

    hp_ratio = set_bar("hp-fill", "hp-text", s["health"], s["healthMax"], s["playerValid"])
    UI.SetClass(DOC, "hp-fill", "low", s["playerValid"] and hp_ratio < 0.3)
    set_bar("stamina-fill", "stamina-text", s["stamina"], s["staminaMax"],
            s["playerValid"])
    combat_status = s.get("combatStatus", "")
    combat_tone = s.get("combatTone", "info")
    UI.SetText(DOC, "combat-status", combat_status)
    UI.SetProperty(DOC, "combat-status", "display",
                   "block" if combat_status != "" else "none")
    UI.SetClass(DOC, "combat-status", "status-warn", combat_tone == "warn")
    UI.SetClass(DOC, "combat-status", "status-fail", combat_tone == "fail")
    stealth_status = s.get("stealthStatus", "")
    stealth_tone = s.get("stealthTone", "info")
    UI.SetText(DOC, "stealth-status", stealth_status)
    UI.SetProperty(DOC, "stealth-status", "display",
                   "block" if stealth_status != "" else "none")
    UI.SetClass(DOC, "stealth-status", "status-ok", stealth_tone == "ok")
    UI.SetClass(DOC, "stealth-status", "status-warn", stealth_tone == "warn")
    UI.SetClass(DOC, "stealth-status", "status-fail", stealth_tone == "fail")
    stealth_prompt = s.get("stealthPrompt", "")
    UI.SetText(DOC, "stealth-prompt", stealth_prompt)
    UI.SetProperty(DOC, "stealth-prompt", "display",
                   "block" if stealth_prompt != "" else "none")
    ranged_status = s.get("rangedStatus", "")
    ranged_tone = s.get("rangedTone", "info")
    UI.SetText(DOC, "ranged-status", ranged_status)
    UI.SetProperty(DOC, "ranged-status", "display",
                   "block" if ranged_status != "" else "none")
    UI.SetClass(DOC, "ranged-status", "status-warn", ranged_tone == "warn")
    UI.SetClass(DOC, "ranged-status", "status-fail", ranged_tone == "fail")
    throw_status = s.get("throwStatus", "")
    throw_tone = s.get("throwTone", "info")
    UI.SetText(DOC, "throw-status", throw_status)
    UI.SetProperty(DOC, "throw-status", "display",
                   "block" if throw_status != "" else "none")
    UI.SetClass(DOC, "throw-status", "status-ok", throw_tone == "ok")
    UI.SetClass(DOC, "throw-status", "status-warn", throw_tone == "warn")
    UI.SetClass(DOC, "throw-status", "status-fail", throw_tone == "fail")
    forage_status = s.get("forageStatus", "")
    forage_tone = s.get("forageTone", "info")
    UI.SetText(DOC, "forage-status", forage_status)
    UI.SetProperty(DOC, "forage-status", "display",
                   "block" if forage_status != "" else "none")
    UI.SetClass(DOC, "forage-status", "status-warn", forage_tone == "warn")
    UI.SetClass(DOC, "forage-status", "status-fail", forage_tone == "fail")
    fishing_status = s.get("fishingStatus", "")
    fishing_tone = s.get("fishingTone", "info")
    UI.SetText(DOC, "fishing-status", fishing_status)
    UI.SetProperty(DOC, "fishing-status", "display",
                   "block" if fishing_status != "" else "none")
    UI.SetClass(DOC, "fishing-status", "status-warn", fishing_tone == "warn")
    UI.SetClass(DOC, "fishing-status", "status-fail", fishing_tone == "fail")
    UI.SetText(DOC, "needs-status", s["needsStatus"])
    UI.SetProperty(DOC, "needs-status", "display",
                   "block" if s["needsStatus"] != "" else "none")
    UI.SetClass(DOC, "needs-status", "status-warn", s["needsTone"] == "warn")
    UI.SetClass(DOC, "needs-status", "status-fail", s["needsTone"] == "fail")
    UI.SetText(DOC, "wound-status", s["woundStatus"])
    UI.SetProperty(DOC, "wound-status", "display",
                   "block" if s["woundStatus"] != "" else "none")
    UI.SetClass(DOC, "wound-status", "status-warn", s["woundTone"] == "warn")
    UI.SetClass(DOC, "wound-status", "status-fail", s["woundTone"] == "fail")
    UI.SetText(DOC, "pain-status", s["painStatus"])
    UI.SetProperty(DOC, "pain-status", "display",
                   "block" if s["painStatus"] != "" else "none")
    UI.SetClass(DOC, "pain-status", "status-warn", s["painTone"] == "warn")
    UI.SetClass(DOC, "pain-status", "status-fail", s["painTone"] == "fail")
    armor_status = s.get("armorStatus", "")
    armor_tone = s.get("armorTone", "info")
    UI.SetText(DOC, "armor-status", armor_status)
    UI.SetProperty(DOC, "armor-status", "display",
                   "block" if armor_status != "" else "none")
    UI.SetClass(DOC, "armor-status", "status-warn", armor_tone == "warn")
    UI.SetClass(DOC, "armor-status", "status-fail", armor_tone == "fail")
    infection_status = s.get("infectionStatus", "")
    infection_tone = s.get("infectionTone", "info")
    UI.SetText(DOC, "infection-status", infection_status)
    UI.SetProperty(DOC, "infection-status", "display",
                   "block" if infection_status != "" else "none")
    UI.SetClass(DOC, "infection-status", "status-warn", infection_tone == "warn")
    UI.SetClass(DOC, "infection-status", "status-fail", infection_tone == "fail")
    weather_status = s.get("weatherStatus", "")
    weather_tone = s.get("weatherTone", "info")
    weather_class = s.get("weatherClass", "clear")
    UI.SetText(DOC, "weather-status", weather_status)
    UI.SetProperty(DOC, "weather-status", "display",
                   "block" if weather_status != "" else "none")
    UI.SetClass(DOC, "weather-status", "status-warn", weather_tone == "warn")
    UI.SetClass(DOC, "weather-status", "status-fail", weather_tone == "fail")
    UI.SetClass(DOC, "weather-overlay", "weather-rain", weather_class == "rain")
    UI.SetClass(DOC, "weather-overlay", "weather-fog", weather_class == "fog")
    UI.SetClass(DOC, "weather-overlay", "weather-cold", weather_class == "cold")
    UI.SetProperty(DOC, "weather-overlay", "display",
                   "block" if weather_class != "clear" else "none")

    threat_active = s["threatDirection"] != "" and s["threatRemaining"] > 0.0
    threat_opacity = 1.0
    if s["threatDuration"] > 0.0:
        threat_opacity = max(0.0, min(1.0, s["threatRemaining"] / s["threatDuration"]))
    for direction in ("left", "right", "top", "bottom"):
        edge_active = threat_active and s["threatDirection"] == direction
        UI.SetProperty(DOC, "threat-" + direction, "display",
                       "block" if edge_active else "none")
        if edge_active:
            UI.SetProperty(DOC, "threat-" + direction, "opacity",
                           "%.2f" % threat_opacity)
    UI.SetText(DOC, "threat-status", s["threatStatus"])
    UI.SetProperty(DOC, "threat-status", "display",
                   "block" if s["threatStatus"] != "" else "none")

    UI.SetText(DOC, "objective-text", s["objective"])
    UI.SetProperty(DOC, "hud-objective", "display",
                   "block" if (s["objective"] != ""
                               or s.get("poiStageVisible", False)
                               or s.get("missionGuide", "") != ""
                               or s.get("eventGuide", "") != ""
                               or s.get("regionGuide", "") != ""
                               or s.get("settlementGuide", "") != ""
                               or s.get("strategicGuide", "") != "") else "none")
    UI.SetClass(DOC, "objective-text", "objective-warn", s["objectiveTone"] == "warn")
    UI.SetClass(DOC, "objective-text", "objective-ok", s["objectiveTone"] == "ok")
    UI.SetText(DOC, "objective-hint", s["objectiveHint"])
    UI.SetProperty(DOC, "objective-hint", "display",
                   "block" if s["objectiveHint"] != "" else "none")
    poi_guide = s.get("poiGuide", "")
    poi_tone = s.get("poiGuideTone", "info")
    UI.SetText(DOC, "poi-guide", poi_guide)
    UI.SetProperty(DOC, "poi-guide", "display",
                   "block" if poi_guide != "" else "none")
    UI.SetClass(DOC, "poi-guide", "status-warn", poi_tone == "warn")
    UI.SetClass(DOC, "poi-guide", "status-ok", poi_tone == "ok")
    poi_stage_visible = s.get("poiStageVisible", False)
    poi_stage_tone = s.get("poiStageTone", "info")
    UI.SetProperty(DOC, "poi-stage-panel", "display",
                   "block" if poi_stage_visible else "none")
    UI.SetClass(DOC, "poi-stage-panel", "status-warn",
                poi_stage_tone == "warn")
    UI.SetClass(DOC, "poi-stage-panel", "status-fail",
                poi_stage_tone == "fail")
    UI.SetClass(DOC, "poi-stage-panel", "status-ok",
                poi_stage_tone == "ok")
    UI.SetText(DOC, "poi-stage-title", s.get("poiStageTitle", ""))
    UI.SetText(DOC, "poi-stage-objective",
               s.get("poiStageObjective", ""))
    UI.SetText(DOC, "poi-stage-meta", s.get("poiStageMeta", ""))
    poi_stage_narrative = s.get("poiStageNarrative", "")
    UI.SetText(DOC, "poi-stage-narrative", poi_stage_narrative)
    UI.SetProperty(DOC, "poi-stage-narrative", "display",
                   "block" if poi_stage_narrative != "" else "none")
    poi_result_visible = s.get("poiResultVisible", False)
    poi_result_tone = s.get("poiResultTone", "info")
    UI.SetProperty(DOC, "poi-result-panel", "display",
                   "block" if poi_result_visible else "none")
    UI.SetClass(DOC, "poi-result-panel", "status-fail",
                poi_result_tone == "fail")
    UI.SetText(DOC, "poi-result-title", s.get("poiResultTitle", ""))
    UI.SetText(DOC, "poi-result-body", s.get("poiResultBody", ""))
    mission_guide = s.get("missionGuide", "")
    mission_tone = s.get("missionTone", "info")
    UI.SetText(DOC, "mission-guide", mission_guide)
    UI.SetProperty(DOC, "mission-guide", "display",
                   "block" if mission_guide != "" else "none")
    UI.SetClass(DOC, "mission-guide", "status-warn", mission_tone == "warn")
    UI.SetClass(DOC, "mission-guide", "status-ok", mission_tone == "ok")
    event_guide = s.get("eventGuide", "")
    event_tone = s.get("eventTone", "warn")
    UI.SetText(DOC, "event-guide", event_guide)
    UI.SetProperty(DOC, "event-guide", "display",
                   "block" if event_guide != "" else "none")
    UI.SetClass(DOC, "event-guide", "status-warn", event_tone == "warn")
    UI.SetClass(DOC, "event-guide", "status-ok", event_tone == "ok")
    UI.SetClass(DOC, "event-guide", "status-fail", event_tone == "fail")
    region_guide = s.get("regionGuide", "")
    region_tone = s.get("regionTone", "info")
    UI.SetText(DOC, "region-guide", region_guide)
    UI.SetProperty(DOC, "region-guide", "display",
                   "block" if region_guide != "" else "none")
    UI.SetClass(DOC, "region-guide", "status-warn", region_tone == "warn")
    UI.SetClass(DOC, "region-guide", "status-ok", region_tone == "ok")
    settlement_guide = s.get("settlementGuide", "")
    settlement_tone = s.get("settlementTone", "info")
    UI.SetText(DOC, "settlement-guide", settlement_guide)
    UI.SetProperty(DOC, "settlement-guide", "display",
                   "block" if settlement_guide != "" else "none")
    UI.SetClass(DOC, "settlement-guide", "status-warn",
                settlement_tone == "warn")
    UI.SetClass(DOC, "settlement-guide", "status-fail",
                settlement_tone == "fail")
    UI.SetClass(DOC, "settlement-guide", "status-ok",
                settlement_tone == "ok")
    strategic_guide = s.get("strategicGuide", "")
    strategic_tone = s.get("strategicTone", "info")
    UI.SetText(DOC, "strategic-guide", strategic_guide)
    UI.SetProperty(DOC, "strategic-guide", "display",
                   "block" if strategic_guide != "" else "none")
    UI.SetClass(DOC, "strategic-guide", "status-warn",
                strategic_tone == "warn")
    UI.SetClass(DOC, "strategic-guide", "status-fail",
                strategic_tone == "fail")

    UI.SetText(DOC, "siege-status", s["siegeStatus"])
    UI.SetProperty(DOC, "siege-status", "display",
                   "block" if s["siegeStatus"] != "" else "none")
    UI.SetClass(DOC, "siege-status", "status-warn", s["siegeTone"] == "warn")
    UI.SetClass(DOC, "siege-status", "status-fail", s["siegeTone"] == "fail")

    base_status = s.get("baseStatus", "")
    base_tone = s.get("baseTone", "info")
    UI.SetText(DOC, "base-status", base_status)
    UI.SetProperty(DOC, "base-status", "display",
                   "block" if base_status != "" else "none")
    UI.SetClass(DOC, "base-status", "status-warn", base_tone == "warn")
    UI.SetClass(DOC, "base-status", "status-fail", base_tone == "fail")
    refresh_mod_fields(s)
    refresh_world_event(s)

    report_open = s.get("siegeReportOpen", False)
    report_tone = s.get("siegeReportTone", "info")
    UI.SetProperty(DOC, "siege-report", "display",
                   "block" if report_open else "none")
    UI.SetClass(DOC, "siege-report", "status-warn", report_tone == "warn")
    UI.SetClass(DOC, "siege-report", "status-fail", report_tone == "fail")
    UI.SetText(DOC, "siege-report-title",
               s.get("siegeReportTitle", "守夜战报"))
    report_rating = s.get("siegeReportRating", "")
    UI.SetText(DOC, "siege-report-rating", report_rating)
    UI.SetProperty(DOC, "siege-report-rating", "display",
                   "block" if report_rating else "none")
    UI.SetText(DOC, "siege-report-hint",
               s.get("siegeReportHint", "ESC 关闭"))
    report_lines = s.get("siegeReportLines", [])
    for i in range(8):
        line = report_lines[i] if i < len(report_lines) else ""
        UI.SetText(DOC, "siege-report-line-%d" % i, line)
        UI.SetProperty(DOC, "siege-report-line-%d" % i, "display",
                       "block" if line else "none")

    progression_valid = s.get("progressionValid", False)
    progression_level = s.get("progressionLevel", 1)
    progression_xp = s.get("progressionXp", 0.0)
    progression_next = s.get("progressionXpNext", 0.0)
    progression_points = s.get("progressionPerkPoints", 0)
    progression_ratio = 0.0
    if progression_valid:
        progression_ratio = (max(0.0, min(1.0, progression_xp / progression_next))
                             if progression_next > 0.0 else 1.0)
    UI.SetProperty(DOC, "hud-level", "display",
                   "block" if progression_valid else "none")
    UI.SetProperty(DOC, "level-fill", "width", "%.1f%%" % (progression_ratio * 100.0))
    UI.SetText(DOC, "level-text", "Lv.%d" % progression_level)
    UI.SetText(DOC, "level-perk", "Perk×%d" % progression_points)
    UI.SetProperty(DOC, "level-perk", "display",
                   "block" if progression_points > 0 else "none")
    if progression_next > 0.0:
        UI.SetText(DOC, "level-xp", "%d / %d" % (math.floor(progression_xp + 0.5),
                                                  math.floor(progression_next + 0.5)))
    else:
        UI.SetText(DOC, "level-xp", "MAX")
    perk_prompt = s.get("perkPrompt", "")
    UI.SetText(DOC, "perk-prompt", perk_prompt)
    UI.SetProperty(DOC, "perk-prompt", "display",
                   "block" if perk_prompt else "none")
    UI.SetProperty(DOC, "character-panel", "display",
                   "block" if progression_valid and s.get("characterOpen", False)
                   else "none")
    if progression_valid:
        points_suffix = (" · 待选 Perk ×%d" % progression_points
                         if progression_points > 0 else "")
        UI.SetText(DOC, "character-level",
                   "等级 %d%s" % (progression_level, points_suffix))
        if progression_next > 0.0:
            UI.SetText(DOC, "character-xp",
                       "XP %d / %d" % (math.floor(progression_xp + 0.5),
                                       math.floor(progression_next + 0.5)))
        else:
            UI.SetText(DOC, "character-xp", "XP MAX")
        UI.SetProperty(DOC, "character-xp-fill", "width",
                       "%.1f%%" % (progression_ratio * 100.0))
        skills = s.get("progressionSkills", [])
        for i in range(4):
            UI.SetText(DOC, "character-skill-%d" % i,
                       skills[i] if i < len(skills) else "")
        UI.SetText(DOC, "character-perks", s.get("progressionPerks", "暂无"))
        choices = s.get("progressionChoices", [])
        for i in range(3):
            line = choices[i] if i < len(choices) else ""
            UI.SetText(DOC, "perk-choice-%d" % i, line)
            UI.SetProperty(DOC, "perk-choice-%d" % i, "display",
                           "block" if line else "none")

    companion = s.get("companion", {})
    companion_valid = companion.get("valid", False)
    companion_open = companion_valid and companion.get("open", False)
    UI.SetProperty(DOC, "companion-panel", "display",
                   "block" if companion_open else "none")
    UI.SetText(DOC, "companion-name", companion.get("name", ""))
    UI.SetText(DOC, "companion-health", companion.get("health", ""))
    UI.SetText(DOC, "companion-state", companion.get("status", ""))
    UI.SetText(DOC, "companion-trust", companion.get("trust", ""))
    UI.SetText(DOC, "companion-equipment", companion.get("equipment", ""))
    UI.SetText(DOC, "companion-bag", companion.get("bag", ""))
    UI.SetText(DOC, "companion-quest", companion.get("quest", ""))
    UI.SetText(DOC, "companion-hint", companion.get("hint", ""))
    companion_health_max = companion.get("healthMax", 0.0)
    companion_health_ratio = 0.0
    if companion_health_max > 0.0:
        companion_health_ratio = max(0.0, min(
            1.0, companion.get("healthCurrent", 0.0) / companion_health_max))
    UI.SetProperty(DOC, "companion-health-fill", "width",
                   "%.1f%%" % (companion_health_ratio * 100.0))
    UI.SetClass(DOC, "companion-health-fill", "low",
                companion.get("lowHealth", False) or
                companion.get("downed", False) or companion.get("dead", False))
    UI.SetText(DOC, "companion-status", companion.get("alert", ""))
    UI.SetProperty(DOC, "companion-status", "display",
                   "block" if companion_valid and companion.get("alert", "") else "none")
    alert_tone = companion.get("alertTone", "info")
    UI.SetClass(DOC, "companion-status", "status-warn", alert_tone == "warn")
    UI.SetClass(DOC, "companion-status", "status-fail", alert_tone == "fail")
    UI.SetClass(DOC, "companion-status", "status-ok", alert_tone == "ok")

    dialogue_open = s.get("dialogueOpen", False)
    dialogue_options = s.get("dialogueOptions", [])
    dialogue_count = s.get("dialogueOptionCount", 0)
    dialogue_selected = s.get("dialogueSelected", 0)
    UI.SetProperty(DOC, "dialogue-panel", "display",
                   "block" if dialogue_open else "none")
    UI.SetText(DOC, "dialogue-speaker", s.get("dialogueSpeaker", ""))
    UI.SetText(DOC, "dialogue-text", s.get("dialogueText", ""))
    UI.SetText(DOC, "dialogue-hint", s.get("dialogueHint", ""))
    for i in range(5):
        option = dialogue_options[i] if i < len(dialogue_options) else ""
        UI.SetText(DOC, "dialogue-option-%d" % i, option)
        UI.SetProperty(DOC, "dialogue-option-%d" % i, "display",
                       "block" if dialogue_open and i < dialogue_count else "none")
        UI.SetClass(DOC, "dialogue-option-%d" % i, "selected",
                    dialogue_open and i == dialogue_selected)

    UI.SetText(DOC, "action-hint", s["actionHint"])
    UI.SetProperty(DOC, "action-hint", "display",
                   "block" if s["actionHint"] != "" else "none")
    UI.SetClass(DOC, "action-hint", "status-warn", s["actionTone"] == "warn")
    UI.SetClass(DOC, "action-hint", "status-fail", s["actionTone"] == "fail")

    for i in range(5):
        slot = s["slots"][i]
        UI.SetClass(DOC, "slot-%d" % i, "selected", s["selectedSlot"] == i)
        UI.SetText(DOC, "slot-name-%d" % i, slot["label"] or "空")
        count = slot["count"]
        UI.SetText(DOC, "slot-count-%d" % i, str(count) if count > 0 else "")
        throwable = slot.get("throwable", False)
        throw_tag = slot.get("throwTag", "")
        UI.SetClass(DOC, "slot-%d" % i, "throwable", throwable)
        UI.SetText(DOC, "slot-throw-%d" % i, throw_tag)
        UI.SetProperty(DOC, "slot-throw-%d" % i, "display",
                       "block" if throwable and throw_tag else "none")

    notice_active = s["notice"] != "" and s["noticeRemaining"] > 0.0
    UI.SetProperty(DOC, "notice", "display", "block" if notice_active else "none")
    if notice_active:
        UI.SetText(DOC, "notice", s["notice"])
        UI.SetClass(DOC, "notice", "notice-info", s["noticeTone"] == "info")
        UI.SetClass(DOC, "notice", "notice-ok", s["noticeTone"] == "ok")
        UI.SetClass(DOC, "notice", "notice-fail", s["noticeTone"] == "fail")
        duration = max(s["noticeDuration"], 0.05)
        elapsed = max(0.0, duration - s["noticeRemaining"])
        rise = min(elapsed * 30.0, 30.0)
        opacity = min(1.0, s["noticeRemaining"] / 0.35)
        UI.SetProperty(DOC, "notice", "bottom", "%.0fpx" % (116.0 + rise))
        UI.SetProperty(DOC, "notice", "opacity", "%.2f" % opacity)


def on_ui_event(e):
    if (e["modId"] == "core_ui" and e["document"] == DOC and
            e["element"] == "hud-bag" and e["type"] == "click"):
        Game.ToggleInventory()


def on_mod_reload(e):
    if e["modId"] == "core_ui":
        refresh()


Events.On("OnUiFrame", lambda _: refresh())
Events.On("OnUiEvent", on_ui_event)
Events.On("OnModReload", on_mod_reload)


# 内置主菜单/暂停菜单控制器：与 Lua 版共用 ui/menu.rml。
# C++ 只负责菜单开关状态、动作回调和设置保存；页面切换/改键采集在脚本内完成。

MENU_DOC = "menu"
MENU_GROUP = "menu"
MAIN_PAGE = "main-menu"
PAUSE_PAGE = "pause-menu"
SETTINGS_PAGE = "settings-menu"
LOAD_PAGE = "load-menu"
SAVE_ROWS = 8

menu_state = {
    "open": False,
    "pause": False,
    "canContinue": False,
    "page": MAIN_PAGE,
    "returnPage": MAIN_PAGE,
    "volume": 65,
    "musicVolume": 70,
    "sfxVolume": 85,
    "ambientVolume": 55,
    "bindings": [],
    "rebind": -1,
    "saves": [],
    "savePage": 0,
    "pendingDelete": None,
}


def copy_bindings(source):
    return [{"action": item["action"], "label": item["label"], "key": item["key"]}
            for item in source]


def read_menu_state():
    s = Menu.GetState()
    menu_state["open"] = s["open"]
    menu_state["pause"] = s["pauseMode"]
    menu_state["canContinue"] = s["canContinue"]
    menu_state["volume"] = s["volume"]
    menu_state["musicVolume"] = s["musicVolume"]
    menu_state["sfxVolume"] = s["sfxVolume"]
    menu_state["ambientVolume"] = s["ambientVolume"]
    menu_state["bindings"] = copy_bindings(s["bindings"])


def set_menu_status(text, tone):
    UI.SetText(MENU_DOC, "settings-status", text)
    UI.SetClass(MENU_DOC, "settings-status", "status-info", tone == "info")
    UI.SetClass(MENU_DOC, "settings-status", "status-ok", tone == "ok")
    UI.SetClass(MENU_DOC, "settings-status", "status-fail", tone == "fail")


def set_load_status(text, tone=""):
    UI.SetText(MENU_DOC, "load-status", text)
    UI.SetClass(MENU_DOC, "load-status", "status-ok", tone == "ok")
    UI.SetClass(MENU_DOC, "load-status", "status-fail", tone == "fail")


def set_pause_status(text):
    UI.SetText(MENU_DOC, "pause-save-status", text)


def save_page_count():
    return max(1, int(math.ceil(len(menu_state["saves"]) / float(SAVE_ROWS))))


def save_global_index(index):
    return menu_state["savePage"] * SAVE_ROWS + index


def refresh_continue():
    result = Save.List()
    menu_state["saves"] = result["saves"] if result["ok"] else []
    menu_state["canContinue"] = len(menu_state["saves"]) > 0
    pages = save_page_count()
    menu_state["savePage"] = max(0, min(menu_state["savePage"], pages - 1))
    start = menu_state["savePage"] * SAVE_ROWS + 1
    UI.SetClass(MENU_DOC, "menu-continue", "disabled",
                not menu_state["canContinue"])
    UI.SetText(MENU_DOC, "menu-continue",
               "继续" if menu_state["canContinue"] else "继续（暂无存档）")
    UI.SetClass(MENU_DOC, "menu-load", "disabled",
                not menu_state["canContinue"])
    for i in range(1, SAVE_ROWS + 1):
        row = "save-row-%d" % i
        item = menu_state["saves"][start + i - 2] \
            if start + i - 1 <= len(menu_state["saves"]) else None
        UI.SetProperty(MENU_DOC, row, "display", "block" if item else "none")
        if item is not None:
            detail = item["updatedText"] or item["id"]
            if item["mapId"]:
                detail = detail + " · " + item["mapId"]
            UI.SetText(MENU_DOC, "save-name-%d" % i, item["name"])
            UI.SetText(MENU_DOC, "save-time-%d" % i, detail)
    UI.SetProperty(MENU_DOC, "load-empty", "display",
                   "block" if not menu_state["saves"] else "none")
    UI.SetProperty(MENU_DOC, "load-more", "display",
                   "block" if len(menu_state["saves"]) > SAVE_ROWS else "none")
    UI.SetText(MENU_DOC, "load-more", "共 %d 个存档" % len(menu_state["saves"]))
    UI.SetProperty(MENU_DOC, "load-pager", "display",
                   "block" if pages > 1 else "none")
    UI.SetText(MENU_DOC, "load-page",
               "第 %d / %d 页" % (menu_state["savePage"] + 1, pages))
    UI.SetClass(MENU_DOC, "load-prev", "disabled",
                menu_state["savePage"] <= 0)
    UI.SetClass(MENU_DOC, "load-next", "disabled",
                menu_state["savePage"] >= pages - 1)


def change_save_page(delta):
    pages = save_page_count()
    next_page = max(0, min(menu_state["savePage"] + delta, pages - 1))
    if next_page == menu_state["savePage"]:
        return
    menu_state["savePage"] = next_page
    refresh_continue()


def refresh_key_buttons():
    for i, binding in enumerate(menu_state["bindings"], start=1):
        UI.SetText(MENU_DOC, "bind-" + binding["action"], binding["key"])
        UI.SetClass(MENU_DOC, "bind-" + binding["action"], "capturing",
                    menu_state["rebind"] == i)


def show_menu_page(page):
    menu_state["page"] = page
    UI.SetProperty(MENU_DOC, MAIN_PAGE, "display",
                   "block" if page == MAIN_PAGE else "none")
    UI.SetProperty(MENU_DOC, PAUSE_PAGE, "display",
                   "block" if page == PAUSE_PAGE else "none")
    UI.SetProperty(MENU_DOC, SETTINGS_PAGE, "display",
                   "block" if page == SETTINGS_PAGE else "none")
    UI.SetProperty(MENU_DOC, LOAD_PAGE, "display",
                   "block" if page == LOAD_PAGE else "none")
    if page != LOAD_PAGE:
        menu_state["pendingDelete"] = None
        UI.SetProperty(MENU_DOC, "load-delete-confirm", "display", "none")


def open_load():
    if not menu_state["saves"]:
        return
    menu_state["savePage"] = 0
    menu_state["pendingDelete"] = None
    set_load_status("")
    UI.SetProperty(MENU_DOC, "load-delete-confirm", "display", "none")
    refresh_continue()
    show_menu_page(LOAD_PAGE)


def load_save(index):
    global_index = save_global_index(index)
    if global_index < 1 or global_index > len(menu_state["saves"]):
        return
    result = Save.Load(menu_state["saves"][global_index - 1]["id"])
    if not result["ok"]:
        set_load_status(result["error"] or "读取存档失败", "fail")


def hide_delete_confirm():
    menu_state["pendingDelete"] = None
    UI.SetProperty(MENU_DOC, "load-delete-confirm", "display", "none")


def ask_delete(index):
    global_index = save_global_index(index)
    if global_index < 1 or global_index > len(menu_state["saves"]):
        return
    item = menu_state["saves"][global_index - 1]
    menu_state["pendingDelete"] = {"id": item["id"], "name": item["name"]}
    UI.SetText(MENU_DOC, "load-delete-text",
               "确定删除存档 “" + item["name"] + "”？")
    UI.SetProperty(MENU_DOC, "load-delete-confirm", "display", "block")


def confirm_delete():
    pending = menu_state["pendingDelete"]
    if pending is None:
        return
    result = Save.Delete(pending["id"])
    hide_delete_confirm()
    if result["ok"]:
        set_load_status("已删除存档：" + pending["name"], "ok")
    else:
        set_load_status(result["error"] or "删除存档失败", "fail")
    refresh_continue()


def refresh_settings_fields():
    fields = [
        ("setting-volume", "setting-volume-value", menu_state["volume"]),
        ("setting-music-volume", "setting-music-volume-value",
         menu_state["musicVolume"]),
        ("setting-sfx-volume", "setting-sfx-volume-value", menu_state["sfxVolume"]),
        ("setting-ambient-volume", "setting-ambient-volume-value",
         menu_state["ambientVolume"]),
    ]
    for element_id, label_id, value in fields:
        value = max(0, min(100, int(value)))
        UI.SetValue(MENU_DOC, element_id, str(value))
        UI.SetText(MENU_DOC, label_id, str(value))
    refresh_key_buttons()


def open_settings():
    menu_state["returnPage"] = menu_state["page"]
    menu_state["rebind"] = -1
    read_menu_state()
    show_menu_page(SETTINGS_PAGE)
    refresh_settings_fields()
    set_menu_status("键位与音量修改后按“应用”保存", "info")


def close_settings():
    menu_state["rebind"] = -1
    show_menu_page(menu_state["returnPage"])


def begin_rebind(index):
    if menu_state["page"] != SETTINGS_PAGE or index < 1 or index > len(
            menu_state["bindings"]):
        return
    menu_state["rebind"] = index
    refresh_key_buttons()
    set_menu_status("请按下新按键，ESC 取消", "info")


def cancel_rebind():
    if menu_state["rebind"] < 0:
        return
    menu_state["rebind"] = -1
    refresh_key_buttons()
    set_menu_status("已取消改键", "info")


def read_volume(element_id, fallback):
    try:
        value = float(UI.GetValue(MENU_DOC, element_id))
    except (TypeError, ValueError):
        return fallback
    return max(0, min(100, int(value + 0.5)))


def apply_menu_settings():
    if menu_state["rebind"] >= 0:
        set_menu_status("请先完成改键，或按 ESC 取消", "fail")
        return
    menu_state["volume"] = read_volume("setting-volume", menu_state["volume"])
    menu_state["musicVolume"] = read_volume("setting-music-volume",
                                            menu_state["musicVolume"])
    menu_state["sfxVolume"] = read_volume("setting-sfx-volume",
                                          menu_state["sfxVolume"])
    menu_state["ambientVolume"] = read_volume("setting-ambient-volume",
                                              menu_state["ambientVolume"])
    result = Menu.ApplySettings({
        "volume": menu_state["volume"],
        "musicVolume": menu_state["musicVolume"],
        "sfxVolume": menu_state["sfxVolume"],
        "ambientVolume": menu_state["ambientVolume"],
        "bindings": copy_bindings(menu_state["bindings"]),
    })
    if result["ok"]:
        read_menu_state()
        refresh_settings_fields()
        set_menu_status("设置已保存", "ok")
    else:
        set_menu_status(result["error"] or "保存设置失败", "fail")


def show_menu():
    if not UI.IsLoaded(MENU_DOC):
        if not UI.LoadDocument(MENU_DOC, "ui/menu.rml", False, True, MENU_GROUP):
            Log.Error("core menu 加载失败：ui/menu.rml")
            return False
    UI.Show(MENU_DOC, True)
    return True


def sync_menu():
    read_menu_state()
    if not menu_state["open"]:
        if UI.IsLoaded(MENU_DOC):
            UI.CloseDocument(MENU_DOC, False)
        menu_state["rebind"] = -1
        return
    if not show_menu():
        return
    menu_state["rebind"] = -1
    menu_state["page"] = PAUSE_PAGE if menu_state["pause"] else MAIN_PAGE
    menu_state["returnPage"] = menu_state["page"]
    refresh_continue()
    set_pause_status("")
    show_menu_page(menu_state["page"])


def save_row_index(element):
    for prefix in ("save-row-", "save-name-", "save-time-"):
        if element.startswith(prefix):
            try:
                return int(element[len(prefix):])
            except ValueError:
                return 0
    return 0


def save_delete_index(element):
    prefix = "save-delete-"
    if not element.startswith(prefix):
        return 0
    try:
        return int(element[len(prefix):])
    except ValueError:
        return 0


def handle_menu_click(element):
    if element == "menu-new-game":
        Menu.Action("new_game")
    elif element == "menu-multiplayer":
        Menu.Action("multiplayer")
    elif element == "menu-continue" and menu_state["canContinue"]:
        Menu.Action("continue")
    elif element == "menu-load" and menu_state["canContinue"]:
        open_load()
    elif element == "pause-save":
        if Save.QuickSave():
            set_pause_status("正在保存并返回游戏…")
            Menu.Action("resume")
        else:
            set_pause_status("存档服务忙或不可用")
    elif element == "load-back":
        show_menu_page(MAIN_PAGE)
    elif element == "load-prev":
        change_save_page(-1)
    elif element == "load-next":
        change_save_page(1)
    elif element == "load-delete-cancel":
        hide_delete_confirm()
    elif element == "load-delete-yes":
        confirm_delete()
    elif save_delete_index(element) > 0:
        ask_delete(save_delete_index(element))
    elif save_row_index(element) > 0:
        load_save(save_row_index(element))
    elif element in ("menu-settings", "pause-settings"):
        open_settings()
    elif element in ("menu-quit", "pause-quit"):
        Menu.Action("quit")
    elif element == "pause-resume":
        Menu.Action("resume")
    elif element == "pause-main-menu":
        Menu.Action("return_to_main")
    elif element == "settings-back":
        close_settings()
    elif element == "settings-apply":
        apply_menu_settings()
    elif element.startswith("bind-"):
        action = element[5:]
        for i, binding in enumerate(menu_state["bindings"], start=1):
            if binding["action"] == action:
                begin_rebind(i)
                return


def handle_volume_change(element):
    fields = {
        "setting-volume": "setting-volume-value",
        "setting-music-volume": "setting-music-volume-value",
        "setting-sfx-volume": "setting-sfx-volume-value",
        "setting-ambient-volume": "setting-ambient-volume-value",
    }
    label = fields.get(element)
    if label is not None:
        value = read_volume(element, 0)
        UI.SetText(MENU_DOC, label, str(value))


def on_menu_ui_event(e):
    if e["modId"] != "core_ui" or e["document"] != MENU_DOC:
        return
    if e["type"] == "click":
        Menu.PlayClick()
        handle_menu_click(e["element"])
    elif e["type"] == "change":
        handle_volume_change(e["element"])
    elif e["type"] == "keydown":
        if menu_state["page"] == SETTINGS_PAGE and menu_state["rebind"] >= 0:
            name = Menu.KeyName(e["key"])
            if name == "ESCAPE":
                cancel_rebind()
            elif name == "":
                set_menu_status("该按键不支持绑定", "fail")
            else:
                menu_state["bindings"][menu_state["rebind"] - 1]["key"] = name
                menu_state["rebind"] = -1
                refresh_key_buttons()
                set_menu_status("按键已修改，按“应用”保存", "info")
        elif Menu.KeyName(e["key"]) == "ESCAPE":
            if menu_state["page"] == SETTINGS_PAGE:
                close_settings()
            elif menu_state["page"] == LOAD_PAGE:
                if menu_state["pendingDelete"] is not None:
                    hide_delete_confirm()
                else:
                    show_menu_page(MAIN_PAGE)
            elif menu_state["page"] == PAUSE_PAGE:
                Menu.Action("resume")


def on_menu_changed(_e):
    sync_menu()


def on_menu_reload(e):
    if e["modId"] == "core_ui":
        sync_menu()


# 内置背包控制器：与 Lua 版共用 ui/inventory.rml。
# 状态由 Inventory.GetState() 读取；动作通过 Inventory.Action() 回到 C++ 容器规则。
INV_DOC = "inventory"
INV_GROUP = "inventory"
INV_CRAFT_ROWS = 4
inv_state = {
    "open": False,
    "state": None,
    "drag": None,
    "hover": None,
    "selected": None,
    "craftQty": 1,
    "craftPage": 1,
    "craftCategory": "all",
    "queueSel": 1,
}
INV_CATEGORY_ORDER = (
    "all", "cooking", "preservation", "medical", "crafting", "general",
)
INV_PREFIXES = (
    ("bag-slot-", "bag"),
    ("bag-name-", "bag"),
    ("bag-throw-", "bag"),
    ("bag-count-", "bag"),
    ("equip-slot-", "equipment"),
    ("equip-name-", "equipment"),
    ("equip-throw-", "equipment"),
    ("equip-count-", "equipment"),
    ("loot-slot-", "loot"),
    ("loot-name-", "loot"),
    ("loot-throw-", "loot"),
    ("loot-count-", "loot"),
)


def inv_slot_id(area, index):
    if area == "equipment":
        return "equip-slot-%d" % index
    if area == "loot":
        return "loot-slot-%d" % index
    return "bag-slot-%d" % index


def inv_parse_slot(element):
    for prefix, area in INV_PREFIXES:
        if element.startswith(prefix):
            try:
                return area, int(element[len(prefix):])
            except ValueError:
                return None, None
    return None, None


def inv_slot_state(area, index):
    if inv_state["state"] is None:
        return None
    if area == "equipment":
        slots = inv_state["state"]["equipment"]
    elif area == "loot":
        slots = inv_state["state"]["loot"]
    else:
        slots = inv_state["state"]["bag"]
    if index < 0 or index >= len(slots):
        return None
    return slots[index]


def inv_update_slot(area, index, slot):
    element_id = inv_slot_id(area, index)
    prefix = "equip" if area == "equipment" else ("loot" if area == "loot" else "bag")
    UI.SetClass(INV_DOC, element_id, "filled", slot["filled"])
    UI.SetClass(INV_DOC, element_id, "usable", slot["filled"] and slot["usable"])
    UI.SetClass(INV_DOC, element_id, "equippable",
                slot["filled"] and slot["equippable"])
    UI.SetClass(INV_DOC, element_id, "throwable",
                slot["filled"] and slot.get("throwable", False))
    if slot["filled"]:
        name = slot["label"]
        condition = slot.get("condition", 1.0)
        if slot.get("perishable", False) and condition <= 0.05:
            name = "%s [变质]" % name
        elif condition < 0.999:
            if slot.get("perishable", False):
                name = "%s 新鲜 %d%%" % (name, int(condition * 100 + 0.5))
            else:
                name = "%s %d%%" % (name, int(condition * 100 + 0.5))
        UI.SetText(INV_DOC, "%s-name-%d" % (prefix, index), name)
        UI.SetText(INV_DOC, "%s-count-%d" % (prefix, index),
                   str(slot["count"]) if slot["count"] > 1 else "")
        throw_tag = slot.get("throwTag", "")
        UI.SetText(INV_DOC, "%s-throw-%d" % (prefix, index), throw_tag)
        UI.SetProperty(INV_DOC, "%s-throw-%d" % (prefix, index), "display",
                       "block" if throw_tag else "none")
    else:
        UI.SetText(INV_DOC, "%s-name-%d" % (prefix, index),
                   "无" if area == "loot" else "空")
        UI.SetText(INV_DOC, "%s-count-%d" % (prefix, index), "")
        UI.SetText(INV_DOC, "%s-throw-%d" % (prefix, index), "")
        UI.SetProperty(INV_DOC, "%s-throw-%d" % (prefix, index), "display", "none")


def inv_recipe_station_ok(recipe):
    station = recipe.get("station", "")
    return station == "" or recipe.get("stationNearby", False) is True


def inv_recipe_station_name(recipe):
    label = recipe.get("stationLabel", "")
    if not label:
        label = recipe.get("station", "") or "工作台"
    return label


def inv_recipe_station_text(recipe):
    return "需要附近" + inv_recipe_station_name(recipe)


def inv_recipe_known(recipe):
    return recipe.get("known", True) is not False


def inv_category_label(category, fallback):
    if not category or category == "all":
        return "全部"
    return fallback or category


def inv_visible_recipes():
    state = inv_state["state"]
    recipes = state.get("recipes", []) if state is not None else []
    visible = []
    for index, recipe in enumerate(recipes):
        if inv_state["craftCategory"] == "all" or \
                recipe.get("category", "general") == inv_state["craftCategory"]:
            visible.append((recipe, index))
    return visible


def inv_recipe_extra_ok(recipe):
    for key in ("cookware", "fuel"):
        requirement = recipe.get(key)
        if requirement is not None and \
                requirement["have"] < requirement["required"]:
            return False
    return True


def inv_recipe_extra_text(recipe):
    parts = []
    for key, label in (("cookware", "锅具"), ("fuel", "燃料")):
        requirement = recipe.get(key)
        if requirement is not None:
            parts.append("%s %s %d/%d" % (
                label, requirement["label"], requirement["have"],
                requirement["required"]))
    return "、".join(parts)


def inv_recipe_tools_ok(recipe):
    for tool in recipe.get("tools", []):
        if tool["have"] < tool["required"]:
            return False
    return True


def inv_recipe_tools_text(recipe):
    parts = []
    for tool in recipe.get("tools", []):
        parts.append("%s %d/%d" % (
            tool["label"], tool["have"], tool["required"]))
    return "、".join(parts)


def inv_recipe_ingredients(recipe):
    if not inv_recipe_known(recipe):
        return "阅读对应配方杂志解锁"
    if not inv_recipe_station_ok(recipe):
        return inv_recipe_station_text(recipe)
    if not inv_recipe_tools_ok(recipe):
        return "需要工具: " + inv_recipe_tools_text(recipe)
    if not inv_recipe_extra_ok(recipe):
        return "需要" + inv_recipe_extra_text(recipe)
    parts = []
    for ingredient in recipe["ingredients"]:
        parts.append("%s %d/%d" % (
            ingredient["label"], ingredient["have"], ingredient["required"]))
    extra = inv_recipe_extra_text(recipe)
    if extra:
        parts.append(extra)
    return "材料: " + "、".join(parts)


def inv_update_recipe(index, recipe, busy_recipe_id, crafting_active):
    row = "craft-row-%d" % index
    if recipe is None:
        UI.SetProperty(INV_DOC, row, "display", "none")
        return
    UI.SetProperty(INV_DOC, row, "display", "block")
    busy = busy_recipe_id is not None and recipe["id"] == busy_recipe_id
    known = inv_recipe_known(recipe)
    UI.SetClass(INV_DOC, row, "craftable", recipe["canCraft"])
    UI.SetClass(INV_DOC, row, "busy", busy)
    UI.SetClass(INV_DOC, row, "locked", not known)
    suffix = " ×%d" % recipe["outputCount"] if recipe["outputCount"] > 1 else ""
    label = recipe["outputLabel"] + suffix
    estimated = recipe.get("estimatedSeconds", 0.0) or 0.0
    if known and estimated > 0:
        label += " · %.1fs" % estimated
    UI.SetText(INV_DOC, "craft-name-%d" % index, label)
    UI.SetText(INV_DOC, "craft-ing-%d" % index, inv_recipe_ingredients(recipe))
    state = "不可制作"
    if not known:
        state = "未学会"
    elif busy:
        state = "制作中"
    elif recipe["canCraft"]:
        state = "可制作"
    elif not inv_recipe_station_ok(recipe):
        state = "缺" + inv_recipe_station_name(recipe)
    elif not inv_recipe_tools_ok(recipe):
        state = "缺工具"
    elif not inv_recipe_extra_ok(recipe):
        state = "缺锅具/燃料"
    UI.SetText(INV_DOC, "craft-state-%d" % index, state)
    UI.SetClass(INV_DOC, "craft-state-%d" % index, "ok",
                recipe["canCraft"] and not busy)
    UI.SetClass(INV_DOC, "craft-state-%d" % index, "busy", busy)
    UI.SetText(INV_DOC, "craft-btn-%d" % index,
               "加入" if crafting_active else "制作")
    UI.SetClass(INV_DOC, "craft-btn-%d" % index, "disabled",
                not known or not recipe["canCraft"])


def inv_update_qty():
    UI.SetText(INV_DOC, "craft-qty-value", str(inv_state["craftQty"]))
    UI.SetClass(INV_DOC, "craft-qty-minus", "disabled",
                inv_state["craftQty"] <= 1)
    UI.SetClass(INV_DOC, "craft-qty-plus", "disabled",
                inv_state["craftQty"] >= 99)


def inv_refresh():
    s = Inventory.GetState()
    inv_state["state"] = s
    inv_update_qty()
    if s["playerValid"]:
        UI.SetText(INV_DOC, "inv-weight",
                   "%.1f / %.1f kg" % (s["weight"], s["capacity"]))
    else:
        UI.SetText(INV_DOC, "inv-weight", "无玩家")
    UI.SetText(INV_DOC, "bag-stack-count", "%d 堆" % s["bagStacks"])
    UI.SetText(INV_DOC, "loot-title", s.get("lootTitle", "附近地面"))
    UI.SetText(INV_DOC, "loot-stack-count", "%d 堆" % s["lootStacks"])
    can_take_all = s.get("lootIsStorage", False) or s.get("lootCanTakeAll", False)
    UI.SetProperty(INV_DOC, "storage-actions", "display",
                   "block" if can_take_all else "none")
    UI.SetProperty(INV_DOC, "storage-store-all", "display",
                   "block" if s.get("lootIsStorage", False) else "none")
    UI.SetProperty(INV_DOC, "storage-sort", "display",
                   "block" if s.get("lootIsStorage", False) else "none")
    UI.SetProperty(INV_DOC, "storage-take-all", "display",
                   "block" if can_take_all else "none")
    show_vehicle_mods = (s.get("lootIsVehicle", False) and
                         s.get("vehicleModsEnabled", False))
    UI.SetProperty(INV_DOC, "storage-vehicle-mods", "display",
                   "block" if show_vehicle_mods else "none")
    UI.SetProperty(INV_DOC, "storage-vehicle-mods", "opacity",
                   "0.55" if s.get("vehicleModsReadOnly", False) else "1.0")
    UI.SetText(INV_DOC, "inv-status", s["status"])
    UI.SetClass(INV_DOC, "inv-status", "status-info", s["statusTone"] == "info")
    UI.SetClass(INV_DOC, "inv-status", "status-ok", s["statusTone"] == "ok")
    UI.SetClass(INV_DOC, "inv-status", "status-fail", s["statusTone"] == "fail")
    for i, slot in enumerate(s["bag"]):
        inv_update_slot("bag", i, slot)
    for i, slot in enumerate(s["equipment"]):
        inv_update_slot("equipment", i, slot)
    for i, slot in enumerate(s["loot"]):
        inv_update_slot("loot", i, slot)
    recipes = s.get("recipes", [])
    visible = inv_visible_recipes()
    if not visible and inv_state["craftCategory"] != "all":
        inv_state["craftCategory"] = "all"
        visible = inv_visible_recipes()
    category_label = "全部分类"
    for recipe in recipes:
        if inv_state["craftCategory"] != "all" and \
                recipe.get("category", "general") == inv_state["craftCategory"]:
            category_label = inv_category_label(
                recipe.get("category"), recipe.get("categoryLabel"))
            break
    UI.SetText(INV_DOC, "craft-category", category_label)
    UI.SetText(INV_DOC, "craft-count", "%d 条" % len(visible))
    page_count = max(1, (len(visible) + INV_CRAFT_ROWS - 1) //
                     INV_CRAFT_ROWS)
    if inv_state["craftPage"] < 1:
        inv_state["craftPage"] = 1
    elif inv_state["craftPage"] > page_count:
        inv_state["craftPage"] = page_count
    UI.SetText(INV_DOC, "craft-page-indicator",
               "%d/%d 页" % (inv_state["craftPage"], page_count))
    UI.SetClass(INV_DOC, "craft-page-prev", "disabled",
                inv_state["craftPage"] <= 1)
    UI.SetClass(INV_DOC, "craft-page-next", "disabled",
                inv_state["craftPage"] >= page_count)
    craft_start = (inv_state["craftPage"] - 1) * INV_CRAFT_ROWS
    crafting = s.get("crafting", {})
    queued = crafting.get("queue", [])
    if not queued:
        inv_state["queueSel"] = 1
    elif inv_state["queueSel"] < 1:
        inv_state["queueSel"] = 1
    elif inv_state["queueSel"] > len(queued):
        inv_state["queueSel"] = len(queued)
    has_queue = bool(queued)
    queue_label = "队列: 空"
    if has_queue:
        item = queued[inv_state["queueSel"] - 1]
        queue_label = "队列 %d/%d: %s×%d" % (
            inv_state["queueSel"], len(queued),
            item["outputLabel"], item["count"])
    UI.SetText(INV_DOC, "craft-queue-label", queue_label)
    UI.SetText(INV_DOC, "craft-cancel", "取消全部" if has_queue else "取消")
    queue_buttons = (
        "craft-queue-prev", "craft-queue-next", "craft-queue-up",
        "craft-queue-down", "craft-queue-dec", "craft-queue-inc",
        "craft-queue-remove",
    )
    for button in queue_buttons:
        UI.SetProperty(INV_DOC, button, "display",
                       "block" if has_queue else "none")
    UI.SetClass(INV_DOC, "craft-queue-prev", "disabled",
                not has_queue or inv_state["queueSel"] <= 1)
    UI.SetClass(INV_DOC, "craft-queue-next", "disabled",
                not has_queue or inv_state["queueSel"] >= len(queued))
    UI.SetClass(INV_DOC, "craft-queue-up", "disabled",
                not has_queue or inv_state["queueSel"] <= 1)
    UI.SetClass(INV_DOC, "craft-queue-down", "disabled",
                not has_queue or inv_state["queueSel"] >= len(queued))
    selected_count = queued[inv_state["queueSel"] - 1]["count"] if has_queue else 0
    UI.SetClass(INV_DOC, "craft-queue-dec", "disabled",
                not has_queue or selected_count <= 1)
    UI.SetClass(INV_DOC, "craft-queue-inc", "disabled",
                not has_queue or selected_count >= 99)
    busy_recipe_id = None
    UI.SetProperty(INV_DOC, "craft-progress", "display",
                   "block" if crafting.get("active") else "none")
    if crafting.get("active"):
        busy_recipe_id = crafting["recipeId"]
        batch = crafting.get("batchCount", 1)
        done = crafting.get("completedCount", 0)
        label = "制作中: %s ×%d" % (crafting["outputLabel"],
                                    crafting["outputCount"])
        if batch > 1:
            label = "%s（%d/%d）" % (label, min(done + 1, batch), batch)
        UI.SetText(INV_DOC, "craft-progress-label", label)
        UI.SetText(INV_DOC, "craft-progress-time",
                   "%.1f / %.1f 秒" % (crafting["elapsed"], crafting["duration"]))
        progress = max(0.0, min(1.0, crafting["progress"]))
        UI.SetProperty(INV_DOC, "craft-progress-fill", "width",
                       "%.1f%%" % (progress * 100.0))
    for i in range(INV_CRAFT_ROWS):
        index = craft_start + i
        entry = visible[index] if index < len(visible) else None
        inv_update_recipe(i, entry[0] if entry is not None else None,
                          busy_recipe_id, bool(crafting.get("active")))


def inv_clear_drag():
    if inv_state["drag"] is not None:
        UI.SetClass(INV_DOC, inv_state["drag"]["id"], "is-dragging", False)
    if inv_state["hover"] is not None:
        UI.SetClass(INV_DOC, inv_state["hover"], "drop-target", False)
    inv_state["drag"] = None
    inv_state["hover"] = None


def inv_hide_context():
    UI.SetProperty(INV_DOC, "context-menu", "display", "none")
    inv_state["selected"] = None


def inv_show_context(area, index, mouse_x, mouse_y):
    slot = inv_slot_state(area, index)
    if slot is None or not slot["filled"]:
        inv_hide_context()
        return
    inv_state["selected"] = {
        "area": area,
        "index": index,
        "entity": slot["entityId"],
    }
    use = area == "bag" and slot["usable"]
    equip = area == "bag" and slot["equippable"]
    unequip = area == "equipment"
    drop = area == "bag" or area == "equipment"
    pickup = area == "loot"
    repair = area == "bag" and slot.get("damaged", False)
    UI.SetProperty(INV_DOC, "ctx-use", "display", "block" if use else "none")
    UI.SetProperty(INV_DOC, "ctx-equip", "display", "block" if equip else "none")
    UI.SetProperty(INV_DOC, "ctx-unequip", "display", "block" if unequip else "none")
    UI.SetProperty(INV_DOC, "ctx-drop", "display", "block" if drop else "none")
    UI.SetProperty(INV_DOC, "ctx-pickup", "display", "block" if pickup else "none")
    UI.SetProperty(INV_DOC, "ctx-repair", "display", "block" if repair else "none")
    if repair:
        UI.SetText(INV_DOC, "ctx-repair",
                   "维修" if slot.get("repairAvailable", False) else "维修（缺维修包）")
    UI.SetProperty(INV_DOC, "context-menu", "left", "%.0fpx" % max(0.0, mouse_x))
    UI.SetProperty(INV_DOC, "context-menu", "top", "%.0fpx" % max(0.0, mouse_y))
    UI.SetProperty(INV_DOC, "context-menu", "display", "block")


def inv_show():
    if not UI.IsLoaded(INV_DOC):
        if not UI.LoadDocument(INV_DOC, "ui/inventory.rml", False, True, INV_GROUP):
            Log.Error("core inventory 加载失败：ui/inventory.rml")
            return False
    UI.Show(INV_DOC, True)
    inv_state["open"] = True
    inv_refresh()
    return True


def inv_hide():
    inv_clear_drag()
    inv_hide_context()
    if UI.IsLoaded(INV_DOC):
        UI.CloseDocument(INV_DOC, False)
    inv_state["open"] = False


def sync_inventory():
    s = Inventory.GetState()
    if not s["open"]:
        inv_hide()
        return
    inv_show()


def inv_apply(area, index, entity, action_type, to_area=None, to_index=-1):
    Inventory.PlayClick()
    Inventory.Action({
        "type": action_type,
        "from": area,
        "fromIndex": index,
        "fromEntityId": entity,
        "to": to_area or area,
        "toIndex": to_index,
    })


def inv_handle_context(element):
    selected = inv_state["selected"]
    if selected is None:
        return
    area = selected["area"]
    index = selected["index"]
    entity = selected["entity"]
    if element == "ctx-use":
        inv_apply(area, index, entity, "use")
    elif element == "ctx-equip":
        if area == "loot":
            inv_apply(area, index, entity, "transfer", "equipment", 0)
        else:
            inv_apply(area, index, entity, "equip")
    elif element == "ctx-unequip":
        inv_apply(area, index, entity, "unequip")
    elif element == "ctx-drop":
        inv_apply(area, index, entity, "drop", "loot")
    elif element == "ctx-pickup":
        inv_apply(area, index, entity, "pickup", "bag")
    elif element == "ctx-repair":
        inv_apply(area, index, entity, "repair")
    inv_hide_context()


def inv_craft(slot):
    if inv_state["state"] is None:
        return
    start = (inv_state["craftPage"] - 1) * INV_CRAFT_ROWS
    visible = inv_visible_recipes()
    index = start + slot
    if index < 0 or index >= len(visible):
        return
    Inventory.PlayClick()
    Inventory.Action({
        "type": "craft",
        "from": "bag",
        "fromIndex": visible[index][1],
        "to": "bag",
        "toIndex": -1,
        "count": inv_state["craftQty"],
    })


def inv_queue_action(action_type, select_delta):
    state = inv_state["state"]
    if state is None or "crafting" not in state:
        return
    queued = state["crafting"].get("queue", [])
    index = inv_state["queueSel"]
    if index < 1 or index > len(queued):
        return
    if (select_delta < 0 and index <= 1) or \
            (select_delta > 0 and index >= len(queued)):
        return
    Inventory.PlayClick()
    accepted = Inventory.Action({
        "type": action_type,
        "from": "bag",
        "fromIndex": index - 1,
        "to": "bag",
        "toIndex": -1,
    })
    if not accepted:
        return
    if select_delta != 0:
        inv_state["queueSel"] = max(1, min(len(queued), index + select_delta))
    inv_refresh()


def inv_queue_count(delta):
    state = inv_state["state"]
    if state is None or "crafting" not in state:
        return
    queued = state["crafting"].get("queue", [])
    index = inv_state["queueSel"]
    if index < 1 or index > len(queued):
        return
    item = queued[index - 1]
    target = max(1, min(99, item["count"] + delta))
    if target == item["count"]:
        return
    Inventory.PlayClick()
    accepted = Inventory.Action({
        "type": "queue_count",
        "from": "bag",
        "fromIndex": index - 1,
        "to": "bag",
        "toIndex": -1,
        "count": target,
    })
    if accepted:
        inv_refresh()


def inv_on_click(element):
    if element == "inventory-overlay":
        inv_hide_context()
        Inventory.Close()
        return
    if element.startswith("ctx"):
        inv_handle_context(element)
        return
    if element == "craft-page-prev":
        if inv_state["craftPage"] > 1:
            inv_state["craftPage"] -= 1
            Inventory.PlayClick()
            inv_refresh()
        return
    if element == "craft-page-next":
        page_count = max(1, (len(inv_visible_recipes()) + INV_CRAFT_ROWS - 1) //
                         INV_CRAFT_ROWS)
        if inv_state["craftPage"] < page_count:
            inv_state["craftPage"] += 1
            Inventory.PlayClick()
            inv_refresh()
        return
    if element == "craft-category":
        try:
            current = INV_CATEGORY_ORDER.index(inv_state["craftCategory"])
        except ValueError:
            current = 0
        for _ in range(len(INV_CATEGORY_ORDER)):
            current = (current + 1) % len(INV_CATEGORY_ORDER)
            candidate = INV_CATEGORY_ORDER[current]
            if candidate == "all":
                inv_state["craftCategory"] = candidate
                break
            recipes = (inv_state["state"].get("recipes", [])
                       if inv_state["state"] is not None else [])
            if any(recipe.get("category", "general") == candidate
                   for recipe in recipes):
                inv_state["craftCategory"] = candidate
                break
        inv_state["craftPage"] = 1
        Inventory.PlayClick()
        inv_refresh()
        return
    if element == "craft-queue-prev":
        if inv_state["queueSel"] > 1:
            inv_state["queueSel"] -= 1
            Inventory.PlayClick()
            inv_refresh()
        return
    if element == "craft-queue-next":
        state = inv_state["state"]
        queued = (state["crafting"].get("queue", [])
                  if state is not None and "crafting" in state else [])
        if inv_state["queueSel"] < len(queued):
            inv_state["queueSel"] += 1
            Inventory.PlayClick()
            inv_refresh()
        return
    if element == "craft-queue-up":
        inv_queue_action("queue_up", -1)
        return
    if element == "craft-queue-down":
        inv_queue_action("queue_down", 1)
        return
    if element == "craft-queue-dec":
        inv_queue_count(-1)
        return
    if element == "craft-queue-inc":
        inv_queue_count(1)
        return
    if element == "craft-queue-remove":
        inv_queue_action("queue_remove", 0)
        return
    if element == "craft-cancel":
        state = inv_state["state"]
        if state is not None and state.get("crafting", {}).get("active"):
            Inventory.PlayClick()
            Inventory.Action({
                "type": "cancel_craft",
                "from": "bag",
                "fromIndex": -1,
                "to": "bag",
                "toIndex": -1,
            })
        return
    if element == "craft-qty-minus":
        inv_state["craftQty"] = max(1, inv_state["craftQty"] - 1)
        Inventory.PlayClick()
        inv_update_qty()
        return
    if element == "craft-qty-plus":
        inv_state["craftQty"] = min(99, inv_state["craftQty"] + 1)
        Inventory.PlayClick()
        inv_update_qty()
        return
    if element.startswith("craft-btn-"):
        try:
            index = int(element[len("craft-btn-"):])
        except ValueError:
            return
        inv_craft(index)
        return
    if element == "storage-vehicle-mods":
        state = inv_state["state"]
        if state is not None and state.get("vehicleModsEnabled", False):
            Inventory.PlayClick()
            Inventory.Action({
                "type": "open_vehicle_mods",
                "from": "loot",
                "fromIndex": -1,
                "fromEntityId": state.get("lootEntityId", 0),
                "to": "bag",
                "toIndex": -1,
            })
        return
    if element in ("storage-store-all", "storage-take-all", "storage-sort"):
        state = inv_state["state"]
        can_take_all = state is not None and (
            state.get("lootIsStorage", False) or
            (element == "storage-take-all" and
             state.get("lootCanTakeAll", False)))
        if can_take_all:
            action_type = {
                "storage-store-all": "store_all",
                "storage-take-all": "take_all",
                "storage-sort": "sort_storage",
            }[element]
            Inventory.PlayClick()
            Inventory.Action({
                "type": action_type,
                "from": "loot",
                "fromIndex": -1,
                "fromEntityId": state.get("lootEntityId", 0),
                "to": "bag",
                "toIndex": -1,
            })
            inv_refresh()
        return
    if element == "inventory-close":
        inv_hide_context()
        Inventory.Close()


def inv_on_mousedown(event):
    if event["button"] == 1:
        area, index = inv_parse_slot(event["element"])
        if area is not None:
            inv_show_context(area, index, event["mouseX"], event["mouseY"])
    elif event["button"] == 0:
        if not event["element"].startswith("ctx"):
            inv_hide_context()


def inv_on_dragstart(element):
    area, index = inv_parse_slot(element)
    slot = inv_slot_state(area or "", index if index is not None else -1)
    if slot is None or not slot["filled"]:
        return
    inv_hide_context()
    inv_clear_drag()
    element_id = inv_slot_id(area, index)
    inv_state["drag"] = {
        "area": area,
        "index": index,
        "entity": slot["entityId"],
        "id": element_id,
    }
    UI.SetClass(INV_DOC, element_id, "is-dragging", True)


def inv_on_dragover(element):
    drag = inv_state["drag"]
    if drag is None:
        return
    area, index = inv_parse_slot(element)
    if area is None:
        return
    element_id = inv_slot_id(area, index)
    if element_id == drag["id"]:
        return
    if inv_state["hover"] != element_id:
        if inv_state["hover"] is not None:
            UI.SetClass(INV_DOC, inv_state["hover"], "drop-target", False)
        inv_state["hover"] = element_id
        UI.SetClass(INV_DOC, element_id, "drop-target", True)


def inv_on_dragout(element):
    if inv_state["hover"] is None:
        return
    area, index = inv_parse_slot(element)
    if area is not None and inv_slot_id(area, index) == inv_state["hover"]:
        UI.SetClass(INV_DOC, inv_state["hover"], "drop-target", False)
        inv_state["hover"] = None


def inv_on_drop(element):
    drag = inv_state["drag"]
    if drag is None:
        return
    area, index = inv_parse_slot(element)
    if area is None:
        inv_clear_drag()
        return
    inv_clear_drag()
    if drag["id"] == inv_slot_id(area, index):
        return
    inv_apply(drag["area"], drag["index"], drag["entity"], "transfer", area, index)


def on_inventory_changed(event):
    if event["open"]:
        sync_inventory()
    else:
        inv_hide()


def on_inventory_ui_event(event):
    if event["modId"] != "core_ui" or event["document"] != INV_DOC:
        return
    if event["type"] == "click":
        inv_on_click(event["element"])
    elif event["type"] == "mousedown":
        inv_on_mousedown(event)
    elif event["type"] == "dragstart":
        inv_on_dragstart(event["element"])
    elif event["type"] == "dragover":
        inv_on_dragover(event["element"])
    elif event["type"] == "dragout":
        inv_on_dragout(event["element"])
    elif event["type"] == "dragdrop":
        inv_on_drop(event["element"])
    elif event["type"] == "dragend":
        inv_clear_drag()
    elif event["type"] == "keydown" and Menu.KeyName(event["key"]) == "ESCAPE":
        if inv_state["selected"] is not None:
            inv_hide_context()
        else:
            Inventory.Close()


def on_inventory_reload(event):
    if event["modId"] == "core_ui":
        sync_inventory()


Events.On("OnUiReady", lambda _: (sync_menu(), sync_inventory()))
Events.On("OnMenuChanged", on_menu_changed)
Events.On("OnInventoryChanged", on_inventory_changed)
Events.On("OnModReload", on_menu_reload)
Events.On("OnModReload", on_inventory_reload)
Events.On("OnUiEvent", on_menu_ui_event)
Events.On("OnUiEvent", on_inventory_ui_event)
