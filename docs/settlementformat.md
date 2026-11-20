# 聚落与 NPC 数据格式（settlements-v1）

> 适用版本：M23-F4-T1~T8（2026-11-02）、M33-F1-T1/T2/T3/T4/T5/T6/T7、M33-F3-T7（2026-11-14）。实现位于 `src/game/Settlement.*`、`src/game/SettlementNpc.*`、`src/game/SettlementReputation.*`、`src/game/Dialogue.*`；契约测试为 `tests/test_settlements.cpp`、`tests/test_settlement_integration.cpp`、`tests/test_settlement_npc.cpp`、`tests/test_settlement_defense.cpp`、`tests/test_settlement_network.cpp`、`tests/test_entity_save.cpp`、`tests/test_player_state.cpp` 与 `tools/tests/test_settlement_data.py`。

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
      "regionId": "village_core",
      "services": ["trade", "missions", "defense", "recruit"],
      "startingReputation": 0,
      "dialogueProfile": "village_haven",
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
- 基础与 mod 聚落都会进入内容指纹 v15（M33-F3-T5 起整体为 v19，M33-F4-T1 起为 v20）；联机客户端/服务器必须加载相同定义。
- 解析器忽略未知字段，未知字段不会进入指纹，也不能被运行时识别。

## 2. 聚落字段

| 字段 | 必填 | 类型/范围 | 说明 |
|---|---|---|---|
| `id` | 是 | 字符串，1~64 字节，`[a-z0-9_.]` | 全局稳定标识；重复时跳过。 |
| `name` | 是 | 字符串，1~128 字节 | 对话说话人后缀与夜袭提示显示名。 |
| `regionId` | 否 | 字符串，1~64 字节，`[a-z0-9_.]` | 关联的战略区域 id；省略为空，不校验区域是否存在。 |
| `services` | 否 | 数组，1~4 项 | 可选 `trade/missions/defense/recruit`，不可重复；省略为 `trade+missions+defense`。 |
| `startingReputation` | 否 | 整数，-100~100 | 玩家初始个人声望；省略为 0。 |
| `dialogueProfile` | 否 | 字符串，1~64 字节，`[a-z0-9_.]` | 内置对话口吻键；`village_haven/north_fields/south_farmland/river_camp` 会追加不同开场，未知/空值回退通用文本。 |
| `centerX` / `centerY` | 是 | 浮点，-1000000~1000000 | 聚落中心/安全区圆心，也是躲避与夜袭目标中心。 |
| `safeRadius` | 是 | 浮点，1~128 格 | 安全圆半径；NPC 点必须落在圆内。不是物理禁区，不阻挡僵尸。 |
| `npcPoints` | 是 | 数组，1~16 项 | 必须至少 1 个 merchant，且至少 1 个 guard 或 requester；无防御小聚落可只有 merchant+requester。 |
| `merchantStock` | 是 | 5 个整数，每项 0~99 | 商人 5 档报价的初始/读档库存；运行时按报价上限夹紧。 |
| `restockDays` | 是 | 整数，1~30 | 该聚落商人的补货周期；M33-F1-T2 起按商人所属聚落读取，不再覆盖全局 `trading.restockDays`。 |
| `missionPool` | 是 | 数组，1~32 项 | 每项为 1~64 字节唯一任务 id；求助者可接取列表，解析时不校验任务是否存在。 |
| `defense` | 否 | 对象 | 夜袭配置；省略表示关闭。 |

`safeRadius` 只参与 NPC 点校验、躲避中心、威胁半径和夜袭生成半径，不改变地图碰撞、僵尸生成或玩家安全判定。

### 2.1 声望配置（M33-F1-T1）

`config/game.json` 的 `settlements.reputation` 定义全局声望规则；数据文件只提供 `startingReputation`，不在每个聚落重复阈值：

