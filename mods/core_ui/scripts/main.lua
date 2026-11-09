-- 内置 HUD 的脚本控制器：与 Python 版共用 ui/hud.rml。
-- 正常路径下由脚本加载/显示/更新/卸载；C++ 只保留 native fallback。

local HUD_DOC = "hud"
local HUD_GROUP = "hud"

local function set_bar(element, text_element, value, maximum, valid)
    local ratio = 0.0
    if valid and maximum > 0.0 then
        ratio = math.max(0.0, math.min(1.0, value / maximum))
    end
    UI.SetProperty(HUD_DOC, element, "width", string.format("%.1f%%", ratio * 100.0))
    if valid then
        UI.SetText(HUD_DOC, text_element, string.format("%d", math.floor(value + 0.5)))
    else
        UI.SetText(HUD_DOC, text_element, "--")
    end
    return ratio
end

local function refresh_mod_fields(s)
    local fields = s.modFields or {}
    local any_visible = false
    for i = 1, 8 do
        local field = fields[i]
        local text = field and field.text or ""
        local label = field and field.label or ""
        local tone = field and field.tone or "info"
        local row = "mod-field-" .. (i - 1)
        local visible = text ~= ""
        UI.SetProperty(HUD_DOC, row, "display", visible and "block" or "none")
        UI.SetText(HUD_DOC, "mod-field-label-" .. (i - 1), label)
        UI.SetText(HUD_DOC, "mod-field-value-" .. (i - 1), text)
        UI.SetClass(HUD_DOC, row, "status-warn", visible and tone == "warn")
        UI.SetClass(HUD_DOC, row, "status-ok", visible and tone == "ok")
        UI.SetClass(HUD_DOC, row, "status-fail", visible and tone == "fail")
        if visible then
            any_visible = true
        end
    end
    UI.SetProperty(HUD_DOC, "mod-fields", "display",
                   any_visible and "block" or "none")
end

