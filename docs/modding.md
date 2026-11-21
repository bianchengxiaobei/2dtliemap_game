# ZomboidLike Mod 开发指南（Lua / Python 可选，API v2，兼容 v1）

> 适用版本：M7/M8 脚本系统 v1 + X-F2a 内容文件 / X-F2b-1 内容指纹 / X-F2b-2a 图集资源 / X-F2b-2b 搜刮表与 POI / X-F2b-2c 任务与事件 / X-F2b-2d HUD 字段 / X-F2b-2e 动态内容（物品、配方、建造、POI、任务） / M23-F5 作物、烹饪、食物状态与存档联机 / M23-F2b 动物与狩猎 / M23-F4 聚落 / M33-F1-T6 四聚落任务池/口吻 / M33-F3-T1/T2/T3/T4/T5/T6 同伴数据、招募、待命、AI、装备背包、倒地、信任委托与联机持久化 / M33-F4-T1/T2/T3/T4/T5/T6 载具改装数据契约、组件、安装/拆除、效果、UI、服务器权威、联机复制、平衡与交付 / M33-F5-T1/T2/T3/T4/T5/T6/T7 多阶段 POI 数据契约、阶段运行时/存档、门禁/钥匙/供电/路线、精英/Boss 战斗、结构化 HUD/地图/叙事、内置内容与双客户端收口 / M34-F1-T1~T7 潜行配置、输入、感知、背刺与 HUD / M34-F2-T1~T7 投掷数据、瞄准、飞行、回收、火焰、联机与平衡 / M34-F3-T1/T2/T3/T4/T5/T6/T7 陷阱数据、放置、布防、触发、存档、联机、UI 与平衡 / M34-F4-T1/T3 跨系统联动、局部扫描、投掷上限与全量回归 / M34-F4-T4 交付文档与人工验收清单 / X-F3-T1~T6 内容文件监听、事务热重载、指纹/联机策略、仅单机动态内容边界、测试与文档收口 / M35-F1 只读查询、F2 行为事件、F3-T2 命令化物品动作、F3-T3 单机直接状态效果、F3-T4 制作动作、F3-T5 建造动作、F3-T6 僵尸变体/声音引怪、F3-T7 联机策略收口、F4-T1 ModData 存档组件、F4-T2 `Mod.Data` API、F4-T3 `Json`、F4-T4 Lua `require`、F4-T5 文档/旧档兼容、F5-T1/T2 示例挑战与脚本面板、F5-T3 Audio.Play2D/Play3D、F5-T4 查询统计/截断日志、F5-T5 示例验收与 UI 点击修复 · 参考示例：`mods/example_hello`、`mods/example_reactive`、`mods/example_ui`、`mods/example_content`、`mods/example_scripted_gameplay`
> （示例目录都同时带 `scripts/main.lua` 与 `scripts/main.py`，用 `script.backend` 切换；`example_content` 另带物品/配方/建造/搜刮表/POI/任务数据与图集）
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
      ├─ main.lua         ← 入口脚本（路径由 mod.json 的 main 指定）
      └─ util.lua         ← 可选：Lua 用 require("util") 加载
```

### 1.2 清单 `mod.json`

```json
{
  "id": "my_first_mod",
  "name": "My First Mod",
  "version": "1.0.0",
  "api": 2,
  "backend": "lua",
  "main": "scripts/main.lua",
  "loadOrder": 100
}
```

### 1.3 入口脚本 `scripts/main.lua`

Lua 可以把逻辑拆到 `scripts/` 下的其他 `.lua`：
`require("util")` → `scripts/util.lua`，`require("wave.director")` → `scripts/wave/director.lua`，
`require("./local")` 相对当前脚本目录。模块返回 `return` 值（未返回时为 `true`）。

```lua
Log.Info("my_first_mod loaded")

local gaveStarter = false