```json
{
  "enabled": true,
  "tiers": [
    { "id": "hostile", "name": "敌对", "minScore": -100, "maxScore": -40 },
    { "id": "wary", "name": "警惕", "minScore": -39, "maxScore": -1 },
    { "id": "neutral", "name": "中立", "minScore": 0, "maxScore": 19 },
    { "id": "friendly", "name": "友善", "minScore": 20, "maxScore": 59 },
    { "id": "trusted", "name": "信任", "minScore": 60, "maxScore": 100 }
  ],
  "sources": {
    "missionComplete": 10,
    "defenseComplete": 15,
    "trade": 1,
    "missionAbandon": -10,
    "attackNpc": -25,
    "theft": -25
  },
  "priceMultipliers": {
    "hostile": { "buy": 1.5, "sell": 0.5 },
    "wary": { "buy": 1.25, "sell": 0.75 },
    "neutral": { "buy": 1.0, "sell": 1.0 },
    "friendly": { "buy": 0.9, "sell": 1.1 },
    "trusted": { "buy": 0.8, "sell": 1.25 }
  },
  "dailyTradeGainLimit": 1
}
```

- `tiers` 必须恰好 5 项，id 顺序固定为 `hostile/wary/neutral/friendly/trusted`；首段从 -100 开始、末段到 100 结束，相邻区间必须连续。
- `sources` 每项为 -100~100；`dailyTradeGainLimit` 为 0~100；`priceMultipliers` 只接受上述等级 id，`buy/sell` 为 0.1~5.0。
- `enabled=false` 时数据仍校验，但后续声望效果不生效。
- F1-T1 只建立配置与数据契约；F1-T2 完成多聚落运行时；F1-T3 完成交互路由/HUD；F1-T4 完成玩家声望写入、事件接线、存档/重连与 `PlayerState` 摘要；F1-T5 完成价格倍率、任务最低声望、五档对话态度与 `services` 后果；F1-T6 新增第 4 聚落、独立任务池/口吻与安全区生成重试；F1-T7 完成跨聚落双客户端/服务器重启回归与 F1 交付。

### 2.2 第 4 个聚落 `river_camp`（M33-F1-T6）

```json
{
  "id": "river_camp",
  "name": "河畔营地",
  "regionId": "riverlands",
  "services": ["trade", "missions"],
  "startingReputation": 0,
  "dialogueProfile": "river_camp",
  "centerX": 216.0,
  "centerY": 120.0,
  "safeRadius": 4.5,
  "npcPoints": [
    { "id": "river_trader", "kind": "merchant", "x": 217.0, "y": 118.0, "wanderRadius": 0.5 },
    { "id": "river_requester", "kind": "requester", "x": 215.0, "y": 122.0, "wanderRadius": 0.5 }
  ],
  "merchantStock": [2, 2, 1, 2, 1],
  "restockDays": 1,
  "defense": { "enabled": false, "minDay": 9999 },
  "missionPool": ["river_fish_supply"]
}
```

- 位置复用 M31 东桥头可走格，不新增地图生成器/建筑；与 `riverlands` 战略锚点 `214,120` 相邻。
- 服务只有交易/委托，`defense.enabled=false`，因此不生成守卫、不触发自动夜袭；HUD 显示“防御 不设防”。
- 该定义证明 `npcPoints` 不再强制三类 NPC 齐全：商人 + 求助者即可，任务池仍可独立工作。
- 四聚落服务组合至少两种（村庄/农场全服务；诊所/河畔无招募；河畔另无防御）。

## 3. NPC 点位

| 字段 | 必填 | 类型/范围 | 说明 |
|---|---|---|---|
| `id` | 是 | 字符串，1~64 字节，`[a-z0-9_.]` | 聚落内唯一；当前仅用于数据校验/指纹，不是实体 StableId。 |
| `kind` | 是 | `merchant` / `guard` / `requester` | NPC 类型。 |
| `x` / `y` | 是 | 浮点，-1000000~1000000 | 世界格浮点坐标；必须满足 `(x-centerX)^2+(y-centerY)^2 <= safeRadius^2`。 |
| `wanderRadius` | 是 | 浮点，0~16 格 | 守卫巡逻半径；商人/求助者当前只驻点，该值仅记录。 |