local function refresh_hud()
    local s = Hud.GetState()
    if not s.active then
        if UI.IsLoaded(HUD_DOC) then
            UI.CloseDocument(HUD_DOC, true)
        end
        return
    end

    if not UI.IsLoaded(HUD_DOC) then
        if not UI.LoadDocument(HUD_DOC, "ui/hud.rml", false, false, HUD_GROUP) then
            Log.Error("core HUD 加载失败：ui/hud.rml")
            return
        end
    end
    if not Game.IsMenuOpen() and not Game.IsInventoryOpen() then
        UI.Show(HUD_DOC, true)
    end

    UI.SetText(HUD_DOC, "hud-day", s.day)
    UI.SetText(HUD_DOC, "hud-time", s.time)
    UI.SetText(HUD_DOC, "hud-speed", s.speed)
    UI.SetClass(HUD_DOC, "hud-speed", "paused", s.speed == "暂停")

    local hp_ratio = set_bar("hp-fill", "hp-text", s.health, s.healthMax, s.playerValid)
    UI.SetClass(HUD_DOC, "hp-fill", "low", s.playerValid and hp_ratio < 0.3)
    set_bar("stamina-fill", "stamina-text", s.stamina, s.staminaMax, s.playerValid)
    local combat_status = s.combatStatus or ""
    local combat_tone = s.combatTone or "info"
    UI.SetText(HUD_DOC, "combat-status", combat_status)
    UI.SetProperty(HUD_DOC, "combat-status", "display",
                   combat_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "combat-status", "status-warn", combat_tone == "warn")
    UI.SetClass(HUD_DOC, "combat-status", "status-fail", combat_tone == "fail")
    local ranged_status = s.rangedStatus or ""
    local ranged_tone = s.rangedTone or "info"
    UI.SetText(HUD_DOC, "ranged-status", ranged_status)
    UI.SetProperty(HUD_DOC, "ranged-status", "display",
                   ranged_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "ranged-status", "status-warn", ranged_tone == "warn")
    UI.SetClass(HUD_DOC, "ranged-status", "status-fail", ranged_tone == "fail")
    local forage_status = s.forageStatus or ""
    local forage_tone = s.forageTone or "info"
    UI.SetText(HUD_DOC, "forage-status", forage_status)
    UI.SetProperty(HUD_DOC, "forage-status", "display",
                   forage_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "forage-status", "status-warn", forage_tone == "warn")
    UI.SetClass(HUD_DOC, "forage-status", "status-fail", forage_tone == "fail")
    local fishing_status = s.fishingStatus or ""
    local fishing_tone = s.fishingTone or "info"
    UI.SetText(HUD_DOC, "fishing-status", fishing_status)
    UI.SetProperty(HUD_DOC, "fishing-status", "display",
                   fishing_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "fishing-status", "status-warn", fishing_tone == "warn")
    UI.SetClass(HUD_DOC, "fishing-status", "status-fail", fishing_tone == "fail")
    UI.SetText(HUD_DOC, "needs-status", s.needsStatus)
    UI.SetProperty(HUD_DOC, "needs-status", "display",
                   s.needsStatus ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "needs-status", "status-warn", s.needsTone == "warn")
    UI.SetClass(HUD_DOC, "needs-status", "status-fail", s.needsTone == "fail")
    UI.SetText(HUD_DOC, "wound-status", s.woundStatus)
    UI.SetProperty(HUD_DOC, "wound-status", "display",
                   s.woundStatus ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "wound-status", "status-warn", s.woundTone == "warn")
    UI.SetClass(HUD_DOC, "wound-status", "status-fail", s.woundTone == "fail")
    UI.SetText(HUD_DOC, "pain-status", s.painStatus)
    UI.SetProperty(HUD_DOC, "pain-status", "display",
                   s.painStatus ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "pain-status", "status-warn", s.painTone == "warn")
    UI.SetClass(HUD_DOC, "pain-status", "status-fail", s.painTone == "fail")
    local armor_status = s.armorStatus or ""
    local armor_tone = s.armorTone or "info"
    UI.SetText(HUD_DOC, "armor-status", armor_status)
    UI.SetProperty(HUD_DOC, "armor-status", "display",
                   armor_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "armor-status", "status-warn", armor_tone == "warn")
    UI.SetClass(HUD_DOC, "armor-status", "status-fail", armor_tone == "fail")
    local infection_status = s.infectionStatus or ""
    local infection_tone = s.infectionTone or "info"
    UI.SetText(HUD_DOC, "infection-status", infection_status)
    UI.SetProperty(HUD_DOC, "infection-status", "display",
                   infection_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "infection-status", "status-warn",
                infection_tone == "warn")
    UI.SetClass(HUD_DOC, "infection-status", "status-fail",
                infection_tone == "fail")
    local weather_status = s.weatherStatus or ""
    local weather_tone = s.weatherTone or "info"
    local weather_class = s.weatherClass or "clear"
    UI.SetText(HUD_DOC, "weather-status", weather_status)
    UI.SetProperty(HUD_DOC, "weather-status", "display",
                   weather_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "weather-status", "status-warn", weather_tone == "warn")
    UI.SetClass(HUD_DOC, "weather-status", "status-fail", weather_tone == "fail")
    UI.SetClass(HUD_DOC, "weather-overlay", "weather-rain", weather_class == "rain")
    UI.SetClass(HUD_DOC, "weather-overlay", "weather-fog", weather_class == "fog")
    UI.SetClass(HUD_DOC, "weather-overlay", "weather-cold", weather_class == "cold")
    UI.SetProperty(HUD_DOC, "weather-overlay", "display",
                   weather_class ~= "clear" and "block" or "none")

    local threat_active = s.threatDirection ~= "" and s.threatRemaining > 0.0
    local threat_opacity = 1.0
    if s.threatDuration > 0.0 then
        threat_opacity = math.max(0.0, math.min(1.0, s.threatRemaining / s.threatDuration))
    end
    for _, direction in ipairs({ "left", "right", "top", "bottom" }) do
        local edge_active = threat_active and s.threatDirection == direction
        UI.SetProperty(HUD_DOC, "threat-" .. direction, "display",
                       edge_active and "block" or "none")
        if edge_active then
            UI.SetProperty(HUD_DOC, "threat-" .. direction, "opacity",
                           string.format("%.2f", threat_opacity))
        end
    end
    UI.SetText(HUD_DOC, "threat-status", s.threatStatus)
    UI.SetProperty(HUD_DOC, "threat-status", "display",
                   s.threatStatus ~= "" and "block" or "none")

    UI.SetText(HUD_DOC, "objective-text", s.objective)
    UI.SetProperty(HUD_DOC, "hud-objective", "display",
                   (s.objective ~= "" or (s.missionGuide or "") ~= ""
                    or (s.eventGuide or "") ~= "")
                   and "block" or "none")
    UI.SetClass(HUD_DOC, "objective-text", "objective-warn", s.objectiveTone == "warn")
    UI.SetClass(HUD_DOC, "objective-text", "objective-ok", s.objectiveTone == "ok")
    UI.SetText(HUD_DOC, "objective-hint", s.objectiveHint)
    UI.SetProperty(HUD_DOC, "objective-hint", "display",
                   s.objectiveHint ~= "" and "block" or "none")
    local poi_guide = s.poiGuide or ""
    local poi_tone = s.poiGuideTone or "info"
    UI.SetText(HUD_DOC, "poi-guide", poi_guide)
    UI.SetProperty(HUD_DOC, "poi-guide", "display",
                   poi_guide ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "poi-guide", "status-warn", poi_tone == "warn")
    UI.SetClass(HUD_DOC, "poi-guide", "status-ok", poi_tone == "ok")
    local mission_guide = s.missionGuide or ""
    local mission_tone = s.missionTone or "info"
    UI.SetText(HUD_DOC, "mission-guide", mission_guide)
    UI.SetProperty(HUD_DOC, "mission-guide", "display",
                   mission_guide ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "mission-guide", "status-warn", mission_tone == "warn")
    UI.SetClass(HUD_DOC, "mission-guide", "status-ok", mission_tone == "ok")
    local event_guide = s.eventGuide or ""
    local event_tone = s.eventTone or "warn"
    UI.SetText(HUD_DOC, "event-guide", event_guide)
    UI.SetProperty(HUD_DOC, "event-guide", "display",
                   event_guide ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "event-guide", "status-warn", event_tone == "warn")
    UI.SetClass(HUD_DOC, "event-guide", "status-ok", event_tone == "ok")
    UI.SetClass(HUD_DOC, "event-guide", "status-fail", event_tone == "fail")

    UI.SetText(HUD_DOC, "siege-status", s.siegeStatus)
    UI.SetProperty(HUD_DOC, "siege-status", "display",
                   s.siegeStatus ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "siege-status", "status-warn", s.siegeTone == "warn")
    UI.SetClass(HUD_DOC, "siege-status", "status-fail", s.siegeTone == "fail")

    local base_status = s.baseStatus or ""
    local base_tone = s.baseTone or "info"
    UI.SetText(HUD_DOC, "base-status", base_status)
    UI.SetProperty(HUD_DOC, "base-status", "display",
                   base_status ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "base-status", "status-warn", base_tone == "warn")
    UI.SetClass(HUD_DOC, "base-status", "status-fail", base_tone == "fail")
    refresh_mod_fields(s)

    local report_open = s.siegeReportOpen or false
    local report_tone = s.siegeReportTone or "info"
    UI.SetProperty(HUD_DOC, "siege-report", "display",
                   report_open and "block" or "none")
    UI.SetClass(HUD_DOC, "siege-report", "status-warn", report_tone == "warn")
    UI.SetClass(HUD_DOC, "siege-report", "status-fail", report_tone == "fail")
    UI.SetText(HUD_DOC, "siege-report-title", s.siegeReportTitle or "守夜战报")
    local report_rating = s.siegeReportRating or ""
    UI.SetText(HUD_DOC, "siege-report-rating", report_rating)
    UI.SetProperty(HUD_DOC, "siege-report-rating", "display",
                   report_rating ~= "" and "block" or "none")
    UI.SetText(HUD_DOC, "siege-report-hint", s.siegeReportHint or "ESC 关闭")
    local report_lines = s.siegeReportLines or {}
    for i = 0, 7 do
        local line = report_lines[i + 1] or ""
        UI.SetText(HUD_DOC, "siege-report-line-" .. i, line)
        UI.SetProperty(HUD_DOC, "siege-report-line-" .. i, "display",
                       line ~= "" and "block" or "none")
    end

    local progression_valid = s.progressionValid or false
    local progression_level = s.progressionLevel or 1
    local progression_xp = s.progressionXp or 0.0
    local progression_next = s.progressionXpNext or 0.0
    local progression_points = s.progressionPerkPoints or 0
    local progression_ratio = 0.0
    if progression_valid then
        if progression_next > 0.0 then
            progression_ratio = math.max(0.0, math.min(1.0, progression_xp / progression_next))
        else
            progression_ratio = 1.0
        end
    end
    UI.SetProperty(HUD_DOC, "hud-level", "display",
                   progression_valid and "block" or "none")
    UI.SetProperty(HUD_DOC, "level-fill", "width",
                   string.format("%.1f%%", progression_ratio * 100.0))
    UI.SetText(HUD_DOC, "level-text", "Lv." .. tostring(progression_level))
    UI.SetText(HUD_DOC, "level-perk", "Perk×" .. tostring(progression_points))
    UI.SetProperty(HUD_DOC, "level-perk", "display",
                   progression_points > 0 and "block" or "none")
    if progression_next > 0.0 then
        UI.SetText(HUD_DOC, "level-xp",
                   string.format("%d / %d", math.floor(progression_xp + 0.5),
                                 math.floor(progression_next + 0.5)))
    else
        UI.SetText(HUD_DOC, "level-xp", "MAX")
    end
    UI.SetText(HUD_DOC, "perk-prompt", s.perkPrompt or "")
    UI.SetProperty(HUD_DOC, "perk-prompt", "display",
                   (s.perkPrompt or "") ~= "" and "block" or "none")
    UI.SetProperty(HUD_DOC, "character-panel", "display",
                   (progression_valid and s.characterOpen) and "block" or "none")
    if progression_valid then
        UI.SetText(HUD_DOC, "character-level",
                   "等级 " .. tostring(progression_level)
                   .. (progression_points > 0
                       and (" · 待选 Perk ×" .. tostring(progression_points))
                       or ""))
        if progression_next > 0.0 then
            UI.SetText(HUD_DOC, "character-xp",
                       string.format("XP %d / %d", math.floor(progression_xp + 0.5),
                                     math.floor(progression_next + 0.5)))
        else
            UI.SetText(HUD_DOC, "character-xp", "XP MAX")
        end
        UI.SetProperty(HUD_DOC, "character-xp-fill", "width",
                       string.format("%.1f%%", progression_ratio * 100.0))
        local skills = s.progressionSkills or {}
        for i = 1, 4 do
            UI.SetText(HUD_DOC, "character-skill-" .. (i - 1), skills[i] or "")
        end
        UI.SetText(HUD_DOC, "character-perks", s.progressionPerks or "暂无")
        local choices = s.progressionChoices or {}
        for i = 1, 3 do
            local line = choices[i] or ""
            UI.SetText(HUD_DOC, "perk-choice-" .. (i - 1), line)
            UI.SetProperty(HUD_DOC, "perk-choice-" .. (i - 1), "display",
                           line ~= "" and "block" or "none")
        end
    end

    local dialogue_open = s.dialogueOpen or false
    local dialogue_options = s.dialogueOptions or {}
    local dialogue_count = s.dialogueOptionCount or 0
    local dialogue_selected = s.dialogueSelected or 0
    UI.SetProperty(HUD_DOC, "dialogue-panel", "display",
                   dialogue_open and "block" or "none")
    UI.SetText(HUD_DOC, "dialogue-speaker", s.dialogueSpeaker or "")
    UI.SetText(HUD_DOC, "dialogue-text", s.dialogueText or "")
    UI.SetText(HUD_DOC, "dialogue-hint", s.dialogueHint or "")
    for i = 0, 4 do
        local option = dialogue_options[i + 1] or ""
        UI.SetText(HUD_DOC, "dialogue-option-" .. i, option)
        UI.SetProperty(HUD_DOC, "dialogue-option-" .. i, "display",
                       dialogue_open and i < dialogue_count and "block" or "none")
        UI.SetClass(HUD_DOC, "dialogue-option-" .. i, "selected",
                    dialogue_open and i == dialogue_selected)
    end

    UI.SetText(HUD_DOC, "action-hint", s.actionHint)
    UI.SetProperty(HUD_DOC, "action-hint", "display",
                   s.actionHint ~= "" and "block" or "none")
    UI.SetClass(HUD_DOC, "action-hint", "status-warn", s.actionTone == "warn")
    UI.SetClass(HUD_DOC, "action-hint", "status-fail", s.actionTone == "fail")

    for i = 1, 5 do
        local slot = s.slots[i]
        local index = i - 1
        UI.SetClass(HUD_DOC, "slot-" .. index, "selected", s.selectedSlot == index)
        UI.SetText(HUD_DOC, "slot-name-" .. index, slot and slot.label or "空")
        local count = slot and slot.count or 0
        UI.SetText(HUD_DOC, "slot-count-" .. index, count > 0 and tostring(count) or "")
    end

    local notice_active = s.notice ~= "" and s.noticeRemaining > 0.0
    UI.SetProperty(HUD_DOC, "notice", "display", notice_active and "block" or "none")
    if notice_active then
        UI.SetText(HUD_DOC, "notice", s.notice)
        UI.SetClass(HUD_DOC, "notice", "notice-info", s.noticeTone == "info")
        UI.SetClass(HUD_DOC, "notice", "notice-ok", s.noticeTone == "ok")
        UI.SetClass(HUD_DOC, "notice", "notice-fail", s.noticeTone == "fail")
        local duration = math.max(s.noticeDuration, 0.05)
        local elapsed = math.max(0.0, duration - s.noticeRemaining)
        local rise = math.min(elapsed * 30.0, 30.0)
        local opacity = math.min(1.0, s.noticeRemaining / 0.35)
        UI.SetProperty(HUD_DOC, "notice", "bottom", string.format("%.0fpx", 116.0 + rise))
        UI.SetProperty(HUD_DOC, "notice", "opacity", string.format("%.2f", opacity))
    end