Events.On("OnTick", function(e)
    -- OnGameStart 时玩家尚未生成：等 Game.GetPlayerId() 非 0，或改用 OnWorldStart
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
| `api` | 否 | 当前版本（2） | **过新会被拒绝加载**（提示"需要更新的 API 版本"）；偏旧告警但尽力加载 |
| `backend` | 否 | `config/game.json` 的 `script.backend`（默认 `lua`） | `lua` / `python`（python 需启用该后端的构建：CMake 预设 `x64-debug-python`；未启用时该 mod 会被跳过并提示"本构建未启用 Python 后端"） |
| `main` | 否 | 按后端：`scripts/main.lua` / `scripts/main.py` | 入口脚本（相对 mod 根目录） |
| `loadOrder` | 否 | `100` | 越小越先加载/派发事件（同值按加载先后） |

任何一项不合法（JSON 坏、类型不对、api 过新、后端不存在……）都只会**跳过这一个 mod** 并在日志说明原因，不影响其它 mod 与游戏本体。

---

## 3. Host API（v2；v1 函数保持兼容）

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
| `Game.GetApiVersion()` | number | 当前 Host API 版本（M35 起为 2） |
| `Game.IsMultiplayer()` | bool | 是否处于联机（主机/加入/loopback 均为 true） |
| `Game.GetNetMode()` | string | `direct` / `loopback` / `client`；主机与加入统一为 `client` |
| `Game.GetTick()` | number | 当前总 tick（定点 60/s，受游戏倍速影响） |
| `Game.GetPlayerId()` | number | 玩家实体稳定 ID（`0` = 尚未生成） |
| `Game.GetTime()` | table | `{year, month, day, hour, minute, totalMinutes}` |
| `Game.SetTimer(seconds, fn)` | - | 一次性定时器：`seconds` 为**游戏秒**，到点在随后的 tick 触发一次 |
| `Game.ToggleInventory()` | bool | 打开/关闭背包，返回切换后的打开状态（菜单打开时无效） |
| `Game.IsInventoryOpen()` / `Game.IsMenuOpen()` | bool | 查询核心 UI 当前是否打开 |

### 3.3 `Player`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Player.GetPosition()` | `{x, y, z}` 或 nil | 玩家位置（无玩家 → nil） |
| `Player.Teleport(x, y, z)` | bool | 传送到格坐标（含物理体同步） |
| `Player.GetHealth()` | number 或 nil | 当前 HP |
| `Player.GetNeeds()` | table 或 nil | `{hunger,thirst,fatigue,nutrition,health,healthMax,moveScale,damageScale,warning}`；需求/营养为 0~1 |
| `Player.GetStamina()` | table 或 nil | `{current,max,exhausted}`；无战斗体力组件 → nil |
| `Player.GetWounds()` | table 或 nil | `{count,wounds=[{type,severity,bandaged,bleeding}]}`；`type` 为 `blunt/bite/claw/fire/debug` |
| `Player.GetPain()` | table 或 nil | `{level,baseLevel,reliefRemaining}`；`level/baseLevel` 为 0~1 |
| `Player.GetInfection()` | table 或 nil | `{stage,progress,slowRemaining,timeToNextStage}`；`stage` 为 `none/incubating/fever/severe` |
| `Player.GetClimate()` | table 或 nil | `{bodyC,wetness,ambientC,effectiveC,insulation,waterproof,indoor,nearFire,warning}` |
| `Player.GetProgression()` | table 或 nil | `{level,xp,xpNext,perkPoints,skills={melee={level,xp,xpNext},...},perks=[...]}`；无成长组件 → nil |
| `Player.GetInventory(offset?,limit?)` | table 或 nil | `{weightKg,capacityKg,totalStacks,stacks=[...],truncated}`；直接读背包容器，不要求背包 UI 打开 |
| `Player.GetEquipment()` | table 或 nil | `{primary=stack 或 nil,body=stack 或 nil}`；无装备槽时两个字段都缺省 |
| `Player.CountItem(defId)` / `Player.HasItem(defId,count?)` | number / bool | 只统计背包，不含装备槽；`count` 缺省 1 |
| `Player.AddItem(defId, count?)` | number | 入背包实际数量（`count` 缺省 1；容量不足/未注册 id → 0） |
| `Player.UseItem(index?)` | result | 使用背包物品；`index` 缺省 -1 = 第一件可用消耗品 |
| `Player.EquipItem(index, slot?)` | result | 装备背包物品；`slot` 为 `primary`/`body` 或 `0`/`1`，缺省 `primary` |
| `Player.Unequip(slot)` | result | 卸下装备槽；`slot` 同上 |
| `Player.DropItem(index?)` | result | 丢弃整堆；`index` 缺省 -1 = 第一堆 |
| `Player.PickupNearest()` | result | 拾取最近地面物品 |
| `Player.Pickup(entityId)` | result | 拾取指定地面物品/容器实体 |
| `Player.Attack(targetId?, heavy?)` | result | 攻击；`targetId=0` 自动瞄准最近目标，`heavy=true` 重击 |
| `Player.ApplyDamage(amount, damageType?)` | result | 单机直调；`damageType` 为 `blunt/bite/claw/fire/debug`，缺省 `blunt`；仍走战斗管线 |
| `Player.AdjustNeeds(hunger?, thirst?, fatigue?)` | result | 单机增量修改需求，结果钳制 [0,1]；负值/NaN 拒绝 |
| `Player.GiveXp(skill, amount)` | result | 单机发放经验；`skill` 为 `melee/building/crafting/survival`；额外返回奖励字段 |

`Player.GetInventory` / `Player.GetEquipment` 的 `stack` 字段：`{index,defId,name,type,count,condition,loadedAmmo,weightKg,maxStack,usable,equippable,perishable,spoilDays}`。`index` 是容器内 0-based 下标（装备槽为 -1）；未知/已删除 mod 物品仍返回 `defId`，名称/类型为空、重量 0。`GetInventory` 的 `limit` 缺省 64、最大 128，超出用 `truncated` 标记。

`Player.UseItem/EquipItem/Unequip/DropItem/PickupNearest/Pickup/Attack` 返回 `{ok,queued,error}`（`result`）：单机 direct 同步执行且 `queued=false`；loopback/KCP 提交成功返回 `queued=true`，结果由服务器执行后通过 HUD/事件体现。`ok=true` 只代表命令被接受；物品不可用、装备槽不匹配、背包满、目标超出范围等由实际命令逻辑拒绝。脚本侧明显非法的参数（负索引/越界索引、非法槽位、`Pickup(0)`、不存在/已死亡/尸体/无敌攻击目标）会直接返回 `ok=false` 和 `error`。

`Player.ApplyDamage/AdjustNeeds/GiveXp` 仅 Direct 可用；loopback/KCP 全部 `ok=false`，不排队。`ApplyDamage` 不绕过护甲/格挡/闪避/伤口：没有方向参数，有 `Facing` 时按正面来袭处理，`Debug`/`Fire` 不可格挡；`ok=true` 只表示进入伤害管线，闪避/弹反/完全格挡时实际扣血可为 0，需用 `OnDamage` 或 `GetHealth()` 判断。`AdjustNeeds` 只改 `hunger/thirst/fatigue`，不触发营养/中毒/派生字段。`GiveXp` 返回 `{ok,queued,error,characterLevels,skillLevels,perkPoints,characterXp,skillXp}`，`perkPoints` 为本次新增；成长配置关闭/缺失、未知技能、`amount<=0`/NaN 均拒绝。`Game.IsMultiplayer()=true` 时，上述三个直接状态效果以及 `Craft.Learn/Build.CanPlace/World.EmitSound` 全部 fail-closed；命令化动作（Use/Equip/Pickup/Attack/Craft.Start/Build.Place 等）仍提交服务器。v1 的 `Player.Teleport/AddItem`、`World.SpawnItem`、`Zombies.Spawn` 为兼容例外，仍按本地行为执行。

### 3.4 `World`

| 接口 | 返回 | 说明 |
|---|---|---|
| `World.IsPassable(x, y)` | bool | 格 (x, y) 是否可通行（未加载区域 = 不可通行） |
| `World.GetSquare(x, y, z?)` | table 或 nil | 读取已加载格的只读分层快照；未加载返回 nil。字段见下 |
| `World.SpawnItem(defId, x, y, z?)` | bool | 在格坐标生成地面物品（defId 必须先注册，见 `assets/items.json`） |
| `World.EmitSound(x, y, radius, kind?, clip?)` | bool | Direct 发出本地世界声音，进入 `OnSound`/AI 听觉；`radius` 为 (0,256]，`kind` 缺省 `generic` |
| `World.GetWeather()` | table | `{kind="clear/rain/fog/cold", ambientC, isNight}` |
| `World.GetTimeOfDay()` | number | 当天进度 0~1（0 = 午夜起点，按 `GameTime::DayFraction`） |

`World.EmitSound` 的 `kind` 支持 `generic/gunshot/impact/footstep/growl/zombie_growl/zombie/ui/vehicle`；`clip` 为空时由音频系统按类型选默认音效。仅 Direct 可用，非 direct 返回 `false`，不广播；Direct 下脚本和 AI 都通过同一个 `OnSound` 消费。

`World.GetSquare` 返回 `{loaded, hasFloor, floor, wall, furniture, item}`：`floor` 为单个对象，`wall/furniture/item` 为数组。对象字段为 `{sprite, hp, playerBuilt, blocksMovement}`；无耐久对象的 `hp=-1`。

### 3.4.1 `Entities`（M35 只读实体查询）

| 接口 | 返回 | 说明 |
|---|---|---|
| `Entities.Get(id)` | table 或 nil | 单个实体摘要；不存在/已移除 → nil |
| `Entities.Query(x, y, z?, radius?, kind?, limit?)` | table | `{count,truncated,entities=[...]}`；半径缺省 8、最大 32，`limit` 缺省 32、最大 128 |
| `Entities.GetContainer(id)` | table 或 nil | 容器/仓库内容；未搜索的 `LootContainer` 返回 `searched=false,stacks=[]` |

`Entities.Query` 的 `kind`：`any/player/zombie/animal/companion/npc/vehicle/container/storage/item/corpse/fire/other`。只返回 ECS 中存活且同楼层（`z` 相同）的实体，按距离升序、同距离按稳定 ID 升序；`limit` 截断时 `truncated=true`。实体字段：`{id,kind,x,y,z,hasHealth,health,healthMax,defId,subKind,ownerId,count,tableId,searched,downed,dead,command}`；与 `kind` 无关的字段保留默认值。容器字段：`{id,tableId,searched,isStorage,capacityKg,weightKg,totalStacks,truncated,stacks}`。

查询会计入当前 mod 的 per-tick 统计；截断时每 mod 每 tick 首条写警告（含匹配数/limit/tick/半径/kind），tick 末写一条 `次数/截断` 汇总。统计只用于观测，不会额外拒绝查询；`OnTick` 中仍建议控制半径/limit 和查询频率，避免每 tick 扫描大地图。

### 3.5 `Zombies`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Zombies.Spawn(x, y, z?, variant?)` | number | 生成一只僵尸，返回其稳定 ID（0 = 失败）；`variant` 缺省 `normal` |
| `Zombies.Count()` | number | 当前实体僵尸数（不含离屏虚拟僵尸） |
| `Zombies.GetPosition(handle)` | `{x, y, z}` 或 nil | 按 ID 查询（已死亡/不存在 → nil） |

`variant` 支持 `normal/fast/strong/armored`：HP/速度倍率分别为 100/1.0、70/1.55、170/0.78、200/0.62，外观缩放、色调、重甲减伤/抗击退沿用现有变体配置。未知值返回 0 并记录警告；`z` 仍只支持地面层语义。`Entities.Get/Query` 的 `subKind` 会返回当前变体名。

### 3.6 `Items`

| 接口 | 返回 | 说明 |
|---|---|---|
| `Items.Exists(defId)` | bool | 物品定义是否已注册 |
| `Items.GetName(defId)` | string | 显示名（未注册 → 空串） |
| `Items.GetInfo(defId)` | table 或 nil | `{id,name,icon,weight,maxStack,type,spoilDays,repairEfficiency,usable,equippable,throwable,ranged}` |

内置物品 id（`assets/items.json`）：`bandage` / `canned_beans` / `axe` / `kitchen_knife` / `machete` / `baseball_bat` / `crowbar` / `rag` / `jacket` / `leather_jacket` / `raincoat` / `tote_bag` / `water_bottle` / `painkillers` / `antibiotics` / `disinfectant` / `energy_bar` / `hammer` / `screwdriver` / `hiking_boots` / `backpack` / `makeshift_spear` / `padded_jacket` / `riot_vest` / `cooked_beans` / `magazine_cooking` / `repair_kit` / `dirty_water` / `empty_bottle` / `wood` / `radio_part` / `forage_stick` / `forage_stone` / `wild_berries` / `wild_mushroom` / `medicinal_herb` / `small_fish` / `cooked_small_fish` / `cooked_wild_berries` / `cooked_wild_mushroom` / `herbal_tea` / `fish_bait` / `fishing_rod` / `river_trash` / `fertilizer` / `fungicide` / `cabbage_seeds` / `fresh_cabbage` / `potato_seeds` / `fresh_potato` / `tomato_seeds` / `fresh_tomato` / `corn_seeds` / `fresh_corn`。

M18-F2 可选物品字段：`spoilDays`（>0 表示可腐败天数，`condition` 作为新鲜度）；`use.healthPenalty`（使用后立即扣血）；`use.replaceWith`（使用后原位替换为另一个已注册物品 id，雨水瓶回收空瓶用）。M23-F5-T3 起 `use.nutrition`（0~1 营养）与 `use.foodPoisonChance`（0~1 食物中毒概率）可用；腐败食物按 `food.spoiledPoisonChance` 提高概率，营养降低中毒概率与饥饿伤害，`FoodState` 的中毒伤害由固定 tick 结算。F5-T4 起 `FoodState` 注册 `FOOD` v1 存档组件（营养/中毒剩余/每秒伤害/总时长），单机、服务器世界与玩家档案快照都会保存；联机通过 `PlayerState v15` 的 owner-only `food` 字段同步，客户端只读显示，不本地推进中毒。F5-T5 的平衡基准把 `hearty_stew` 设为饥饿 0.85/营养 1.0/治疗 3/保鲜 2.5 天，并要求每份批量制作消耗燃料、锅具不消耗；自定义食物可参考该收益与 `food.*` 安全上界。M20-F1 新增 `use.infectionCure`（0~1，直接降低感染病程）、`use.infectionSlow`（0~1，抑制强度）与 `use.infectionSlowSeconds`（0~600 秒）；`infectionSlow > 0` 时必须提供持续时间，无感染时抗生素/消毒剂拒绝使用且不消耗。M23-F6-T2 新增 `use.infectionRisk`（0~1，有伤口时使用物品的感染风险）；概率按确定性子种子判定并受 `infection.maxChance` 约束，无伤口不触发，内置污染绷带示例为 0.35。内置雨水收集器用 `object.locker` 占位图，制作站 id 为 `locker`。

M23-F3 火焰数据：`fire.file` 指向 `assets/fire.json`（`fire-v1`），每个材料含唯一 `id/name`、`sprites[]`、`flammability{ignitionThreshold,burnSeconds,smokePerSecond,heatRadius}` 与可选 `charredDrops[{item,count}]`；未知字段、重复 id/sprite、越界值整体拒绝。当前火焰文件由启动配置直接加载，不参与 `content/` 合并；需要不同火焰配置时用 `--fire.file=...` 指向另一份文件，并确保服务器/客户端一致。

M18-F3 内置睡眠：`building.types` 中 id 为 `bed`、sprite 为 `object.bed` 的可建造项触发床边交互；`config/game.json` 的 `sleep.*` 控制睡眠时长/时间倍率/疲劳恢复/僵尸中断半径。当前仅单机、仅内置床，mod 自定义床位与联机睡眠未开放。

M18-F4 建造项扩展字段（均为可选）：`spriteVertical`/`openSprite`/`openSpriteVertical` 提供横竖朝向和门打开时的 sprite；`door:true` 启用 E 开关门与关门碰撞（必须同时提供 `openSprite`）；`layer` 为 `wall`/`furniture`/`floor`（默认 `furniture`）；`category` 为 `defense`/`facility`/`storage`（默认 `facility`）。门状态通过 sprite + `blocksMovement` 写入 chunk diff，不新增布尔字段；木地板使用 `SquareObject.underlay` 记录覆盖前地面，chunk diff 已升 v3（兼容 v1/v2）。地图自带门/墙 `playerBuilt=false`，不会被玩家维修/拆除。

M19-F2/F6-T1/T2/T3/M24-T2 POI 数据（当前内置 11 个固定区域：加油站/诊所/农场仓/圣玛丽医院/军事检查站/超市/警察局/消防站/汽车旅馆/教堂/仓库，另支持 mod 追加；联机由服务器按已加载 chunk 生成并同步）：`config/game.json` 的 `pois.defs[]` 每项包含 `id/name/threat(1..3)/markerX/markerY/containers[]/enemies/task`；`containers[]` 为 `{x,y,tableId,sprite}`，`enemies` 为 `{count,kind:"normal|fast|strong|mixed|armored",armoredCount?,radius}`，`task` 为 `{description,targetTable,rewardXp,rewardItem,rewardCount}`。`targetTable` 必须指向该 POI 自己的某个容器表；基础搜刮表在 `assets/loot.json`（`loot-v1`），mod 可在 `content/loot.json` 追加。任务完成条件为目标表首次搜索 + 该 POI 的 `PoiGuard` 全部清除；完成状态存 `PoiTasks`，奖励物品只支持已注册 id。内置地图建筑坐标在 `tools/gen_map.py` 的 `POI_SPECS`，修改后需重新生成地图并同步 `marker/containers` 配置；生成器 v5 的医院用 `interiorWalls` 生成带门洞隔墙，军事检查站复用 `object.barricade` 路障；M24-T2 的超市/警察局/消防站/汽车旅馆/教堂/仓库各自至少 3 个容器、独立掉落表与自动任务（T3 为仓库追加第 4 个中继货架 `poi_warehouse_relay`），均不新增 tile 集。F6-T2 的医院为威胁 2/7 名 mixed 守军，药房/物资箱含 `tainted_bandage`；军事检查站为威胁 3/9 名 strong 守军，军械柜保底手枪 1+9mm 16+霰弹 8，容量 24、rolls 4。M24-T2 的超市/旅馆为威胁 1，警察局/消防站/教堂为威胁 2 且各 1 名重甲，仓库为威胁 3/8 strong/2 重甲；目标表分别为 `poi_supermarket_storage`、`poi_motel_storage`、`poi_police_armory`、`poi_fire_gear`、`poi_church_supplies`、`poi_warehouse_valuables`。M24-T6 将超市食品架/仓库食品区 rolls 调为 3（仓库罐头汤保底 1）、警察局枪柜 9mm 保底 6、仓库贵重品柜霰弹单条 max 6，保持单 POI 食物 ≤1.5 日、警局 9mm 12~24、仓库霰弹 6~10 的期望区间；修改这些表后需同步 `tools/tests/test_m24_balance.py`。F6-T3 起 `enemies.armoredCount`（0~count，默认 0）把前 N 名守军替换为 `armored` 军械守卫：2.0 HP/0.62 速度/14.5 咬击，爪/咬/钝减伤 35%、击退 ×0.35，火焰/调试伤害不减；内置军事检查站 `armoredCount=2`（2 精英 + 7 strong）。`armoredCount` 与 kind 均进入内容指纹。mod POI 不生成房间，需自行保证坐标可达；新增 POI 需同时补房间、容器、搜刮表与玩家可达性测试。

M19-F3/F6-T4/F4-T4/M24-T3/T4/M33-F1-T6 任务数据（当前内置 35 条、13 条一次性链、8 个随机事件；联机由服务器权威推进并同步）：`config/game.json` 的 `missions.defs[]` 每项包含 `id/name/chain/order/kind/source/task?/prerequisite/minDay/minSettlementReputation?/chance/autoActivate?/timeLimitDays?/retryDays?/objectives/reward`。`kind` 为 `once|daily|random`；`source` 为 `radio|note|npc|base`；`task` 可选 `generic|help|escort|clear|supply`，仅用于委托类型/HUD 标签，缺省 generic。`autoActivate` 默认 true；设为 false 时不会自动接取、每日刷新或随机触发，只能由对话/脚本等显式接取（当前聚落四段链与北部/南部/河畔单点任务）。`timeLimitDays` 为 0 或省略表示不限时，否则在接取日 + N 天到期失败；`retryDays` 默认 1，失败/放弃后需等待该天数才可重接。`minSettlementReputation` 为可选 -100~100，缺省 -100；聚落任务按当前聚落分数过滤，未达到时对话显示锁定提示。`objectives[]` 每项为 `{kind,target,count,description}`，其中 `kind` 支持 `collect|interact|kill|loot|poi|reach`，`loot` 的 `target` 可为 `any` 或搜刮表 id，`poi` 指向 `pois.defs[].id`，`reach` 另需 `x/y/radius`。`reward` 支持 `xp/skill/item/itemCount/extraItem/extraItemCount/blueprint/label`；`extraItem` 为可选的第二件奖励（如伏击的止痛药），`blueprint` 为配方 id，完成后直接写入 `KnownRecipes`。`building.types[]` 的 `requiresMission` 可把设施绑定到任务完成状态（当前 `rain_collector` = `farm_tools`）。任务完成/进度写入玩家 `MissionLog` 存档组件；新增任务需同步 Python `test_mission_data.py` 的引用校验。M33-F1-T6 追加 `north_blood_drive`（诊所绷带）、`south_harvest_help`（农场野果）、`river_fish_supply`（桥头小鱼）3 条聚落任务，分别由北部诊所、南部农场、河畔营地独立 `missionPool` 提供；均 `source=npc`、`autoActivate=false`、2–3 天时限、1 天重试，最低声望 -39/0/-39。F6-T4 新增医院/军事两条两段链（`hospital_supply→hospital_quarantine`、`military_armory→military_extract`）与两个随机击杀事件；链首用 `poi` 目标读取 `PoiTasks.completed`，链尾用特定 `loot` 表 + `reach`。F4-T4 新增四段聚落链 `settlement_help→settlement_escort→settlement_clear→settlement_supply`（求助/护送/清剿/物资），由 `assets/settlements.json` 的 `missionPool` 提供给求助者；护送当前为“医院南门接应 + 返回村庄”的路线目标，不生成跟随 NPC。F6-T5/F4-T4 起 once 任务失败后记录失败日，按 `retryDays` 重试；前置未完成不会提前解锁，已完成/进行中不会重复接取，奖励仅在 Active→Completed 时发放一次。M24-T3 追加 `super_stock→super_return`、`police_case→police_return`、`motel_guest→motel_return`、`relay_recover→relay_extract`；中继链 `relay_recover` 前置 `radio_repair`，同时要求搜索 `poi_warehouse_relay` 并持有 `relay_part`。任务叙事写入 `objectives[].description` 与 POI `task.description`；HUD 会按当前未完成目标解析 POI marker、匹配 loot 表容器、reach 坐标或电台坐标，追加“目标<方位> <距离>格”。M24-T4 追加 `event_wandering_horde`（第 3 天/0.50/8 杀）、`event_infected_house`（第 2 天/0.55/3 杀+`event_infected_cache`）、`event_survivor_ambush`（第 4 天/0.40/2 名）、`event_cold_snap`（第 2 天/0.35/到达村庄）；随机事件总数 8。空投 `event_supply_drop` 保持 `loot any×2`，由 `game/RandomEvents` 生成 2 个 `event_supply_crate`；事件箱/敌人按 `RandomEventStableId(mission,day,index)` 去重，事件敌人标签随存档保存。事件内容只由单机 `UpdateMissions` 或服务器 `TickServerMissions` 生成，网络客户端不本地生成；寒潮使用 `WeatherSystem` 临时覆盖 15 游戏分钟，不写存档字段。mod 任务若复用 `event_` 前缀并依赖事件内容，需要自行提供同 id 的 `RandomEvents` 内容计划，否则只作为普通随机任务。
M19-F4 装备数据（武器/衣物）：weapon 继续使用 `combat`；clothing 可新增 `armor{claw,bite,blunt,wear,moveScale,insulation,waterproof}`，各字段均为 0~1（`moveScale` 0.5~1.0），`condition` 会在运行时按比例削弱减伤。weapon/clothing 可新增 `repairEfficiency`（0.1~3.0，缺省 1.0），维修包恢复量 = `inventory.repairAmount × repairEfficiency`。内置新增 `machete`/`baseball_bat`/`crowbar` 与 `leather_jacket`/`raincoat`/`riot_vest`，图标复用现有占位资源；`insulation/waterproof` 已由 M20-F2 用于体温/潮湿结算（保暖降低失温、防水减少淋湿）。护甲只读取躯干槽，减伤在 `CombatSystem` 写伤口之前生效。

M30 战斗 2.0 配置（T7）：`config/game.json` 的 `combat.rules.*` 共 43 项，包括 `staminaMax/staminaRegenPerSecond/staminaRegenDelaySeconds/staminaExhaustedThreshold/staminaExhaustedSeconds/fatigueMaxPenalty/fatigueRegenPenalty`、`heavy*` 9 项、`block*` 8 项、`parry*` 5 项、`dodge*` 5 项、`poise*` 4 项、`poiseMaxNormal/Fast/Strong/Armored` 4 项与 `armoredStaggerPoiseThreshold`。缺失键使用 `CombatRuleConfig` 默认值；NaN/越界在 `SanitizeCombatRuleConfig` 回退或钳制；单机/服务器共用 `WorldSimulation::LoadCombatRuleConfig`，命令行 `--combat.rules.<key>=...` 同样生效。武器 `combat{stamina,windup,poise,blockReduction,blockStamina,heavy}` 仍按物品数据覆盖全局规则；旧扁平 `combat` 继续兼容。

M23-F1 远程武器数据：weapon 可新增 `ranged{damage,range,spreadDegrees,pellets,cooldownSeconds,magazine,reloadSeconds,ammoDefId,noiseRadius,wearPerShot,projectileSpeed,falloffStart,falloffEnd,falloffScale}`，14 个字段全部必填；`pellets`/`magazine` 为整数，`falloffStart <= falloffEnd <= range`，`ammoDefId` 必须指向已注册物品。远程武器可同时保留 `combat` 作为近战备用；当前 T1–T6 已接线单机射击/换弹/投射物/枪声引怪与服务器权威扣弹/换弹，`noiseRadius` 直接作为 Gunshot 世界声音半径；联机弹匣/换弹复制与 HUD 已由 T7/T8 接线；双客户端测试见 T10，实机验收留 T11。

M34-F2 投掷数据：物品可新增可选 `throwable{range,damage,impactRadius,arcHeight,noiseRadius,returnable,breakChance,fire?}`；`range` 1–16、`damage` 0–80、`impactRadius` 0–3、`arcHeight` 0–3、`noiseRadius` 0–32、`breakChance` 0–1，`returnable` 必须为布尔值。`fire` 可选，格式 `{radius,minutes}`，半径 0–3 格、持续 0–240 游戏分钟；仅带该对象的投掷物在落地时接入火焰。未带 `throwable` 的旧物品/旧 mod 保持不可投掷；`forage_stone`、`makeshift_spear`、`bone_spear` 已带回收和损坏风险，`molotov` 为一次性火焰投掷物。`craft_molotov` 配方固定为空瓶 + 破布 + 燃料罐、`workbench`、1.5 秒；F2-T6 已固定数值：石块 7.5/8/0.6/1.0/6/回收 95%，简易长矛 9/28/0.45/1.2/5/回收 65%，骨矛 8.5/24/0.45/1.1/4.5/回收 55%，燃烧瓶 8/6/1.2/1.1/5/一次性/1.5 格 45 分钟。F2-T7 回归确认这些值、噪声和回收风险不再变动。非法字段/越界值/未知字段会使该物品文件或 mod 事务整体回退，不产生部分定义。`throwable` 全部字段进入内容指纹，M34-F2-T1 起整体指纹为 **v22**；F2 不新增存档 schema，F3 才升 v23。服务器命令 `ThrowItem=62` 用于联机投掷；不含该命令或内容哈希不一致的旧客户端在握手阶段拒绝。旧存档可加载，但飞行投掷物在保存/退出前结算、冷却不持久化，且不会获得 T6 之后新增的加油站燃烧瓶掉落。

M34-F3-T1/T2/T3/T4/T5/T6 陷阱数据：`config/game.json` 新增 `trapDefs.enabled`/`trapDefs.file`（默认文件 `assets/traps.json`，`traps-v1`）；定义字段为 `id/name/buildingId/damage/radius/cooldownSeconds/uses/staggerSeconds/noiseRadius/triggerMask/armSeconds/fire?`。`buildingId` 必须指向已加载的 `building.types`；`triggerMask` 只能包含 `zombie`/`animal`，缺省两者；`fire` 可选 `{radius,minutes}`（0.5–3 格、1–240 游戏分钟）；`damage` 0–100、`uses` 1–99，且必须至少提供伤害、噪声或火焰之一。内置 `bear_trap`（55 伤害/2.0s 硬直/4 次）、`tripwire_alarm`（18 格噪声/8 次）、`fuel_trap`（1.5 格 45 分钟火焰/1 次），三者 `armSeconds=1.0`。`mods/<id>/content/traps.json` 按加载顺序追加，非法条目逐条跳过，重复 id 不能覆盖基础定义；`config/game.json` 的 `building.types` 与 `DefaultBuildables` 已加入对应 3 个木材建造项。`traps.json` 全部字段进入内容指纹，M34-F3-T1 起整体指纹为 **v23**；`PlacedTrap` 使用 `TRP2 v2` 存档（defId/armed/cooldown/uses/owner/armRemaining，兼容读取 v1）和 `ZLR1 TRAP2` 只读复制。T1 提供数据/组件/注册；T2 已接建造放置、owner、`armSeconds` 自动布防、`E` 布防/解除与 `X` 拆除返还，且 `PlacedTrap` 已进入 ZLR1 Prop 候选并在客户端还原到 ECS；T3 已接 `TrapSystem` 触发：按 `triggerMask` 只选僵尸/动物，排除玩家/友方/尸体/Raider/异层并取最近目标，捕兽夹结算伤害/硬直/冷却，绊线发噪声事件，燃油陷阱经 `FireSystem::IgniteForcedAt` 在 `fire.radius` 覆盖格强制创建 `trap_fuel` 火源（`fire.minutes` 换算为真实秒数），次数耗尽自动解除。T4 已接单机快照、服务器 `SaveWorld`/重启恢复，保存布防剩余时间、冷却、次数与 owner；旧档无 `PlacedTrap` 仍正常加载，旧 `SpikeTrap`/电网不被迁移。mod 作者可复用 `fire.radius=0.5–3`、`minutes=1–240` 定义新燃油陷阱，但火焰伤害/蔓延/存档仍走 M23 `FireSystem`，T3 不额外保证结构燃尽掉落；`FireSource.ownerStableId` 仍按 M23 约定不落盘。`armRemaining` 已进入 `TRP2 v2`；F3-T5 已用真实双客户端验证 `TRAP2` 只读复制（defId/armed/cooldown/uses/owner）、非 owner/重复命令拒绝、服务器单目标触发和同身份重连恢复。`TRAP2` 不复制 `armRemaining`，客户端只等服务器 armed 状态。T6 新增 `TrapEffectText`/`TrapBuildSummary`，建造菜单显示效果、次数/冷却；`HudState` 增加 `trapValid/trapName/trapEffect/trapArmed/trapArming/trapArmRemaining/trapUses/trapUsesMax/trapCooldownRemaining/trapCooldownDuration/trapReadOnly/trapStatus/trapTone`，`trap-status` 显示附近最近一座已放置陷阱的布防/冷却/次数/owner 只读摘要。Lua/Python `Hud.GetState()` 暴露同名 camelCase 字段；`HudTrapStatusSuppressed` 在菜单/背包/交易/对话及角色/同伴/地图/载具改装/睡眠/战报等模态界面隐藏。E 布防/立即布防/解除复用 `InteractFacility`；X 仅在建造模式拆除，显示二次确认和返还木材。1280 单机 core_ui 烟雾已补 7 张截图，双窗口表现仍留 F4 联调。F3-T7 已锁定平衡：木材成本捕兽夹 3/绊线 1/燃油 4；捕兽夹 55 伤害/2s 硬直/4 次/2s 冷却，绊线 0 伤害/18 格噪声/8 次/1s 冷却，燃油 0 直接伤害/1.5 格 45 游戏分钟/1 次。mod 新增陷阱仍走同一 `traps-v1` 校验与内容指纹，不新增陷阱实体或地图格式字段。

M20-F1 感染数据：`config/game.json` 的 `infection` 段控制暴露概率、病程总秒数、阶段阈值、未包扎伤口加速、发热疲劳和重症掉血；`clawChance`/`biteChance` 是基础概率，已有未包扎伤口按 `woundRiskBonus` 增加暴露概率，最终不超过 `maxChance`。感染阶段由 `progress` 与 `incubatingEnd`/`feverEnd` 派生：潜伏→发热/疲劳→重症→死亡。绷带战利品/制作、诊所/村庄储物柜保证产出的抗生素与随机消毒剂都通过现有 `use` 字段治疗；治疗道具只在感染激活时可使用。感染状态为 `INFC` v1 存档组件，同时经 `ZLR1` 复制和 `ZLP1 v4/v5` owner 镜像；HUD 字段为 `infectionStatus`/`infectionTone`。

M20-F2 天气/温湿数据：`config/game.json` 的 `weather` 段控制天气块周期、过渡时长、四类天气权重、各天气环境温度、夜间降温、僵尸视距/移速缩放与潮湿强度；`climate` 段控制淋雨/烘干速率、室内与篝火增温、衣物保暖/湿冷、失温阈值、疲劳/移速惩罚与失温伤害。天气由累计游戏时间确定性生成，不需要额外随机游标；`ecs::Climate` 的 `bodyC/wetness` 写入 `CLMT` v1，`ZLP1` v5 只读镜像。HUD 字段为 `weatherStatus`/`weatherTone`/`weatherClass`，`weatherClass` 为 `clear|rain|fog|cold`，对应 `weather-overlay` 调色。

M20-F3 特殊僵尸/尸潮数据：`config/game.json` 的 `baseEvent` 段新增 `maxWaveSize`、`scalePerDay`、`scalePerStructure`、`specialStartWave`、`specialCount`、`howler*` 与 `breaker*`。`scalePerDay` 使用生存天数（第一天为 1），`scalePerStructure` 使用触发时基地半径内的玩家结构数；两者只放大每波数量，最终同时受 `maxWaveSize` 与 `maxAlive` 限制。特殊僵尸在第二波起按波次交替生成，`ZombieSpecial` 的 `kind/cooldown/windup` 进入 `ZSPC` v1 与 `ZLR1`；Mod 若读取 HUD，沿用 `siegeStatus/siegeTone` 即可看到“特殊 嚎N 破N”和蓄力文本，无需新增字段。

M20-F4 基地经营数据（单机）：`config/game.json` 的 `farming` 控制 `growMinutes/waterMinutes/maxWaterMinutes/witherMinutes/harvestYield/seedYield/seedItem/produceItem/waterItem`；`power` 控制 `fuelMinutesPerCan/radius/fuelItem`；`traps` 控制 `damage/radius/cooldownSeconds/staggerSeconds/uses`；`trading` 控制 `range/merchantX/merchantY/restockDays`。建造项 `farm_plot/generator/fridge/spike_trap` 仍在 `building.types[]`，设施精灵使用 `object.farm_plot/generator/fridge/trap` 逻辑名（当前映射自有图集占位帧）；新物品 `cabbage_seeds/fresh_cabbage/fuel_can` 在 `assets/items.json`，农场/加油站掉落写入 `assets/loot.json`。`FARM` 为 v2（F5-T2 增加病害/施肥标记，旧 v1 兼容），`GENR/COLD/TRAP/MRCH` 为 v1 存档组件；`MerchantStock.stock` 固定 5 档（对应内置报价），`restockDay` 按生存日补货。HUD 新增 `baseFarms/baseFarmGrowing/baseFarmMature/baseFarmDiseased/baseFarmThirsty/baseFarmFertilized/baseGenerators/baseGeneratorsOnline/baseFridges/baseFridgesPowered/baseTraps/baseTrapsArmed` 字段（Lua/Python `Hud.GetState()` 同名）；农田/发电机/陷阱/商人提示复用 `action-hint`，交易面板无需新增 RML 元素。Mod 新增同类设施时应复用 `WorldDemoState` 的建造分支与 `Homestead` 纯规则，不能只放置世界对象而不写状态组件。F5-T1 起多作物由 `assets/crops.json` 驱动，`farming.*` 的 `growMinutes/harvestYield/seedItem/produceItem` 只在作物数据缺失时作为回退；详见 3.6.3.2。

### 3.6.1 Mod 内容文件（X-F2）

M7/M8 的脚本 API 负责行为，X-F2 起 mod 还可以在 `content/` 下声明**物品、配方、作物、建造项、搜刮表、觅食/钓鱼节点、POI、任务、事件、聚落、同伴和车辆改装**，启动时合并进基础注册表；X3-T2 起单机 content 文件变更为整批事务热重载，不需要改 C++。参考实现：`mods/example_content`。

目录结构：

```text
mods/my_content_mod/
├─ mod.json
├─ content/
│  ├─ items.json
│  ├─ recipes.json
│  ├─ crops.json
│  ├─ buildings.json
│  ├─ loot.json
│  ├─ forage.json
│  ├─ pois.json
│  ├─ missions.json
│  ├─ settlements.json
│  ├─ world_events.json
│  ├─ companions.json
│  ├─ vehicle_mods.json
│  └─ traps.json
└─ assets/
   ├─ asset_manifest.json
   └─ atlas_example.atlas.json
```

全部内容文件和 `assets/` 都可省略；`mod.json` 仍然必需，内容按 `loadOrder`（缺省 100，越小越先）和目录名排序合并。合并发生在脚本加载前，因此内容对 `OnGameStart`/`OnWorldStart`/`OnTick` 和核心 UI 都可见。

**items.json**：与 `assets/items.json` 相同，根为 `{"format":"items-v1","items":[...]}`；字段校验、`use`、`combat`、`ranged`、`armor`、`spoilDays`、`unlocksRecipes` 规则与内置物品一致。`use` 另支持 `nutrition`（0~1 营养）、`foodPoisonChance`（0~1 食物中毒概率）与 `infectionRisk`（0~1 使用感染风险）；`ranged.ammoDefId` 可引用同批或更早加载的弹药物品，不能引用更晚加载的 mod。

**recipes.json**：与 `assets/recipes.json` 相同，支持 `recipes-v4`（兼容 v1/v2/v3）的 `{id,name,category,output,ingredients,tools,cookware,fuel,station,time}`。`category` 取 `general/cooking/preservation/medical/crafting`；`cookware` 为 `{item,count:1}` 的不可堆叠锅具（不消耗），`fuel` 为 `{item,count}`（每次制作消耗）；v4 才允许这三个字段。`station` 仍指向地图逻辑对象，例如 `campfire`/`workbench`/`locker`。内置烹饪示例：`cook_small_fish`/`cook_wild_berries`/`cook_wild_mushroom`/`brew_herbal_tea`/`cook_hearty_stew`（篝火），以及 `craft_fishing_rod`/`craft_fish_bait`（无站点）；`forage_meadow` 也会产出鱼饵。

**buildings.json**：新增格式：

```json
{
  "format": "buildings-v1",
  "types": [
    {
      "id": "my_crate",
      "name": "补给箱",
      "sprite": "mod.supply_crate",
      "woodCost": 3,
      "maxHp": 0,
      "blocksMovement": false,
      "storageKg": 35,
      "storageSlots": 4,
      "category": "storage"
    }
  ]
}
```

字段与 `config/game.json` 的 `building.types[]` 相同。`storageKg > 0` 的建造项会自动走现有仓库实体、容量/堆数、存取 UI、拆除掉落与联机复制路径；`layer/door/openSprite/owner` 等规则也复用现有实现。非存储设施若需要新的运行时行为，仍要由 C++ 提供对应组件/系统，纯数据只能放置静态对象或复用已有行为。

合并语义：

- 每个 mod 的 items+recipes 作为一个事务；物品/配方校验失败（重复 id、未知材料、未知 `unlocksRecipes` 等）只跳过该 mod，之前已接受的 mod 内容保留。
- `unlocksRecipes` 可以引用同 mod 或更早 mod 的配方；不要前向引用更晚加载的 mod。
- 建造项重复 id 会被拒绝；非存储建造项的 sprite 不能与已有项冲突；两个存储项可复用同一 sprite，但容量或堆数必须不同（客户端/服务器用容量/堆数区分）。
- `loot.json` 在基础 `assets/loot.json` 后追加表；表 id 与已有表冲突时整份 mod 战利品文件拒绝且不改动已有表。表项可引用当前 mod 或更早 mod 已加载的物品；基础 `seed` 保持不变。格式与字段见 3.6.4。
- `forage.json` 在基础 `assets/forage.json` 后追加觅食区域/钓鱼点；区域或钓鱼点 id 与已有/同文件重复时整份文件拒绝。`tableId`（以及钓鱼点的 `trashTableId`）必须引用最终合并后的战利品表。格式见 3.6.4。
- `pois.json` 只追加新 POI id，重复 id 跳过；基础 `pois.enabled` 为 `false` 时不加载 mod POI，`guideRange` 仍由基础配置决定。mod POI 不创建地图房间/墙体，坐标和容器可达性由作者保证。
- `missions.json` 只在基础 `missions.enabled=true` 时追加任务/事件；任务 id 重复、条目无效、前置缺失或成环时整份文件跳过，已有任务不变。前置可引用基础任务或同批次更早文件的任务；同一文件内可自由引用。格式见 3.6.5。
- `settlements.json` 只在基础 `settlements.enabled=true` 时追加聚落；重复 id 跳过，条目无效跳过，格式错误/无有效定义时整份文件跳过。安全区/NPC 点位/库存/任务池格式见 3.6.3.4。
- `world_events.json` 只在基础 `worldEvents.enabled=true` 时追加世界事件；重复 id/坏字段/坏链跳过，无有效定义时整份文件跳过。格式见 3.6.3.5；T3 已接生成/结算，T4/T5 接 UI 与网络/存档。
- `companions.json` 只在基础 `companions.enabled=true` 时追加同伴定义；重复 id/坏字段/未知物品引用跳过，无有效定义时整份文件跳过。格式见 3.6.3.6。
- `traps.json` 只在基础 `trapDefs.enabled=true` 时追加陷阱定义；`buildingId` 必须引用基础或同批/更早 mod 建造项，重复 id、非法/越界字段和坏引用按单条跳过，不能覆盖基础定义。T2/T3/T4/T5/T6 起 `traps.json` 绑定的建造项会走正常建造 UI 放置为 `PlacedTrap`，`armSeconds` 后自动布防，支持 E 布防/解除与 X 建造模式拆除；触发按 `triggerMask` 选最近僵尸/动物，燃油火焰和噪声已接入；`TRP2 v2` 保存布防剩余时间/冷却/次数/owner，单机读档与服务器重启恢复，旧 v1 与旧 `SpikeTrap` 存档兼容；`TRAP2` 只读复制已通过双客户端、非 owner/重复命令拒绝和重连恢复；native/core_ui 的 `trap-status`、HUD 字段和 1280 建造/布防/触发/火焰/拆除截图已补，双窗口画面联调留 F4。
- X3-T2 起单机（`net.mode=direct`）且没有脚本动态内容时，修改 `content/*.json` 会在防抖后重新构建独立 `ContentBundle`；items/recipes/buildings/loot/forage/animals/crops/POI/missions/settlements/world_events/companions/vehicle_mods/traps 全部校验通过才一次性替换，失败保留旧内容并在日志输出 `mod 内容热重载失败，保留旧内容`。成功后记录旧→新内容指纹，`WorldDemoState` 下一次 tick 重建建造目录并复制新 POI/任务/聚落配置。`assets/*.json` 仍只通知、需重启；联机/主机只告警并提示重启服务器和所有客户端后重连；X3-T4 确认动态内容仅单机，联机热重载拒绝显示 5 秒 HUD 提示，均不替换注册表/不刷新指纹。X3-T5 已用 3 用例锁定失败原子性、未变指纹、五类动态内容联机拒绝与旧档未知物品；X3-T6 完成文档收口。脚本与 `mod.json` 的现有热重载继续生效。
- 示例 `mods/example_content` 新增 `mod_pumpkin`（南瓜作物，种子/产物）、`pumpkin_soup`（南瓜浓汤）、`cook_pumpkin_soup`（锅具+木材的南瓜汤配方）、`camp_stew`（营地炖豆）、`cook_camp_stew`（篝火配方）、`mod_supply_crate`（35kg/4 堆补给箱）、`mod_camp_cache`（补给箱搜刮表）、`mod_depot`（补给站 POI）、`mod_depot_supply`（补给站任务）和 `depot_status`（HUD 字段）；同时用 `mod.camp_stew`/`mod.supply_crate` 演示 mod 本地图集绑定，验证“无需改 C++ 新增作物→种子/产物→锅具燃料烹饪 + 可建造仓库 + 资源名 + 搜刮表/POI/任务 + HUD 字段”。

当前限制：HUD 字段已支持（见 3.7），物品/配方/建造/POI/任务可用 `Content` API 在单机运行时追加（见 3.6.6）；X3-T2 的 content 事务热重载仅限单机且无脚本动态内容，X3-T3 起联机只拒绝并要求重启+重连，X3-T4 确认动态内容仅单机、联机注册/启动拒绝并显示 HUD 提示，assets 仍只通知并需重启；新增/删除整个 mod 目录仍需重启（监听器只跟踪启动时来源），已生成实体/容器/NPC 和旧档中已删除物品 id 不回溯迁移；X3-T5 锁定旧档未知 mod 物品仍保留 `defId`，但名称/重量/图标不可用，直到重新安装对应 mod 或手动清理。动态内容不写存档、禁 mod 不回滚，联机时客户端与服务器必须安装相同的物品/配方/作物/建造/搜刮表/觅食钓鱼数据/POI/任务/聚落/世界事件/同伴内容；启动时会自动校验内容指纹（见 3.6.3），热重载成功后重算，但脚本/图集/地图/M24 运行时随机事件内容尚未纳入。启动时 POI/任务定义同时进入客户端与服务器内容指纹；服务器按已加载 chunk 生成 POI 容器/守军，并由 `TickServerMissions` 权威推进任务日志、奖励与放弃命令，联机客户端跳过本地 `UpdateMissions`。`Content` API 动态追加仍是单机能力，联机连接/主机启动会被拒绝；Mod HUD 字段只显示在客户端。

### 3.6.2 Mod 图集与资源绑定（X-F2b-2a）

mod 可以在 `mods/<id>/assets/asset_manifest.json` 声明新的逻辑资源名，启动时在基础 `assets/asset_manifest.json` 之后合并。格式与基础清单的 `logical` 段相同，provider 仍为 `own`/`placeholder_pz`；当前 provider 没有帧时会按基础逻辑回退 `own`。

```json
{
  "logical": {
    "mod.supply_crate": {
      "own": {
        "atlas": "atlas_example.atlas.json",
        "frames": [ "mod_supply_crate" ]
      }
    }
  }
}
```

图集 JSON 使用 `pz-atlas-v1`，其中 `image` 相对图集 JSON 所在目录；mod 清单里的 `atlas` 相对 mod 清单所在目录解析，找不到时再按项目根目录解析（兼容基础清单写法）。内容文件的 `sprite`/`icon` 可以直接引用这些新逻辑名。

```json
{
  "format": "pz-atlas-v1",
  "image": "atlas_example.png",
  "subtextures": {
    "mod_supply_crate": {
      "x": 0, "y": 0, "w": 64, "h": 64,
      "ox": 0, "oy": 64, "fx": 64, "fy": 128
    }
  }
}
```

合并规则：

- 只允许新增逻辑名；与基础清单或更早 mod 重名时跳过并告警，不覆盖既有资源。
- content 校验失败的 mod 不会加入资源清单；只有资源的 mod 也可以应用。
- `mods/example_content` 新增 `mod.camp_stew`（物品图标）与 `mod.supply_crate`（建造精灵），用 mod 本地图集 JSON 复用内置 PNG 帧作为示例；实际 mod 应把 PNG 放在自己的 `assets/` 下。
- 资源在进入世界时加载，修改后需重启；资源不参与内容指纹，服务器也不加载贴图，因此联机双方仍需安装同一份 mod 资源。

### 3.6.3 内容指纹与联机握手（X-F2b-1）

客户端与服务器启动时都会对**最终生效**的物品、配方、建造项、搜刮表、觅食/钓鱼数据、动物数据、作物数据、POI、任务、聚落、世界事件与同伴定义计算 64 位内容指纹（`game::ComputeContentHash`，v22），覆盖定义顺序和全部字段（含名称、图标、重量、战斗/护甲/投掷、配方分类/材料/工具/锅具/燃料/时间、建造成本/耐久/容量、战利品权重/min/max、觅食区域/钓点密度/刷新/工具/鱼饵、动物速度/警觉/逃跑/伤害/尸体搜索/掉落表/生成权重、作物阶段/水耗/病害/产量/种子、POI 容器/守军/任务奖励与多阶段 stages/entries/keys/branches/boss/narrative、任务类型/目标/前置/自动接取/时限/重试/最低声望/随机权重、聚落安全区/NPC 点位/库存/补货日/任务池/夜袭防御/区域与服务/初始声望/对话口吻、声望等级阈值/来源增量/价格倍率/每日上限、世界事件阶段/选项/奖励/压力/失败后果/冷却/预警/持续时间/nextEvent、同伴背景/生命/忠诚/装备/背包/特长/招募条件/对话 profile/个人委托阶段与奖励、车辆改装槽位/效果/材料/安装时间/重量/互斥等），并在 KCP 握手的 `Hello/Welcome` 中比较；不一致会拒绝连接。

`net.contentHash` 默认 `0`，表示自动计算；仅在需要手动覆盖时填写非 0 值，此时客户端与服务器必须一致。该指纹用于避免玩法数据顺序或数值不同造成的联机错位；脚本、图集/贴图、地图与事件内容尚未纳入，仍需双方安装同一份游戏内容。

### 3.6.3.1 动物数据（M23-F2b-T1/T2/T3/T4）

`assets/animals.json` 使用 `format: "animals-v1"`，`animals` 数组定义物种。必填字段：`id/name/kind/sprite/maxHealth/wanderSpeed/fleeSpeed/alertRadius/fleeRadius/hearingRadius/fleeSeconds/damage/attackRange/attackCooldown/lootTableId`；可选 `corpseSprite`（默认 `corpse.placeholder`）、`corpseSearchSeconds`、`corpseTool/corpseToolWear` 与 `spawn`。`spawn` 为 `{forest, meadow, poi, maxPerChunk, poiIds}`：`forest/meadow/poi` 是该区块类别 0~1 的命中权重，`maxPerChunk` 为 0~8，`poiIds` 只能引用 `config/game.json` 的 POI id；权重全 0 时上限必须为 0，配置 `poiIds` 时 `poi` 必须大于 0。`kind` 为 `passive` 或 `dangerous`；被动动物的伤害/攻击距离/冷却必须为 0，危险动物必须为正。`lootTableId` 必须在 `assets/loot.json` 存在，`corpseTool` 必须在 `assets/items.json` 存在。

当前基础物种：`deer`（鹿，被动，森林/草地）、`rabbit`（兔子，被动，森林/草地）、`boar`（野猪，危险，森林/草地/农场仓 POI）；对应尸体表 `animal_deer/animal_rabbit/animal_boar`。该数据在客户端和服务器启动时加载并参与内容指纹；暂不接受 mod `content/` 覆盖。生成全局参数在 `config/game.json` 的 `animals.spawn`（`enabled/maxLive/minPlayerDistance/poiRadius/seed`）：单机 `AnimalSystem` 只在室外草地/泥地生成，树旁算森林、无树算草地；T6 起正式服务器/内嵌服务器通过 `ServerHost::Options::spawnAnimals` 开启权威生成与战斗，客户端只接收 `Animal/AnimalCorpse` 复制，轻量服务器单测可显式关闭。动物实体游荡使用运行时 `AnimalWander`，远端按 `LodState` 降频；chunk 卸载保存到运行时快照并在重载恢复。T7 起 `AnimalWorldState`（`AWST` v1）把未加载 chunk 的动物快照（物种/位置/生命/Flee）与已播种 chunk 标记写入世界存档，单机快速存档和服务器 `SaveWorld` 都会写入稳定状态实体 `0x4D323342414E4D53`，`WorldSimulation::LoadSave` 与单机读档恢复；上限 4096 chunk / 单 chunk 16 条 / 全局 2048 条，超出部分下次加载按种子重新播种。T3 起 `alertRadius` 是视觉发现距离（需视线），`fleeRadius` 是近距离压迫半径（不要求视线），`hearingRadius` 与 `SoundEvent.radius` 取较小值作为有效听觉半径并受墙衰减，`fleeSeconds` 同时作为逃跑/野猪威胁记忆时长，危险动物的 `damage/attackRange/attackCooldown` 用于反击；`retaliating`/攻击冷却为运行时状态。T4 起动物死亡生成 `AnimalCorpse`，`corpseTool` 必须可用且 condition>0，按 `corpseSearchSeconds` 剥取并磨损 `corpseToolWear`，掉落按 `lootTableId`/尸体稳定 ID 确定性 roll 到尸体 `Container`；已剥取标记防重复。T6 起服务器权威结算动物受伤/死亡/尸体/剥取，按会话预约防止双人重复剥取；仍不做专用动画、玩家/动物碰撞；尸体掉落不自动入包，需打开尸体容器拿取。T7 已补未加载 chunk 快照与服务器保存/读档链路；真实服务器重启和长时间双客户端狩猎仍需按 `docs/07` P1-H 做人工烟测。T8 起补充本指南、README、devlog 与狩猎平衡说明；基础物种仍使用占位角色帧，不提供专用动物动画、玩家/动物推挤；M34-F1 已提供 `N` 潜行与降低脚步噪声，但动物视觉仍使用自身警觉半径，不叠加潜行视距倍率。H2 野猪反击/剥取/1920 容器、H3 F12 存档重载与 H4 双客户端/服务器重启已用临时探针做自动构造烟测；自然流程/低伤害平衡仍需人工验收。

**狩猎操作与平衡（T8）**：`SPACE` 近战，`V` 远程（需装备枪械和弹匣），靠近尸体按 `E` 剥取（需物种指定工具；当前鹿/兔/野猪均为厨刀），剥取完成后按 `I` 打开尸体容器取物。动物视觉警觉、枪声和脚步都会触发逃跑；野猪受击会反击，接近时需保留退路。M34-F1 起可按 `N` 潜行降低脚步噪声，动物视觉仍按自身警觉半径判定；也可利用树木阻挡视线接近或远程射击。

按当前 `assets/loot.json` 的 rolls/权重/min 计算期望下限（实际每具尸体随机浮动，不等于保底）：

| 物种 | 尸体容量 | 生肉 | 兽皮 | 骨头 | 熟制饥饿恢复 |
|---|---:|---:|---:|---:|---:|
| 鹿 | 12kg / 3 rolls | 约 2.82 | 0.88 | 0.71 | 约 1.19 |
| 兔 | 6kg / 2 rolls | 约 1.23 | 0.31 | 0.46 | 约 0.37 |
| 野猪 | 18kg / 4 rolls | 约 3.76 | 1.18 | 0.94 | 约 2.07 |

以 `needs.hungerPerDay=1.4` 计，熟制鹿肉约覆盖 0.85 个游戏日、野猪约 1.48 日、兔约 0.26 日。高收益由 36 只全局上限、每区块每物种 1 只、最小生成距离 18 格、弹药/厨刀工具和野猪反击风险约束；狩猎适合作为探索补给，不替代稳定农业/钓鱼。数值回归见 `tools/tests/test_animal_data.py`，验收矩阵见 `docs/07` P1-H。

### 3.6.3.2 作物数据（M23-F5-T1–T6）

`assets/crops.json` 使用 `format: "crops-v1"`，`crops` 数组定义作物。必填字段：`id/name/seedItem/produceItem/growMinutes/waterMinutes/maxWaterMinutes/witherMinutes/waterUsePerMinute/diseaseChance/harvestYield/seedYield/stages`；`seedItem`/`produceItem` 必须在 `assets/items.json` 或同批 mod 物品中存在，且不能相同；种子 id 在所有作物间唯一。`stages` 为 2~6 项 `{name,start}`，`start` 严格递增、首项 0、末项 1；当前用于状态/HUD 文案，实际结算仍由 `growMinutes` 推进。

| 字段 | 约束 | 作用 |
|---|---|---|
| `id` / `name` | 唯一 id；显示名非空 | 存档作物 id 与 HUD 文案 |
| `seedItem` / `produceItem` | 已注册物品，不能相同 | 播种扣除 1 个种子；收获产出与种子返还 |
| `growMinutes` | 10~100000 | 无季节/水分限制时的成熟所需游戏分钟 |
| `waterMinutes` / `maxWaterMinutes` | 各 ≥1，后者不小于前者 | 单次浇水补充量 / 土壤储水上限 |
| `witherMinutes` | 1~100000 | 连续缺水达到该值后枯萎 |
| `waterUsePerMinute` | 0.1~5.0 | 生长时每分钟耗水 |
| `diseaseChance` | 0~1 | 缺水病害阈值调节，不是每次 tick 随机 |
| `harvestYield` | 1~20 | 成熟时产物数量；施肥在此基础 +`fertilizerYieldBonus` |
| `seedYield` | 0~5 | 收获时返还种子数；0 表示只收产物 |
| `stages[]` | 2~6 项，`start` 严格递增、0→1 | HUD 阶段名与进度门槛 |

生长结算：每个 `AdvanceFarmPlot(gameMinutes, season, raining)` 先按雨水补水，再按 `min(gameMinutes, water/waterUsePerMinute)` 计算有效生长，乘以季节 `seasonGrowthScale`；水量耗尽后累计 `dryMinutes`。病害阈值约为 `max(15, witherMinutes × diseaseDryFraction × (1 − 0.35 × diseaseChance) / seasonDiseaseScale)`，达到后停止生长并按 `diseaseYieldPenalty` 减产；`fungicide` 只清除病害，不恢复已损失产量。`farming` 段只在 `crops.json` 缺失时回退单作物参数；`season.{spring,summer,autumn,winter}{Growth,Disease}` 仍由基础配置控制。

```json
{
  "format": "crops-v1",
  "crops": [
    {
      "id": "mod_pumpkin",
      "name": "南瓜",
      "seedItem": "pumpkin_seeds",
      "produceItem": "fresh_pumpkin",
      "growMinutes": 600.0,
      "waterMinutes": 300.0,
      "maxWaterMinutes": 600.0,
      "witherMinutes": 2880.0,
      "waterUsePerMinute": 1.4,
      "diseaseChance": 0.08,
      "harvestYield": 2,
      "seedYield": 1,
      "stages": [
        { "name": "幼苗", "start": 0.0 },
        { "name": "藤蔓", "start": 0.3 },
        { "name": "结果", "start": 0.7 },
        { "name": "成熟", "start": 1.0 }
      ]
    }
  ]
}
```

- mod 文件放在 `mods/<id>/content/crops.json`；每个文件按事务追加，id 或种子冲突、物品引用缺失、阶段非法时整份跳过，不修改已有作物。mod 作物可以引用同 mod 或更早加载 mod 的物品。
- `config/game.json` 的 `crops.file` 指向基础文件；未加载到时单机/服务器回退旧 `farming.*` 单作物参数。X3-T2 起单机 content 热重载会重读作物数据；文件树和 CropDef 都进入内容指纹（M33-F1-T1 起 v14）。
- 当前种植 E 使用客户端当前选择的种子（空田 `Q` 在可用种子间循环）；`T` 使用 `fertilizer` 施肥一次或使用 `fungicide` 杀菌；E 继续浇水/收获/清理。示例见 `mods/example_content/content/crops.json`（南瓜）。
- F5-T2 规则由 `config/game.json` 的 `farming` 段控制：`fertilizerYieldBonus`、`diseaseChance`（无作物数据时的回退）、`diseaseDryFraction`、`diseaseYieldPenalty`、`rainWaterPerMinute` 与 `season.{spring,summer,autumn,winter}{Growth,Disease}`。雨天自动补水；夏季病害风险更高、冬季生长更慢；缺水累计达到阈值后作物进入病害，施肥/杀菌状态写入 `FarmPlot` 并存档（`FARM` v2）与复制（`ZLR1` v3）。
- HUD 的状态文本显示生长阶段、缺水/水分钟、已施肥、病害和季节；基地状态行显示农田总数及生长/成熟/缺水/病害计数。脚本字段见 §3.6.7 的 `baseFarm*`。

### 3.6.3.3 烹饪与食物状态数据（M23-F5-T3–T5）

`recipes-v4` 在 v3 基础上增加三个可选字段：

| 字段 | 约束 | 说明 |
|---|---|---|
| `category` | `general/cooking/preservation/medical/crafting`，缺省 general | 背包制作面板分类筛选；只用于展示与脚本状态 |
| `cookware` | `{item,count:1}`，物品必须 `maxStack=1` | 不消耗；必须具备且可用，锅具耐久参与实际耗时 |
| `fuel` | `{item,count}`，count≥1 | 每个成功产物消耗一次；批量制作逐份扣除 |

`cookware`/`fuel` 不能与 `ingredients`/`tools` 重复；v1/v2/v3 文件出现这三个字段会被 `RecipeRegistry` 整体拒绝。`MaxCraftCount` 在容器副本上按材料、工具、锅具、燃料和输出空间预检；`Craft` 每件原子扣料/燃料、磨损工具并产出，锅具保留。实际制作时长 = `time / toolEfficiency`，锅具和工具的最低耐久决定效率（0.5~1.0 系数，缺工具/锅具为 0）。

食物 `items.json` 的可选字段：

| 字段 | 约束 | 说明 |
|---|---|---|
| `use.nutrition` | 0~1 | 食用后写入 `FoodState.nutrition`，提高饱腹缓冲并降低中毒概率 |
| `use.foodPoisonChance` | 0~1 | 基础中毒概率；腐败食物取 `max(基础, food.spoiledPoisonChance)` |
| `spoilDays` | >0 | 可腐败游戏日数；`condition` 表示新鲜度；烹饪产物也按此规则腐败 |

内置平衡（`farming`/`food` 配置不变时）：白菜 240 分钟/2 个、土豆 360/3、番茄 300/3 且病害最高、玉米 480/4 且返还 2 种子；丰盛炖菜消耗土豆+白菜+净水+木材并需锅具，产物饥饿 **0.85**、营养 **1.0**、治疗 **3**、保鲜 **2.5 天**；熏野猪肉消耗生肉+2 木材，饥饿 **0.6**、营养 **0.75**、保鲜 **4 天**。作物取舍、烹饪价值与安全上界由 `tools/tests/test_f5_balance.py` 回归。

食物中毒：`chance = clamp(base × (1 − nutrition × food.nutritionPoisonReduction), 0, 1)`；中毒持续时间与每秒伤害取当前值与 `food.poisonDurationSeconds`/`food.poisonDamagePerSecond` 的较大值。当前默认 45 秒 × 0.12 HP/s，完整中毒最多 5.4 HP；营养还会按 `nutritionHungerRelief`/`nutritionDamageRelief` 降低饥饿增长和饥饿伤害。`FoodState` 存档为 `FOOD` v1，联机由服务器权威结算并通过 `PlayerState v15` 的 owner-only `food` 字段复制，客户端只读显示。

**示例 mod（`mods/example_content`）**：`content/crops.json` 定义 `mod_pumpkin`（`pumpkin_seeds`→`fresh_pumpkin`），`content/items.json` 给南瓜和南瓜浓汤声明 `nutrition`，`content/recipes.json` 的 `cook_pumpkin_soup` 使用南瓜+净水、`cooking_pot` 锅具、1 木材燃料和篝火：

```json
{
  "id": "cook_pumpkin_soup",
  "name": "南瓜浓汤",
  "category": "cooking",
  "output": { "item": "pumpkin_soup", "count": 1 },
  "ingredients": [
    { "item": "fresh_pumpkin", "count": 1 },
    { "item": "water_bottle", "count": 1 }
  ],
  "cookware": { "item": "cooking_pot", "count": 1 },
  "fuel": { "item": "wood", "count": 1 },
  "station": "campfire",
  "time": 2.5
}
```

- 示例的 `loot.json` 还在 `mod_camp_cache` 中放入 `pumpkin_seeds`，形成“搜刮种子→种植南瓜→锅具/燃料烹饪→营养食物”的完整链路。作物/物品/配方按 mod 文件事务合并；引用缺失、id/种子冲突、配方字段非法会整体跳过该文件。X3-T2 起单机修改农业/烹饪 content 后会事务热重载，内容进入内容指纹（M33-F1-T1 起 v14，F1-T5 起 v15）；联机客户端与服务器必须使用相同内容。示例契约由 `test_mod_content.cpp` 与 `tools/tests/test_f5_mod_example.py` 回归。

### 3.6.3.4 聚落数据（M23-F4-T1/T5/T8 / M33-F1-T6）

完整字段、范围、默认值、运行时/存档/联机边界和 mod 检查清单见 [settlementformat.md](settlementformat.md)。本节是 mod 作者需要的最小契约。

`assets/settlements.json` 使用 `format: "settlements-v1"`，根节点 `defs` 数组定义聚落；`config/game.json` 的 `settlements.enabled/file` 控制基础文件。必填字段：`id/name/centerX/centerY/safeRadius/npcPoints/merchantStock/restockDays/missionPool`。

- `id` 为 1~64 字节小写 `[a-z0-9_.]`；`name` 1~128 字节；`safeRadius` 1~128 格。安全区只用于点位校验、躲避、威胁和夜袭，不是物理禁区。
- `npcPoints[]`：`{id,kind,x,y,wanderRadius}`，`kind` 为 `merchant|guard|requester`；1~16 项，id 唯一，坐标必须满足在安全圆内；`wanderRadius` 0~16。M33-F1-T6 起至少 1 个 merchant，且至少 1 个 guard 或 requester（无防御小聚落可只有 merchant+requester）。每个聚落只生成一个商人（取第一个 merchant 点）；守卫/求助者按所有匹配点逐点生成。
- `merchantStock`：固定 5 项整数（0~99），对应现有 5 档报价；运行时按每档 `maxStock` 夹紧，补货重置为 `maxStock` 而不是配置初值。`restockDays` 为 1~30 天，按该聚落商人独立使用，不再覆盖全局 `trading.restockDays`。
- `missionPool`：1~32 个唯一任务 id（每个 1~64 字节），按顺序选择第一个可接任务；同池同时只允许一条 Active。解析时不校验任务是否存在，运行时忽略缺失/不可激活项。推荐任务使用 `source: "npc"`、`autoActivate: false`、`kind: "once"`。
- 可选 `defense`（M23-F4-T5，缺省关闭）：`enabled/minDay/nightStartHour/nightEndHour/activationRadius/spawnRadius/baseRaiders/raidersPerNight/maxRaiders/guardBehavior/guardHealth/guardDamage/guardRange/guardAttackSeconds/raiderHealth/raiderDamage/raiderRange/raiderAttackSeconds/raiderSpeed`。`minDay` 1~10000；小时 0~23 且起止不同，支持跨午夜；`guardBehavior` 为 `defend|retreat`；数量/血量/伤害/速度均有上限。夜袭数量为 `baseRaiders + max(0, nightKey-minDay) * raidersPerNight`，再夹紧 `maxRaiders`；守卫生命、失守与当晚标记由 `STLS` v1 保存，夜袭 active 不跨档恢复。

mod 可在 `content/settlements.json` 追加同格式 `defs`，只允许新增 id；单条非法跳过，整份文件无有效定义时失败。M33-F1-T1 起可选 `regionId/services/startingReputation/dialogueProfile` 已进入数据契约与指纹；`services` 不得重复，`startingReputation` 为 -100~100。X3-T2 起单机 content 热重载会重读聚落数据，联机/主机或存在脚本动态内容时仍需重启；聚落字段与任务 `minSettlementReputation` 进入内容指纹 v15，M33-F3-T5 起整体指纹为 v19，M33-F4-T1 起为 v20，M33-F5-T1 起为 v21。`services` 同时控制交易/委托入口和实际夜袭/守卫防御；Hostile 只拒绝交易/委托/招募，防御仍可参与。`dialogueProfile` 只选择内置开场口吻，未知值回退通用文本。M33-F1-T6 起生成点若被挤到安全区外会跳过并下一帧重试。联机双方必须安装相同内容；服务器权威处理 `AcceptSettlementMission=39` 和 `TradeOffer=21`，失守/夜袭时拒绝接取与交易，客户端只读 `ZLR1` NPC 与 `PlayerState v21`（M33-F5-T2 起整体 v22）聚落状态/声望摘要。

扩展边界：

- **M33-F1-T2/T3/T4/T5/T6 起所有聚落均接线**：单机/服务器按 `settlementId` 和点位生成独立商人/守卫/求助者，商人库存与补货周期按聚落隔离；旧存档未绑定商人迁移到最近聚落并保留库存。死亡/失守不补生，战略恢复后重建。交易面板按商人 StableId 绑定，服务器反查聚落；对话/任务/HUD/地图按 NPC 聚落路由。M33-F1-T4 新增玩家级 `SettlementReputation`（`settlementId -> score` + 幂等键）、任务来源 `MissionLog v2`、每日交易上限与 `PlayerState v21`（M33-F5-T2 起整体 v22）声望摘要；单机/服务器任务、交易、守城/事件结算已接声望。M33-F1-T5 已接价格倍率、任务最低声望、五档对话态度与 `services` 硬限制。M33-F1-T6 新增 `river_camp`、四聚落独立任务池/库存/口吻；自定义对话节点与按聚落商品表仍需改 C++。
- 对话树为代码内置，`settlement.name` 只进入说话人，`dialogueProfile` 只选择内置开场口吻；当前没有对话 JSON，不能通过内容文件新增 NPC 类型、对话节点/选项或结果。
- 交易报价全局固定 5 档，不能按聚落改商品表；实际价格按声望买卖倍率调整。守卫策略只有 `defend/retreat`。新增类型/策略需改 C++。
- 追加聚落不会创建地图房间、墙体、床或容器；坐标、地面和可达性由地图/POI 作者保证。

### 3.6.3.5 世界事件数据、排期、UI 与联机（M33-F2-T1/T2/T3/T4/T5/T6/T7）

`assets/world_events.json` 使用 `format: "world-events-v1"`，根节点 `defs` 数组定义世界事件；`config/game.json` 的 `worldEvents.enabled/file/randomSeed` 控制基础文件。M33-F2-T1 建立数据契约与加载，M33-F2-T2 新增 `WorldEventDirector` 确定性排期与状态机，M33-F2-T3 新增生成计划/稳定 id/结果账本与经验/物品/声望/区域压力结算；M33-F2-T6 起基础文件默认启用并内置 5 类主事件 + 1 个链后续。启用后每日至多 1 个事件，按 `randomSeed`/日期/定义权重/区域面积权重选择定义和区域，站点优先区域 POI；同一天重复 Update 不会重复排期，事件链默认排到次日，链目标（如 `world_caravan_aftermath`）不会被随机主排期选中。`Scheduled` 在 `activeMinute-warningMinutes` 进入 `Warning`，`Active` 使用绝对截止分钟，阶段推进/选择/超时/失败由导演维护；玩家离开只降级为摘要并记 `downgraded`，不会暂停计时。T3 在 Active 且进入详细半径时按 `Kill/Defend` 阶段生成僵尸、按 `Loot` 阶段生成容器，StableId 由事件 id/序号确定；击杀生成目标会推进当前 Kill/Defend 阶段。选择/失败/超时结果通过 `WorldSimulation::SettleWorldEvent` 一次结算，使用事件 id+玩家 StableId 幂等。M33-F2-T4 已接单机 HUD 事件面板：显示事件名/区域/状态/倒计时/阶段/选项效果/结果和 6 条电台记录；玩家在事件点 6 格内按 `E` 打开选项、`1–8` 选择、`ESC` 收起，离开范围会关闭选择并提示返回现场；导演进入 Resolved/Failed/Expired 后单机 HUD 自动结算一次。世界地图为 Warning/Active 事件绘制可点击橙色标记并支持导航。原生 HUD 与 `mods/core_ui` 同步，Lua/Python `Hud.GetState` 暴露 `worldEvent*` 字段。M33-F2-T5 已接服务器权威：服务器广播 `ZLW2` 事件快照（独立 magic，旧客户端忽略），客户端只发送 `ChooseWorldEvent`（索引）命令；服务器复核 Active/阶段/距离/索引后按参与者结算一次，重复/远端/终态选择拒绝，超时由服务器自动结算。manifest 升 **v6**，记录事件快照与结果账本键，服务器重启、客户端重连和单机读档恢复同一事件；离线的参与者不会在事件结束时补发奖励。M33-F2-T6 的内容包包含 `world_caravan_ambush`（商队伏击→次日 `world_caravan_aftermath`）、`world_survivor_distress`（幸存者求救）、`world_migrating_horde`（迁徙尸潮）、`world_infected_house`（感染房屋）、`world_supply_contest`（空投争夺）；主事件均为 2 阶段、2–3 个结果，持续 90–240 游戏分钟（默认日长下约 3.75–10 现实分钟），奖励不超过同风险 POI。内置 ID 使用 `world_` 前缀，与 M24 `event_*` 随机任务不重叠；新增 mod 事件仍按同一契约追加，避免复用 M24 事件 id 或依赖其生成内容。M33-F2-T7 已验证实际内容双客户端 15 游戏分钟流程、服务器重启后 Resolved 快照与声望恢复、重复结算幂等；网络模式客户端现在与单机共用 `RefreshWorldEventHud` 入口（内部委托 `RefreshNetworkWorldEventHud`），不再出现收到 `ZLW2` 快照但事件面板不刷新的情况。

```json
{
  "format": "world-events-v1",
  "defs": [
    {
      "id": "caravan_ambush",
      "name": "商队伏击",
      "kind": "caravan",
      "minDay": 3,
      "maxDay": 60,
      "weight": 1.0,
      "cooldownDays": 5,
      "warningMinutes": 120,
      "durationMinutes": 30,
      "region": "any",
      "stages": [
        { "kind": "reach", "radius": 8, "description": "赶到商队遇袭点" },
        { "kind": "kill", "target": "event_caravan_raider", "count": 3 }
      ],
      "choices": [
        { "id": "rescue", "label": "护送商队", "reputation": 15, "rewardXp": 80, "pressure": -10 },
        { "id": "rob", "label": "抢走货物", "reputation": -25, "rewardItem": "ammo_9mm", "rewardItemCount": 2, "pressure": 8 }
      ],
      "nextEvent": "caravan_aftermath",
      "failureReputation": -5,
      "failurePressure": 8
    }
  ]
}
```

- 必填 `id/name/stages/choices`；`id` 为 1~64 字节小写 `[a-z0-9_.]` 且全局唯一，`name` 1~128 字节。可选字段默认 `kind="custom"`、`minDay=1`、`maxDay=100000`、`weight=1.0`、`cooldownDays=1`、`warningMinutes=60`、`durationMinutes=30`、`region="any"`、`failureReputation=-5`、`failurePressure=5`。
- `stages` 1~8 项：`kind` 为 `reach|interact|kill|loot|defend|choice`；`interact/kill/loot` 必须有 `target`；`count` 1~999，`radius` 0.5~128，`description` 0~256 字节。`choices` 1~8 项：`id/label` 必填，`reputation` -100~100，`rewardXp` 0~1000000，`rewardItem` 必须是已注册物品，`rewardItemCount` 1~9999，`pressure` -100~100（正值增加区域压力，负值缓解）。失败/超时使用事件的 `failureReputation/failurePressure`（声望 -100~100，压力 0~100）。
- `nextEvent` 只能引用已加载事件（基础、更早 mod 或同文件）；缺失引用、自环、循环链或超过 8 段深度的事件会被跳过。单条非法定义跳过并告警；mod 文件无有效定义时整份拒绝，基础文件全部非法时加载失败。空 `defs` 或 `worldEvents.enabled=false` 表示无事件。
- `mods/<id>/content/world_events.json` 必须有 `format` 和非空 `defs`，按加载顺序追加；重复 id、坏字段/坏奖励引用/坏链跳过，不能覆盖基础事件。M33-F2-T3 起世界事件字段进入内容指纹 v17，M33-F3-T5 起整体 v19，M33-F4-T1 起 v20，M33-F5-T1 起 v21；单机 content 热重载会随 `ContentBundle` 事务重读，联机仍需重启服务器和全部客户端后重连。M33-F2-T2 起 `WorldSimulation::WorldEventSystem` 使用加载后的配置排期；T3 已接生成/结算，T4 已接单机事件 HUD/电台/地图与自动超时结算，T5 已接服务器快照/选择命令/manifest v6/重连恢复；T6 已填 5 类基础内容并使链目标不参与随机主排期；T7 已补实际内容双客户端 15 游戏分钟流程、服务器重启恢复与重复结算幂等，并修复网络模式 HUD 刷新；终态内容清理、鼠标选项、独立聊天频道和离线参与者补发仍未接入，且热重载后导演配置副本不会自动重建（联机需重启服务器和全部客户端）。

### 3.6.3.6 同伴数据、招募、AI、装备背包、倒地、信任与 UI（M33-F3-T1/T2/T3/T4/T5/T6/T7）

`assets/companions.json` 使用 `format: "companions-v1"`，根节点 `defs` 数组定义同伴；`config/game.json` 的 `companions.enabled/file` 控制基础文件。M33-F3-T1 建立数据契约与组件，T2 已接聚落招募/归队/解散待命，T3 已接跟随/停留/撤退、威胁选择与低血撤退，T4 已接初始装备/背包、武器伤害/护甲、倒地救援/死亡结算与世界存档，T5 已接信任/个人委托/动态对话，T6 已接 `ZLR1 CMPN` 只读复制、断线待命、重连 owner 恢复与服务器重启，T7 已接同伴面板/HUD、指令、赠礼、个人委托与救援交互。

```json
{
  "format": "companions-v1",
  "defs": [
    {
      "id": "companion_medic_lin",
      "name": "林医生",
      "background": "村庄营地的战地医护。",
      "baseHealth": 95,
      "startingLoyalty": 55,
      "equipment": { "primary": "kitchen_knife", "body": "jacket" },
      "inventoryCapacityKg": 10,
      "inventory": [{ "item": "bandage", "count": 2 }],
      "traits": ["field_medic"],
      "recruit": {
        "minReputationTier": "friendly",
        "requiredMission": "settlement_help",
        "requiredItem": "bandage",
        "requiredItemCount": 2
      },
      "dialogueProfile": "companion_lin"
    }
  ]
}
```

- 完整内置内容、DPS 公式与 T8 验收见 [docs/19-M33同伴数据与平衡.md](19-M33同伴数据与平衡.md)。
- 必填：`id/name`；`id` 为 1–64 位小写字母/数字/`_`/`.`，`name` 1–128 字节。可选字段缺省：`background=""`、`baseHealth=100`（1–1000）、`startingLoyalty=50`（0–100）、`inventoryCapacityKg=12`（1–200）、`equipment` 空、`inventory=[]`、`traits=[]`、`personalQuest=[]`、`recruit.minReputationTier="friendly"`、`dialogueProfile=id`。
- `equipment` 只接受 `primary`/`body` 两个槽位，物品必须存在于物品注册表；`inventory[]` 为 `{item,count}`，物品必须存在、count 1–999、同条目物品不重复、最多 16 条。`traits[]` 最多 8 个 token 且不重复。
- `recruit.minReputationTier` 取 `hostile|wary|neutral|friendly|trusted`；`requiredMission` 必须是 target 聚落 `missionPool` 中的任务，并在招募时已 `Completed`；空字符串表示无前置任务。`requiredItem` 和 `requiredItemCount` 必须成对，物品必须存在。
- `Companion` 组件保存 `defId/ownerStableId/homeSettlementId`；`CompanionState` 保存 `loyalty/command/standby/downed/dead/downedRemaining/personalStatus/personalStage/personalProgress/personalRewardMask/extraTraits/inventory/equipment`。生命复用通用 `ecs::Health`，避免和伤害/死亡系统形成两个真值源；`ValidateCompanionComponents` 校验 id/owner/home、生命、忠诚、指令、倒地/死亡互斥、个人委托边界/额外特长、背包容量/条目和装备槽。
- 招募归属：聚落 `services` 必须包含 `recruit`；`FindSettlementCompanion` 选择第一条 `requiredMission` 属于该聚落 `missionPool` 的同伴定义。`EvaluateCompanionRecruitment` 统一校验声望档位、已完成任务、消耗物资和已有同伴；物资扣除在生成前完成，失败不改背包。
- 服务器权威：`RecruitCompanion=51`/`DismissCompanion=52` 的 `payload.entityId` 为求助者 StableId。服务器重新校验实体/类型/同层/`building.actionRange+1`/聚落失守或夜袭/归属/唯一性；同一 `ownerStableId` 最多一名活动或待命同伴，解散后同一 StableId 回聚落待命，客户端不本地生成、扣物或改状态。T7 新增 `SetCompanionCommand=53`（flags 低 2 位 0/1/2 = follow/stay/retreat）、`GiftToCompanion=54`（index=玩家背包索引）、`AcceptCompanionQuest=55`、`AbandonCompanionQuest=56`、`RescueCompanion=57`；服务器复核 owner、同层、距离、倒地/死亡/待命、背包物品和委托状态，单机走同一规则。
- mod 的 `content/companions.json` 必须有 `format` 和非空 `defs`，按加载顺序追加；重复 id、坏字段、未知物品引用跳过，不能覆盖基础定义。基础 `companions.enabled=false` 或缺少配置时同伴数据保持禁用；空 `defs` 只告警，仍允许后续 mod 追加。同伴定义（含 `personalQuest` 全部阶段/奖励字段）进入内容指纹 v19（M33-F4-T1 起整体 v20，M33-F5-T1 起 v21），单机 content 热重载会随 `ContentBundle` 整批重读，联机仍需重启服务器和全部客户端后重连。
- AI（T3）：`CompanionSystem` 消费 `CompanionState.command` 的 `follow/stay/retreat`。`follow` 以 owner `Facing` 背后 3 格为落点，1.8–4.5 格内停步；`stay` 以切换时位置为锚点；`retreat` 回 owner（owner 不存在时回家位置），生命 ≤30% 自动撤退、恢复到 50% 解除。威胁仅在 owner 被锁定/攻击中或进入 owner 3.5 格时获取，取离 owner 最近者，最多在 owner 12 格 leash 内追击；owner 距离超过 40 格或目标异层时冻结。攻击只选敌对 `AiState`/聚落夜袭/基地围攻目标，排除玩家、友方、聚落 NPC、动物、其他同伴和 `NoCombatTarget`，近战伤害走通用伤害管线；跟随/停留/脱离陷阱落点避开武装 `SpikeTrap`。T8 起 AI/战斗参数从 `config/game.json` 的 `companions.ai.*` 读取（跟随即停 1.8–4.5、leash 12、冻结 40、低血 30%、恢复 50%、倒地 60 秒、救援 35%/2.5 格等）；主手武器伤害/冷却分别乘 `attackDamageScale=0.38`/`attackCooldownScale=1.20`，空手回退 `attackDamage=24`/`attackCooldown=0.85`，使内置同伴同武器 DPS 约为玩家的 31.67%。`CompanionAiState` 不存档/不复制；门/墙/建筑边界沿用静态 A* 通行位图，异层楼梯只冻结、不跨层寻路。
- 装备/背包与倒地（T4）：生成时按 `equipment.primary/body` 放入主手/躯干（槽位不匹配只告警并跳过），按 `inventory` 放入初始物品（容量不足可部分放入）；主手武器 `combat.damage/range/cooldown/wear` 覆盖同伴近战参数，躯干 `armor` 派生 `DamageResist`，受击磨损后重算。`CompanionInventory` 提供入包/出包/换装/卸装/使用药品/装填弹药，迁移先试算容量再提交，失败回滚；药品和弹药规则由单机/服务器共用，不自动使用、不自动制作。生命 ≤0 进入 60 秒倒地窗口，`TryRescue` 在 2.5 格内恢复 35% 最大生命；窗口结束死亡并把装备/背包移入 `companion_corpse` 尸体容器（`LootContainer.searched=true`），死亡同伴保留实体并阻止再次招募/解散。`Companion`/`CompanionState` 已进入世界存档，`CompanionAiState` 仍不存档；T6 起联机客户端只读显示同伴。敌对 AI 当前仍主要锁定玩家，同伴通过火焰/环境/范围伤害与脚本承伤；专用同伴仇恨留后续。
- 信任/对话/个人委托（T5）：`loyalty` 作为 0~100 信任值，四档阈值 25/50/75；同伴击杀敌人 +2，`GiftToCompanion` 从玩家背包原子转移物品并 +2，阶段完成按 `rewardTrust` 增加，全部钳制。`personalQuest` 最多 8 段，阶段按 `minTrust` 与上一段完成解锁；`kill`（target=`any` 或筛选标记）或 `gift`（物品 id）目标达到 count 后自动完成，按 `personalRewardMask` 只发一次 `rewardTrust/rewardTrait/rewardItem/rewardEquipSlot`，`extraTraits` 最多 8 个；死亡或显式 `FailCompanionQuest` 会中止当前阶段，可重新接取。`MakeCompanionDialogue` 按信任档、正常/低血/倒地/死亡和委托 Available/Active/Failed/Completed 生成不同文本与选项；`AcceptCompanionQuest`/`AbandonCompanionQuest` 已接同伴对话与服务器命令。`CompanionState` 存档组件升 v3，旧 v2 默认无委托；48 个内置组件不变。
- 联机复制与重连（T6）：`ZLR1` 新增 `CMPN` 只读组件（defId、命令态、待命/倒地/死亡、信任、个人委托状态/阶段/进度/奖励 mask、倒地剩余），实体 metadata 带 owner StableId；位置/生命/朝向/动画沿用通用复制，装备从 `CompanionState.equipment` 复用 `EQUIP`，客户端副本只加 `FriendlyNpc`/`NoCombatTarget`，不本地生成 `Companion`/`CompanionState` 真值。旧负载无 `CMPN` 时安全显示为普通角色。玩家断线、死亡或同身份接管时服务器让归属同伴回所属聚落待命（位置不可用则原地清路径/AI），保留 StableId/owner，不重复生成；世界存档恢复 owner、生命、装备、信任、个人委托、额外特长和倒地状态。完整背包不复制，重连后需重新召回。
- UI/HUD/交互（T7）：`game/CompanionUi` 生成只读面板文本，原生 `assets/ui/hud.rml` 与 `mods/core_ui`（Lua/Python 共用 `ui/hud.rml`）同步；`P` 开关面板（`CompanionPanel` 普通绑定），显示生命/状态/信任/装备/背包/个人委托，`1/2/3` 发送跟随/停留/撤退、`4` 赠礼当前快捷栏物品、`E` 靠近交谈/救援、`P/ESC` 关闭。HUD `companion-status` 按死亡/倒地/低血/远离/路径受阻/待命优先显示；聚落求助者交互提示和对话 hint 追加招募声望档位/当前分数/前置委托/物资。联机面板从只读 `CMPN` + `EQUIP` 构造，不显示完整背包；`GiftToCompanion` 一次只转移 1 件，换装/服药/装填未提供 UI；不做 RTS 框选/编队。
- T2/T4/T5/T6/T7/T8 限制：同伴实体、信任、个人委托、装备和倒地状态已写世界存档；复制只含装备摘要而非完整背包，`CompanionAiState` 不复制、读档重建；玩家断线/死亡时同伴回聚落待命，重连只恢复 owner 关系不自动召回；个人委托是同伴专属链，不写入全局 `MissionLog`；`traits`/`extraTraits` 当前是持久化内容标签，不直接修改伤害/护甲公式；数据字段变更不应依赖临时行为。

### 3.6.3.7 载具改装数据、组件、安装/拆除、效果、UI、联机与平衡（M33-F4-T1/T2/T3/T4/T5/T6）

`assets/vehicle_mods.json` 使用 `format: "vehicle_mods-v1"`，根节点 `defs` 数组定义改装；`config/game.json` 的 `vehicleMods.enabled/file` 控制基础文件，`vehicleMods.removeRefundRatio=0.5` 控制拆除返还比例，`vehicleMods.removeSecondsScale=0.5` 控制拆除耗时倍率，`vehicleMods.weightAccelerationKg=400`/`weightFuelKg=600` 控制重量对加速/油耗的折算。第一版槽位固定为 `armor/storage/plow/lights/engine`，每辆车每个槽位最多装 1 个改装。T1 建立数据、合并、校验和内容指纹；T2 已接单机安装/拆除、`ecs::VehicleMods` 存档与面板；T3 已接效果聚合与驾驶/碰撞/车况/容量/灯光/油耗；T4 已移除 `O` 调试入口，改由后备箱/HUD 正常入口打开，并补齐逐槽效果与总览；T5 已接服务器安装/拆除命令、ZLR1 `VMOD` 只读摘要、联机面板写操作与重启/重连恢复；T6 固定 5 个内置改装的收益/代价、材料来源与全套总账，补充平衡测试和 1280 交付烟测，详见 [docs/20-M33载具改装数据与平衡.md](20-M33载具改装数据与平衡.md)。

```json
{
  "format": "vehicle_mods-v1",
  "defs": [{
    "id": "vehicle_armor_plate",
    "name": "焊接装甲板",
    "description": "撞墙和感染者抓挠时更耐撞，但更重、更费油。",
    "slot": "armor",
    "installSeconds": 30,
    "weightKg": 28,
    "effects": {
      "crashConditionScale": 0.68,
      "zombieConditionScale": 0.72,
      "fuelUseScale": 1.07
    },
    "materials": [
      { "item": "repair_kit", "count": 2 },
      { "item": "wood", "count": 4 }
    ],
    "excludes": []
  }]
}
```

- 必填：`id/name/slot/materials`；`id` 为 1–64 位小写字母/数字/`_`/`.`，`name` 1–96 字节；`slot` 只接受 5 个槽位。`description` 最多 384 字节。
- `materials` 必须 1–4 条 `{item,count}`，物品必须已注册、同条目不重复、count 1–99；材料是按安装消耗，不新增货币。
- `installSeconds` 缺省 10（0.1–600），`weightKg` 缺省 0（0–250）；`excludes` 最多 4 个已存在改装 id，不得自引用或重复。
- `effects` 缺省为中性值；已知字段与范围：`crashConditionScale/zombieConditionScale/ramConditionScale` 0.25–2.0（缺省 1）、`ramDamageScale` 0.25–3.0（1）、`trunkCapacityKg` -20–80（0）、`trunkStacks` -2–6（0）、`headlightRangeScale/headlightBrightnessScale` 0.5–3.0（1）、`maxSpeedScale/accelerationScale` 0.5–2.0（1）、`fuelUseScale` 0.5–3.0（1）、`conditionWearScale` 0.25–2.0（1）。未知效果字段拒绝该条定义。
- 内置 5 条覆盖全部槽位：焊接装甲板（碰撞/抓挠车况 x0.68/0.72，重量 28）、车顶行李架（容量 +24 kg、堆数 +2）、撞尸犁（撞尸伤害 x1.45、撞尸车况损耗 x0.65）、车顶探照灯（灯光范围 x1.45/亮度 x1.25）、发动机调校（极速 x1.18/加速 x1.12、油耗 x1.15、磨损 x1.10）。
- 效果应用（T3）：`VehicleModStatsFor` 按已安装槽位求积/求和并再次 clamp；单集合计重量 0–500kg，加速重量项 `1/(1+weight/weightAccelerationKg)`、油耗重量项 `1+weight/weightFuelKg`。`VehicleStep` 使用极速/加速/油耗/撞墙车况倍率；车辆间碰撞用 `crashConditionScale`；撞击用 `ramDamageScale/ramConditionScale`；僵尸/危险动物攻击用 `zombieConditionScale`；`conditionWearScale` 叠加到以上所有车况损耗；`trunkCapacityKg` 直接加到 `vehicle.trunkCapacityKg`，`headlightRangeScale/headlightBrightnessScale` 只缩放夜间车灯光束。`trunkStacks` 仍参与校验/聚合但因现有 6 格背包/Loot UI 上限未应用，T3 只应用容量。
- 效果文案与 UI（T4）：`VehicleModEffectSummary` 把单条改装的非中性效果格式化为 `撞墙车况 -32%`、`后备箱 +24kg`、`车灯范围 +45%` 等短句；`VehicleModStatsSummary` 输出总重、当前后备箱容量、极速/加速/油耗/撞墙/撞尸/撞击/车灯总览。面板每槽显示当前/候选名称、效果、材料、安装时间、重量、拆除耗时和返还；总览与当前状态来自已安装 defId，不另存 UI 文本。`trunkStacks` 不显示，因为现有 6 格 UI 尚未应用。
- mod 的 `content/vehicle_mods.json` 必须有 `format` 和非空 `defs`，按加载顺序追加；重复 id、非法字段、越界效果、未知/重复材料、自互斥或未知互斥目标按单条跳过，合法条目保留，不覆盖基础定义。没有 `vehicleMods` 配置时改装功能保持禁用，旧车辆加载/驾驶不变。
- 改装定义全部字段进入内容指纹（M33-F4-T1 起整体 **v20**，M33-F5-T1 起 **v21**），单机 content 热重载会随 `ContentBundle` 整批重读，联机仍需服务器和全部客户端使用相同内容后重连。
- 运行时状态（T2/T3）：`ecs::VehicleMods` 按 5 个槽位保存 defId，注册为 `VMOD v1` 存档组件；`Vehicle` 组件存档升 **v4**（载荷不变，旧 v1–v3 仍可读），旧档没有 `VehicleMods` 时按无改装运行。世界快照/服务器重启会恢复改装；单机拆除最后一个改装时移除空组件，服务器保留空组件以便客户端增量复制。效果不单独存档，始终由已安装 defId 和当前内容重新派生。服务器读档/单机换车/安装/拆除时按基础后备箱容量重算容量，拆除行李架不会删除已有超重物品，只阻止继续装入。
- 安装规则（T2）：同槽已被占用、目标定义不存在、与任一已安装改装双向 `excludes`、材料不足都拒绝；安装按 `installSeconds` 计时，完成时才事务扣除 `materials`，离开车辆/死亡/关闭面板中止且不扣材料。
- 拆除规则（T2）：按 `installSeconds × removeSecondsScale` 计时（0.1–600 秒），完成时清空槽位并按 `floor(count × removeRefundRatio)` 返还每种材料；背包放不下的返还物掉在玩家脚下。改装定义从内容中移除后仍可拆除该未知 id，但不返还材料。
- 单机交互（T2/T4）：靠近车辆（未驾驶/乘坐）按 `I` 打开背包/后备箱，后备箱标题下显示“车辆改装 · 查看槽位与效果”按钮，点击打开面板；原生背包与 `mods/core_ui` Lua/Python 背包共用该入口，不再绑定 `O` 调试键。HUD 在车辆附近提示 `I 后备箱/改装`。面板显示 5 槽当前/候选、效果、材料、安装时间、重量、拆除返还、进度和失败原因；鼠标安装/拆除，`ESC`/X 关闭。入口按背包实体 StableId 精确绑定车辆，并校验玩家未驾驶/乘坐、车辆存活/同层/在交互距离内。
- 联机权威（T5）：新增 `InstallVehicleMod=58`（`payload.entityId=载具 StableId`、`index=槽位`、`targetIndex=该槽位候选索引`）与 `RemoveVehicleMod=59`（`index=槽位`）。服务器复核玩家非驾驶/乘坐、载具存活/空载/停稳、同层、距离、候选属于目标槽位、同槽占用/双向 `excludes`、材料计数；材料事务扣除后写权威组件，拆除按返还比例把材料加入玩家背包，溢出掉在玩家脚下。客户端不本地改 `VehicleMods`，只显示服务器快照与 `PlayerState` 状态。网络面板不再只读，候选/材料/安装/拆除按钮恢复。
- 联机复制与恢复（T5）：ZLR1 新增 `VMOD` 只读组件，按 5 槽编码 defId 摘要，注册字节上限 384；客户端镜像到本地车辆 ECS。没有 `VMOD` 的旧 ZLR1 负载按无改装回退，不改 `VHCL` 旧字段。服务器世界存档沿用 `ecs::VehicleMods`/`VMOD v1`，重启或客户端重连后从全量快照恢复；服务器拆除空槽时保留空组件，确保增量快照能清空客户端旧槽位。
- 平衡与交付（T6）：5 个内置改装全部有收益和明确代价；全套总重 86kg，极速 +18%、加速 -8%、油耗 +55%、撞墙 -25%、撞尸 -21%、撞击 +45%、车灯 +45%、后备箱 64kg。材料总消耗为维修包×8、木材×14、无线电零件×2、螺丝刀×1，全部来自现有搜刮表，不新增货币。`M33-F4-T6*` 固定单件取舍、全套总账、材料总账与 UI 摘要，完整公式与验收见 [docs/20-M33载具改装数据与平衡.md](20-M33载具改装数据与平衡.md)。
- 当前限制：联机安装/拆除即时结算，单机保留施工进度；面板不显示未应用的货架格位。重量只影响加速/油耗，不改悬挂/制动；装甲不减乘员碰撞伤害；探照灯不改世界光照或 AI 感知；`trunkStacks` 尚未扩到 8 格 UI。

### 3.6.3.8 潜行配置、输入与 HUD（M34-F1）

- `config/game.json` 的 `stealth` 段控制单机/服务器共用的潜行规则：`enabled`、`crouchSpeedScale`（0.1–1.0，默认 0.55）、`stepNoiseScale`（0–1，默认 0.45）、`visibilityScale`（0.05–1，默认 0.65）、`backstabMultiplier`（1–3，默认 1.6）、`backstabHalfAngleDegrees`（0–180，默认 60）和 `exposeOnAttack`（默认 true）。非法/非有限值逐项回退默认并记录 warning，不影响启动。
- 输入绑定 `input.bindings.Stealth` 默认 `N`，可在设置菜单改键；单机直接切换，联机发送服务器权威 `SetStealth=61`。攻击、投掷、受伤、上车、倒地/死亡会退出潜行；驾驶中服务器拒绝进入潜行。
- 基线派生值：玩家步行 4.0 格/s → 蹲行 2.2 格/s；脚步噪声 2.5 格 → 1.125 格；AI 基础视距 12 格 ×0.65 = 7.8 格；`closeRadius=2.5` 内仍全向感知；背刺只对未警觉 AI 的单次近战 1.6 倍，不改变硬直/击退/耐久，不秒杀。
- `Hud.GetState()` 提供 `stealthValid/stealthCrouched/stealthMoveSpeed/stealthFootstepRadius/stealthStatus/stealthTone`，以及单机本地感知字段 `stealthAwarenessValid/stealthNearbyAlertCount/stealthBackstabAvailable/stealthPrompt`。联机客户端只同步姿态/速度/脚步，`stealthAwarenessValid=false`，不复制 AI `alertness` 或背刺结果。
- 潜行状态只存在于运行时 `ecs::StealthState`，不写世界存档；服务器只向 owner 同步摘要。`N` 键目前只影响僵尸 `PerceptionSystem` 的视觉视距和玩家脚步噪声；动物仍使用自身 `alertRadius`，不读取 `stealth.visibilityScale`。PvP 背刺、完全隐身、潜行技能树均不在 M34-F1 范围。

### 3.6.3.9 投掷配置、输入、飞行与回收（M34-F2-T2/T3）

- `config/game.json` 的 `throwing` 段：`cooldownSeconds`（0.1–10，默认 0.7）、`maxRange`（1–64，默认 12）、`minRange`（0–16，默认 1）；非法/非有限值回退默认，`minRange >= maxRange` 时两者回退默认并记录 warning。有效射程取 `min(item.throwable.range, throwing.maxRange)`。
- `V` 仍是 `input.bindings.DemoProjectile` 的默认键，不新增动作。主手/当前快捷栏 1–5 有 `throwable` 时进入瞄准；两者都有时主手优先，否则回退枪械。瞄准中光标移到 HUD、进入建造/背包/菜单/其他模态、倒地/死亡、载具、目标不在已加载区域或距离小于 `minRange` 会拒绝/取消。
- 瞄准会退出潜行（`StealthExitReason::Throw`），每帧按鼠标世界格更新落点；先按射程夹取，再沿玩家→落点采样已加载格/`blocksMovement` 对象，遇到墙或未加载区域时把落点和预览夹到墙前。预览绘制最大射程圈、落点 `impactRadius` 圈和按 `arcHeight` 抬升的抛物线，HUD action hint 显示“松开 V 投掷”和“墙前落点”。
- 松开 `V` 时再次校验存活/倒地/驾驶/UI/冷却和来源物品，成功则从主手或当前快捷栏扣除 1 件，生成本地 `ThrownProjectile`（飞行时间按距离 0.18–1.2s），并开始 `throwing.cooldownSeconds` 冷却。飞行表现只为本地影子+抬升方块，不写存档、不复制。
- 落地时在 `impactRadius` 内选择最近、同层、存活、非玩家/友方/`NoCombatTarget` 目标，经 `CombatSystem::ApplyDamage` 结算 `throwable.damage`（当前类型 `Blunt`，无额外击退/硬直），并在落点发送 `noiseRadius` 的 `SoundEvent`。`returnable=true` 时按 `breakChance` 掷骰：未损坏在落点生成同 condition 的 `ItemInstance` 并派发 `OnItemDrop`；损坏或 `returnable=false` 不生成地面物品。M34-F2-T4 起 `fire` 为运行时字段：落地按格心收集 `radius` 内格子（中心优先），逐格调用 `FireSystem.IgniteAt`，传入天气潮湿度和玩家 owner；只对新点燃格覆盖 `burnDuration/burnRemaining` 为 `minutes` 按 `game.dayLengthMinutes` 换算的现实秒。无易燃格/过湿只告警，不生成回收物；火焰伤害/蔓延/存档/联机沿用 M23，不新增协议。
- F12 快速存档和退出前会立即结算所有在飞投掷物到落点并清空运行时，不保存飞行实体；进入世界/读档清空冷却和在飞列表。纯逻辑在 `game/Throwing.{h,cpp}`；`Hud.GetState()` 自 F2-T6 暴露 `throwValid/throwName/throwRange/throwReturnable/throwFire/throwCooldownRemaining/throwCooldownDuration/throwLastResult/throwTone/throwStatus`，单机显示本地结算，联机只显示“请求已发送”并以复制状态为准。F2-T7 完成：`ThrowItem=62` 服务器权威、落地音效/复制、客户端快捷栏本地选中同步、双客户端 1280 回归和 `throwable` 契约均已收口；不做远端逐帧投掷物、PvP 投掷或服务器逐投掷 HUD 回包。

### 3.6.3.10 M34 潜行、投掷与陷阱交付契约/限制（M34-F1~F4）

**操作**：`N` 切换潜行（`input.bindings.Stealth`，可改键）；`V` 在主手/快捷栏有 `throwable` 时进入投掷瞄准，否则沿用枪械；`B` + 左键在建造模式放置 3 种陷阱；靠近陷阱按 `E` 布防/解除；建造模式内按 `X` 拆除并返还木材；`ESC` 取消瞄准/建造。投掷瞄准中光标在 HUD 上、背包/菜单/交易/对话等模态打开、倒地/死亡、驾驶、目标未加载或过近时拒绝；投掷与攻击会退出潜行。

**数据契约**：`config/game.json` 的 `stealth`（速度/脚步/可见度/背刺倍率与半角）、`throwing`（冷却/最小/最大射程）、`trapDefs.enabled/file`；物品可选 `throwable{range,damage,impactRadius,arcHeight,noiseRadius,returnable,breakChance,fire?}`；`assets/traps.json` 使用 `traps-v1`，mod 通过 `content/traps.json` 追加且不能覆盖基础 id。字段范围与校验见 3.6.3.8、3.6.3.9 与上方 M34-F2/F3 数据段。`throwable` 与陷阱定义全部进入内容指纹。

**版本/旧档**：manifest **v6**；`PlayerState` **v23**（v22 默认站立、零噪声）；ZLR1 **33** 组件（`STLH` 姿态、`TRAP2` 只读陷阱摘要）；内置存档 **53**（`PlacedTrap` 为 `TRP2 v2`，兼容 v1）；网络命令 **62**（`SetStealth=61`、`ThrowItem=62`）；内容指纹 **v23**。旧档缺少 `STLH`/`TRAP2`/`PlacedTrap` 时按站立/无远端姿态/无新陷阱处理；旧 `SpikeTrap`/电网不迁移、不复用 `TRP2`。

**权威/性能**：单机本地结算；联机潜行姿态、投掷扣件/命中/火焰/回收、陷阱布防/触发/拆除由服务器权威结算，客户端只发命令并显示复制结果。陷阱目标收集通过 `SpatialIndex` 只查询已布防陷阱附近 chunk，上限 64 个中心/2048 次访问/256 个候选；无空间索引时回退全量。在飞投掷物单机/服务器上限 64。F12/退出前结算在飞投掷物，不保存飞行实体。

**已知限制/未做**：不做可携带陷阱、PvP 陷阱/队友伤害、拆陷阱小游戏、正式动画/美术/混音、远端逐帧投掷物、投掷弹跳/风偏/刚体；只有显式 `throwable` 物品可投；动物仍使用自身警觉半径，不叠加潜行视距倍率；火焰可伤害进入火区的玩家/同伴；旧尖刺陷阱仍是每陷阱全 AI 扫描；局部扫描按 chunk 粗粒度，候选超限时尾部目标本 tick 不触发；动态 mod 热重载仍需服务器与所有客户端重启后重连。F4-T2 的 15–30 分钟自然流程和双客户端服务器重启尚未人工验收。

**复验**：`M34-F4-T3*` **1/1（11）**；`M34*` Release/Debug **64/64（1633）**；Release 全量排除既有 `M30-T6`/`M31-T9` **1151/1151（145479，4 skipped）**；Debug 全量排除 7 组既有 flake **1127/1127（144696，28 skipped）**；Python 数据 **127/127**。完整 1280 截图/双客户端自然流程仍按 F4-T2 人工清单执行。

### 3.6.4 Mod 搜刮表、觅食/钓鱼节点与 POI（X-F2b-2b / M23-F2a-T1/T3/T6）

**loot.json**：在基础 `assets/loot.json` 后追加搜刮表，格式仍为 `loot-v1`：

```json
{
  "format": "loot-v1",
  "tables": [
    {
      "id": "my_cache",
      "name": "营地补给箱",
      "capacityKg": 12,
      "rolls": 2,
      "guaranteed": [ { "defId": "bandage", "count": 1 } ],
      "entries": [
        { "defId": "my_food", "weight": 2, "min": 1, "max": 1 },
        { "defId": "rag", "weight": 2, "min": 1, "max": 3 }
      ]
    }
  ]
}
```

- 表 id 全局唯一；与基础/更早 mod 冲突时整份文件拒绝，已有表不变。
- `defId` 可引用最终合并后的物品（启动时或 X3-T2 热重载候选构建时所有 mod 已加载）；`rolls/weight/min/max/capacityKg` 规则与基础战利品表一致。
- mod 文件中的根 `seed` 不参与合并，随机种子仍由基础 `assets/loot.json` 决定；单机 content 热重载会重建表，联机/主机需重启。
- 只有搜刮表的 mod 也可以单独应用。

**forage.json**：在基础 `assets/forage.json` 后追加资源节点，格式为 `forage-v1`：

```json
{
  "format": "forage-v1",
  "forageZones": [
    {
      "id": "my_forest_patch",
      "name": "模组森林",
      "tableId": "my_forage_table",
      "floors": ["tile.floor.grass"],
      "requiredNearbySprites": ["object.tree"],
      "nearbyRadius": 4,
      "density": 0.08,
      "spacing": 4,
      "maxNodes": 48,
      "respawnMinutes": 240
    }
  ],
  "fishingSpots": [
    {
      "id": "my_pond",
      "name": "模组池塘",
      "tableId": "my_fish_table",
      "trashTableId": "my_trash_table",
      "waterFloors": ["tile.floor.water"],
      "bankRadius": 3,
      "density": 0.3,
      "spacing": 4,
      "maxSpots": 12,
      "respawnMinutes": 360,
      "waitSeconds": 5.0,
      "biteWindowSeconds": 2.0,
      "catchChance": 0.62,
      "survivalXp": 10,
      "rodDefId": "fishing_rod",
      "rodWear": 0.03,
      "baitDefId": "fish_bait",
      "baitCost": 1
    }
  ]
}
```

- `forageZones[].floors` 与 `fishingSpots[].waterFloors` 使用逻辑地块名；`requiredNearbySprites/excludedNearbySprites` 使用逻辑对象名；`poiIds/poiRadius` 可把节点限制在指定 POI 的范围内。
- `density` 为 0~1 的命中率，`spacing` 为节点间最小格距，`maxNodes/maxSpots` 为上限；`respawnMinutes` 控制觅食节点耗尽后的刷新时间（钓鱼点刷新预留）。节点位置由基础 seed 与坐标确定性生成，不写地图母文件。
- `searchSeconds`（0.5~30，默认 2.5）是基础搜索耗时，会再乘生存技能、天气与夜晚修正；`survivalXp`（0~200，默认 6）是完成搜索的生存经验。
- `toolDefId` 可选，存在时采集需要背包/装备中至少一件可用工具；`toolWear`（0~1，提供 `toolDefId` 时默认 0.01）按成功搜索磨损。基础草地需要 `forage_stick`。
- 节点耗尽按玩家存档组件 `FRGS` 保存，使用游戏分钟刷新；同一节点完成搜索后才写入，E 开始/中断不会消耗节点。
- `fishingSpots[].tableId` 是成功鱼获表，`trashTableId` 是失败/杂物表；未写 `trashTableId` 时回退到 `tableId`。`waitSeconds`（1~60）是基础等待，`biteWindowSeconds`（0.5~10）是收杆窗口，`catchChance`（0.05~0.95）是基础成功率，`survivalXp`（0~200）是成功经验。
- 钓鱼需要 `rodDefId` 的可用耐久和至少 `baitCost` 个 `baitDefId`；抛竿即消耗鱼饵，收杆/错过咬钩按 `rodWear` 磨损鱼竿。置空 `rodDefId` 可让点位不需要鱼竿，置空 `baitDefId` 时 `baitCost` 必须为 0。
- `tableId/trashTableId` 必须引用最终合并后的 `loot.json` 表；内置 `tile.floor.water` 已随 T3 落在村庄南侧池塘，水面地块会阻挡移动，岸格才可生成钓鱼点。
- 服务器加载同一份 forage 数据并独立规划附近节点/钓点；客户端通过 `PlayerState` v13 的 `forageNodes/fishingSpots/survival` 只读展示。觅食节点耗尽、产出、工具磨损与鱼饵/鱼竿消耗均由服务器决定，重复采集和超距离请求会被拒绝。钓鱼点仍未耗尽/刷新；T7 起服务器全局 `ForageState` 由保留 StableId 的世界实体随世界存档落盘，读档恢复 `gameMinutes` 并清理过期节点，旧档缺失该实体时创建空状态。
- 基础产出率（M23-F2a-T8，期望值，不含行走/烹饪）：森林每次 2.6 秒搜索约 2.85 件、1.00 件食物，生食 0.066 饥饿、熟制 0.172；草地每次 2.0 秒约 2.92 件、1.00 件食物，生食 0.060、熟制 0.160；静水钓鱼每次尝试（5 秒等待 + 2 秒咬钩窗口）按 0.62 成功率约 0.93 条小鱼，生食 0.112、熟制 0.298，消耗 1 鱼饵。数值按 `needs.hungerPerDay=1.4` 与 60 现实分钟/游戏日计算；调整 `loot.json` 权重/rolls 时同步 `docs/05` 与 `tools/tests/test_forage_data.py`。

**pois.json**：在基础 `config/game.json` 的 `pois.defs` 后追加 POI：

```json
{
  "format": "pois-v1",
  "defs": [
    {
      "id": "my_depot",
      "name": "我的补给站",
      "threat": 1,
      "markerX": 54.5,
      "markerY": 28.5,
      "containers": [
        { "x": 54.5, "y": 28.5, "tableId": "my_cache", "sprite": "object.crate" }
      ],
      "enemies": { "count": 1, "kind": "normal", "radius": 4 },
      "task": {
        "description": "搜索补给箱",
        "targetTable": "my_cache",
        "rewardXp": 20,
        "rewardItem": "my_food",
        "rewardCount": 1
      }
    }
  ]
}
```

- 字段与内置 POI 定义相同；`targetTable` 必须出现在自己的 `containers[]` 中，`rewardItem` 必须是已注册物品。
- 只追加新 id；与基础/更早 mod 重复时跳过并告警。基础 `pois.enabled` 为 `false` 时不加载 mod POI，`guideRange` 仍由基础配置决定。
- 现有实现不生成地图房间/墙体；`markerX/Y`、容器坐标和守军生成点需要作者保证可达。加载后自动复用现有 POI HUD 指引、容器生成、守军、任务奖励与 `PoiTasks`。
- `sprite` 可引用基础或 mod 图集逻辑名；`tableId` 可引用基础或 mod 搜刮表；服务器也会按已加载 chunk 生成容器/守军并同步 POI 进度；仅运行时 `Content` API 动态追加不支持联机。
- 示例 `mods/example_content` 的 `mod_depot` 使用 `mod_camp_cache`、`mod.supply_crate` 和 `camp_stew`，演示物品/战利品/POI/图集四类内容联动。
- M33-F5-T1 起 POI 可选追加 `stages[]/entries[]/keys[]/branches[]/boss/narrative`，用于 2–3 阶段探索；`stages` 按数组顺序推进，`next` 只能指向后续阶段，`choice` 需 2–4 个后继，分支最多 2 条。无 `stages` 时保持单段 `task` 行为；有有效阶段时 `task` 可省略。字段、默认值、钥匙来源顺序与逐条跳过规则见 [docs/21-M33多阶段POI数据格式.md](21-M33多阶段POI数据格式.md)。这些字段全部进入内容指纹 v21。
- M33-F5-T2 起阶段进度为**世界级、服务器/单机权威**：`PoiWorldProgress`（`PWPR`）保存当前/完成阶段、分支、钥匙标记、供电、失败/重试；进入 marker 16 格内启动。`reach/kill/loot` 已由位置、守军存活数和玩家持有物品自动判定；`interact/power/choice` 由 T3 接门禁、钥匙物品消耗、发电机和路线选择。阶段奖励使用玩家级 `PoiTasks.rewardClaims`（`poiId.stageId`）幂等，服务器只发给阶段完成时 32 格内玩家；`PoiTasks` 存档 v2，旧 v1 仍可读。`PlayerState v22` 追加只读 `poiStageProgress` 镜像（当前阶段/完成数/分支/失败/供电），客户端不自行推进。服务器重启和单机读档恢复世界进度，已完成 staged POI 不再补生成守军。注意 T2 仍要求 `containers` 至少有一个合法条目；阶段专属刷怪、Boss 失败、完整阶段 HUD/地图标记留 T4/T5。
- M33-F5-T3 起 `keys[].source` 阶段会发放实际钥匙物品，`consumed` 控制门禁/入口使用时是否消耗；`entries[]` 可按钥匙从非首阶段进入，`choice` 可用快捷栏 1/2 选择路线。`power` 阶段消耗 `homestead.power.fuelItem`（默认 `fuel_can`），启动阶段坐标附近已有的 `ecs::Generator` 并写入 `powered`；电力系统关闭时允许手动合闸。服务器新增权威命令 `PoiInteract=60`（`payload.index`=POI 定义索引，`payload.targetIndex`=路线索引），复核距离/阶段/钥匙/燃料；单机/服务器 HUD 会提示缺钥匙、缺燃料、E 交互与路线选择，完整 HUD/地图留 T5。门禁仍是阶段坐标上的逻辑交互，不绑定可开关建造成门实体；入口为靠近自动进入，暂无独立入口 UI。
- M33-F5-T4 起 `boss` 只在绑定的 `kill` 阶段生效：`BuildPoiStageEnemyPlan` 按阶段 `count` 生成 `boss.count` 个 Boss（截断到 `count`）和剩余精英，精英变体由 `stage.target` 选择 `fast|strong|armored|normal`，未知值回退 `armored`。Boss/精英带 `PoiStageEnemy`（Boss/Elite）并计入阶段击杀；`summon` 召唤物角色为 `Summon`，不参与阶段 `count`，阶段完成后统一清除。`pois.boss.*` 提供 `healthScale/resetRange/resetSeconds/summonIntervalSeconds/summonCount/summonMax/summonTotal/chargeIntervalSeconds/chargeSpeedScale/chargeSeconds` 调参：Boss 脱战超过 `resetSeconds` 会失败，返回 POI marker 16 格或入口 6 格内自动重试；服务器团灭也会失败。`PoiStageEnemy` 使用内置存档段 `PSEN`（内置组件 51）随实体保存，旧档无该组件时按无阶段敌人；`PlayerState v22` 与内容指纹 v21 不变。`fortify` 复用现有 `ZombieSpecial::Breaker` 破坏工事，`charge` 是速度倍率冲锋而非新动画；T4 不新增内置 Boss/精英内容（T6），完整阶段 HUD/地图/1280 截图留 T5。
- M33-F5-T5 起单机/服务器 HUD 增加 `poi-stage-panel`：显示当前阶段/目标/钥匙/供电/路线/Boss 机制与当前阶段 `narrative[]` 文本；完成或失败后显示约 10 秒 `poi-result-panel`，不阻塞战斗。`narrative[].item` 只作为“文件：id”提示，实际搜刮仍应把物品 id 配到 `loot` 阶段 `target`；地图 POI 标记显示阶段/完成度并优先落在当前阶段坐标。HUD 字段通过 native/core_ui 与 Lua/Python `Hud.GetState` 的 `poiStage*`/`poiResult*` 暴露。T5 不新增内置 staged 内容（T6），门禁/Boss 截图是构造状态。

- M33-F5-T6 起默认 M31 内容把诊所/检查站/农场仓改为 3 阶段：诊所 `triage_choice` 分支（急诊 Boss/药房搜刮），检查站巡逻记录→门禁卡→指挥官，农场到达→入库单→农场主；新增医院/检查站/农场笔记和门禁卡，旧 `village_demo` 仍为单段。T7 已验证双客户端共享 world 阶段、每人 `rewardClaims` 幂等、服务器重启后不重刷 Boss 或重复发奖，以及商队余波→招募→车辆改装→诊所 3 阶段→存档/继续读档的 15.3 游戏分钟循环。

### 3.6.5 Mod 任务与事件（X-F2b-2c）

**missions.json**：在基础 `config/game.json` 的 `missions.defs` 后追加任务和随机/每日事件，格式为 `missions-v1`：

```json
{
  "format": "missions-v1",
  "defs": [
    {
      "id": "my_depot_supply",
      "name": "我的补给站",
      "chain": "my_depot",
      "order": 1,
      "kind": "once",
      "source": "note",
      "prerequisite": "",
      "objectives": [
        { "kind": "poi", "target": "my_depot", "count": 1, "description": "搜索补给站" }
      ],
      "reward": {
        "xp": 30,
        "skill": "survival",
        "item": "my_food",
        "itemCount": 1,
        "label": "30 生存经验 + 食物×1"
      }
    }
  ]
}
```

- 任务字段与 `config/game.json` 的 `missions.defs[]` 相同：`kind` 为 `once`/`daily`/`random`，`source` 为 `radio`/`note`/`npc`/`base`；目标支持 `collect`/`interact`/`kill`/`loot`/`poi`/`reach`，`reward` 支持经验、技能、物品和图纸。
- 只追加任务，不能覆盖基础任务；基础 `missions.enabled=false` 时不加载 mod 任务，`randomSeed` 仍由基础配置决定。每日/随机事件复用现有 `UpdateMissions` 的刷新和随机判定。
- 每个 mod 的 `missions.json` 是一个事务：任意任务条目无效、id 与基础/已接受任务重复、前置不存在或前置链成环时，整份文件跳过且已有任务不变。
- 前置可引用基础任务或同批次更早加载文件的任务；同一文件内可自由引用。不要引用更晚加载的 mod。
- 任务 HUD、焦点切换、奖励发放、存档字段和放弃逻辑完全复用 M19-F3；单机运行 `UpdateMissions`，联机客户端跳过本地更新，由服务器 `TickServerMissions` 权威推进并同步日志/奖励。任务定义参与内容指纹。
- 示例 `mods/example_content` 的 `mod_depot_supply` 以 `poi` 目标完成 `mod_depot`，启动后基础 35 条任务变为 36 条并自动接取。

### 3.6.6 运行时动态内容（X-F2b-2e）

单机脚本可以调用 `Content` 表追加物品、配方、建造项、POI 和任务。接口接收与内容文件同格式的完整 JSON 文档字符串，返回实际新增条数；失败返回 `0`，错误写入日志：

| 接口 | 说明 |
|---|---|
| `Content.RegisterItems(json)` | `items-v1` 文档，追加物品；重复 id/非法字段会整份拒绝 |
| `Content.RegisterRecipes(json)` | `recipes-v1/v2/v3/v4` 文档，追加配方；未知产物/材料/工具/锅具/燃料或重复 id 整份拒绝 |
| `Content.RegisterBuildings(json)` | `buildings-v1` 文档，追加建造项；重复 id 或 sprite 冲突项跳过 |
| `Content.RegisterPois(json)` | `pois-v1` 文档，追加 POI；重复 id/无效条目跳过，format/defs 结构错误不追加 |
| `Content.RegisterMissions(json)` | `missions-v1` 文档，追加任务；重复 id、坏前置或成环时整份拒绝 |

```lua
local items = [[{
  "format": "items-v1",
  "items": [
    { "id": "runtime_torch", "name": "临时火把", "icon": "item.rag", "weight": 0.3, "maxStack": 1, "type": "generic" }
  ]
}]]
local added = Content.RegisterItems(items)
```

规则：

- 注册按文档事务校验：物品/配方/任务失败时注册表保持原样；建造项和 POI 按各自合并规则逐个追加，至少成功一项才返回非 0。
- 物品应先于引用它的配方注册；动态物品/配方/建造/POI/任务会立即刷新单机内容指纹，`WorldDemoState` 会在下一次 tick 重建建造目录并复制最新 POI/任务配置，因此运行中新增的建造项、POI 和任务也能生效。存在脚本动态内容时，磁盘 content 热重载会拒绝，避免整体替换丢失这些定义。
- 动态内容只在 `net.mode=direct` 单机模式可用；开始主机或连接服务器后接口返回 `0` 并给出“动态内容仅支持单机；请移除动态注册后重启”，避免客户端/服务器内容指纹不一致。动态定义不写入存档，禁用 mod 也不会移除已注册定义。X3-T4 已确认不做“服务器注册、客户端校验”，联机双方必须安装相同静态内容；X3-T5 验证五类入口在联机禁用时全部拒绝且注册表/计数不变，X3-T6 完成文档收口。
- 动态 POI 不创建地图房间，坐标/容器可达性和搜刮表存在性由作者保证；动态任务复用现有 `UpdateMissions`、任务 HUD 和奖励逻辑。动态物品引用的图标/建造 `sprite` 仍需能在基础或 mod 资源清单中解析，否则使用占位帧或不可见。

### 3.7 `Hud`（HUD 快照与 Mod 字段）

`Hud.GetState()` 返回当前 HUD 数据；核心 HUD 由 `mods/core_ui` 的 Lua/Python 控制器消费，
主菜单/暂停时没有 tick，因此脚本应只在 `active=true` 时显示 HUD。
普通 mod 还可以声明最多 8 个自己的状态字段，内置 HUD 会在右侧“模组”面板逐行显示。

| 字段 | 类型 | 说明 |
|---|---|---|
| `active` | bool | 当前是否有玩家/世界（主菜单为 false） |
| `day` / `time` / `speed` | string | 天数、时间、速度文本（如 `暂停`） |
| `playerValid` | bool | 生命/体力数据是否有效 |
| `health` / `healthMax` | number | 当前/最大生命 |
| `stamina` / `staminaMax` | number | 当前/最大体力 |
| `combatStatus` / `combatTone` | string | 联机服务器权威战斗姿态（格挡/弹反窗口/闪避无敌/破防恢复）与 `info`/`ok`/`warn`/`fail`（M30-T6/T7） |
| `needsStatus` / `needsTone` | string | 饥饿/口渴/疲劳状态文本与 `info`/`warn`/`fail` |
| `woundStatus` / `woundTone` | string | 伤口/流血状态文本与 `info`/`warn`/`fail`（M15-T2） |
| `painStatus` / `painTone` | string | 疼痛/止痛状态文本与 `info`/`warn`/`fail`（M15-T4） |
| `armorStatus` / `armorTone` | string | 当前躯干护甲的爪/咬/钝减伤、移速与 `info`/`warn`/`fail`（M19-F4） |
| `infectionStatus` / `infectionTone` | string | 感染阶段、病程、下一阶段/死亡倒计时与 `info`/`warn`/`fail`（M20-F1） |
| `weatherStatus` / `weatherTone` | string | 天气、环境温度、体温、潮湿、室内/火边与 `info`/`warn`/`fail`（M20-F2） |
| `weatherClass` | string | `clear`/`rain`/`fog`/`cold`，用于 `weather-overlay` 调色（M20-F2） |
| `rangedStatus` / `rangedTone` | string | 主手远程武器弹匣/备弹/换弹文案与 `info`/`warn`/`fail`（M23-F1-T8） |
| `forageStatus` / `forageTone` | string | 可觅食/觅食中/耗尽文案与 `info`/`warn`（M23-F2a-T5） |
| `forageValid` / `forageMissingTool` / `forageExhausted` / `forageSearching` | bool | 是否存在附近节点、缺工具、耗尽、正在搜索 |
| `forageName` / `forageToolName` | string | 节点/区域名与所需工具名 |
| `forageSeconds` / `forageProgress` / `forageRespawnMinutes` | number | 预计耗时、搜索进度 0~1、耗尽刷新剩余分钟 |
| `fishingStatus` / `fishingTone` | string | 可钓鱼/钓鱼中/咬钩文案与 `info`/`warn`（M23-F2a-T5） |
| `fishingValid` / `fishingMissingRod` / `fishingMissingBait` / `fishingWaiting` / `fishingBiting` | bool | 是否存在附近钓点、缺鱼竿/鱼饵、等待咬钩、咬钩窗口 |
| `fishingName` / `fishingRodName` / `fishingBaitName` | string | 钓点名、所需鱼竿/鱼饵名 |
| `fishingSeconds` / `fishingProgress` / `fishingBiteRemaining` | number | 预计等待、等待进度 0~1、咬钩剩余秒数 |
| `threatStatus` / `threatDirection` | string | 最近受击提示文本与边缘方向 `left/right/top/bottom`（M15-T3） |
| `threatRemaining` / `threatDuration` | number | 受击边缘提示剩余/总时长（秒，用于淡出） |
| `objective` / `objectiveTone` / `objectiveHint` | string | 目标文本、色调与附近交互提示 |
| `poiGuide` / `poiGuideTone` | string | 最近未完成 POI 的威胁/方向/距离/任务进度文案与 `info`/`warn`/`ok`（M19-F2） |
| `poiStageVisible` / `poiStageTone` | bool/string | 是否显示多阶段 POI 详情卡与 `info`/`warn`/`fail`/`ok` 色调（M33-F5-T5） |
| `poiStageTitle` / `poiStageObjective` / `poiStageMeta` / `poiStageNarrative` | string | 阶段标题、当前目标、钥匙/供电/路线/Boss 条件与当前阶段叙事；字段为空时隐藏对应行（M33-F5-T5） |
| `poiResultVisible` / `poiResultTitle` / `poiResultBody` / `poiResultTone` | bool/string | POI 完成/失败结果卡是否显示、标题、奖励/重试说明与 `info`/`ok`/`fail`；自动收起（M33-F5-T5） |
| `missionGuide` / `missionTone` | string | 当前焦点任务的来源/进度/奖励文案与 `info`/`warn`/`ok`（M19-F3；`L` 切换、`K` 放弃） |
| `eventGuide` / `eventTone` | string | 最近进行中随机事件的事件名/目标进度/方向/奖励与 `info`/`warn`/`ok`（M24-T5；不参与任务焦点切换） |
| `siegeStatus` / `siegeTone` | string | 夜间守夜预警/波次文本与 `info`/`warn`/`fail` |
| `siegeReportOpen` | bool | 战后独立战报面板是否打开；`ESC` 关闭（M21-F1c） |
| `siegeReportTitle` / `siegeReportRating` / `siegeReportTone` / `siegeReportHint` | string | 战报标题、评级、`info`/`warn`/`ok`/`fail` 色调与关闭提示 |
| `siegeReportLines` | array | 最多 8 行战报明细（波次/击杀/结构损失/破口/奖励等） |
| `baseActive` | bool | 玩家附近 16 格内是否有玩家建造的基地 |
| `baseStorageCount` | number | 基地内玩家仓库数量 |
| `baseStorageWeight` / `baseStorageCapacity` | number | 基地仓库总重量/总容量（kg，M18-F5） |
| `baseFarms` | number | 基地内农田数量 |
| `baseFarmGrowing` / `baseFarmMature` / `baseFarmDiseased` / `baseFarmThirsty` / `baseFarmFertilized` | number | 农田生长/成熟/病害/缺水/已施肥数量（F5-T2） |
| `baseBeds` / `baseCampfires` / `baseWorkbenches` | number | 基地内床位/篝火/工作台数量 |
| `baseDefensesIntact` / `baseDefensesDamaged` | number | 玩家建造防御结构的完好/受损数量 |
| `baseStatus` / `baseTone` | string | 基地汇总文案与 `info`/`warn`/`fail`（M18-F5） |
| `progressionValid` | bool | 成长数据是否可用（M19-F1） |
| `progressionLevel` | number | 角色等级 |
| `progressionXp` / `progressionXpNext` | number | 当前等级经验/下一级所需；`xpNext=0` 表示满级 |
| `progressionPerkPoints` | number | 待分配 Perk 点数 |
| `progressionSkills` | array | 4 项技能文案：近战/建造/制作/生存（含等级、效果与 XP） |
| `progressionPerks` | string | 已选 Perk 的展示文案（如 `猛击、疾行`） |
| `progressionChoices` | array | 3 项升级候选文案；无待选时为空串 |
| `progressionChoiceCount` | number | 当前有效候选数量（0~3） |
| `perkPrompt` | string | 升级待选提示；无待选时为空串 |
| `characterOpen` | bool | `C` 角色面板是否打开 |
| `dialogueOpen` | bool | 聚落对话面板是否打开（M23-F4-T3） |
| `dialogueSpeaker` / `dialogueText` / `dialogueHint` | string | 对话说话人、正文与操作提示 |
| `dialogueOptions` | array | 最多 5 项对话选项文案（Lua 1-based，Python 0-based list） |
| `dialogueSelected` / `dialogueOptionCount` | number | 当前选项下标（0~4）与有效选项数（0~5） |
| `actionHint` / `actionTone` | string | 动态操作提示（流血/疼痛/搜刮/拾取/基础操作）与 `info`/`warn`/`fail`（M15-T5） |
| `selectedSlot` | number | 选中快捷栏下标（0~4） |
| `slots` | array | 5 项 `{label, count}`（Lua 1-based，Python 0-based list） |
| `modFields` | array | 已注册 Mod HUD 字段，每项 `{modId, id, label, text, tone}`；空文本不显示 |
| `notice` / `noticeTone` | string | 提示文本与 `info`/`ok`/`fail` |
| `noticeRemaining` / `noticeDuration` | number | 提示剩余/总时长（秒） |

Mod 字段接口（每个 mod 只能 Set/Clear 自己的字段；`Hud.GetState().modFields` 可读取全部字段；最多 8 个）：

| 接口 | 返回 | 说明 |
|---|---|---|
| `Hud.RegisterField(id, label)` | bool | 声明字段；同 mod 重复调用只更新 label，满 8 个返回 false |
| `Hud.SetField(id, text, tone?)` | bool | 更新字段文本；`tone` 为 `info`/`warn`/`ok`/`fail`，缺省 `info`；未知字段/非法 tone 返回 false |
| `Hud.ClearField(id)` | bool | 清空文本并隐藏该行；字段仍保留 |

字段只在客户端显示，不写入存档、不参与内容指纹或联机同步；`Hud.GetState().modFields` 同时可供自定义 UI 读取。示例：
`mods/example_content/scripts/main.lua` 声明 `depot_status` 并显示“补给站 / 就绪”。空字符串会隐藏该行，但 label 保留在快照中。

```lua
Hud.RegisterField("depot_status", "补给站")
Hud.SetField("depot_status", "就绪", "ok")
Hud.ClearField("depot_status")
```

### 3.8 `UI`（脚本 UI 文档）

脚本 UI 文档由脚本按需加载；脚本 UI 的 C++ 侧只提供 RmlUi context、路径校验和输入路由，
不包含具体 mod 界面逻辑。内置 HUD、主菜单/暂停菜单与背包已迁移到 `mods/core_ui`
（Lua/Python 共用 `ui/hud.rml`、`ui/menu.rml`、`ui/inventory.rml`）。

| 接口 | 说明 |
|---|---|
| `UI.LoadDocument(documentId, path, show?, modal?, group?)` | 从 mod 根目录加载 `.rml`；`show` 默认 true，`modal` 默认 false |
| `UI.OpenDocument(documentId, path, modal?, group?)` | 未加载则加载，然后显示；`modal` 默认 true |
| `UI.CloseDocument(documentId, unload?)` | 隐藏并清除 modal；`unload=true` 同时卸载 |
| `UI.UnloadDocument(documentId)` | 销毁文档、listener 和元素引用 |
| `UI.UnloadGroup(group)` | 一次卸载同组文档（例如离开世界时清 HUD） |
| `UI.Show(documentId, visible?)` | 显示/隐藏已加载文档 |
| `UI.IsLoaded(documentId)` / `UI.IsVisible(documentId)` | 查询状态 |
| `UI.SetModal(documentId, modal)` | 设置输入模态；打开菜单/背包时使用 |
| `UI.IsInputCaptured()` | 当前是否有模态脚本 UI |
| `UI.SetText(documentId, elementId, text)` | 设置文本（内部转义 RML） |
| `UI.SetAttribute` / `UI.RemoveAttribute` | 设置/移除元素属性 |
| `UI.SetClass(documentId, elementId, className, enabled?)` | 切换 CSS class |
| `UI.SetProperty(documentId, elementId, property, value)` | 设置内联样式（如 `width`、`display`、`opacity`） |
| `UI.GetValue` / `UI.SetValue` | 读写表单控件值 |
| `UI.Focus(documentId, elementId)` | 聚焦元素 |

`path` 必须位于当前 mod 根目录内，拒绝绝对路径和 `..`，第一版只允许 `.rml`。
RML 里的 RCSS/图片相对路径由 RmlUi `JoinPath` 解析。

HUD 类非模态文档应把 `body` 设为 `pointer-events:none`，只给按钮/可点击子元素设 `pointer-events:auto`；否则全屏 body 会拦截其他脚本 UI 和世界点击。`mods/core_ui/ui/hud.rcss` 与 `example_scripted_gameplay/ui/challenge.rcss` 采用该写法；世界点击移动会跳过光标下方有可交互 UI 的点击。

生命周期：

```text
OnUiReady
  -> 脚本 Load/Open 当前界面
OnModReload（modId）
  -> C++ 已卸载该 mod 的旧文档
  -> 脚本重新创建 UI
```

热重载前 C++ 会强制 `UnloadOwner(modId)`；脚本错误禁用 mod 时也会卸载其文档。
脚本通过 `UI.IsInputCaptured()` 或 `SetModal` 配合游戏输入；文本框焦点沿用现有焦点阻断。

内置 HUD 依赖 `core_ui` 脚本 owner：owner 存在时 C++ 不加载 native HUD；owner 不存在
（脚本禁用/加载失败）时自动回退到 `assets/ui/hud.rml`。`hud` group 由引擎在菜单/背包
打开时统一隐藏，脚本无需自行处理这两个状态。

主菜单/暂停菜单同样由 `core_ui` 的 `ui/menu.rml` 控制。C++ 在 `ShowMainMenu` /
`ShowPauseMenu` 时同步发送 `OnMenuChanged`（暂停时没有 tick，不能依赖 `OnTick`）；
脚本据此加载/显示文档。`core_ui` 未加载、菜单文档加载失败或脚本被禁用时，C++ 自动回退
到 `assets/ui/menu.rml` 的 native `MenuPanel`。

### 3.8.1 `Audio`（M35-F5-T3）

| 接口 | 返回 | 说明 |
|---|---|---|
| `Audio.Play2D(clip, volume?)` | bool | 播放 `assets/audio/<clip>.wav`；`clip` 只允许字母/数字/`_`/`-`，不含扩展名或路径 |
| `Audio.Play3D(clip, x, y, radius?, volume?)` | bool | 世界空间播放；`radius` 默认 0（音频引擎默认距离），有效范围 0~256 |

`volume` 缺省 1，非有限值返回 false，有限值钳制到 0~1；`x/y` 必须有限。只播放已有 clip，不注册新资源；文件不存在、设备关闭/引擎未就绪或参数非法返回 false。`Audio.Play3D` 直接调用音频引擎，不写 `OnSound`，因此不会进入 AI 听觉；要引怪请用 `World.EmitSound`。调用不等待播放完成，首次文件解码仍沿用现有 miniaudio 同步加载路径。

### 3.9 `Menu`（内置菜单控制器）

`Menu` 面向前两者中由 `mods/core_ui` 实现的内置菜单；普通 mod 通常只需用 `UI`。
菜单开关由 C++ 状态机驱动，脚本通过 `OnMenuChanged` 同步事件加载/显示对应文档。

| 接口 | 返回 | 说明 |
|---|---|---|
| `Menu.GetState()` | table | `{open, pauseMode, canContinue, volume, musicVolume, sfxVolume, ambientVolume, bindings}` |
| `Menu.Action(action)` | bool | 触发核心菜单动作：`new_game` / `multiplayer` / `continue` / `resume` / `return_to_main` / `quit` |
| `Menu.ApplySettings(settings)` | table | 校验并保存 `{volume…, bindings=[{action,label,key}]}`；返回 `{ok, error}` |
| `Menu.KeyName(keyIdentifier)` | string | RmlUi 键盘事件 `e.key` → 配置键名（`W`/`SPACE`/`F1`…）；修饰键/未知键返回空串 |
| `Menu.PlayClick()` | - | 播放 UI 点击音效 |

`bindings` 的顺序与 `kMenuBindingDefs` 一致；`Menu.ApplySettings` 会校验音量范围、键位合法性和
重复键位，成功后写入 `config/game.json` 并应用输入绑定。主菜单/暂停菜单的页面切换与改键采集
由 `core_ui` 脚本完成；`Menu.Action` 只负责把动作交回游戏状态机。

### 3.10 `Inventory`（内置背包控制器）

`Inventory` 面向 `mods/core_ui` 的背包控制器；普通 mod 通常只需用 `UI`。C++ 保留容器规则和
native fallback，脚本只消费快照并提交动作。背包开/关或数据变化时发送 `OnInventoryChanged`。

| 接口 | 返回 | 说明 |
|---|---|---|
| `Inventory.GetState()` | table | `open, playerValid, weight, capacity, bagStacks, lootStacks, lootTitle, lootIsStorage, lootCanTakeAll, lootEntityId, status, statusTone, bag, equipment, loot, recipes, crafting` |
| `Inventory.Action(action)` | bool | 提交 `{type, from, fromIndex, fromEntityId, to, toIndex, count?}`；返回动作是否已交给 C++ |
| `Inventory.Close()` | bool | 关闭背包（打开时返回 true） |
| `Inventory.PlayClick()` | - | 播放背包 UI 点击音效 |

`bag` / `equipment` / `loot` 分别为 25 / 2 / 6 项槽位数组，每项字段：
`{filled, label, count, usable, equippable, entityId, condition, damaged, repairAvailable, perishable}`；
`condition` 为 0~1 的耐久/状态，内置 UI 在低于 100% 时于名称后显示百分比；
`damaged` 表示耐久未满的武器/衣物，`repairAvailable` 表示当前背包内有足够维修材料；
`perishable` 表示可腐败食物，内置 UI 显示“新鲜 %”或“[变质]”。
Lua 数组从 1 开始，Python 从 0 开始；槽位索引 `fromIndex/toIndex` 始终是 **0-based**。靠近玩家仓库时，`loot` 数组表示仓库内容，`lootIsStorage=true`，`lootEntityId` 为仓库实体 StableId。

`recipes` 是全部已加载配方的快照（内置背包 UI 每页显示 4 条，可用 `‹` / `›` 翻页）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` / `name` | string | 配方 id / 显示名 |
| `category` / `categoryLabel` | string | recipes-v4 分类 id / 中文标签（cooking/preservation/medical/crafting/general） |
| `outputDefId` / `outputLabel` | string | 产物物品 id / 显示名 |
| `outputCount` | number | 每次制作产出数量 |
| `known` | bool | 是否已学会；false 时 UI 显示“未学会”，不能制作 |
| `canCraft` | bool | 已学会且材料、工具、锅具、燃料、背包空间与制作站范围都满足 |
| `estimatedSeconds` | number | 按当前工具状态估算的实际耗时（秒）；缺工具时为 0 |
| `ingredients` | array | 消耗材料 `{label, required, have}`（Lua 1-based / Python 0-based list） |
| `tools` | array | 工具 `{label, required, have}`；只要求背包内拥有，不消耗但会磨损 |
| `cookware` | object? | 锅具 `{label, required, have}`；不消耗，需 `maxStack=1` 且可用（F5-T3） |
| `fuel` | object? | 燃料 `{label, required, have}`；每次制作消耗（F5-T3） |
| `station` / `stationLabel` | string | 配方要求的制作站 id / 显示名（空 = 任意地点） |
| `stationNearby` | bool | 当前是否处于该制作站范围内 |

`crafting` 是当前制作任务快照；无任务时 `active=false`，其余字段为空/0：

| 字段 | 类型 | 说明 |
|---|---|---|
| `active` | bool | 是否正在制作 |
| `recipeId` | string | 当前配方 id |
| `outputLabel` / `outputCount` | string / number | 产物显示名与数量 |
| `elapsed` / `duration` | number | 已用/总时长（秒；`duration` 已包含工具效率影响） |
| `progress` | number | 0~1 进度，可直接用于进度条 |
| `toolEfficiency` | number | 本次制作的工具效率（0.5~1.0；无工具为 1.0） |
| `batchCount` / `completedCount` | number | 本批总件数 / 已完成件数；单件制作为 `1/0` |
| `queue` | array | 后续制作队列，最多 8 项；每项 `{recipeId, outputLabel, outputCount, count}`，`count` 为请求件数，开始时会按当时材料/空间重新截断；队列随存档保存，读档后恢复 |

区域名为 `bag` / `equipment` / `loot`。动作 `type`：

| type | 字段 | 语义 |
|---|---|---|
| `transfer` | `from/fromIndex/to/toIndex` | 背包内换位、装备/卸下、丢地面、地面拾取；靠近仓库时 `bag→loot` 存入仓库、`loot→bag` 取出 |
| `use` | `from/fromIndex` | 使用背包物品 |
| `equip` | `from/fromIndex` | 按物品类型自动选择主手/躯干 |
| `unequip` | `from/fromIndex` | 卸下装备 |
| `drop` | `from/fromIndex` | 丢弃背包/装备物品 |
| `pickup` | `fromEntityId` | 拾取附近地面物品/容器/仓库物品 |
| `craft` | `fromIndex`, `count?` | 开始制作 `recipes[fromIndex]`（0-based）；空闲时立即开始，已有任务时追加到制作队列；`count` 为请求件数（1~99，缺省 1）。追加队列只校验配方已学会、制作站范围与队列上限，材料/工具/空间在真正开始该项时再校验；工具效率决定每件实际时长 |
| `cancel_craft` | 无 | 取消当前制作/剩余批次并清空整个制作队列；已完成件已扣除材料，当前计时件未扣材料，因此无需返还 |
| `queue_up` / `queue_down` | `fromIndex` | 将队列第 `fromIndex` 项（0-based）上移/下移一位；越界或已到队首/队尾时拒绝并提示，不改变制作与材料 |
| `queue_remove` | `fromIndex` | 移除队列第 `fromIndex` 项（0-based）；只移除后续队列项，不取消当前制作、不返还材料 |
| `queue_count` | `fromIndex`, `count` | 把队列第 `fromIndex` 项的请求件数设为 `count`（1~99）；越界/队列项不存在时拒绝，不校验材料，真正开始时仍按可用材料/工具/空间截断 |
| `repair` | `from/fromIndex` | 维修背包中耐久未满的武器/衣物；消耗 `inventory.repairKit` 指定材料，恢复 `inventory.repairAmount × repairEfficiency` 耐久 |
| `store_all` | `fromEntityId` | 将背包全部可存物品存入附近仓库；仅 `lootIsStorage` 时有效 |
| `take_all` | `fromEntityId` | 从指定尸体/世界容器的 `LootContainer` 取出全部可容纳物品（单机），或从附近仓库取出；仓库路径要求 `lootIsStorage`，尸体/世界容器要求 `lootCanTakeAll` |
| `sort_storage` | `fromEntityId` | 按物品类型/defId/状态整理附近仓库；仅 `lootIsStorage` 时有效 |

`craft` 只启动计时任务，材料在计时完成时才原子扣除/校验，工具在同一时刻按配方的 `wear` 磨损；
`Inventory.Action` 的返回值只表示动作已进入玩家命令管线。制作进度与结果以随后
`OnInventoryChanged` / `GetState()` 为准。`count>1` 时服务端先在容器副本上连续预检，
把实际可做件数截断到材料、工具磨损和输出空间允许的上限（例如请求 5 件但只够 3 件，则本批为 3 件）；
每件完成后自动开始下一件，并重新计算工具效率与耗时。中途材料/工具/制作站/背包空间不足会停止剩余批次，
已完成件保留；`batchCount` 为实际总件数，`completedCount` 为已完成件数。取消只丢弃当前计时件，
已完成件不会返还材料。

已有任务时再次提交 `craft` 会进入 FIFO 制作队列（最多 8 项，可跨配方）；当前批次完成后自动开始
队首，开始前重新检查已学会、制作站、材料、工具与输出空间，请求件数超过实际上限时按实际数量截断。
任一批次中断或 `cancel_craft` 会清空剩余队列并保留已完成产物。
当前批次进度与后续队列随存档保存（`CraftingJob` 组件），读档后恢复；若配方已不存在或未学会，会取消该任务并清空队列。制作站与材料在继续计时完成或队首真正开始时再校验。
`crafting.queue` 不包含当前任务；当前任务仍在 `recipeId` / `batchCount` / `completedCount` 中。
队列编辑使用 `queue_up` / `queue_down` / `queue_remove` / `queue_count`，`fromIndex` 指向
`crafting.queue` 的 0-based 下标；调序/移除不改变请求件数，`queue_count` 只改请求件数，均不会改动当前任务或已扣材料。
内置 UI 用 `<` / `>` 切换选中项，`↑` / `↓` 调序，`−` / `+` 调整请求件数，`×` 移除；
选中项只是 UI 本地状态，不写入快照，队列刷新后越界会自动钳制到有效范围。

配方数据默认来自 `assets/recipes.json`（`config/game.json` 的 `recipes.file` 可替换）：

```json
{
  "format": "recipes-v4",
  "recipes": [
    {
      "id": "padded_jacket",
      "name": "加固夹克",
      "category": "crafting",
      "output": { "item": "padded_jacket", "count": 1 },
      "ingredients": [
        { "item": "jacket", "count": 1 },
        { "item": "rag", "count": 2 }
      ],
      "tools": [ { "item": "hammer", "count": 1, "wear": 0.1 } ],
      "station": "workbench",
      "time": 3.0
    }
  ]
}
```

`tools` 为可选的**非空数组**；工具必须存在于背包中、不扣除，且不能同时出现在 `ingredients` 中。
工具必须是不可堆叠物品（`maxStack=1`），每次成功制作按可选 `wear`（0~1，缺省 0.1）降低其
`condition`；`condition` 降到 0 时工具损坏并从背包移除。`condition<=0` 的工具不计入 `have`，
因此损坏后配方会显示缺工具。`time` 为可选的制作时长（秒，0.1~60，缺省 1.0）。
工具状态会影响实际耗时：工具效率为 `0.5 + 0.5 × 最低可用工具耐久`（无工具为 1.0，缺工具
为 0），实际耗时 = `time / 效率`，最多延长到基础耗时的 2 倍。`estimatedSeconds` 是当前工具
状态下的预计耗时；背包 UI 在配方名后显示 `· 3.2s`，制作开始后 `crafting.duration` 为实际
时长、`crafting.toolEfficiency` 为本次效率。`category` 为 recipes-v4 可选分类；`cookware`/`fuel`
分别要求不消耗的锅具与每次消耗的燃料；`station` 为 recipes-v3+ 可选制作站 id：世界对象
用逻辑名 `object.<station>` 标记（内置 `workbench` → `object.workbench`、`campfire` →
`object.campfire`、`locker` → `object.locker`，后者由可建造“雨水收集器”提供）；玩家需在
`crafting.stationRange`（默认 2.5 格）内才能开始/完成该配方，
离开范围后计时完成会失败且不消耗材料。显示名由 `config/game.json` 的
`crafting.stationLabels` 映射，缺省回退为 station id；未知 station 不会被拒绝，便于 mod
先用自定义对象扩展，示例：
`{"id":"campfire_beans","station":"campfire","ingredients":[{"item":"canned_beans","count":1}],"output":{"item":"cooked_beans","count":1},"time":2.0}`。
`recipes-v1` 仍兼容（无 `tools`/`time`/`station`）；要使用工具、自定义时长/磨损
或制作站需把 `format` 升级为 `recipes-v3`（v2 仍兼容 tools/time）；分类/锅具/燃料需 `recipes-v4`。

蓝图解锁由物品的可选 `unlocksRecipes` 数组声明：
```json
{ "id": "magazine_cooking", "name": "烹饪杂志", "icon": "item.magazine_cooking",
  "weight": 0.2, "maxStack": 1, "type": "generic",
  "unlocksRecipes": ["campfire_beans"] }
```
被任意物品引用的配方默认显示“未学会”，不能用材料/脚本直接开始；玩家对杂志/图纸执行
`Inventory.Action({type="use", from="bag", fromIndex=N})` 后，物品消耗 1 件并学会全部未学配方
（全部已学会时不消耗）。学会状态存在玩家 `KnownRecipes` 组件并随存档保存；未被任何物品引用的
配方始终已学会。`RecipeRegistry` 会拒绝引用不存在配方 id 的 `unlocksRecipes`。
启动时 `RecipeRegistry` 会与物品定义交叉校验（未知物品、重复 id/材料/工具、工具与材料重复、
非法数量/时长/磨损/station、未知蓝图配方都会整体拒绝）。制作是原子的：计时完成时才校验材料、工具、制作站与扣除后的背包空间，
全部成功才一次性替换容器；失败不消耗材料也不磨损工具，取消同样不扣材料。
制作任务与制作队列写入玩家 `CraftingJob` 存档组件（活跃批次时长/进度/批次计数 + 后续队列）；退出/读档会恢复，但因计时期间未扣材料，失效任务被取消时不会损失物品。

装备维修（内置 `repair` 动作）：只允许耐久在 `(0,1)` 的不可堆叠武器/衣物，消耗配置
`inventory.repairKit`（默认 `repair_kit`）指定数量（`inventory.repairKitCount`，默认 1），
恢复 `inventory.repairAmount × repairEfficiency`（默认 0.5 × 1.0）耐久并钳制到 1.0；
缺少维修包或装备已损坏时不修改容器。槽位快照的 `damaged` / `repairAvailable` 可直接驱动
自定义 UI 的维修按钮；内置 UI 在背包右键菜单显示“维修”，没有维修包时提示“维修（缺维修包）”。
维修结果通过状态行/日志反馈前后耐久。维修只在背包内操作，不改变正在进行的制作任务；
成功维修后武器会立即按新耐久参与后续制作效率。护甲可先卸下到背包再维修。
配方产物可以引用任意已注册物品，新增配方只需改 JSON（内置 UI 每页 4 条，超出部分用
`‹` / `›` 翻页；自定义脚本 UI 可直接读取 `Inventory.GetState().recipes` 自行展示）。

### 3.11 `Save`（存档列表、读档、删除与快速保存）

`Save` 面向 `mods/core_ui` 的主菜单读档页与暂停菜单保存按钮；普通 mod 可用它做存档管理界面。
列表是同步只读快照；读档会请求状态机切换，实际切换在当前帧末完成。

| 接口 | 返回 | 说明 |
|---|---|---|
| `Save.List()` | table | `{ok, error, saves=[{id,name,updatedText,mapId,gameVersion}]}`；按更新时间倒序，返回全部存档 |
| `Save.Load(id)` | table | `{ok, error}`；主菜单请求载入指定存档，帧末切换状态 |
| `Save.Delete(id)` | table | `{ok, error}`；主菜单删除指定存档及其备份；存档写入中拒绝 |
| `Save.QuickSave()` | bool | 请求带缩略图的后台快速存档；仅在世界内可用，成功只代表请求已提交 |

`Save.Load`/`Save.Delete` 只允许在主菜单调用；`Load` 是异步状态切换：调用返回时菜单仍存在，当前帧末进入新会话。
`Save.QuickSave` 的最终结果通过 `OnSaveBegin/OnSaveEnd` 与 HUD 提示反馈，写入由 `zl-save` 线程完成。
`core_ui` 读档页每页显示 8 条，底部“上一页/下一页”翻页；删除后自动收敛页码。

### 3.11.1 `Mod.Data`（跨存档脚本状态）

`Mod.Data` 保存当前 mod 自己的字符串 key/value，按 mod id 自动隔离。适合最高波次、最佳成绩、解锁进度等小状态；复杂结构先用 `Json.Encode`（F4-T3）。

| 接口 | 返回 | 说明 |
|---|---|---|
| `Mod.Id()` | string | 当前 mod id，便于日志/命名空间 |
| `Mod.Data.Get(key)` | string/nil/None | 不存在返回 nil/None |
| `Mod.Data.Set(key,value)` | bool | value 必须是字符串；非法/超限 false 且不覆盖旧值 |
| `Mod.Data.Remove(key)` | bool | 不存在返回 false |
| `Mod.Data.Keys()` | table/list | 当前 mod 的 key，字典序 |
| `Mod.Data.Clear()` | bool | 清空当前 mod；无数据时幂等 true |

限制：最多 256 个 mod；每 mod 最多 64 个 key；mod id ≤ 128 字节；key ≤ 64 字节；value ≤ 4096 字节；全局 key+value ≤ 128 KB。mod id/key 不能为空或含控制字符。`Set` 在副本上校验后一次提交，超限不会留下部分写入。

```lua
Mod.Data.Set("best_wave", tostring(wave))
local best = Mod.Data.Get("best_wave")
for _, key in ipairs(Mod.Data.Keys()) do
    Log.Info(key .. "=" .. tostring(Mod.Data.Get(key)))
end
```

```python
Mod.Data.Set("best_wave", str(wave))
best = Mod.Data.Get("best_wave")
for key in Mod.Data.Keys():
    print(key, Mod.Data.Get(key))
```

生命周期：

- 热重载脚本保留数据；新游戏/换存档清空，读档恢复当前存档的数据。
- 数据随世界实体快照保存；`Set` 后下一次存档生效。
- 联机（loopback/client）下 `Get/Keys` 为空、`Set/Remove/Clear` 返回 false；每个 mod 每次加载最多警告一次。
- 不参与内容指纹；换 mod 不自动清理旧数据，重新安装同名 mod 可恢复。
- 应用退出不保证 `OnWorldExit`；需要保存的状态应在游戏中定期写，或写后调用 `Save.QuickSave()`。

### 3.11.2 `Json`（双后端 JSON 工具）

Lua 没有内置 JSON；`Json` 提供与 Python 后端同名的编解码，适合把复杂结构存入 `Mod.Data` 或传给 UI。

| 接口 | 返回 | 说明 |
|---|---|---|
| `Json.Encode(value)` | string/nil | 支持对象/数组/字符串/数字/bool/null；失败返回 nil/None |
| `Json.Decode(text)` | value/nil | JSON → Lua table / Python dict/list；失败返回 nil/None |
| `Json.Array()` | array | 空数组标记；`Json.Encode(Json.Array()) == "[]"` |
| `Json.Null()` | null | null 标记；Lua 解码 JSON null 得到该标记，Python 得到 `None` |

Lua 规则：

- key 为连续整数 `1..N` 的 table 编码为数组；空 table 编码为 `{}`（对象），空数组用 `Json.Array()`。
- 非数组 table 的 key 必须是字符串；数字/混合 key 会编码失败。
- JSON `null` 在嵌套结构中用 `Json.Null()` 保留；`Json.Encode(nil)` 顶层按 null 输出。
- 对象 key 会排序，双后端输出稳定。

Python 规则：用 `dict`/`list`；dict key 必须是 `str`；`None` 为 null，`Json.Array()` 返回 `[]`，`Json.Null()` 返回 `None`。

```lua
local text = Json.Encode({ wave = 3, tags = { "night", "rain" } })
local value = Json.Decode(text)
Mod.Data.Set("best_run", Json.Encode({ score = 1200, wave = 3 }))
```

限制：容器深度 ≤ 16、单个字符串 ≤ 64 KB、数组/对象 ≤ 1024 项；数字必须有限；循环引用失败并 `ZL_WARN`。非法 JSON、非法类型、超限、非字符串 `Decode` 输入均返回 nil/None。

### 3.7 `Craft` / `Build` / `Mission`（M35 只读目录与制作/建造动作）

| 接口 | 返回 | 说明 |
|---|---|---|
| `Craft.List()` | table | `{count,truncated,recipes=[...]}`；`recipes` 字段见下 |
| `Craft.GetState()` | table | `{active,recipeId,outputDefId,outputLabel,outputCount,elapsed,duration,progress,batchTotal,batchCompleted,queue=[{recipeId,count}]}` |
| `Craft.IsKnown(recipeId)` | bool | 是否已学会（锁定配方读 `KnownRecipes`） |
| `Craft.Start(recipeId,count?)` | result | 开始制作/入队；`count` 1~99，缺省 1；校验配方/学会/材料/站点 |
| `Craft.Cancel()` | result | 取消失败/清空制作队列；没有活跃批次与队列时 `ok=false` |
| `Craft.Learn(recipeId)` | result | 仅单机；未知/已会拒绝，成功追加 `KnownRecipes` |
| `Build.List()` | table | `{count,truncated,items=[...]}`；`items` 字段见下 |
| `Build.CanPlace(defId,x,y,vertical?)` | result | 仅单机；预检距离/地面/占用/任务/系统开关/木材，不扣物品 |
| `Build.Place(defId,x,y,vertical?)` | result | 放置；单机扣木材/触发事件，联机提交 `PlaceStructure` |
| `Build.Demolish(x,y)` | result | 拆除；单机立即执行，联机提交 `DemolishStructure` |
| `Build.Repair(x,y)` | result | 维修；单机立即执行，联机提交 `RepairStructure` |
| `Build.ToggleDoor(x,y)` | result | 开关门；单机立即执行，联机提交 `ToggleDoor` |
| `Mission.List(filter?)` | table | `{count,truncated,missions=[...]}`；`filter` 为 `all/active/available/completed/failed/locked`，缺省 `all` |
| `Mission.Get(id)` | table 或 nil | 单个任务定义 + 玩家进度 |

配方字段：`{index,id,name,category,categoryLabel,outputDefId,outputLabel,outputCount,seconds,station,stationLabel,stationNearby,known,canCraft,ingredients=[{defId,label,required,have,wear}],tools=[...],cookware?,fuel?}`。`index` 与 `Craft.Start` 的配方索引一致；`canCraft` 已包含已知/材料/工具/站点判定。

`Craft.Start/Cancel` 映射到 `InventoryActionType::Craft/CancelCraft`：单机立即执行，loopback/KCP 提交成功返回 `queued=true`，实际结果由服务器通过 HUD/事件体现。`Start` 在提交前预检数量 1~99、配方存在、已学会、制作站与 `CanCraft`（材料/工具/锅具/燃料/输出空间）；`Cancel` 在无活跃批次与队列时 `ok=false`。`Craft.Learn` 仅 Direct，联机拒绝；成功只追加 `KnownRecipes`，不消耗物品。

建造字段：`{index,id,name,sprite,woodCost,maxHp,blocksMovement,storageKg,storageSlots,layer,category,door,requiresMission}`。`index` 与建造目录顺序一致。

`Build.CanPlace` 仅 Direct，复用建造模式的距离/地面/占用/任务/系统开关/木材预检，不扣木材；联机返回 `ok=false`（由服务器校验 `Build.Place`）。`Build.Place` 拒绝不支持竖向的建造项；Direct 走 `WorldDemoState` 放置路径（扣木材/生成设施或地图结构/建筑 XP/`OnBuildPlaced`），loopback/KCP 提交 `PlaceStructure`（`index`=目录索引，`flags bit0`=竖向）。`Build.Demolish/Repair/ToggleDoor` Direct 按格坐标查找玩家结构并校验距离/所有权/耐久/门口占用；联机分别提交 `DemolishStructure`（flags=0 立即执行）、`RepairStructure`、`ToggleDoor`。所有动作返回 `{ok,queued,error}`。

任务字段：`{index,id,name,chain,order,kind,source,task,status,day,progress=[int...],objectives=[{kind,target,count,progress,description}],reward={xp,skill,item,itemCount,extraItem,extraItemCount,blueprint,label}}`。`status` 为 `active/completed/failed/available/locked`。

---

## 4. 事件表 v2（`Events.On`，向后兼容 v1）

```lua
Events.On("事件名", function(e) ... end)   -- e 为字段表
Events.Off("事件名")                        -- 取消该事件全部订阅
Events.Off("事件名", 某个函数)              -- 只取消指定函数
```

| 事件 | 触发时机 | `e` 的字段 |
|---|---|---|
| `OnGameStart` | 应用启动、脚本加载后（可能还在主菜单，玩家未生成） | - |
| `OnWorldStart` | 世界加载/新游戏后、玩家已生成（同步一次） | `saveId, isNewGame, day, playerId` |
| `OnWorldExit` | 离开世界、自动存档前（同步一次） | `saveId` |
| `OnTick` | 每个模拟 tick 末 | `tick` |
| `OnPlayerMove` | 玩家到达点击移动的目标格 | `playerId, x, y, z` |
| `OnPlayerDeath` | 玩家死亡（转尸体时） | `playerId, sourceId, cause`（`sourceId` 为击杀者稳定 ID，环境伤害为 0；`cause` 见下） |
| `OnDamage` | 伤害实际扣血后（SP；每次扣血一条） | `targetId, sourceId, amount, damageType, cause, remainingHp, lethal, blocked`；闪避/弹反/完全格挡/零伤害不发 |
| `OnZombieSpawn` | 僵尸实体生成/实体化 | `zombieId, x, y, z` |
| `OnZombieDead` | 僵尸死亡（转尸体时） | `zombieId, killer`（`killer` 为 `LastDamage.sourceId`，未知为 0） |
| `OnAnimalKilled` | 动物死亡（转尸体时） | `animalId, speciesId, killerId, x, y, z` |
| `OnItemPickup` / `OnItemDrop` | 玩家拾取/丢弃物品 | `itemId, defId, count` |
| `OnItemUsed` | 玩家成功使用消耗品/工具后（SP） | `playerId, defId, spoiled, remaining`；`remaining` 为使用后槽位剩余数量（0 = 空） |
| `OnLootSearched` | 容器首次搜刮成功（SP） | `playerId, containerId, tableId, itemCount`；空容器也发，`itemCount=0`；重复搜索/失败不发 |
| `OnCraftStart` | 一个制作批次开始（SP） | `playerId, recipeId, outputDefId, outputCount, batchCount`；`batchCount` 为实际开始件数（可能被材料/空间截断） |
| `OnCraftFinished` | 一个制作批次完成/失败/中断（SP） | `playerId, recipeId, outputDefId, outputCount, batchCompleted, failed`；成功时 `failed=false` |
| `OnBuildPlaced` | 建造成功（SP） | `playerId, defId, x, y, z, vertical`；x/y 为世界格坐标，`vertical` 为实际朝向 |
| `OnBuildDemolished` | 拆除成功（SP） | `playerId, defId, x, y, z`；x/y 为世界格坐标 |
| `OnBuildRepaired` | 维修成功（SP） | `playerId, x, y, z`；批量维修每个结构各发一条 |
| `OnSound` | 世界声音（脚步/枪声等） | `x, y, z, radius, soundType, clip`（`soundType`：`"Generic"/"Gunshot"/"Impact"/"Footstep"/"ZombieGrowl"/"Ui"`；`clip` 为可选逻辑音效名） |
| `OnChunkLoaded` / `OnChunkUnloaded` | 地图 chunk 加载/卸载 | `cx, cy` |
| `OnModReload` | 某 mod 热重载成功后 | `modId` |
| `OnSaveBegin` / `OnSaveEnd` | 后台存档提交/完成 | - |
| `OnUiReady` | UI Host 初始化完成，可以加载脚本 UI | - |
| `OnUiEvent` | 脚本 UI 文档内发生鼠标/键盘/拖拽/表单事件 | `modId, document, element, type, value, key, button, mouseX, mouseY` |
| `OnMenuChanged` | 主菜单/暂停菜单打开、关闭或切换页面模式 | `open, pauseMode, canContinue` |
| `OnInventoryChanged` | 背包打开/关闭或背包数据刷新 | `open` |

注意事项：

- `OnPlayerDeath.cause` 为小写稳定标识：`unknown/blunt/bite/claw/debug/bleed/starvation/dehydration/infection/hypothermia/fire/poison`；不要用它做本地化显示。
- `OnDamage.damageType` 为小写 `DamageType`：`blunt/bite/claw/debug/fire`；`amount` 是护甲/格挡减伤后的实际伤害，`remainingHp` 最低为 0。普通格挡的剩余伤害仍会发事件（`blocked=true`）；闪避、弹反、完全格挡和零伤害不发。该事件 M35 只在单机派发。
- `OnItemUsed` / `OnLootSearched` M35 也只在单机派发；`OnItemUsed.remaining` 是使用后槽位剩余数量（0 = 空；水壶替换为空瓶时仍为 1），蓝图/杂志学会配方时 `spoiled=false`。`OnLootSearched` 只在容器首次搜索成功时发一次，空容器 `itemCount=0`，重复搜索和搜索失败不发。
- `OnCraftStart` / `OnCraftFinished` M35 只在单机派发；一个批次开始发一次 Start，批末完成/失败/取消发一次 Finished（不是每件一条）。`outputCount` 是每次制作产物数量，成功总产量为 `outputCount * batchCompleted`；`batchCompleted` 是批内已完成件数；失败/取消时 `failed=true`，材料中断时保留已完成件数，取消时已完成件数照常返回。无效 Start（配方不存在/未学会/缺材料/站位不符）不发 Start，仅队列取消（没有活跃批次）不发 Finished。
- `OnBuildPlaced` / `OnBuildDemolished` / `OnBuildRepaired` M35 只在单机派发，联机建造/拆除/维修走服务器权威，事件留给 M36；放置/拆除/维修成功后才发，距离/木材/占用/归属/任务门槛拒绝不发。`OnBuildPlaced.vertical` 是实际朝向（不支持竖向的建造项恒为 false）。拆除首次按 X 只进入确认窗口，不发 Demolished；`OnBuildRepaired` 对“维修全部”中的每个实际修复结构各发一条，不含车辆/工具维修。
- 世界事件是**排队派发**：世界系统产生的事件在当前 tick 末统一派发；脚本回调里再
  产生事件（如 `Zombies.Spawn`）会留到**下一 tick**，天然不会自己触发自己形成风暴。
- `OnUiEvent` / `OnMenuChanged` / `OnInventoryChanged` 是例外：它们同步派发（不进入下一
  tick 队列），因此主菜单/暂停等 `timeScale=0` 状态下也能响应 UI 点击、菜单切换和背包
  刷新；回调里可以安全地 Load/Close 文档。
- `OnUiEvent` 的拖拽类型为 `dragstart / dragover / dragout / dragdrop / dragend`；
  `mouseX/mouseY` 为 RmlUi 文档坐标。
- `OnUiReady` 是加载脚本 UI 的推荐时机；不要在脚本入口顶层直接加载界面。
- `OnGameStart` 是**应用启动、脚本加载后**（可能还在主菜单），玩家**尚未生成**；要操作玩家请用 `OnWorldStart`，或在 `OnTick`/`SetTimer` 里先判 `Game.GetPlayerId() ~= 0`。
- `OnWorldStart` 在玩家实体已生成/读档实体恢复后同步发一次；`OnWorldExit` 在离开世界、自动存档前同步发一次。单机暂停菜单里的“返回主菜单”只是覆盖层，不算离开世界；应用进程退出时 EventBridge 已先释放，不能保证收到 `OnWorldExit`。
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

**不可用**（一律为 `nil`）：`os`、`io`、`debug`、`package`、`dofile`、`loadfile`、`load`。
mod 不能读写任意文件、不能起进程。

**Lua `require` 受限可用**：只能加载当前 mod 的 `<modRoot>/scripts/**` 下的 `.lua`，支持
`require("util")` / `require("wave.director")` / `require("./local")`；拒绝 `..`、绝对路径、
盘符/冒号和 symlink 逃逸。单文件 ≤ 1 MB、嵌套 ≤ 32 层、循环依赖报错；每个 mod 独立缓存，
热重载清空。Python 后端仍只执行单入口 `main.py`，不支持本地模块 import（`import` 仅白名单标准库）。

---

## 7. 官方示例

| 示例 | 演示点 | 进游戏后看什么 |
|---|---|---|
| `mods/example_hello` | 最小结构：启动日志、等玩家生成后发开局物品、`OnTick` 周期查询玩家/僵尸 | 进游戏约 1 秒后日志 `starter items: bandage x2, axe x1`；每约 10 秒一行 `survivor at ...` |
| `mods/example_reactive` | 事件反应：击杀奖励（5 杀送绷带）、拾取日志、枪声日志、死亡提示、`SetTimer` | 按 `F6` 演示击杀看 `zombie N down (kills=...)`；按 `F7` 看 `gunshot at ...`；按 `F` 拾取看 `picked up ...` |
| `mods/example_ui` | 脚本 UI：懒加载 `ui/panel.rml`、文本输入/按钮事件、关闭文档、热重载重建 | 右下角 Script UI 面板；输入框按回车/失焦看 `input:`，点刷新/关闭 |
| `mods/core_ui` | 内置 HUD、主菜单/暂停菜单与背包控制器：Lua/Python 共用 `ui/hud.rml`、`ui/menu.rml`、`ui/inventory.rml`，读取 `Hud.GetState()`/`Menu.GetState()`/`Inventory.GetState()`/`Save.List()` | 进入世界后 HUD 由脚本加载；菜单/背包由脚本打开，支持设置/改键、拖拽、右键操作（含武器/衣物维修）、定时制作/批量数量/跨配方队列/队列选中调序、数量调整与移除/进度/取消、配方每页 4 条翻页、分页读取/删除存档与暂停保存；禁用脚本后自动回退 native UI |

这些示例都可以直接改文件保存体验热重载（例如把 `example_reactive` 里"每 5 杀"改成"每 2 杀"）。
每个示例目录同时提供 `scripts/main.lua` 与 `scripts/main.py`（同 id、同行为）——
改 `config/game.json` 的 `script.backend`（或命令行 `--script.backend=python`）即可切换，
用同一份 mod 对照两个后端。

### 7.1 Python 后端（可选，M8）

启用：CMake 用预设 `x64-debug-python` / `x64-release-python`（或 `-DZL_ENABLE_PYTHON=ON`）构建；运行期把
`config/game.json` → `script.backend` 设为 `python`（或加 `--script.backend=python`）。
未启用该后端的构建里，backend=python 的 mod 会以清晰错误跳过，不影响其它 mod。

> Python 预设与普通预设**共用构建目录**：`x64-debug-python` → `build/x64-debug`，
> `x64-release-python` → `build/x64-release`；不存在 `build/x64-release-python` 目录。
> 构建后直接运行 `build/x64-release/ZomboidLike.exe`。第一次配置会安装 Python
> 依赖，可能停留约 1 分钟；切换回普通预设时需要重新配置以关闭 `ZL_ENABLE_PYTHON`。

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
- 入口默认 `scripts/main.py`（可在 `mod.json` 的 `main` 显式指定）；M35 不支持 Python 本地模块
  import（不能 `import` 自己 scripts/ 下的 .py），需要多文件请用 Lua 后端。

---

## 8. 常见问题（FAQ）

| 现象 | 原因与处理 |
|---|---|
| 日志没有"mod 已加载" | 看扫描日志：目录无 `mod.json` / JSON 语法错 / `api` 过新 / `backend` 未注册 → 日志会写明并跳过 |
| `Player.AddItem` 返回 0 | 物品 id 未注册或背包容量不足 |
| 中文日志乱码 | 脚本文件保存为 UTF-8（BOM 兼容） |
| 改了文件没重载 | 确认保存成功；重载轮询默认 1 秒（`config/game.json` → `script.hotReloadSeconds`）；也可在配置里关闭 `script.hotReload` 排查 |
| 想关掉全部脚本 | `config/game.json` → `script.enabled: false` |
| 能加载别的 .lua 文件吗 | Lua 可 `require("util")` / `require("wave.director")` / `require("./local")`，只能在当前 mod 的 `scripts/**` 内，单文件 ≤1 MB、嵌套 ≤32 层；Python 仍单文件 |

---

## 9. v1 已知限制

- Lua 支持 `require` 多文件（仅 `<modRoot>/scripts/**`）；Python 仍单入口 `main.py`，不支持本地模块 import。无资源（贴图/音效）注册 API。
- `OnZombieDead.killer` 来自 `LastDamage.sourceId`（环境/未知伤害为 0）；`OnPlayerMove` 仅点击移动到达；`OnSaveBegin/End` 已接入后台存档。
- `Zombies.Count` 只统计实体僵尸（离屏虚拟僵尸不计）。
- 死循环防护边界见 §6.4。
- Python 后端（M8）与 Lua 共用同一套 Host API / 事件表（差异见 §7.1）；默认构建仍只带 Lua。

---

## 10. API v1 → v2 兼容

- `mod.json` 省略 `api` 或声明 `"api": 1` 时继续加载，初始化日志有一条“API 版本较旧”告警；`api` 大于当前版本（2）仍拒绝加载。
- v1 的 `Game/Player/World/Zombies/Items/Hud/Events/...` 函数签名与返回值保持不变；M35 新增 API/事件均为可选。
- v1 兼容写 API 例外仍按本地行为执行：`Player.Teleport/AddItem`、`World.SpawnItem`、`Zombies.Spawn`；新写 API 在联机下 fail-closed。
- 旧存档没有 `MDAT` 时 `Mod.Data` 为空（`Get` nil/None、`Keys` 空），M35 不要求旧档新增组件或迁移；`OnWorldStart` 在读档实体恢复后派发。
- Lua `require` 是可选的；Python 仍单入口 `main.py`，不支持本地模块 import。v1 Python mod 的旧函数照常工作。
- 回归：`M35-F4-T5` 覆盖 `api:1` 告警加载与 v1 `Log/Events`，`M35-F4-T1` 覆盖旧档无 `MDAT`；全量 Debug/Release/Python 由交付回归执行。
