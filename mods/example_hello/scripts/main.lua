-- Example Hello：最小入门示例（详见 docs/modding.md）
-- 演示：启动日志 / 等玩家生成后发开局物品 / OnTick 周期性查询玩家与僵尸数
-- 试玩：进游戏后按 F 捡物品、F6 演示击杀；改本文件并保存 → 约 1 秒后热重载

Log.Info("hello mod loaded")

local gaveStarter = false

Events.On("OnTick", function(e)
    -- 注意：OnGameStart 触发时玩家实体尚未生成（世界刚初始化）；
    -- 要操作玩家，等 Game.GetPlayerId() 非 0（或先判 nil）——本示例用"首次就绪即发"
    if not gaveStarter and Game.GetPlayerId() ~= 0 then
        gaveStarter = true
        Log.Info("player ready, giving starter items...")
        local bandages = Player.AddItem("bandage", 2)
        local axes = Player.AddItem("axe", 1)
        Log.Info("starter items: bandage x" .. bandages .. ", axe x" .. axes)
    end

    if e.tick % 600 == 0 then -- 每 600 tick（1 倍速约 10 秒）报一次
        local p = Player.GetPosition()
        if p then
            Log.Info(string.format("survivor at %.1f, %.1f (z=%d) | health=%s | zombies=%d",
                p.x, p.y, p.z, tostring(Player.GetHealth()), Zombies.Count()))
        end
    end
end)
