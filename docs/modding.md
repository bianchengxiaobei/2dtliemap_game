# ZomboidLike Mod 开发指南（Lua / Python 可选，API v1）

> 适用版本：M7/M8 脚本系统 v1 · 参考示例：`mods/example_hello`、`mods/example_reactive`
> （两个示例目录都同时带 `scripts/main.lua` 与 `scripts/main.py`，用 `script.backend` 切换）
> 设计稿（内部实现说明）：`docs/03-脚本系统设计.md`

---

## 1. 五分钟写出第一个 mod

### 1.1 目录结构

在游戏根目录的 `mods/` 下新建一个以 mod id 命名的目录：

```
mods/
└─ my_first_mod/
   ├─ mod.json            ← 清单（必需）
   └─ scripts/
      └─ main.lua         ← 入口脚本（路径由 mod.json 的 main 指定）
```

### 1.2 清单 `mod.json`

```json
{
  "id": "my_first_mod",
  "name": "My First Mod",
  "version": "1.0.0",
  "api": 1,
  "backend": "lua",
  "main": "scripts/main.lua",
  "loadOrder": 100
}
```

### 1.3 入口脚本 `scripts/main.lua`

```lua
Log.Info("my_first_mod loaded")

local gaveStarter = false

Events.On("OnTick", function(e)
    -- OnGameStart 时玩家尚未生成：等 Game.GetPlayerId() 非 0 再操作玩家
    if not gaveStarter and Game.GetPlayerId() ~= 0 then
        gaveStarter = true
        Player.AddItem("bandage", 3)
        Log.Info("gave 3 bandages")
    end
    if e.tick % 600 == 0 then              -- 每约 10 秒
        Log.Info("zombies=" .. Zombies.Count())
    end
end)
```

### 1.4 运行与观察

1. 启动游戏。日志（控制台与 `logs/zomboidlike.log`）出现：
   - `mod 已加载：my_first_mod v1.0.0（backend=lua；共 N 个）`
   - `[script-mod] [my_first_mod] my_first_mod loaded`
2. 进游戏后第一个 tick 就会检测到玩家生成并发放开局物品（日志 `gave 3 bandages`）。
3. **热重载**：开着游戏改 `main.lua` 并保存 → 约 1 秒后日志出现
   `mod 重载成功：[my_first_mod]`，新代码立即生效，无需重启、无需改一行 C++。
   重载 = 整个脚本重新执行（全局变量、订阅、定时器全部重置）。

> 提示：文件请保存为 **UTF-8**（带不带 BOM 都兼容）；建议用能显示 Lua 高亮的编辑器。

---

## 2. `mod.json` 字段说明

| 字段 | 必填 | 缺省 | 说明 |
|---|---|---|---|
| `id` | 否 | 目录名 | 稳定标识（日志/热重载用）。与目录名不一致时以 `id` 为准（仅告警） |
| `name` | 否 | = id | 展示名 |
| `version` | 否 | `"0.0.0"` | 仅记录/日志 |
| `api` | 否 | 当前版本（1） | **过新会被拒绝加载**（提示"需要更新的 API 版本"）；偏旧告警但尽力加载 |
| `backend` | 否 | `config/game.json` 的 `script.backend`（默认 `lua`） | `lua` / `python`（python 需启用该后端的构建：CMake 预设 `x64-debug-python`；未启用时该 mod 会被跳过并提示"本构建未启用 Python 后端"） |
| `main` | 否 | 按后端：`scripts/main.lua` / `scripts/main.py` | 入口脚本（相对 mod 根目录） |
| `loadOrder` | 否 | `100` | 越小越先加载/派发事件（同值按加载先后） |

任何一项不合法（JSON 坏、类型不对、api 过新、后端不存在……）都只会**跳过这一个 mod** 并在日志说明原因，不影响其它 mod 与游戏本体。

---

## 3. Host API v1

通用约定：