生成语义：

- M33-F1-T2 起单机/服务器共用 `SettlementNpcSystem::EnsurePopulation`，按 `settlementId` 和点位生成：每个聚落 1 个商人，守卫/求助者数量取决于 `npcPoints`；单机同帧、服务器 tick 内统一处理，不再只处理第一个聚落。
- 实际生成点取 `floor(x)+0.5, floor(y)+0.5`，不是精确浮点坐标；目标格不可走时在 6 格内寻找最近可走格。
- M33-F1-T6 起，最近可走格必须仍在聚落安全圆内，否则本帧跳过并在后续帧重试；避免快速传送/chunk 未加载时把 NPC 生成到安全区外。
- 单机检查目标格是否阻挡；服务器还要求 chunk 已加载、地面存在、格未阻挡且未被其他会话占用。
- 生成失败会在后续帧重试；数据点必须可达，否则对应 NPC 可能一直不出现。
- 每个聚落只生成**一个商人**（取第一个 merchant 点）；守卫/求助者按所有匹配点位逐点生成，因此同一聚落可有多名守卫/求助者。点位 `id` 仍只是数据校验/指纹标识，不是实体 StableId。
- 商人仍使用 `MerchantStock`：每个聚落独立创建商人实体并挂 `SettlementNpc{settlementId, Merchant}`；新商人库存取该聚落 `merchantStock`。旧存档中未挂 `SettlementNpc` 的商人会迁移到最近的、尚无商人的聚落，保留原库存/restockDay，不重置。
- 守卫由该聚落 `guardHealth` 生成独立生命；求助者无战斗生命。
- 人口标记按聚落/角色记录：同一聚落不会重复生成，守卫死亡、平民失守或商人缺失都不会自动补生；战略 `RestoreSettlement` 会清除守卫/平民人口标记并允许重建。
- NPC 无普通 `AiState`，由 `SettlementNpcSystem` 统一驱动寻路；守卫/求助者从世界快照排除，读档后按 `SettlementState` 重建。

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
- `restockDays` 按商人所属聚落读取；没有 `SettlementNpc` 绑定的旧/后备商人使用全局 `trading.restockDays`。
- 交易仍是固定 5 档物物交换，不支持每个聚落追加自定义报价；新增商品需改 C++ 报价表。
- 聚落失守或夜袭 active 时，服务器拒绝 `TradeOffer`，客户端关闭/拦截交易面板。
- F1-T5 起买入成本按 `priceMultipliers.<tier>.buy` 调整并四舍五入到整数（最少 1）；面板显示实际成本与等级价。卖出倍率提供 `SettlementSellPrice` 纯函数，但当前交易 UI 尚未提供卖出报价。
- `services` 未包含 `trade` 时商人不提供交易入口，服务器也不接受 `TradeOffer`；Hostile 仍保留配置入口但拒绝交易并提示原因。

### 4.1 交互路由与 UI（M33-F1-T3）

- 对话、交易、任务接取和守卫提示都从 NPC 的 `SettlementNpc.settlementId` 解析聚落；没有聚落字段的命令由服务器用 NPC StableId 反查，不新增协议。
- 交易面板保存打开对话的商人 StableId；购买、库存读取、范围校验和失守/夜袭关闭都使用该商人，不再回退到“最近商人”。
- 服务器 `TradeOffer` 要求目标为 `merchant`、聚落定义存在、玩家在交易范围内且聚落未失守/夜袭；错误 NPC 类型直接拒绝。
- HUD 显示当前聚落名、真实声望等级/分数、服务和防御状态；动态分数由 F1-T4 的玩家组件或网络摘要提供，缺省回退 `startingReputation`。
- 世界地图显示已发现聚落的等级菱形标记和侧栏声望摘要（名称/等级/分数）；地图标签只读。
- 价格倍率、任务声望门槛、对话态度和 `services` 硬限制由 F1-T5 接线；详见 4.3。