end

local function on_hud_ui_event(e)
    if e.modId == "core_ui" and e.document == HUD_DOC and e.element == "hud-bag" and
        e.type == "click" then
        Game.ToggleInventory()
    end
end

local function on_core_hud_reload(e)
    if e.modId == "core_ui" then
        refresh_hud()
    end
end

-- 内置主菜单/暂停菜单控制器：与 Python 版共用 ui/menu.rml。
-- C++ 只负责菜单开关状态、动作回调和设置保存；页面切换/改键采集在脚本内完成。

local DOC = "menu"
local GROUP = "menu"
local MAIN_PAGE = "main-menu"
local PAUSE_PAGE = "pause-menu"
local SETTINGS_PAGE = "settings-menu"
local LOAD_PAGE = "load-menu"
local SAVE_ROWS = 8

local state = {
    open = false,
    pause = false,
    canContinue = false,
    page = MAIN_PAGE,
    returnPage = MAIN_PAGE,
    volume = 65,
    musicVolume = 70,
    sfxVolume = 85,
    ambientVolume = 55,
    bindings = {},
    rebind = -1,
    saves = {},
    savePage = 0,
    pendingDelete = nil,
}

local function copy_bindings(source)
    local out = {}
    for i = 1, #source do
        local item = source[i]
        out[i] = { action = item.action, label = item.label, key = item.key }
    end
    return out
end

local function read_state()
    local s = Menu.GetState()
    state.open = s.open
    state.pause = s.pauseMode
    state.canContinue = s.canContinue
    state.volume = s.volume
    state.musicVolume = s.musicVolume
    state.sfxVolume = s.sfxVolume
    state.ambientVolume = s.ambientVolume
    state.bindings = copy_bindings(s.bindings)
end

local function set_status(text, tone)
    UI.SetText(DOC, "settings-status", text)
    UI.SetClass(DOC, "settings-status", "status-info", tone == "info")
    UI.SetClass(DOC, "settings-status", "status-ok", tone == "ok")
    UI.SetClass(DOC, "settings-status", "status-fail", tone == "fail")
end

local function set_load_status(text, tone)
    UI.SetText(DOC, "load-status", text)
    UI.SetClass(DOC, "load-status", "status-ok", tone == "ok")
    UI.SetClass(DOC, "load-status", "status-fail", tone == "fail")
end

local function set_pause_status(text)
    UI.SetText(DOC, "pause-save-status", text)
end