- 实体一律用**稳定 ID**（数字，跨存档稳定；`0` = 无效/不存在）。
- 坐标是**格**（浮点，`x/y` 格 + `z` 层）；相机/渲染与本 API 无关。
- 查询类接口在目标不存在时返回 `nil`（如 `Player.GetPosition()` 在玩家未生成时），
  写代码时**先判 nil**。

### 3.1 `Log`

| 接口 | 说明 |
|---|---|
| `Log.Info(msg)` / `Log.Warn(msg)` / `Log.Error(msg)` | 写游戏日志，自动带 `[mod-id]` 前缀 |

### 3.2 `Game`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Game.GetTick()` | number | 当前总 tick（定点 60/s，受游戏倍速影响） |
| `Game.GetPlayerId()` | number | 玩家实体稳定 ID（`0` = 尚未生成） |
| `Game.GetTime()` | table | `{year, month, day, hour, minute, totalMinutes}` |
| `Game.SetTimer(seconds, fn)` | - | 一次性定时器：`seconds` 为**游戏秒**，到点在随后的 tick 触发一次 |

### 3.3 `Player`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Player.GetPosition()` | `{x, y, z}` 或 nil | 玩家位置（无玩家 → nil） |
| `Player.Teleport(x, y, z)` | bool | 传送到格坐标（含物理体同步） |
| `Player.GetHealth()` | number 或 nil | 当前 HP |
| `Player.AddItem(defId, count?)` | number | 入背包实际数量（`count` 缺省 1；容量不足/未注册 id → 0） |

### 3.4 `World`

| 接口 | 返回 | 说明 |
|---|---|---|
| `World.IsPassable(x, y)` | bool | 格 (x, y) 是否可通行（未加载区域 = 不可通行） |
| `World.SpawnItem(defId, x, y, z?)` | bool | 在格坐标生成地面物品（defId 必须先注册，见 `assets/items.json`） |

### 3.5 `Zombies`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Zombies.Spawn(x, y, z?)` | number | 生成一只僵尸，返回其稳定 ID（0 = 失败） |
| `Zombies.Count()` | number | 当前实体僵尸数（不含离屏虚拟僵尸） |
| `Zombies.GetPosition(handle)` | `{x, y, z}` 或 nil | 按 ID 查询（已死亡/不存在 → nil） |

### 3.6 `Items`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Items.Exists(defId)` | bool | 物品定义是否已注册 |
| `Items.GetName(defId)` | string | 显示名（未注册 → 空串） |

内置物品 id（`assets/items.json`）：`bandage` / `canned_beans` / `axe` / `kitchen_knife` / `rag` / `jacket` / `tote_bag`。

---

## 4. 事件表 v1（`Events.On`）

```lua
Events.On("事件名", function(e) ... end)   -- e 为字段表
Events.Off("事件名")                        -- 取消该事件全部订阅
Events.Off("事件名", 某个函数)              -- 只取消指定函数
```

| 事件 | 触发时机 | `e` 的字段 |
|---|---|---|
| `OnGameStart` | 新游戏开始（世界初始化后） | - |
| `OnTick` | 每个模拟 tick 末 | `tick` |
| `OnPlayerMove` | 玩家到达点击移动的目标格 | `playerId, x, y, z` |
| `OnPlayerDeath` | 玩家死亡（转尸体时） | `playerId` |
| `OnZombieSpawn` | 僵尸实体生成/实体化 | `zombieId, x, y, z` |
| `OnZombieDead` | 僵尸死亡（转尸体时） | `zombieId, killer`（`killer` 当前恒 0，伤害来源追踪未实现） |
| `OnItemPickup` / `OnItemDrop` | 玩家拾取/丢弃物品 | `itemId, defId, count` |
| `OnSound` | 世界声音（脚步/枪声等） | `x, y, z, radius, soundType, clip`（`soundType`：`"Generic"/"Gunshot"/"Impact"/"Footstep"/"ZombieGrowl"/"Ui"`；`clip` 为可选逻辑音效名） |
| `OnChunkLoaded` / `OnChunkUnloaded` | 地图 chunk 加载/卸载 | `cx, cy` |
| `OnModReload` | 某 mod 热重载成功后 | `modId` |
| `OnSaveBegin` / `OnSaveEnd` | 存档前后（预留，生产端待存档系统接入） | `slotName` |

