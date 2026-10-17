# Example Hello（Python 后端版，与同目录 Lua 版行为一致；详见 docs/modding.md）
# 演示：启动日志 / 等玩家生成后发开局物品 / OnTick 周期性查询玩家与僵尸数
# 试玩：进游戏后按 F 捡物品、F6 演示击杀；改本文件并保存 → 约 1 秒后热重载

import math

Log.Info('hello mod loaded (pi=%.2f)' % math.pi)

gaveStarter = False


def on_tick(e):
    global gaveStarter
    # 注意：OnGameStart 触发时玩家实体尚未生成（世界刚初始化）；
    # 要操作玩家，等 Game.GetPlayerId() 非 0（或先判 None）——本示例用"首次就绪即发"
    if not gaveStarter and Game.GetPlayerId() != 0:
        gaveStarter = True
        Log.Info('player ready, giving starter items...')
        bandages = Player.AddItem('bandage', 2)
        axes = Player.AddItem('axe', 1)
        Log.Info('starter items: bandage x%d, axe x%d' % (bandages, axes))

    if e['tick'] % 600 == 0:  # 每 600 tick（约 10 秒）报一次
        p = Player.GetPosition()
        if p is not None:
            Log.Info('survivor at %.1f, %.1f (z=%d) | health=%s | zombies=%d' %
                     (p['x'], p['y'], p['z'], Player.GetHealth(), Zombies.Count()))


Events.On('OnTick', on_tick)
