# 聚落与 NPC 数据格式（settlements-v1）

> 适用版本：M23-F4-T1~T8（2026-11-02）。实现位于 `src/game/Settlement.*`、`src/game/SettlementNpc.*`、`src/game/Dialogue.*`；契约测试为 `tests/test_settlements.cpp`、`tests/test_settlement_integration.cpp`、`tests/test_settlement_defense.cpp`、`tests/test_settlement_network.cpp` 与 `tools/tests/test_settlement_data.py`。

本文是聚落数据、运行时行为、存档/联机边界和 mod 扩展流程的完整参考。玩家 mod 的通用内容合并规则见 [modding.md](modding.md) 的 3.6 节。

## 1. 文件与加载

基础数据：`assets/settlements.json`。

```json
{
  "format": "settlements-v1",
  "defs": [
    {
      "id": "village_haven",
      "name": "村庄营地",
      "centerX": 75.0,
      "centerY": 26.5,
      "safeRadius": 8.0,
      "npcPoints": [
        { "id": "merchant_mara", "kind": "merchant", "x": 75.5, "y": 26.5, "wanderRadius": 1.5 },
        { "id": "guard_cole", "kind": "guard", "x": 76.5, "y": 25.5, "wanderRadius": 2.0 },
        { "id": "requester_ivy", "kind": "requester", "x": 73.5, "y": 27.5, "wanderRadius": 1.5 }
      ],
      "merchantStock": [4, 2, 1, 2, 1],
      "restockDays": 1,
      "missionPool": [
        "settlement_help",
        "settlement_escort",
        "settlement_clear",
        "settlement_supply"
      ],
      "defense": {
        "enabled": true,
        "minDay": 10,
        "nightStartHour": 22,
        "nightEndHour": 5,
        "activationRadius": 24.0,
        "spawnRadius": 13.0,
        "baseRaiders": 2,
        "raidersPerNight": 1,
        "maxRaiders": 4,
        "guardBehavior": "defend",
        "guardHealth": 120.0,
        "guardDamage": 18.0,
        "guardRange": 1.6,
        "guardAttackSeconds": 0.9,
        "raiderHealth": 50.0,
        "raiderDamage": 5.0,
        "raiderRange": 1.35,
        "raiderAttackSeconds": 1.5,
        "raiderSpeed": 2.1
      }
    }
  ]
}
```

`config/game.json` 的 `settlements.enabled` 控制是否启用，`settlements.file` 指向基础文件。X3-T2 起单机且无脚本动态内容时，修改基础文件或 `mods/<id>/content/settlements.json` 会随 `content/` 事务热重载；联机/主机、存在脚本动态内容或整个 mod 目录增删时仍需重启。mod 只追加同格式定义，不提供脚本运行时注册。

合并规则：

- 根节点必须是对象，`format` 必须精确为 `settlements-v1`，`defs` 必须是非空数组；否则整份文件失败。
- 聚落 `id` 与基础或更早 mod 重复时跳过该条；单条字段非法时跳过该条。
- 一个 mod 文件全部条目都无效时，该文件视为失败，不改变已有定义。
- 基础与 mod 聚落都会进入内容指纹 v13；联机客户端/服务器必须加载相同定义。
- 解析器忽略未知字段，未知字段不会进入指纹，也不能被运行时识别。

## 2. 聚落字段

| 字段 | 必填 | 类型/范围 | 说明 |
|---|---|---|---|
| `id` | 是 | 字符串，1~64 字节，`[a-z0-9_.]` | 全局稳定标识；重复时跳过。 |
| `name` | 是 | 字符串，1~128 字节 | 对话说话人后缀与夜袭提示显示名。 |
| `centerX` / `centerY` | 是 | 浮点，-1000000~1000000 | 聚落中心/安全区圆心，也是躲避与夜袭目标中心。 |
| `safeRadius` | 是 | 浮点，1~128 格 | 安全圆半径；NPC 点必须落在圆内。不是物理禁区，不阻挡僵尸。 |
| `npcPoints` | 是 | 数组，1~16 项 | 必须至少包含 merchant、guard、requester 各 1 个。 |
| `merchantStock` | 是 | 5 个整数，每项 0~99 | 商人 5 档报价的初始/读档库存；运行时按报价上限夹紧。 |
| `restockDays` | 是 | 整数，1~30 | 补货周期；主聚落会覆盖 `trading.restockDays`。 |
| `missionPool` | 是 | 数组，1~32 项 | 每项为 1~64 字节唯一任务 id；求助者可接取列表，解析时不校验任务是否存在。 |
| `defense` | 否 | 对象 | 夜袭配置；省略表示关闭。 |