注意事项：

- 所有事件都是**排队派发**：世界系统产生的事件在当前 tick 末统一派发；脚本回调里再
  产生事件（如 `Zombies.Spawn`）会留到**下一 tick**，天然不会自己触发自己形成风暴。
- `OnGameStart` 时玩家**尚未生成**（世界刚初始化）：要操作玩家请用 `OnTick`/`SetTimer`，
  或先判 `Game.GetPlayerId() ~= 0`。
- `OnPlayerMove` 目前只在**点击移动到达目标**时触发（WASD 无路点概念）。
- 回调里可以随时 `Events.On/Off`（本轮已在派发的函数列表不受影响）。

---

## 5. 定时器

```lua
Game.SetTimer(5, function()          -- 5 游戏秒后触发一次
    Log.Info("5 game-seconds later")
end)
```

- 按**游戏时间**计时（受倍速影响；暂停时不走）。
- 一次性：触发后自动移除；热重载会连同旧订阅一起清空。
- 回调里的错误与事件回调同等处理（归因 mod、禁用、修复后可恢复）。

---

## 6. 调试、错误与防护

### 6.1 打印

`print(...)` 与 `Log.Info(...)` 等价写日志（多个参数以 Tab 分隔，前缀 `[script-mod] [mod-id]`）。
`print` 尊重 `tostring`/`__tostring`；调试复杂结构可用 `string.format`。

### 6.2 报错怎么定位

脚本运行错误会带**路径:行号 + stack traceback** 记入日志：

```
[script] mod [my_first_mod] 脚本错误（事件 OnTick）：mods/my_first_mod/scripts/main.lua:5: attempt to call a nil value
stack traceback:
    ...
```

### 6.3 错误隔离与恢复

- 出错的 mod 会被**立即禁用**并打印原因；其它 mod 与游戏本体不受影响（游戏不崩）。
- 禁用后产生再多的相关事件也不会再执行该 mod（避免日志/性能风暴）。
- **修复文件并保存**（触发热重载）→ 重载成功后自动解除禁用；重载失败则保持旧版本闲置、
  提示"修复文件再保存可恢复"。

### 6.4 死循环防护

单次脚本调用（入口执行 / 事件回调 / 定时器回调）最多执行约 **500 万条 Lua 指令**，
超限会中止本次调用并按普通错误隔离处理（如 `while true do end` 会被拦下）。
注意：这是"防手滑"级别的防护——如果脚本自己用 `pcall` 把错误吞掉再循环，防护不生效
（本项目定位是信任本地 mod，不是防恶意）。

### 6.5 沙箱边界

可用：`print`、`assert/error/pairs/ipairs/type/tostring/tonumber/select/pcall/xpcall` 等基础函数，
以及 `string` / `table` / `math` / `coroutine` / `utf8` 标准库。

**不可用**（一律为 `nil`）：`os`、`io`、`debug`、`package`、`require`、`dofile`、`loadfile`、`load`。
mod 不能读写任意文件、不能起进程、不能加载额外的 lua 文件（v1 单入口）。

---

## 7. 两个官方示例