### 4.2 玩家声望数据与事件（M33-F1-T4）

- 玩家实体挂 `SettlementReputation`：`settlements[]` 按 `settlementId` 保存分数（-100~100，空/缺项回退 `startingReputation`），`appliedKeys[]` 保存有界幂等键。组件随单机实体快照保存为 `SRPT v1`；旧档无组件按初始值处理。
- `AwardSettlementReputation` 统一映射来源：任务完成/事件结果使用 `missionComplete`，放弃/超时使用 `missionAbandon`，交易使用 `trade`，守城使用 `defenseComplete`；`attackNpc/theft` 入口已预留。所有来源 clamp 到 -100~100。
- 任务完成/失败/放弃的键形如 `mission:<settlementId>:<missionId>:<day>:<complete|fail|abandon>`；守城键为 `defense:<settlementId>:<regionId>:<attack>:<resolved>:<defend|reclaim>`。重复键不再加声望。
- 交易不要求客户端提供事件 id；按 `trade:<settlementId>:<day>:<n>` 记录，并受 `dailyTradeGainLimit` 限制。
- `MissionRunState.settlementId` 记录接取委托的来源聚落，随 `MissionLog v2` 存档；旧 v1 MissionLog 读取为空来源。
- 服务器玩家档案保存 `PlayerState v21`（M33-F5-T2 起整体 v22）的 owner-only `PlayerSettlementReputation{id,score}` 摘要；重连/复活时先恢复实体快照，再把摘要中缺失条目合并回 ECS。v20/旧负载摘要为空。

### 4.3 声望后果与服务限制（M33-F1-T5）

- 价格：`SettlementBuyPrice(base, score)` 按当前等级 `buy` 倍率取整（四舍五入、最少 1）；`SettlementSellPrice` 同理使用 `sell`。单机购买、交易面板和服务器 `TradeOffer` 使用同一函数，客户端不能自报价格。
- 任务：`MissionDef.minSettlementReputation` 可选，范围 -100~100，缺省 -100；`FindAvailableSettlementMission`、对话 `MissionAvailable/MissionNone/MissionLocked` 和服务器接取都要求当前聚落分数达到任务门槛。内置聚落池门槛为 `settlement_help=-39`、`settlement_escort=0`、`settlement_clear=20`、`settlement_supply=30`；F1-T6 新任务 `north_blood_drive=-39`、`south_harvest_help=0`、`river_fish_supply=-39`；战略触发的 `refugee_relief` 保持默认 -100。
- 对话：`MakeSettlementDialogue` 接收当前分数，按 Hostile/Wary/Neutral/Friendly/Trusted 生成不同态度文本；任务被声望锁定时显示“声望不足？”分支，而不是伪装成“没有委托”。F1-T6 起内置 `dialogueProfile` 会在根节点追加聚落口吻（村庄广场、诊所消毒水、农场谷仓/泥土、桥头河风），未知 profile 回退通用文本。
- `services`：交易/委托入口按服务位隐藏；未启用 `defense` 时聚落不会自动触发夜袭或让守卫迎战（`SettlementProvidesDefense`），但战略失守/恢复状态仍可记录；M33-F3-T2 起 `recruit` 控制求助者对话中的同伴招募/归队/解散待命入口。Hostile 只拒绝交易/委托/招募，防御仍可参与。
- Hostile 服务拒绝原因：单机/服务器分别提示 `声望敌视：商人拒绝交易`、`声望敌视：拒绝委托` 或 `该聚落不提供交易/委托`。不做声望高自动送装备。
- 内容指纹 v15（M33-F3-T5 起整体为 v19，M33-F4-T1 起为 v20）：任务 `minSettlementReputation` 与既有聚落声望配置共同参与指纹，联机双方不一致时握手拒绝。

## 5. `missionPool` 与求助者对话

求助者使用代码内置对话树，`missionPool` 决定“接一件委托/看看我现在的委托/现在没有委托”三个分支是否可见。