`safeRadius` 只参与 NPC 点校验、躲避中心、威胁半径和夜袭生成半径，不改变地图碰撞、僵尸生成或玩家安全判定。

## 3. NPC 点位

| 字段 | 必填 | 类型/范围 | 说明 |
|---|---|---|---|
| `id` | 是 | 字符串，1~64 字节，`[a-z0-9_.]` | 聚落内唯一；当前仅用于数据校验/指纹，不是实体 StableId。 |
| `kind` | 是 | `merchant` / `guard` / `requester` | NPC 类型。 |
| `x` / `y` | 是 | 浮点，-1000000~1000000 | 世界格浮点坐标；必须满足 `(x-centerX)^2+(y-centerY)^2 <= safeRadius^2`。 |
| `wanderRadius` | 是 | 浮点，0~16 格 | 守卫巡逻半径；商人/求助者当前只驻点，该值仅记录。 |

生成语义：

- 实际生成点取 `floor(x)+0.5, floor(y)+0.5`，不是精确浮点坐标。
- 单机只检查目标格是否阻挡；服务器还要求 chunk 已加载、地面存在、格未阻挡且未被其他会话占用。
- 生成失败会在后续帧重试；数据点必须可达，否则对应 NPC 可能一直不出现。
- 每类 NPC 当前只使用**第一个**匹配点位。允许写多个同类型点，但额外的 merchant/guard/requester 不会生成，只进入指纹。
- 商人不新建独立 NPC：单机/服务器找到现有 `MerchantStock` 实体（必要时在商人点附近创建），再挂 `SettlementNpc` 日程；守卫由 `guardHealth` 生成独立生命；求助者无战斗生命。
- NPC 无普通 `AiState`，由 `SettlementNpcSystem` 统一驱动寻路；守卫/求助者从世界快照排除，读档后重建。

## 4. 交易与补货

`merchantStock` 固定对应现有 5 档报价：

| 下标 | 报价 | 单价/消耗 | 运行时上限 |
|---|---|---|---|
| 0 | 绷带 ×2 | 木材 2 | 4 |
| 1 | 抗生素 ×1 | 木材 3 | 2 |
| 2 | 图纸：烤豆子 | 木材 3 | 1 |
| 3 | 9mm 弹药 ×12 | 木材 2 | 2 |
| 4 | 手枪 ×1 | 木材 8 | 1 |

规则：

- 配置值允许 0~99，但加载后按上表 `maxStock` 夹紧；不能靠配置绕过报价上限。
- `merchantStock` 是初始库存/读档种子；补货时每档重置为 `maxStock`，不是回到配置里的初始值。
- `restockDays` 取主聚落的值，覆盖 `trading.restockDays`。
- 交易仍是固定 5 档物物交换，不支持每个聚落追加自定义报价；新增商品需改 C++ 报价表。
- 聚落失守或夜袭 active 时，服务器拒绝 `TradeOffer`，客户端关闭/拦截交易面板。

## 5. `missionPool` 与求助者对话

求助者使用代码内置对话树，`missionPool` 决定“接一件委托/看看我现在的委托/现在没有委托”三个分支是否可见。

- 池内任意任务处于 Active 时，视为已有进行中委托，对话只显示“看看我现在的委托”，不再提供接取。
- 没有 Active 时，按 `missionPool` 顺序寻找第一个 `MissionCanActivate` 为真的任务；接取时也是这个顺序。
- 接取后走现有任务系统的目标/时限/失败/放弃/奖励规则；聚落对话不负责交任务，完成仍由目标推进触发。
- 已完成 once 任务不会重接；失败/放弃按任务的 `retryDays` 冷却。
- `missionPool` 中不存在或不可激活的任务在运行时被忽略；建议在 mod 测试中校验引用。
- `AcceptSettlementMission=39` 只携带求助者 StableId，不携带任务 id；服务器重新计算可接任务，客户端不能指定任意任务。
- 推荐聚落任务使用 `source: "npc"`、`autoActivate: false`、`kind: "once"`，并设置 `timeLimitDays`/`retryDays`；否则可能与自动接取/每日刷新冲突。

内置对话树是固定文本/节点/条件；`settlement.name` 只进入说话人后缀。当前不提供对话 JSON，mod 不能新增节点、改写选项或增加新的对话结果类型，这些需要扩展 C++ `game::Dialogue`。