| 示例 | 演示点 | 进游戏后看什么 |
|---|---|---|
| `mods/example_hello` | 最小结构：启动日志、等玩家生成后发开局物品、`OnTick` 周期查询玩家/僵尸 | 进游戏约 1 秒后日志 `starter items: bandage x2, axe x1`；每约 10 秒一行 `survivor at ...` |
| `mods/example_reactive` | 事件反应：击杀奖励（5 杀送绷带）、拾取日志、枪声日志、死亡提示、`SetTimer` | 按 `F6` 演示击杀看 `zombie N down (kills=...)`；按 `F7` 看 `gunshot at ...`；按 `F` 拾取看 `picked up ...` |

两者都可以直接改文件保存体验热重载（例如把 `example_reactive` 里"每 5 杀"改成"每 2 杀"）。
每个示例目录同时提供 `scripts/main.lua` 与 `scripts/main.py`（同 id、同行为）——
改 `config/game.json` 的 `script.backend`（或命令行 `--script.backend=python`）即可切换，
用同一份 mod 对照两个后端。

### 7.1 Python 后端（可选，M8）

启用：CMake 用预设 `x64-debug-python`（或 `-DZL_ENABLE_PYTHON=ON`）构建；运行期把
`config/game.json` → `script.backend` 设为 `python`（或加 `--script.backend=python`）。
未启用该后端的构建里，backend=python 的 mod 会以清晰错误跳过，不影响其它 mod。

Python 与 Lua 的 Host API 完全同名同参数，只是写法不同：

```python
import math

Log.Info('hi, pi=%.2f' % math.pi)

def on_tick(e):
    if e['tick'] % 600 == 0:
        p = Player.GetPosition()
        if p is not None:
            Log.Info('pos %.1f,%.1f | hp=%s' % (p['x'], p['y'], Player.GetHealth()))

Events.On('OnTick', on_tick)
Game.SetTimer(5, lambda: Log.Info('五游戏秒后提示一次'))
```

差异清单（与 Lua 对照）：
- 位置/时间返回 **dict**（`p['x']`、`t['year']`），无玩家/无效句柄返回 `None`；
- 事件回调参数是字段 dict（`e['tick']`）；**0 形参回调也可用**（与 Lua 忽略多余实参一致）；
- `Events.Off(name, fn)` 按函数对象同一性移除；`Events.Off(name)` 清空该事件全部订阅；
- `print(...)` 也接到游戏日志（`[script-py] [mod-id]`）；`import` 白名单：
  `math/random/string/json/re/itertools/functools/collections/time/datetime/...`，
  其余 `ImportError`；`open/eval/exec/compile` 等不可用（防手滑，非强安全边界）；
- 入口默认 `scripts/main.py`（可在 `mod.json` 的 `main` 显式指定）。

---

## 8. 常见问题（FAQ）

| 现象 | 原因与处理 |
|---|---|
| 日志没有"mod 已加载" | 看扫描日志：目录无 `mod.json` / JSON 语法错 / `api` 过新 / `backend` 未注册 → 日志会写明并跳过 |
| `Player.AddItem` 返回 0 | 物品 id 未注册或背包容量不足 |
| 中文日志乱码 | 脚本文件保存为 UTF-8（BOM 兼容） |
| 改了文件没重载 | 确认保存成功；重载轮询默认 1 秒（`config/game.json` → `script.hotReloadSeconds`）；也可在配置里关闭 `script.hotReload` 排查 |
| 想关掉全部脚本 | `config/game.json` → `script.enabled: false` |
| 能加载别的 .lua 文件吗 | v1 不支持 `require`，把逻辑写进入口文件即可 |

---

## 9. v1 已知限制

- 单入口脚本（无 `require`/多文件拆分）；无资源（贴图/音效）注册 API。
- `OnZombieDead.killer` 恒 0；`OnPlayerMove` 仅点击移动到达；`OnSaveBegin/End` 生产端未接。
- `Zombies.Count` 只统计实体僵尸（离屏虚拟僵尸不计）。
- 死循环防护边界见 §6.4。
- Python 后端（M8）与 Lua 共用同一套 Host API / 事件表（差异见 §7.1）；默认构建仍只带 Lua。
