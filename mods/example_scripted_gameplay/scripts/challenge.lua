local M = {}

local config = nil
local director = nil
local modId = "example_scripted_gameplay"
local fields = {
    state = "challenge_state",
    wave = "challenge_wave",
    score = "challenge_score",
    info = "challenge_info"
}
local UI_DOC = "challenge"
local uiReady = false
local uiEnabled = true

local run = nil
local playerId = 0
local bestScore = 0
local bestWave = 0
local runs = 0
local lastHealTick = 0
local scanTicks = 0
local builtX = nil
local builtY = nil

local function tps()
    return config.ticksPerSecond or 60
end

local function seconds(ticks)
    return math.max(0, math.ceil((ticks or 0) / tps()))
end

local function set_field(id, text, tone)
    Hud.SetField(id, text or "", tone or "info")
end

local function set_ui_text(id, text)
    if uiReady then
        UI.SetText(UI_DOC, id, text or "")
    end
end

local function ui_percent(remaining, duration)
    if duration <= 0 then
        return 0
    end
    local elapsed = duration - (remaining or 0)
    return math.max(0, math.min(1, elapsed / duration))
end

local function push_ui()
    if not uiReady then
        return
    end

    local phase = run and run.phase or "idle"
    local state_text = "等待世界"
    local wave_text = "1/" .. tostring(#config.waves)
    local timer_text = "--"
    local score_text = "0"
    local objective = "进入世界后自动开始"
    local progress = 0
    local wave = config.waves[run and run.wave or 1]

    if run == nil then
        -- 未进入世界：保留面板但显示等待状态。
    elseif phase == "disabled" then
        state_text = "仅单机"
        wave_text = "多人"
        objective = "多人模式不启动挑战；单机进入世界后自动开始"
    elseif phase == "countdown" then
        state_text = "准备"
        timer_text = seconds(run.remaining) .. "s"
        objective = string.format("第 1 波：%d 只，倒计时结束后开始",
                                  (wave and wave.count) or 0)
        progress = ui_percent(run.remaining,
                              (config.startDelaySeconds or 4) * tps())
    elseif phase == "wave" then
        state_text = "挑战中"
        wave_text = string.format("%d/%d", run.wave or 1, #config.waves)
        timer_text = seconds(run.remaining) .. "s"
        objective = string.format("击杀 %d/%d · 连杀 x%d · 清场 +%d",
                                  run.waveKills or 0,
                                  (wave and wave.count) or 0,
                                  run.combo or 0,
                                  config.score.waveClear or 50)
        progress = ui_percent(run.remaining,
                              (config.waveSeconds or 30) * tps())
    elseif phase == "intermission" then
        state_text = "波次间隙"
        wave_text = string.format("%d/%d", run.wave or 1, #config.waves)
        timer_text = seconds(run.remaining) .. "s"
        objective = string.format("第 %d 波完成；准备第 %d 波",
                                  run.wave or 1,
                                  math.min((run.wave or 1) + 1, #config.waves))
        progress = ui_percent(run.remaining,
                              (config.intermissionSeconds or 6) * tps())
    elseif phase == "finished" then
        state_text = run.result or "完成"
        wave_text = string.format("波次 %d/%d", run.wave or 1, #config.waves)
        timer_text = seconds(run.remaining) .. "s"
        objective = string.format("本局 %d 分 · 最佳 %d 分 · %s",
                                  run.score or 0, bestScore,
                                  run.result or "完成")
        progress = ui_percent(run.remaining,
                              (config.restartSeconds or 12) * tps())
    end

    set_ui_text("challenge-state", state_text)
    set_ui_text("challenge-wave", wave_text)
    set_ui_text("challenge-timer", timer_text)
    set_ui_text("challenge-score", tostring(run and run.score or 0))
    set_ui_text("challenge-best", tostring(bestScore))
    set_ui_text("challenge-objective", objective)
    UI.SetProperty(UI_DOC, "challenge-fill", "width",
                   string.format("%d%%", math.floor(progress * 100 + 0.5)))
end

local function open_ui()
    if not uiEnabled then
        return
    end
    if not UI.OpenDocument(UI_DOC, "ui/challenge.rml", false, "hud") then
        uiReady = false
        return
    end
    uiReady = true
    push_ui()
end

local function close_ui(unload)
    if uiReady or UI.IsLoaded(UI_DOC) then
        UI.CloseDocument(UI_DOC, unload == true)
    end
    uiReady = false
end

local function on_ui_event(e)
    if e.modId ~= modId or e.document ~= UI_DOC then
        return
    end
    if e.element == "challenge-hide" and e.type == "click" then
        uiEnabled = false
        close_ui(true)
        Log.Info("挑战面板已关闭；挑战继续运行")
    end
end

local function load_persisted()
    bestScore = tonumber(Mod.Data.Get("best_score") or "") or 0
    bestWave = tonumber(Mod.Data.Get("best_wave") or "") or 0
    runs = tonumber(Mod.Data.Get("runs") or "") or 0
    local text = Mod.Data.Get("run")
    if text == nil or text == "" then
        return
    end
    local decoded = Json.Decode(text)
    if type(decoded) == "table" then
        run = decoded
    else
        Log.Warn("挑战存档损坏，已忽略 run 状态")
        run = nil
    end
end

local function save_run()
    if run == nil then
        return
    end
    local encoded = Json.Encode(run)
    if encoded == nil then
        Log.Warn("挑战状态无法编码为 JSON")
        return
    end
    Mod.Data.Set("run", encoded)
end

local function scan_environment()
    if playerId == 0 then
        return
    end
    local me = Entities.Get(playerId)
    local p = Player.GetPosition()
    if me == nil or p == nil then
        return
    end
    local nearby = Entities.Query(p.x, p.y, p.z, 12)
    local bag = Entities.GetContainer(playerId)
    local weather = World.GetWeather()
    local needs = Player.GetNeeds()
    local progression = Player.GetProgression()

    run.nearby = nearby and nearby.count or 0
    run.bagStacks = bag and bag.totalStacks or 0
    run.playerKind = me.kind or "player"
    run.weather = weather and weather.kind or "clear"
    run.hunger = needs and needs.hunger or 0
    run.level = progression and progression.level or 0
end

local function push_hud()
    if run == nil then
        set_field(fields.state, "等待世界", "info")
        set_field(fields.wave, "未开始", "info")
        set_field(fields.score, "本局 0 · 最佳 " .. bestScore, "info")
        set_field(fields.info, "进入世界后自动开始", "info")
        push_ui()
        return
    end

    local phase = run.phase or "idle"
    local tone = "info"
    local state_text = "准备"
    local wave_text = string.format("%d/%d", run.wave or 1, #config.waves)
    local score_text = string.format("本局 %d · 最佳 %d", run.score or 0,
                                     bestScore)

    if phase == "disabled" then
        state_text = "仅单机"
        tone = "warn"
        wave_text = "多人模式不启动"
        score_text = "最佳 " .. bestScore
    elseif phase == "countdown" then
        state_text = "准备 " .. seconds(run.remaining) .. "s"
        tone = "warn"
    elseif phase == "wave" then
        state_text = "波次 " .. (run.wave or 1) .. " · " ..
                     seconds(run.remaining) .. "s"
        wave_text = string.format("%d/%d · 击杀 %d/%d", run.wave or 1,
                                  #config.waves, run.waveKills or 0,
                                  (config.waves[run.wave or 1] or {}).count or 0)
        tone = "warn"
    elseif phase == "intermission" then
        state_text = "波次间隙 " .. seconds(run.remaining) .. "s"
        tone = "ok"
        wave_text = string.format("%d/%d · 击杀 %d", run.wave or 1,
                                  #config.waves, run.waveKills or 0)
    elseif phase == "finished" then
        state_text = (run.result or "完成") .. " · " .. seconds(run.remaining) ..
                     "s 后重开"
        tone = "ok"
        wave_text = string.format("波次 %d/%d · 击杀 %d", run.wave or 1,
                                  #config.waves, run.kills or 0)
        score_text = string.format("本局 %d · 最佳 %d", run.score or 0,
                                   bestScore)
        if (run.result or "") == "阵亡" then
            tone = "fail"
        end
    end

    set_field(fields.state, state_text, tone)
    set_field(fields.wave, wave_text, tone)
    set_field(fields.score, score_text, "ok")
    local info = string.format("%s · 附近 %d · 饥饿 %.0f%% · 背包 %d 格 · Lv%d",
                               run.weather or "clear", run.nearby or 0,
                               (run.hunger or 0) * 100, run.bagStacks or 0,
                               run.level or 0)
    set_field(fields.info, info, "info")
    push_ui()
end

local function equip_weapon()
    local inventory = Player.GetInventory()
    if inventory == nil then
        return
    end
    for i, stack in ipairs(inventory.stacks or {}) do
        local info = Items.GetInfo(stack.defId)
        if info ~= nil and info.type == "weapon" then
            local action = Player.EquipItem(i - 1, "primary")
            if action and action.ok then
                Log.Info("挑战准备：装备 " .. stack.defId)
            end
            return
        end
    end
end

local function build_door()
    if Player.CountItem("wood") < 3 then
        return
    end
    local p = Player.GetPosition()
    if p == nil then
        return
    end
    local bx = math.floor(p.x) + 2
    local by = math.floor(p.y)
    local probe = Build.CanPlace("door", bx, by, false)
    if probe == nil or not probe.ok then
        return
    end
    local placed = Build.Place("door", bx, by, false)
    if placed and placed.ok then
        builtX = bx
        builtY = by
        Log.Info("挑战准备：在 " .. bx .. "," .. by .. " 放置木门")
    end
end

local function cleanup_door()
    if builtX == nil or builtY == nil then
        return
    end
    local toggled = Build.ToggleDoor(builtX, builtY)
    local repaired = Build.Repair(builtX, builtY)
    local removed = Build.Demolish(builtX, builtY)
    Log.Info(string.format("挑战收尾：门 toggle=%s repair=%s demolish=%s",
                           tostring(toggled and toggled.ok),
                           tostring(repaired and repaired.ok),
                           tostring(removed and removed.ok)))
    builtX = nil
    builtY = nil
end

local function try_craft()
    if not Craft.IsKnown("field_bandage") then
        return
    end
    if Player.CountItem("rag") < 2 then
        return
    end
    local action = Craft.Start("field_bandage", 1)
    if action and action.ok then
        Log.Info("挑战波次奖励：开始制作简易绷带")
    end
end

local function finish_run(reason)
    if run == nil or not run.active then
        return
    end
    run.active = false
    run.phase = "finished"
    run.result = reason
    run.remaining = (config.restartSeconds or 12) * tps()
    if (run.score or 0) > bestScore then
        bestScore = run.score or 0
    end
    if (run.wave or 0) > bestWave then
        bestWave = run.wave or 0
    end
    Mod.Data.Set("best_score", tostring(bestScore))
    Mod.Data.Set("best_wave", tostring(bestWave))
    Mod.Data.Set("last_score", tostring(run.score or 0))
    Mod.Data.Set("runs", tostring(runs))
    Player.GiveXp("survival", math.max(10, math.floor((run.score or 0) / 4)))
    cleanup_door()
    save_run()
    push_hud()
    Audio.Play2D("ui_click", 0.55)
    Log.Info(string.format("挑战结束：%s，得分 %d，最佳 %d", reason,
                           run.score or 0, bestScore))
end

local function begin_wave(index)
    local wave = config.waves[index]
    run.phase = "wave"
    run.wave = index
    run.remaining = (config.waveSeconds or 30) * tps()
    run.nextSpawn = 0
    run.spawned = 0
    run.waveKills = 0
    save_run()
    push_hud()
    Audio.Play2D("ui_click", 0.4)
    Log.Info(string.format("挑战开始第 %d/%d 波：%d 只（%s）", index,
                           #config.waves, wave.count, table.concat(wave.variants, "/")))
end

local function complete_wave(reason)
    if run == nil or run.phase ~= "wave" then
        return
    end
    local wave = config.waves[run.wave]
    if (run.waveKills or 0) >= wave.count then
        run.score = (run.score or 0) + (config.score.waveClear or 50)
    end
    Player.GiveXp("survival", 15 + (run.wave or 1) * 5)
    try_craft()
    Log.Info(string.format("第 %d 波%s：击杀 %d/%d，得分 %d", run.wave,
                           reason, run.waveKills or 0, wave.count,
                           run.score or 0))
    if (run.wave or 0) >= #config.waves then
        finish_run("完成")
        return
    end
    run.phase = "intermission"
    run.remaining = (config.intermissionSeconds or 6) * tps()
    save_run()
    push_hud()
end

local function begin_run()
    runs = runs + 1
    lastHealTick = -(config.healCooldownTicks or 300)
    scanTicks = 0
    builtX = nil
    builtY = nil
    run = {
        active = true,
        phase = "countdown",
        wave = 1,
        score = 0,
        combo = 0,
        kills = 0,
        waveKills = 0,
        spawned = 0,
        nextSpawn = 0,
        remaining = (config.startDelaySeconds or 4) * tps(),
        result = "",
        weather = "clear",
        nearby = 0,
        hunger = 0,
        bagStacks = 0,
        level = 0,
        tick = 0
    }
    Mod.Data.Set("runs", tostring(runs))
    scan_environment()
    equip_weapon()
    if not Craft.IsKnown("field_bandage") then
        Craft.Learn("field_bandage")
    end
    build_door()
    save_run()
    push_hud()
    Log.Info("挑战开始：准备阶段")
end

local function tick()
    if run == nil then
        return
    end
    if not run.active then
        if run.phase == "finished" then
            run.remaining = (run.remaining or 0) - 1
            if run.remaining <= 0 then
                begin_run()
            end
        end
        return
    end

    run.tick = (run.tick or 0) + 1
    if run.phase == "countdown" then
        run.remaining = (run.remaining or 0) - 1
        if run.remaining <= 0 then
            begin_wave(1)
        end
    elseif run.phase == "wave" then
        run.remaining = (run.remaining or 0) - 1
        run.nextSpawn = (run.nextSpawn or 0) - 1
        local wave = config.waves[run.wave]
        if run.spawned < wave.count and run.nextSpawn <= 0 then
            local handle, variant = director.spawn(run.wave, run.spawned + 1,
                                                   config)
            if handle ~= 0 then
                run.spawned = run.spawned + 1
                run.nextSpawn = math.max(1,
                    math.floor((wave.spawnIntervalSeconds or 3) * tps()))
                run.lastSpawnId = handle
                run.lastVariant = variant
            else
                run.nextSpawn = tps()
            end
        end
        if run.spawned >= wave.count and run.waveKills >= wave.count then
            complete_wave("清场")
        elseif run.remaining <= 0 then
            complete_wave("时间到")
        end
    elseif run.phase == "intermission" then
        run.remaining = (run.remaining or 0) - 1
        if run.remaining <= 0 then
            begin_wave((run.wave or 1) + 1)
        end
    end

    scanTicks = scanTicks + 1
    if run.phase == "wave" and scanTicks % (config.scanIntervalTicks or 30) == 0 then
        scan_environment()
    end
    if run.tick % 15 == 0 then
        push_hud()
    end
end

local function on_zombie_dead(e)
    if run == nil or not run.active or run.phase == "countdown" then
        return
    end
    if e.killer ~= playerId then
        return
    end
    run.kills = (run.kills or 0) + 1
    run.waveKills = (run.waveKills or 0) + 1
    run.combo = (run.combo or 0) + 1
    local bonus = math.min((run.combo - 1) * (config.score.comboStep or 2),
                           config.score.comboCap or 10)
    run.score = (run.score or 0) + (config.score.kill or 10) + bonus
    Audio.Play3D("impact", e.x or 0, e.y or 0, 18, 0.45)
    save_run()
    push_hud()
end

local function on_damage(e)
    if run == nil or not run.active or e.targetId ~= playerId or e.amount <= 0 then
        return
    end
    run.combo = 0
    local needs = Player.GetNeeds()
    if needs ~= nil and needs.healthMax > 0 and
       needs.health / needs.healthMax < config.healThreshold and
       (run.tick or 0) - lastHealTick >= (config.healCooldownTicks or 300) then
        local action = Player.UseItem(-1)
        lastHealTick = run.tick or 0
        if action and action.ok then
            Log.Info("挑战自动治疗：使用背包物品")
        end
    end
    push_hud()
end

local function on_craft_finished(e)
    if run == nil or not run.active or e.failed then
        return
    end
    run.score = (run.score or 0) + (config.score.craft or 15)
    run.crafts = (run.crafts or 0) + 1
    Log.Info("挑战制作奖励：" .. tostring(e.recipeId))
    save_run()
    push_hud()
end

local function on_build_placed(e)
    if run == nil or not run.active then
        return
    end
    run.score = (run.score or 0) + (config.score.build or 20)
    run.builds = (run.builds or 0) + 1
    Log.Info("挑战建造奖励：" .. tostring(e.defId))
    save_run()
    push_hud()
end

function M.setup(settings, waveDirector)
    config = settings
    director = waveDirector
    modId = Mod.Id()
    Hud.RegisterField(fields.state, "挑战")
    Hud.RegisterField(fields.wave, "波次")
    Hud.RegisterField(fields.score, "得分")
    Hud.RegisterField(fields.info, "环境")
end

function M.bind_events()
    Events.On("OnWorldStart", function(e)
        playerId = e.playerId or Game.GetPlayerId()
        load_persisted()
        uiEnabled = true
        if Game.IsMultiplayer() then
            run = run or {}
            run.phase = "disabled"
            run.active = false
            push_hud()
            open_ui()
            return
        end
        if e.isNewGame then
            run = nil
        end
        if run ~= nil and (run.active or run.phase == "finished") then
            push_hud()
            Log.Info("挑战恢复：波次 " .. tostring(run.wave or 1))
        else
            begin_run()
        end
        open_ui()
    end)
    Events.On("OnWorldExit", function()
        save_run()
        close_ui(true)
    end)
    Events.On("OnUiReady", open_ui)
    Events.On("OnUiEvent", on_ui_event)
    Events.On("OnModReload", function(e)
        if e.modId ~= modId then
            return
        end
        uiReady = false
        uiEnabled = true
        open_ui()
    end)
    Events.On("OnTick", function()
        tick()
    end)
    Events.On("OnZombieDead", on_zombie_dead)
    Events.On("OnDamage", on_damage)
    Events.On("OnCraftFinished", on_craft_finished)
    Events.On("OnBuildPlaced", on_build_placed)
    Events.On("OnPlayerDeath", function()
        if run ~= nil and run.active then
            finish_run("阵亡")
        end
    end)
end

function M.resume_or_show()
    modId = Mod.Id()
    load_persisted()
    if Game.GetPlayerId() ~= 0 and run ~= nil and
       (run.active or run.phase == "finished") then
        push_hud()
    else
        push_hud()
    end
end

return M