## 6. `defense` 夜袭配置

`defense` 省略时关闭。对象存在时 `enabled` 默认 `true`；显式写 `"enabled": false` 可保留配置但关闭夜袭。

| 字段 | 默认 | 范围 | 说明 |
|---|---:|---|---|
| `enabled` | true（有 `defense` 时） | bool | 关闭后不触发夜袭，也不写 `STLS`。 |
| `minDay` | 1 | 1~10000 | 最早可触发生存日；基础村庄为 10。 |
| `nightStartHour` | 22 | 0~23 | 夜袭窗口开始小时。 |
| `nightEndHour` | 5 | 0~23 | 夜袭窗口结束小时；必须与开始不同，支持跨午夜。 |
| `activationRadius` | 24 | 0~128 格 | 玩家进入该半径才触发。 |
| `spawnRadius` | 13 | 0~64 格 | 感染者生成在 `safeRadius + spawnRadius` 外环。 |
| `baseRaiders` | 2 | 0~12 | 首夜基础数量。 |
| `raidersPerNight` | 1 | 0~12 | 随夜晚递增数量。 |
| `maxRaiders` | 4 | 0~24 且 ≥ `baseRaiders` | 单夜数量上限。 |
| `guardBehavior` | `defend` | `defend` / `retreat` | 守卫迎战或与平民一起撤离。 |
| `guardHealth` | 120 | 1~1000 | 守卫最大生命。 |
| `guardDamage` | 14 | 0~100 | 守卫每次攻击伤害。 |
| `guardRange` | 1.5 | 0.5~8 格 | 守卫近战距离。 |
| `guardAttackSeconds` | 1.0 | 0.2~10 秒 | 守卫攻击间隔。 |
| `raiderHealth` | 50 | 1~1000 | 夜袭感染者生命。 |
| `raiderDamage` | 5 | 0~100 | 感染者攻击伤害。 |
| `raiderRange` | 1.2 | 0.5~8 格 | 感染者近战距离。 |
| `raiderAttackSeconds` | 1.5 | 0.2~10 秒 | 感染者攻击间隔。 |
| `raiderSpeed` | 2.2 | 0.5~8 格/秒 | 感染者移动速度。 |

基础村庄显式覆盖了守卫伤害/范围/间隔、感染者范围/速度和 `minDay`，所以以上表默认值只用于省略字段的 mod 聚落。

### 触发与结束

- 时间段按整点小时判断，支持 `22 → 05` 跨午夜；跨午夜时 `lastNightKey` 使用夜袭开始日（午夜后仍是前一天）。
- 触发要求：开启防御、未失守、`day >= minDay`、处于夜间、当晚未触发、玩家/焦点在 `activationRadius` 内、至少 1 名存活守卫。
- 感染者数量：`baseRaiders + max(0, nightKey - minDay) * raidersPerNight`，再夹紧到 `maxRaiders`。
- 生成使用 `nightKey` 与序号确定性取角，落在 `safeRadius + spawnRadius` 外环，并在 6 格内寻找可走格。
- 进行中守卫归零 → 聚落失守；黎明、玩家离开 `activationRadius × 1.75`、或感染者清空 → 夜袭结束。
- `defend` 时守卫主动追击最近感染者并近战；`retreat` 时守卫与平民撤到中心，感染者向中心推进，直到黎明、玩家离开或感染者清空。
- 进行中的感染者/active 不跨存档恢复；`lastNightKey` 保留，避免同一晚重新触发。守卫位置/巡逻目标也不持久化。

## 7. 日程、威胁与战斗

- 守卫：08:00–18:00 巡逻，18:00–22:00 站岗，22:00–08:00 休息。
- 商人/求助者：08:00–20:00 驻点，20:00–08:00 休息。
- 任意存活僵尸或夜袭感染者进入聚落中心 `safeRadius + 4` 格内时，NPC 切到躲避并前往中心最近可走格；威胁消失后保留 4 秒再恢复日程。
- 夜袭是否进行会覆盖日程：`defend` 守卫进入迎战，其余 NPC 躲避。
- NPC 距玩家焦点超过 36 格时冻结并停止寻路，回到范围内恢复。
- 商人/求助者不参与战斗；守卫只在聚落夜袭中对夜袭感染者造成伤害，不走普通僵尸击杀/任务进度。
- 守卫死亡后不会由 `EnsureSettlementNpcs` 补生成；失守状态永久暂停该聚落的交易/委托，直到管理员改档或后续版本提供恢复机制。

## 8. 存档与联机