- 池内任意任务处于 Active 时，视为已有进行中委托，对话只显示“看看我现在的委托”，不再提供接取。
- 没有 Active 时，按 `missionPool` 顺序寻找第一个 `MissionCanActivate` 且当前聚落声望达到 `minSettlementReputation` 的任务；接取时也是这个顺序。
- 接取后走现有任务系统的目标/时限/失败/放弃/奖励规则；聚落对话不负责交任务，完成仍由目标推进触发。
- 已完成 once 任务不会重接；失败/放弃按任务的 `retryDays` 冷却。
- `missionPool` 中不存在或不可激活的任务在运行时被忽略；建议在 mod 测试中校验引用。
- M33-F1-T6 起四个内置聚落使用独立池：村庄 `settlement_help/escort/clear/supply`；北部 `north_blood_drive`；南部 `south_harvest_help`；河畔 `river_fish_supply`。新任务均带 2–3 天时限与 1 天重试。
- `AcceptSettlementMission=39` 只携带求助者 StableId，不携带任务 id；服务器重新计算可接任务，客户端不能指定任意任务。
- M33-F3-T2 起求助者根节点还提供同伴入口：聚落有 `recruit` 服务且无同伴时显示“招募一名同伴”，有活动同伴时显示“让同伴留在聚落待命”，有本聚落待命同伴时显示“让同伴归队”。条件为 `recruit.minReputationTier`、前置任务 Completed、配置物资；失败不扣物。
- 伙伴归属由 `recruit.requiredMission` 是否属于该聚落 `missionPool` 决定；内置林医生归村庄营地、阿默归南部农场，北部诊所/河畔营地不提供招募。
- 推荐聚落任务使用 `source: "npc"`、`autoActivate: false`、`kind: "once"`，并设置 `timeLimitDays`/`retryDays`；否则可能与自动接取/每日刷新冲突。

内置对话树是固定节点/条件/选项，口吻开场由 `dialogueProfile` 选择；`settlement.name` 只进入说话人后缀。当前不提供对话 JSON，mod 不能新增节点、改写选项或增加新的对话结果类型，这些需要扩展 C++ `game::Dialogue`。

## 6. `defense` 夜袭配置

`defense` 省略时关闭。对象存在时 `enabled` 默认 `true`；显式写 `"enabled": false` 可保留配置但关闭夜袭。F1-T5 起还必须包含 `services` 的 `defense` 位，未包含时不会自动触发夜袭/守卫迎战。

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
| `guardEngageRadius` | 8 | 0~64 格 | 守卫在聚落外主动搜索普通感染者的半径；战略围攻/夜袭时生效。 |
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
- 守卫死亡后不会由 `EnsurePopulation` 补生成；失守期间暂停该聚落的交易/委托。战略反攻把区域恢复为 Human 后，`RestoreSettlement` 清除人口标记，下一次人口检查重建守卫/平民；商人保留。

## 8. 存档与联机

