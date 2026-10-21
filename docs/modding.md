# ZomboidLike Mod 开发指南（Lua / Python 可选，API v1）

> 适用版本：M7/M8 脚本系统 v1 · 参考示例：`mods/example_hello`、`mods/example_reactive`、`mods/example_ui`
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
| `Game.ToggleInventory()` | bool | 打开/关闭背包，返回切换后的打开状态（菜单打开时无效） |
| `Game.IsInventoryOpen()` / `Game.IsMenuOpen()` | bool | 查询核心 UI 当前是否打开 |

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

内置物品 id（`assets/items.json`）：`bandage` / `canned_beans` / `axe` / `kitchen_knife` / `rag` / `jacket` / `tote_bag` / `water_bottle` / `painkillers` / `energy_bar` / `hammer` / `screwdriver` / `hiking_boots` / `backpack` / `makeshift_spear` / `padded_jacket` / `cooked_beans` / `magazine_cooking` / `repair_kit`。

### 3.7 `Hud`（只读 HUD 快照）

`Hud.GetState()` 返回当前 HUD 数据；核心 HUD 由 `mods/core_ui` 的 Lua/Python 控制器消费，
主菜单/暂停时没有 tick，因此脚本应只在 `active=true` 时显示 HUD。

| 字段 | 类型 | 说明 |
|---|---|---|
| `active` | bool | 当前是否有玩家/世界（主菜单为 false） |
| `day` / `time` / `speed` | string | 天数、时间、速度文本（如 `暂停`） |
| `playerValid` | bool | 生命/体力数据是否有效 |
| `health` / `healthMax` | number | 当前/最大生命 |
| `stamina` / `staminaMax` | number | 当前/最大体力 |
| `selectedSlot` | number | 选中快捷栏下标（0~4） |
| `slots` | array | 5 项 `{label, count}`（Lua 1-based，Python 0-based list） |
| `notice` / `noticeTone` | string | 提示文本与 `info`/`ok`/`fail` |
| `noticeRemaining` / `noticeDuration` | number | 提示剩余/总时长（秒） |

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
| `Inventory.GetState()` | table | `open, playerValid, weight, capacity, bagStacks, lootStacks, status, statusTone, bag, equipment, loot, recipes, crafting` |
| `Inventory.Action(action)` | bool | 提交 `{type, from, fromIndex, fromEntityId, to, toIndex, count?}`；返回动作是否已交给 C++ |
| `Inventory.Close()` | bool | 关闭背包（打开时返回 true） |
| `Inventory.PlayClick()` | - | 播放背包 UI 点击音效 |

`bag` / `equipment` / `loot` 分别为 25 / 2 / 6 项槽位数组，每项字段：
`{filled, label, count, usable, equippable, entityId, condition, damaged, repairAvailable}`；
`condition` 为 0~1 的耐久/状态，内置 UI 在低于 100% 时于名称后显示百分比；
`damaged` 表示耐久未满的武器，`repairAvailable` 表示当前背包内有足够维修材料。
Lua 数组从 1 开始，Python 从 0 开始；槽位索引 `fromIndex/toIndex` 始终是 **0-based**。