- `SettlementState`（`STLS` v1，固定 StableId `0x4D32334634535441`）只保存 `defense.enabled` 的聚落：`id/fallen/guardHealth/lastNightKey`。
- 单机 F12 与服务器 `SaveWorld` 在实体快照前写入；读档恢复实体后 `ApplyState`，守卫按保存生命重建，失守不重建。
- 商人库存由世界实体 `MerchantStock` 保存；玩家任务进度由 `MissionLog` 保存。
- `ZLR1` 复制 `SettlementNpc`（聚落/类型/activity/home），客户端只读显示，不本地生成或调度 NPC。
- `PlayerState` v16 追加 owner-only `PlayerSettlementProgress{id,fallen,raidActive,guardHealth,guardHealthMax}` 与聚落提示；用于 HUD/对话/交易拦截。
- `AcceptSettlementMission=39` 和 `TradeOffer=21` 都由服务器校验距离、实体、类型、失守/夜袭和任务可用性；客户端不本地扣物、发奖或接取。
- 聚落字段（含 `defense`）进入内容指纹 v13；联机双方定义不一致会在握手阶段拒绝。

## 9. Mod 扩展流程

最小目录：

```text
mods/my_survivor_camp/
├─ mod.json
└─ content/
   ├─ settlements.json
   └─ missions.json
```

聚落示例（配合 `missions.json` 中的 `my_camp_help`）：

```json
{
  "format": "settlements-v1",
  "defs": [
    {
      "id": "my_survivor_camp",
      "name": "河边营地",
      "centerX": 82.0,
      "centerY": 30.0,
      "safeRadius": 6.0,
      "npcPoints": [
        { "id": "my_merchant", "kind": "merchant", "x": 82.5, "y": 30.5, "wanderRadius": 1.0 },
        { "id": "my_guard", "kind": "guard", "x": 83.5, "y": 29.5, "wanderRadius": 1.5 },
        { "id": "my_requester", "kind": "requester", "x": 81.5, "y": 31.5, "wanderRadius": 1.0 }
      ],
      "merchantStock": [2, 1, 0, 1, 0],
      "restockDays": 2,
      "missionPool": ["my_camp_help"],
      "defense": {
        "minDay": 12,
        "baseRaiders": 1,
        "maxRaiders": 3,
        "guardBehavior": "retreat"
      }
    }
  ]
}
```

任务侧只需要一个可手动接取的任务，例如 `source: "npc"`、`autoActivate: false`、`kind: "once"`、`task: "help"`；字段见 modding.md 的任务数据章节。任务目标、奖励、时限和失败重试沿用现有任务系统。

检查清单：

1. 所有 `npcPoints` 在安全圆内，坐标处有地面且可达。
2. `missionPool` 引用真实存在、可激活的任务；任务不要自动接取。
3. `defense` 数值在范围内，`maxRaiders >= baseRaiders`；不打算夜袭时省略 `defense`。
4. 客户端与服务器安装同一 mod；单机 content 热重载可用，联机/主机修改后仍需重启。
5. 用 `tests/test_settlements.cpp`/Python 数据测试的同类校验检查 id、点位和任务引用。

## 10. 当前边界

- 当前玩法只完整接线**第一个** `defs[0]`（主聚落）：商人、守卫、求助者生成和交易入口都以它为准。追加的后续聚落会加载并进入指纹；若开启 `defense` 还会出现在防御状态读取中，但不会自动生成 NPC/商人，也没有独立入口。要替换主聚落需在启动配置中改 `settlements.file`，不能靠 mod 覆盖内置 id。
- 只支持 merchant/guard/requester 三类 NPC 和固定对话树；新增 NPC 类型、对话节点、条件/结果需要改 C++。
- 交易报价全局固定 5 档，`merchantStock` 只能调初始数量，不能按聚落改商品或价格。
- 守卫行为只有 `defend`/`retreat`；新增策略需要扩展 `SettlementGuardBehavior` 和 `SettlementNpcSystem`。
- 同一聚落同时只允许一条进行中的池内委托；多任务并行、委托交付对话和自定义任务 UI 未提供。
- `safeRadius` 不是物理安全区，不会阻止僵尸或玩家进入。
- NPC 位置、巡逻目标、当前活动不存档；`STLS` 只保存失守、守卫生命和当晚标记。
- 夜袭 active 不跨存档恢复；读档只保留当晚已触发标记。
- 追加聚落不会自动创建地图房间、墙体、床或容器；地图/POI 布局仍由地图生成器或地图 mod 保证。