- `SettlementState`（`STLS` v1，固定 StableId `0x4D32334634535441`）按全部聚落定义保存 `id/fallen/guardHealth/lastNightKey`；`defense.enabled=false` 的条目不会触发夜袭，但状态按 id 保留。
- 单机 F12 与服务器 `SaveWorld` 在实体快照前写入；读档恢复实体后 `ApplyState`，守卫按保存生命重建，失守/守卫生命 0 不重建。
- 每个聚落的商人库存由各自 `MerchantStock` 世界实体保存，补货按该商人 `SettlementNpc.settlementId` 对应 `restockDays`；玩家任务进度由 `MissionLog` 保存。
- `ZLR1` 复制 `SettlementNpc`（聚落/类型/activity/home），客户端只读显示，不本地生成或调度 NPC。
- 玩家任务进度由 `MissionLog` 保存；M33-F1-T4 起 `MissionLog v2` 追加任务来源 `settlementId`。
- `PlayerState` v16 追加 owner-only `PlayerSettlementProgress{id,fallen,raidActive,guardHealth,guardHealthMax}` 与聚落提示；v21 追加 `PlayerSettlementReputation{id,score}` 声望摘要。用于 HUD/对话/交易拦截与联机分数显示。
- `AcceptSettlementMission=39` 和 `TradeOffer=21` 都由服务器校验距离、实体、类型、失守/夜袭和任务可用性；客户端不本地扣物、发奖或接取。
- M33-F3-T2 新增 `RecruitCompanion=51`/`DismissCompanion=52`，同样携带求助者 StableId 并由服务器复核范围、聚落状态、声望/任务/物资或同伴归属。同伴按 `ownerStableId` 唯一；解散只改 `CompanionState.standby` 并回到聚落，不新建实体。M33-F3-T3 起单机/服务器同伴自动跟随 owner、在 owner 受威胁时参战并可低血撤退；T4 起生成时带初始装备/背包，主手武器与躯干护甲影响战斗，倒地可救援/死亡后装备转入尸体容器，`Companion`/`CompanionState` 进入世界存档；T5 起信任随击杀/赠礼/个人委托阶段变化，`companions-v1` 的 `personalQuest[]` 提供两段个人委托、额外特长/装备/对话状态，`CompanionState` 存档 v3 保存信任/阶段/进度/防重复奖励标记；T6 起 `ZLR1 CMPN` 复制同伴 defId/指令/待命/倒地/死亡/信任/委托进度与 owner，客户端只读，旧负载安全回退；玩家断线、死亡或同身份接管时服务器让同伴回聚落待命并保留 StableId/owner，重连不复制，服务器重启从世界存档恢复。T7 起同伴面板（P）显示生命/状态/信任/装备/背包/委托，面板 1/2/3 发送跟随/停留/撤退、4 赠礼当前快捷栏物品、E 交谈/救援；聚落求助者提示与对话 hint 显示招募声望/前置委托/物资条件，HUD 显示同伴倒地/低血/远离/卡住提示。T8 起 `config/game.json` 的 `companions.ai.*` 配置跟随/leash/攻击/倒地/救援参数，主手武器伤害/冷却按 0.38/1.20 缩放，内置同伴同武器 DPS 为玩家的 31.67%，空手回退 35.29%，落在 30–50%；已招募时求助者提示显示“已有同伴：名字（状态）”，对话按医疗/斥候 profile 区分闲聊。联机仍只同步装备摘要，不显示完整背包。完整同伴平衡见 [docs/19-M33同伴数据与平衡.md](19-M33同伴数据与平衡.md)。
- 聚落字段（含 `regionId/services/startingReputation/dialogueProfile/defense` 与声望配置）及任务 `minSettlementReputation` 进入内容指纹 v15（M33-F3-T5 起整体为 v19，M33-F4-T1 起为 v20）；联机双方定义不一致会在握手阶段拒绝。

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
      "regionId": "riverlands",
      "services": ["trade", "missions", "defense"],
      "startingReputation": 0,
      "dialogueProfile": "river_camp",
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

M33-F1-T2 起追加聚落会自动获得独立商人/守卫/求助者与库存，不需要改 C++；坐标仍必须落在地图可走区域并位于安全圆内。服务器按 chunk 加载/格占用重试，单机按阻挡重试。

检查清单：

1. 所有 `npcPoints` 在安全圆内，坐标处有地面且可达。
2. `missionPool` 引用真实存在、可激活的任务；任务不要自动接取。
3. `services` 条目不重复；`startingReputation` 在 -100~100；`regionId`/`dialogueProfile` 使用 `[a-z0-9_.]`。
4. `defense` 数值在范围内，`maxRaiders >= baseRaiders`；不打算夜袭时省略 `defense`。
5. 客户端与服务器安装同一 mod；单机 content 热重载可用，联机/主机修改后仍需重启。
6. 用 `tests/test_settlements.cpp`/Python 数据测试的同类校验检查 id、点位、服务、声望和任务引用。

## 10. 当前边界