`recipes` 是全部已加载配方的快照（内置背包 UI 每页显示 4 条，可用 `‹` / `›` 翻页）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` / `name` | string | 配方 id / 显示名 |
| `outputDefId` / `outputLabel` | string | 产物物品 id / 显示名 |
| `outputCount` | number | 每次制作产出数量 |
| `known` | bool | 是否已学会；false 时 UI 显示“未学会”，不能制作 |
| `canCraft` | bool | 已学会且材料、工具、背包空间与制作站范围都满足 |
| `estimatedSeconds` | number | 按当前工具状态估算的实际耗时（秒）；缺工具时为 0 |
| `ingredients` | array | 消耗材料 `{label, required, have}`（Lua 1-based / Python 0-based list） |
| `tools` | array | 工具 `{label, required, have}`；只要求背包内拥有，不消耗但会磨损 |
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
| `transfer` | `from/fromIndex/to/toIndex` | 背包内换位、装备/卸下、丢地面、地面拾取 |
| `use` | `from/fromIndex` | 使用背包物品 |
| `equip` | `from/fromIndex` | 按物品类型自动选择主手/躯干 |
| `unequip` | `from/fromIndex` | 卸下装备 |
| `drop` | `from/fromIndex` | 丢弃背包/装备物品 |
| `pickup` | `fromEntityId` | 拾取附近地面物品 |
| `craft` | `fromIndex`, `count?` | 开始制作 `recipes[fromIndex]`（0-based）；空闲时立即开始，已有任务时追加到制作队列；`count` 为请求件数（1~99，缺省 1）。追加队列只校验配方已学会、制作站范围与队列上限，材料/工具/空间在真正开始该项时再校验；工具效率决定每件实际时长 |
| `cancel_craft` | 无 | 取消当前制作/剩余批次并清空整个制作队列；已完成件已扣除材料，当前计时件未扣材料，因此无需返还 |
| `queue_up` / `queue_down` | `fromIndex` | 将队列第 `fromIndex` 项（0-based）上移/下移一位；越界或已到队首/队尾时拒绝并提示，不改变制作与材料 |
| `queue_remove` | `fromIndex` | 移除队列第 `fromIndex` 项（0-based）；只移除后续队列项，不取消当前制作、不返还材料 |
| `queue_count` | `fromIndex`, `count` | 把队列第 `fromIndex` 项的请求件数设为 `count`（1~99）；越界/队列项不存在时拒绝，不校验材料，真正开始时仍按可用材料/工具/空间截断 |
| `repair` | `from/fromIndex` | 维修背包中耐久未满的工具；消耗 `inventory.repairKit` 指定材料，恢复 `inventory.repairAmount` 耐久 |

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
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "padded_jacket",
      "name": "加固夹克",
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
时长、`crafting.toolEfficiency` 为本次效率。`station` 为 recipes-v3 可选制作站 id：世界对象
用逻辑名 `object.<station>` 标记（内置 `workbench` → `object.workbench`、`campfire` →
`object.campfire`）；玩家需在 `crafting.stationRange`（默认 2.5 格）内才能开始/完成该配方，
离开范围后计时完成会失败且不消耗材料。显示名由 `config/game.json` 的
`crafting.stationLabels` 映射，缺省回退为 station id；未知 station 不会被拒绝，便于 mod
先用自定义对象扩展，示例：
`{"id":"campfire_beans","station":"campfire","ingredients":[{"item":"canned_beans","count":1}],"output":{"item":"cooked_beans","count":1},"time":2.0}`。
`recipes-v1` 仍兼容（无 `tools`/`time`/`station`）；要使用工具、自定义时长/磨损
或制作站需把 `format` 升级为 `recipes-v3`（v2 仍兼容 tools/time）。

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

工具维修（内置 `repair` 动作）：只允许耐久在 `(0,1)` 的不可堆叠武器，消耗配置
`inventory.repairKit`（默认 `repair_kit`）指定数量（`inventory.repairKitCount`，默认 1），
恢复 `inventory.repairAmount`（默认 0.5）耐久并钳制到 1.0；缺少维修包或工具已损坏时不修改容器。
槽位快照的 `damaged` / `repairAvailable` 可直接驱动自定义 UI 的维修按钮；内置 UI 在右键菜单
显示“维修”，没有维修包时提示“维修（缺维修包）”。维修结果通过状态行/日志反馈前后耐久。
维修只在背包内操作，不改变正在进行的制作任务；成功维修后工具会立即按新耐久参与后续制作效率。
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

`path` 必须位于当前 mod 根目录内，拒绝绝对路径和 `..`，第一版只允许 `.rml`。
RML 里的 RCSS/图片相对路径由 RmlUi `JoinPath` 解析。

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
| `OnSaveBegin` / `OnSaveEnd` | 后台存档提交/完成 | - |
| `OnUiReady` | UI Host 初始化完成，可以加载脚本 UI | - |
| `OnUiEvent` | 脚本 UI 文档内发生鼠标/键盘/拖拽/表单事件 | `modId, document, element, type, value, key, button, mouseX, mouseY` |
| `OnMenuChanged` | 主菜单/暂停菜单打开、关闭或切换页面模式 | `open, pauseMode, canContinue` |
| `OnInventoryChanged` | 背包打开/关闭或背包数据刷新 | `open` |

注意事项：

- 世界事件是**排队派发**：世界系统产生的事件在当前 tick 末统一派发；脚本回调里再
  产生事件（如 `Zombies.Spawn`）会留到**下一 tick**，天然不会自己触发自己形成风暴。
- `OnUiEvent` / `OnMenuChanged` / `OnInventoryChanged` 是例外：它们同步派发（不进入下一
  tick 队列），因此主菜单/暂停等 `timeScale=0` 状态下也能响应 UI 点击、菜单切换和背包
  刷新；回调里可以安全地 Load/Close 文档。
- `OnUiEvent` 的拖拽类型为 `dragstart / dragover / dragout / dragdrop / dragend`；
  `mouseX/mouseY` 为 RmlUi 文档坐标。
- `OnUiReady` 是加载脚本 UI 的推荐时机；不要在脚本入口顶层直接加载界面。
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

## 7. 官方示例

| 示例 | 演示点 | 进游戏后看什么 |
|---|---|---|
| `mods/example_hello` | 最小结构：启动日志、等玩家生成后发开局物品、`OnTick` 周期查询玩家/僵尸 | 进游戏约 1 秒后日志 `starter items: bandage x2, axe x1`；每约 10 秒一行 `survivor at ...` |
| `mods/example_reactive` | 事件反应：击杀奖励（5 杀送绷带）、拾取日志、枪声日志、死亡提示、`SetTimer` | 按 `F6` 演示击杀看 `zombie N down (kills=...)`；按 `F7` 看 `gunshot at ...`；按 `F` 拾取看 `picked up ...` |
| `mods/example_ui` | 脚本 UI：懒加载 `ui/panel.rml`、文本输入/按钮事件、关闭文档、热重载重建 | 右下角 Script UI 面板；输入框按回车/失焦看 `input:`，点刷新/关闭 |
| `mods/core_ui` | 内置 HUD、主菜单/暂停菜单与背包控制器：Lua/Python 共用 `ui/hud.rml`、`ui/menu.rml`、`ui/inventory.rml`，读取 `Hud.GetState()`/`Menu.GetState()`/`Inventory.GetState()`/`Save.List()` | 进入世界后 HUD 由脚本加载；菜单/背包由脚本打开，支持设置/改键、拖拽、右键操作（含工具维修）、定时制作/批量数量/跨配方队列/队列选中调序、数量调整与移除/进度/取消、配方每页 4 条翻页、分页读取/删除存档与暂停保存；禁用脚本后自动回退 native UI |

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
- `OnZombieDead.killer` 恒 0；`OnPlayerMove` 仅点击移动到达；`OnSaveBegin/End` 已接入后台存档。
- `Zombies.Count` 只统计实体僵尸（离屏虚拟僵尸不计）。
- 死循环防护边界见 §6.4。
- Python 后端（M8）与 Lua 共用同一套 Host API / 事件表（差异见 §7.1）；默认构建仍只带 Lua。
