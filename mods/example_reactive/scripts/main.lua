-- Example Reactive：事件反应示例（详见 docs/modding.md）
-- 演示：击杀/拾取/枪声/玩家死亡事件 + Game.SetTimer 一次性定时器 + 状态累积
-- 玩法：每击杀 5 只僵尸奖励 1 个绷带；枪声（F7）日志；玩家死亡提示
-- 热重载演示：改一句话保存，观察日志里订阅被整体重建（kills 计数归零）

Log.Info("reactive mod loaded")

local kills = 0
local pickups = 0

Events.On("OnGameStart", function()
    kills = 0
    pickups = 0
    Log.Info("reactive mod ready: kill 5 zombies to earn a bandage")
    Game.SetTimer(5, function() -- 5 游戏秒后提示一次（一次性定时器演示）
        Log.Info("tip: press F6 for a demo-kill, F7 to fire a gunshot")
    end)
end)

Events.On("OnZombieDead", function(e)
    kills = kills + 1
    Log.Info("zombie " .. e.zombieId .. " down (kills=" .. kills .. ", killer=" .. e.killer .. ")")
    if kills % 5 == 0 then
        local got = Player.AddItem("bandage", 1) -- 5 杀奖励
        Log.Info("5-kill reward: bandage x" .. got)
    end
end)

Events.On("OnItemPickup", function(e)
    pickups = pickups + 1
    Log.Info("picked up " .. e.defId .. " x" .. e.count .. " (total pickups=" .. pickups .. ")")
end)

Events.On("OnSound", function(e)
    if e.soundType == "Gunshot" then
        Log.Info(string.format("gunshot at %.0f,%.0f (radius %.0f)", e.x, e.y, e.radius))
    end
end)

Events.On("OnPlayerDeath", function(e)
    Log.Info("you died (player " .. e.playerId .. "); a fresh survivor spawns soon")
end)