- M33-F1-T2 起每个聚落按点位生成独立商人/守卫/求助者，`SettlementState` 覆盖全部定义，主聚落生成假设已移除；追加聚落无需改 C++ 即可进入运行时。F1-T6 起允许只有商人+求助者的无防御小聚落。
- M33-F1-T3 起交易/对话/任务/HUD 按 NPC 的 `settlementId` 路由，交易目标按商人 StableId 绑定；HUD 和世界地图显示玩家真实聚落声望。
- M33-F1-T4 起声望随单机存档、服务器玩家档案和重连恢复；随机世界事件通过 `EventComplete` 入口接线，未绑定聚落时默认不发声望；`attackNpc/theft` 入口保留给后续攻击/偷窃行为。
- M33-F1-T5 已接价格倍率、任务最低声望、五档对话态度与 `services` 入口；F1-T6 已接入四聚落独立任务池、口吻和桥头第 4 聚落；F1-T7 已验证双客户端跨聚落交易/任务声望独立，以及服务器重启后的声望、任务和库存恢复。
- 只支持 merchant/guard/requester 三类 NPC 和内置固定节点；`dialogueProfile` 只能选择内置口吻，新增节点/条件/结果需要改 C++。
- 交易报价全局固定 5 档，`merchantStock` 只能调初始数量，不能按聚落改商品或价格。
- 守卫行为只有 `defend`/`retreat`；新增策略需要扩展 `SettlementGuardBehavior` 和 `SettlementNpcSystem`。
- 同一聚落同时只允许一条进行中的池内委托；多任务并行、委托交付对话和自定义任务 UI 未提供。
- `safeRadius` 不是物理安全区，不会阻止僵尸或玩家进入。
- NPC 位置、巡逻目标、当前活动不存档；`STLS` 只保存失守、守卫生命和当晚标记。
- 夜袭 active 不跨存档恢复；读档只保留当晚已触发标记。
- 追加聚落不会自动创建地图房间、墙体、床或容器；地图/POI 布局仍由地图生成器或地图 mod 保证。

## 11. F1 回归基线（M33-F1-T7）

- 双客户端跨聚落用例：两个玩家分别在 `village_haven` 与 `north_fields` 交易、接取并完成 `settlement_help`/`north_blood_drive`；验证各聚落库存独立、交易 +1、任务 +10、最终声望各 11 且对方聚落为 0，服务器重启后声望、任务完成状态和库存恢复；用例位于 `tests/test_settlement_network.cpp`。
- 旧档/旧客户端：`SettlementReputation` 缺失按空/初始值，`MissionLog v1` 无来源仍可读且 `rewardClaimed` 不被重置，`PlayerState v20` 聚落声望摘要默认空；覆盖见 `tests/test_entity_save.cpp`、`tests/test_player_state.cpp`、`tests/test_settlement_integration.cpp`。
- 2026-11-14 基线：`M33-F1*` 25/25（654 assertions）、`M33-F3-*` 31/31（792 assertions），其中 `M33-F3-T8*` 3/3（56 assertions）；全量 Debug **1010/1010（141028 assertions）**、Release **1010/1010（141042 assertions）**、Python **108/108**；1280 烟测沿用 `build/m33_f1_t6_smoke/`、`build/m33_f2_t7_dual_smoke/`，同伴面板见 `build/m33_f3_t7_smoke/`，T8 双客户端 15 分钟见 `build/m33_f3_t8_dual_smoke/`；F2-T5/T7 联机行为见 `tests/test_world_event_network.cpp`，F3-T2/T3/T4/T7/T8 双客户端与真实同伴战斗见 `tests/test_companion_network.cpp`，界面见 `tests/test_companion_ui.cpp`，装备/背包/倒地/存档见 `tests/test_companion_state.cpp`。
- 限制仍为：交易 UI 只有买入、报价固定 5 档、`dialogueProfile` 仅内置口吻、`merchantStock` 不能改商品表、`recruit` 已由 F3-T2~T8 实现、无防御聚落不自动夜袭。