local function save_page_count()
    return math.max(1, math.ceil(#state.saves / SAVE_ROWS))
end

local function save_global_index(index)
    return state.savePage * SAVE_ROWS + index
end

local function refresh_continue()
    local result = Save.List()
    state.saves = result.ok and result.saves or {}
    state.canContinue = #state.saves > 0
    local pages = save_page_count()
    state.savePage = math.max(0, math.min(state.savePage, pages - 1))
    local start = state.savePage * SAVE_ROWS + 1
    UI.SetClass(DOC, "menu-continue", "disabled", not state.canContinue)
    UI.SetText(DOC, "menu-continue",
               state.canContinue and "继续" or "继续（暂无存档）")
    UI.SetClass(DOC, "menu-load", "disabled", not state.canContinue)
    for i = 1, SAVE_ROWS do
        local row = string.format("save-row-%d", i)
        local item = state.saves[start + i - 1]
        UI.SetProperty(DOC, row, "display", item ~= nil and "block" or "none")
        if item ~= nil then
            local detail = item.updatedText
            if detail == "" then
                detail = item.id
            end
            if item.mapId ~= "" then
                detail = detail .. " · " .. item.mapId
            end
            UI.SetText(DOC, string.format("save-name-%d", i), item.name)
            UI.SetText(DOC, string.format("save-time-%d", i), detail)
        end
    end
    UI.SetProperty(DOC, "load-empty", "display",
                   #state.saves == 0 and "block" or "none")
    UI.SetProperty(DOC, "load-more", "display",
                   #state.saves > SAVE_ROWS and "block" or "none")
    UI.SetText(DOC, "load-more", string.format("共 %d 个存档", #state.saves))
    UI.SetProperty(DOC, "load-pager", "display",
                   pages > 1 and "block" or "none")
    UI.SetText(DOC, "load-page", string.format("第 %d / %d 页", state.savePage + 1,
                                               pages))
    UI.SetClass(DOC, "load-prev", "disabled", state.savePage <= 0)
    UI.SetClass(DOC, "load-next", "disabled", state.savePage >= pages - 1)
end

local function change_save_page(delta)
    local pages = save_page_count()
    local nextPage = math.max(0, math.min(state.savePage + delta, pages - 1))
    if nextPage == state.savePage then
        return
    end
    state.savePage = nextPage
    refresh_continue()
end

local function refresh_key_buttons()
    for i = 1, #state.bindings do
        local binding = state.bindings[i]
        UI.SetText(DOC, "bind-" .. binding.action, binding.key)
        UI.SetClass(DOC, "bind-" .. binding.action, "capturing", state.rebind == i)
    end
end

local function show_page(page)
    state.page = page
    UI.SetProperty(DOC, MAIN_PAGE, "display", page == MAIN_PAGE and "block" or "none")
    UI.SetProperty(DOC, PAUSE_PAGE, "display", page == PAUSE_PAGE and "block" or "none")
    UI.SetProperty(DOC, SETTINGS_PAGE, "display", page == SETTINGS_PAGE and "block" or "none")
    UI.SetProperty(DOC, LOAD_PAGE, "display", page == LOAD_PAGE and "block" or "none")
    if page ~= LOAD_PAGE then
        state.pendingDelete = nil
        UI.SetProperty(DOC, "load-delete-confirm", "display", "none")
    end
end

local function open_load()
    if #state.saves == 0 then
        return
    end
    state.savePage = 0
    state.pendingDelete = nil
    set_load_status("")
    UI.SetProperty(DOC, "load-delete-confirm", "display", "none")
    refresh_continue()
    show_page(LOAD_PAGE)
end

local function load_save(index)
    local item = state.saves[save_global_index(index)]
    if item == nil then
        return
    end
    local result = Save.Load(item.id)
    if not result.ok then
        set_load_status(result.error ~= "" and result.error or "读取存档失败", "fail")
    end
end

local function hide_delete_confirm()
    state.pendingDelete = nil
    UI.SetProperty(DOC, "load-delete-confirm", "display", "none")
end

local function ask_delete(index)
    local item = state.saves[save_global_index(index)]
    if item == nil then
        return
    end
    state.pendingDelete = { id = item.id, name = item.name }
    UI.SetText(DOC, "load-delete-text", "确定删除存档 “" .. item.name .. "”？")
    UI.SetProperty(DOC, "load-delete-confirm", "display", "block")
end

local function confirm_delete()
    local pending = state.pendingDelete
    if pending == nil then
        return
    end
    local result = Save.Delete(pending.id)
    hide_delete_confirm()
    if result.ok then
        set_load_status("已删除存档：" .. pending.name, "ok")
    else
        set_load_status(result.error ~= "" and result.error or "删除存档失败", "fail")
    end
    refresh_continue()
end

local function refresh_settings_fields()
    local fields = {
        { "setting-volume", "setting-volume-value", state.volume },
        { "setting-music-volume", "setting-music-volume-value", state.musicVolume },
        { "setting-sfx-volume", "setting-sfx-volume-value", state.sfxVolume },
        { "setting-ambient-volume", "setting-ambient-volume-value", state.ambientVolume },
    }
    for i = 1, #fields do
        local id = fields[i][1]
        local value = math.max(0, math.min(100, fields[i][3]))
        UI.SetValue(DOC, id, tostring(value))
        UI.SetText(DOC, fields[i][2], tostring(value))
    end
    refresh_key_buttons()
end

local function open_settings()
    state.returnPage = state.page
    state.rebind = -1
    read_state()
    show_page(SETTINGS_PAGE)
    refresh_settings_fields()
    set_status("键位与音量修改后按“应用”保存", "info")
end

local function close_settings()
    state.rebind = -1
    show_page(state.returnPage)
end

local function begin_rebind(index)
    if state.page ~= SETTINGS_PAGE or index < 1 or index > #state.bindings then
        return
    end
    state.rebind = index
    refresh_key_buttons()
    set_status("请按下新按键，ESC 取消", "info")
end

local function cancel_rebind()
    if state.rebind < 0 then
        return
    end
    state.rebind = -1
    refresh_key_buttons()
    set_status("已取消改键", "info")
end

local function read_volume(element, fallback)
    local value = tonumber(UI.GetValue(DOC, element))
    if value == nil then
        return fallback
    end
    return math.max(0, math.min(100, math.floor(value + 0.5)))
end

local function apply_settings()
    if state.rebind >= 0 then
        set_status("请先完成改键，或按 ESC 取消", "fail")
        return
    end
    state.volume = read_volume("setting-volume", state.volume)
    state.musicVolume = read_volume("setting-music-volume", state.musicVolume)
    state.sfxVolume = read_volume("setting-sfx-volume", state.sfxVolume)
    state.ambientVolume = read_volume("setting-ambient-volume", state.ambientVolume)
    local bindings = copy_bindings(state.bindings)
    local result = Menu.ApplySettings({
        volume = state.volume,
        musicVolume = state.musicVolume,
        sfxVolume = state.sfxVolume,
        ambientVolume = state.ambientVolume,
        bindings = bindings,
    })
    if result.ok then
        read_state()
        refresh_settings_fields()
        set_status("设置已保存", "ok")
    else
        set_status(result.error ~= "" and result.error or "保存设置失败", "fail")
    end
end

local function show_menu()
    if not UI.IsLoaded(DOC) then
        if not UI.LoadDocument(DOC, "ui/menu.rml", false, true, GROUP) then
            Log.Error("core menu 加载失败：ui/menu.rml")
            return false
        end
    end
    UI.Show(DOC, true)
    return true
end

local function sync_menu()
    read_state()
    if not state.open then
        if UI.IsLoaded(DOC) then
            UI.CloseDocument(DOC, false)
        end
        state.rebind = -1
        return
    end
    if not show_menu() then
        return
    end
    state.rebind = -1
    state.page = state.pause and PAUSE_PAGE or MAIN_PAGE
    state.returnPage = state.page
    refresh_continue()
    set_pause_status("")
    show_page(state.page)
end

local function save_row_index(element)
    local suffix = string.match(element, "^save%-row%-(%d+)$")
        or string.match(element, "^save%-name%-(%d+)$")
        or string.match(element, "^save%-time%-(%d+)$")
    return tonumber(suffix)
end

local function save_delete_index(element)
    return tonumber(string.match(element, "^save%-delete%-(%d+)$"))
end

local function handle_click(element)
    if element == "menu-new-game" then
        Menu.Action("new_game")
    elseif element == "menu-multiplayer" then
        Menu.Action("multiplayer")
    elseif element == "menu-continue" and state.canContinue then
        Menu.Action("continue")
    elseif element == "menu-load" and state.canContinue then
        open_load()
    elseif element == "pause-save" then
        if Save.QuickSave() then
            set_pause_status("正在保存并返回游戏…")
            Menu.Action("resume")
        else
            set_pause_status("存档服务忙或不可用")
        end
    elseif element == "load-back" then
        show_page(MAIN_PAGE)
    elseif element == "load-prev" then
        change_save_page(-1)
    elseif element == "load-next" then
        change_save_page(1)
    elseif element == "load-delete-cancel" then
        hide_delete_confirm()
    elseif element == "load-delete-yes" then
        confirm_delete()
    elseif save_delete_index(element) ~= nil then
        ask_delete(save_delete_index(element))
    elseif save_row_index(element) ~= nil then
        load_save(save_row_index(element))
    elseif element == "menu-settings" or element == "pause-settings" then
        open_settings()
    elseif element == "menu-quit" or element == "pause-quit" then
        Menu.Action("quit")
    elseif element == "pause-resume" then
        Menu.Action("resume")
    elseif element == "pause-main-menu" then
        Menu.Action("return_to_main")
    elseif element == "settings-back" then
        close_settings()
    elseif element == "settings-apply" then
        apply_settings()
    elseif string.sub(element, 1, 5) == "bind-" then
        local action = string.sub(element, 6)
        for i = 1, #state.bindings do
            if state.bindings[i].action == action then
                begin_rebind(i)
                return
            end
        end
    end
end

local function handle_volume_change(element)
    local fields = {
        ["setting-volume"] = "setting-volume-value",
        ["setting-music-volume"] = "setting-music-volume-value",
        ["setting-sfx-volume"] = "setting-sfx-volume-value",
        ["setting-ambient-volume"] = "setting-ambient-volume-value",
    }
    local label = fields[element]
    if label ~= nil then
        local value = math.max(0, math.min(100, tonumber(UI.GetValue(DOC, element)) or 0))
        UI.SetText(DOC, label, tostring(math.floor(value + 0.5)))
    end
end

local function on_ui_event(e)
    if e.modId ~= "core_ui" or e.document ~= DOC then
        return
    end
    if e.type == "click" then
        Menu.PlayClick()
        handle_click(e.element)
    elseif e.type == "change" then
        handle_volume_change(e.element)
    elseif e.type == "keydown" then
        if state.page == SETTINGS_PAGE and state.rebind >= 0 then
            local name = Menu.KeyName(e.key)
            if name == "ESCAPE" then
                cancel_rebind()
            elseif name == "" then
                set_status("该按键不支持绑定", "fail")
            else
                state.bindings[state.rebind].key = name
                state.rebind = -1
                refresh_key_buttons()
                set_status("按键已修改，按“应用”保存", "info")
            end
        elseif Menu.KeyName(e.key) == "ESCAPE" then
            if state.page == SETTINGS_PAGE then
                close_settings()
            elseif state.page == LOAD_PAGE then
                if state.pendingDelete ~= nil then
                    hide_delete_confirm()
                else
                    show_page(MAIN_PAGE)
                end
            elseif state.page == PAUSE_PAGE then
                Menu.Action("resume")
            end
        end
    end
end

local function on_menu_changed(_e)
    sync_menu()
end

local function on_mod_reload(e)
    if e.modId == "core_ui" then
        sync_menu()
    end
end

-- 内置背包控制器：与 Python 版共用 ui/inventory.rml。
-- 状态由 Inventory.GetState() 读取；动作通过 Inventory.Action() 回到 C++ 容器规则。
local INV_DOC = "inventory"
local INV_GROUP = "inventory"
local INV_CRAFT_ROWS = 4
local inv = {
    open = false,
    state = nil,
    drag = nil,
    hover = nil,
    selected = nil,
    craftQty = 1,
    craftPage = 1,
    craftCategory = "all",
    queueSel = 1,
}
local INV_CATEGORY_ORDER = {
    "all", "cooking", "preservation", "medical", "crafting", "general",
}

local function inv_slot_id(area, index)
    if area == "equipment" then
        return "equip-slot-" .. tostring(index)
    elseif area == "loot" then
        return "loot-slot-" .. tostring(index)
    end
    return "bag-slot-" .. tostring(index)
end

local function inv_parse_slot(element)
    local prefixes = {
        { "bag-slot-", "bag" },
        { "bag-name-", "bag" },
        { "bag-count-", "bag" },
        { "equip-slot-", "equipment" },
        { "equip-name-", "equipment" },
        { "equip-count-", "equipment" },
        { "loot-slot-", "loot" },
        { "loot-name-", "loot" },
        { "loot-count-", "loot" },
    }
    for i = 1, #prefixes do
        local prefix = prefixes[i][1]
        if string.sub(element, 1, #prefix) == prefix then
            local index = tonumber(string.sub(element, #prefix + 1))
            if index ~= nil then
                return prefixes[i][2], index
            end
        end
    end
    return nil, nil
end

local function inv_slot_state(area, index)
    if inv.state == nil then
        return nil
    end
    if area == "equipment" then
        return inv.state.equipment[index + 1]
    elseif area == "loot" then
        return inv.state.loot[index + 1]
    end
    return inv.state.bag[index + 1]
end

local function inv_update_slot(area, index, slot)
    local id = inv_slot_id(area, index)
    local prefix = area == "equipment" and "equip" or (area == "loot" and "loot" or "bag")
    UI.SetClass(INV_DOC, id, "filled", slot.filled)
    UI.SetClass(INV_DOC, id, "usable", slot.filled and slot.usable)
    UI.SetClass(INV_DOC, id, "equippable", slot.filled and slot.equippable)
    if slot.filled then
        local name = slot.label
        local condition = slot.condition or 1.0
        if slot.perishable and condition <= 0.05 then
            name = string.format("%s [变质]", name)
        elseif condition < 0.999 then
            if slot.perishable then
                name = string.format("%s 新鲜 %d%%", name,
                    math.floor(condition * 100 + 0.5))
            else
                name = string.format("%s %d%%", name,
                    math.floor(condition * 100 + 0.5))
            end
        end
        UI.SetText(INV_DOC, prefix .. "-name-" .. tostring(index), name)
        UI.SetText(INV_DOC, prefix .. "-count-" .. tostring(index),
                   slot.count > 1 and tostring(slot.count) or "")
    else
        UI.SetText(INV_DOC, prefix .. "-name-" .. tostring(index),
                   area == "loot" and "无" or "空")
        UI.SetText(INV_DOC, prefix .. "-count-" .. tostring(index), "")
    end
end

local function inv_recipe_station_ok(recipe)
    local station = recipe.station or ""
    return station == "" or recipe.stationNearby == true
end

local function inv_recipe_station_name(recipe)
    local label = recipe.stationLabel
    if label == nil or label == "" then
        label = recipe.station or "工作台"
    end
    return label
end

local function inv_recipe_station_text(recipe)
    return "需要附近" .. inv_recipe_station_name(recipe)
end

local function inv_recipe_known(recipe)
    return recipe.known ~= false
end

local function inv_category_label(category, fallback)
    if category == nil or category == "" or category == "all" then
        return "全部"
    end
    return fallback or category
end

local function inv_visible_recipes()
    local recipes = inv.state and inv.state.recipes or {}
    local visible = {}
    for i = 1, #recipes do
        if inv.craftCategory == "all" or
            (recipes[i].category or "general") == inv.craftCategory then
            visible[#visible + 1] = { recipe = recipes[i], index = i - 1 }
        end
    end
    return visible
end

local function inv_recipe_extra_ok(recipe)
    local cookware = recipe.cookware
    if cookware ~= nil and cookware.have < cookware.required then
        return false
    end
    local fuel = recipe.fuel
    if fuel ~= nil and fuel.have < fuel.required then
        return false
    end
    return true
end

local function inv_recipe_extra_text(recipe)
    local parts = {}
    if recipe.cookware ~= nil then
        parts[#parts + 1] = string.format("锅具 %s %d/%d",
                                          recipe.cookware.label,
                                          recipe.cookware.have,
                                          recipe.cookware.required)
    end
    if recipe.fuel ~= nil then
        parts[#parts + 1] = string.format("燃料 %s %d/%d",
                                          recipe.fuel.label,
                                          recipe.fuel.have,
                                          recipe.fuel.required)
    end
    return table.concat(parts, "、")
end

local function inv_recipe_tools_ok(recipe)
    local tools = recipe.tools or {}
    for i = 1, #tools do
        if tools[i].have < tools[i].required then
            return false
        end
    end
    return true
end

local function inv_recipe_tools_text(recipe)
    local tools = recipe.tools or {}
    local parts = {}
    for i = 1, #tools do
        local tool = tools[i]
        parts[#parts + 1] = string.format("%s %d/%d", tool.label, tool.have,
                                         tool.required)
    end
    return table.concat(parts, "、")
end

local function inv_recipe_ingredients(recipe)
    if not inv_recipe_known(recipe) then
        return "阅读对应配方杂志解锁"
    end
    if not inv_recipe_station_ok(recipe) then
        return inv_recipe_station_text(recipe)
    end
    if not inv_recipe_tools_ok(recipe) then
        return "需要工具: " .. inv_recipe_tools_text(recipe)
    end
    if not inv_recipe_extra_ok(recipe) then
        return "需要" .. inv_recipe_extra_text(recipe)
    end
    local parts = {}
    for i = 1, #recipe.ingredients do
        local ingredient = recipe.ingredients[i]
        parts[#parts + 1] = string.format("%s %d/%d", ingredient.label,
                                         ingredient.have, ingredient.required)
    end
    local extra = inv_recipe_extra_text(recipe)
    if extra ~= "" then
        parts[#parts + 1] = extra
    end
    return "材料: " .. table.concat(parts, "、")
end

local function inv_update_recipe(index, recipe, busy_recipe_id, crafting_active)
    local row = "craft-row-" .. tostring(index)
    if recipe == nil then
        UI.SetProperty(INV_DOC, row, "display", "none")
        return
    end
    UI.SetProperty(INV_DOC, row, "display", "block")
    local busy = busy_recipe_id ~= nil and recipe.id == busy_recipe_id
    local known = inv_recipe_known(recipe)
    UI.SetClass(INV_DOC, row, "craftable", recipe.canCraft)
    UI.SetClass(INV_DOC, row, "busy", busy)
    UI.SetClass(INV_DOC, row, "locked", not known)
    local suffix = recipe.outputCount > 1 and (" ×" .. tostring(recipe.outputCount)) or ""
    local label = recipe.outputLabel .. suffix
    local estimated = recipe.estimatedSeconds or 0
    if known and estimated > 0 then
        label = string.format("%s · %.1fs", label, estimated)
    end
    UI.SetText(INV_DOC, "craft-name-" .. tostring(index), label)
    UI.SetText(INV_DOC, "craft-ing-" .. tostring(index), inv_recipe_ingredients(recipe))
    local state = "不可制作"
    if not known then
        state = "未学会"
    elseif busy then
        state = "制作中"
    elseif recipe.canCraft then
        state = "可制作"
    elseif not inv_recipe_station_ok(recipe) then
        state = "缺" .. inv_recipe_station_name(recipe)
    elseif not inv_recipe_tools_ok(recipe) then
        state = "缺工具"
    elseif not inv_recipe_extra_ok(recipe) then
        state = "缺锅具/燃料"
    end
    UI.SetText(INV_DOC, "craft-state-" .. tostring(index), state)
    UI.SetClass(INV_DOC, "craft-state-" .. tostring(index), "ok",
                recipe.canCraft and not busy)
    UI.SetClass(INV_DOC, "craft-state-" .. tostring(index), "busy", busy)
    UI.SetText(INV_DOC, "craft-btn-" .. tostring(index),
               crafting_active and "加入" or "制作")
    UI.SetClass(INV_DOC, "craft-btn-" .. tostring(index), "disabled",
                not known or not recipe.canCraft)
end

local function inv_update_qty()
    UI.SetText(INV_DOC, "craft-qty-value", tostring(inv.craftQty))
    UI.SetClass(INV_DOC, "craft-qty-minus", "disabled", inv.craftQty <= 1)
    UI.SetClass(INV_DOC, "craft-qty-plus", "disabled", inv.craftQty >= 99)
end

local function inv_refresh()
    local s = Inventory.GetState()
    inv.state = s
    inv_update_qty()
    if s.playerValid then
        UI.SetText(INV_DOC, "inv-weight",
                   string.format("%.1f / %.1f kg", s.weight, s.capacity))
    else
        UI.SetText(INV_DOC, "inv-weight", "无玩家")
    end
    UI.SetText(INV_DOC, "bag-stack-count", tostring(s.bagStacks) .. " 堆")
    UI.SetText(INV_DOC, "loot-title", s.lootTitle or "附近地面")
    UI.SetText(INV_DOC, "loot-stack-count", tostring(s.lootStacks) .. " 堆")
    local canTakeAll = s.lootIsStorage == true or s.lootCanTakeAll == true
    UI.SetProperty(INV_DOC, "storage-actions", "display",
                   canTakeAll and "block" or "none")
    UI.SetProperty(INV_DOC, "storage-store-all", "display",
                   s.lootIsStorage == true and "block" or "none")
    UI.SetProperty(INV_DOC, "storage-sort", "display",
                   s.lootIsStorage == true and "block" or "none")
    UI.SetProperty(INV_DOC, "storage-take-all", "display",
                   canTakeAll and "block" or "none")
    UI.SetText(INV_DOC, "inv-status", s.status)
    UI.SetClass(INV_DOC, "inv-status", "status-info", s.statusTone == "info")
    UI.SetClass(INV_DOC, "inv-status", "status-ok", s.statusTone == "ok")
    UI.SetClass(INV_DOC, "inv-status", "status-fail", s.statusTone == "fail")
    for i = 1, #s.bag do
        inv_update_slot("bag", i - 1, s.bag[i])
    end
    for i = 1, #s.equipment do
        inv_update_slot("equipment", i - 1, s.equipment[i])
    end
    for i = 1, #s.loot do
        inv_update_slot("loot", i - 1, s.loot[i])
    end
    local recipes = s.recipes or {}
    local visible = inv_visible_recipes()
    if #visible == 0 and inv.craftCategory ~= "all" then
        inv.craftCategory = "all"
        visible = inv_visible_recipes()
    end
    local categoryLabel = "全部分类"
    for i = 1, #recipes do
        if inv.craftCategory ~= "all" and
            (recipes[i].category or "general") == inv.craftCategory then
            categoryLabel = inv_category_label(recipes[i].category,
                                               recipes[i].categoryLabel)
            break
        end
    end
    if inv.craftCategory == "all" then
        categoryLabel = "全部分类"
    end
    UI.SetText(INV_DOC, "craft-category", categoryLabel)
    UI.SetText(INV_DOC, "craft-count", tostring(#visible) .. " 条")
    local pageCount = math.max(1, math.ceil(#visible / INV_CRAFT_ROWS))
    if inv.craftPage < 1 then
        inv.craftPage = 1
    elseif inv.craftPage > pageCount then
        inv.craftPage = pageCount
    end
    UI.SetText(INV_DOC, "craft-page-indicator",
               string.format("%d/%d 页", inv.craftPage, pageCount))
    UI.SetClass(INV_DOC, "craft-page-prev", "disabled", inv.craftPage <= 1)
    UI.SetClass(INV_DOC, "craft-page-next", "disabled", inv.craftPage >= pageCount)
    local craftStart = (inv.craftPage - 1) * INV_CRAFT_ROWS
    local crafting = s.crafting or {}
    local queued = crafting.queue or {}
    if #queued == 0 then
        inv.queueSel = 1
    elseif inv.queueSel < 1 then
        inv.queueSel = 1
    elseif inv.queueSel > #queued then
        inv.queueSel = #queued
    end
    local hasQueue = #queued > 0
    local queueLabel = "队列: 空"
    if hasQueue then
        local item = queued[inv.queueSel]
        queueLabel = string.format("队列 %d/%d: %s×%d", inv.queueSel, #queued,
                                   item.outputLabel, item.count)
    end
    UI.SetText(INV_DOC, "craft-queue-label", queueLabel)
    UI.SetText(INV_DOC, "craft-cancel", hasQueue and "取消全部" or "取消")
    local queueButtons = {
        "craft-queue-prev", "craft-queue-next", "craft-queue-up",
        "craft-queue-down", "craft-queue-dec", "craft-queue-inc",
        "craft-queue-remove",
    }
    for i = 1, #queueButtons do
        UI.SetProperty(INV_DOC, queueButtons[i], "display",
                       hasQueue and "block" or "none")
    end
    UI.SetClass(INV_DOC, "craft-queue-prev", "disabled",
                not hasQueue or inv.queueSel <= 1)
    UI.SetClass(INV_DOC, "craft-queue-next", "disabled",
                not hasQueue or inv.queueSel >= #queued)
    UI.SetClass(INV_DOC, "craft-queue-up", "disabled",
                not hasQueue or inv.queueSel <= 1)
    UI.SetClass(INV_DOC, "craft-queue-down", "disabled",
                not hasQueue or inv.queueSel >= #queued)
    local selectedCount = hasQueue and queued[inv.queueSel].count or 0
    UI.SetClass(INV_DOC, "craft-queue-dec", "disabled",
                not hasQueue or selectedCount <= 1)
    UI.SetClass(INV_DOC, "craft-queue-inc", "disabled",
                not hasQueue or selectedCount >= 99)
    local busy_recipe_id = nil
    UI.SetProperty(INV_DOC, "craft-progress", "display",
                   crafting.active and "block" or "none")
    if crafting.active then
        busy_recipe_id = crafting.recipeId
        local batch = crafting.batchCount or 1
        local done = crafting.completedCount or 0
        local label = string.format("制作中: %s ×%d", crafting.outputLabel,
                                    crafting.outputCount)
        if batch > 1 then
            label = string.format("%s（%d/%d）", label,
                                  math.min(done + 1, batch), batch)
        end
        UI.SetText(INV_DOC, "craft-progress-label", label)
        UI.SetText(INV_DOC, "craft-progress-time",
                   string.format("%.1f / %.1f 秒", crafting.elapsed, crafting.duration))
        local progress = math.max(0.0, math.min(1.0, crafting.progress or 0.0))
        UI.SetProperty(INV_DOC, "craft-progress-fill", "width",
                       string.format("%.1f%%", progress * 100.0))
    end
    for i = 0, INV_CRAFT_ROWS - 1 do
        local entry = visible[craftStart + i + 1]
        inv_update_recipe(i, entry and entry.recipe or nil, busy_recipe_id,
                          crafting.active == true)
    end
end

local function inv_clear_drag()
    if inv.drag ~= nil then
        UI.SetClass(INV_DOC, inv.drag.id, "is-dragging", false)
    end
    if inv.hover ~= nil then
        UI.SetClass(INV_DOC, inv.hover, "drop-target", false)
    end
    inv.drag = nil
    inv.hover = nil
end

local function inv_hide_context()
    UI.SetProperty(INV_DOC, "context-menu", "display", "none")
    inv.selected = nil
end

local function inv_show_context(area, index, mouse_x, mouse_y)
    local slot = inv_slot_state(area, index)
    if slot == nil or not slot.filled then
        inv_hide_context()
        return
    end
    inv.selected = { area = area, index = index, entity = slot.entityId }
    local use = area == "bag" and slot.usable
    local equip = area == "bag" and slot.equippable
    local unequip = area == "equipment"
    local drop = area == "bag" or area == "equipment"
    local pickup = area == "loot"
    local repair = area == "bag" and slot.damaged == true
    UI.SetProperty(INV_DOC, "ctx-use", "display", use and "block" or "none")
    UI.SetProperty(INV_DOC, "ctx-equip", "display", equip and "block" or "none")
    UI.SetProperty(INV_DOC, "ctx-unequip", "display", unequip and "block" or "none")
    UI.SetProperty(INV_DOC, "ctx-drop", "display", drop and "block" or "none")
    UI.SetProperty(INV_DOC, "ctx-pickup", "display", pickup and "block" or "none")
    UI.SetProperty(INV_DOC, "ctx-repair", "display", repair and "block" or "none")
    if repair then
        UI.SetText(INV_DOC, "ctx-repair",
                   slot.repairAvailable and "维修" or "维修（缺维修包）")
    end
    UI.SetProperty(INV_DOC, "context-menu", "left",
                   string.format("%.0fpx", math.max(0.0, mouse_x)))
    UI.SetProperty(INV_DOC, "context-menu", "top",
                   string.format("%.0fpx", math.max(0.0, mouse_y)))
    UI.SetProperty(INV_DOC, "context-menu", "display", "block")
end

local function inv_show()
    if not UI.IsLoaded(INV_DOC) then
        if not UI.LoadDocument(INV_DOC, "ui/inventory.rml", false, true, INV_GROUP) then
            Log.Error("core inventory 加载失败：ui/inventory.rml")
            return false
        end
    end
    UI.Show(INV_DOC, true)
    inv.open = true
    inv_refresh()
    return true
end

local function inv_hide()
    inv_clear_drag()
    inv_hide_context()
    if UI.IsLoaded(INV_DOC) then
        UI.CloseDocument(INV_DOC, false)
    end
    inv.open = false
end

local function sync_inventory()
    local s = Inventory.GetState()
    if not s.open then
        inv_hide()
        return
    end
    inv_show()
end

local function inv_apply(area, index, entity, action_type, to_area, to_index)
    Inventory.PlayClick()
    Inventory.Action({
        type = action_type,
        from = area,
        fromIndex = index,
        fromEntityId = entity,
        to = to_area or area,
        toIndex = to_index or -1,
    })
end

local function inv_handle_context(element)
    if inv.selected == nil then
        return
    end
    local area = inv.selected.area
    local index = inv.selected.index
    local entity = inv.selected.entity
    if element == "ctx-use" then
        inv_apply(area, index, entity, "use")
    elseif element == "ctx-equip" then
        if area == "loot" then
            inv_apply(area, index, entity, "transfer", "equipment", 0)
        else
            inv_apply(area, index, entity, "equip")
        end
    elseif element == "ctx-unequip" then
        inv_apply(area, index, entity, "unequip")
    elseif element == "ctx-drop" then
        inv_apply(area, index, entity, "drop", area == "equipment" and "loot" or "loot")
    elseif element == "ctx-pickup" then
        inv_apply(area, index, entity, "pickup", "bag")
    elseif element == "ctx-repair" then
        inv_apply(area, index, entity, "repair")
    end
    inv_hide_context()
end

local function inv_craft(slot)
    local start = (inv.craftPage - 1) * INV_CRAFT_ROWS
    local visible = inv_visible_recipes()
    local entry = visible[start + slot + 1]
    if entry == nil then
        return
    end
    Inventory.PlayClick()
    Inventory.Action({
        type = "craft",
        from = "bag",
        fromIndex = entry.index,
        to = "bag",
        toIndex = -1,
        count = inv.craftQty,
    })
end

local function inv_queue_action(action_type, select_delta)
    local s = inv.state
    if s == nil or s.crafting == nil then
        return
    end
    local queued = s.crafting.queue or {}
    local index = inv.queueSel
    if index < 1 or index > #queued then
        return
    end
    if (select_delta < 0 and index <= 1) or
        (select_delta > 0 and index >= #queued) then
        return
    end
    Inventory.PlayClick()
    local accepted = Inventory.Action({
        type = action_type,
        from = "bag",
        fromIndex = index - 1,
        to = "bag",
        toIndex = -1,
    })
    if not accepted then
        return
    end
    if select_delta ~= 0 then
        inv.queueSel = math.max(1, math.min(#queued, index + select_delta))
    end
    inv_refresh()
end

local function inv_queue_count(delta)
    local s = inv.state
    if s == nil or s.crafting == nil then
        return
    end
    local queued = s.crafting.queue or {}
    local index = inv.queueSel
    if index < 1 or index > #queued then
        return
    end
    local item = queued[index]
    local target = math.max(1, math.min(99, item.count + delta))
    if target == item.count then
        return
    end
    Inventory.PlayClick()
    local accepted = Inventory.Action({
        type = "queue_count",
        from = "bag",
        fromIndex = index - 1,
        to = "bag",
        toIndex = -1,
        count = target,
    })
    if accepted then
        inv_refresh()
    end
end

local function inv_on_click(element)
    if element == "inventory-overlay" then
        inv_hide_context()
        Inventory.Close()
        return
    end
    if string.sub(element, 1, 3) == "ctx" then
        inv_handle_context(element)
        return
    end
    if element == "craft-page-prev" then
        if inv.craftPage > 1 then
            inv.craftPage = inv.craftPage - 1
            Inventory.PlayClick()
            inv_refresh()
        end
        return
    end
    if element == "craft-page-next" then
        local pageCount = math.max(1, math.ceil(#inv_visible_recipes() /
                                                 INV_CRAFT_ROWS))
        if inv.craftPage < pageCount then
            inv.craftPage = inv.craftPage + 1
            Inventory.PlayClick()
            inv_refresh()
        end
        return
    end
    if element == "craft-category" then
        local current = 1
        for i = 1, #INV_CATEGORY_ORDER do
            if INV_CATEGORY_ORDER[i] == inv.craftCategory then
                current = i
                break
            end
        end
        for _ = 1, #INV_CATEGORY_ORDER do
            current = current % #INV_CATEGORY_ORDER + 1
            local candidate = INV_CATEGORY_ORDER[current]
            if candidate == "all" then
                inv.craftCategory = candidate
                break
            end
            local recipes = inv.state and inv.state.recipes or {}
            local found = false
            for i = 1, #recipes do
                if (recipes[i].category or "general") == candidate then
                    found = true
                    break
                end
            end
            if found then
                inv.craftCategory = candidate
                break
            end
        end
        inv.craftPage = 1
        Inventory.PlayClick()
        inv_refresh()
        return
    end
    if element == "craft-queue-prev" then
        if inv.queueSel > 1 then
            inv.queueSel = inv.queueSel - 1
            Inventory.PlayClick()
            inv_refresh()
        end
        return
    end
    if element == "craft-queue-next" then
        local queued = inv.state ~= nil and inv.state.crafting ~= nil and
                           inv.state.crafting.queue or {}
        if inv.queueSel < #queued then
            inv.queueSel = inv.queueSel + 1
            Inventory.PlayClick()
            inv_refresh()
        end
        return
    end
    if element == "craft-queue-up" then
        inv_queue_action("queue_up", -1)
        return
    end
    if element == "craft-queue-down" then
        inv_queue_action("queue_down", 1)
        return
    end
    if element == "craft-queue-dec" then
        inv_queue_count(-1)
        return
    end
    if element == "craft-queue-inc" then
        inv_queue_count(1)
        return
    end
    if element == "craft-queue-remove" then
        inv_queue_action("queue_remove", 0)
        return
    end
    if element == "craft-cancel" then
        if inv.state ~= nil and inv.state.crafting ~= nil and
            inv.state.crafting.active then
            Inventory.PlayClick()
            Inventory.Action({
                type = "cancel_craft",
                from = "bag",
                fromIndex = -1,
                to = "bag",
                toIndex = -1,
            })
        end
        return
    end
    if element == "craft-qty-minus" then
        inv.craftQty = math.max(1, inv.craftQty - 1)
        Inventory.PlayClick()
        inv_update_qty()
        return
    end
    if element == "craft-qty-plus" then
        inv.craftQty = math.min(99, inv.craftQty + 1)
        Inventory.PlayClick()
        inv_update_qty()
        return
    end
    local craftPrefix = "craft-btn-"
    if string.sub(element, 1, #craftPrefix) == craftPrefix then
        local index = tonumber(string.sub(element, #craftPrefix + 1))
        if index ~= nil then
            inv_craft(index)
        end
        return
    end
    if element == "storage-store-all" or element == "storage-take-all" or
        element == "storage-sort" then
        local s = inv.state
        local canTakeAll = s ~= nil and
            (s.lootIsStorage == true or
             (element == "storage-take-all" and s.lootCanTakeAll == true))
        if canTakeAll then
            local action_type = "store_all"
            if element == "storage-take-all" then
                action_type = "take_all"
            elseif element == "storage-sort" then
                action_type = "sort_storage"
            end
            Inventory.PlayClick()
            Inventory.Action({
                type = action_type,
                from = "loot",
                fromIndex = -1,
                fromEntityId = s.lootEntityId or 0,
                to = "bag",
                toIndex = -1,
            })
            inv_refresh()
        end
        return
    end
    if element == "inventory-close" then
        inv_hide_context()
        Inventory.Close()
        return
    end
end

local function inv_on_mousedown(e)
    if e.button == 1 then
        local area, index = inv_parse_slot(e.element)
        if area ~= nil then
            inv_show_context(area, index, e.mouseX, e.mouseY)
        end
    elseif e.button == 0 then
        if string.sub(e.element, 1, 3) ~= "ctx" then
            inv_hide_context()
        end
    end
end

local function inv_on_dragstart(element)
    local area, index = inv_parse_slot(element)
    local slot = inv_slot_state(area or "", index or -1)
    if slot == nil or not slot.filled then
        return
    end
    inv_hide_context()
    inv_clear_drag()
    local id = inv_slot_id(area, index)
    inv.drag = { area = area, index = index, entity = slot.entityId, id = id }
    UI.SetClass(INV_DOC, id, "is-dragging", true)
end

local function inv_on_dragover(element)
    if inv.drag == nil then
        return
    end
    local area, index = inv_parse_slot(element)
    if area == nil then
        return
    end
    local id = inv_slot_id(area, index)
    if id == inv.drag.id then
        return
    end
    if inv.hover ~= id then
        if inv.hover ~= nil then
            UI.SetClass(INV_DOC, inv.hover, "drop-target", false)
        end
        inv.hover = id
        UI.SetClass(INV_DOC, id, "drop-target", true)
    end
end

local function inv_on_dragout(element)
    if inv.hover == nil then
        return
    end
    local area, index = inv_parse_slot(element)
    if area ~= nil and inv_slot_id(area, index) == inv.hover then
        UI.SetClass(INV_DOC, inv.hover, "drop-target", false)
        inv.hover = nil
    end
end

local function inv_on_drop(element)
    if inv.drag == nil then
        return
    end
    local area, index = inv_parse_slot(element)
    if area == nil then
        inv_clear_drag()
        return
    end
    local drag = inv.drag
    inv_clear_drag()
    if drag.id == inv_slot_id(area, index) then
        return
    end
    inv_apply(drag.area, drag.index, drag.entity, "transfer", area, index)
end

local function on_inventory_changed(e)
    if e.open then
        sync_inventory()
    else
        inv_hide()
    end
end

local function on_inventory_ui_event(e)
    if e.modId ~= "core_ui" or e.document ~= INV_DOC then
        return
    end
    if e.type == "click" then
        inv_on_click(e.element)
    elseif e.type == "mousedown" then
        inv_on_mousedown(e)
    elseif e.type == "dragstart" then
        inv_on_dragstart(e.element)
    elseif e.type == "dragover" then
        inv_on_dragover(e.element)
    elseif e.type == "dragout" then
        inv_on_dragout(e.element)
    elseif e.type == "dragdrop" then
        inv_on_drop(e.element)
    elseif e.type == "dragend" then
        inv_clear_drag()
    elseif e.type == "keydown" and Menu.KeyName(e.key) == "ESCAPE" then
        if inv.selected ~= nil then
            inv_hide_context()
        else
            Inventory.Close()
        end
    end
end

local function on_inventory_reload(e)
    if e.modId == "core_ui" then
        sync_inventory()
    end
end

Events.On("OnUiReady", function(_) sync_menu(); sync_inventory() end)
Events.On("OnUiFrame", function(_) refresh_hud() end)
Events.On("OnMenuChanged", on_menu_changed)
Events.On("OnInventoryChanged", on_inventory_changed)
Events.On("OnModReload", on_mod_reload)
Events.On("OnModReload", on_core_hud_reload)
Events.On("OnModReload", on_inventory_reload)
Events.On("OnUiEvent", on_hud_ui_event)
Events.On("OnUiEvent", on_ui_event)
Events.On("OnUiEvent", on_inventory_ui_event)
