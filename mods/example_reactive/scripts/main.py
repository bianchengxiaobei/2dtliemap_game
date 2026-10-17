# Example Reactive（Python 后端版，与同目录 Lua 版行为一致；详见 docs/modding.md）
# 演示：击杀/拾取/枪声/玩家死亡事件 + Game.SetTimer 一次性定时器 + 状态累积
# 玩法：每击杀 5 只僵尸奖励 1 个绷带；枪声（F7）日志；玩家死亡提示
# 热重载演示：改一句话保存，观察日志里订阅被整体重建（kills 计数归零）

Log.Info('reactive mod loaded')

kills = 0
pickups = 0


def on_game_start():
    global kills, pickups
    kills = 0
    pickups = 0
    Log.Info('reactive mod ready: kill 5 zombies to earn a bandage')
    Game.SetTimer(5, lambda: Log.Info(
        'tip: press F6 for a demo-kill, F7 to fire a gunshot'))  # 5 游戏秒后提示一次


def on_zombie_dead(e):
    global kills
    kills += 1
    Log.Info('zombie %d down (kills=%d, killer=%d)' %
             (e['zombieId'], kills, e['killer']))
    if kills % 5 == 0:
        got = Player.AddItem('bandage', 1)  # 5 杀奖励
        Log.Info('5-kill reward: bandage x%d' % got)


def on_item_pickup(e):
    global pickups
    pickups += 1
    Log.Info('picked up %s x%d (total pickups=%d)' %
             (e['defId'], e['count'], pickups))


def on_sound(e):
    if e['soundType'] == 'Gunshot':
        Log.Info('gunshot at %.0f,%.0f (radius %.0f)' %
                 (e['x'], e['y'], e['radius']))


def on_player_death(e):
    Log.Info('you died (player %d); a fresh survivor spawns soon' % e['playerId'])


Events.On('OnGameStart', on_game_start)
Events.On('OnZombieDead', on_zombie_dead)
Events.On('OnItemPickup', on_item_pickup)
Events.On('OnSound', on_sound)
Events.On('OnPlayerDeath', on_player_death)
