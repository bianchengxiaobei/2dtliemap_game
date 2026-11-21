# 开发日志（devlog）

> 每个里程碑结束写一段小结：学了什么 / 踩了什么坑 / 下阶段风险（见 docs/01 附录）。

---

## 2026-11-21 M36-T7 回归、性能与文档收口

**范围**：完成 M36 室内可见性自动化收尾。启动时输出一行剖切配置摘要；`render.cutawayDebug=true` 时重算日志增加射线数与耗时，超过 16 条射线告警。性能排查发现原候选扫描每次重算都遍历整个可见 DrawItem（约 5k）并给每个格写入稀疏表，室内单次重算约 1.6–2.9ms；改为只扫描玩家 ±4 格、当前房间建筑与遮挡建筑包围盒（外扩 2 格），候选按坐标排序合并，且“无目标且无旧状态”的格不再插入，室内候选降至 97–105，重算 P50/P95 **0.072/0.111ms**，射线 0（预算 ≤16）。无新增配置键、协议、存档或复制字段。

**回归/性能**：Release 全量 **1178/1178（145776 assertions）**、Debug 全量 **1178/1178（145762 assertions）**、Python **127/127** 全绿。500 人口村庄巡航 42s FPS 59/59/60/60/72/60/60/60，中位 **60**；同场景关闭剖切对照中位 **60**，chunk 失败 0。专用服务器 + Alice/Bob 双客户端 15s：2 会话、快照 1079/失败 0、坏包/超大/过期/越权 0，三份 stderr 均为空。

**截图/限制**：归档 `build/m36_t7_shots/m36-entry-room_1280_hud.png`（临时出生点验证后已还原默认 70,24）。第 10 节 10 张 1280 截图与 3 张 1920 抽检、A 室内/B 室外同屏观感、用户实机确认仍待人工，因此 DoD 未勾选、不宣称 M36 已完整交付。

**下一步**：按 `docs/24` 第 10 节做 1280/1920 人工截图与双客户端同屏验收；用户确认后再勾选 DoD 并进入下一里程碑。

## 2026-11-21 M34-F4-T4 文档、验收与交付

**范围**：补齐 M34 交付文档。README 增加潜行/投掷/陷阱操作、M34 自动回归状态与已知限制；`docs/modding.md` 版本行补 M34-F1/F2/F4，并新增 `3.6.3.10 M34 潜行、投掷与陷阱交付契约/限制`，汇总键位、配置/内容契约、版本/旧档、服务器权威、性能上限、未做项和复验命令；`docs/22` 补齐 F4-T4 文档记录、F4-T2 未验收说明与已知限制；`docs/01` 同步状态。

**状态**：M34 仍为 **23/25**。F4-T2 的 15–30 分钟自然流程、陷阱触发/火焰/背刺/读档和双客户端服务器重启未人工验收，因此 T4 完成标准未勾选，不宣称 M34 已完整可交付。无新增代码、协议、存档或内容字段；未提交 Git。

**下一步**：按 F4-T2 清单做人工闭环并勾选 T2/T4；用户确认后才进入下一里程碑。

## 2026-11-21 M34-F4-T3 全量回归、版本与性能

**范围**：为陷阱目标收集增加 `ecs::SpatialIndex` 局部 chunk 查询（`CollectTrapTargetsAround`），单机 `UpdateTraps` 和服务器 `TickTrapTriggers` 不再每 tick 扫描全图 `Health/Position`；按 chunk/实体去重，限制 64 个扫描中心/2048 次访问/256 个候选，超限安全跳过。新增 64 个在飞投掷物上限，单机 `ReleaseThrowAim` 与服务器 `ThrowItemAction` 超限拒绝。无空间索引时保留旧全量收集回退。

**测试/证据**：`M34-F4-T3*` **1/1（11 assertions）**；`M34*` Release/Debug **64/64（1633）**。Release 全量排除既有 `M30-T6`/`M31-T9` **1151/1151（145479 assertions，4 skipped）**；Release 相关 `M5/M20/M23/M25/M30/M33*` 排除 `M30-T6`/`M31-T9` **373/373（8916）**。Debug 全量排除七组既有 flake 后 **1127/1127（144696，28 skipped）**；此前过滤五组时 X-F1、M26-V2 各失败一次，隔离 **13/13（482）**、**2/2（66）** 均通过。版本定向 **20/20（736）**，确认内容指纹 v23、`PlayerState v23`、ZLR1 组件、存档 53、命令 62、旧档回退；Python **127/127**。Release 无 mod 600 tick 烟测 `build/m34_f4_t3_startup_nomods/startup_nomods.out.log`：物品 99、配方 31、陷阱 3、指纹 `0xa086c91a0d3c7030`、tick=600、stderr 0。

**限制/下一步**：F4-T2 的 15–30 分钟自然流程/双客户端重启仍待人工（本轮按用户选择先做 T3），不勾选 T2；Debug 原始全量仍有既有非 M34 flake；局部扫描按 chunk 粗粒度，同一 chunk 内实体仍全部分类，候选超限时尾部本 tick 不触发。下一步 F4-T2 人工收口或 F4-T4 文档交付。

## 2026-11-21 M34-F4-T1 跨系统联动

**范围**：新增 `M34-F4-T1` 组合用例，覆盖潜行脚步 2.5→1.125 格、投掷石块独立 6 格噪声、投掷物/地面物品/玩家/同伴不会成为陷阱目标、捕兽夹与动物掩码、燃油 112.5s/9 格火焰、冷却阻止重复消耗、同伴从武装 `PlacedTrap` 上移开。新增 `ThrowAimBlockReason::Gathering` 与 `WorldDemoState::IsGatheringActionActive`：单机觅食/钓鱼/剥取和联机 survival/animalHarvest 进行中拒绝投掷瞄准与潜行切换；服务器 `SetServerStealth`/`ThrowItemAction` 同步拒绝，避免投掷或潜行覆盖采集动作。菜单/背包/建造/载具/UI 拦截沿用 F2-T2，投掷仍不会替代战斗动作。

**验证**：`M34-F4-T1*` **2/2（42 assertions）**；`M34*` Release/Debug **63/63（1622）**；`M34-F3*` Release/Debug **20/20（531）**；`M34-F2-T2*` **4/4（59）**；相关旧系统 `M33-F3-T3/M23-F2b-T6/M30-T7/X-F1 2b-3` **11/11（182）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake **1150/1150（145468 assertions，4 skipped）**。绊线噪声/零伤害、燃油火焰、投掷扣除/回收/损坏与非 owner 拒绝沿用 F3-T3/T5、F2-T5/T7 真实服务器用例。

**限制/下一步**：投掷/陷阱自然组合与 15–30 分钟 1280 流程留 F4-T2；火焰仍可伤害进入火区的玩家/同伴（只保证陷阱直接目标排除）；本机 Python 后端未编译；无 PvP 陷阱/可携带陷阱。下一步 **M34-F4-T2**。

## 2026-11-21 M34-F3-T7 陷阱平衡、回归与 F3 收口

**范围**：新增 `M34-F3-T7` 数据平衡用例，从真实 `config/game.json`/`assets/traps.json`/`assets/items.json` 锁定木材成本捕兽夹 3、绊线 1、燃油 4；捕兽夹 55 伤害/2s 硬直/4 次/2s 冷却/1s 布防，绊线 0 伤害/18 格噪声/8 次/1s 冷却，燃油 0 直接伤害/1.5 格 45 游戏分钟/1 次。断言捕兽夹单次低于消防斧轻击、DPS 低于其 50%，绊线不造成伤害，燃油成本最高且 9 格火焰一次性；未改生产逻辑、数据、协议或存档。README 补充三种陷阱与 HUD/拆除能力；F3 验收项全部勾选。

**验证**：`M34-F3-T7*` **1/1（53 assertions）**；`M34-F3*` Release/Debug 均 **20/20（531）**；Release 定向 `M34-F3*,M20*,M23*,M32*` **245/245（6068）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake **1148/1148（145425 assertions，4 skipped）**；Python **127/127**。Debug 全量首轮 **1146/1152**，6 个失败均为既有非 F3 计时/网络/性能用例（X-F1 建筑格、M22 睡眠、M23-F2a-T6/T7、M26-V2、M31-T9）；排除既有 `M22-F1`/`M23-F2a-T6`/`M30-T6`/`M31-T9` 组后 **1142/1142（145265）**，M23-F2a-T6 单跑仍失败，未改该旧测试/生产路径。

**限制/下一步**：平衡为数据/纯逻辑断言，不替代自然 15 分钟试玩；Debug 原始全量仍有上述既有 flake。未新增陷阱实体/地图格式字段，`mapformat.md` 无需变更。F3 无阻塞，下一步 **M34-F4-T1 跨系统联动**。

## 2026-11-20 M34-F3-T6 陷阱 UI、提示与 1280 验证

**范围**：新增 `TrapEffectText`/`TrapBuildSummary`，建造菜单显示陷阱名称、木材、效果、次数/冷却；`HudState` 增加附近已放置陷阱摘要字段与 `TrapStatusText`，显示 armed/布防中/解除/冷却/剩余次数/耗尽/owner 只读；`TrapHintText` 统一 E 布防/立即布防/解除与 X 建造模式拆除/二次确认/返还文案。Native `UiSystem`、两份 RML/RCSS、`core_ui` Lua/Python 与 `Hud.GetState` 增加 `trap-status` 和 camelCase 字段；`HudTrapStatusSuppressed` 在背包/菜单/对话/角色/地图/载具改装/睡眠/战报等模态界面隐藏摘要。

**验证**：`M34-F3-T6*` **2/2（31 assertions）**；`M34-F3*` **19/19（478）**；P1 HUD **2/2（730）**、Lua UI binding **36**；Release 全量 **1147/1147（145372 assertions，4 skipped）**、Debug 定向 **67/67（1734）**、Python 数据 **127/127**。Release 1280 core_ui 构造烟测 `build/m34_f3_t6_ui/`：建造菜单捕兽夹效果/次数、捕兽夹 E/X 与已布防 HUD、绊线 7/8 次、燃油 0/1 次/9 格火焰、拆除返还 2 木材共 7 张截图；日志确认 `m34-f3-t6-ui-smoke-pass`，stderr 0。

**限制/下一步**：烟测使用临时探针、传送、加木材/绷带与构造僵尸/火焰，不替代自然试玩；Python 后端本机关闭，Python 字段代码已同步但未执行 Python API 断言；native fallback 未单独出图；X 仍仅在建造模式生效，普通 E/X 提示明确标注。下一步 M34-F3-T7 平衡、全量回归与文档收口。

## 2026-11-20 M34-F3-T5 联机权威、复制与双客户端

**范围**：T5 复用已接好的 `TRAP2` 只读复制和 `PlaceStructure`/`InteractFacility`/`DemolishStructure` 命令，不新增命令、存档字段或协议。`WorldDemoState::UpdateTraps` 仍只在单机分支运行；网络客户端只把服务器 `PlacedTrap` 副本应用到 ECS 显示，不本地布防、触发、扣次数或发奖。`TRAP2` 服务 defId/armed/cooldown/uses/owner，owner 同时还原 `StructureOwner`，非 owner 客户端前置拦截，服务器再次校验。

**验证**：新增真实 UDP/KCP 双客户端用例：A/B 看到同一陷阱 StableId 与 defId/armed/cooldown/uses/owner；B 非 owner 发送 `InteractFacility` 被 `ownershipRejected`，A 对同格重复 `PlaceStructure` 被 `commandsRejected` 且不生成第二座陷阱；两名僵尸同时进入范围时只结算一名、捕兽夹 4→3 次并进入 2s 冷却，A/B 的 `TRAP2` 都看到 uses/cooldown；A 断开后同身份重连恢复同一 StableId 和陷阱 owner 状态；重连后的 A 用旧客户端 `demolish.flags=0` 立即拆除成功，验证旧路径兼容。`M34-F3-T5*` **1/1（46 assertions）**，连续 3 次 Release 通过；`M34-F3*` **17/17（447）**、Release 全量 **1145/1145（145337 assertions，4 skipped）**、Debug 定向 **65/65（1703）**、Python 数据 **127/127**。

**限制/下一步**：没有 1280 双窗口画面/UI 验证，留 T6；网络测试不替代自然双客户端试玩；`TRAP2` 不复制 `armRemaining`，布防剩余时间只影响服务器/读档，客户端等服务器 armed 状态；旧 `SpikeTrap`/电网仍独立。下一步 M34-F3-T6。

## 2026-11-20 M34-F3-T4 陷阱存档、读档与旧档兼容

**范围**：`PlacedTrap` 从 `TRP2 v1` 升到 v2，在 defId/armed/cooldown/uses/owner 后追加 `armRemaining`；读取仍接受 v1，旧负载缺失该字段时按 0 处理。单机通过 `SaveManager` 快照/读档，服务器通过 `ServerHost::SaveWorld` 与重启恢复，布防中的陷阱继续按剩余时间自动布防；旧档只有 `SpikeTrap` 时保持 `TRAP v1` 与电网/占位实体独立，不生成 `PlacedTrap`。燃油陷阱的 `FireSource/FireAnchor` 沿用 M23 火焰存档，`trap_fuel` 火源跨重启保留。

**关键修复/边界**：T2/T3 的 `armRemaining` 只是运行时字段，布防中存档会永久保持解除；T4 将其纳入组件 v2，同时保留 v1 回退，避免旧档被误判为布防中。现有 `FireSource.ownerStableId` 按 M23 约定不落盘，重启后火焰本身恢复但归属为 0，守城贡献不回溯。读档不会重跑建造/拆除，不返还木材也不重复触发；T4 不保存飞行中投掷物，不改变旧 `SpikeTrap`/电网运行时。

**验证**：`M34-F3-T4*` **4/4（126 assertions）**，覆盖 TRP2 v2 往返/v1 回退、单机快照后继续游戏再读档、旧档无 `PlacedTrap` 与旧 `TRAP v1`、服务器 `SaveWorld` 重启恢复 3 个陷阱与燃油火源；`M34-F3*` **16/16（401）**、保存/复制回归 **48/48（1256）**、Release 全量 **1144/1144（145291 assertions，4 skipped）**、Debug 定向 **64/64（1657）**、Python 数据 **127/127**，Release app 构建成功。**限制/下一步**：T5 负责 `TRAP2` 客户端只读一致性与双客户端重连；T6 负责 1280 陷阱状态/建造面板。下一步 M34-F3-T5。

## 2026-11-20 M34-F3-T3 触发规则与三种效果

**范围**：新增 `TrapSystem` 纯函数与 `FireSystem::IgniteForcedAt`。目标筛选按 `triggerMask` + 半径取最近僵尸/动物，排除玩家、同伴、聚落友军、尸体/`Dying`、`NoCombatTarget`、Human Raiders 和异层；捕兽夹 55 Blunt/2 秒硬直/4 次，绊线只发 18 格噪声/8 次，燃油陷阱强制生成 1.5 格、45 游戏分钟的 `trap_fuel` 火焰/1 次。单机 `UpdateTraps` 改为所有非联机 tick 推进，服务器 `TickTrapTriggers` 同时处理新陷阱，同伴 AI 依据 `PlacedTrap.defId` 半径避开武装陷阱；旧尖刺仍只在基地守城激活时处理。

**关键修复**：T2 把 `UpdateTraps` 放在守城流程里，导致新陷阱平时不自动布防；T3 移到所有非联机 tick，并给旧 `SpikeTrap` 保留守城门控。服务器绊线噪声经 `core::kOnSound` + `WorldSound` 发出，网络用例用 `PerceptionState.sinceHeard` 验证目标确实听见；燃油火源不依赖目标格可燃物/湿度，但仍走现有火焰伤害、燃烧僵尸、存档和视觉管线。

**验证**：`M34-F3-T3*` **5/5（103 assertions）**，覆盖目标掩码/最近目标/友军/玩家/尸体/Dying/异层/Raider 排除、布防/冷却/次数、45 分钟换算、9 格燃油、无可燃物强制点燃、同伴避让，以及真实 UDP/KCP 的捕兽夹 55 伤害、绊线噪声与零伤害、燃油 9 格火焰与 `BurningZombie`；`M34-F3*` **12/12（275）**、Debug 定向 **26/26（600）**、Release 全量 **1140/1140（145165 assertions，4 skipped）**、Python 数据 **127/127**；旧 `X-F1 2b-3*` **1/1（28）**、`M32-T18*` **1/1（23）** 回归通过。

**限制/下一步**：`PlacedTrap` 布防/冷却/次数存档与重启恢复留 T4；客户端只读复制和双客户端一致性留 T5；没有 1280 陷阱状态/建造面板（T6）。燃油形成的环境火焰可伤害进入范围的玩家/友方，T3 只保证陷阱目标选择不误触；目标收集仍按每 tick 一次候选快照，20+ 陷阱空间索引优化留 F4-T3。下一步 M34-F3-T4。

## 2026-11-20 M34-F3-T2 建造放置、布防与拆除

**范围**：把 3 个陷阱建造项接进现有建造/设施/拆除命令。`Traps` 新增 `FindByBuilding`、`TrapCanToggle`、`SetPlacedTrapArmed`、`AdvancePlacedTrapArming` 和 `SpawnPlacedTrap`；单机与服务器共用实体生成、自动布防和 E 切换规则，不碰旧 `SpikeTrap`/电网。`X` 继续复用 3 秒二次确认和 `demolishRefundRatio`，只返还木材。

**关键修复**：T1 的 `PlacedTrap` 虽已注册 `TRAP2`，但不在 ZLR1 Prop 候选里，客户端也不会把 `hasPlacedTrap` 还原成 ECS 组件；T2 一并接线。`object.trap` 是三个新陷阱和旧尖刺共用占位图，拆除/脚本查询现在先按 `PlacedTrap.defId → TrapDefinition.buildingId` 定位建造项，避免把捕兽夹误认成尖刺陷阱。

**验证**：`M34-F3-T2*` **2/2（104 assertions）**，覆盖建造项映射、实体字段/owner、1 秒自动布防、手动布防/解除不扣次数、耗尽拒绝、旧尖刺无新映射；真实 UDP/KCP 用例覆盖越界/木材不足/占用/成功放置、服务器布防、客户端 `TRAP2` 复制、E 切换、非 owner 拒绝、拆除返还 2 木材和复制删除。`M34-F3*` **7/7（172）**、Debug 定向 **21/21（497）**、Release 全量 **1139/1139（145154 assertions）**、Python 数据 **127/127**。

**限制/下一步**：不接触发、伤害、噪声、火焰（T3）；`armRemaining` 不写入 `TRP2 v1`，布防中的陷阱存档后会保持解除（T4 再接）；没有 1280 建造菜单/陷阱状态 UI（T6）。网络用例是无窗口客户端，F4 再做双客户端画面联调。下一步 M34-F3-T3。

## 2026-11-20 M34-F3-T1 traps-v1 数据、组件与版本

**范围**：新增 `assets/traps.json`（`traps-v1`）与 `src/game/Traps.{h,cpp}`，加载/合并捕兽夹、绊线警报、燃油陷阱；`building.types` 与 `DefaultBuildables` 增加 3 个木材建造项。新增 `ecs::PlacedTrap`、`TRP2` 存档组件与 ZLR1 `TRAP2` 复制组件，内容指纹升 v23，内置存档组件 52→53、ZLR1 32→33。数据由 `ContentBundle`/`GameApp`/`ServerHost` 统一加载并参与联机握手。

**校验**：`id`/`name`/`buildingId` 必须合法且建造项存在；`damage` 0–100、`radius` 0.5–3、`uses` 1–99、`noiseRadius` 0–32、`armSeconds` 0–10；`triggerMask` 仅接受 `zombie`/`animal`；可选 `fire{radius,minutes}` 为 0.5–3 格/1–240 游戏分钟；至少提供伤害、噪声或火焰之一。mod 文件按加载顺序追加，非法条目逐条跳过，重复 id 不覆盖基础定义。

**测试**：`M34-F3-T1*` **5/5（68 assertions）**、`M13 Replication*` **5/5（158）**、`M10*` **30/30（825）**、`M31*` **37/37（2385）**、`*content*` **11/11（197）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1133/1133（144958 assertions）**。重新运行 `ZL_ENABLE_PYTHON=ON` 的 `*Python*` **14/14**、`M8*` **11/11**，此前 SIGSEGV 未复现；Python 数据工具 **127/127**。限制：T1 只完成数据/组件/存档/复制注册，尚未接放置、布防、触发、燃油火焰和服务器权威结算；下一步 **F3-T2**。

## 2026-11-20 M35-F5-T5 全量收口与 1280 验收

**范围**：补齐 `README` 脚本内容包说明、示例验收记录与 M35 状态；用 Release 1280×720 真实窗口、真实键盘/鼠标跑完示例三波并验证关闭面板后继续。

**1280 验收**：准备→第 1 波→真实鼠标点击“隐藏”→三波清场→结算→12 秒自动重开第二局；首局 360 分/最佳 360，HUD 右侧挑战字段在面板关闭后持续更新。证据 `build/m35_f5_t5_smoke/01-prep-1280.png`～`05-restart-1280.png` 与 `world.out.log`，日志含三次 `波清场`、`挑战结束：完成`、`挑战面板已关闭；挑战继续运行`、第二局 `挑战开始第 1/3 波`，stderr 为空，音频引擎系统默认设备就绪。

**修复**：core_ui 全屏 HUD body 会拦截脚本面板点击，改为 `body pointer-events:none` 并只给 `#hud-bag` 重开；点击 UI 仍会触发世界点击移动，`WorldDemoState` 左键移动增加 `IsPointerOverUi` 守卫；`challenge.rcss` 子元素默认 inline 导致面板文字垂直重叠，显式 block 布局并让 body 不拦截点击，仅隐藏按钮可点。`M35-F5-T2*` 2/2（83）复验通过。

**测试**：`M35*` Debug/Release 39/39（1406）、Python 44/44（1456）；Release 全量排除既有 `M30-T6`/`M31-T9` flake **1129/1129（144885 assertions）**。Debug 全量在本机负载下仍命中 6 个既有计时/性能用例（M5 boids、ChunkManager p95、M31-T9 soak、3 个网络集成），与 M35 路径无关；X-F1 2b-2b 的 Debug diff 等待调整为 10s，M31-T9 Debug 快照预算调整为 20ms，以减少慢构建误报。三套 app 已重建。

**限制**：自动化真实窗口覆盖了键鼠/视觉/关闭/重开，人工听感仍建议单独确认；Debug 全量计时用例仍受机器负载影响。

## 2026-11-20 M35-F5-T4 脚本查询性能护栏

**范围**：为 `Entities.Query` 补截断日志与每 mod 每 tick 查询统计；只观测不额外限流，不改 API 返回结构。

**实现**：`ScriptApi` 新增 `QueryEntitiesForMod`、`QueryTickStats`、`FlushQueryStats`；Lua/Python 绑定把当前 mod id 带入，C++ 直调归入 `<direct>`。每 mod 每 tick 首条截断写 `ZL_WARN`（匹配数/limit/返回数/tick/半径/kind），后续仅计数；`GameApp::Tick` 末尾输出 `ZL_INFO` 汇总，下一 tick 自动重置。查询路径从“全量构造 `ScriptEntityInfo` 再排序”改为轻量 `Candidate`（entity/StableId/距离）排序，最终只对 limit 条构造快照；单次查询缓存 `Position/Health/StableId` 等组件 storage 指针，避免逐实体 `try_get` 的 type_hash 查找。

**验证**：新增 `M35-F5-T4*` 1/1（44 assertions），覆盖默认/最大 limit、半径钳制、截断只告警一次、per-mod/tick 汇总与下一 tick 重置；Lua M7-T3/Python M8-T3 补充 mod 归属与截断日志断言。`M35*` Debug/Release 39/39（1406）、Python 44/44（1456）；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1129/1129（144885 assertions）**。性能基线：140 实体/半径 32/默认 limit 32，20 次平均 **0.38–0.61 ms Debug**、**0.011–0.016 ms Release**（目标 <1 ms Debug）。

**踩坑**：最初把完整 `ScriptEntityInfo` 放进排序候选，Debug 下 140 实体约 4.8–7.4 ms；改成轻量候选后仍约 1.6 ms，最后发现逐实体 `try_get` 的 storage 查找才是大头，缓存组件 storage 指针后降到 0.4–0.6 ms。

**限制**：统计只覆盖 `Entities.Query`，不统计 `Get`/容器/目录查询；日志不做硬限流，mod 仍可高频查询；直接 C++ 调用没有真实 mod id，统一记为 `<direct>`。下一步 M35-F5-T5 全量收口与 1280 实机验收。

## 2026-11-20 M35-F5-T3 脚本音频

**范围**：新增 Lua/Python `Audio.Play2D/Play3D`，只播放现有 `assets/audio/*.wav`；不注册新素材、不进入 AI 听觉。

**实现**：`ScriptApi::PlayAudio2D/PlayAudio3D` 校验 clip 为字母/数字/`_`/`-`（拒绝路径/扩展名）、音量有限并钳制 0~1、3D 坐标有限、`radius` 0~256；通过既有 `SoundManager::Engine()` 的 SFX 组播放，文件缺失/引擎未就绪/`audio.device=off` 返回 false。`PlayAudio3D` 直接调音频引擎，不写 `OnSound`；Lua/Python `Audio` 表支持可选 `volume`/`radius`。示例挑战在波次开始/结束播放 `ui_click` 2D、击杀按事件坐标播放 `impact` 3D。

**验证**：C++ 覆盖已有 clip（null device）、缺失 clip、路径逃逸、非法坐标/半径/音量、设备关闭与 `OnSound` 不触发；Lua/Python 绑定分别覆盖正常/失败/设备关闭。`M35-F5-T3*` Debug/Release 2/2（30 assertions）、Python 3/3（39）；`M35*` Debug/Release 38/38（1362）、Python 43/43（1412）；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1128/1128（144838 assertions）**。

**限制**：首次文件解码仍走 miniaudio 同步路径（调用不等待播放完成）；无新增音频素材；实机听感/1280 音频设备验收留 F5-T5。

**下一步**：M35-F5-T4 性能护栏。

## 2026-11-20 M35-F5-T2 示例脚本挑战面板

**范围**：给 `example_scripted_gameplay` 增加 `ui/challenge.rml` 脚本面板，显示波次/倒计时/本局/最佳/目标；不新增 C++ HUD 或 API。

**实现**：新增 `mods/example_scripted_gameplay/ui/challenge.rml` 与 `challenge.rcss`，左上角 16px、244px 宽，`pointer-events:none` 仅“隐藏”按钮可点，非模态、`hud` 组。`challenge.lua` 在 `OnUiReady` 打开面板，`push_ui` 每 15 tick 与 HUD 同步，覆盖等待/准备/波次/间隙/完成/阵亡/多人禁用，进度条按剩余时间计算；点击 `challenge-hide` 调用 `UI.CloseDocument(..., true)` 后停止 UI 更新但挑战/HUD 继续；`OnWorldStart/OnModReload` 重开，`OnWorldExit` 卸载。新增 2 个测试：1280x720/1920x1080 与 `hud-clock/hud-right/hud-left/hotbar/hud-bag` 不重叠且面板无内部溢出；等待/准备/波次文本更新、非模态不捕获输入、真实鼠标点击隐藏后继续刷怪计分。

**验证**：`M35-F5-T2*` Debug/Release/Python 均 **2/2（83 assertions）**；`M35*` Debug/Release **36/36（1332）**、Python **40/40（1373）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1126/1126（144809 assertions）**。

**限制**：自动布局只覆盖内置 HUD 主要块，实机 1280 视觉与键鼠完整一局留 F5-T5；音频/性能留 F5-T3/T4。

**下一步**：M35-F5-T3 音频（可选）。

## 2026-11-20 M35-F5-T1 示例 mod example_scripted_gameplay

**范围**：交付端到端 Lua 内容包，串起 M35 的只读查询、行为事件、命令化动作、`Mod.Data`、`Json`、`require` 与 HUD；不新增 C++ API。

**实现**：新增 `mods/example_scripted_gameplay`（`api:2`、Lua，`loadOrder=130`），拆为 `main.lua`、`config.lua`、`challenge.lua`、`wave/director.lua`。进入世界 4 秒准备后开始 3 波（3/4/5 只，normal/fast/strong/armored 混编，每波 30 秒、间隙 6 秒）；击杀 +10、连杀最多 +10、清场 +50，制作/建造事件另有加分；完成/阵亡后 12 秒重开。最佳分数/最佳波次/局数/当前 run 写入 `Mod.Data`，run 用 `Json` 编码；热重载和读档从 run 状态继续。HUD 注册 4 个字段显示状态、波次/倒计时、本局/最佳分、天气/附近实体/饥饿/背包/等级。示例在准备阶段尝试装备武器、用 3 木材放木门，低血用背包物品，每波清场尝试 `Craft.Start("field_bandage")`，结束切换/维修/拆除木门，并调用 `Player.GiveXp`。新增 `tests/test_example_gameplay.cpp` 两用例：完整 3 波模拟 + 击杀归属/动作/HUD/Mod.Data 断言；`Mod.Data` 实体快照往返后成绩保留；运行中热重载继续当前 run。

**验证**：`M35-F5-T1*` Debug/Release/Python 均 **2/2（68 assertions）**；`M35*` Debug/Release **34/34（1249）**、Python **38/38（1290）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1124/1124（144726 assertions）**。热重载后 HUD 与分数保持，读档快照恢复 `best_score`/`best_wave`。

**限制**：自动化用 `OnZombieDead` 模拟击杀，不等同真实 1280 键鼠战斗视觉验收；自定义 `ui/challenge.rml` 留 F5-T2，音频/性能护栏留 F5-T3/F5-T4；多人只显示“仅单机”；木门/制作受实际站位、材料和背包限制，失败只记录不阻断挑战。

**下一步**：M35-F5-T2 HUD 辅助与示例 UI（`ui/challenge.rml`）。

## 2026-11-20 M35-F4-T5 文档与旧档兼容收口

**范围**：审计 M35 脚本文档、补 v1 mod/旧档兼容回归，完成 F4 持久化与工具收口；不改生产代码。

**实现/文档**：`docs/modding.md` 补齐 v1→v2 兼容说明，明确 `api:1` 告警加载、v1 函数签名不变、旧档无 `MDAT` 时 `Mod.Data` 为空、Lua `require` 可选而 Python 仍单入口；`docs/03` 更新 API 清单 header、F4 说明、沙箱/热重载/双后端与验收表；`docs/23` 回填 F4-T5 记录。新增 `M35-F4-T5` 用例：`legacy_v1` 以 `api:1` 经 `ModLoader` 告警加载，脚本内 `Game.GetApiVersion()==2`，v1 `Log.Info/Events.On` 正常执行；旧档无 `MDAT` 由既有 `M35-F4-T1` 用例覆盖。

**测试**：`M35-F4-T5*` Debug/Release/Python 均 **1/1（6 assertions）**；`M35*` Debug/Release **32/32（1181）**、Python **36/36（1222）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1122/1122（144658 assertions）**。

**下一步**：M35-F5-T1 `example_scripted_gameplay` 示例与玩法包。

## 2026-11-20 M35-F4-T4 Lua require 多文件

**范围**：为 Lua mod 提供受限 `require`，支持多文件拆分、点号/相对路径、模块返回值与热重载清缓存；Python 后端保持单入口。

**实现**：每个 mod 环境绑定带 5 个 upvalue 的 `require` C 闭包（Impl、canonical scripts 根、缓存表、加载中/深度表、环境）；缓存表作为 Lua upvalue 随环境 GC，`ReloadMod` 新建环境即清空。模块名支持 `util`、`wave.director`、可选 `.lua`；`./`/`.\` 相对调用文件目录。路径拒绝空段、`.`/`..`、绝对路径、盘符/冒号，`weakly_canonical` 后再校验仍位于 `<modRoot>/scripts/**`，symlink 逃逸拒绝；单文件 ≤1 MB、嵌套 ≤32 层、循环依赖报错。模块在同一环境执行，返回首值（无返回/`nil` 为 `true`），缓存按 canonical 路径；解析/加载/执行错误带调用文件行号，运行时错误带模块路径、行号与 traceback。嵌套 `RunChunk` 改用预算作用域，保存/恢复外层指令预算。

**测试**：新增 3 个 `test_lua_runtime` 用例（37 assertions），覆盖多文件/点号/相对路径/缓存身份/无返回值、类型/空名/`..`/绝对路径/symlink 越界、循环/深度/1 MB/语法/运行时行号、热重载 v1→v2 与每 mod 独立缓存；M7 沙箱用例更新为 `require=function`。`M35-F4-T4*` Debug/Release/Python 均 **3/3（37 assertions）**；`M35*` Debug/Release **31/31（1175）**、Python **35/35（1216）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1121/1121（144652 assertions）**。

**下一步**：M35-F4-T5 文档与旧档兼容收口。

## 2026-11-20 M35-F4-T3 Json 双后端

**范围**：为 Lua/Python 暴露同名 `Json.Encode/Decode/Array/Null`，统一处理对象/数组/空表/null、中文、浮点、循环与大小上限。

**实现**：Lua 手写 table ↔ nlohmann；连续整数 key `1..N` 为数组，空 table 为对象，对象 key 必须字符串；`Json.Array()`/`Json.Null()` 用私有标记表保留空数组/null，解码空数组/null 还原为标记。Python 由 C++ 预校验类型/深度/项数/字符串/有限数字/循环，再调用标准 `json.dumps/loads`（`ensure_ascii=false`、`sort_keys=true`、`allow_nan=false`、紧凑分隔符）。两端统一容器深度 16、字符串 64 KB、数组/对象 1024 项；循环失败并 `ZL_WARN`；非法 JSON/类型/非字符串 Decode 返回 nil/None。修复 Release 下 sol2 `is<int64_t>` 将浮点误判为整数的问题，改用 `lua_isinteger`。

**测试**：Lua 覆盖嵌套/数组/中文/浮点/空表/空数组/null/非法 JSON/循环/深度/项数/字符串/NaN；Python 覆盖同名场景及 dict 非字符串 key、`NaN/Infinity`。`M35-F4-T3*` Debug/Release **1/1（4 assertions）**、Python **2/2（10 assertions）**；`M35*` Debug/Release **28/28（1138）**、Python **32/32（1179）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1118/1118（144614 assertions）**。

**下一步**：M35-F4-T4 Lua `require` 多文件与热重载缓存。

## 2026-11-20 M35-F4-T2 Mod.Data Lua/Python API

**范围**：在 F4-T1 `ModData` 组件上暴露 per-mod `Mod.Id()` 与 `Mod.Data` 五个操作，覆盖热重载、存档、新世界与联机失败关闭。

**实现**：`ScriptApi` 新增 `GetModData/SetModData/RemoveModData/ListModDataKeys/ClearModData`；`Set` 复制当前组件后校验再整块提交，非法 mod id/key、非字符串、4097 bytes、单 mod 64 key、256 mod、全局 128 KB 均 false 且无部分写入；`Remove` 不存在 false，移空后删除 mod 条目；`Clear` 幂等 true；`Keys` 字典序。Lua/Python 均绑定 `Mod.Id()` 与 `Mod.Data.{Get,Set,Remove,Keys,Clear}`，Lua 非字符串 value 返回 false、Python 非 `str` 返回 false；loopback/client 下 `Get=nil/None`、`Keys` 空、写 false，每个 mod 每次加载最多 `ZL_WARN` 一次。热重载复用同一 `GameContext` 的组件，新 `EcsWorld` 为空，`MDAT` 存档往返后 API 直接可读。

**测试**：`test_script_api` 覆盖键序/四类上限/无部分写入/存档往返/新世界/loopback+client；`test_lua_runtime`/`test_python_api` 覆盖绑定、类型拒绝、热重载、新世界、联机与单次警告。`M35-F4-T2*` Debug/Release **4/4（161 assertions）**、Python **6/6（185 assertions）**；`M35*` Debug/Release **27/27（1134）**、Python **30/30（1169）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1117/1117（144610 assertions）**。

**下一步**：M35-F4-T3 `Json` 双后端。

## 2026-11-20 M35-F4-T1 ModData 组件与 codec

**范围**：新增跨存档脚本状态的世界单例组件、`MDAT` v1 二进制编码与惰性创建辅助；本任务不暴露 `Mod.Data` API。

**实现**：`src/script/ModDataComponents.h` 定义 `ModDataTag/ModDataEntry/ModData` 与上限常量；`EnsureModData` 首次创建无 `Position/SpriteRef` 的单例实体，重复调用复用同一实体。`ComponentRegistry` 注册 `MDAT`（内置组件 51→52），编码 u32 mod 数→mod id/u32 entry 数→key/value；校验 mod 数 ≤256、每 mod 64 key、id ≤128 bytes、key ≤64 bytes、value ≤4096 bytes、全局 key+value ≤128 KB、重复 id/key/控制字符/段剩余长度。`ModDataTag` 不单独入档，读档 codec 在还原后补挂 tag，查找优先 `ModData` 组件，避免加载后 tag 丢失。旧档无 `MDAT` 视为空；不新增 API/网络复制/`PlayerState` 字段。

**测试**：`test_entity_save` 新增 `M35-F4-T1` **1/1（60 assertions）**，覆盖空数据、多 mod/中文/空值往返、单例复用、无 Position、旧档缺组件、65 key/4097 bytes/全局超限写拒绝与 65 key 读拒绝；同步更新内置组件基线 51→52。Debug/Release/Python 定向全绿，`M35*` Debug/Release **23/23（973）**、Python **24/24（984）**；Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1113/1113（144449 assertions）**；三套 `ZomboidLike` 构建通过。

**下一步**：M35-F4-T2 `Mod.Data` Lua/Python API、联机拒绝与热重载保留。

## 2026-11-20 M35-F3-T7 联机策略与双后端收口

**范围**：非 direct 下所有 M35 新写 API fail-closed；`Game.IsMultiplayer/GetNetMode` 文档化；Lua/Python 双模式一致。

**实现**：`Game.IsMultiplayer()` 在主机/加入/loopback 均为 true，`GetNetMode()` 返回 `direct/loopback/client`，`networkClient` 优先。`ApplyDamage/AdjustNeeds/GiveXp/Craft.Learn/Build.CanPlace` 在 loopback/client 下返回 `ok=false` 且状态不变；`World.EmitSound` 追加非 direct 拒绝，避免只在本地客户端派发声音造成 AI/音频分歧。命令化动作仍提交服务器；v1 的 `Teleport/AddItem/SpawnItem/Zombies.Spawn` 保持本地兼容行为并文档化。`Mod.Data` 属 F4-T2，当前未暴露，未添加无效占位；其非 direct 写拒绝/读 nil 契约保留到 F4-T2 落地时补测。

**测试**：`test_script_api`、M7、M8 分别覆盖 loopback/client；`M35-F3-T7*` Debug/Release **2/2（49 assertions）**、Python **3/3（60 assertions）**，`M35*` Debug/Release **22/22（913）**、Python **23/23（924）**。Release 全量排除既有 `M30-T6`/`M31-T9` flake 后 **1112/1112（144390 assertions）**；Debug/Release/Python 三套 `ZomboidLike` 构建通过。

**限制/下一步**：`Mod.Data` 的联机拒绝用例随 F4-T2 补；命令化动作的服务器端真实结果仍由既有 HUD/事件验收。下一步 M35-F4-T1 `ModData` 组件与 codec。

## 2026-11-20 M35-F3-T6 僵尸变体与声音引怪

**范围**：`Zombies.Spawn(x,y,z,variant?)` 可选变体；`World.EmitSound(x,y,radius,kind?,clip?)` 本地世界声音。

**实现**：`SpawnZombie` 严格解析 `normal/fast/strong/armored`，未知变体或非有限坐标返回 0；生成继续复用 `game::SpawnZombie`，HP/速度/缩放/色调/硬直条沿用现有变体配置，旧三参调用默认 `normal`。`EmitSound` 校验 `(0,256]` 半径与声音类型，构造 `core::SoundEvent`；有 `SoundManager` 时走 `EmitWorldSound`，无音频时回退 `OnSound` 事件总线，联机不广播。Lua/Python 绑定同步追加可选参数；脚本与 AI 通过既有 `OnSound` 订阅消费。

**测试**：`test_script_api` 新增四变体 HP/速度断言、默认/非法变体、非有限坐标、声音半径/类型/clip、`OnSound` 载荷与 PerceptionSystem 可闻；M7/M8 补双后端调用。`M35-F3-T6*` Debug/Release/Python 均 **1/1（58 assertions）**，`M35*` **20/20（864）**，`M7-T3*` **6/6（154）**，`M8-T3*` **6/6（111）**。Release 全量重试命中既有 M30-T6 闪避、M31-T9 性能阈值与 M23 网络时序抖动；对应失败用例均不经过 F3-T6 路径，排除 `M30-T6`/`M31-T9` 两组既有 flake 后 **1110/1110（144340 assertions）**，F3-T6 定向与 `M35*` 全绿。

**下一步**：M35-F3-T7 联机策略与双后端收口。

## 2026-11-19 M35-F3-T5 建造动作

**范围**：`Build.CanPlace(defId,x,y,vertical?) / Place / Demolish / Repair / ToggleDoor`。CanPlace 仅单机只读预检；Place/Demolish/Repair/ToggleDoor 单机直调 `WorldDemoState`，loopback/KCP 走 `PlaceStructure/DemolishStructure/RepairStructure/ToggleDoor`。

**实现**：`GameContext` 新增 5 个建造 hook，`WorldDemoState` 进入赋值/退出清空。`Build.Place` 解析 `defId`→目录 index、拒绝不支持竖向项；Direct 复用原建造路径（距离/地面/占用/任务/系统开关/木材、设施生成、建筑 XP、`OnBuildPlaced`），MP 命令带 `index` 与 `flags bit0=vertical`。`Demolish/Repair/ToggleDoor` 新增按格坐标的 Direct 路径：按 Wall/Furniture/Floor 顺序找玩家结构，校验距离/所有权/耐久/门口占用；拆除支持地图结构与玩家设施/仓库，维修支持结构/泛光灯。`Demolish` 的 MP 命令用 flags=0 立即执行，不进入 UI 两次确认。Lua/Python `Build` 增加同名 5 个方法。

**测试**：`test_script_api` 覆盖 SP hook、CanPlace 占用错误、竖向校验、MP 命令字段/index/排队、无玩家拒绝；M7/M8 补双后端调用。`M35-F3-T5*` Debug/Release/Python 均 **1/1（56 assertions）**，`M35*` **19/19（806）**，`M7-T3*` **6/6（146）**，`M8-T3*` **6/6（100）**，Release 全量 **1113/1113（144366 assertions）**。

**下一步**：M35-F3-T6 僵尸变体与声音引怪（`Zombies.Spawn` 可选 variant、`World.EmitSound`）。

## 2026-11-19 M35-F3-T4 制作动作

**范围**：`Craft.Start(recipeId,count?) / Craft.Cancel() / Craft.Learn(recipeId)`。Start/Cancel 走现有 `InventoryActionType::Craft/CancelCraft` 命令，Direct 立即执行、loopback/KCP 排队；Learn 仅单机。

**实现**：`StartCraft` 预检数量 1~99、配方存在、`recipeKnown`、`craftStationNearby`、`items::CanCraft`（材料/工具/锅具/燃料/输出空间），按 registry 顺序取 `fromIndex`，构造 `InventoryActionType::Craft` 命令（flags 高字节=数量）。`CancelCraft` 在 `CraftingJob`/UI 队列均为空时返回 `ok=false`；`LearnRecipe` 仅 Direct，未知/已会/联机拒绝，成功追加 `KnownRecipes` 并排序去重。`GameContext` 新增 `craftStationNearby`，`WorldDemoState` 进入赋值/退出清空，避免在脚本层复制制作站判定。Lua/Python `Craft` 增加 `Start/Cancel/Learn`，结果表/dict 复用 `{ok,queued,error}`。

**测试**：`test_script_api` 覆盖未知/锁定/已会/缺材料/无站点/数量边界、Craft/Cancel 命令字段与 loopback 排队；M7/M8 补双后端调用与命令映射。`M35-F3-T4*` Debug/Release/Python 均 **1/1（38 assertions）**，`M35*` **18/18（750）**，`M7-T3*` **6/6（135）**，`M8-T3*` **6/6（95）**，Release 全量 **1112/1112（144299 assertions）**。

**下一步**：M35-F3-T5 建造动作（`Build.Place/Demolish/Repair/ToggleDoor`）。

## 2026-11-19 M35-F3-T3 单机直接状态效果

**范围**：`Player.ApplyDamage/AdjustNeeds/GiveXp`，仅 Direct；loopback/KCP 直接返回 `ok=false`，不排队。为撤离计时、赏金奖励、脚本惩罚提供最小写入口。

**实现**：`ApplyDamage` 解析 `blunt/bite/claw/fire/debug`，拒绝 `<=0`/NaN/Inf，保持 `CombatSystem::ApplyDamage` 单一伤害管线（护甲/格挡/闪避/伤口/受击反馈/OnDamage 全部复用）。接口无方向参数；有 `Facing` 时把事件方向设为正面来袭，使格挡/弹反可参与，`Debug`/`Fire` 仍不可格挡；`ok=true` 只表示进入管线，闪避/弹反时实际扣血可为 0。`AdjustNeeds` 只改 `hunger/thirst/fatigue`，非负有限增量后钳制 [0,1]，不触发营养/中毒或派生字段。`GiveXp` 校验技能/成长配置后调用 `AwardXp + RefreshDerived`，返回 `characterLevels/skillLevels/perkPoints/characterXp/skillXp`；Lua/Python 均绑定同名同参数与结果表/dict。

**测试**：`test_script_api` 覆盖 MP 拒绝、无玩家/无战斗/无成长配置、非法伤害类型与数值、护甲减伤、正面格挡、闪避、Debug 穿透、死亡目标、需求钳制/负值/NaN、经验奖励与非法输入；Lua M7-T3/Python M8-T3 各补直接效果调用与 C++ 状态校验。`M35-F3-T3*` Debug/Release/Python 均 **1/1（59 assertions）**，`M35*` **17/17（712）**，`M7-T3*` **6/6（126）**，`M8-T3*` **6/6（89）**，Release 全量重跑 **1111/1111（144252 assertions）**。

**下一步**：M35-F3-T4 制作动作（`Craft.Start/Cancel/Learn`）。

## 2026-11-19 M35-F3-T2 命令化物品动作

**范围**：把 `Player.UseItem/EquipItem/Unequip/DropItem/PickupNearest/Pickup/Attack` 接到 F3-T1 的 `SubmitCommand` 上，不新增命令类型，不改服务器协议。

**实现**：7 个 `ScriptApi` 动作分别映射 `UseBag/EquipBag/Unequip/DropBag/PickupNearest/PickupEntity/Attack`；`index` 0-based、`-1` 为第一件/第一堆；`slot` 接受 `primary/body` 或 `0/1`，Lua/Python 绑定层统一转换；重击写 `kAttackHeavyFlag`。提交前 fail-closed：空背包/非法索引、非法槽位、`Pickup(0)`、攻击目标不存在/自己/非 AI 或动物/死亡/尸体/`Dying`/闪避无敌均返回 `ok=false`。单机实际效果仍由既有 `ExecutePlayerCommand`/库存与战斗逻辑执行，`ok=true` 只代表命令被接受；loopback/KCP 返回 `queued=true`。Lua/Python 均返回 `{ok,queued,error}`。

**测试**：`test_script_api` 覆盖 7 条命令的 type/payload、默认 `-1`/primary、越界索引、非法槽位、重击标记、尸体/无敌/死亡目标与 loopback 入队；`test_lua_runtime`/`test_python_api` 覆盖双后端结果表及命令映射。`M35-F3-T2*` Debug/Release/Python **1/1（50 assertions）**，`M35*` **16/16（653）**，`M7-T3*` **6/6（117）**，`M8-T3*` **6/6（84）**，Release 全量 **1110/1110（144184 assertions）**。真实库存效果沿用既有 `test_network_inventory`。

**下一步**：M35-F3-T3 直接状态效果（`Player.ApplyDamage/AdjustNeeds/GiveXp`，SP only）。

## 2026-11-19 M35-F3-T1 动作结果与命令路由

**范围**：先建脚本写动作的统一返回与命令下发基础设施，不新增脚本可见的 `Player.*` 动作；F3-T2 起在此之上绑定物品动作。

**实现**：新增 `ScriptActionResult{ok,queued,error}`；`ScriptApi::SubmitCommand` 只负责填当前玩家 `playerStableId` 与 `GameLoop::TotalTicks()`，然后调用 `GameContext::submitPlayerCommand`，不在脚本层判断网络模式。`WorldDemoState` 在 OnEnter 注入该入口、OnExit 清理；新增 `game/PlayerCommand.{h,cpp}`：direct 同步执行、loopback 入批、KCP 走 `NetworkClient::SendCommand`，成功时 loopback/KCP 返回 `queued=true`，无玩家/无入口/无管线/未连接均返回 `ok=false` 与可读错误。`WorldDemoState::SubmitPlayerCommand` 保留原 bool 重载给既有 UI 调用，新重载透传 `queued/error`。

**测试**：`test_script_api` 新增 F3-T1 用例，覆盖未绑定/无玩家/无入口失败、字段填充与覆盖、direct 立即执行、loopback Flush/Pump 前不执行且 `queued=true`、KCP 分支优先于本地管线及未连接错误透传。`M35-F3-T1*` Debug/Release/Python 均 **1/1（35 assertions）**，`M35*` **15/15（603 assertions）**；Release 全量 **1109/1109（144114 assertions）**。Debug/Release/Python 三套应用目标构建通过。KCP 成功发送沿用既有 M12/M13 `NetworkClient` 网络用例。

**下一步**：M35-F3-T2 在此 helper 上绑定 `Player.UseItem/EquipItem/Unequip/DropItem/PickupNearest/Pickup/Attack`，并接真实 `ExecutePlayerCommand`。

## 2026-11-19 M35-F2-T6 世界生命周期

**范围**：给脚本一个真正的世界开局/退出时机；不改变存档格式、世界生成或联机协议。F2 事件层 T1–T6 至此完成。

**实现**：`GameEvents.h` 新增 `kOnWorldStart/kOnWorldExit` 与 `WorldStartEvent{saveId,isNewGame,day,playerId}`、`WorldExitEvent{saveId}`，加入 `EventBus` variant；新增 `game/WorldLifecycle.h` 一次性状态机（Start 需 playerId 非 0，Exit 只在 Start 后）。`WorldDemoState` 在玩家实体生成/读档恢复后的首个 `OnTick` 顶部同步发 `OnWorldStart`，`OnExit` 开头、自动存档提交前同步发 `OnWorldExit`；单机暂停菜单的“返回主菜单”覆盖层不算退出，应用进程退出因 EventBridge 先释放不保证收到 Exit。联机客户端同样发，`saveId` 为空、`isNewGame=false`。`EventBridge` 事件键增至 30 个；`OnGameStart` 的文档语义纠正为“应用启动、脚本加载后（可能仍在主菜单，玩家未生成）”。

**测试**：`test_event_bridge` 增加两事件字段断言与 `M35-F2-T6 世界生命周期事件只发一次`，覆盖未生成玩家不发、读档/新游戏字段、同步派发、重复 Start/Exit、Reset 与空总线。`M35-F2-T6*` Debug/Release **1/1（32 assertions）**，`M35*` **14/14（568 assertions）**，`M7-T4*` **5/5（187 assertions）**；Python `M8-T3*,M8-T5*` **6/6（72）**、`M35-F2-T6*` **1/1**、`M7-T4*` **5/5**。Release 全量 **1108/1108（144079 assertions）**；Debug 本轮不再跑全量（上一任务已记录既有联机 `PumpUntil` 超时），F2-T6 定向 Debug 全绿；Debug/Release/Python 构建通过。

**限制/下一步**：`Mod.Data` 本体属 F4；本任务保证 `OnWorldStart` 在存档实体恢复后同步派发，F4 落地后可直接在该事件读取。`OnWorldExit` 不带退出原因；MP 的 `saveId` 为空。下一步 M35-F3-T1 动作结果与命令辅助。

## 2026-11-19 M35-F2-T5 建造事件

**范围**：让单机脚本知道建造、拆除、维修成功的位置和对象；不改变建造规则、材料/返还、维修量、存档或网络协议。

**实现**：`GameEvents.h` 新增 `kOnBuildPlaced/kOnBuildDemolished/kOnBuildRepaired` 与 `BuildPlacedEvent{playerId,defId,x,y,z,vertical}`、`BuildDemolishedEvent{playerId,defId,x,y,z}`、`BuildRepairedEvent{playerId,x,y,z}`，加入 `EventBus` variant；新增 `game/BuildingEvents.h` 统一排队派发。`WorldDemoState` 单机放置（设施/仓库/普通对象）、拆除（地图结构/仓库/设施）和维修（泛光灯/单结构/维修全部）成功后发事件；网络路径不发，拆除首次确认、门槛拒绝和维修失败不发。`OnBuildRepaired` 对“维修全部”的每个实际修复结构各发一条；`OnBuildPlaced.vertical` 为实际朝向（不支持竖向的建造项恒为 false）。`EventBridge` 事件键增至 28 个。

**测试**：`test_event_bridge` 增加 3 类事件字段断言；`test_building` 新增 `M35-F2-T5 建造事件载荷与门槛`，覆盖排队/载荷、空总线、占用与归属门槛。`M35-F2-T5*` Debug/Release **1/1（31 assertions）**，`M35*` **13/13（536 assertions）**，`M7-T4*` **5/5（180 assertions）**；Python `M8-T3*,M8-T5*` **6/6（72 assertions）**，`M35-F2-T5*` **1/1**，`M7-T4*` **5/5**。Release 全量 **1107/1107（144041 assertions）**；Debug 全量本轮受既有联机 `PumpUntil` 超时影响，15 分钟中止未产生汇总（失败均不在建造事件路径），F2-T5 定向 Debug 全绿；Debug/Release/Python 构建通过。Release 前两次全量中 `M31-T9 1000+ 脏 chunk` 的 `snapshotMax < 5ms` 阈值出现 20.16/5.10ms 抖动，单独与第三次全量通过。

**限制/下一步**：三个事件 M35 只在单机派发，联机服务器建造/拆除/维修留给 M36；`OnBuildRepaired` 不含 `defId`，脚本需要对象类型时可按格坐标查询/等 M36 扩展；车辆/工具维修不归此事件。下一步 F2-T6 世界生命周期。

## 2026-11-19 M35-F2-T4 制作事件

**范围**：让单机脚本知道一个制作批次何时开始、实际开始多少件、批末完成/失败/取消以及批内完成件数；不改变制作耗时、材料扣除、工具磨损、经验或网络协议。

**实现**：`GameEvents.h` 新增 `kOnCraftStart`/`kOnCraftFinished`、`CraftStartEvent{playerId,recipeId,outputDefId,outputCount,batchCount}` 与 `CraftFinishedEvent{playerId,recipeId,outputDefId,outputCount,batchCompleted,failed}`，并加入 `EventBus` variant；`EventBridge` 翻译字段，事件键增至 25 个。`PlayerInventoryService::StartCraft` 在成功启动后发 Start，`TickCrafting` 在整批成功、材料中断/制作失败时发 Finished，`Dispatch CancelCraft` 对活跃批次发 `failed=true`；`WorldDemoState` 的单机 UI 路径同样覆盖开始、批末完成、失败中断与取消。`batchCount` 为实际开始件数（可能被材料/空间截断）；`OnCraftFinished` 每批只发一次，`batchCompleted` 为批内已完成件数，成功总产量为 `outputCount * batchCompleted`；无效 Start 不发 Start，仅队列取消（无活跃批次）不发 Finished，读档恢复不补发 Start。

**测试**：`test_event_bridge` 增加两个事件的字段断言；`test_inventory_service` 新增无效 Start、批量开始、首批完成、材料中断、整批完成、取消中断用例。`M35-F2-T4*` Debug/Release **1/1（42 assertions）**，`M35*` **12/12（505 assertions）**，`M7-T4*` **5/5（162 assertions）**，`M13-T7*` **7/7（327 assertions）**；Python `M8-T3*,M8-T5*` **6/6（72 assertions）**。Release 全量 **1106/1106（143991 assertions）**；Debug 全量 **1100/1106**，失败为既有网络/性能时序（M31-T9 soak、X-F1 建筑格、M22 睡眠、M23-F2a-T6 觅食、M26-V2 载具碰撞、X-F1 2b-2b），均不经过制作路径；Debug/Release/Python 构建通过。全量日志 `build/m35_f2_t4_full_release.log`、`build/m35_f2_t4_full_debug.log`。

**限制/下一步**：两个事件 M35 只在单机派发，联机服务器脚本与 M36 的事件同步不做；`OnCraftFinished` 不包含失败原因/工具损坏列表，脚本需要提示时读 HUD/状态或等待后续字段；队列项只有在真正启动时才发 Start。下一步 F2-T5 建造事件。

## 2026-11-19 M35-F2-T3 使用与搜刮事件

**范围**：让单机脚本知道玩家成功使用了什么、剩余多少，以及哪个容器首次被搜刮、产出多少；不改变物品效果、搜刮 roll、任务/经验或网络协议。

**实现**：`GameEvents.h` 新增 `kOnItemUsed`/`kOnLootSearched`、`ItemUsedEvent{playerId,defId,spoiled,remaining}` 与 `LootSearchedEvent{playerId,containerId,tableId,itemCount}`，并加入 `EventBus` variant；`EventBridge` 翻译字段，事件键增至 23 个。`WorldDemoState` 在 UI/快捷键/蓝图三条成功使用路径和首次搜刮路径排队派发；`PlayerInventoryService` 在共用的成功使用/首次搜索路径也派发，供服务器/单测使用。`OnLootSearched` 只在 `searchedNow=true` 时发，重复搜索、失败、未知容器不发；普通消耗品只在 `UseResult.applied` 后发，蓝图在消耗并学会配方后发，`spoiled=false`，`remaining` 为使用后槽位剩余数量（替换物也计 1）。

**测试**：`test_event_bridge` 增加两个事件的字段断言；`test_inventory_service` 新增成功/失败/耗尽 `OnItemUsed` 用例；`test_loot` 新增首次搜索发一次、重复/失败不重发 `OnLootSearched` 用例。`M35-F2-T3*` Debug/Release **2/2（35 assertions）**，与 F2-T1/T2 组合 **4/4（86 assertions）**，`M35*` **11/11（463 assertions）**；`M7-T4*` Debug/Release **5/5（149 assertions）**；Python `M8-T3*,M8-T5*` **6/6（72 assertions）**。Release 全量 **1105/1105（143936 assertions）**；Debug 全量 **1103/1105**，失败为既有网络时序（X-F1 建筑格、M13-T5 移动复制），均不经过使用/搜刮路径；Debug/Release/Python 构建通过。

**限制/下一步**：两个事件 M35 只在单机派发，联机服务器脚本与 M36 的事件同步不做；`OnItemUsed` 不包含具体效果结果，脚本需读状态或自行累计；`OnLootSearched` 不覆盖动物尸体收获。下一步 F2-T4 制作事件。

## 2026-11-19 M35-F2-T2 伤害事件 OnDamage

**范围**：让脚本在伤害真正扣血后收到一条结构化事件，用于伤害统计、受击反馈与赏金/战斗日志；不改变护甲、格挡、闪避、伤口和死亡流程。

**实现**：`CombatSystem` 新增 `DamageAppliedCallback`，在 `ApplyDamage` 扣血后回传 `targetId/sourceId/type/amount/remainingHp/lethal/blocked`；`WorldSimulation` 转成 `core::DamageEvent`，排队派发 `OnDamage`；`EventBridge` 输出小写 `damageType`（`blunt/bite/claw/debug/fire`）与 `cause`。零伤害、闪避、弹反、完全格挡不发；普通格挡的剩余伤害发且 `blocked=true`；`remainingHp` 钳制到 0。

**测试**：`test_combat_m30` 新增 `M35-F2-T2`，覆盖轻击、普通格挡剩余伤害、闪避不发、火焰、零伤害不发与致死；`test_event_bridge` 增加 `OnDamage` 字段翻译断言。定向 Debug/Release `M35-F2-T2*,M35-F2-T1*,M7-T4*` 均 **7/7（190 assertions）**，Python `M8-T3*,M8-T5*` **6/6（72 assertions）**；Debug/Release/Python 构建通过。全量 Debug 当前机器出现既有网络/性能时序失败（`M26-V2`、`X-F1 2b-2b` 隔离通过；`M4-T6`/chunk/soak 性能阈值受负载影响；`X-F1 2b-4` 仅 Debug 5s 超时、Release 通过），未改网络/换弹路径。

**限制/下一步**：`OnDamage` 仅单机派发（M36 再统一联机脚本事件）；回调按实际扣血触发，火焰持续伤害会每个伤害 tick 产生一条，脚本侧自行聚合。下一步 F2-T3 使用与搜刮事件。

## 2026-11-19 M35-F2-T1 死亡归属与动物击杀事件

**范围**：补齐死亡事件的击杀者与死因，不改变死亡流程、倒地窗口或尸体生成时机。

**实现**：`DeathSystem` 在转尸体时读取 `ecs::LastDamage`：`OnZombieDead.killer` 填 `sourceId`，`OnPlayerDeath` 追加 `sourceId/cause`；新增 `core::kOnAnimalKilled` 与 `AnimalKilledEvent{animalId,speciesId,killerId,x,y,z}`，动物不再误发 `OnZombieDead`。`DamageCauseName` 提供脚本稳定小写名（`bite/claw/starvation/infection/...`），日志仍用中文标签。`PlayerEvent` 只追加字段，旧构造与 v1 脚本字段不变。

**测试**：`test_death` 新增 `M35-F2-T1`（玩家环境死亡 sourceId=0/cause=starvation、僵尸 killer 非 0、动物单独事件）；`test_event_bridge` 扩展翻译与 `M7-T4` 死亡链路；`test_needs` 补环境伤害清旧 `sourceId`。全量 Debug **1102/1102（143835 assertions）** 绿；Release 全量 **1101/1102**，唯一失败为既有 `M30-T6` 闪避计时（`test_network_integration`，隔离复跑 **2/2（56 assertions）** 通过）；Python `M8*` **11/11（101 assertions）**。Debug/Release/Python 构建通过。

**限制/下一步**：`LastDamage` 仍是瞬时组件，不参与存档；饥饿/脱水/感染/失温/中毒/流血的写入点已显式把 `sourceId` 归零，避免继承旧攻击者。下一步 F2-T2 `OnDamage`。

## 2026-11-19 M35-F1 脚本 API v2 只读观测层

**范围**：`ScriptApi::kApiVersion` 升到 2，保持 v1 函数与 mod 兼容；补齐脚本判断玩法所需的状态读取，不新增写动作、事件或存档格式。设计见 `docs/23-M35脚本API与内容化扩展.md`。

**实现**：`Game` 增加 `GetApiVersion/IsMultiplayer/GetNetMode`；`Player` 增加需求/体力/伤口/疼痛/感染/体温/成长/背包/装备/计数查询；`World` 增加天气/当天进度；新增 `Entities.Get/Query/GetContainer`、`Items.GetInfo`、`Craft.List/GetState/IsKnown`、`Build.List`、`Mission.List/Get`。`Craft.*` 优先复用 `GameContext::getInventoryState`（与 HUD 同源），无 UI 时回退注册表/`KnownRecipes`/`CanCraft`/`CraftingJob`；`GameContext` 增加 `progressionConfig` 与 `recipeKnown`，由 `WorldDemoState::OnEnter` 赋值、`OnExit` 清空。Lua/Python 同步绑定，返回 table/dict。

**测试**：`test_script_api` 新增 7 个 M35-F1 用例（377 assertions）；`test_lua_runtime`/`test_python_api` 各补 v2 烟测；`M35-F1-T*` Debug/Release 7/7，`M7-T3*` 6/6（Lua）与 `M8-T3*` 6/6（Python）。全量 Debug 首轮 6 失败：1 个 `test_mod_loader` 默认 api 断言已从 1 改为 2；其余 5 个为 `test_network_integration` UDP 超时，未改网络路径，需在干净工作区复验。Debug/Release 构建通过。

**限制/下一步**：F1 只读；`Player.UseItem/ApplyDamage/GiveXp` 等写动作在 F3，行为事件在 F2，`Mod.Data`/`Json`/`require` 在 F4。联机写 API 仍按规划 fail-closed。

## 2026-11-19 构建加速：Core 静态库、PCH 与快速测试目标

**范围**：解决 M34 迭代中全量 C++ 构建耗时明显、单测与 app 重复编译 168 个生产源的问题，不改游戏逻辑、协议、存档或内容指纹。

**实现**：CMake 将 app/test 共用的 168 个源抽成 `ZomboidLikeCore` 静态库；`ZomboidLike` 仅保留 18 个 app 专属源（main、GameApp、states、render、UiSystem 等），`ZomboidLikeTests` 只编译 140 个测试源，二者链接 core。新增 `src/ZomboidLikePch.h`（标准库 + entt/glm/nlohmann/spdlog）并给 core/app/tests 配置预编译头；新增 `EXCLUDE_FROM_ALL` 的 `ZomboidLikeTestsFast`，只编 7 个投掷测试源 + core，用于快速回归。Python 宏与运行时库复制改为 core PUBLIC 传播，原有 `ZL_ENABLE_PYTHON=OFF/ON` 边界不变；同时修复 `x64-debug-python` / `x64-release-python` 未设独立 `binaryDir`、与普通 Debug/Release 共用 build 目录导致切换 Python 后全量重编的预设问题。

**验证**（i7-10700，`-j12`，PCH 冷启动）：Debug app **190/190 步 80.6s**（core 168 + app 18 + 链接），Debug 全量测试 **142/142 步 67.4s**（core 复用），快速测试目标首次 **44.5s**、PCH 重建后增量 **16s**；Release app **173.3s**、Release 全量测试 **193.7s**。基线不再要求每次构建 `all`；全量测试 Debug **1094/1094（143405 assertions）**、Release **1094/1094（143419 assertions）** 绿；`M34-F2-T7*` **2/2（100）**；快速目标 **18/18（730）**。Debug 首轮全量命中既有 `X-F1 2b-2b` 世界重启时序 flake，隔离该组 **6/6（139）** 后全量复跑绿。Python 独立目录配置 **13.1s**、Python app **190/190 步 72.2s** 编译通过；Python API 测试 SIGSEGV 仍为既有环境问题，不在本次构建改动内。

**限制/下一步**：PCH 每个目标约 289MB，属构建产物且不入库；首次构建仍需编译 168 个 core 源，后续共用 core 的增量修改只需一次编译 + 链接。下一次先按此结构做 F3-T1；若继续追求增量速度，再把 `ServerHost.cpp` / `WorldDemoState.cpp` 的热点头文件继续解耦。

## 2026-11-19 M34-F2-T7 F2 回归、双客户端与文档

**范围**：收口 F2 投掷。补跨 F2 的定向/全量回归、双客户端 1280 视觉联调、`throwable` 契约文档，并记录 v22 内容指纹、`ThrowItem=62` 与旧档/旧客户端限制。

**实现/验证**：新增 `tests/test_throw_regression.cpp`：一个可复用的夹具用例生成 A 的 8 石块/8 简易长矛/2 燃烧瓶、静态靶、木屋站位和 `players.bin`/世界存档；另一个真实 UDP/KCP 双客户端用例验证石头 `Impact` 噪声让 AI 监听者更新 `lastKnownX/Y` 并提升警觉、B 收到远端落点音效；长矛命中静态靶结算 28 伤害并在 roll=0.541 时回收，B 的 ZLR1 健康与地面长矛一致；燃烧瓶在木地板点燃 8/9 格，B 看到 `FireSource`。排查联机 1280 烟测时发现快捷栏选择只在服务器校验、客户端本地 `m_hotbarSelected` 不更新，导致按 2/3 仍投槽 1；现 `SelectHotbar` 发送成功后立即更新本地选中槽，服务器仍只做索引合法性校验。`M34-F2-T7*` **2/2（100 assertions；夹具环境 125 assertions）**，定向 **81/81（4710）**，Release 全量 **1094/1094（143419 assertions）**（首轮 M30-T6 闪避超时 flake，隔离 **2/2** 后复跑绿），Debug 全量 **1094/1094（143405 assertions）**，Python **123/123**。Release 1280 双客户端烟测 `build/m34_f2_t7_dual/`：A/B 均 1280×720，A 石块命中（伤害 8）、简易长矛命中并回收（伤害 28）、燃烧瓶点燃 8/9 格；B 日志三次 `网络落点音效`，B 截图同步看到命中/地面长矛/火区；6 张截图、server/A/B stderr 0，临时脚本 `build/m34_f2_t7_dual_smoke.ps1` 不入库。

**记录/限制**：内容指纹 v22；无 mod（空 mods 目录）启动指纹 `0x2d8e84bb23e35479`，带 core_ui+探针的烟测 `0x49d19e1384ee7aeb`（mod 文件树 `0x3c76f75955ea3f7e`）。`ThrowItem=62`、`SetStealth=61` 不变；旧客户端命令/内容哈希不兼容，握手拒绝；旧存档无新增 schema，飞行投掷物在保存/退出前结算、冷却不持久化。1280 烟测靶为夹具静态实体，石头引怪由 AI 监听者单测覆盖；B 只看到落地音效和复制的生命/火焰/地面物品，A HUD 仍显示“请求已发送”。F2 无阻塞，下一步 **F3-T1**。

## 2026-11-18 M34-F2-T6 投掷内容、UI 与平衡

**范围**：固定石块/简易长矛/骨矛/燃烧瓶的射程、伤害、噪声、回收/损坏和火焰数值；给原生 HUD/背包与 `core_ui` Lua/Python 补投掷标记、射程/回收提示、冷却和最近结果；把燃烧瓶材料/成品获取路径写进数据并做用途/成本平衡测试。

**实现**：`HudSlot`/`InventorySlot` 增加投掷字段，`ThrowableHotbarTag`/`ThrowableInventoryTag` 生成紧凑标记，原生 `UiSystem`/`InventoryPanel` 与 `mods/core_ui` 的 Lua/Python 同步显示快捷栏 `投7.5 回95%`、背包/地面/装备 `可投7.5 回95%` 和 HUD `可投掷：石块 · 射程 6 格 · 回收 95% · 冷却 0.3/0.7s · 上次：命中 · 回收`。单机落地写 4 秒最近结果；联机只显示本地冷却与“请求已发送”，不伪造服务器结果。`poi_gas_station` 新增 weight 1 的成品燃烧瓶，空瓶/破布/燃料罐继续使用既有 loot 表；未新增货币、命令或存档字段。

**验证**：`M34-F2-T6*` **2/2（96 assertions）**，Debug/Release 各通过；定向 `M34-F2*`/`M34-F1*` + 相关 M23/M12/UI **79/79（4610）** Debug 全绿；Release 全量 **1092/1092（143319 assertions）**、Python **123/123**；Debug 全量 **1092/1092（143306）** 全绿。Release 1280 core_ui 烟测 `build/m34_f2_t6_ui/`：探针放入石块×2/简易长矛×4/燃烧瓶×1，日志确认背包标签、HUD `cool=0.57/0.70`、第二块石头和长矛 `结算=回收`、燃烧瓶点燃 `8/9` 格；6 张截图 `m34_f2_t6_inventory/aim/cooldown/stone_land/spear_recover/molotov_fire.png`，stderr 0，临时脚本 `build/m34_f2_t6_ui_smoke.ps1` 不入库。

**限制/下一步**：背包 64×64 格使用紧凑标记，完整说明在 HUD；联机没有逐投掷服务器结果回包，F2-T7 再做双客户端 1280 结果核验。烟测使用探针传送/加物品，第一块石头固定 roll=0.023 损坏，第二块展示回收；不是自然搜刮。内容版本仍 v22，`assets/loot.json` 变更后运行时哈希为 `0xd8b6b4d816d4cae0`。F2-T6 无阻塞，下一步 **F2-T7**。

## 2026-11-18 M34-F2-T5 联机投掷权威、在飞账本与双客户端复制

**范围**：把 T3/T4 的单机投掷接入服务器权威：新增 `ThrowItem=62`，服务器校验来源/射程/冷却/存活/倒地/驾驶/落点并把物品扣进在飞账本；客户端只做瞄准和本地视觉飞行，远程客户端通过落点音效与现有生命/火源/地面物品复制看到结果。不做远端逐帧投掷物、PvP 投掷或投掷 UI/平衡。

**实现**：`CommandType` 扩到 62（`ThrowItem`，`index=-1` 主手、`>=0` 背包，`x/y` 世界落点，其余 payload 必须默认），`CommandTypeName`/`IsValidCommandType` 和命令包往返同步。`ServerHost` 新增 `ThrowItemAction`、`ServerThrow` 在飞账本、`UpdateServerThrows`、`SettleServerThrows`：开始校验 `throwable` 来源、射程/最小距离、墙体/加载格、存活/倒地/驾驶和服务器冷却，成功后扣 1 件并记录飞行时间（0.18–1.2s，服务器 RNG）；落地选最近有效目标 `CombatSystem::ApplyDamage`、发送 `Impact` 噪声、调用现有 `FireSystem::IgniteAt`、按服务器 roll 生成回收/损坏/消耗，并广播 `WorldSoundEvent`。`RemoveSession`、`SaveWorld`、`Shutdown` 强制结算，掉线/重连/关机不复制物品；生命、火源、地面物品继续走 ZLR1/实体复制。`WorldDemoState` 联机 `V` 改为发送 `ThrowItem` 并生成 `visualOnly` 本地飞行，不扣件、不本地结算伤害，库存以 `PlayerState` 回流为准；单机 T3/T4 路径不变。

**验证**：新增 `tests/test_throw_network.cpp`，用真实 UDP/KCP 双客户端验证：A 连续投石块，B 看到目标生命下降、至少一个地面石块和落点 `Impact` 音效；燃烧瓶在木地板目标点燃 8/9 格，B 看到 `FireSource`；同一命令连续发送只启动 1 次且 `throwsRejected` 增加；A 在飞状态断线，服务器结算为地面物品，重连后背包只剩 1 件且 `InFlightThrowCount=0`。`M34-F2-T5*` **1/1（135 assertions）**，Debug 重复 3 次、Release 1 次通过；定向 `M34-F2*`/`M34-F1*` + 相关 M23/M12 **57/57（2225）**；Release 全量 **1090/1090（143220 assertions）**。Debug 全量本轮 **1088/1090（143204/143206）**，失败为既有 `M5 500 僵尸 tick 计时` 和 `M31-T9 快照写盘` 性能时序 flake，失败不涉及 T5。

**限制/下一步**：不做远端逐帧投掷物复制，远程客户端只看到落地音效/结果及复制的生命/火焰/地面物品；目标选择沿用 T3，排除玩家/友方/`NoCombatTarget`，没有新增 PvP 投掷；服务器冷却只在会话运行时保存，重连会重置；测试使用传送空地和临时代码设置木地板，不是 1280 画面双客户端烟测；投掷 HUD/背包标记和最终平衡留 F2-T6，F2-T7 再做 1280 双客户端与全量回归。F2-T5 无阻塞，下一步 M34-F2-T6。

## 2026-11-18 M34-F2-T4 燃烧瓶火区与火源存档联动

**范围**：把燃烧瓶的 `throwable.fire` 接到现有 M23 火焰系统：落地范围内创建/点燃 `FireSource`，按物品数据覆盖燃烧时长；不做第二套火焰状态、联机投掷或投掷 UI/平衡。

**实现**：`Throwing` 新增 `ThrowFireAreaCells`（格心半径、中心优先、最多 3 格）与 `ThrowFireBurnSeconds`（游戏分钟按 `game.dayLengthMinutes` 换算现实秒）；`ThrowAimRuntime`/`ThrownProjectile` 快照 `fireRadius/fireMinutes`。`WorldDemoState` 在投掷落地伤害/噪声后逐格调用 `FireSystem::IgniteAt`，传入天气湿度和玩家 owner；新点燃的 `FireSource` 覆盖 `burnDuration/burnRemaining`，已存在的活跃火源不覆盖，无易燃格/过湿只告警且不生成回收物。火焰继续走 `FireSource`/`FireAnchor` 的 M23 点燃、伤害、蔓延、存档与联机组件；owner 仅运行时用于守城贡献，不写存档。燃烧瓶仍为不可回收/100% 损坏，不产生地面物品。

**验证**：新增 `tests/test_throw_fire.cpp` 3 个用例，覆盖范围/时长换算、无易燃格与潮湿失败、火区伤害/僵尸燃烧/蔓延、生成火源实体快照往返；`M34-F2-T4*` **3/3（171 assertions）**，T2+T3+T4 定向 **11/11（291）**，T2/T3/T4+相关 M23 **29/29（541）** Debug/Release 均绿。Release 全量 **1089/1089（143071 assertions）**、Python 工具 **123/123**。Release 1280 烟测 `build/m34_f2_t4_fire/`：传送到木地板房屋后投掷燃烧瓶，日志 `投掷火焰：范围 1.5 格，持续 45.0 游戏分钟（112.5s），点燃 6/9 格`、`投掷落地：燃烧瓶…火焰 6/9`；玩家受到火焰伤害 100000→99885；最终烟测落点为墙前、未命中僵尸，火区对僵尸的命中/持续燃烧与蔓延由 T4 单测覆盖；2 张截图、stderr 0。

**限制/下一步**：范围按格心收集，稀疏地表不会强行点燃无易燃物格；玩家/友方/安全区沿用 M23 火焰规则，进入火区仍会受伤；owner 不持久化；联机权威/客户端表现留 F2-T5，投掷 HUD/背包标记和最终平衡留 F2-T6。本机 Debug 全量本轮受既有 M31-T9/网络压力 flake 影响（失败不涉及 T4），Release 全量及 Debug 定向回归均通过。F2-T4 无阻塞，下一步 M34-F2-T5。

## 2026-11-18 M34-F2-T3 单机投掷飞行、落点伤害与回收

**范围**：把 T2 的“松开 V 只预览”闭合成单机投掷：墙体夹取、扣 1 件、飞行时间、落点目标伤害、噪声、可回收物品落点生成/损坏、冷却，以及存档/退出前结算在飞投掷物；不做火焰、联机权威或投掷 HUD/平衡。

**实现**：`game/Throwing.{h,cpp}` 新增 `ThrownProjectile`、`ClipThrowPath`（按 0.125 格步进采样，遇阻挡/未加载格夹到上一安全点）、`SelectThrowTarget`（`impactRadius` 内最近、同层、非自身）、`ResolveThrowLanding`（不可回收=消耗；可回收按 `breakChance` 与 roll 判损坏/回收）、`ConsumeThrowableSource`（主手/快捷栏精确校验 defId 后扣 1 件，保留 condition，防误删）和 `ThrowFlightSeconds`（0.18–1.2s）。`WorldDemoState` 瞄准每帧在射程夹取后调用墙体夹取并更新“墙前落点”，松开 V 校验存活/倒地/驾驶/UI/冷却/来源后扣件并生成运行时投掷物；固定 tick 推进飞行，落地筛掉玩家/友方/`NoCombatTarget`/死亡/异层目标，调用 `CombatSystem::ApplyDamage`（Blunt、0 击退/硬直），发送 `noiseRadius` 的 `SoundEvent`，按 `breakChance` 生成地面物品并派发 `OnItemDrop`，不可回收/损坏只消耗；投掷冷却 0.7s。F12/退出前 `SettleThrownProjectiles` 立即结算并清空，不保存飞行实体；进入世界/读档清空冷却和在飞列表。HUD action hint 从“V 投掷预览”改为“松开 V 投掷/墙前落点”，仍不新增 `HudState`/脚本字段。联机仍不生成投掷预览/命令。

**验证**：新增 `tests/test_throw_flight.cpp` 4 个用例，覆盖墙体夹取/飞行时间、落点最近目标/排除同层与自身/未命中、回收-损坏边界、主手/快捷栏扣件防误删、`Advance`/`ForceSettle` 幂等；`M34-F2-T3*` **4/4（62 assertions）**，T2 提示断言更新为 **4/4（58）**，T2+T3 定向 **8/8（120）**。Debug 全量 **1086/1086（142886 assertions）**；Release 首轮命中既有 M30-T6 闪避时序 flake，隔离 **2/2（56）** 通过后全量 **1086/1086（142900）**；Python 工具 **123/123**。Release 1280 真实按键 `build/m34_f2_t3_aim/`：5 块石头连续投掷，库存日志 5→4→3→2→1→0，`roll=0.023/0.001` 两块损坏、其余 3 块生成地面物品；F12 在飞时日志 `存档/退出前结算在飞投掷物：1 件（不保存飞行中实体）`，2 张截图、stderr 0。`build/m34_f2_t3_hit/`：快捷栏 2 的燃烧瓶命中脚本在投掷开始生成的僵尸，日志 `伤害：来源 34 → 目标 …（6 点），剩 94.0 HP` 与 `结算=消耗`，2 张截图、stderr 0；两个脚本均为临时探针，不入库。

**限制/下一步**：T3 不生成火焰（F2-T4）、不新增 `ThrowItem`/网络复制/服务器权威冷却（F2-T5）、不做投掷 HUD/背包标记/最终平衡（F2-T6）。墙体夹取与目标选择由纯逻辑测试覆盖；烟测目标由探针生成，长矛未单独命中烟测，石块/长矛共用同一运行时。F2-T3 无阻塞，下一步 M34-F2-T4。

## 2026-11-18 M34-F2-T2 投掷瞄准、射程夹取与预览

**范围**：给 `V` 增加“主手/当前快捷栏有 `throwable` 时优先投掷”的上下文，完成射程夹取、落点/抛物线预览、UI/建造/背包/菜单拦截、退出潜行和“不消耗物品”的预览闭环；不做飞行、伤害、噪声、回收、火焰或联机投掷。

**实现**：新增 `game/Throwing.{h,cpp}`：`ThrowingConfig` 默认 `cooldownSeconds=0.7/maxRange=12/minRange=1`，`LoadThrowingConfig` 对非法值与非有限值回退并告警；纯函数 `ResolveThrowAimSource`/`ResolveThrowAimAction` 固定“主手 → 快捷栏 1–5 → 枪械”优先级，`ThrowAimBlockReasonFor` 覆盖 UI 光标、建造、背包、菜单、其他模态、死亡/倒地、载具、未加载区域与过近距离，`EvaluateThrowAim` 取 `min(item.range, throwing.maxRange)` 并输出夹取后的连续落点与方向。`WorldDemoState` 新增投掷源检测、每帧瞄准更新、ESC/失源/界面变化取消、松开 V 预览确认；瞄准成功即 `ExitStealth(Throw)`，只写朝向和预览状态，不扣物品。预览绘制最大射程圆、落点 `impactRadius` 圆与按 `arcHeight` 抬升的抛物线，HUD 复用 `actionHint` 显示“V 投掷/投掷预览”，未新增 HudState 字段或脚本 API。联机模式不生成投掷预览或投掷命令，联机投掷留 F2-T5。

**验证**：新增 `tests/test_throw_aim.cpp` 4 个用例（投掷/主手优先与枪械回退、射程夹取/最小距离/无效落点、UI/建造/背包/菜单/载具拦截与文案、配置读取/非法回退），`M34-F2-T2*` **4/4（57 assertions）**；Debug 全量 **1082/1082（142823 assertions）**、Release 全量 **1082/1082（142837 assertions）**、Python 工具 **123/123** 全绿。Release 1280 真实按键烟测 `build/m34_f2_t2_aim/`：临时 mod 先加手枪/9mm 并装备，再加 5 个石块到快捷栏 1；`throwing.maxRange=4.0` 下 5.83 格目标夹到 4.0，日志含 `投掷瞄准：石块`、`夹取=true`、`退出潜行：投掷`、`投掷预览确认…未扣物品`；松开 V 后石块仍为 5。光标移到 HUD 后 V 被拒绝为“光标在界面元素上”；快捷栏切到空槽并 R 换弹后 V 正常开火 `手枪 7/8`。截图 `m34_f2_t2_aim_ready.png`、`m34_f2_t2_aim_released.png`、`m34_f2_t2_ui_block.png`、`m34_f2_t2_gun_fallback.png`，stderr 0；脚本 `build/m34_f2_t2_aim_smoke.ps1`（临时，不入库）。

**限制/下一步**：T2 只是瞄准/夹取/预览；松开 V 仍只确认预览，不扣物品、不结算伤害/噪声/火焰，也没有投掷冷却、回收/损坏和联机命令。联机投掷留 F2-T5，实际飞行/落点/回收留 F2-T3。F2-T2 无阻塞，下一步 M34-F2-T3。

## 2026-11-18 M34-F2-T1 投掷物品数据、校验与内容指纹

**范围**：只建立投掷数据契约、内置投掷物与燃烧瓶配方，不接 `V` 瞄准、飞行、伤害、回收或火焰运行时。

**实现**：`ItemDef` 新增可选 `throwable{range,damage,impactRadius,arcHeight,noiseRadius,returnable,breakChance,fire?}`，`fire{radius,minutes}` 可选；`ItemRegistry` 严格校验对象、必填字段、布尔、范围、未知字段和 `fire` 子对象，非法物品/ mod 事务回退且不崩溃。`assets/items.json` 为 `forage_stone`（7.5/8/0.6/噪声6/回收/5%损坏）、`makeshift_spear`（9/28/0.45/噪声5/35%损坏）、`bone_spear`（8.5/24/0.45/噪声4.5/45%损坏）补 `throwable`，新增 `molotov`（8/6/1.2/噪声5/不可回收/100%损坏/1.5格火45分钟，内置物品 98→99）；`assets/recipes.json` 新增 `craft_molotov`（空瓶+破布+燃料罐，工作台 1.5s，配方 30→31）。`HashItemDef` 纳入全部 `throwable` 字段并把 `kContentHashVersion` 升 **v22**。

**验证**：新增 `tests/test_throwing.cpp` 与 `tools/tests/test_throwing_data.py`，覆盖合法/旧物品缺省/越界/未知字段/`fire` 子字段/mod 追加、内置物品/配方和 v22 指纹；`M34-F2-T1*` **5/5（128 assertions）**、`M6-T1*` **8/8（695）**；Python 工具 **123/123**、内置物品 99/配方 31 可加载；Release 600 tick 无 mod 启动烟测 `build/m34_f2_t1_startup/startup_nomods.out.log` 加载物品 99、配方 31、内容指纹 `0x10757d00f3d1acda`、stderr 0。Debug **1078/1078（142766 assertions）**、Release **1078/1078（142780 assertions）**（首轮 Release 命中 M23-F2b-T6/M30-T6/M32-T5 既有网络/时序 flake，隔离通过后复跑绿）。

**限制/下一步**：仍无投掷输入、射程预览、飞行、落点伤害、噪声、回收和火焰；非法 `throwable` 沿用严格物品契约整体回退，不逐条保留。F2-T6 再平衡数值，下一步 M34-F2-T2。

## 2026-11-17 M34-F1-T7 F1 平衡、回归与文档

**范围**：固定 F1 潜行基线，补 8 格/3 格定点感知用例，用独立 startup + 真实按键复核远距、近距与被追击三组场景；不改 T1–T6 的潜行、背刺、HUD 或联机协议。

**实现**：基线保持 `crouchSpeedScale=0.55`、`stepNoiseScale=0.45`、`visibilityScale=0.65`、`backstabMultiplier=1.6`、半角 60°；派生蹲行 2.2 格/s、脚步 1.125 格、AI 有效视距 7.8 格，`closeRadius=2.5` 全向感知不变。新增 `M34-F1-T7*`：8.2 格站立可见，同距蹲下不可见并保留 lastKnown/警觉，3.0 格蹲下仍可见。`docs/modding.md` 补 `stealth` 配置范围、`N` 键、派生值和“动物视觉不叠加潜行视距”的限制；README/docs01/docs22/devlog 更新到 7/25、F1 完成。

**验证**：`M34-F1-T7*` **1/1（6 assertions）**、`M34-F1*` **21/21（301）**、`M5/M30/P1 HUD/M9-T4` 定向 **50/50（1710）**；Debug 全量 **1074/1074（142624 assertions）**、Release 全量 **1074/1074（142638 assertions）**（首轮 Release 命中 `M31-T9` maxSnapshotMs 与 network inventory pending-work 两个时序断言，复跑绿）、Python 工具 **119/119**。启用 `ZL_ENABLE_PYTHON=ON` 的独立构建中 Python API **9/9（86 assertions）**、`M34-F1*` **21/301** 全绿，实际执行了 Python `Hud.GetState` 的 T6 字段断言。3 次独立 startup 1280（core_ui、10:00、关闭天气/游荡者/动物/POI、1000 HP）在开阔草地 34.5,28.5 用真实 `N` 键进入潜行，探针固定生成普通僵尸：远距站立 10.242 格在 7.821 格被发现（alert 1）；同距蹲下保持 10.242 格、alert 0；近距蹲下 6.5 格在 5.967 格被发现并追到 1.4 格。截图 `%TEMP%/m34_t7_smoke_standing-far/m34-f1-t7-far-standing-1280.png`、`%TEMP%/m34_t7_smoke_crouched-far/m34-f1-t7-far-crouched-1280.png`、`%TEMP%/m34_t7_smoke_crouched-near/m34-f1-t7-near-crouched-1280.png`、`%TEMP%/m34_t7_smoke_crouched-near/m34-f1-t7-chasing-crouched-1280.png`，stderr 均为空。

**限制/下一步**：烟测是开阔草地定点生成 + 真实按键，不是自然村庄遭遇；远距目标位于 0.5x 镜头边缘/林缘，距离证据以 HUD/探针日志为准。动物视觉仍不读取 `visibilityScale`；联机仍不显示附近警觉/背刺；投掷退出留 F2。F1 无阻塞，下一步 M34-F2-T1。

## 2026-11-17 M34-F1-T6 HUD、提示与脚本字段

**范围**：让玩家不用翻日志就能判断潜行是否生效、附近是否已有警觉目标、当前是否可背刺；不改 AI/战斗判定，不做联机 AI 真值复制。

**实现**：`HudState` 增加 `stealthAwarenessValid/stealthNearbyAlertCount/stealthBackstabAvailable/stealthPrompt`。单机 `RefreshStealthHud` 以玩家为中心 8 格扫描存活敌意 `AiState`，警觉数沿用 `StealthTargetUnaware`；背刺沿用 T4 的未警觉/背后/倍率规则，攻击距离取当前武器重击 reach（重矛 2.6 格可越过 `closeRadius=2.5`）。`stealth-status` 追加“附近警觉 N”和“可背刺”；背刺可用时另显 `#stealth-prompt`“背刺可用 · SPACE”，不复用会抢占战斗提示的 `actionHint`。菜单、背包、交易、对话、角色/同伴/地图/建造/载具改装/睡眠/战报打开时由 `HudStealthPromptSuppressed` 统一抑制。Native `hud.rml/hud.rcss`、`core_ui` 的 RML/RCSS/Lua/Python、Lua/Python `Hud.GetState` 字段同步。联机保持 awareness unknown，只显示姿态/速度/脚步，不由客户端残缺副本推断 AI 真值。

**验证**：`M34-F1-T6*` **1/1（11 assertions）**；`M34-F1*` **20/20（295）**、P1 HUD/M9-T4/Lua UI binding 定向通过；Debug 全量 **1073/1073（142617 assertions）**；Release `M34-F1*` 20/295、`M30*/M15*/P1 HUD*/M9-T4*` 66/66（2000）通过（首轮 M30-T6 闪避时序 flake，隔离 2/2 通过后组合复跑绿）。1280 烟测 `%TEMP%/m34_t6_smoke`（core_ui）与 `%TEMP%/m34_t6_native_smoke`（native fallback）各 4 张截图（站立、潜行、背刺提示、低姿态移动），探针日志确认 `backstab=true/prompt=true`，打开背包后 `prompt=false`、关闭后恢复，两份 stderr 均为空。

**限制**：附近警觉/背刺提示只读单机本地 AI 真值；联机客户端不复制 `alertness`，因此不显示附近警觉/背刺可用。烟测用构造生成的未警觉僵尸 + 重矛验证，不等于自然遭遇下每个距离都能背刺；菜单抑制由纯函数测试覆盖，实际只截图了背包抑制。当前本地 `ZL_ENABLE_PYTHON=OFF`，Python `Hud.GetState` 字段代码已同步但未执行 Python 后端用例，F1-T7 需在启用 Python 的构建复跑。下一步 M34-F1-T7。

## 2026-11-17 M34-F1-T5 联机权威、姿态复制与旧档

**范围**：把潜行从单机开关升级为服务器权威状态，覆盖命令、owner-only 摘要、ZLR1 远端姿态、攻击/受伤/上车/倒地的退出与重连/读档/复活复位；不改 F1-T4 的伤害公式，不做 PvP 背刺。

**实现**：`CommandType::SetStealth=61`，`flags bit0=1` 进入、0 退出，无其他 payload；服务器校验存活、非倒地、非驾驶后写 `ecs::StealthState`，菜单只由客户端拦截。`PlayerState` 升 **v23**，尾部 owner-only `valid/crouched/noiseRadius/visibilityScale`，v22 解码默认站立、噪声 2.5、视距 1；玩家档案保存时剥离摘要，潜行不写世界存档。`ZLR1` 新增 `STLH` 1B（bit0 低姿态、bit1 移动），只对 `PlayerTag` 发出；远端客户端按 actor 显示潜行标记/移动短线，本地 AI 真值仍只来自服务器。服务器接受近战后把背刺倍率/半角写入 `AttackIntent`，并在攻击、远程攻击、受伤、上车、倒地/死亡时退出潜行；生成/恢复/复活复位站立，客户端断线复位本地加速。旧 v22 或无 `STLH` 的新客户端回退站立。

**验证**：`M34-F1-T5*` **5/5（119 assertions）**，覆盖 SetStealth loopback 字段、ZLR1 1B 移动位/缺失回退、双客户端权威切换与远端 `STLH`、非法 payload/倒地拒绝、服务器攻击背刺参数与退出、重启/重连/复活默认站立；`M34-F1*` **19/19（284）**、`M13 Replication*` **5/5（158）**、`M13-T7*` **7/7（327）**；Debug 全量 **1072/1072（142580 assertions）**；Release `M34-F1*` 19/284、`M30*/M15*` 63/976、`M13-T7/T8` 13/440 通过。未新增 live 双客户端 1280 烟测，F1-T7 用真实输入复核。

**限制**：`STLH` 只带姿态/移动位，不复制噪声/视距；远端仅显示标记，AI 发现仍只在服务端。驾驶中 `SetStealth` 由服务器统一拒绝；旧客户端仍需同协议重建。PvP 背刺仍不生效。下一步 M34-F1-T6。

## 2026-11-17 M34-F1-T4 背刺与攻击暴露

**范围**：在现有近战伤害管线内接入背刺，不新建攻击/伤害入口，不改硬直、击退、耐久与磨损；背刺只对 AI，PvP 留后续。

**实现**：`StealthRules` 新增 `StealthTargetUnaware`（Idle/Wander 且 `alertness < 0.5`，Chase/Attack/SearchLastKnown/Dead 不可）、`StealthBehindTarget`（目标→攻击者向量与目标朝向反方向的夹角 ≤ 半角）和 `StealthBackstabDamageScale`（1..3 安全倍率）。`AttackIntent` 增加 `backstabMultiplier/backstabHalfAngleDegrees`，单机 `WorldDemoState::PlayerAttack` 从已清洗的 `stealth` 配置写入；`CombatSystem` 命中循环只允许 `PlayerTag` 攻击者、`AiState` 目标，排除倒地/死亡/友方/动物/玩家，每次挥击最多消费一次背刺，倍率只乘伤害。攻击仍走既有 `ExitStealth(Attack)`；既有挥击动画 `AnimEvent::Hit` 的 6.0/7.5 格 `CombatCue` 保持为一次挥击一次的攻击噪声，未新增重复 `SoundEvent`；命中反馈新增“背刺！”提示。

**验证**：`M34-F1-T4*` **3/3（54 assertions）**，覆盖正后方 10 格伤害 16、正面/侧面、Idle+0.49 可背刺、Chase/Attack/SearchLastKnown/0.5 警觉不可、动物/友方/玩家/倒地/死亡排除、一次挥击双目标只增伤一个、60° 边界与倍率 1/1.6/3/NaN 回退、攻击退出潜行条件。`M34-F1*` **14/14（165）**、`M30*/M15*` **63/63（976）**；Debug 全量 **1067/1067（142450 assertions）**；Release 同组定向通过。未新增 live 背刺烟测，F1-T7 用真实输入三距离验收。

**限制**：T3 的 `closeRadius=2.5` 全向感知保留，常规 1.45~2.3 格武器贴脸时目标通常已警觉；背刺实际主要靠 2.6 格重矛或尚未完成感知的目标，T7 平衡复核。联机 `SetStealth` 与服务器权威背刺随 F1-T5 接入。下一步 M34-F1-T5。

## 2026-11-17 M34-F1-T3 AI 视觉/听觉接入

**范围**：让 AI 真正按潜行可视倍率判断玩家，同时保留既有天气视距、近距全向、视线遮挡、听觉和搜索行为；不新增 AI 监听器、不改联机协议。

**实现**：`PerceptionSystem::VisibleTarget` 快照增加 `visibilityScale`，从玩家 `StealthState` 读取，无该组件的旧/远端实体按 1；`CanSee` 增加目标倍率参数，先保留 M20-F2 的天气视距门控，非 `closeRadius` 时有效视距 = `sightRadius × 环境倍率 × visibilityScale`，非法/越界倍率回退 1（避免损坏状态变成隐身）。`closeRadius` 内仍全向且不乘潜行倍率，但 `LineOfSight` 始终执行，树木/墙体不会因潜行失效。听觉、警觉衰减、`lastKnown` 和 `SearchLastKnown` 均未改；脚步声只沿用 T2 的 1.125 格半径进入既有 `SoundEvent` 链路。

**验证**：`M34-F1-T3*` **4/4（24 assertions）**，覆盖站立 10 格可见 / 蹲下 10 格不可见 / 7 格可见、近距 2 格背后全向、树木遮挡、天气 0.5×潜行 0.65 叠乘、警觉衰减不清零、失去目标后 `SearchLastKnown`、潜行脚步 1.125 格内可闻/外无感；`M34-F1*` **11/11（111）**、`Perception*` **8/8（43）**、`M5-*` **3/3（31）**、`M20-*` **29/29（348）**。Debug 全量 **1064/1064（142395 assertions）**；Release 同组定向通过。live AI 烟测未新增，真实输入三距离验收按 F1-T7 执行。

**限制**：远端玩家姿态复制留 F1-T5；AI 仍只在单机/服务端读取玩家快照，客户端不参与感知真值。下一步 M34-F1-T4。

## 2026-11-16 M34-F1-T2 单机低姿态移动、脚步噪声与 HUD

**范围**：让潜行从“状态骨架”变成可感知的单机行为：低姿态降速、玩家脚步降噪、攻击/受击/载具/倒地自动退出，以及 HUD 可见反馈；不接 AI 视距、背刺、投掷实际动作或联机复制。

**实现**：`StealthScales` 增加 `speedScale`，`StealthState` 增加运行时 `speedScale`；`MovementSystem` 只对带 `StealthState.crouched` 的玩家叠加 `crouchSpeedScale`，站立/非玩家路径不变。`UpdateAudio` 把玩家脚步基础半径 2.5 格乘 `stepNoiseScale`，潜行脚步输出 `潜行脚步噪声：x.xx 格` 日志；枪声、载具、陷阱的 `SoundEvent.radius` 不动。新增 `StealthExitReason`（Attack/Throw/Damage/Vehicle/Downed）与 `StealthExitRequired`；近战/远程攻击在成功起手时退出，`HitReaction.hitSerial` 变化时按受击退出，`EnterVehicle` 与每帧 `Dying/Health<=0/VehicleOccupant` 检查兜底。HUD 新增 `stealthValid/stealthCrouched/stealthMoveSpeed/stealthFootstepRadius/stealthStatus/stealthTone`，native `hud.rml`、`core_ui` RML/脚本、Lua/Python `Hud.GetState` 同步；显示“潜行/站立 · x.x 格/s · 脚步 x.x 格”。

**验证**：`M34-F1-T2*` **3/3（29 assertions）**，覆盖速度/脚步倍率边界、禁用/非法配置回退、五种退出原因、MovementSystem 站立/潜行/坏倍率、HUD 文案；`M34-F1*` **7/7（87）**、`M9-T4` **1/1（296）**、`M9-T6` **2/2（98）**、`M15-T5` **2/2（31）**、`P1 HUD` **2/2（702）**、`Lua UI binding` **1/1（36）**。Debug 全量 **1060/1060（142370 assertions）**；Release 同组定向通过。1280 烟测 `%TEMP%/m34_t2_smoke` 验证站立 4.0 格/s → 潜行 2.2 格/s、脚步 2.5→1.12 格、攻击退出（4 张截图）；`%TEMP%/m34_t2_damage_smoke` 验证受击退出（2 张截图）；两目录 stderr 均为 0。

**限制**：HUD 速度用 `LocalControl.walkSpeed × 潜行倍率`，未叠加疲劳/疼痛/护甲/气候；`Throw` 原因已预留但实际投掷动作在 F2 接入；载具/倒地实际烟测、AI 视距、背刺、姿态复制留 F1-T3/T5/T6/T7。下一步 M34-F1-T3。

## 2026-11-16 M34-F1-T1 潜行配置、输入与状态骨架

**范围**：为 M34 潜行建立可验证的最小闭环：配置读取、默认键位/设置页、运行时状态和单机切换；不接移动、AI、战斗、背刺或联机权威。

**实现**：新增 `characters::StealthRuleConfig`/`SanitizeStealthRuleConfig`/`StealthScalesFor` 与 `game::LoadStealthRuleConfig`；`config/game.json` 增加 `stealth` 段（速度/脚步噪声/可视距离/背刺/暴露）和 `input.bindings.Stealth=N`。`kDefaultBindings`、`kMenuBindingDefs`、设置菜单 RML 与 `MenuPanel::kKeyBindingCount` 同步增加“潜行”；`N` 与联机睡眠投票 `SleepVoteNo` 共用键，但睡眠/投票在前段直接 return，不触发潜行。新增运行时 `ecs::StealthState`（`crouched/noiseScale/visibilityScale`，不注册存档），玩家生成/旧档运行时恢复默认站立；单机按 `N` 写状态、输出 `stealth` 日志并显示 notice，联机/菜单/背包/载具/倒地/死亡/失败状态拒绝本地切换。未新增 `PlayerState`/ZLR1/网络命令。

**验证**：`M34-F1-T1*` **4/4（55 assertions）**，覆盖默认值/范围/NaN/非法回退、旧配置缺键回退、设置页第 14 项 `Stealth=N`、输入动作按下与 UI 阻断；`M9-T6` **2/2（98 assertions）**。Debug 全量 **1057/1057（142337 assertions）**通过；首轮命中 `M31-T9`/`M22-F1`/`M26-V2`/`X-F1 2b-2b` 时序/覆盖 flake，隔离均通过后全量复跑绿。Release 定向与 UI 定向通过。1280 实机烟测 `%TEMP%/m34_t1_smoke` 发送 `N` 两次：日志 `潜行切换：开（噪声 x0.45，可视 x0.65）`、`关（x1.00/x1.00）`，菜单打开后再按 `N` 不产生第三次切换，3 张截图、stderr 0。

**限制**：HUD 当时仅用 notice；低姿态移动、脚步噪声、自动退出已由 F1-T2 补齐；AI 视觉/听觉、背刺、联机复制与 `PlayerState v23` 仍未实现。下一步当时为 M34-F1-T2。

## 2026-11-16 M33-F5-T7 F5 回归与 M33 收口

**范围**：完成 F5 双客户端阶段验收、15–30 分钟跨包循环、全量回归和 M33 文档收口；不新增内容字段或协议。

**双客户端**：新增 `tests/test_m33_f5_t7.cpp`，加载 3 阶段测试 POI：A/B 共同推进 world 级阶段，A 击杀 Boss 后双方各获得一次阶段奖励；每人 3 条 `rewardClaims` 且奖励物品各 1 份。服务器重启并以同一身份重连后，完成阶段保持、Boss 不重刷、claim 与物品不重复。`M33-F5-T7*` **1/1（68 assertions）**；`M33-F5*` **21/21（646 assertions）**。

**1280 循环**：`build/m33_f5_t7_loop_smoke/` 使用实际 M31 内容与正常 `E/1/2/I`、鼠标菜单完成：商队余波选择交易（声望 +5）→村庄求助者招募林医生→安装车顶行李架→诊所药房 3 阶段→返回村庄→快速存档→主菜单继续读档；游戏内 **15.3 分钟**，15 张 1280 截图，主进程/读档进程 stderr 为空，读档恢复 70 实体/1 玩家。

**回归**：Debug **1053/1053（142279 assertions）**、Release **1053/1053（142293 assertions）**、Python **119/119**；Release 首轮全量命中既有 `M32-T5` 区域围攻 active 时序偶发失败，隔离 5/5 与全量复跑通过。Release 600 tick 无 mod 启动烟测 `build/m33_f5_t7_startup_final/startup_nomods.out.log`：POI 4 个、物品 98、内容指纹 `0xcd19753800f05050`、tick=600、stderr 0。同步 docs/18（35/35）、docs/01、README、docs/21、docs/modding、mapformat 和本日志。

**限制**：循环烟测用 `dayLengthMinutes=180`、传送、100000 HP、事件起始直开和放宽林医生招募条件，不等同无修改的 15–30 分钟墙钟真人流程；双客户端 T7 用测试 POI，不是 M31 诊所/检查站的 1280 双窗口。真实自然流程与旧图迁移取舍建议后续人工复验。

## 2026-11-16 M33-F5-T6 POI 内容包

**范围**：把默认 M31 的诊所、检查站、农场仓改成 3 阶段内置内容，覆盖分支、钥匙门禁、阶段搜刮与 Boss；不改旧 `village_demo` 的 11 个单段 POI，不新增 Boss 血条/动画。

**内容**：`clinic` 为 `triage_choice` 分支——`ward` 夜班医师（armored/summon）+1 精英，奖励急救包；`pharmacy` 搜刮 `hospital_patient_log`，奖励抗生素。`checkpoint` 为 `patrol_cache` 搜刮 `checkpoint_patrol_log` 并发放 `checkpoint_keycard` → `armory_gate` 消耗钥匙按 `E` 开门 → `commander` 检查站指挥官（armored/fortify）+2 精英。`farmstead` 为 `farm_yard` 到达 → `supply_cache` 搜刮 `farm_supply_log` → `farm_boss` 农场主（strong/summon）+2 精英。新增 4 个物品，笔记分别保底在 `poi_clinic_medicine`/`poi_military_supplies`/`poi_farm_tools`；物品总数 98。

**修复**：1280 烟测发现玩家绑定过远处皮卡后，`WorldDemoState::FindNearbyStorage` 会把 `m_vehicle` 无条件当作附近仓库，导致所有 POI 容器被空后备箱覆盖、搜刮动作拿不到笔记。现在非驾驶/乘坐只接受 `FindNearestVehicle(enterRange)`，驾驶/乘坐才保留绑定车辆；日志确认三个容器 `can=true/stacks>0` 并成功 `take_all`。

**验证**：C++ `M33-F5-T6*` **1/1（77 assertions）** 从实际 M31 内容加载并验证分支/钥匙/Boss/奖励与阶段流；Python `test_f5_poi_content.py` 5 用例验证引用/保底/可达性。Debug **1052/1052（142211 assertions）**、Release **1052/1052（142225 assertions）**、Python **119/119**。Release 600 tick 无 mod 启动烟测 `build/m33_f5_t6_startup_final/startup_nomods.out.log`：POI 4 个、物品 98、指纹 `0xcd19753800f05050`、tick=600、stderr 0。1280 烟测 `build/m33_f5_t6_smoke/` 使用临时传送/高伤害/高生命，正常 `1/2`、`E`、`I` 与 `take_all` 完成三处 POI；13 张截图（分支/背包/钥匙/门禁/三处 Boss/完成/地图），stderr 为空。

**限制**：默认 M31 使用诊所/检查站/农场仓对应医疗/军事/农仓场景；旧 `village_demo` 未迁移；诊所分支为 Boss/搜刮二选一，检查站与农场为线性；烟测用传送和高伤害隔离阶段，不等同真实 5–10 分钟步行计时；自然 15–30 分钟循环、双客户端阶段一致性与 M33 收口留 T7。下一步 M33-F5-T7。

## 2026-11-16 M33-F5-T5 HUD、地图、叙事与阶段提示

**范围**：把 T2–T4 的阶段状态接成可读的玩家界面：结构化阶段卡、钥匙/供电/路线/Boss 条件、阶段叙事、完成/失败结果卡，以及世界地图上的阶段标记与完成度；不新增内置 staged/Boss 内容（T6）。

**实现**：`Poi` 新增 `PoiStageProgressText`/`PoiStageHudObjective`/`PoiStageHudRequirements`/`PoiStageNarrativeText`/`PoiStageMapStatusText`/`PoiStageResultTitle`/`PoiStageResultText`/`PoiStageMapPoint`。`HudState` 与 native/core_ui 两套 HUD 新增 `poi-stage-panel`、`poi-result-panel`，Lua/Python `Hud.GetState` 暴露 `poiStage*`/`poiResult*` camelCase 字段；阶段卡显示当前阶段、目标、钥匙/供电/路线/Boss 与当前 `narrative[]` 文本，完成/失败卡约 10 秒后自动收起，不阻塞战斗。`WorldMapPoi` 新增阶段序号/完成数/失败/待选路线/当前阶段坐标与 `WorldMapPoiLabel`，地图标记优先落在当前阶段坐标，右侧已发现地点列表同步；`RefreshWorldMapState` 从单机 `PoiWorldProgress` 或网络只读镜像构建 `WorldMapPoiProgress`。完成/失败面板由单机阶段完成、Boss 脱战失败与网络阶段 flags 转换触发。同步 docs/18、docs/21、docs/01、README、docs/modding、docs/07、docs/devlog。

**验证**：`M33-F5-T5*` **2/2（43 assertions）**，覆盖 HUD/叙事/结果文本、地图阶段标记与标签；`M9-T4 HUD` **294 assertions** 在 1280 下验证阶段/结果面板不越界且不压目标区/mod 面板。Debug **1051/1051（142114 assertions）**、Release **1051/1051（142128 assertions）**、Python **114/114**。Release 600 tick 无 mod 启动烟测 `build/m33_f5_t5_startup_final/startup_nomods.out.log`：POI 4 个、指纹 `0x38f10fa308f0fc42`、tick=600、stderr 0。1280 构造截图 `build/m33_f5_t5_capture/` 六张（阶段 1/2/3、门禁、Boss、完成面板）；临时 staged POI 实测 `build/m33_f5_t5_actual/actual-stage.png`、`actual-complete.png`、`actual-map.png` 验证真实 `HudState` 路径。

**限制**：T5 不新增内置 staged/Boss 内容（T6）；门禁/Boss 截图是临时视觉探针构造状态，不替代 T6/T7 自然流程；叙事目前是阶段卡文本，没有独立阅读弹窗/物品阅读 UI；地图阶段标记由当前进度驱动，未做多层地图/图例筛选。下一步 M33-F5-T6 POI 内容包。

## 2026-11-16 M33-F5-T4 精英/Boss 与阶段战斗

**范围**：把 T3 的 kill 阶段接成真实 Boss/精英战斗，加入阶段敌人、三种简单 Boss 机制、脱战/团灭失败和自动重试；不新增内置 Boss 内容（T6），不做完整阶段 HUD/地图（T5）。

**实现**：`Poi` 新增 `BuildPoiStageEnemyPlan`/`PoiStageHasBoss`/`PoiBossStage`/`PoiBossMechanicFor`/`PoiBossMechanicName`/`PoiBossMechanicText`；`PoiStageObjectiveText` 对 Boss 阶段显示名称、机制和击杀进度。`kill` 阶段生成 `PoiStageEnemy`（Boss/精英/召唤物），Boss 按 `boss.count` 生成并截断到 `stage.count`，剩余精英由 `stage.target` 选择变体；实体同时带 `PoiGuard`，但 kill 只统计 `PoiStageEnemy` 的 Boss/精英，普通守军不参与。Boss 生命乘 `pois.boss.healthScale`；`summon` 在 Boss 附近生成不参与计数的快速增援，`charge` 周期性触发速度冲锋，`fortify` 复用 `ZombieSpecial::Breaker` 破坏工事。阶段敌人新增 `PoiStageEnemy` 内置存档段 `PSEN`（组件 50→51），`PoiBossRuntime` 为不存档的机制计时器。Boss 脱战超过 `resetRange/resetSeconds` 或服务器团灭时失败并清除敌人；玩家回到 marker 16 格/入口 6 格内自动重试，阶段奖励继续用 `PoiTasks.rewardClaims` 只发一次。`PlayerState` v22、内容指纹 v21 不变；`config/game.json` 新增 `pois.boss.*` 调参。同步 docs/18、docs/21、docs/01、README、docs/modding。

**验证**：`M33-F5-T4*` **3/3（86 assertions）**，覆盖生成计划/机制映射/失败重试、`PoiStageEnemy` 存档往返、服务器 Boss 生成+召唤+脱战失败+自动重试+击杀完成+奖励一次；Debug **1049/1049（142038 assertions）**、Release **1049/1049（142052 assertions）**、Python **114/114**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f5_t4_startup_final2/startup_nomods.out.log`：POI 4 个、指纹 `0x38f10fa308f0fc42`、stderr 0。

**限制**：T4 不新增内置 Boss/精英内容（T6）；`boss.stage` 必须绑定 `kill` 阶段；冲锋是速度倍率而非独立动画/碰撞技能，fortify 复用 Breaker；单机死亡仍按既有规则结束本局；没有独立 Boss 血条/技能树；完整阶段 HUD/地图/叙事与 1280 截图留 T5。下一步 M33-F5-T5。

## 2026-11-16 M33-F5-T3 门禁、钥匙、供电与可选路线

**范围**：把 T2 的权威阶段信号接成可玩的钥匙/门禁/供电/入口/路线交互；不新增内置 staged 内容，不做 Boss 机制或完整阶段 HUD/地图。

**实现**：`Poi` 新增 `FindPoiStage`/`FindPoiKey`/`FindPoiEntry`、`StartPoiProgressAtStage`、`MarkPoiKeyAcquired`、`PoiStageKeyAwards`、`PoiAvailableBranches`、`PoiKeyDisplayName`、`PoiStageObjectiveText`/`PoiStageRequiredKeysText`/`PoiChoiceText`。阶段完成时按 `keys[].source` 向单机玩家或服务器 32 格内玩家发放实际物品；门禁/供电/路线交互前检查玩家背包钥匙，`consumed` 控制消耗并写入世界钥匙标记。`entries[]` 允许从非首阶段开始（靠近且钥匙满足时自动进入），`choice` 用快捷栏 1/2 选择 `branches[]`。服务器新增 `PoiInteract=60`（POI 定义索引 + 路线索引），复核距离/阶段/钥匙/燃料；单机复用同一规则。`power` 消耗 `homestead.power.fuelItem`（默认 `fuel_can`），启动阶段附近已有 `ecs::Generator` 并写 `powered`；电力系统关闭时允许手动合闸，缺钥匙/燃料都有明确 `notice`/HUD 提示。`PlayerState` v22、`PoiTasks` v2、`PoiWorldProgress` 存档组件和内容指纹 v21 不变。同步 docs/18、docs/21、docs/01、README、docs/modding。

**验证**：`M33-F5-T3*` **2/2（66 assertions）**，覆盖钥匙物品/入口/路线纯数据、缺钥匙/有钥匙、燃料、实际发电机启动、服务器路线选择与入口；Debug **1046/1046（141952 assertions）**、Release **1046/1046（141966 assertions）**、Python **114/114**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f5_t3_startup_final/startup_nomods.out.log`：POI 4 个、指纹 `0x38f10fa308f0fc42`、stderr 0。Release 全量首跑再次命中既有 `M30-T6` 闪避时序 flake；隔离复跑 **2/2（56 assertions）** 通过，随后全量 Release 复跑绿。

**限制**：T3 不新增 3 个内置多阶段 POI（T6）；门禁是阶段坐标上的逻辑交互，不绑定可开关的建造成门实体；发电机只在附近已有实体时启动，否则只写世界供电标记；入口为靠近自动进入、无独立入口面板；无 Boss/阶段专属刷怪（T4）或完整 HUD/地图/1280 截图（T5）。`pois-v1` 仍要求至少一个合法容器占位。下一步 M33-F5-T4 精英/Boss 与阶段战斗。
## 2026-11-15 M33-F5-T2 POI 阶段运行时与存档

**范围**：为 T1 的 staged POI 增加世界级阶段状态、单机/服务器权威推进、阶段奖励幂等、世界存档/重启恢复与客户端只读镜像；不接真实门禁/钥匙物品消耗/发电机/路线 UI/Boss 机制/阶段 HUD。

**实现**：新增 `PoiProgress`（当前/失败阶段、完成阶段、已选分支、已获得钥匙标记、供电、失败/重试、击杀进度、守军基线）与纯函数 `StartPoiProgress`/`TickPoiStage`/`SignalPoiStage`/`ChoosePoiBranch`/`FailPoiProgress`/`RetryPoiProgress`。单机/服务器共享 `ecs::PoiWorldProgress`，StableId 必须放高段 `0xF333504F49575001`：最初使用低段 `0x4D33...` 会把 `EcsWorld::m_nextStableId` 推高，导致所有后续自动 StableId 和载具生成确定性变化，`M26-V14` 由通过变为固定失败；改为 `>= 1<<63` 后恢复。进度进入实体世界存档（内置组件 49→50）；服务器/单机在玩家进入 marker 16 格内启动，`reach` 按位置、`kill` 按守军存活数下降、`loot` 按玩家持有目标物品数推进，`interact/power/choice` 保留 `SignalPoiStage`/`ChoosePoiBranch` 权威接口供 T3 接玩法。阶段奖励用 `PoiTasks.rewardClaims` 的 `poiId.stageId` 幂等键，`PoiTasks` 存档升 v2（旧 v1 可读），服务器只给阶段完成时 32 格内玩家发奖。`PlayerState` 升 v22，在尾部追加 `poiStageProgress[]` 只读镜像（阶段序号/总数/完成数、choice/failed/powered、当前阶段、已选分支），旧 v21 缺省为空。已完成 staged POI 不再补生成守军，旧单段 POI 路径不变。同步 docs/18、docs/21、docs/01、README、docs/modding。

**验证**：`M33-F5-T2*` **7/7（127 assertions）**，覆盖到达/搜刮/钥匙阻塞、击杀/供电、分支选择、奖励幂等、失败重试、旧 POI 无 stages、世界进度存档、`PlayerState v22` 往返、服务器三阶段推进+重启恢复；Debug **1044/1044（141884 assertions）**、Release **1044/1044（141899 assertions）**、Python **114/114**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f5_t2_startup/startup_nomods.out.log`：POI 4 个、指纹 `0x38f10fa308f0fc42`、stderr 0。Release 全量首跑命中一次既有 `M30-T6` 闪避时序 flake；隔离复跑 **2/2（56 assertions）** 通过，随后全量 Release 复跑绿。

**限制**：T2 不消费真实钥匙物品、不执行门禁/发电机/分支交互、不生成阶段专属敌人/Boss、不触发 Boss 失败、不显示阶段 HUD/地图标记；这些留 T3–T5。`pois-v1` 解析仍要求至少一个合法容器条目，纯阶段 POI 目前需放占位容器。下一步 M33-F5-T3 门禁、钥匙、供电与可选路线。

## 2026-11-15 M33-F5-T1 POI 多阶段数据契约与校验

**范围**：为 `pois-v1` 增加可选多阶段数据契约，不改现有 11 个单段 POI 的运行时；阶段状态、存档、门禁/钥匙、Boss 机制和 HUD 留 F5-T2–T5。

**实现**：`PoiDef` 新增 `stages/entries/keys/branches/boss/narrative` 与 `PoiStageKind{reach,interact,kill,loot,power,choice}`。阶段按数组顺序推进，`next` 只能指向后续阶段；非 `choice` 最多 1 个后继，`choice` 必须 2–4 个；分支最多 2 条；阶段、入口、分支、Boss、叙事各有限额。解析校验阶段 id/坐标/半径/数量/奖励、钥匙 item/source/consumed、入口/分支/Boss/叙事引用；非法阶段逐条跳过并级联跳过依赖阶段，非法 Boss 只禁用而不丢弃 POI。阶段 `requires` 引用的钥匙若带 `source`，来源阶段必须在本阶段之前，避免先要求后发放。没有 `stages` 时保持旧 `task` 必填与单段行为；有有效阶段时 `task` 可省略；mod 可追加 staged POI，重复 id 仍跳过。`kContentHashVersion` 升 **v21**，全部新字段与数组顺序进入指纹。新增 `docs/21-M33多阶段POI数据格式.md`，同步 `docs/18`、`docs/01`、README、`docs/modding` 及同伴/载具文档的指纹版本说明。

**验证**：`M33-F5-T1*` **5/5（179 assertions）**，覆盖合法字段/默认值、非法阶段与引用级联跳过、旧 v1 不变、mod 追加、阶段/入口/钥匙/分支/Boss/叙事指纹；Debug **1037/1037（141754 assertions）**、Release **1037/1037（141768 assertions）**、Python **114/114**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f5_t1_startup/startup_nomods.out.log`：内容指纹 `0x38f10fa308f0fc42`、地图内容 POI 4 个、tick=600、stderr 0。Release 全量首轮受外部 `hdrp-bgfx` 的 16 个 `shaderc.exe` 占满 CPU 影响，命中 `M31-T9` 快照耗时与 `X-F1 2b-6` chunk 加载时序两个既有 flake；分别隔离 **1/1（22 assertions）**、**1/1（16 assertions）** 通过，空闲后全量复跑绿。

**限制**：T1 只解析/校验，不新增 `PoiProgress`、阶段存档、服务器权威推进、钥匙门禁/供电、Boss 生成/机制或阶段 HUD/地图标记；不修改基础 11 个 POI 内容，也没有 1280 阶段截图。非法阶段当前按单条跳过并告警，不提供编辑器级错误定位。下一步 M33-F5-T2 阶段运行时与存档。

## 2026-11-15 M33-F4-T6 改装内容、平衡与交付

**范围**：固定 F4 五个内置改装的收益、代价、材料来源与全套总账，补平衡测试、1280 交付烟测和 F4 文档收口；不改已完成的安装/复制/存档协议。

**实现**：`assets/vehicle_mods.json` 继续保持 5 槽 5 件，全部有明确代价：装甲撞击车况 -32%/-28% 但油耗 +7%、重 28kg；行李架容量 +24kg 但油耗 +3%、重 12kg；撞尸犁撞击伤害 +45%、撞击车况 -35% 但油耗 +5%、重 32kg；探照灯车灯范围 +45%/亮度 +25% 但油耗 +2%、重 6kg；发动机极速 +18%/加速 +12% 但油耗 +15%、车况损耗 +10%、重 8kg。材料总消耗维修包×8、木材×14、无线电零件×2、螺丝刀×1，全部来自现有搜刮表，不新增货币。新增 `tests/test_vehicle_mod_balance.cpp` 固定单件收益/代价、全套重量 86kg、加速 -8%、油耗 +55%、车况倍率、容量 64kg、材料总账和面板摘要；Python 数据测试补材料来源校验。新增 `docs/20-M33载具改装数据与平衡.md`，同步 `docs/18`、`docs/01`、README、`docs/modding`。

**验证**：`M33-F4-T6*` **2/2（106 assertions）**；`M33-F4*` **23/23（545 assertions）**、`M25*,M26*` **71/71（1478 assertions）**、`M13 Replication*` **5/5（157 assertions）**；Debug **1033/1033（141578 assertions）**、Release **1033/1033（141592 assertions）**、Python **114/114**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f4_t6_startup/startup_nomods.out.log`：车辆改装 5 个、内容指纹 `0xe0db877f3199c38d`、世界事件 6 个、stderr 0。Release 1280 烟测 `build/m33_f4_t6_smoke/` 通过正常 `I` 背包入口依次安装 5 个改装（145s），拆除并重装行李架，随后上车开灯；12 张截图，stderr 0。首次全量跑在外部 `shaderc` 编译占满 CPU 时命中网络用例超时，空闲后两套全量串行复跑全绿。

**限制**：数值为确定性公式与构造烟测，不等同于长时间自然驾驶油耗/维护体验；`trunkStacks` 仍受 6 格 UI 限制；装甲不减乘员碰撞伤害，重量不改悬挂/制动；探照灯不改世界光照或 AI 感知。F4 检查表 5/5 通过，下一步 M33-F5-T1 POI 阶段数据契约与校验。

## 2026-11-15 M33-F4-T5 服务器权威、复制与重连

**范围**：把载具改装从单机闭环接到服务器权威命令、ZLR1 只读摘要与重启/重连恢复；不改改装数值，不扩 6 格背包 UI，不做 T6 平衡交付。

**实现**：新增 `InstallVehicleMod=58`（载具 StableId、槽位、候选索引）与 `RemoveVehicleMod=59`。服务器复核玩家非驾驶/乘坐、载具存活/空载/停稳、同层/距离、候选属于目标槽位、同槽占用/双向 `excludes` 与材料计数；材料事务扣除后写权威 `ecs::VehicleMods`，拆除按 `removeRefundRatio` 返还并将背包溢出掉在玩家脚下，重复/非法候选/远端/使用中载具均拒绝。ZLR1 新增 `VMOD` 只读组件（5 槽 defId，384B），客户端镜像到 ECS；旧负载无 `VMOD` 按无改装回退，不改 `VHCL` 旧字段。网络面板取消 `readOnly`，恢复候选/材料/安装/拆除按钮；客户端只提交命令，不本地改组件，服务器提示经 `PlayerState` 回写面板；服务器拆除空槽时保留空组件，避免增量快照留下客户端旧槽位。服务器重启、客户端重连和车辆重载从 `VehicleMods v1` 与全量快照恢复。

**验证**：`M33-F4-T5*` **3/3（100 assertions）**，覆盖旧 ZLR1 负载回退、双客户端一致、重复/非法候选拒绝且不扣料、不串车、服务器重启、客户端重连和容量 64→40kg 恢复；`M33-F4*,M25*,M26*` **92/92（1917 assertions）**、`M13 Replication*` **5/5（157 assertions）**；Debug **1031/1031（141472 assertions）**、Release **1031/1031（141486 assertions）**、Python **113/113**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f4_t5_startup/startup_nomods.out.log`：车辆改装 5 个、指纹 `0xe0db877f3199c38d`、世界事件 6 个、stderr 0。

**限制**：联机安装/拆除由服务器即时结算，单机保留施工进度；网络面板状态只来自服务器 `PlayerState`，客户端不做预测；`trunkStacks` 仍未扩到 8 格 UI，面板不显示未应用格位；T5 未新增 1280 截图，联机一致性由双客户端 headless 用例覆盖。下一步 M33-F4-T6 改装内容、平衡与交付。

## 2026-11-15 M33-F4-T4 改装 UI、HUD 与交互

**范围**：把 T2 的调试面板入口改为正常车辆后备箱/HUD 入口，补齐逐槽效果、总览与只读联机状态；不新增网络命令，不做 ZLR1 改装摘要复制，不扩 6 格背包 UI。

**实现**：移除 `VehicleMods=O` 绑定和 HUD 的 `O 改装` 提示，车辆附近提示改为 `I 后备箱/改装`。原生背包与 `mods/core_ui`（Lua/Python）背包增加“车辆改装 · 查看槽位与效果”按钮，通过 `Inventory.Action(type=open_vehicle_mods, fromEntityId=车辆 StableId)` 打开面板；`InventoryPanel` 同时支持原生点击。`VehicleModsPanel` 新增逐槽当前/候选效果文案和总览行（总重、后备箱容量、极速/加速/油耗/撞墙/撞尸/撞击/车灯）；`VehicleModEffectSummary`/`VehicleModStatsSummary` 统一格式化，当前/候选/材料/时间/重量/进度/失败提示保留。入口按 StableId 精确绑定车辆；`VehicleModTargetInRange` 统一校验存活、车辆类型、同 z、距离，驾驶/乘坐、死亡、离车或车辆失效时拒绝/中止。联机模式 `readOnly`：只显示服务器结果，隐藏候选/切换/安装/拆除，不提交本地修改；T5 再接复制和服务器命令。

**验证**：`M33-F4-T4*` **3/3（77 assertions）**，覆盖效果文案/总览、目标距离、面板效果/失败色/只读按钮/鼠标点击和后备箱入口；`M33-F4*,M25*,M26*` **89/89（1817 assertions）**；Debug **1028/1028（141367 assertions）**、Release **1028/1028（141381 assertions）**、Python **113/113**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f4_t4_startup/startup_nomods.out.log`：车辆改装 5 个、指纹 `0xe0db877f3199c38d`、世界事件 6 个、stderr 0。Release 1280 夜间烟测 `build/m33_f4_t4_smoke/`：按 `I` 打开后备箱并点击改装入口，安装行李架（20s，容量 40→64kg），拆除（10s，容量 64→40kg，返还木材×3），再安装探照灯（15s），ESC 关闭后上车开灯；8 张截图，日志含 2 次安装/1 次拆除/容量往返/`车灯：开`，stderr 0。

**限制**：联机面板只读且没有改装摘要复制/服务器安装拆除（T5）；`trunkStacks` 仍未扩到 8 格 UI，面板不显示未应用格位；联机候选/材料与操作反馈留 T5。下一步 M33-F4-T5 服务器权威、复制与重连。

## 2026-11-15 M33-F4-T3 改装效果接入驾驶、碰撞、载重与灯光

**范围**：把 F4-T1 的改装效果数据接到 M25/M26 现有载具运行逻辑；不新增网络命令，不做改装 UI 效果摘要，不扩 6 格背包 UI。

**实现**：`VehicleMods.{h,cpp}` 新增 `VehicleModStatsFor`，按 5 槽聚合产品倍率、容量加值、堆数加值和重量；产品倍率分别 clamp，重量合计 0–500kg，加速项 `1/(1+weight/400)`、油耗项 `1+weight/600`（可配置）。`VehicleStep`/`VehiclePredictStep` 接极速/加速/油耗/撞墙车况/重量；单机与服务器车辆间碰撞接 `crashConditionScale`，撞击接 `ramDamageScale/ramConditionScale`，僵尸/危险动物攻击接 `zombieConditionScale`，发动机 `conditionWearScale` 叠加到所有车况损耗。后备箱容量在安装/拆除/单机换车/服务器读档时按基础值重算；探照灯缩放夜间光束距离和透明度。纯逻辑与服务器重启测试覆盖 5 类效果和无改装基线；Release 1280 夜间烟测用真实车辆安装行李架/探照灯/发动机后上车开灯。

**验证**：`M33-F4-T3*` 5/5（74 assertions）、`M33-F4-T1/T2/T3 + M25/M26` 86/86（1740）、Debug 1025/1025（141290）、Release 1025/1025（141304）、Python 113/113。Release 全量首跑出现一次与本 T 无关的 `M32-T5 真实失守到反攻恢复并重建守卫` 时序失败；单跑该过滤 5/5、Release 全量复跑 1025/1025 通过。1280 烟测 `build/m33_f4_t3_smoke/`：3 个改装安装完成、容量 40→64kg、夜间车灯开启、stderr 0。

**限制**：`trunkStacks` 仍解析但未扩到 8 格 UI；装甲不额外减乘员碰撞伤害；重量不改悬挂/制动；探照灯不改世界光照/AI；联机客户端效果摘要与服务器安装/拆除留 T5，效果摘要/HUD 入口留 T4；拆下行李架后已有超重物品保留。

## 2026-11-15 M33-F4-T2 车辆改装组件、安装/拆除与存档

**范围**：为现有载具增加按槽位持久化的改装组件、单机安装/拆除面板、材料/时间/返还规则和车辆/服务器存档恢复；本 T 不接驾驶/碰撞/容量/灯光效果，不做联机命令与 ZLR1 复制。

**实现**：新增 `ecs::VehicleMods`（5 个槽位 defId）与 `VMOD v1` 存档组件，内置存档组件 **49**；`Vehicle` 存档组件升 **v4**（载荷不变，v1–v3 解码路径保留）。`VehicleMods.{h,cpp}` 增加同槽占用、双向 `excludes`、材料计数/事务扣除、`floor(count×ratio)` 返还和拆除耗时；`config/game.json` 新增 `removeRefundRatio=0.5`、`removeSecondsScale=0.5`。单机靠近车辆按 `O` 打开独立 `vehiclemods.rml` 面板，显示 5 槽当前/候选/材料/安装时间/重量/拆除返还，鼠标安装/拆除，进度条和状态；完成时安装扣材料、拆除返还材料，离开车辆/死亡/关闭面板中止，背包放不下的返还物掉在脚下。面板打开时隐藏 HUD，`ESC`/X 关闭。服务器重启测试把 3 个槽位写入载具后保存/重启并恢复。

**验证**：`M33-F4-T2*` **4/4（92 assertions）**，覆盖安装/拆除/重复/缺材料/互斥/候选/退款/旧 v3 存档/服务器重启；载具存档定向 **16/16（355 assertions）**、载具旧回归 `M25*,M26*` **71/71（1478 assertions）**；Debug **1020/1020（141212 assertions）**、Release **1020/1020（141226 assertions）**、Python **113/113**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f4_t2_startup/startup_nomods.out.log` 加载车辆改装 5 个、内容指纹 `0xe0db877f3199c38d`、世界事件 6 个、stderr 为空。Release 1280 烟测 `build/m33_f4_t2_smoke/` 用真实车辆和 `O` 面板完成装甲安装（30s，维修包×2+木材×4）→ 拆除（15s，返还维修包×1+木材×2）→ 关闭，7 张截图，日志有安装/拆除/关闭，stderr 为空。

**限制/下一步**：T2 仅单机安装/拆除和持久化；效果值仍待 T3 接入驾驶/碰撞/后备箱/车灯/油耗；面板入口与效果摘要、后备箱/HUD 入口留 T4；服务器安装/拆除命令、ZLR1 改装摘要、重连和旧负载回退留 T5。下一步 M33-F4-T3 改装效果接入驾驶、碰撞、载重与灯光。

## 2026-11-14 M33-F4-T1 改装数据契约与校验

**范围**：为 F4 载具改装建立独立内容文件、5 个槽位和效果字段，接入 mod 追加、内容指纹、客户端/服务器/ContentBundle 加载和单机热重载；本 T 不创建车辆组件、不安装/拆除、不改变驾驶物理。

**实现**：新增 `game/VehicleMods.{h,cpp}` 与 `assets/vehicle_mods.json`（`vehicle_mods-v1`）。每条定义含 `id/name/description/slot/effects/materials/installSeconds/weightKg/excludes`；槽位为 `armor/storage/plow/lights/engine`，效果覆盖碰撞/被攻击/撞尸车况、撞尸伤害、后备箱容量/堆数、车灯范围/亮度、极速/加速/油耗/车况磨损。材料引用现有物品，安装时间/重量/效果均有范围校验；重复 id、非法槽位、越界效果、未知/重复材料、自互斥和未知互斥目标按单条跳过，合法条目保留。内置 5 条改装覆盖全部槽位。`config/game.json` 新增 `vehicleMods.enabled/file`；`ModContent` 扫描 `content/vehicle_mods.json`，`ContentBundle`、`GameApp`、`ServerHost` 和热重载均加载；内容指纹升 **v20**。无 `vehicleMods` 配置时保持禁用，旧车辆运行时不变。

**验证**：`M33-F4-T1*` **6/6（92 assertions）**；`M33-F3-T5*`、`X3-T2/T3/T5`、`M13-T9` 等定向 **41/41（899 assertions）**、载具旧回归 `M25*,M26*` **71/71（1478 assertions）**；Debug **1016/1016（141120 assertions）**、Release **1016/1016（141135 assertions）**、Python **113/113**；Release 首轮命中既有 `M23-F2b-T6` 动物剥取时序 flake，隔离 **2/2（51 assertions）** 后全量复跑通过。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f4_t1_startup/startup_nomods.out.log` 加载车辆改装 5 个、内容指纹 `0xe0db877f3199c38d`、世界事件 6 个、stderr 为空。

**限制/下一步**：T1 仅数据/加载/校验/指纹；尚无 `VehicleMods` 组件、安装/拆除、效果应用、UI、服务器复制和存档。下一步 M33-F4-T2 车辆改装组件、安装/拆除与存档。

## 2026-11-14 M33-F3-T8 同伴内容、平衡与交付

**范围**：收口两名内置同伴的差异化内容、单同伴 DPS/受伤平衡、配置化 AI 参数、双客户端 15 分钟验收和文档；不新增同伴类型、不做 RTS 框选/编队、不做完整背包复制或换装/服药/装填 UI。

**实现**：保留林医生（村庄营地，95 HP，厨刀/夹克，`field_medic/steady_hands`，绷带×2+罐头汤，`settlement_help`+绷带×2）与阿默（南部农场，115 HP，长矛/加固夹克，`scout/cool_headed`，能量棒×2+绷带，`south_harvest_help`+罐头汤）两名定义，每人最多 1 名活动/待命同伴。新增 `config/game.json` 的 `companions.ai.*` 24 个 AI/战斗参数，`CompanionSystem` 通过 `LoadCompanionSystemOptions` 读取并 clamp；主手武器伤害/冷却按 `attackDamageScale=0.38`/`attackCooldownScale=1.20` 缩放，空手回退 24/0.85。林医生 41.56 DPS（厨刀玩家 131.25 的 31.67%）、阿默 28.79 DPS（长矛玩家 90.91 的 31.67%）、空手 28.24/80.00（35.29%），均落在 30–50%；倒地 60 秒、救援 35%/2.5 格、低血 30%/恢复 50%。`MakeCompanionDialogue` 按 `companion_lin`/`companion_amo` 区分医疗/斥候闲聊和受伤文本；已有同伴时求助者提示改为“已有同伴：名字（状态）”，不再重复显示招募条件。新增 `tests/test_companion_balance.cpp` 和 [docs/19-M33同伴数据与平衡.md](19-M33同伴数据与平衡.md)。

**验证**：`M33-F3-T8*` **3/3（56 assertions）**、`M33-F3-*` **31/31（792）**、`M13 Replication*` **5/5（156）**、`M33-F1*` **25/25（654）**、`M23-F4*` **27/27（559）**、`M30-T6*` **2/2（56）**；Debug **1010/1010（141028 assertions）**、Release **1010/1010（141042 assertions）**、Python **108/108**。Release 服务器 600 tick 无 mod 启动烟测 `build/m33_f3_t8_startup/startup_nomods.out.log` 加载同伴 2 个、指纹 `0x915728de32ed9b54`、世界事件 6 个、stderr 为空。Release 1280×720 双客户端 15 游戏分钟烟测 `build/m33_f3_t8_dual_smoke/`：A/B 通过正常 `E` 对话各招募 1 名林医生，服务器 2 次 `服务器同伴招募`/`同伴生成`，双方均记录 15 分钟标记，10 张截图，stderr 为空。

**限制/下一步**：联机只同步装备摘要，不显示完整背包；不提供换装/服药/装填 UI；赠礼一次 1 件；待命同伴需回聚落召回；`traits`/`extraTraits` 当前是持久化内容标签，不直接改伤害/护甲公式；未做 RTS 框选/编队或好感度恋爱线。下一步 M33-F4-T1 改装数据契约与校验。

## 2026-11-14 M33-F3-T7 同伴 UI、HUD 与交互

**范围**：给玩家一套正常游戏内 UI 管理同伴：面板/HUD、跟随/停留/撤退、赠礼、个人委托接取/放弃、救援和聚落招募条件；不引入 RTS 框选/编队，不做换装/服药/装填面板。

**实现**：新增 `game/CompanionUi.{h,cpp}` 与 `ui/CompanionUi.h` 生成只读快照；原生 `assets/ui/hud.rml` 与 `mods/core_ui` Lua/Python HUD 同步新增 `companion-panel`/`companion-status`。`P` 开关面板（`CompanionPanel` 普通绑定），显示生命/状态/信任/装备/背包/个人委托；`1/2/3` 跟随/停留/撤退、`4` 赠礼当前快捷栏物品、`E` 靠近交谈/救援、`P/ESC` 关闭。HUD 按死亡、倒地（剩余秒数/E 救援）、低血、远离、路径受阻、聚落待命优先提示；聚落求助者交互提示和对话 hint 追加招募所需声望档位/当前分数、前置委托和物资数量。新增服务器权威 `SetCompanionCommand=53`、`GiftToCompanion=54`、`AcceptCompanionQuest=55`、`AbandonCompanionQuest=56`、`RescueCompanion=57`，服务器复核 owner/同层/距离/倒地/待命/背包；单机走同一规则。同伴对话新增赠礼选项，接取/放弃个人委托接实际状态；联机客户端用只读 `CMPN` 快照构造上下文，不本地改信任/委托/死亡。内容指纹保持 v19，未改存档/复制 schema。

**验证**：新增 `tests/test_companion_ui.cpp` 2 用例并扩展 `tests/test_companion_network.cpp` T7 服务器用例；`M33-F3-T7*` **3/3（78 assertions）**，覆盖面板文本/信任/装备/背包、倒地/远离/卡住/待命/死亡告警、服务器指令/赠礼/委托/救援。`M33-F3-*` **28/28（736 assertions）**、`M13 Replication*` **5/5（156 assertions）**；Debug **1007/1007（140972 assertions）**、Release **1007/1007（140987 assertions）**、Python **108/108**；Release 服务器 600 tick 启动烟测 `build/m33_f3_t7_startup/` 加载同伴 2 个、内容指纹 `0x915728de32ed9b54`、世界事件 6 个、stderr 为空。Release 1280×720 烟测 `build/m33_f3_t7_smoke/` 通过：正常 `E` 对话招募林医生，`P` 打开面板、`2` 停留、`P` 关闭；截图 `companion_recruit_hint_1280.png`、`companion_recruited_1280.png`、`companion_panel_1280.png`、`companion_panel_stay_1280.png`、`companion_panel_closed_1280.png`，stderr 为空。

**限制/下一步**：联机仍只同步装备摘要，不显示完整背包；面板不提供换装/服药/装填 UI；赠礼一次只转移当前快捷栏物品 1 件；待命同伴需回所属聚落对话召回；未做 RTS 框选/编队。下一步 M33-F3-T8 同伴内容、平衡与交付。

## 2026-11-14 M33-F3-T6 存档、服务器权威与重连

**范围**：让同伴实体/状态进入联机只读复制，补玩家断线/死亡/同身份接管时的安全待命、重连 owner 关系与服务器重启恢复；不做同伴交互 UI、对话/委托/赠礼命令、完整背包复制或自动召回。

**实现**：`EntityReplicationProducer` 不再跳过 `Companion`，新增 `ZLR1 CMPN` 只读组件，携带 `defId`、跟随/停留/撤退指令、待命/倒地/死亡、信任、个人委托状态/阶段/进度/奖励 mask、倒地剩余；实体 `ownerStableId` 来自 `Companion.ownerStableId`。位置/生命/朝向/动画继续走通用组件，装备摘要从 `CompanionState.equipment` 复用既有 `EQUIP` 编码；客户端副本只补 `FriendlyNpc`/`NoCombatTarget`，不生成本地 `Companion`/`CompanionState`，无 `CMPN` 的旧负载按普通角色安全回退。`ServerHost::StandbyOwnedCompanion` 在断线、玩家死亡和同身份接管时保留 StableId/owner，健康同伴传送到所属聚落待命，位置不可用时原地清路径/AI；服务器只恢复玩家档案，不补生成同伴。世界存档沿用 `Companion`/`CompanionState` v3，重启恢复 owner、生命、装备、信任、委托、额外特长和倒地状态；注册复制组件 29→30，内容指纹保持 v19。

**验证**：新增 `tests/test_companion_persistence.cpp` 3 用例与 `tests/test_replication.cpp` 同伴 `CMPN` 用例；`M33-F3-T6*` **4/4（188 assertions）**，覆盖全量/增量/旧负载、双玩家 owner 隔离、死亡/断线待命、重连不复制、单机读档、服务器重启。`M13 Replication*` **5/5（156 assertions）**、`M33-F3-*` **25/25（658 assertions）**；Debug **1004/1004（140884 assertions）**、Release **1004/1004（140898 assertions）**、Python **108/108**。Release 服务器 600 tick 启动烟测 `build/m33_f3_t6_startup/` 加载同伴配置 2 个、内容指纹 `0x915728de32ed9b54`、stderr 为空。

**限制/下一步**：同伴面板/命令、对话/个人委托接取、赠礼/救援/服药/装填/换装 UI 留 T7；复制只含装备摘要，不含完整背包；重连后同伴保持聚落待命，需要玩家重新召回；旧客户端的未知 `CMPN` 组件不在本次兼容范围。下一步 M33-F3-T7 UI、HUD 与交互。

## 2026-11-14 M33-F3-T5 信任、对话与个人委托

**范围**：让同伴信任随战斗/赠礼/个人委托变化，补信任档、状态化同伴对话和两人各一条个人委托链；委托奖励只给少量物资/装备/特长；不做恋爱线、不做玩家 UI、不做服务器权威对话/接取、不做 `ZLR1`。

**实现**：新增 `game/CompanionTrust.{h,cpp}`，复用 `CompanionState.loyalty` 为 0~100 信任值（Wary<25、Steady<50、Trusted<75、Bonded），`AddCompanionTrust` 统一钳制。`CompanionSystem` 在同伴击杀敌人时 +2 战斗信任并记录 `kill` 委托进度；`GiftToCompanion` 从玩家背包原子转移物品到同伴背包并 +2，容量不足回滚。`companions-v1` 新增可选 `personalQuest[]`，林医生/阿默各两段：`gift`（绷带/热汤）→`kill`（3/4 名感染者），每段有 `minTrust`、`rewardTrust`、`rewardTrait`、`rewardItem`、`rewardEquipSlot`、`completionText`；`CompanionState` 追加 `personalStatus/personalStage/personalProgress/personalRewardMask/extraTraits`，阶段完成按 mask 只发一次奖励，死亡/显式失败中止当前阶段。`Dialogue` 新增 `MakeCompanionDialogue` 与信任/委托/低血/倒地条件，根节点按状态显示不同文本和入口；`AcceptCompanionQuest`/`AbandonCompanionQuest` 结果已预留。`CompanionState` 存档组件升 v3（旧 v2 默认无委托），内置组件保持 48；`personalQuest` 全部字段进入内容指纹 v19。

**验证**：`M33-F3-T5*` **4/4（108 assertions）**，覆盖信任边界、击杀+2、赠礼原子转移、委托前置/失败/完成/防重复、额外特长与装备、对话正常/低血/倒地/死亡/信任/委托分支；`M33-F3-T1*` **4/4（95 assertions）**、`M33-F3-T2*` **4/4（126 assertions）**、`M33-F3-T3*` **5/5（43 assertions）**、`M33-F3-T4*` **4/4（98 assertions）**。Debug **1000/1000（140697 assertions）**、Release **1000/1000（140710 assertions）**、Python **108/108**。Release 服务器 600 tick 启动烟测 `build/m33_f3_t5_startup/` 加载同伴 2 个、内容指纹 `0x915728de32ed9b54`、世界事件 6 个、stderr 为空。

**限制/下一步**：同伴对话/个人委托接取/赠礼/救援等玩家 UI 留 T7；服务器权威对话/接取、`ZLR1`/客户端可见、重连与旧 v2 完整回归留 T6；个人委托是同伴专属链，不写入全局 `MissionLog`/任务 HUD；赠礼目前只按一次调用 +2 信任，不按物品价值细分。另将既有 `M23-F2a-T6` 钓鱼咬钩等待从 15s 放宽到 30s，避免 Debug 慢机把 5.5 秒游戏等待误判为失败（不改变断言/玩法）。下一步 M33-F3-T6 存档、服务器权威与重连。

## 2026-11-14 M33-F3-T4 同伴生命、装备、背包与倒地

**范围**：复用通用 `Health` 与 `CompanionState.inventory/equipment`，生成时放入初始装备/背包；主手武器影响近战伤害/距离/冷却/磨损，躯干护甲影响受击减伤；补背包/装备迁移、药品/弹药、倒地救援窗口、死亡尸体与装备转移、世界存档；不做玩家 UI、不自动制作、不做客户端复制。

**实现**：新增 `game/CompanionInventory.{h,cpp}`。初始装备按 `equipment.primary/body` 放入现有两槽，初始背包按 `inventory[]` 放入容量；`CompanionSystem` 从主手武器读取 `damage/range/cooldown/wear`，躯干护甲派生 `DamageResist` 并在装备变化/受击磨损后重算。入包/出包/换装/卸装/药品/弹药都先试算目标容量再提交，失败回滚；药品走 `items::UseAt` + `ApplyUseEffect`，远程武器从背包扣匹配弹药后填 `loadedAmmo`，单机/服务器共用同一规则。生命 ≤0 进入 60 秒倒地窗口，`TryRescue`/`WorldSimulation::RescueCompanion` 在 2.5 格内恢复 35% 最大生命；窗口结束标记死亡并把装备/背包 move 到 `companion_corpse` 尸体容器（`Corpse` + `LootContainer.searched=true`），死亡同伴保留实体并阻止再次招募/解散。`Companion`/`CompanionState` 注册为存档组件（内置组件 46→48，嵌套物品按 loadedAmmo v2）；单机/服务器存档都包含同伴，读档后补 `FriendlyNpc`/`NoCombatTarget`/`DamageResist`，单机保存前 Flush 避免延迟尸体丢失。

**验证**：`M33-F3-T4*` **4/4（98 assertions）**；`M33-F3-T2*` 因新增初始装备/护甲断言 **4/4（126 assertions）**；`M33-F3-T3*` 保持 **5/5（43 assertions）**。Debug **997/997（140589 assertions）**、Release **997/997（140603 assertions）**、Python **108/108**。Release 服务器 600 tick 启动烟测 `build/m33_f3_t4_startup/` 加载同伴 2 个、内容指纹 `0x53a59928194171fd`、世界事件 6 个、stderr 为空。测试覆盖装备/背包往返、初始装备/护甲、武器伤害、倒地/救起/死亡、尸体物品总数与状态清空、药品/弹药不复制、SaveManager 磁盘读档。

**限制/下一步**：倒地救援/服药/装填/换装的玩家 UI 留 T7；`CompanionAiState`、队伍复制、客户端可见、重连恢复留 T6；`CompanionAiState` 不存档，读档后从当前位置重建；尸体当前是通用容器内容，专用尸体搜刮 UI/提示留后续；死亡同伴为永久状态，不复活/不重新招募；敌对 AI 仍主要锁定玩家，同伴当前通过火焰/环境/范围伤害与脚本承伤，专用同伴仇恨留后续。下一步 M33-F3-T5 信任、对话与个人委托。

## 2026-11-14 M33-F3-T3 同伴跟随与战斗 AI

**范围**：让已招募同伴自动跟随 owner、停留、撤退并在 owner 受威胁时参战；复用 A* 与动态物理体，补 leash/卡住/远距离冻结/陷阱落点/低血撤退与友伤过滤；不做装备/倒地死亡、玩家指令 UI、存档/复制。

**实现**：新增 `game/CompanionSystem.{h,cpp}`，运行时 `CompanionAiState` 保存 targetId、停留锚点、home、攻击冷却、重寻路计时、autoRetreat、frozen、engaged。跟随落点取 owner `Facing` 背后 3 格并带侧向偏移，1.8–4.5 格内停步；停留以切换时位置为锚点，威胁靠近时在 4.5 格内迎战；撤退/低生命（≤30%）回 owner，收缩到 50% 后恢复。统一复用 `PathRequest`/`PathFollow`/A*，失败/到达按 0.35 秒重试，保留 PathFollow 卡住重寻路；owner 超过 40 格或异层时冻结，门口/建筑边界沿用静态通行位图，不穿墙。目标只在 owner 被攻击、owner 攻击中或威胁进入 owner 3.5 格时获取，取最近 owner 威胁，最多追到 owner 12 格 leash；过滤玩家/友方/聚落 NPC/动物/同伴/`NoCombatTarget`，近战走现有伤害管线，不产生 AOE。跟随时落点、停留锚点和被陷阱覆盖时的离开点避开武装 `SpikeTrap`；`SpawnCompanionAt` 补 Collider/RigidBody，`TeleportCompanion` 同步物理体并清理 AI 状态。

**验证**：`M33-F3-T3*` **5/5（43 assertions）**；`tests/test_companion_network.cpp` 追加服务器真实同伴攻击僵尸，`M33-F3-T2*` 现 **4/4（118 assertions）**。Debug **993/993（140483 assertions）**、Release **993/993（140497 assertions）**、Python **108/108**。Debug 全量首轮命中既有 M23-F2a-T6 觅食/钓鱼时序 flake，隔离通过后复跑通过；Release 全量重试期间命中既有 M30-T6 双客户端闪避时序（2 次）与 M32-T5 反攻时序，均隔离通过，最终全量复跑通过。测试覆盖跟随距离/背后落点、停留/撤退、低血自动撤退、目标选择、玩家开战前不拉怪、leash 脱战、友伤过滤、跨区域绕墙、卡住重试、远距离冻结、陷阱落点。Release 服务器 600 tick 启动烟测 `build/m33_f3_t3_startup/` 加载同伴 2 个、内容指纹 `0x53a59928194171fd`、世界事件 6 个、stderr 为空。服务器真实战斗回归最初因同伴与僵尸被建筑/路径隔开而在 7 秒内未接战，改为先 `TeleportCompanion` 到 owner 侧并把僵尸物理体同步到 0.6 格内后稳定通过（连续 4 次）。

**限制/下一步**：跟随/停留/撤退玩家 UI 留 T7；装备/背包/倒地死亡留 T4；AI 状态不存档、不复制，联机客户端不可见，留 T6；路径只避开静态墙体与陷阱落点/当前覆盖，不做动态陷阱代价图，楼梯/异层仅冻结不跨层。下一步 M33-F3-T4 同伴生命、装备、背包与倒地。

## 2026-11-14 M33-F3-T2 聚落招募、解散与唯一性

**范围**：在求助者对话中加入同伴招募/归队/聚落待命入口，按聚落 `recruit` 服务、声望、已完成前置任务和物资校验；实现同伴实体生成/StableId 绑定、同 owner 唯一、解散回聚落待命、服务器权威命令与双玩家回归；不做 AI、装备/倒地死亡、存档/复制。

**实现**：`CompanionState` 新增 `standby`；`Companion.{h,cpp}` 新增 `FindSettlementCompanion/EvaluateCompanionRecruitment/ConsumeCompanionRecruitItems/FindOwnedCompanion`，聚落按 `recruit.requiredMission` 是否属于 `missionPool` 选择同伴定义，统一返回 `Ready/NoCompanion/ServiceUnavailable/ReputationTooLow/MissionRequired/ItemRequired/AlreadyRecruited`，物资先校验后原子扣除。`DialogueConditionKind` 新增招募/归队/解散待命条件，`DialogueResultKind` 新增 `RecruitCompanion/DismissCompanion`，求助者根节点显示对应入口；`DialogueContext` 携带同伴配置与玩家当前同伴状态。`WorldSimulation::SpawnCompanionAt` 生成带独立 StableId、`Companion`/`CompanionState`/`Health`/位置/精灵/寻路组件的同伴，`TeleportCompanion` 负责聚落待命与归队。新增 `RecruitCompanion=51`、`DismissCompanion=52`，服务器复核求助者/距离/聚落失守夜袭/声望/任务/物资/归属与唯一性，成功后回 status；单机复用同一校验和生成路径。当前同伴实体暂不写世界存档、不进入 `ZLR1`/实体复制，避免 T2 产生半持久化或半复制实体。

**验证**：`M33-F3-T2*` **4/4（110 assertions）**；Debug **988/988（140432 assertions）**、Release **988/988（140446 assertions）**、Python **108/108**。新增纯逻辑/对话/归属测试与 `tests/test_companion_network.cpp` 双客户端用例：条件不足不扣物、两名玩家各招募一名、重复招募拒绝、解散回聚落待命、归队不换 StableId；双客户端用例在每步命令前重设玩家位置，避免服务器权威位置校正造成范围拒绝抖动。Release 服务器 600 tick 启动烟测 `build/m33_f3_t2_startup/` 加载同伴 2 个、内容指纹 `0x53a59928194171fd`、世界事件 6 个、stderr 为空。

**限制/下一步**：同伴不会跟随/战斗（T3），装备/初始背包/倒地/死亡结算未接（T4），信任/对话/个人委托未接（T5），完整存档/复制/重连未接（T6）；联机客户端暂不可见同伴，服务器重启后需重新招募但不会重复生成。下一步 M33-F3-T3 同伴跟随与战斗 AI。

## 2026-11-13 M33-F3-T1 同伴数据契约与组件

**范围**：为 F3 建立同伴 `companions-v1` 数据、组件、mod 合并、内容指纹与加载链路；不做招募、AI、UI、存档/复制。

**实现**：新增 `game/Companion.{h,cpp}`，定义 `CompanionDef/CompanionConfig/CompanionRecruitRequirement` 与 `Companion/CompanionState` 组件。`assets/companions.json` 内置 `companion_medic_lin`（村庄营地，林医生）与 `companion_scout_amo`（南部农场，阿默）两名定义；支持背景、基础生命、初始忠诚、主手/躯干装备、背包容量与物品、特长、最低声望/前置任务/物资招募条件、对话 profile。缺省值为生命 100、背包 12kg、忠诚 50、Friendly、对话 id；校验 item 引用、重复 id/trait、装备槽、背包数量/容量与招募物资。`LoadCompanionConfig`、`MergeCompanionFile/Json`、`ModContent` 的 `content/companions.json` 扫描、`ContentBundle`、`GameApp`、`ServerHost` 加载与热重载接线；内容指纹升 **v18**。生命复用 `ecs::Health`，`CompanionState` 只保存忠诚/指令/倒地/背包/装备，`ValidateCompanionComponents` 统一校验。旧配置无 `companions` 时 `enabled=false`，无同伴不启用。

**验证**：`M33-F3-T1*` **5/5（96 assertions）**；Debug **984/984（140318 assertions）**、Release **984/984（140332 assertions）**、Python **108/108**。Debug 全量首轮命中既有 M31-T9 soak `uniqueChunks` 与 M23-F2a-T6 觅食时序 flake，分别隔离通过后全量复跑通过。Release 服务器 600 tick 启动烟测 `build/m33_f3_t1_startup/` 加载同伴 2 个、内容指纹 `0x53a59928194171fd`、世界事件 6 个、stderr 为空。

**限制/下一步**：T1 只有数据/组件，没有聚落招募入口、同伴实体生成、AI/战斗、存档或复制；两名定义的具体数值与第二名招募条件留给 T8 平衡。下一步 M33-F3-T2 聚落招募、解散与唯一性。

## 2026-11-13 M33-F2-T7 F2 回归与交付

**范围**：完成 F2 世界事件回归与交付：实际内容双客户端 15 游戏分钟流程、链后续独立排期、服务器重启/重复结算、网络 HUD 刷新修复、M33-F2/M31/M24/M32/M23-F4 定向和全量 Debug/Release/Python、1280 双客户端烟测与文档收口。

**实现**：新增 `M33-F2-T7 双客户端实际内容事件 15 分钟窗口、结算与重启恢复`：加载实际 `world_survivor_distress`，在 `north_fields` 完成 Reach + Defend 两阶段，双客户端共享快照；推进 15 游戏分钟后选择 `escort`，验证双方各得 +15 声望、经验、压力 -8，服务器重启后保留 Resolved 快照与声望，重复 `SettleWorldEvent` 返回 `alreadyApplied` 且不二次加声望。新增 `M33-F2-T7 链后续事件可单独排期并复制到客户端`，确保 `world_caravan_aftermath` 在只含该定义的配置中可独立排期。修复 `WorldDemoState::RefreshHud` 只在单机调用 `RefreshWorldEventHud` 的问题：网络客户端虽然收到 `ZLW2` 快照，但事件面板不刷新；现在统一调用，网络模式内部委托 `RefreshNetworkWorldEventHud`。

**验证**：`M33-F2-T7*` **2/2（79 assertions）**，`M33-F2*` 定向 **27/27（698 assertions）**，`M33-F1* + M23-F4* + M31* + M24* + M32*` **174/174（6203 assertions）**；Debug **980/980（140222 assertions）**、Release **980/980（140236 assertions）**、Python **104/104**。Release 首轮命中既有 M31-T9 写盘快照 `maxSnapshotMs` 阈值与 M30-T6 闪避时序 flake，隔离 **2/2**、**2/2** 通过后全量复跑通过。1280 双客户端烟测 `build/m33_f2_t7_dual_smoke/`：A/B 连接同一服务器，使用实际 `world_caravan_aftermath`（临时排期窗口/区域），事件在约 00:10 进行中、00:23 已解决；A 发送 `E` + `1` 后服务器日志 `world event choice: 与商队交易`，两客户端均显示声望 5、经验 40、汤罐头、区域压力 -2 的结果面板；截图 `client_a_event_1280.png`、`client_b_event_1280.png`、`client_a_near_event_1280.png`、`client_b_near_event_1280.png`、`client_a_event_result_1280.png`、`client_b_event_result_1280.png`，三份 stderr 为空。

**限制/下一步**：15 分钟为游戏时间窗口，不是墙钟计时；双客户端烟测使用固定链后续事件缩短路径，5 类事件未逐一自然实机完成；终态内容清理、鼠标选项、独立聊天频道与离线参与者补发未做。F2 检查表 6/6 通过，下一步进入 F3-T1 幸存者同伴。

## 2026-11-13 M33-F2-T6 事件内容包 v1

**范围**：填入 5 类可交互世界事件与 1 个商队后续链，启用默认内容，补内容/平衡/去重/排期测试和 1280 内容包烟测；不做 T7 双客户端 15 分钟全流程。

**实现**：`assets/world_events.json` 新增 `world_caravan_ambush`（商队伏击）、`world_survivor_distress`（幸存者求救）、`world_migrating_horde`（迁徙尸潮）、`world_infected_house`（感染房屋）、`world_supply_contest`（空投争夺）和链后续 `world_caravan_aftermath`（商队余波）。5 个主事件均为 Reach + Kill/Defend 两阶段，各 2–3 个选项；商队伏击通过 `nextEvent` 进入次日余波。`WorldEventDirector::SelectDefinition` 新增链目标过滤，`world_caravan_aftermath` 只能由链触发，不能被每日随机排期独立刷出。事件 ID 使用 `world_` 前缀，避开 M24 `event_*` 随机任务；同日最多 1 个事件、站点唯一，跨日使用 cooldown。持续时间 90–240 游戏分钟（默认 `dayLengthMinutes=60` 下约 3.75–10 现实分钟），选项奖励上限 140 XP，物品数量不超过同风险 POI，声望约 ±20、失败不低于 -8；`config/game.json` 的 `worldEvents.enabled` 改为 true。

**验证**：新增 T6 内容加载/分支/奖励边界/M24 ID 去重用例与链目标不参与随机排期用例；`M33-F2-T6*` **2/2（155 assertions）**，Debug **978/978（140143 assertions）**、Release **978/978（140157 assertions）**、Python **104/104**。Debug 全量首轮唯一失败为既有 `M23-F2b-T6` 动物剥取联机等待超时，隔离 **2/2（52 assertions）** 通过后全量复跑通过。1280 烟测 `build/m33_f2_t6_smoke/` 使用实际 `world_survivor_distress`（临时只改排期窗口/区域并传送至诊所）：日志生成 4 个事件敌人，阶段完成后选择“护送至聚落”，HUD 显示声望 +15、经验 +100、区域压力 -8；截图 `world_event_content_panel_1280.png`、`world_event_content_progress_1280.png`、`world_event_content_choice_1280.png`、`world_event_content_result_1280.png`，stderr 为空。

**限制/下一步**：烟测用临时排期和传送缩短路径，5 类事件尚未逐一自然实机完成；链后续只有数据/排期覆盖，没有独立截图；终态内容清理、鼠标选项与独立聊天频道未做。T7 做 `M33-F2*`/M31/M24/战略/存档/网络全量回归、双客户端 15 分钟流程和文档收口。

## 2026-11-13 M33-F2-T5 服务器权威、复制与持久化

**范围**：把 F2 世界事件从单机 HUD 接到服务器权威快照、选择命令、参与者结算、manifest 存档与重连恢复；不做 T6 事件内容包和 T7 全流程验收。

**实现**：新增 `net/WorldEventProtocol`（`ZLW2` 独立 magic，不能复用 `ZLW1`，后者是 WorldSound；旧客户端忽略未知消息）与 `CommandType::ChooseWorldEvent=50`（只传选项索引，不传奖励/阶段）。服务器 `WorldEventDirector` 仍唯一持有排期/阶段/结果；`ChooseServerWorldEvent` 在服务器重新校验 Active、阶段完成、玩家距离和索引后才执行选择，并通过 T3 事件+玩家账本结算。服务器按事件记录附近参与者，选择或进入 Resolved/Failed/Expired 时为每个参与者单独结算一次，重复选择/终态/远端命令拒绝；超时失败压力只应用一次。新增客户端 `NetworkClient::WorldEventState`/`HasWorldEventState`；`WorldDemoState` 网络模式从快照生成 HUD、电台和地图标记，`E` 打开、`1–8` 发命令，结果等待服务器快照，不做本地发奖。manifest 升 **v6**，新增 `worldEvents` 快照和 `worldEventOutcomes` 账本键；单机保存/读档与服务器保存/重启共用 `CaptureWorldEventsToManifest`/`RestoreWorldEventsFromManifest`，恢复后事件内容用稳定 id 幂等重放，不重复生成。

**验证**：新增 `tests/test_world_event_network.cpp` 5 用例（`ZLW2` 编解码/旧负载、manifest v5/v6、双客户端共享事件/个人 XP/重复拒绝、超时自动结算、服务器重启与两次客户端重连），并补账本恢复断言；`M33-F2-T5*` **5/5（123 assertions）**。Debug **976/976（139988 assertions）**、Release **976/976（140002 assertions）**、Python **104/104**。单机 T4 1280 烟测重跑通过，stderr 为空；本地事件选择/结果截图仍由 `build/m33_f2_t4_smoke/` 生成。Release 全量首轮曾命中既有 M31 存档写入性能阈值抖动，隔离 M31-T5 通过后全量复跑通过。

**限制/下一步**：离线参与者在事件结束时不会补发奖励；终态事件内容/标记不清理，客户端选项仍为键盘操作，独立聊天频道未做；多事件同时详细状态仍受“同一时间最多 1 个详细事件”约束。T6 补至少 5 类事件内容包，T7 做双客户端 15 分钟流程与 F2 全量回归。

## 2026-11-13 M33-F2-T4 单机事件 UI、电台和地图标记

**范围**：把 F2-T2/T3 的事件状态、阶段、选择结果接到单机 HUD、电台记录和世界地图；补自动超时结算入口。不做 T5 服务器选择命令、复制、manifest 存档和终态内容清理。

**实现**：新增 `src/ui/WorldEventUi.{h,cpp}` 纯文本构建，统一阶段中文名、倒计时（分钟/小时）、选项效果（声望/经验/物资/压力）和成功/失败/错过结果；`HudState` 增加事件名/区域/状态/阶段/倒计时/选项/结果/6 条电台记录字段。`WorldDemoState` 每帧查找当前 Warning/Active（或刚结束）事件，显示非模态面板；`E` 仅在 Active 且玩家在事件点 6 格内、阶段可抉择时打开，`1–8` 选择（补 `Hotbar6–8` 绑定），`ESC` 收起；离开范围自动关闭选择并提示“已错过现场 · 返回事件地点后可继续”。导演进入 Resolved/Failed/Expired 后，单机 HUD 路径自动调用 `WorldSimulation::SettleWorldEvent`，沿用事件+玩家幂等账本，超时失败声望/压力只结算一次并写结果/电台。`WorldMapState` 增加 Warning/Active 事件标记（名称/状态/区域），`WorldMapPanel` 绘制可点击橙色标记并支持导航到事件点。原生 HUD 与 `mods/core_ui` 的 RML/RCSS/Lua/Python HUD 同步；Lua/Python `Hud.GetState` 暴露新字段。面板 `pointer-events: none`，不捕获移动/攻击输入。

**验证**：新增 `tests/test_world_event_ui.cpp` 3 用例（倒计时/阶段/选项、成功/错过结果、地图标记）；`M33-F2-T4*` **3/3（36 assertions）**；`M33-F2-T1/T2/T3*` 回归通过；HUD 文档用例覆盖原生与 `core_ui` 面板元素。Debug **971/971（139857 assertions）**、Release **971/971（139871 assertions）**、Python **104/104**。1280 烟测（Debug，正常选择/地图/结果）`build/m33_f2_t4_smoke/world_event_panel_1280.png`、`world_event_choice_1280.png`、`world_event_map_1280.png`、`world_event_result_1280.png`；超时自动结算 `build/m33_f2_t4_timeout_smoke/world_event_timeout_4_1280.png`，日志显示 `声望 -5，压力 +5` 且 stderr 为空。Release 默认启动 5 秒 stderr 为空，`core_ui` 无脚本错误。

**限制/下一步**：UI 与自动超时结算仅单机；事件终态内容/标记不清理，结果面板保留 12 秒，鼠标选项与独立聊天频道未做；T5 仍需服务器权威快照/选择命令/复制/manifest 存档、重连恢复和双客户端一致。下一步 M33-F2-T5。

## 2026-11-13 M33-F2-T3 生成回调、结果与声望/战略接线

**范围**：在 F2-T2 状态机上补事件生成计划/稳定 id、单机与服务器共用 ECS 生成回调、选择/失败/超时结果、经验/物品/聚落声望/区域压力结算与按事件+玩家的幂等账本；不做 T4 UI/电台/地图、T5 服务器命令/复制/存档。

**实现**：新增 `src/game/WorldEventRuntime.{h,cpp}`。`BuildWorldEventSpawnPlan` 只从 def/stage 生成 `Kill/Defend` 僵尸和 `Loot` 容器请求，位置按事件站点确定性分布，最多 32 个，StableId 由事件 id+序号生成；`EnsureWorldEventContent` 只通过回调落地，导演不持有 ECS。`WorldSimulation` 在事件 Active 且进入详细半径时调用 `SpawnZombieAt`/`SpawnLootContainer`，写 `WorldEventActor/Container`；生成的 Kill/Defend 目标死亡后自动推进阶段，失败按稳定 id 重试而不重复生成。`BuildWorldEventOutcome` 统一生成 Resolved/Failed/Expired 结果；`WorldEventOutcomeLedger` 用事件 id+玩家 StableId 领取一次。`WorldSimulation::ChooseWorldEvent/SettleWorldEvent` 结算经验（有 Kill/Defend 阶段走 Melee，否则 Survival）、物品/背包满掉落、聚落声望与 M32 区域 pressure；F1 新增 `ApplySettlementReputationDelta/AwardSettlementReputationDelta`，M32 新增 `ApplyStrategicWorldEventEffect`，均复用既有 clamp/事件幂等。选项/事件新增 `pressure`、`failureReputation`、`failurePressure`（默认 -5/5），内容指纹升 **v17**。

**验证**：新增 `tests/test_world_event_runtime.cpp` 4 用例（生成计划/稳定 id/回调、选择与失败超时结果/账本、声望增量、压力幂等），并扩展 T1 数据契约/内容指纹断言；`M33-F2-T3*` **5/5（96 assertions）**、`M33-F2-T1*` **6/6（102 assertions）**；Debug **968/968（139778 assertions）**、Release **968/968（139792 assertions）**、Python **104/104**。Release 6 秒 world 烟测 stderr 为空；启用空配置日志 `世界事件导演已启用：0 个定义，10 个区域`。

**限制/下一步**：事件内容目前只在 Active 且进入详细半径时生成，未接 T4 UI/电台/地图或自动超时结算入口；事件容器/敌人终态清理、服务器选择命令、复制和存档留 T4/T5。基础事件文件仍为空且默认关闭，热重载后导演配置副本仍不会自动重建。下一步 M33-F2-T4。

## 2026-11-13 M33-F2-T2 事件状态机与确定性调度

**范围**：在 F2-T1 数据契约上建立可交互世界事件的纯状态机与确定性排期；不生成敌人/容器、不发奖励、不做 UI/地图标记、不写存档/网络。

**实现**：新增 `src/game/WorldEventDirector.{h,cpp}`，`WorldEventDirector` 持有事件实例（定义/区域/站点/阶段/绝对分钟）。每日至多排 1 个事件，使用 `worldEvents.randomSeed`、游戏日、定义权重和区域面积权重选择定义/区域；站点优先区域 POI，随后按区域/世界边界夹取；`activeMinute` 不早于 08:00 且给预警留出窗口。状态实现 `Scheduled→Warning→Active→Resolved` 与 `Failed/Expired`：预警从 `activeMinute-warningMinutes` 开始，Active 使用绝对 `deadlineMinute`，离开只清 `detail` 并记 `downgraded`，回来可继续；迟到加载若已过 Active 直接 `Expired`，避免无预警激活。阶段用 `ReportStageProgress` 推进，Reach 在焦点进入阶段半径时自动推进；`Choose` 要求阶段完成并校验选项；`Fail` 可显式失败；`nextEvent` 默认排到次日，保留链深度 8 和循环防护，且不会与当天详细事件重叠。`WorldSimulation` 新增 `worldEvents` 选项、`WorldEventSystem`、`WorldEvents()` 访问器；`GameApp`/`ServerHost` 传入已加载配置，默认 `enabled=false` 时不创建导演。

**验证**：新增 `tests/test_world_event_director.cpp` 4 用例 100 assertions：确定性/同日去重/跨日推进、阶段/离开降级/超时/事件链、迟到加载与非法恢复原子性、显式区域/冷却日。`M33-F2-T2*` **4/4（100 assertions）**；Debug **964/964（139675 assertions）**、Release **964/964（139689 assertions）**、Python **104/104**。Debug/Release 首轮各命中既有联机时序 flake（`M23-F2a-T6`、`M32-T4`、睡眠/闪避等待），隔离复跑通过后全量复跑全绿。Release 8 秒 world 烟测 stderr 为空；启用空配置时日志 `世界事件导演已启用：0 个定义，10 个区域`。

**限制/下一步**：事件内容生成、结果/声望/战略接线、T4 UI/电台/地图、T5 服务器权威/复制/存档均未开始；基础文件仍为空且默认关闭，`nextEvent` 当前固定次日、离开不暂停计时；内容热重载后导演仍持有旧配置副本，需在 T3/T5 或热重载收口时重建。下一步 M33-F2-T3。

## 2026-11-13 M33-F2-T1 世界事件数据契约与加载

**范围**：建立 F2 动态世界事件的唯一数据契约、加载/校验/mod 合并/内容指纹；不接状态机、生成、UI、奖励或网络，默认关闭，避免在 F1 收口后提前生成无 UI 事件。

**实现**：新增 `src/game/WorldEvent.{h,cpp}` 与 `assets/world_events.json`（`world-events-v1` 空占位）；`config/game.json` 新增 `worldEvents.enabled/file/randomSeed`，当前 `enabled=false`。`WorldEventDef` 覆盖 id/name/kind/minDay/maxDay/weight/cooldownDays/warningMinutes/durationMinutes/region/stages/choices/nextEvent；阶段支持 `reach/interact/kill/loot/defend/choice`（1–8 项），选项支持声望、经验、物品与数量（1–8 项），并校验目标、半径、奖励物品存在性、重复 id、坏 `nextEvent` 和深度 8 的循环链。基础文件单条非法跳过，全部非法时失败；`mods/<id>/content/world_events.json` 进入 `ModContentResult` 并按顺序追加，重复 id/坏定义跳过，整份无有效定义时保留旧内容。`ContentBundle`、`GameApp`、`ServerHost` 均已加载事件配置；`kContentHashVersion` 升 **v16**，指纹纳入启用状态、随机种子和全部字段。

**验证**：新增 6 个 C++ 用例（契约/默认值、非法字段/坏引用、奖励物品/mod 追加、mod 文件扫描、内容事务加载、指纹）与 Python `test_world_event_data.py` 5 用例（合法/缺省/越界/坏引用/mod 追加）；Debug **960/960（139575 assertions）**、Release **960/960（139589 assertions）**、Python **104/104**。Release 单机 8 秒 world 烟测显示内容指纹刷新为 v16、世界事件 0、stderr 为空；内容指纹不一致仍由 KCP 握手拒绝；旧配置无 `worldEvents` 时为禁用空配置，旧聚落/任务/网络内容不受影响。

**限制/下一步**：仅数据契约与加载，尚未实现 `WorldEventDirector`、排期、阶段推进、生成回调、选择/奖励结算、UI/地图标记、服务器复制与事件存档；基础事件文件为空且关闭，F2-T6 再填内容。下一步进入 F2-T2 事件状态机与确定性调度。

## 2026-11-13 M33-F1-T7 F1 回归与交付

**范围**：收尾 M33-F1 多聚落/声望包，固定双客户端跨聚落交易、任务、声望独立与服务器重启恢复证据；只补测试与交付文档，不改生产协议/存档格式。

**实现**：新增 `M33-F1-T7 双客户端跨聚落交易任务声望与重启恢复` 网络用例。两个客户端分别落在 `village_haven` 与 `north_fields`，各自购买本地商人库存（不同商品位）、接取并完成 `settlement_help`/`north_blood_drive`；验证交易 +1、任务 +10、最终声望各 11，且对方聚落保持 0，商人库存按聚落独立；服务器重启后从玩家档案恢复声望与任务完成状态、从聚落状态恢复库存。双客户端跨聚落用例 1/1（51 assertions），`M33-F1*` 定向 26/26（657 assertions）、`M23-F4-*` 27/27（559 assertions），Debug **954/954（139483 assertions）**、Release **954/954（139496 assertions）**、Python **99/99**；Debug/Release 应用构建通过。F1 完成检查表 6/6 勾选，旧档/旧客户端由 T4 的 `SettlementReputation` 缺省、`MissionLog v1`、`PlayerState v20` 用例覆盖，缺省安全且不重复发奖；1280 烟测沿用 T6 `build/m33_f1_t6_smoke/`。未提交 Git。

**限制/下一步**：交易 UI 仍只有买入，报价固定 5 档；`dialogueProfile` 仅内置口吻，自定义对话/按聚落商品表仍属后续扩展；`recruit` 服务位留给 F3；无防御聚落不自动夜袭。下一步进入 F2 动态世界事件（从 `M33-F2-T1` 开始）。

## 2026-11-13 M33-F1-T6 第 4 个聚落与聚落内容

**范围**：在 M31 东桥头新增第 4 个聚落 `river_camp`，补齐四聚落独立任务池/口吻/库存/服务差异与 3 条聚落任务；不新增地图生成器、建筑或新协议。

**实现**：`river_camp` 复用桥头可走格，中心 `(216,120)`、安全半径 4.5、区域 `riverlands`，服务只有交易/委托，关闭 `defense` 且不生成守卫；HUD 显示“防御 不设防”。四聚落任务池独立：村庄保留四段委托，北部 `north_blood_drive`、南部 `south_harvest_help`、河畔 `river_fish_supply`；3 条新任务均为 `missions-v1`/`source=npc`/`autoActivate=false`，2–3 天时限、1 天重试，最低声望 -39/0/-39，完成 +10、放弃 -10。商人库存/补货差异化；`dialogueProfile` 为四聚落根节点选择独立开场口吻，未知值回退通用。`npcPoints` 契约放宽为“至少一个商人 + 至少一个守卫或求助者”，生成点若被可走格查询推到安全区外则跳过并在后续帧重试，避免快速传送时 NPC 落到桥外。M31 内容文件同步 4 段聚落任务最低声望与 3 条新任务。

**结果**：新增 6 个 T6 用例（四聚落数据/服务差异、NPC 点位可走与道路可达、独立任务池/门槛/时限/重试、完成正声望/放弃负声望、口吻与内容指纹），T6 定向 **6/6（206 assertions）**；Debug **953/953（139431 assertions）**、Release **953/953（139446 assertions）**、Python **99/99**；Debug/Release 应用构建通过；1280 截图 `build/m33_f1_t6_smoke/river_camp_hud_1280.png`、`river_requester_dialogue_1280.png`、`river_mission_active_1280.png`、`village_trade_1280.png`、`world_map_settlements_1280.png`。未提交 Git。

**限制/下一步**：河畔营地是桥头露营点，无新增建筑/房间；世界地图烟测只显示已访问的村庄与河畔标记，四聚落数据均已进入运行时/指纹；无防御聚落仍可被战略层记录失守/恢复，但不触发自动夜袭。下一步 M33-F1-T7 F1 回归与交付。

## 2026-11-12 M33-F1-T5 声望后果与服务限制

**范围**：把已有聚落声望接到交易价格、任务门槛、五档对话态度和 `services` 入口；不做自动赠送、派系战争或按聚落商品表，当前交易 UI 仍只有买入。

**实现**：`SettlementBuyPrice`/`SettlementSellPrice` 按等级买卖倍率取整（四舍五入、最少 1），单机购买、面板报价与服务器 `TradeOffer` 共用；单机面板显示“警惕价/友善价”等实际成本。`MissionDef` 新增可选 `minSettlementReputation`（-100~100，默认 -100），聚落任务门槛 `help/escort/clear/supply = -39/0/20/30`，战略触发的 `refugee_relief` 保持默认 -100；`FindAvailableSettlementMission`、对话 `MissionAvailable/MissionNone/MissionLocked` 与服务端接取统一过滤，锁定任务显示“声望不足？”分支。对话按 Hostile/Wary/Neutral/Friendly/Trusted 生成不同态度文本；Hostile 隐藏交易/委托入口并提示“声望敌视”，防御仍可参与，守卫不主动追杀。`services` 控制交易/委托入口、实际夜袭与守卫防御，未启用的入口隐藏或显示不可用。`ContentHash` 升 v15 并纳入任务最低声望字段。

**结果**：新增 T5 五档价格/任务/对话/服务矩阵、防御服务门控、Hostile 不仇恨用例，并扩展服务器交易调价与敌对拒绝；Debug **947/947（139102 assertions）**、Release **947/947（139117 assertions）**、Python **99/99**（Release 首轮 M30-T6 闪避用例 flake，隔离复跑通过后全量复跑全绿）；Debug/Release 应用构建通过；1280 截图 `build/m33_f1_t5_smoke/trade/merchant_dialogue_wary_1280.png`（警惕对话）、`trade_price_wary_1280.png`（成本 3 木材，警惕价）、`hostile/hostile_dialogue_1280.png`（敌视拒绝委托）。未提交 Git。

**限制/下一步**：卖出倍率已有纯函数但尚无卖出报价 UI；`recruit` 服务位留给 F3 同伴入口；无防御服务的聚落仍可记录战略失守/恢复，但不自动夜袭或守卫迎战。下一步 M33-F1-T6 第 4 个聚落与聚落内容。

## 2026-11-12 M33-F1-T4 声望数据与事件接线

**范围**：把聚落声望从 HUD 数据初始值变成玩家持久状态，接通任务、交易、守城与事件入口；不做价格/任务/对话/服务后果。

**实现**：新增玩家级 `SettlementReputation`（`settlementId -> score` + 有界幂等键）与 `AwardSettlementReputation`；所有来源 clamp -100~100，任务完成/放弃/超时、守城/事件使用幂等键，交易按 `dailyTradeGainLimit` 每日每聚落限次。`MissionRunState` 增加来源 `settlementId` 并写入 `MissionLog v2`，单机/服务器接取聚落委托时记录；任务完成/失败/放弃、单机与服务器交易、围攻守城结果都在结算处接声望。单机 `SRPT v1` 组件随实体快照存档；服务器玩家档案保存 `PlayerState v21` owner-only 摘要并在重连/复活时合并回 ECS；v20/无组件旧负载默认空。HUD/世界地图改读真实分数，地图侧栏追加分数。

**结果**：新增 4 个 T4 定向用例（纯函数/ECS 边界与每日交易、组件存档往返、旧 v1 MissionLog、PlayerState v21/v20），扩展 T6 服务器重启与 T7 双客户端验证任务声望独立/恢复；Debug **944/944（139004 assertions）**、Release **944/944（139018 assertions）**、Python **99/99**；Debug/Release 应用构建通过；1280 截图 `build/m33_f1_t4_smoke/settlement_reputation_1280.png`（自然对话接取并完成 `settlement_help` 后 HUD 显示声望 10）、`settlement_map_reputation_1280.png`。日志 `build/m33_t4_*.log`。未提交 Git。

**限制/下一步**：随机世界事件通过 `EventComplete` 入口接线，未绑定聚落时默认不发声望；`attackNpc/theft` 入口保留给后续聚落攻击/偷窃行为。声望尚未影响价格、任务过滤、对话态度或 `services` 入口，这些属于 F1-T5。下一步 M33-F1-T5。

## 2026-11-12 M33-F1-T3 聚落交互路由与 HUD

**范围**：把对话/交易/任务/HUD 从“最近商人/NPC”提升为按 NPC 的 `settlementId` 解析，不新增协议，不做声望价格/服务后果。

**实现**：交易面板记录打开对话的商人 StableId，购买、提交 `TradeOffer`、范围校验、面板文本与失守/夜袭自动关闭都只使用该目标；旧无 `SettlementNpc` 商人仍兼容。服务器 `TradeOffer` 由 StableId 反查 `SettlementNpc`，要求 `Merchant`、聚落定义存在、距离/失守/夜袭合法，拒绝错误类型且不扣库存。对话与任务继续读取 NPC 聚落；HUD 新增 `settlementGuide`（聚落名、初始声望等级/分数、服务、防御状态），世界地图新增聚落标记/侧栏声望摘要；Lua/Python HUD API 与 `core_ui` 的 RML/RCSS/脚本同步。声望数值暂用 `startingReputation`，价格倍率、动态声望与 `services` 硬限制留给 T4/T5。

**结果**：新增 3 个 T3 用例（声望等级/服务摘要与聚落定位、双聚落对话/任务隔离、服务器双商人库存路由与错误 NPC 类型拒绝）；Debug **940/940（138889 assertions）**、Release **940/940（138903 assertions）**、Python **99/99**；Debug/Release 应用构建通过；1280 截图 `build/m33_f1_t3_smoke/settlement_hud_1280.png`、`settlement_map_1280.png`。日志 `build/m33_t3_*.log`。未提交 Git。

**限制/下一步**：当前声望只显示数据初始值，交易价格仍未按等级倍率变化，`services` 仍未隐藏/拒绝入口；动态声望留给 F1-T4，价格/服务后果留给 F1-T5。T3 测试覆盖两个商人库存隔离；价格矩阵在 T5 补。下一步 M33-F1-T4 声望数据与事件接线。

## 2026-11-12 M33-F1-T2 单机与服务器多聚落运行时

**范围**：消除单机初始化/日志和服务器商人中的 `PrimarySettlement` 假设，让 3 个内置聚落按 `settlementId` 各自生成商人/守卫/求助者并隔离库存、补货周期、守卫生命与夜袭进度。

**实现**：`SettlementNpcSystem::EnsurePopulation` 统一单机/服务器人口生成，按聚落只生成一次；每个聚落独立 `SpawnMerchant + SettlementNpc + MerchantStock`，旧存档未绑定商人按最近聚落接管并保留库存。单机改用该入口，删除主聚落距离门控和主聚落商人特判；服务器 `EnsureServerMerchant` 仅保留无聚落配置时的旧交易后备，`EnsureServerSettlementNpcs` 走同一人口入口。`SettlementState` 捕获/恢复覆盖全部定义；守卫死亡记录 0 生命，死亡/失守不补生，`RestoreSettlement` 清除人口标记后重建。补货按商人所属聚落 `restockDays` 计算。

**结果**：新增 5 个 T2 用例（4 聚落并存/库存隔离、旧商人迁移、状态捕获恢复、死亡/失守/恢复、服务器南部聚落真实生成）；Debug **937/937（138807 assertions）**、Release **937/937（138821 assertions）**、Python **99/99**；Debug/Release 应用构建通过。日志 `build/m33_t2_*.log`。未提交 Git。

**限制/下一步**：T2 只完成生成/状态/库存所有权；交易、任务、对话和 HUD 仍按最近 NPC 路由，尚未按聚落服务与声望限制；内容仍为 3 个聚落。下一步 M33-F1-T3 统一交互路由与 HUD。

## 2026-11-12 M33-F1-T1 聚落数据契约与声望配置

**范围**：为多聚落/声望建立数据契约和全局配置，不接线交易、任务、对话或 HUD 行为。

**实现**：`SettlementDef` 新增可选 `regionId/services/startingReputation/dialogueProfile`，旧数据缺省为 `regionId=""/trade+missions+defense/0/""`；新增 `SettlementService`/声望等级与价格倍率数据结构。`config/game.json` 新增 `settlements.reputation`（5 档阈值、6 类来源增量、5 档买卖倍率、每日交易上限），全部严格校验。3 个内置聚落补齐 regionId/services/初始声望/对话口吻；`ComputeContentHash` 升 v14，聚落新字段与声望配置进入指纹。解析器仍支持 mod 原子追加，单条非法/重复 id 跳过。

**结果**：新增 M33-F1-T1 定向 4 用例；`M23-F4-T1` 回归 5/5（87 assertions）；Debug **932/932（138716 assertions）**、Release **932/932（138730 assertions）**、Python **99/99**。Release 首轮命中既有 chunk 卸载/动物剥取时序 flake，隔离通过后全量复跑全绿。日志 `build/m33_t1_*.log`。未提交 Git。

**限制/下一步**：本任务只解析和校验；声望尚未写入玩家、未影响价格/任务/对话/服务，单机/服务器仍以主聚落为完整入口。下一步 M33-F1-T2 消除 `PrimarySettlement` 运行时假设并按聚落隔离商人/库存/NPC。

## 2026-11-12 M33 世界内容扩展规划文档

**范围**：规划 M33 五段内容包：F1 多聚落/声望、F2 动态世界事件、F3 幸存者同伴、F4 载具改装、F5 多阶段 POI 探索；共 35 个可勾选任务。

**实现**：新增 `docs/18-M33世界内容扩展.md`，明确目标/非目标、复用边界、DoD、版本兼容、35 个任务、每包验收、跨包联调与 M33 总验收；同步 README 文档索引和 docs/01 入口。本阶段只写规划，不改生产代码、协议或存档。

**状态/下一步**：规划时 0/35；下一步从 `M33-F1-T1 聚落数据契约与声望配置` 开始，每个 T 完成后停止汇报。

## 2026-11-12 M32-T25 战略贡献账本持久化

**范围**：把 T12–T24 的服务器运行时区域贡献账本写入世界存档，服务器重启后仍可用于后续 Human 转归个人经验。

**实现**：`StrategicState` 新增 `contributions`；`world_manifest_v1.bin` 升 v5，在战略区域/事件后追加 StableId/区域/点数，最多 1024 条；`ServerHost::SaveWorld` 同步运行时账本，读档时重建。v4 及更旧 manifest 仍可读取；`ZLS3/4/5` 和客户端快照不变。目标级守城伤害/火焰归属/守卫去重仍仅在运行时。

**结果**：`M32-T25` **2/2（44 assertions）**（manifest v5 往返、v4 旧档兼容、真实服务器“捐献 20 → 保存 → 重启 → 区域转 Human”仍发 120 经验）；T15–T24 保持通过，回归 **26/26（525）**、`M32-*` **67/67（1608）**；Debug **929/929（138642）**、Release **929/929（138656）**、Python **98/98**；Debug 首轮命中既有 X-F1 网络时序 flake，串行复跑通过。日志 `build/m32_t25_targeted5.log`、`build/m32_t25_regression_v2.log`、`build/m32_t25_m32_v2_1.log`、`build/m32_t25_release_full2.log`、`build/m32_t25_debug_full3.log`、`build/m32_t25_python1.log`；构建 `build/m32_t25_build5.log`、`build/m32_t25_release_build2.log`。未提交 Git。

**限制/下一步**：v5 需要当前版本读取新贡献段；目标级伤害明细和完整历史榜单仍留后续。
## 2026-11-12 M32-T24 守卫击杀助攻

**范围**：守卫收掉被玩家打伤的围攻目标时，给实际伤害者按比例分配击杀助攻点；与 T15–T23 共用账本。

**实现**：`LastDamage` 增加攻击来源 StableId；`ServerHost` 记录目标级玩家伤害明细与守卫击杀去重集合，`CheckSiegeGuardKills` 在模拟 tick 后识别“目标死亡 + 最后一击是同聚落守卫”，用 `AllocateSiegeKillAssist` 分配 `defenseKillAssistPoints`（默认 5）。玩家最后一击/无玩家伤害/非守卫不分配；不新增协议或存档版本。

**结果**：`M32-T24` **2/2（36 assertions）**（配置/分配 + 真实守城中“陷阱伤害 + 守卫 AI 自然最后一击”集成，战报贡献 51）；T15–T23 保持通过，回归 **24/24（481）**、`M32-*` **65/65（1564）**；Python **98/98**；全量 Release **927/927（138612 assertions）** 通过；Debug 本轮多次命中既有网络/M31 时序性能 flake，未取得单轮全绿，历史同构基线 927/927（138598）。日志 `build/m32_t24_targeted12.log`、`build/m32_t24_regression_v2.log`、`build/m32_t24_m32_v2_1.log`、`build/m32_t24_debug_full9.log`、`build/m32_t24_release_full7.log`、`build/m32_t24_python3.log`；构建 `build/m32_t24_build13.log`、`build/m32_t24_release_build4.log`。无新增 RML 控件或截图。未提交 Git。

**限制/下一步**：T24 集成测试由守卫 AI 自然攻击完成最后一击，但目标被直接移到守卫身边并压低生命，未覆盖远距离寻路接敌/自然波次全流程；目标级伤害明细仅运行时、不持久化；完整历史榜单、环境/自然火归属仍留后续。

## 2026-11-12 M32-T23 火焰守城伤害

**范围**：把玩家点燃/传播的火焰对围攻僵尸的实际扣血纳入 T15–T18 守城贡献账本；环境火/自然火不计。

**实现**：`FireSource`/`BurningZombie` 增加运行时 `ownerStableId`；服务器点燃篝火时写入点火玩家，火焰传播保留 owner。`FireSystem` 在即时/持续火焰伤害成功后产出 `FireDamageCredit`，`ServerHost::DrainFireDamageCredits` 在模拟 tick 后取出并调用 `RecordSiegeDefenseDamage`。owner 不写存档，环境火 owner=0 过滤；不新增协议/存档版本。

**结果**：`M32-T23` **1/1（23 assertions）**；T15–T22 保持通过，回归 **22/22（445）**、`M32-*` **63/63（1528）**；全量 Debug **925/925（138562 assertions）**、Release **925/925（138577 assertions）**、Python **98/98**。日志 `build/m32_t23_targeted1.log`、`build/m32_t23_regression.log`、`build/m32_t23_m32_1.log`、`build/m32_t23_debug_full2.log`、`build/m32_t23_release_full2.log`、`build/m32_t23_python.log`；构建 `build/m32_t23_build2.log`、`build/m32_t23_release_build.log`。Debug 首轮命中既有 X-F3/M31 性能 flake，Release 首轮命中 M23 动物网络 flake，均串行复跑通过。复用既有状态行/面板/聊天/单机战报，未新增 RML 控件或截图。未提交 Git。

**限制/下一步**：只统计玩家点火/火焰传播；环境火不计数；测试直接挂 BurningZombie owner，不覆盖自然点燃到蔓延的完整链路；owner 不持久化；守卫击杀归属、完整历史榜单仍留后续。

## 2026-11-12 M32-T22 单机侧守城战报

**范围**：补单机详细围攻的结果面板，使单机打完 M32 围攻也能看到守住/失守、波次、击杀、守卫和评级；不改奖励/战略回写。

**实现**：新增 `game::RegionalSiegeReportText` 格式化结果；`WorldDemoState::PollLocalRegionalSiegeReport` 在单机 tick 检测 `RegionalSiegeLastResult` 变化（按区域/攻击/结算时间/反攻/结果去重），调用现有 `OpenSiegeReport`，复用 T20 的 `BuildDefenseReportLines` 与 `siege-report` 面板。联机不执行，不新增协议/存档/聊天/RML 控件。

**结果**：`M32-T22` **1/1（13 assertions）**；T15–T21 保持通过，回归 **21/21（422）**、`M32-*` **62/62（1505）**；全量 Debug **924/924（138539 assertions）**、Release **924/924（138553 assertions）**、Python **98/98**。日志 `build/m32_t22_targeted1.log`、`build/m32_t22_regression.log`、`build/m32_t22_m32_1.log`、`build/m32_t22_debug_full2.log`、`build/m32_t22_release_full2.log`、`build/m32_t22_python.log`；构建 `build/m32_t22_build1.log`、`build/m32_t22_release_build.log`。Debug 首轮命中既有 X-F3/M31 性能 flake，Release 首轮命中 M23 动物网络 flake，均串行复跑通过。复用既有面板，未新增 RML 控件或截图。未提交 Git。

**限制/下一步**：单机只显示战果，不含贡献明细/经验/聊天历史；无持久化榜单；服务器无伤害 T14 回退仍可能让 AFK 均分；火焰/环境伤害、守卫击杀归属仍留后续。

## 2026-11-12 M32-T21 守城战报进入聊天记录

**范围**：把 T19 战报同时写入服务器聊天历史，状态行被覆盖后仍能在聊天记录回看；不新增协议/存档/UI 控件。

**实现**：新增 `ServerHost::PushServerChat`（“系统”发送者，200 字节/最近 16 条限制，置 `m_socialDirty`）；`PublishSiegeReport` 统一先更新状态行，再写聊天记录。T15–T18 伤害结算与 T14 无伤害回退都走同一入口。

**结果**：`M32-T21` **1/1（20 assertions）**；T15–T20 保持通过，回归 **20/20（409）**、`M32-*` **61/61（1492）**；全量 Debug **923/923（138526 assertions）**、Release **923/923（138540 assertions）**、Python **98/98**。日志 `build/m32_t21_targeted1.log`、`build/m32_t21_regression.log`、`build/m32_t21_m32_1.log`、`build/m32_t21_debug_full2.log`、`build/m32_t21_release_full.log`、`build/m32_t21_python.log`；构建 `build/m32_t21_build1.log`、`build/m32_t21_release_build.log`。Debug 首轮命中既有 X-F3 chunk 卸载性能 flake，串行复跑通过。复用既有聊天/战报面板，未新增 RML 控件或截图。未提交 Git。

**限制/下一步**：聊天消息最多 200 字节，完整 480 字节战报仍可能截断，最多保留 16 条；系统战报不写存档，重启/重连不恢复；单机无服务器会话时不生成；无伤害时 T14 回退仍可能让 AFK 均分；火焰/环境伤害、守卫击杀归属和完整历史榜单仍留后续。

## 2026-11-12 M32-T20 守城贡献战报面板

**范围**：把 T19 的 `守城战报` 状态行接入现有战报面板，玩家不再只看到短暂提示；不新增协议/服务器字段。

**实现**：`ui::BuildDefenseReportLines` 按 ` · ` 拆分 T19 文本（最多 8 行）；`WorldDemoState::OpenSiegeReport` 识别 `守城战报` 前缀，标题显示“守城战报”，复用 `siege-report` RML 面板；网络状态序列变化时自动打开。基础围攻 `守夜结束` 和评级显示保持兼容。

**结果**：`M32-T20` **1/1（8 assertions）**；T15–T19 保持通过，回归 **19/19（389）**、`M32-*` **60/60（1472）**；全量 Debug **922/922（138506 assertions）**、Release **922/922（138521 assertions）**、Python **98/98**。日志 `build/m32_t20_targeted1.log`、`build/m32_t20_regression.log`、`build/m32_t20_m32_1.log`、`build/m32_t20_debug_full2.log`、`build/m32_t20_release_full.log`、`build/m32_t20_python.log`；构建 `build/m32_t20_build1.log`、`build/m32_t20_release_build.log`。Debug 首轮命中既有 X-F3 chunk 卸载性能 flake，串行复跑通过。复用既有面板，未新增 RML 控件或截图。未提交 Git。

**限制/下一步**：面板只显示状态行拆出的最多 8 行，不显示实际伤害/来源/完整榜单；状态被覆盖后不自动刷新，单机无服务器会话时不出现；无伤害时 T14 回退仍可能让 AFK 均分；火焰/环境伤害与守卫击杀归属仍留后续。

## 2026-11-12 M32-T19 守城贡献战报广播

**范围**：守住 Human 区域后，把 T15–T18 的贡献结算以状态行广播给所有在线会话；不新增协议/存档/配置。

**实现**：`RewardSiegeContributors` 发奖后构造 `守城战报 · <region> · 守住`，按排序追加最多 3 名玩家的 `贡献+N 经验+N`，调用 `BroadcastServerNotice`；T14 无伤害回退路径广播 `N 人在场均分 X 经验`。名称截断 16 字节、整条 480 字节，避免超过 `PlayerState.status` 上限。播报是现有状态行，不是聊天/历史面板，后续状态会覆盖。

**结果**：`M32-T19` **1/1（18 assertions）**（覆盖 T14 回退播报）；T15–T18 保持通过，回归 **18/18（381）**、`M32-*` **59/59（1464）**；全量 Debug **921/921（138498 assertions）**、Release **921/921（138512 assertions）**、Python **98/98**。日志 `build/m32_t19_targeted1.log`、`build/m32_t19_regression.log`、`build/m32_t19_m32_1.log`、`build/m32_t19_debug_full2.log`、`build/m32_t19_release_full.log`、`build/m32_t19_python.log`；构建 `build/m32_t19_build1.log`、`build/m32_t19_release_build.log`。Debug 首轮命中既有 M31-T9 读档耗时 flake，串行复跑通过。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：战报只显示贡献点/经验，最多 3 人，不显示实际伤害或来源；不是持久化事件日志/聊天记录，单机无服务器会话时不弹 HUD；无伤害时 T14 仍让 AFK 均分；火焰/环境伤害、完整结算面板/榜单仍留后续。

## 2026-11-12 M32-T18 陷阱守城伤害

**范围**：把守城伤害账本扩到玩家所有的尖刺陷阱；复用 T15–T17 的配置、加权奖励与 T12 折算，不新增协议/存档。

**实现**：`TickTrapTriggers` 目标从仅 `BaseSiegeZombie` 扩到也命中 `SettlementRaidZombie`；陷阱 `ApplyDamage` 成功后按扣血差值和 `StructureOwner.ownerStableId` 交给 `RecordSiegeDefenseDamage`，无主/非围攻/无会话/零伤害由统一 helper 过滤。T15 近战、T16 远程、T17 载具已覆盖；火焰/环境伤害未接入。`ZLS3/4/5`、`PlayerState`、存档 schema 不变。

**结果**：`M32-T18` **1/1（23 assertions）**；`M32-T15/T16/T17` 保持通过、`M32-*` **58/58（1446）**。回归 **17/17（363）**；全量 Debug **920/920（138480 assertions）**、Release **920/920（138494 assertions）**、Python **98/98**。日志 `build/m32_t18_targeted3.log`、`build/m32_t18_regression.log`、`build/m32_t18_m32_1.log`、`build/m32_t18_debug_full3.log`、`build/m32_t18_release_full.log`、`build/m32_t18_python.log`；构建 `build/m32_t18_build3.log`、`build/m32_t18_release_build.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：只统计玩家所有的尖刺陷阱；测试直建陷阱，不覆盖自然建造/放置/燃料流程；陷阱现在会伤害外部围攻僵尸，只对 `SettlementRaidZombie` 计贡献；火焰/环境伤害、守卫击杀仍未接入；无伤害时 T14 在场回退仍可能让 AFK 均分；5 区域快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-12 M32-T17 载具撞击守城伤害

**范围**：把 T15/T16 的守城伤害账本扩到服务器驾驶位载具撞击；复用同一配置、加权奖励与 T12 折算，不新增协议/存档。

**实现**：`ServerHost::ApplyServerVehicleRam` 在 `ApplyDamage` 成功后读取目标扣血前后差值，交给 `RecordSiegeDefenseDamage`；贡献者取 `driverStableId`，统一过滤围攻活动、settlement、目标 `SettlementRaidZombie`、在线会话和正伤害。T15/T16 已覆盖近战与远程；乘客/动物/玩家不计。`ZLS3/4/5`、`PlayerState`、存档 schema 不变。

**结果**：`M32-T17` **1/1（28 assertions）**；`M32-T15/T16` 保持通过、`M32-*` **57/57（1423）**。回归 **16/16（340）**；全量 Debug **919/919（138457 assertions）**、Release **919/919（138471 assertions）**、Python **98/98**。日志 `build/m32_t17_targeted4.log`、`build/m32_t17_regression_final.log`、`build/m32_t17_m32_final1.log`、`build/m32_t17_debug_full.log`、`build/m32_t17_release_full.log`、`build/m32_t17_python.log`；构建 `build/m32_t17_build7.log`、`build/m32_t17_release_build.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：只统计驾驶位驾驶员的载具撞击；乘客/无人车不获得贡献，载具撞动物/玩家不计；测试直接创建围攻目标并驾车撞击，不覆盖自然围攻路线/燃料/车况长流程；陷阱、火焰/环境伤害仍未接入；无伤害时 T14 在场回退仍可能让 AFK 均分；5 区域快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-12 M32-T16 远程守城伤害

**范围**：把 T15 的守城伤害账本从近战 `HitInfo` 扩到服务器远程投射物；复用同一配置、加权奖励与 T12 折算，不新增协议/存档。

**实现**：`ServerHost::UpdateProjectiles` 在 `ApplyDamage` 成功后读取目标扣血前后差值，交给 `RecordSiegeDefenseDamage`；统一校验围攻活动、settlement 匹配、目标 `SettlementRaidZombie`、owner 在线会话和正伤害，区域切换清空。T15 近战也改走该入口。距离衰减/护甲后的实际伤害才计入；载具/陷阱/火焰仍未接入。`ZLS3/4/5`、`PlayerState`、存档 schema 不变。

**结果**：`M32-T16` **1/1（29 assertions）**；`M32-T15` **2/2（51）**、`M32-*` **56/56（1395）**。全量 Debug **918/918（138429 assertions）**、Release **918/918（138443 assertions）**、Python **98/98**。日志 `build/m32_t16_targeted1.log`、`build/m32_t16_regression_final.log`、`build/m32_t16_m32_2.log`、`build/m32_t16_debug_final.log`、`build/m32_t16_release_final.log`、`build/m32_t16_python.log`；构建 `build/m32_t16_build_final.log`、`build/m32_t16_release_build_final.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：只覆盖服务器投射物；T17/T18 已补驾驶位载具撞击与玩家陷阱，火焰/环境伤害、守卫击杀仍不计；测试直建投射物，不覆盖真实枪械开火链路；无伤害时 T14 在场回退仍可能让 AFK 均分；5 区域快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-12 M32-T15 守城伤害贡献与加权奖励

**范围**：把守城奖励从“在场均分”升级为按实际伤害贡献加权；覆盖近战 HitInfo 伤害累计、Human 守城加权、转 Human/失守折算 T12，不升战略协议。

**实现**：`StrategicContributionConfig` 新增 `defenseDamagePerPoint=10`。`ServerHost::TickServerMissions` 在详细围攻活动期间读取本 tick `HitInfo`，对 `SettlementRaidZombie` 且 settlement 匹配的玩家实际伤害累计到运行时账本；Human 区域守住且有伤害时按点数权重分配 `defenseRewardXp`，未伤害者不发；无伤害时回退 T14 在场均分。失守/反攻/非 Human 转 Human/中止时把伤害折算点转入 T12 区域贡献，后续由 T12 结算。`ZLS3/4/5`、`PlayerState`、存档 schema 不变。

**结果**：`M32-T15` **2/2（51 assertions）**；`M32-T7/T11/T12/T13/T14/T15` 回归 **14/14（283）**、`M32-*` **55/55（1366）**。全量 Debug **917/917（138400 assertions）**、Release **917/917（138414 assertions）**、Python **98/98**。日志 `build/m32_t15_targeted_final.log`、`build/m32_t15_regression_final.log`、`build/m32_t15_m32_final.log`、`build/m32_t15_debug_final.log`、`build/m32_t15_release_final.log`、`build/m32_t15_python.log`；构建 `build/m32_t15_game_build_final.log`、`build/m32_t15_release_build_final.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：T15 初版只统计近战 HitInfo；T16/T17/T18 已补远程投射物、驾驶位载具与玩家陷阱，火焰/环境伤害仍不计；敌人被守卫击杀不计玩家贡献；伤害账本运行时、不写存档；无伤害时 T14 在场回退可能让 AFK 均分；不拆分物品/弹药/战略库存，无贡献榜单/结算面板；5 区域全量快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-12 M32-T14 守城胜利在场奖励

**范围**：为 Human 区域成功守住详细围攻补一次在场玩家经验奖励；覆盖单机/联机在场、重复 tick 防重、反攻/失守不误发，不升战略协议。

**实现**：`StrategicContributionConfig` 新增 `defenseRewardXp=80`。`ServerHost::ProcessRegionalSiegeResult` 按区域/攻击分钟/结束分钟/反攻/结果去重；仅守住、非反攻、至少生成过一波且当前区域仍为 Human 时处理。结束瞬间仍在 `WorldGraph` 目标区域的本地玩家/在线会话按 StableId 排序均分经验池，经 `AwardProgressionXp(..., Survival, ...)` 发放，在线会话显示“守城奖励：+N 生存经验”。战前非 Human 转 Human 仍由 T12 结算，T14 跳过；失守/中止/无人留场不发。`ZLS3/4/5`、`PlayerState`、存档 schema 不变；T14 奖励不写 T12 贡献账本。

**结果**：`M32-T14` **2/2（17 assertions）**；`M32-T7/T11/T12/T13/T14` 回归 **12/12（232）**、`M32-*` **53/53（1315）**。全量 Debug **915/915（138349 assertions）**、Release **915/915（138363 assertions）**、Python **98/98**；全量首轮 `M23-F2b-T6`/`M30-T6` 时序抖动，串行复跑通过。日志 `build/m32_t14_targeted.log`、`build/m32_t14_regression.log`、`build/m32_t14_m32.log`、`build/m32_t14_debug_final.log`、`build/m32_t14_release_final2.log`、`build/m32_t14_python.log`；构建 `build/m32_t14_game_build.log`、`build/m32_t14_release_build.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：无伤害时只按结束瞬间在场判定，T15 已补伤害加权；固定经验池均分，不拆分物品/弹药/战略库存，无结算面板/贡献榜单；守城击杀计贡献仍留后续；5 区域全量快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-12 M32-T13 战略任务贡献归属

**范围**：把指定战略区域的服务器任务完成计入该玩家贡献，与捐献/清核/修复/反攻统一按 T12 规则拆分；不升协议、不改存档、不影响单机。

**实现**：`StrategicContributionConfig` 新增 `mission=25`，`assets/strategic.json` 显式配置。`ServerHost::CompleteServerMission` 在战略奖励结算成功后读取 `economy.FindMission(def.id)`；仅当 `regionId` 非空且区域存在时，按 `contribution.mission` 记入该玩家对该区域的贡献。任务完成状态追加“战略贡献 +N”，并写 `strategic` 日志；全局奖励任务不计。贡献仍在区域转 Human 时按比例拆分个人生存经验，结算后清空；`ZLS3/4/5`、`PlayerState`、存档 schema 不变，单机不启用账本。

**结果**：`M32-T13` **2/2（31 assertions）**；`M32-T7/T11/T12/T13` 回归 **10/10（215）**、`M32-*` **51/51（1298）**。全量 Debug **913/913（138332 assertions）**、Release **913/913（138347 assertions）**、Python **98/98**。日志 `build/m32_t13_targeted.log`、`build/m32_t13_regression.log`、`build/m32_t13_m32.log`、`build/m32_t13_debug_full.log`、`build/m32_t13_release_full.log`、`build/m32_t13_python.log`；构建 `build/m32_t13_game_build.log`、`build/m32_t13_release_build.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：只有 `missionRewards` 指定区域的服务器任务计贡献，全局奖励任务不计；只进入运行时账本，不写存档；不拆分物品/弹药/战略库存，无贡献榜单；T14/T15/T16/T17/T18 已补守城在场、近战、远程、载具与陷阱伤害贡献，火焰/环境伤害仍留后续；5 区域全量快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-11 M32-T12 玩家战略贡献与经验拆分

**范围**：在联机战略层增加按玩家贡献拆分的个人经验奖励；覆盖捐献/清核/修复/反攻计点、区域转 Human 一次性结算、重复 tick 防重，不升战略协议。

**实现**：`StrategicConfig` 新增 `contribution`（`rewardXp=120`、`donatePerSupply=1`、`clearCore=20`、`repairFacility=15`、`reclaim=30`、`minContribution=1`），`assets/strategic.json` 显式配置。`ServerHost` 按 `playerStableId` 维护区域贡献账本；`DonateStrategicSupply` 输出实际送达量，服务器按 1 点/补给计，清核/修复/反攻按权重计；行动结果显示“贡献 +N”。战略 tick 后对比区域 owner，非 Human 转 Human 时用整数最大余数法拆分经验池，经 `AwardProgressionXp(..., Survival, ...)` 发给在线贡献者并显示“战略贡献奖励：+N 生存经验”，随后清空该区域贡献。账本只在服务器运行时内存；单机、`ZLS3/4/5`、存档 schema 均不变。

**结果**：`M32-T12` **2/2（39 assertions）**；`M32-T7` **4/4（115）**、`M32-T11` **2/2（30）**、`M32-*` **49/49（1267）**。全量 Debug **911/911（138301 assertions）**、Release **911/911（138315 assertions）**、Python **98/98**；Debug 首轮与 Release 构建并行时 X-F3 性能基线超时，串行复跑通过。日志 `build/m32_t12_targeted.log`、`build/m32_t12_regression.log`、`build/m32_t12_m32_final.log`、`build/m32_t12_debug_final2.log`、`build/m32_t12_release_final.log`、`build/m32_t12_python.log`；构建 `build/m32_t12_build3.log`、`build/m32_t12_release_build2.log`。无新增地图/面板控件，未新增截图。未提交 Git。

**限制/下一步**：贡献账本不持久化，服务器重启/离线贡献者不补发，结算后清空；只奖励个人生存经验，不拆分物品/弹药/战略库存；T14/T15/T16/T17/T18 已补守城在场、近战、远程、载具与陷阱伤害奖励；火焰/环境伤害、单机贡献与贡献榜单留后续；任务归属已由 T13 补上；5 区域全量快照、4 玩家 1 条详细围攻、跨机 NAT 未变化。

## 2026-11-11 M32-T11 争夺派系 v1 与掠夺者波次

**范围**：为两个争夺区域补敌方派系元数据与掠夺者波次，区分河谷感染者与北部诊所掠夺者；验证地图/HUD/协议/外部波次并保持双争夺线回归。

**实现**：新增 `ContestedFaction::None/Infected/Raiders`，`strategic.json` 的 `contestFaction` 支持 `Infected`/`Raiders`（缺省 Infected）；`north_fields` 为 `Raiders`，`riverlands` 为 `Infected`。`StrategicMapRegion`、`ZLS3` 区域块与服务端快照增加派系字段，快照升 v2（`ZLS4/5` 不变）；侧栏/HUD 显示“争夺 · 掠夺者/感染者”。`RegionalSiegeStart` 透传派系，`SettlementNpcSystem::SpawnExternalWave` 按派系生成不同外观（感染者 `character.zombie`/红色 tint、掠夺者 `character.placeholder`/橙红 tint），掠夺者对守卫用钝击、感染者用爪击；日志/通知同步派系文案。

**结果**：`M32-T11` **2/2（30 assertions）**（配置/地图/标签 11 + 掠夺者/感染者波次外观 19）；`M32-T7` **4/4（115）**、`M32-T8` **2/2（100）**、`M32-T9` **2/2（47）**、`M32-T10` **1/1（51）**、`M32-T4` **8/8（123）**；`M32-*` **47/47（1228）**。全量 Debug **909/909（138262 assertions）**、Release **909/909（138276 assertions）**、Python **98/98**；Debug 首轮命中既有 `M31-T9` soak/路径调试计时 flake，Release 首轮命中既有 `M23-F2b-T6` 动物剥取网络时序 flake；隔离与全量复跑均通过。日志 `build/m32_t11_targeted.log`、`build/m32_t11_t7.log`、`build/m32_t11_t8.log`、`build/m32_t11_t9.log`、`build/m32_t11_t10.log`、`build/m32_t11_t4.log`、`build/m32_t11_settlement.log`、`build/m32_t11_m32.log`、`build/m32_t11_debug_final.log`、`build/m32_t11_release_final2.log`、`build/m32_t11_python.log`；截图 `build/m32_t11_strategic_map_1280.png`、`build/m32_t11_strategic_map_1920.png`、`build/m32_t11_strategic_hud_1280.png`。未提交 Git。

**限制/下一步**：派系 v1 是静态元数据与波次外观/伤害类型，不新增独立 owner、外交、持久派系实体或全图掠夺者巡逻；掠夺者仍复用 `SettlementRaidZombie` 且主要攻击守军；`ZLS3` v2 与旧 v1 快照不兼容；5 区域快照、4 玩家 1 条详细围攻、跨机 NAT 与 15–30 分钟自然流程未变化。

## 2026-11-11 M32-T10 第二争夺区域与北线

**范围**：把北部诊所指为第二个初始争夺区域；验证双争夺线、诊所守住/失守/夺回、聚落绑定与 HUD 目标选择；更新 T8/T9 真实配置组合、截图与全量回归。

**实现**：`assets/strategic.json` 将 `north_fields` 从 Human 改为 Contested（锚点仍为诊所 150,64，人口 2、压力 60、补给 50、守军/工事 1，`coreSafe=false`）。当前为 2 Human（村庄核心/南部农田）+ 2 Contested（北部诊所/河谷）+ 1 Infested（东部荒野），仍为 5 区域、`ZLS3/4/5` 不升版本。`WorldSimulation::StartRegionalSiege` 现在把锚点所在聚落写入 `RegionalSiegeStart.settlementId`，使详细围攻复用 M23 守卫，守住/失守后 `MarkFallen` 与后续 `BeginExternalReclaim` 有明确聚落目标；河谷桥无聚落时保持空值。HUD 争夺目标从首个 Contested 改为最高战略值 Contested，河谷仍优先于北部诊所。

**结果**：`M32-T10` **1/1（50 assertions）**；`M32-T9` **2/2（46）**、`M32-T8` **2/2（99）**；`M32-*` **45/45（1192）**。全量 Debug **907/907（138227 assertions）**、Release **907/907（138240 assertions）**、Python **98/98**。日志 `build/m32_t10_targeted.log`、`build/m32_t10_m32.log`、`build/m32_t10_debug_full.log`、`build/m32_t10_release_full.log`、`build/m32_t10_python.log`；截图 `build/m32_t10_strategic_map_1280.png`、`build/m32_t10_strategic_map_1920.png`、`build/m32_t10_strategic_hud_1280.png`。未提交 Git。

**限制/下一步**：第二争夺区仍复用 `Contested`，没有独立派系/外交/多方争夺；河谷桥锚点无聚落时详细反攻仍回退战略自动恢复；5 区域快照全量、4 玩家最多 1 条详细围攻；跨机与 15–30 分钟自然流程待实机确认。


## 2026-11-11 M32-T9 第五区域与争夺层

**范围**：新增 `riverlands` 第五区域与初始 Contested 争夺层；守住/失守/疏散、河谷桥通行/封锁、侧栏/HUD 争夺提示、1280/1920 截图与全量回归。

**实现**：`assets/strategic.json` 新增 `riverlands`（锚点 `bridge_east` 214,120，Contested，人口 2、压力 55、补给 40、守军/工事 1，`coreSafe=false`）。既有 T6 结算直接支持：守住转 Human；失守转 Infested 并进入清核/修复/反攻恢复，人口撤离到 `village_core`。`WorldMapPanel` 增加“争夺中 · 守住后归人类，失守将沦陷”行提示；单机/联机 HUD 显示“战略争夺/服务器战略争夺”，继续提示地图支援。core_ui Lua/Python HUD 补齐 `strategicGuide/strategicTone`，避免脚本 HUD 覆盖 native fallback 后提示不可见。世界地图道路规则验证争夺时河谷桥可通行、沦陷后封锁；`ZLS3/4/5` 不升版本。

**结果**：`M32-T9` **2/2（46 assertions）**；`M32-T8` **2/2（99 assertions）**；`M32-*` **44/44（1141）**。全量 Debug **906/906（138175 assertions）**、Release **906/906（138190 assertions）**、Python **98/98**。日志 `build/m32_t9_t9_only.log`、`build/m32_t9_t8_only.log`、`build/m32_t9_m32_debug4.log`、`build/m32_t9_debug_full2.log`、`build/m32_t9_release_full2.log`、`build/m32_t9_python_final2.log`；Release 构建 `build/m32_t9_release_build3.log`。1280/1920 地图截图 `build/m32_t9_strategic_map_1280.png`、`build/m32_t9_strategic_map_1920.png`；HUD 截图 `build/m32_t9_strategic_hud_1280.png`。未提交 Git。

**限制/下一步**：只有 1 个初始争夺区域，未做派系/外交/多方争夺；5 区域快照仍全量，4 玩家最多 1 条详细围攻；跨机 NAT/高延迟与第五区域 15–30 分钟自然流程待实机确认。M32 全量首轮遇到一次既有 T5 真实围攻时序 flaky，复跑通过。

## 2026-11-11 M32-T8 垂直切片与平衡

**范围**：3 Human + 1 Infested 区域；世界地图选择/战略行动面板；单机与 2 客户端预警→支援→守城/失守→反攻恢复；真实配置两次复跑；1280 截图与全量回归。

**实现**：`WorldMap.h` 新增 `WorldMapStrategicAction`；`WorldMapPanel` 增加战略行动面板（前往/支援+20/捐献+40/清核/修复/反攻）、自动选择预警/Active/当前区域、禁用态和回调。`UiSystem` 透传 `SetWorldMapStrategicActionHandler`；`WorldDemoState` 单机直接调用 `DonateStrategicSupply`/恢复 API，联机走 `SendStrategicRequest`，非前往行动要求锚点 4 格内。新增 `PollStrategicActionResult`，把 `ZLS5` 接受/拒绝原因写入 HUD notice，并在地图打开时刷新；HUD 预警/围攻/恢复文案提示可用地图行动。战略地图侧栏加 `overflow: auto`，1280 下行动面板不溢出。

**结果**：`M32-T8` **2/2（99 assertions）**：真实配置两次复跑守住与“失守→清核→修复→反攻→Human”闭环，覆盖 1280 面板选择/支援回调/反攻禁用；`M32-T7` 扩为 **4/4（113 assertions）**，新增双客户端清核/修复/反攻请求链。`M32-*` **42/42（1095）**；全量 Debug **904/904（138127 assertions）**、Release **904/904（138141 assertions）**、Python **98/98**。日志 `build/m32_t8_t7_net1.log`、`build/m32_t8_test6.log`、`build/m32_t8_m32_debug4.log`、`build/m32_t8_debug_full3.log`、`build/m32_t8_release_full4.log`、`build/m32_t8_python_final.log`；Release 构建 `build/m32_t8_release_build3.log`。1280 实机截图 `build/m32_t8_strategic_map_1280.png`。未提交 Git。

**限制/下一步**：支援/捐献固定 20/40，无数量滑条；捐献仍为抽象转移；4 人只 1 条详细围攻；快照全量 30 tick；1920 行动面板已由 T9 补截图，跨机 NAT/高延迟未验收。T9 已扩第五区域与争夺层；后续派系/正式美术/独立战略服；当前待玩家确认 T8/T9 手感。

## 2026-11-11 M32-T7 联机共享战略

**范围**：服务器权威战略快照/请求/结果；2 玩家共享区域状态与详细战线；4 玩家分散/同区域压力；断线重连与服务器重启；客户端只读地图/HUD；任务战略奖励与请求防刷。

**实现**：新增 `net/StrategicProtocol`（`ZLS3` 快照/`ZLS4` 请求/`ZLS5` 结果，独立 magic/version，不升 `SessionProtocol v2`）。`ServerHost` 每 30 tick/首次/变更时广播全量快照，`HandleStrategicRequest` 按玩家所在区域、战略阶段、全局补给、区域上限、请求 sequence 与每 tick 4 条限频校验；支援补给为全球库存→区域补给的抽象转移，`mission.<id>` 仍幂等。详细围攻最多 1 条，但改选当前 Active 且至少一名玩家在场的战线，其他区域自动结算。`NetworkClient` 保存只读快照/结果并在断线时清空；`WorldDemoState` 联机世界地图/HUD 使用快照。服务器任务完成接入 `GrantStrategicMissionReward`。顺带补上单机恢复 E 交互的实际输入调用。

**结果**：`M32-T7` **4/4（103 assertions）**；`M32-*` **40/40（986）**；全量 Debug **902/902（138020 assertions）**、Release **902/902（138034 assertions）**、Python **98/98**。日志 `build/m32_t7_tests6.log`、`build/m32_t7_m32_debug3.log`、`build/m32_t7_debug_full4.log`、`build/m32_t7_release_full6.log`、`build/m32_t7_python_final.log`；Release 构建 `build/m32_t7_release_build3.log`。未提交 Git。

**限制/下一步**：快照全量下发、无增量/区域裁剪；T8 已把支援/捐献/清核/修复/反攻绑定到世界地图战略侧栏；捐献不消耗背包物品/不生成商队；4 人仅 1 条详细围攻；跨机高延迟人工验收未做。下一步 M32-T8 垂直切片与平衡。

## 2026-11-11 M32-T6 NPC 守军、资源与自动结算平衡

**范围**：守军/工事/补给进入防御评分；自动结算伤亡、工事/设施/区域补给损失；每日资源产消、交易、区域补给恢复和驻军修复工事；护送/补给/难民任务转战略奖励；完善守卫对普通感染者的自主集火。

**实现**：`StrategicAutoResolveConfig` 新增 `defensePerFortification`、`heldGarrisonLossMax/heldFortificationLoss/heldFacilityDamageMax/coreFacilityDamage/coreGarrisonLossPercent/heldRegionSupplyLoss/lostRegionSupplyLoss/coreRegionSupplyLoss`；`StrategicResolutionInfo` 增加防御分项和结算前后守军/人口/工事/设施/区域补给，自动与详细回写共用严重度伤亡逻辑。新增 `economy`：Human 区域按战略值产出补给，人口/守军/难民消耗；缺补给加压力/降士气；每日恢复区域补给、交易弹药/燃料、驻军修复工事。新增 `missionRewards` 与 `ApplyStrategicMissionReward`（`mission.<id>` 幂等），单机任务完成接入 `GrantStrategicMissionReward`。M23 守卫新增 `guardEngageRadius` 与 `FindNearestThreat`，战略围攻/夜袭时先打夜袭者、再打半径内普通感染者；平民继续 Shelter。地图/HUD/预警显示守军、工事、防御分项和防御 vs 敌情。

**结果**：`M32-T6` **5/5（167 assertions）**；`M32-*` **36/36（883）**；全量 Debug **898/898（137917 assertions）**、Release **898/898（137932 assertions）**、Python **98/98**。日志 `build/m32_t6_t6_final2.log`、`build/m32_t6_m32_final4.log`、`build/m32_t6_debug_full_final5.log`、`build/m32_t6_release_full_final6.log`、`build/m32_t6_python_final.log`；Release 构建 `build/m32_t6_release_build_final4.log`。未提交 Git。

**限制/下一步**：工事修复是驻军每日摘要，未逐块维修玩家建造工事；每日经济为自动产消/自动交易，不生成商队或护送实体；详细围攻实际守卫生命与战略摘要不做逐实体对账；服务器任务奖励/经济广播留 T7。下一步 M32-T7 联机共享战略。

## 2026-11-11 M32-T5 失守、难民与夺回

**范围**：失守不是读档；平民撤到相邻 Human 区域并形成资源/任务压力；玩家清理感染核心、修复设施、守住反攻波次；区域先 Contested 再恢复 Human；道路/POI 与聚落交易恢复；测试覆盖失守→恢复→再次被围攻。

**实现**：`StrategicRegionState` 增加 `facilityDamage/refugees/infectionCores/recoveryStage/recoveryReadyMinute/refugeeHost`，`StrategicConfig.recovery` 与区域 `refugeeTarget` 数据化。失守时按人口撤离（至少保留 1 名死亡/失踪）到目标 Human 区域，宿主难民 +压力/+补给消耗并写 `refugee.*` 事件；沦陷区设施 100%、感染核心 1、进入 `ClearCore`。新增 `ClearStrategicRecoveryCore`/`RepairStrategicRecoveryFacility`/`ResolveStrategicReclaim`：核心、修复成本、反攻重试/恢复窗口与奖励走配置；反攻复用 `RegionalSiege` 新 `reclaim` 模式与独立 `beginReclaim/endReclaim` 回调，无守卫也可刷波。守住先转 Contested，`restoreMinutes` 后 Human、返还部分难民并恢复人口/守军。`SettlementNpcSystem` 新增 `MarkFallen/BeginExternalReclaim/EndExternalReclaim/RestoreSettlement`：失守撤离守卫/平民但保留商人，恢复后 `EnsureSettlementNpcs` 重建。地图对 Infested 区域道路标为不可通行、POI 标“沦陷”；HUD/战略侧栏显示反攻阶段、设施损伤、难民；单机在锚点按 E 完成三步。宿主有难民时自动接取 `refugee_relief` 任务。`WorldManifest` 升 v4 保存恢复字段，v1–v3 旧档默认 `None/0`。

**结果**：`M32-T5` **5/5（117 assertions）**（失守/难民/核心/修复/反攻/恢复/再预警、v4 恢复字段、反攻控制器、聚落撤离恢复、地图禁用、真实 ServerHost 农场闭环）；`M32-*` **31/31（711）**；全量 Debug **893/893（137745 assertions）**、Release **893/893（137759 assertions）**、Python **98/98**。真实农场闭环日志：失守→撤离→清核心→修设施→2 波 10 敌守住→Contested→恢复 Human→守卫/平民重建。日志 `build/m32_t5_t5_final.log`、`build/m32_t5_debug_full_final2.log`、`build/m32_t5_release_full_final.log`、`build/m32_t5_python_final.log`；Release 构建 `build/m32_t5_release_build_final.log`。未提交 Git。

**限制/下一步**：反攻 E 交互仅单机，服务器反攻 API 已通但网络命令/战略同步留 T7；道路/POI 目前是地图/指引层拦截，不阻断底层局部寻路；`east_wilds` 无聚落节点，暂不走详细反攻；难民为战略计数与任务触发；核心/修复为锚点交互摘要，不生成独立核心实体。T6 已补守军/工事/补给防御、自动结算伤亡/设施损失与每日经济（见上条）。

## 2026-11-11 M32-T4 详细围攻接入

**范围**：把战略 `Active` 区域接到真实区域围攻：玩家在场时预热、分波、破口、守卫/平民撤离与实时战斗；玩家离开按宽限回落自动结算；结果只回写一次。仍只控制主角，不做 RTS 编队/单位指令。

**实现**：新增 `src/game/RegionalSiege.h/.cpp`（Warmup/Waves/Finished、`maxAlive/maxWaveSize`、破口/守卫清零/玩家离开中止、M21 八向与 `BaseSiegeRatingFor` 评级）。`StrategicRegionConfig` 增加可选 `anchorX/anchorY`，`assets/strategic.json` 补 4 个锚点（村庄 75,26.5；诊所 150,64；农庄 110,210；检查站 300,280），世界地图区域导航优先用锚点。`assets/settlements.json` 扩为村庄/诊所/农场 3 节点：诊所 1 守卫+1 平民，农场 1 守卫+2 平民（`requester` 代理），两者 `minDay=9999` 不自行夜袭；`EnsureSettlementNpcs` 单机/服务器均按需生成守卫/平民，服务器找最近空格避免玩家占位。`SettlementNpcSystem` 新增 `BeginExternalDefense/SpawnExternalWave/LiveGuards/LiveRaiders/RaidersWithin/UpdateExternalDefense/EndExternalDefense`，复用 M23 `Defend/Shelter` 与 `SettlementRaidZombie`。`WorldSimulation` 读 `strategic.siege.*`，`TickStrategic` 启动控制器，`UpdateRegionalSiege` 推进，`ResolveStrategicSiege` 用同一 `siege.<region>.<minute>` 事件回写（幂等）；GameApp/ServerHost 都按玩家实际 region 填入 `playerRegions`。HUD `strategic-guide` 显示波次/敌数/守军/破口。

**结果**：`M32-T4` **8/8（121 assertions）**（控制器 4、M23 外部围攻 2、战略回写 1、真实 ServerHost+农场集成 1）；`M32-*` **26/26（593）**、`M23-F4-*` **27/27（461）**；全量 Debug **888/888（137619 assertions）**、Release **888/888（137633 assertions）**、Python **98/98**。集成用例在 `south_farmland` 锚点真实打完 3 波共 18 名感染者，守住后 owner=Human、`siege.south_farmland.0` 事件存在、围攻实体清零。日志 `build/m32_t4_targeted_final.log`、`build/m32_t4_debug_full_final.log`、`build/m32_t4_release_full_final.log`、`build/m32_t4_python_final.log`；Release 构建 `build/m32_t4_release_build_final.log`。未提交 Git。

**限制/下一步**：平民复用 `requester`，无独立 civilian；`attackRoutes` 仍空，方向用 M21 确定性 fallback；同屏一个详细围攻；围攻波次进度不跨存档，读档/重启若玩家在场从预热重开，否则由 Active 宽限/自动结算。T5 已补失守/难民/夺回与设施损坏（见上条）。

## 2026-11-11 M32-T3 战略地图与 HUD

**范围**：把 T1/T2 的区域状态、预警和资源接到 M31 世界地图与 HUD；只读展示控制区、压力/防御/补给/人口、预警倒计时/情报、事件日志、路线 ETA 与载具油耗，不做真实围攻、单位选择/编队/框选。

**实现**：新增 `src/game/StrategicMap.h/.cpp` 生成只读快照，事件按新到旧最多 8 条；`DescribeStrategicWarning` 从持久化 `nextAttackMinute`/pressure/intel 重建预警，复用 T2 固定 seed 与情报误差。`WorldMapState` 增加 `strategic`/`gameMinute`/`travel`/`routeCost`，`WorldMapRoute` 增加 `targetRegion`；`WorldMapPanel` 新增战略图层、控制区颜色、预警标签、侧栏资源/区域摘要/电台日志、路线“道路/绕路距离 + 步行/载具 ETA + 油耗/迟到”展示，点击战略区域只规划到区域中心的道路路线。`RefreshStrategicGuide` 在 HUD 显示最高预警剩余游戏小时/现实秒/情报/约敌军，围攻中显示“战略围攻”。同步 `assets/ui/worldmap.*`、`assets/ui/hud.*` 与 `mods/core_ui/ui/hud.*`；不改 manifest/协议，网络客户端战略同步留 T7。

**结果**：`M32-T3` **5/5（147 assertions）**，覆盖实际配置快照、预警重建与 tick 一致、步行/载具 ETA/绕路/油耗/无效参数、世界地图目标区域、1280×720/1920×1080 战略图层/侧栏/路线文本与不溢出；`M9-T4` HUD 文档同步 `strategic-guide`（218 assertions）。全量 Debug **880/880（137482 assertions）**、Release **880/880（137495 assertions）**、Python **98/98**。日志 `build/m32_t3_targeted11.log`、`build/m32_t3_debug_full_final2.log`、`build/m32_t3_release_full_final2.log`、`build/m32_t3_python_final2.log`；Release 构建 `build/m32_t3_release_build_final2.log`。未提交 Git。

**限制/下一步**：未做预警方向/详细波次、道路安全/中断、设施损坏、电台语音/正式美术；1280/1920 为 Rml 自动布局验收，未人工截图。T4 在玩家已在场时把 `detailedEnabled=true`，复用 M21 围攻导演/M23 守卫，离开后按宽限回落自动结算；地图/HUD 只消费 `Active`，不新增单位控制。

## 2026-11-10 M32-T2 威胁导演、预警与自动结算

**范围**：在 T1 状态层上加入战略 tick、压力增长、48/84 小时预警、`intelLevel` 强度/时间误差、固定 seed、玩家不在场时的确定性自动结算、详细围攻占位分支、核心保护与离线宽限；仍不做战略 UI 和真实围攻内容。

**实现**：新增 `src/game/StrategicDirector.h/.cpp`。达到 `attackPressure` 生成 `warning.<region>.<attackMinute>` 事件和 `nextAttackMinute`；情报误差只影响报告值，真实结算由 `round(pressure×0.7)` 与 `round(defense+8×garrison+0.25×supply)` 比较，平局守住。结果写 `siege.<region>.<attackMinute>`，事件 id 幂等；失守区域归零人口/守军，`coreSafe` 只转 `Contested` 并立即生成下一次预警。`StrategicState::ApplyEvent` 统一记录 Owner/Resource/Warning/Siege，`WorldSimulation::TickStrategic` 供单机与服务器共用；`GameApp` 单机 tick、`ServerHost` 权威 tick 已接入，网络客户端跳过。`StrategicConfig` 新增 `seed`/`pressurePerHour`/`attackPressure`/`autoResolve`，区域新增 `remote`；`assets/strategic.json` 已补默认值和东部荒野远程标记。

**结果**：`M32-T2` **8/8（208 assertions）**，覆盖实际配置、预警/远程预警/情报/固定 seed、暂停重复 tick、farm 失守、core 守住、防御=敌军边界、coreSafe 转争夺、双结算防护、提前到达 `Active`、离开/离线宽限、manifest 恢复后继续结算、压力增长不跳过预警，以及真实 `ServerHost` tick 预警+离线宽限自动结算。全量 Debug **876/876（137360 assertions）**、Release **876/876（137374 assertions）**、Python **98/98**。日志 `build/m32_t2_targeted5.log`、`build/m32_t2_debug_full_final4.log`、`build/m32_t2_release_full4.log`、`build/m32_t2_python.log`；服务器日志含 `战略预警：south_farmland ...` 与 `战略自动结算：south_farmland 失守，防御 54 vs 敌军 70`。未提交 Git。

**限制/下一步**：单机/服务器仍传 `detailedEnabled=false`，玩家到场暂走自动结算；T4 才接真实围攻并打开详细分支。没有战略地图/HUD/事件日志 UI，没有被动资源生产/难民/道路状态，预警报告不持久化。T3 在世界地图上叠加控制区/压力/防御/补给/人口/预警/ETA，只读展示，不做单位选择/编队。

## 2026-11-10 M32-T1 战略数据、状态机与 manifest v3

**范围**：把 M32 从纸面推进到可保存/恢复的纯逻辑状态层：4 个区域 owner/压力/防御/补给/人口/情报、全局四资源、事件日志、`strategic.json` 校验、M31 manifest 持久化和服务器重启恢复；不做 tick/预警/结算/UI/详细围攻。

**实现**：新增 `src/game/StrategicState.h/.cpp` 与 `StrategicPersistence.h/.cpp`。`StrategicOwner`/`StrategicSiegePhase`/`StrategicEventKind` + `StrategicRegionState`/`StrategicEvent`/`StrategicResources`；`ApplyOwnerChange`/`ApplyResourceDelta` 按事件 id 幂等，冲突拒绝，资源夹紧非负，`AdvanceTick` 单调，`Validate` 校验区域/事件/数值。`assets/strategic.json` 含村庄核心/北部农田/南部农田/东部荒野（3 Human + 1 Infested），初始资源 12/180/96/3；`WorldSimulation` 按 `world.json` region id 校验后初始化，并暴露 `Strategic()`；`GameApp`/`ServerHost` 加载配置；`WorldDemoState` 与 `ServerHost::SaveWorld` 写入 manifest。`WorldManifest` 升 v3，保留 v1/v2 解码；旧档无战略段按配置初始化。

**结果**：`M32-T1` **5/5（110 assertions）**，覆盖实际配置/与 world 对齐、状态转换/资源/幂等/非法输入、manifest v3 编解码、v2 旧档初始化、SaveManager 跨实例重载。`M31-T8` 扩展现有服务器重启测试：保存前把 `south_farmland` 改为 Contested 并扣资源，manifest 出现 4 个战略区域和 `m31-t8-strategic` 事件；重启恢复 owner/事件/资源（`M31-T8` **5/5（243 assertions）**）。全量 Debug **868/868（137152 assertions）**、Release **868/868（137166 assertions）**、Python **98/98**。日志 `build/m32_t1_debug_full_final3.log`、`build/m32_t1_release_full_final2.log`、`build/m32_t1_region_network.log`；服务器重启日志 `战略状态已恢复：4 个区域，事件 1，资源 人11 补给175 弹药94 燃料3`。未提交 Git。

**限制/下一步**：战略 tick、pressure 增长、预警、自动结算、HUD/地图和详细围攻均未做；`strategic.json` 尚未纳入内容指纹，客户端只读同步留 T7。T2 进入威胁导演/预警/确定性自动结算，复用 `nextAttackMinute`/`intelLevel`/资源字段，仍不做 UI。

## 2026-11-10 M32-T0 区域战略纸面原型

**范围**：依据当前 M31 `world.json`、M23 `settlements.json` 和 M21 `baseEvent` 做 3 个行动节点（村庄/农场/检查站）的预警→守城/失守→夺回纸面流程；不改生产代码、配置、协议或存档字段。

**结果**：首版锁定 4 个战略区域（3 Human：村庄核心/诊所/农场；1 Infested：检查站）。主路线村庄→农场 **225.0 格**（步行 56.2 秒/22.5 游戏小时；载具 22.5 秒/9.0 游戏小时、单程 27% 油），村庄→检查站 **485.5 格**（步行 121.4 秒、载具 48.6 秒、往返 116% 油，至少 2 罐）。M21 第 12 天 3 波为 9/14/19（合计 42、峰值 24、2 特殊）；M23 守卫 120 HP/18 伤害可作守军基线。纸面自动结算 `defense + 8×garrison + 0.25×supply` 在农场无人支援时为 54.25 < 62，失守；玩家在场走详细围攻并可守住。结论：24 游戏小时预警不足，改为基础 48/远程 84 游戏小时（至少步行 ETA+60 真实秒），离线宽限 2880 游戏分钟；NPC 只自主支援，不做编队/单位指令。

**限制/下一步**：农场/诊所尚无真实聚落数据；M23 守卫只搜索 `SettlementRaidZombie`，不能攻击 M21 `BaseSiegeZombie`；M21 `warningSeconds` 是真实秒、仅夜间/玩家建筑触发且联机跳过；M31 manifest 无战略字段；检查站油耗/补给点需 T6 平衡。T1 只做 `StrategicRegionState`、`strategic.json`、资源/事件日志和 manifest 持久化。未改代码、未提交 Git。

## 2026-11-10 M31-T10 垂直切片与单机/双客户端验收

**范围**：把 36×36 大地图从“能跑/能存档”推进到可玩的垂直切片：主村庄/道路、4 个 POI（含资源/危险区）、主线任务/引导、返回交付与存档重载、单机 + 2 客户端 1280 验收。不做无限世界、区域服、正式美术，也不改会话协议/命令包。

**实现**：新增 `src/world/WorldSelection.h/.cpp`，单机默认 `village_demo_m31` + `assets/maps/village_demo_m31_content.json`；`world.m31.enabled=false` 回退旧图/旧内容。专用服务器默认保持旧图，显式 `--world.mapDir=...village_demo_m31`/`serverEnabled=true` 才启用 M31；`GameApp`、`ServerHost`、`EmbeddedServer` 共用选择结果，`WorldSimulation::Options.mapDir` 只传递选择、不写回 `config/game.json`。内容 profile 使用独立 `core::Config`，mod 的 POI/任务继续合并；`ContentReload` 支持同一 profile 热重载。新增 4 个运行时 POI（诊所/农场/河畔补给点/检查站；河畔标注资源/危险区）、5 段主线、`farm_tools` 支线和 4 条村庄支线；`MissionFocusPriority` 将 `m31_slice` 固定为主任务优先级，M31 下旧电台/撤离 Objective 的 HUD/推进被隔离，HUD 目标摘要改为当前主线。地图图例增加间距。

**结果**：主路线（村庄→诊所→农场→河畔→检查站→村庄）图论距离 **1810.35 格**（步行约 7.54 分钟、载具约 3.02 分钟），加 4 POI 清剿/8 容器搜刮、区域事件、村庄支线与返回交付构成 15–30 分钟内容预算。Release 1280 单机默认 M31：42 驻留 chunk、FPS 59–62、首帧 27.1ms、地图打开 59 FPS/689 UI drawcall；截图 `build/m31_t10_slice/village-1280.png`、`map-1280.png`、`route-map-1280.png`、`route-hud-1280.png`。双客户端 1280：服务器/两客户端内容 hash 一致，2 个会话、2441 快照/失败 0、客户端 FPS 55–60；截图 `build/m31_t10_dual/client-a-1280.png`、`client-b-1280.png`。测试新增 `M31-T10 默认/回退选择`、`内容契约/路线`、`双客户端诊所/村庄`、`村庄存档/重载` 4 用例，`M31-T10*` **4/4（225 assertions）**；全量 Debug **863/863（137028 assertions）**、Release **863/863（137042 assertions）**、Python **98/98**；T8/T9 定向 **7/7（269 assertions）**。

**限制/下一步**：15–30 分钟是路线/内容预算，人工计时仍待用户验收；双客户端视觉烟测关闭区域事件/演示人群并使用 1000 HP，避免无关死亡；默认 mod 示例仍贡献 1 个 POI/任务（运行时 5 POI/11 任务）；专用服务器 M31 不是默认；chunk 仍一文件一块。M31-T10 后无限世界、区域服、正式美术均为独立任务；未提交 Git。

## 2026-11-10 M31-T9 性能、存档体积与长时稳定性

**范围**：验证 36×36 正式地图能持续玩，而不是只能打开；覆盖 1000+ 脏 chunk 后台写/读、文件数/存档体积、36×36 全图长时巡游/建造/战斗，并复验 2/4 玩家预算。不做 T10 垂直切片。

**实现**：`SaveManager` 新增 `StorageStats`/`MeasureStorage()`，统计存档文件数、总字节、chunk 文件/字节、实体桶/字节与 manifest 字节；`SaveService` 新增 `ChunkWriteStats`，记录后台 chunk 写完成/失败/重试、累计与最大耗时。新增 `tests/test_m31_stress.cpp` 2 用例：1024 个脏 chunk（共 4096 个修改格）通过 `SaveService` 后台写入并全部回读；本地玩家在 36×36 地图绕行覆盖全部 1296 chunk，期间每 600 tick 建墙、每 100 tick 生成并击杀僵尸，使用 3,888 tick × 1.0s 模拟 64.8 虚拟分钟。

**结果**：Release 1024 chunk/4096 格得到 1027 文件、280,649 bytes（chunk 268,288 bytes）；主线程快照 max **0.10ms**、入队合计 **0.11ms**，后台总写 **3,805ms**、单文件 max **13.66ms**，回读 **333ms**；T0 同步 1000 脏 chunk 为 3.88s/单文件 max 248ms，尾延迟已移出主线程。Soak 覆盖 **1296/1296 chunk**、最多驻留 **49**、累计加载 7,872/卸载 7,856、失败 **0**、6 次建造/38 次战斗，Release tick P50/P95/max **0.58/1.39/3.59ms**；Debug 同用例 P50/P95/max 11.24/27.01/64.56ms。T8 Release 复跑 **5/5（233 assertions）**，2/4 玩家仍为 50/100 chunk。Release 1280 巡航烟测 `build/m31_t9_smoke/world.out.log` 4303 tick、加载 1,912/卸载 1,863/失败 0、FPS 58–60、工作集约 192MB、1 次 32.1ms 帧告警。新增 `M31-T9*` **2/2（36 assertions）**；全量 Debug **859/859（136803 assertions）**、Release **859/859（136817 assertions）**、Python **98/98**。未提交 Git。

**限制/下一步**：Soak 是 headless 加速逻辑时间（64.8 虚拟分钟），不等于 1 小时真实墙钟；玩家数增加时仍无动态半径/密度降级（2/4 玩家当前在预算内）；chunk 仍一文件一块，1024 个文件低于 1296 目标，未按 cell 打包；1280 烟测不是自然 1 小时人工流程。下一步 T10 做主村庄+道路+2–3 POI 的 15–30 分钟自然垂直切片、单机+2 客户端验收与截图/日志归档。

## 2026-11-10 M31-T8 联机多区域、AOI 与服务器世界存档

**范围**：让 2–4 个分散在不同区域的玩家各自拥有 chunk/区域事件兴趣，服务器只加载并保存兴趣并集；按会话过滤结构快照；让服务器世界存档写入 manifest/区域桶/事件并跨重启恢复。不做 T9 长时 soak/cell 打包、T10 垂直切片，也不改 `SessionProtocol`/命令包。

**实现**：`ChunkManager::UpdateFocuses()` 取多焦点窗口并集，任一焦点可见的 chunk 不卸载，候选按距离与既有 request/attach 预算派发；`RegionStreaming::SetFocuses()` 让多个区域同时 Active，`RegionEventDirector::UpdateFocuses()` 允许事件被任一近距玩家激活；`WorldSimulation::SetFocuses/StreamAtFocuses` 接服务器兴趣并集，第一焦点继续作为 SpawnLod/动物主焦点。`ServerHost` 每 tick 先轮询网络，再汇总在线玩家、待恢复档案位置和死亡复活默认点，调用并集流式；`BroadcastStructureDiffs()` 按 `net.structureInterestRadius`（默认 48 格）过滤 chunk/墓碑；`SaveWorld` 写 `world_manifest_v1.bin` v2、区域实体桶和事件，重启读档沿用 T4/T6 校验与恢复。`SpawnRegionEventContent` 不再要求本地 `Player()`，服务器可生成区域事件内容。

**结果**：新增 `tests/test_region_network.cpp` 5 用例：多焦点区域相位/事件可见性、ChunkManager 并集与单焦点收缩、双区域兴趣+实体/结构 AOI、四玩家分散压力、服务器 manifest/桶/事件重启恢复；`M31-T8*` **5/5（233 assertions）**。全量 Debug **857/857（136767 assertions）**、Release **857/857（136781 assertions）**、Python **98/98**。Release 日志 `build/m31_t8_release_t8.log`：服务器世界保存 `实体 2，chunk 13–15，桶 1，事件 10`；双区域测试并集 **50 chunk**，四玩家分散测试并集 **100 chunk**；运行时无协议/命令包版本变化。未提交 Git。

**限制/下一步**：T4 启动仍合并全部实体桶，未做按区域实体懒加载；SpawnLod/动物仍以第一焦点为主，第二玩家普通环境刷怪依赖 POI/区域事件；四玩家压力验证并集与既有预算，未实现按玩家数动态降低半径的降级；结构 diff 仍是固定间隔广播，未做逐 chunk 脏标记。下一步 T9 做 36×36 长时 soak、1000+ 脏 chunk 存档、文件/存档体积统计与最终降级基线。

## 2026-11-10 M31-T7 世界地图、发现与长距离导航

**范围**：把 T1/T5 的 `world.json` 区域/道路与现有 POI/任务/区域事件汇总成可读的世界地图；补迷雾发现、路标/区域 HUD 与 POI 驾车路线提示。不做 T8 多区域服务器、T9 soak/cell 打包、T10 垂直切片验收。

**实现**：新增 `game/WorldMap.*`，从 `WorldGraph`/`PoiConfig`/`WorldDefinition` 构建 10 区域、10 段道路、桥梁端点派生河流、11 个路标、12 个 POI、区域事件、玩家和聚焦任务目标快照；`ui/WorldMapPanel.*` + `assets/ui/worldmap.rml|rcss` 用 RmlUi 动态图层绘制区域/迷雾、道路点阵、河流、路标、POI、事件、玩家/目标标记及图例。`M` 打开全屏地图并在单机暂停时间，按 `world.map.discoverRadius`（默认 36 格）记录已探索区域/POI；未探索区域上迷雾，未发现 POI 仅显示灰色未知标记。点击 POI 调用 `WorldGraph::PlanRoute` 生成驾车路线，关闭地图后 `HudState.regionGuide` 显示“区域 · 最近路标 | 导航 名称 距离”；不传送、不自动行驶。原生 HUD 与 `mods/core_ui` Python/Lua HUD 同步新增 `region-guide`。

**结果**：`tests/test_world_map.cpp` 2 用例（数据/发现/迷雾/事件/路线、无效地图降级）+ `test_ui.cpp` 世界地图 1280/1920 布局，`M31-T7*` **3/3（72 assertions）**。全量 Debug **852/852（136534 assertions）**、Release **852/852（136548 assertions）**、Python **98/98**。Release 1280 地图烟测 `build/m31_t7_map_smoke/world.out.log`：打开时 `已探索 POI=5/12`；点击 `fire_station` 后 `设置世界地图导航：废弃消防站（22 格）`，world 截图显示 `导航：废弃消防站 · 道路 22 格`；截图 `map-1280.png`、`route-map-1280.png`、`route-1280.png`、`map-1920.png`，地图打开采样 46–63（开图瞬间 46，稳定 60）、无 Rml 错误。未提交 Git。

**限制/下一步**：发现/导航状态只在当前会话内存，未写入 manifest；地图固定全图总览，不支持自由缩放/任意路点；河流由 bridge 边派生而非逐格水面；POI 点击只给路线，不自动行驶；无 `world.json` 的旧地图明确提示不可用；服务器地图兴趣并集与事件可见性留 T8。

## 2026-11-10 M31-T6 区域事件、冻结与事件持久化

**范围**：让大地图有区域事件，但只在玩家附近/当前区域推进；把事件状态写入 manifest，区域卸载/重载可解释地恢复。不做 T7 世界地图、T8 多区域服务器、T9 cell 打包。

**实现**：新增 `game/RegionEventDirector.*`，按 region/day/seed 确定性生成尸潮、商队/巡逻、空投、感染爆发，站点优先 POI 否则区域中心；事件仅 `Active` 且距焦点 ≤ `contentRadius`（默认 24 格）时生成内容/推进计时，`Ambient/Frozen` 冻结且不后台刷怪。`WorldSimulation` 新增 `RegionEventSystem`：尸潮/感染复用 `SpawnZombieAt`，空投/感染复用 `SpawnLootContainer`，商队复用 `SettlementNpc`；稳定 ID 保证重载不重复。`WorldManifest` 升 v2，新增事件数组并兼容 v1；单机 `WorldDemoState` 快速存档写入、`WorldSimulation::LoadSave` 恢复；`RefreshRegionEventGuide` 用 `eventGuide` 显示类型/状态/方向/距离。

**结果**：`tests/test_region_events.cpp` 3 用例（确定性/近距生成/冻结恢复、重载不重复/未知区域拒绝、manifest 往返与校验），`M31-T6*` **3/3（60 assertions）**。全量 Debug **849/849（136462 assertions）**、Release **849/849（136476 assertions）**、Python **98/98**（Release 全量首轮命中既有 `M30-T6` 双客户端闪避时序 flake，隔离与全量复跑通过）。Release 1280 m31 日志 `build/m31_t6_smoke/world.out.log`：`区域事件内容：商队/巡逻（village_core）生成 2 个，位置 (80.5,30.5)`，FPS 59–60，chunk 加载 42/卸载 0/失败 0；F12 存档 `build/m31_t6_quick_saves/20261110-032242/world/world_manifest_v1.bin` 含 10 条 `region_event_*` 事件状态。未提交 Git。

**限制/下一步**：服务器 `SaveWorld` 尚未写 manifest/事件状态（T8）；商队/巡逻为现有聚落 NPC 占位，无专属交易/护送；事件结束不自动清理已生成实体；HUD 只接 `eventGuide`，地图事件标记留 T7。

## 2026-11-10 M31-T5 道路图、地形通行与区域密度

**范围**：把 T1 的 `world.json` 道路/区域数据变成可查询的 `WorldGraph`，补齐长距离路线、地形/载具通行和 biome 密度；不做 T6 区域事件、T7 世界地图、T8 多区域服务器、T9 cell 打包。

**实现**：新增 `world/WorldGraph.*`（11 节点/10 边，Dijkstra 长距离路线、vehicle 过滤、起终点接地距离、road/bridge 距离）；`TerrainKind` 派生 road/dirt/forest/paved/water/bridge/blocked，水面不可走，桥/道路可走，`vehicle:false` 边对载具关闭；`WorldDemoState` 载具碰撞与路面分类、`ServerHost::IsServerVehicleCellWalkable` 接入载具通行；`WorldGraph` 按 region/biome 读取 `spawn` 密度，`SpawnLodSystem::SetDensityScale` 随焦点缩放虚拟尸群目标；局部 A* 继续复用 `PathfindingService`。

**结果**：`tests/test_world_graph.cpp` 3 用例 + `test_spawn_lod.cpp` 密度缩放 1 用例；`M31-T5*` **4/4（82 assertions）**。全量 Debug **846/846（136402 assertions）**、Release **846/846（136416 assertions）**、Python **98/98**。Release 1280 正式 m31 启动日志 `build/m31_t5_smoke/world.out.log`：`道路图: 11 节点/10 边，出生点→checkpoint 车辆路线 可达，距离 492.0`；出生 60fps，chunk 加载 42/卸载 0/失败 0。未提交 Git。

**限制/下一步**：道路图只提供长距离走廊，局部接地由现有 A* 承担；载具可草地/森林低速通行，不做道路专属 AI；地形标志运行时派生，不新增 chunk 持久化字段；区域事件（T6）尚未接入。

## 2026-11-10 M31-T4 世界持久化、区域实体桶与地图身份保护

**范围**：在 T3 流式加载之上补齐 `world_manifest_v1.bin`、按区域拆分的实体桶、只保存脏 chunk、卸载后台写、旧档兼容和 `mapHash` 不匹配只读保护；不做 T5 道路/密度、T6 区域事件、T8 服务器多区域兴趣、T9 cell 打包。

**实现**：新增 `save/WorldManifest.*`（`ZLWM` 段，记录地图身份/`worldSaveVersion`/chunk 索引/区域相位/实体桶）与 `save/RegionEntityBuckets.*`（按区域矩形分桶，玩家和无位置状态留全局）。`SaveManager` 在 generation 临时目录写 manifest 和 `entities_<region>_<hash>.bin`，读档合并全局+桶并拒绝重复 StableId；无 manifest 旧档走原 `entities.bin`，旧布局保存会移除 stale manifest/bucket。`SaveService` 新增后台 chunk 队列，优先于整档处理，失败重试 2 次并计数；`WorldSimulation` 卸载只提交 `IsDirty()` 的 `ChunkDiff`，`WorldDemoState` 整档只采集脏 chunk，并在保存时生成 manifest/桶。`WorldDemoState`/`WorldSimulation::LoadSave` 在加载前调用 `ReadWorldManifest` + `CheckWorldSaveCompatibility`，不匹配即停止并清 active save，避免套用旧地形。

**结果**：新增 `tests/test_world_manifest.cpp` 4 用例（manifest 编解码/桶文件安全、区域桶、manifest+桶+墙体跨重载/重启并回读车辆/尸体/动物/聚落/物品组件、旧档/身份不匹配），`test_save_service.cpp` 增加 1 个后台 chunk 队列用例；`M31-T4*` **5/5（117 assertions）**。全量 Debug **842/842（136320 assertions）**、Python **98/98**；Release 前次全量 **842/842（136314 assertions）**，最终回归 841/842（136328，唯一失败为既有 `M30-T6*` 时序 flake，隔离 2/2 通过）。Release 1280：F12 快速存档后 F8 自动巡航，累计加载 311/卸载 262/取消 5/失败 0；`build/m31_t4_unload_saves/20261109-181603` 含 manifest、4 个区域桶和 `.backup`，日志 `build/m31_t4_unload/world.out.log`；`build/m31_t4_saves/20261109-180237` 为首次退出存档。未提交 Git。

**限制/下一步**：实体桶启动时仍全量合并，未做按区域懒加载；服务端 `SaveWorld` 仍写旧全局 `entities.bin` 并会移除 manifest（T8）；终态 chunk 写失败只重试 2 次并计数，跨重启 dirty 清单留 T9；未做自然长时间跨区域手工验收。

## 2026-11-10 M31-T3 流式加载、区域焦点与预测加载

**范围**：复用 `ChunkManager` 的状态机，新增 resident 轻量窗口、速度/朝向预测、有限地图边界和 `RegionStreaming` 冻结/恢复；不接世界存档/动态实体持久化/联机多区域。

**实现**：`StreamingPolicy.h` 统一预测公式（速度方向，`ceil(speed×0.6/10)` 取 1–2 chunk，低速不预测）；`RegionStreaming` 维护 `Active/Ambient/Frozen`、focus/predicted chunk、freeze/restore 计数并夹到 `world.json` bounds；`ChunkManager` 增加 `residentRadius` 坐标摘要（不创建 Chunk）、预测加载中心、`enforceMapBounds`，request/attach 预算不变。`WorldSimulation::StreamAt` 接收焦点+玩家速度，`WorldDemoState` 从玩家/驾驶载具取得速度；旧地图不走预测/边界限制，保持既有相机窗口。

**结果**：新增 `tests/test_region_streaming.cpp` 4 用例/152 assertions，覆盖预测/区域冻结恢复/边界、resident 窗口、连续 13 chunk、快速跳跃取消、无 POI 区域卸载重载；全量 Debug **837/837（136203 assertions）**、Release **837/837（136217 assertions）**、Python **98/98**，T3 定向 Debug/Release **4/4（152 assertions）**。Release M31 冒烟：启动 36×36、mapHash b00ef11346939918；出生点因上边界驻留 42 chunk、首帧约 26ms、稳定 60fps；自动巡航累计加载/卸载并保持 49 chunk、失败 0，日志 `build/m31_t3_smoke/m31_t3_cruise.out.log`、`m31_t3_world.out.log`。未提交 Git。

**限制/下一步**：resident 只有坐标摘要，区域冻结不保存/暂停动态实体；未做自然长时间手动探索；T4 接 manifest、区域实体桶与未加载区域恢复。

## 2026-11-10 M31-T2 世界生成器与内容骨架

**范围**：新增 `tools/gen_world.py`，从 seed 生成 36×36 chunk 有限世界、`world.json`、chunk 与 POI 模板；不接运行时流式加载/存档/联机，不改变默认地图。

**实现**：生成 `assets/maps/village_demo_m31`，默认 seed `20261109`、10 个不重叠区域、11 个道路节点/10 条边、3 个 POI 模板锚点（`clinic_v1`/`farmstead_v1`/`checkpoint_v1`）、主村庄 6 栋房、河流/桥、主路/支路、8 块田地、7 处废墟、可配置森林/树木/草丛。新增 `zomboidlike-poi-template-v1` 行式模板（floor/object 行、局部 anchor、房间），生成时校验行宽/字符/门/越界、模板与道路/村庄/POI 不重叠、POI 最小中心距 88.8 格、边界 2 格水闭合、无越界对象和区域覆盖。`mapHash=b00ef11346939918`。

**结果**：36×36 共 1025/1296 个非空 chunk、827,370 bytes、6381 objects / 17 rooms；同 seed + generatorVersion 逐字节一致。新增 `tests/test_world_generator.cpp` 2 用例/1337 assertions，定向 Debug/Release 2/2；新增 Python `tools/tests/test_m31_world.py` 4 用例，Python 98/98。全量 Debug **833/833（136051 assertions）**、Release **833/833（136065 assertions）**。1280 初始视角 `build/m31_t2_smoke/m31_t2_world-08s.png`，全图预览 `build/m31_t2/world_overview.png`；启动日志确认 36×36、world.json、mapHash 加载。未提交 Git。

**限制/下一步**：没有运行时流式/区域冻结/世界 manifest；默认 `config/game.json` 仍指向 `village_demo`，仅通过 `--world.mapDir=assets/maps/village_demo_m31` 冒烟；启动首帧仍有约 445ms 的加载/挂接尖峰，留 T3 双层半径/预测加载处理。

## 2026-11-09 M31-T1 世界数据契约与校验

**范围**：新增 `zomboidlike-world-v1` sidecar 契约、只读解析/校验、地图内容指纹与存档身份检查；不生成世界、不改变 chunk 加载/存档/协议。

**实现**：`src/world/WorldDefinition.h/.cpp` 新增 `LoadWorldDefinition()`（`Loaded/Missing/Invalid`）、`ComputeMapHash()`、`FormatMapHash()`、`MakeWorldIdentity()` 与 `CheckWorldSaveCompatibility()`。校验 `bounds` 与 `map.json` 一致、区域矩形不重叠/不越界、节点唯一且连通（显式 `isolated` 豁免）、边端点/自环/宽度、POI id/模板文件/半径、区域 POI 引用和生成密度；未知字段 warn，缺字段/类型/范围/引用错误硬失败。`mapHash` 对地图目录全部普通文件按相对路径排序做 FNV-1a 64（16 位小写 hex），覆盖 map/world/chunks/templates。`WorldSimulation::Init` 在 map 元数据成功后校验 sidecar：缺失走旧路径，非法启动失败，合法仅记录身份。

**测试/文档**：新增 `tests/test_world_definition.cpp` 6 用例/76 assertions，覆盖合法契约、旧地图 Missing、缺字段/未知格式/坏 JSON/bounds 不一致、区域重叠/越界、模板缺失、POI 越界/重复、断头路/缺节点/自环、mapHash 变更与身份停止文案。全量 Debug **831/831（134714 assertions）**、Release **831/831（134728 assertions）**、Python **94/94**。`docs/mapformat.md` 新增 §7 契约/指纹规则；`docs/16` T1 勾选、README/docs/00/docs/01 状态同步。未提交 Git。

## 2026-11-09 M31-T0 大世界基线测量与可行性 Spike

**范围**：在不改正式存档/协议、不写 M31 功能代码的前提下，用临时 36×36 地图测 chunk 解析/挂接、跨 20 chunk 流式加载、1/2 玩家内存/CPU、1000 个脏 chunk 存档。临时工具与结果在 `build/m31_t0`（被 gitignore）。

**结果**：保留 **36×36 chunks**。临时地图 662/1296 个非默认 chunk、443,293 bytes、2,884 objects；冷解析 P95 **8.33ms**、热解析 P95 **0.30ms**。1 玩家初始 49 chunk **140.99ms**（CPU 62.5ms），跨 20 chunk 主线程 chunk 工作 P95 **0.033ms**、max 1.09ms，0 cancelled，最终驻留 49。2 玩家近似 98 chunks、private **15.27MB**、双路 chunk 工作 P95 0.049ms。1000 个 4 格 dirty chunk 同步写 **3.88s**，P95 6.37ms、**max 247.94ms** —— 总量可控但尾延迟必须后台保存。

**测试/文档**：Debug **825/825（134638 assertions）**、Release **825/825（134652 assertions）**、Python **94/94**；首轮旧 Release 二进制出现一次 M30-T6 时序偶发，重建后全绿，定向 M30-T6* 2/2。基线、T0 预算和限制写入 `docs/perf-baseline.md` 的“M31-T0 大世界基线”；`docs/16` T0 勾选、验收指标与默认决策更新。**未改生产源码、未提交 Git**，T1 待用户确认后开始。

## 2026-11-09 M30-T10 程序化近战挥击与战斗音效

**范围**：在不新增正式动画帧/外部音频的前提下，为本地近战增加程序化挥击弧和 7 类战斗 cue；不改变战斗数值、服务器权威、`PlayerState` v20、`SessionProtocol` v2、存档或 `ZLR1` 组件。

**实现**：`CombatPresentation.h` 新增 `MeleeSwingPresentation`/`ResolveMeleeSwingPresentation`，按 `ActionState` 进度计算 alpha、半径和厚度；轻击 range 1.7 对应 43.6px、重击对应 51.6px，半角 0.85/1.05rad，厚度 4/7px。`WorldDemoState` 用 0.44s 本地 `LocalSwingVisual` 保存方向/范围，`DrawMeleeSwingOverlay` 画全弧、扫过弧、重击外弧和弧尖。`CombatAudio.h`/`SoundIds.h` 新增 light_swing/heavy_swing/hit/block/parry/guard_break/dodge 7 个 cue（半径 6.0/7.5/7.5/7.0/8.5/9.5/4.5 格），`tools/gen_combat_audio.py` 生成 7 个占位 WAV；`PlayCombatCue` 通过 `SoundEvent` 本地发声，攻击/防御/命中事件接线。

**测试/证据**：`tests/test_combat_presentation.cpp` 覆盖挥击进度、alpha、43.6/51.6 半径、重击厚度、NaN/fallback；`tests/test_audio.cpp` 覆盖 7 个 WAV 加载；T10 定向此前 **3/3（59 assertions）** 通过。`build/m30_t10_combat_presentation_smoke.ps1` 使用 Release、`--audio.device=off`、临时 `HeavyAttack=U`，真实窗口触发 parry/block/dodge/light/heavy/exhausted，断言 5 类 cue 与挥击日志，输出 `m30-t10-combat-presentation-smoke-pass`，6 张 1280 截图在 `build/m30_t10_combat_presentation_smoke/`。文本恢复后重新构建 Debug/Release，并重跑 T10 smoke 通过；未重跑 T10 单测/全量。

**限制/下一步**：T10 只做本地表现，远端仍用 T9 姿态，无远端挥击/音效复制；合成 WAV 为占位素材；截图主要验证输入/UI/cue，实际挥击弧像素级可见性仍弱，后续用 active 帧或增强 alpha/厚度复核；全量测试和测试 exe 挂起问题仍待处理。

## 2026-11-09 M30 后续自然 100HP 四面围攻完整清场复验

**范围**：补一条默认 100HP/默认战斗规则下四方向 4 只普通僵尸的完整清场。`build/m30_natural_surround_clear_smoke.ps1` 使用真实 1280 窗口；探针在玩家 N/W/S/E 各 1.3 格生成 1 只普通僵尸并发放大锤（`sledgehammer`）；仅通过命令行 `--input.bindings.HeavyAttack=U` 临时重绑重击键（后台窗口发送 LSHIFT 不稳定），不修改 `config/game.json`；无 `--combat.*` 覆盖。

**结果**：稳定 ID 29/30/31/32；真实输入 4 次大锤重击（105/105/105/108.15）按 31→30→29→32 清除，重击后短格挡吸伤，僵尸计数 4→3→2→1→0；最终 HP **51.9**、体力 **74.74**，无玩家死亡；连续复跑最终 HP 51.3–51.9。脚本输出 `m30-natural-surround-clear-smoke-pass finalHp=51.9 finalStamina=74.74 kills=4`；证据 `build/m30_natural_surround_clear_smoke/` 3 张 1280（ready/first-pair/final），`app.err.log` 为空。

**限制**：每方向仅 1 只普通僵尸，依赖大锤重击 105 伤害一击清除；不覆盖消防斧逐只清场、轻/重击混用、连续换向、闪避脱困、重甲自然战、多波围攻、多人联机或正式动画/音效；探针生成/定位与命令行重击键重绑仍属自动化测试起点。不改源码、协议、存档或默认数值；全量基线沿用 Debug 822/134579、Release 822/134593、Python 94/94；未提交。

## 2026-11-09 M30 后续自然 100HP 双客户端轻击/受击/闪避复验

**范围**：补一条真实双客户端服务器权威自然战斗烟测。`build/m30_natural_net_combat_smoke.ps1` 使用真实无窗口服务器 + 两个 1280 客户端；除 `--combat.pvp=true` 外不添加任何 `--combat.*` 覆盖，保持默认 `health.playerMaxHp/playerStartHp=100` 与默认 `combat.rules`，只关闭背景生成/动物/POI；A/B 使用真实窗口输入发送轻击与闪避。

**结果**：A 轻击命中 B **40**（100→60），B 轻击命中 A **40**（100→60），A 闪避 B 轻击零伤害（60→60），A 收尾轻击命中 B **40**（60→20）。服务器日志 **4 条攻击目标**、**3 条 40 伤害**、无死亡；最终 A **HP 60**、B **HP 20**，默认体力回复后约 99.8。脚本输出 `m30-natural-net-combat-smoke-pass attacks=4 damageLines=3`；证据 `build/m30_natural_net_combat_smoke/` 8 张 1280（双方 ready、B 受击、A 受击、A 闪避、B 收尾受击、双方 final），`server.err.log`/`a.err.log`/`b.err.log` 为空。

**限制**：不覆盖双客户端自然重击、普通格挡、弹反、击杀、完整硬直链或完整围攻清场。自然格挡自动化在服务器 `Facing` 与出生点碰撞下无法稳定取得正面扇区，重击输入在窗口焦点切换下不稳定；构造双客户端重击/格挡/弹反/闪避仍由 `build/m30_t7_net_smoke.ps1` 覆盖，单机自然重击/格挡/弹反由 `build/m30_natural_combat_smoke.ps1` 覆盖。临时服务器朝向调试日志已移除并重建正式 Release；不改协议/存档/默认数值；全量基线沿用 Debug 822/134579、Release 822/134593、Python 94/94；未提交。

## 2026-11-09 M30 后续自然 100HP 重甲伤害/硬直边界复验（自动化隔离）

**范围**：补一条真实 POI 生成、真实输入的重甲伤害/硬直边界烟测，不改源码、协议、存档或默认战斗数值。`build/m30_natural_armored_combat_smoke.ps1` 通过动态 `pois-v1` 注册只含 1 名 `armored` 守军的 `M30 Armored Range`，探针发放 `makeshift_spear`。为自动化时序稳定，仅设置 `combat.zombieAttacks.armored.staggerSeconds=0`（避免连续硬直中断按键）与 `combat.rules.poiseRegenDelaySeconds=10`（避免脚本等待期间硬直条回复）；玩家 100HP、重甲 35% 减伤、55 硬直条、14.5 撕咬伤害等保持默认。

**结果**：守军稳定 ID 65；2 次前置轻击 + 1 次破防后补刀各 **32.5**、2 次重击各 **40.3**，第二次重击触发 `破防！目标硬直`，最后 1 次轻击击杀（32.5，剩 -10.6 HP）。玩家被重甲撕咬命中 2 次（各 14.5），最终 HP **59.2**、体力 **88.21**；脚本输出 `m30-natural-armored-smoke-pass guardId=65 lightHits=3 heavyHits=2 finalHp=59.2 finalStamina=88.21`。截图 `build/m30_natural_armored_combat_smoke/` 2 张 1280（ready/kill），日志 `build/m30_natural_armored_combat_smoke/app.log`，`app.err.log` 为空。

**限制**：这是自动化隔离烟测，不是完整自然手感试玩；动态 POI/探针定位、守卫攻击硬直关闭和硬直条回复窗口延长均属测试构造。不覆盖完整自然重甲压制、多人联机、正式动画/音效或长时间生存。单测/全量基线不变（Debug 822/134579、Release 822/134593、Python 94/94）。未提交。

## 2026-11-09 M30 后续自然 100HP 四面围攻格挡边界复验

**范围**：补自然 100HP 下 4 只普通僵尸的四面围攻初始格挡边界，不改战斗数值、`PlayerState` v20、`SessionProtocol` v2 或存档。`build/m30_natural_surround_combat_smoke.ps1` 使用默认规则，在玩家四个等距对角方向各生成 1 只普通僵尸；探针发放消防斧，生成前先按 `W` 面向西北并按住普通格挡，避免首个正面攻击落入弹反窗口。

**结果**：4 个普通僵尸稳定 ID 为 29/30/31/32；正面僵尸只造成 3.6 点格挡减伤，东北/西南/东南三个方向各造成 8 点完整伤害，日志按来源 ID 解析到 3 个不同攻击者。格挡窗口末 HP 72.4，最终 HP **71.7**、体力 **83.98**，脚本输出 `m30-natural-surround-smoke-pass finalHp=71.7 finalStamina=83.98 flankHits=3`。截图 `build/m30_natural_surround_combat_smoke/` 2 张 1280（ready/block），日志 `build/m30_natural_surround_combat_smoke/app.log`。

**限制**：本脚本只验证初始四面围攻的格挡方向边界，不执行完整围攻清场，也不覆盖连续换向、闪避脱困、弹反时序、重甲自然战、多人联机或正式动画/音效；探针生成/定位仍属构造起点。单测/全量基线不变（Debug 822/134579、Release 822/134593、Python 94/94）。未提交。

## 2026-11-09 M30 后续自然 100HP 双普通僵尸同场复验

**范围**：补自然 100HP 下的双普通僵尸同场战斗，不改战斗数值、`PlayerState` v20、`SessionProtocol` v2 或存档。`build/m30_natural_multi_combat_smoke.ps1` 使用默认 `health.playerMaxHp/playerStartHp=100`、默认 `combat.rules` 与普通僵尸攻击，不传 `--combat.*` 覆盖；只关闭背景生成/动物/POI，由探针发放消防斧并在玩家西北/西侧生成 2 只普通僵尸。

**结果**：真实输入先完成两次普通格挡；一次消防斧重击同时命中两只普通僵尸（各 92 伤害、剩 8 HP），随后轻击同时命中并以 65 伤害双杀；最终 HP **52.5**、体力 **49.41**，`zombie_count count=0`。首轮探索性连续重击会被双僵尸反复打断，最终仅剩 6.6 HP，因此最终脚本采用“重击削血 + 轻击收尾”的战术，未调整任何战斗规则。截图 `build/m30_natural_multi_combat_smoke/` 4 张 1280（ready/block/heavy/final），日志 `build/m30_natural_multi_combat_smoke/app.log`，脚本输出 `m30-natural-multi-smoke-pass`。

**限制**：两只僵尸仍位于同一前方扇区，不是真正的四面围攻；本脚本不重复覆盖闪避/弹反时序、重甲自然战、多人联机、正式动画/音效或长时间自然试玩。单测/全量基线不变（Debug 822/134579、Release 822/134593、Python 94/94）。未提交。

## 2026-11-09 M30 后续自然 100HP 全动作与格挡朝向修复

**范围**：补自然 100HP 下的重击/普通格挡/弹反/闪避验收，不改战斗数值、`PlayerState` v20、`SessionProtocol` v2 或存档。脚本 `build/m30_natural_combat_smoke.ps1` 使用默认 `health.playerMaxHp/playerStartHp=100`、默认 `combat.rules` 与默认普通僵尸攻击，不传 `--combat.*` 覆盖；只关闭背景生成/动物/POI，由探针发放消防斧并生成 1 只普通僵尸，真实窗口按键完成动作。

**结果**：重击两次 92 伤害（第一次破防、第二次击杀），普通格挡承受 3.6 伤害（消防斧 55% 减伤），弹反与闪避均零伤害；有普通爪击命中，最终 HP **62.9**、体力 **84.71**。日志含 `格挡成功（爪击）`、`弹反！攻击者硬直（爪击）`、`闪避：无敌帧`、2 条 `玩家挥击（LSHIFT 重击）`、`击杀：目标稳定 ID 29`；截图 `build/m30_natural_combat_smoke/` 6 张 1280（ready/dodge/parry/block/heavy/final），脚本输出 `m30-natural-combat-smoke-pass`。

**修复**：自然复验发现第一次格挡后的击退速度会让 `AnimationSystem` 把 `Facing` 转向远离攻击者，后续攻击落在 140° 格挡扇区外。`AnimationSystem` 现在在 `HitReaction` 硬直或击退速度未衰减时保持原朝向，衰减后再由移动更新；新增 `M30-T4` 回归用例。`M30-T4*` 现为 **5/5（33 assertions）**。

**测试/证据**：全量 Debug **822/822（134579 assertions）**、Release **822/822（134593 assertions）**、Python **94/94**；日志 `build/m30_natural_combat_debug_full.log`、`build/m30_natural_combat_release_full.log`、`build/m30_natural_combat_python.log`、`build/m30_natural_combat_smoke.log`、`build/m30_natural_fix_targeted.log`。单只普通僵尸、探针生成/定位仍是构造起点；不覆盖重甲、多人围攻、正式动画/音效或长时间自然试玩。未提交。

## 2026-11-09 M30-T9 远端格挡/闪避姿态

**范围**：补齐 T8 遗留的远端姿态表现，不改服务器权威战斗判定、体力/伤害、存档或 `PlayerState v20`。`ZLR1` 新增 `CombatStance`（`CBST`，25B）组件，只抓取 `PlayerTag`，复制 blocking/blockElapsed/guardBreakLeft/dodgeLeft/dodgeInvulnLeft/dodgeDirX/Y；客户端 `ReplicatedActor` 全量/增量保存姿态，非法值/非法闪避方向拒绝。

**表现**：新增 `game/CombatPresentation.h` 纯映射（闪避优先、弹反窗口、破防、闪避比例/无敌/方向归一）；`DrawRemoteCombatOverlay` 按 `Facing` 画格挡盾弧、弹反白色内弧、破防红 X、闪避残影/方向弧/无敌横线，跳过本地、死亡、载具隐藏与 AOI 外实体。不新增贴图/音频；正式动画仍不在此步。

**测试/证据**：`M30-T9*` **4/4（52 assertions）**，覆盖姿态映射、非法方向/时长回退、`CombatStance` 全量/增量与非法负载拒绝；`M30-T6*` 扩为 **2/2（56 assertions）**，增加远端复制端断言。全量 Debug **821/821（134576 assertions）**、Release **821/821（134589 assertions）**、Python **94/94**；日志 `build/m30_t9_debug_full.log`、`build/m30_t9_release_full.log`、`build/m30_t9_python.log`、`build/m30_t9_targeted.log`、`build/m30_t9_net_targeted.log`。`build/m30_t7_net_smoke.ps1` 先在开放地 staging 再驱动格挡/闪避，`build/m30_t9_remote_pose_smoke/` 9 张 1280（原有 5 + 4 张远端），输出 `m30-t9-remote-pose-pass`。

**限制/下一步**：姿态是线段/矩形标记，不是骨骼动画；只复制姿态，不复制体力/冷却；`ZLR1` 组件注册数 28→29，旧客户端需同版本重建；烟测用长闪避/长弹反构造参数，不替代自然手感。未提交。

## 2026-11-09 M30 逐武器平衡基线

**范围**：补 M30 的“逐武器平衡表自动生成”缺口，不改运行时战斗代码、配置、协议或存档。新增 `tools/combat_balance_report.py`，读取 `config/game.json` 与 `assets/items.json`，按 C++ 轻/重击派生、四变体硬直条、重甲 35% 减伤和格挡公式，生成 `docs/15-M30战斗平衡基线.md` 与 `build/combat_balance_report.json`。

**结果**：覆盖 11 把近战、轻/重击参数、DPS、伤害/体力、满体力轻击次数、格挡承伤/体力，以及普通/快速/强壮/重甲 TTK 与破防时间。当前读数：厨刀轻击 DPS **127.27** 最高、轻击伤害/体力 **7.00** 最高；砍刀重击 DPS **121.66** 最高；厨刀/简易长矛/大锤重击 DPS 低于轻击（换取破防/硬直）；消防斧轻/重 DPS **92.86/96.84**。新增 `tools/tests/test_combat_balance_report.py` **4/4**，全量 Python **94/94**。

**限制/下一步**：报告是公式化数据基线，不含移动/未命中/成长/疼痛/疲劳/自然操作差异，也不替代实机试玩；未调整任何武器数值。新增测试使 Python 计数从 90 升至 94，Debug/Release 不变。未提交。

## 2026-11-09 M30 后续自然 100HP 轻击烟测

**范围**：补一条不修改玩家生命、战斗规则或僵尸伤害的自然轻击验收；仍按 T7 方式关闭背景生成/动物/POI，由探针发放消防斧并依次生成 3 只普通僵尸。

**结果**：默认 `health.playerMaxHp/playerStartHp=100`、默认 `combat.rules`；6 次 `SPACE` 轻击全部命中并击杀，仅被爪击 1 次（8 伤害），最终 **约 90 HP**（体力最低约 69，复跑末尾约 84–89）；脚本 `build/m30_natural_smoke.ps1`，截图 3 张在 `build/m30_natural_smoke/`。

**限制**：只覆盖自然 100HP 下的轻击/体力/受伤；自然重击、格挡、弹反、闪避仍由 T7 构造烟测覆盖，未新增代码/协议/存档字段。

## 2026-11-09 M30 战斗 2.0 T1–T8 完成

**范围**：把单键近战升级为体力、轻重击、格挡/弹反、闪避与敌人硬直循环；单机先行，联机保持服务器权威。T1 规则/武器数据、T2 ECS 核心、T3 单机轻重击/体力 HUD、T4 格挡/弹反/闪避、T5 敌人硬直/反馈、T6 联机权威/v20、T7 配置/平衡/1280 烟测、T8 文档/清理/交付全部完成。

**实现**：`CombatComponents` 增加 `CombatStamina/CombatStance/Poise/AttackIntent`；`CombatRules`/`PlayerCombat` 统一轻/重击派生、体力、格挡/弹反/闪避与武器覆盖；`CombatSystem` 推进前摇、体力/姿态、硬直条并按闪避无敌→格挡/弹反→护甲/变体减伤→伤口/硬直/死亡结算。`WorldSimulation::LoadCombatRuleConfig` 读取 `config/game.json` 的 `combat.rules.*`（43 键）并在单机/服务器共用钳制；四变体硬直上限（20/14/32/55）与重甲轻击阈值（16）由 `ZombieFactory` 注入。HUD 体力条改用 `CombatStamina`，格挡/闪避/弹反/破防走 notice；Lua/Python `combatStatus/combatTone` 与 native/core_ui `combat-status` 同步。

**联机**：`PlayerState` 升 **v20**，末尾仅 owner 追加 `PlayerCombatState`（体力/上限/姿态/弹反窗口/破防/闪避无敌）；旧 v19 及更早回退满体力/None。`Attack` 复用 flags bit0 表示重击，新增 `Block=48`（按下/松开）与 `Dodge=49`（世界方向）。服务器校验体力、姿态、硬直、驾驶/死亡/冷却并权威写 `AttackIntent`/`CombatStance`；`combat.pvp=false` 时玩家目标不可选。客户端只发命令并读 `PlayerState` 镜像；`ZLR1` 不新增战斗体力/姿态，远端格挡/闪避动画不在 v1。`SessionProtocol` v2/命令包 v1 不变，旧客户端需重建后加入 v20。

**测试/证据**：新增 `M30-T7*` **3/3（60 assertions）**（`combat.rules` 读取/默认回退/钳制、消防斧轻/重击变体 TTK/体力/硬直/格挡收益、变体 Poise 上限注入）；全量 Debug **817/817（134520 assertions）**、Release **817/817（134534 assertions）**、Python **90/90**；日志 `build/m30_t8_debug_full.log`、`build/m30_t8_release_full.log`、`build/m30_t8_python.log`、`build/m30_t7_targeted.log`。Release 1280 单机构造烟测 `build/m30_t7_combat_smoke/` 6 张（轻击/重击/格挡/弹反/闪避/体力耗尽），双客户端 PvP `build/m30_t7_net_smoke/` 5 张；服务器 6 条攻击目标、仅 2 条伤害（未格挡 10、普通格挡 3.5），弹反/闪避零伤害。

**限制/下一步**：烟测使用低体力/长弹反窗口/长闪避时长等构造参数，单机 3 只普通僵尸；变体上限/阈值由 T5/T7 单测覆盖，不替代自然 100HP 手感试玩。远端格挡/闪避动画、更细动作/正式音效仍属后续；逐武器平衡表已由 `docs/15` 补齐；动物保持既有受击规则，不初始化 `Poise`。未提交。

## 2026-11-08 M26-V14 低速持续接触冲量

**范围**：在 V13 的车身/轮胎模型上，补车损门槛以下的载具间接触冲量：接触但接近速度低于 `vehicleCollisionMinSpeed` 时仍按质量传递完全非弹性法线速度，让低速顶推可推动停放/无人物车；不改高速碰撞的车况/乘员伤害/推挤/音效/冷却，不新增命令、配置、协议或存档字段。

**实现**：新增 `kVehicleContactMinSpeed=0.15f` 与 `VehicleContactImpulseFor`，接触路径恢复系数/切向摩擦为 0；`VehicleCollisionImpulseFor` 追加可选 `minClosingSpeed`，默认仍用 `vehicleCollisionMinSpeed`。服务器 `ApplyServerVehicleVehicleCollisions` 在 `bodyContact`/`bodyCollision` 时先计算接触冲量，无车损时只写速度并同步目标 `Position`/`Velocity`/乘员；无人目标由既有 `vehicleCoastBraking` 滑行，不扣车况、不伤乘员、不广播 `kVehicleRam`、不设置碰撞冷却。单机 `ApplyLocalVehicleVehicleCollision` 复用同一纯逻辑，低速目标写入 `m_localParkedVehicleMotions` 并滑行；接触日志降为 debug。

**测试/证据**：新增 `M26-V14 VehicleContactImpulse` **1/1（15 assertions）**、`M26-V14 服务器载具低速接触` **1/1（20 assertions）**；`M26*` **25/25（421 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **59/59（750 assertions）**、`M25-T4* + M26*` **58/58（1003 assertions）**；全量 Debug **782/782（133914 assertions）**、Release **782/782（133928 assertions）**、Python **89/89**；日志 `build/m26_v14_*.log`。Release 1280 重跑 V5 单机多车烟测 `build/m26_v14_v5_regression_smoke/`：A/B 驾驶 4.02/5.06 格、走回 A 漂移 0.00 格，stderr 为空。

**限制/下一步**：只做速度传递，不做接触位置分离/持续法向约束；目标被墙体/第三辆车卡住时不会强制挤出；低速接触无独立音效/车损/乘员伤害；阈值是代码常量。仍缺稳定 1280 碰撞/接触构造烟测；下一步优先补 1280 碰撞/乘客/接触验收，再考虑逐车轮接地/纵向滑移或完整刚体。未提交。

## 2026-11-08 M26-V13 轮胎转向极限与悬挂姿态

**范围**：在 V12 显式车型/heading 上补每车型轴距、轮距、最大前轮转角、轮胎横向加速度上限和悬挂 roll/pitch，加入自行车模型转向不足与运行时车身姿态；不改命令、协议、存档或复制字段。

**实现**：`VehicleProfile` 轻/标准/重轴距 1.02/1.15/1.28 格、轮距 0.84/0.90/0.98 格、最大前轮转角 0.92/0.85/0.72 rad、最大横向加速度 20/18/15 格/秒²；`VehicleTuningForType` 继承到单机、服务器和客户端预测。`VehicleTireYawRate` 用 `speed*tan(steer)/wheelbase` 求横摆，并由 `turnRate` 与 `maxLateralAcceleration/max(speed,0.5)` 双重钳制；`VehicleStep` 先求前轮转角再更新 heading，低速仍直接对齐。`VehicleMotion` 新增 `steerAngle/lateralAcceleration/longitudinalAcceleration/bodyRoll/bodyRollVelocity/bodyPitch/bodyPitchVelocity`；`VehicleSuspensionStep` 用弹簧-阻尼趋近目标姿态，运行时数据不写存档/不复制。`VehicleSurfaceProfile` 追加横向附着倍率：泥土路 0.90、草地 0.62。

**测试/证据**：新增 `M26-V13 VehicleProfile` **1/1（10 assertions）**、`M26-V13 VehicleTire` **1/1（9 assertions）**、`M26-V13 VehicleSuspension` **1/1（8 assertions）**、`M26-V13 VehicleStep` **1/1（4 assertions）**；`M26*` **23/23（386 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **58/58（735 assertions）**、`M25-T4* + M26*` **56/56（968 assertions）**；全量 Debug **780/780（133879 assertions）**、Release **780/780（133893 assertions）**、Python **89/89**；日志 `build/m26_v13_debug_full.log`、`build/m26_v13_release_full.log`、`build/m26_v13_build_debug.log`、`build/m26_v13_build_release.log`、`build/m26_v13_v5_smoke.log`、`build/m26_v13_python.log`。Release 1280 重跑 V5 单机多车烟测 `build/m26_v13_v5_regression_smoke/`：3 辆车 A/B 驾驶 2.57/3.95 格、走回 A 漂移 0.00 格，stderr 为空。

**限制/下一步**：没有逐车轮接地采样、轮胎纵向滑移/ABS、悬挂碰撞、车身姿态渲染旋转或持续接触冲量；姿态只是运行时状态；稳定 1280 双车碰撞/乘客构造烟测仍待补。下一步优先补可重复的碰撞/乘客 1280 验收，再考虑持续接触冲量或逐车轮模型。未提交。

## 2026-11-08 M26-V12 显式车型与完整车身朝向

**范围**：把 V11 的 StableId 质量档升级为显式车型表，并把碰撞角速度从短时 `spinAngle` 外观升级为持久 `heading` 车身朝向；加入转向速率、侧向抓地与车型性能/损伤倍率，补齐存档/ZLR1/PlayerState 复制。新增命令/配置为零，但存档、ZLR1、PlayerState 与复制组件版本均前移。

**实现**：`ecs::VehicleType` 为轻/标准/重皮卡；`VehicleProfile` 表含质量 1100/1600/2400 kg、半径 0.60/0.65/0.65 格、长宽、极速/加速/制动/转向/侧向抓地/车况伤害/乘员伤害倍率。`VehicleMotion` 新增 `heading/type`，`VehicleStep` 先转向再沿 heading 加速并按 `lateralGrip` 衰减侧向速度，角速度积分到 heading，`spinAngle` 移除；单机/服务器按 `VehicleTuningForType` 使用各自车型参数，质量用于冲量、半径用于碰撞/角惯量、质量比缩放推挤距离，车型倍率缩放车况/乘员伤害。`ecs::Vehicle` 持久化 type/heading，存档 Vehicle 组件 v3、ZLR1 `VHCL` 37B、复制组件 v4/40B，`PlayerState` v19；客户端预测用 `VehicleCorrectHeading` 平滑校正，HUD/日志显示车型/质量。

**测试/证据**：新增 `M26-V12 VehicleProfile` **1/1（22 assertions）**、`M26-V12 VehicleIntegrateAngular` **1/1（6 assertions）**、`M26-V12 VehicleStep` **1/1（5 assertions）**、`M26-V12 VehicleCorrectHeading` **1/1（4 assertions）**；`M26*` **19/19（360 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **54/54（704 assertions）**、`M25-T4* + M26*` **52/52（937 assertions）**；全量 Debug **776/776（133848 assertions）**、Release **776/776（133862 assertions）**、Python **89/89**；日志 `build/m26_v12_targeted.log`、`build/m26_v12_m26.log`、`build/m26_v12_vehicle.log`、`build/m26_v12_combined.log`、`build/m26_v12_debug_full.log`、`build/m26_v12_release_full.log`、`build/m26_v12_build_debug.log`、`build/m26_v12_build_release.log`、`build/m26_v12_app_debug.log`、`build/m26_v12_app_release.log`、`build/m26_v12_python.log`。Release 1280 重跑 V5 单机多车烟测 `build/m26_v12_v5_regression_smoke/`：A/B 驾驶 3.57/3.92 格、走回 A 漂移 0.00 格，HUD 显示 `标准皮卡 1600kg`/`重型皮卡 2400kg`，stderr 为空。

**限制/下一步**：没有车轮/悬挂/轮胎受力、持续接触冲量或完整刚体求解；视觉仍是八向资源；重型碰撞半径不放大到标准车以上以保持狭窄道路通行；未新增 1280 碰撞烟测。下一步优先 1280 碰撞/乘客验收，再考虑车轮/悬挂与持续接触模型。未提交。

## 2026-11-08 M26-V11 分级质量与碰撞角动量

**范围**：在 V9/V10 的线性冲量与乘员伤害基础上，把质量参数接入载具间碰撞，并增加切向摩擦产生的角速度与短时八向外观偏转。只改碰撞速度/外观与配置，不改命令、复制字段、协议或存档 schema。

**实现**：`VehicleSpike.h` 新增 `VehicleMassClass`（轻/标准/重）与 `VehicleMassClassForStableId`，质量 0.85/1.0/1.35 按 StableId 稳定分配；`VehicleCollisionImpulseFor` 用双方质量计算法线冲量并保持动量守恒。新增 `vehicleCollisionFriction` 切向摩擦冲量与 `VehicleAngularInertia`，产生双方 `angularVelocity`；`VehicleIntegrateAngular` 在 `VehicleStep`/`VehiclePredictStep` 中积分 `spinAngle` 并按 `vehicleAngularDamping` 回正。服务器写回双方角速度并通过既有 `visualDirection`/`SpriteRef` 复制；单机活动车与 `m_localParkedVehicleMotions` 停放车复用同一角速度与八向外观；HUD 显示质量档位。新增 `vehicleCollisionFriction`/`vehicleAngularDamping` 配置，默认 0.35/3.0。

**测试/证据**：新增 `M26-V11 VehicleMass：StableId 分级与碰撞质量比` **1/1（13 assertions）**、`M26-V11 VehicleCollisionImpulse：切向摩擦与角速度` **1/1（10 assertions）**、`M26-V11 VehicleIntegrateAngular：角速度偏转与阻尼回正` **1/1（6 assertions）**、`M26-V11 服务器载具碰撞：分级质量与角速度` **1/1（24 assertions）**；`M26*` **17/17（337 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **52/52（684 assertions）**、`M25-T4* + M26*` **50/50（919 assertions）**；全量 Debug **774/774（133822 assertions）**、Release **774/774（133836 assertions）**、Python **89/89**；日志 `build/m26_v11_targeted.log`、`build/m26_v11_m26.log`、`build/m26_v11_vehicle.log`、`build/m26_v11_combined.log`、`build/m26_v11_debug_full.log`、`build/m26_v11_release_full.log`、`build/m26_v11_python.log`。服务器日志实测质量 1.35/0.85、接近速度 4.0、角速度 1.12/1.79。

**限制/下一步**：角速度只驱动短时八向外观偏转/阻尼回正，不改变驾驶输入目标方向；没有完整刚体朝向、车轮/悬挂/轮胎抓地、持续接触冲量或 1280 碰撞烟测。下一步优先补 1280 碰撞验收，或继续完整刚体朝向/持续冲量。未提交。

## 2026-11-08 M26-V10 碰撞乘员伤害

**范围**：在 V2/V4/V9 载具间碰撞、推挤和冲量基础上，对碰撞双方车内乘员结算生命伤害。只改乘员生命、提示与配置，不改命令、复制字段、协议或存档 schema；撞墙/撞僵尸/撞动物仍走各自既有路径。

**实现**：`VehicleSpike.h` 新增 `VehicleCollisionOccupantDamage`，以 `vehicleCollisionMinSpeed` 为门槛，按 `base + (impactSpeed - minSpeed) × perSpeed` 计算 HP，并受 `max` 上限钳制；NaN/非法配置返回 0。服务器遍历碰撞双方驾驶位、第一乘客位和第二乘客位，对存活乘员发出一次 `Blunt` 伤害事件，不施加击退/硬直，保留既有护甲钝击减伤，并通过 `stateDirty`/`PlayerState` 同步生命。单机在 `ApplyLocalVehicleVehicleCollision` 中对当前驾驶员使用同一规则并显示 `碰撞受伤：-X HP`。新增 `vehicleCollisionOccupantDamageBase/PerSpeed/Max` 配置，默认 3.0/1.2/20.0。

**测试/证据**：新增 `M26-V10 VehicleCollisionOccupantDamage：速度映射与配置上限` **1/1（10 assertions）**；V2 服务器三客户端碰撞用例追加双驾驶员+乘客 HP 下降断言（V2 服务器用例 **49 assertions**）；`M26*` **13/13（284 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **49/49（660 assertions）**、`M25-T4* + M26*` **46/46（866 assertions）**；全量 Debug **770/770（133769 assertions）**、Release **770/770（133783 assertions）**、Python **89/89**；日志 `build/m26_v10_passenger_v2.log`、`build/m26_v10_m26_passenger.log`、`build/m26_v10_vehicle_passenger.log`、`build/m26_v10_combined_passenger.log`、`build/m26_v10_debug_passenger_full.log`、`build/m26_v10_release_passenger_full.log`、`build/m26_v10_python.log`。V2 日志实测两名驾驶员和乘客各扣约 4.4 HP（剩余 96 HP），车况各 -1.9%。

**限制/下一步**：V10 未新增 1280 碰撞烟测，但 V2 三客户端集成已覆盖乘客槽位；不做角动量/旋转、分级质量、持续冲量或轮胎抓地模型；撞墙/撞僵尸/撞动物不结算乘员生命伤害。下一步优先补 1280 碰撞/乘客验收或完整刚体朝向。未提交。

## 2026-11-08 M26-V9 等质量碰撞冲量与无人车滑行

**范围**：在 V2/V4/V6 位置阻挡、推挤和双方车况基础上，增加等质量线性碰撞冲量。撞击车与被撞车按中心法线分配碰撞后速度，被撞无人车可短时滑行；同时用 `bodyContact` 让轴滑动接触也触发碰撞结算。只改位置/速度表现与碰撞触发，不改命令、复制字段、协议或存档 schema。

**实现**：`VehicleSpike.h` 新增 `kVehicleCollisionRestitution=0.20`、`kVehicleCoastBraking=3.0`、`VehicleCollisionImpulseFor` 与 `VehicleStepResult::bodyContact`；冲量保留切向分量，NaN/非法质量/低于最小速度返回未应用，位置重合时用速度方向或 +X 兜底。服务器在 V2/V4 结算后写回双方 `VehicleMotion`，无人物车用 `VehiclePredictStep` 按 3.0 格/秒² 滑行，不消耗燃料、不追加车损；单机新增 `m_localParkedVehicleMotions`，按 StableId 保存被撞车残余运动并同步 ECS `Position`/`Velocity`。新增 `vehicleCollisionRestitution`/`vehicleCoastBraking` 配置，默认 0.20/3.0。

**测试/证据**：新增 `M26-V9 VehicleCollisionImpulse：等质量冲量与质量比` **1/1（19 assertions）** 与 `M26-V9 服务器载具碰撞：冲量继承与无人车滑行` **1/1（23 assertions）**；`M26*` **12/12（261 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **48/48（645 assertions）**、`M25-T4* + M26*` **45/45（843 assertions）**；全量 Debug **769/769（133746 assertions）**、Release **769/769（133760 assertions）**、Python **89/89**；日志 `build/m26_v9_targeted_final.log`、`build/m26_v9_only.log`、`build/m26_v9_vehicle_final.log`、`build/m26_v9_combined_final.log`、`build/m26_v9_debug_final4.log`、`build/m26_v9_release_final6.log`、`build/m26_v9_python_final.log`。

**限制/下一步**：V9 未新增 1280 碰撞烟测，自动化覆盖冲量、目标速度、滑行与服务器无人物车；不做角动量/旋转、分级质量、持续冲量、乘员伤害或轮胎抓地模型，1280 碰撞与自然多人手感仍待补。未提交。
## 2026-11-08 M26-V8 多车远端引擎音

**范围**：监听客户端不再只播放当前绑定车辆的远端引擎循环。按 StableId 为最近最多 4 辆、18 格内、车况大于 0 的远端载具同时维护引擎声部，本地驾驶声部优先；只改客户端音频状态，不改服务器、协议、存档、碰撞、推挤或车况。

**实现**：`VehicleSpike.h` 新增 `VehicleEngineAudioCandidate`/`SelectRemoteVehicleEngineAudio`（远端/车况/距离筛选、同距 StableId 排序、去重、4 声部上限、NaN/非法参数安全）；`WorldDemoState` 用 `m_vehicleEngineVoices` 按 StableId 保存句柄，逐帧按各自速度更新位置/音量/音高；驾驶员退出、车况归零、超出 18 格/4 声部或断线时停止对应声部。复用 `vehicle_engine` 资源，不新增配置/复制字段/协议/存档；网络绑定离开视野时不再顺带停止其他车辆声部。

**测试/证据**：新增 `M26-V8 VehicleEngineAudio：远端多车距离筛选与声部上限` **1/1（12 assertions）**；`M26*` **10/10（218 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **47/47（625 assertions）**、`M25-T4* + M26*` **43/43（800 assertions）**；全量 Debug **767/767（133703 assertions）**、Release **767/767（133717 assertions）**、Python **89/89**；日志 `build/m26_v8_targeted.log`、`build/m26_v8_targeted_release_all.log`、`build/m26_v8_vehicle_only.log`、`build/m26_v8_combined.log`、`build/m26_v8_debug_final.log`、`build/m26_v8_release_final.log`、`build/m26_v8_python_final.log`。三客户端 Release 1280 烟测 `build/m26_v8_multi_engine_smoke/`：A/C 分别驾驶载具 6/8，B null device 监听并同时启动/停止两个远端声部，A/C 分别驾驶 2.67/2.55 格，4 张截图、服务端/三客户端 stderr 为空，结果 `m26-v8-multi-engine-smoke-pass cars=3 listeners=1 remoteVoices=2`。

**限制/下一步**：最多 4 辆远端声部，超出按距离截断；null device 只验证句柄/资源/状态，不做正式混音、遮挡、距离曲线或多车听感验收；V9 已补等质量线性冲量，V10 已补乘员伤害，V11 已补分级质量/角速度；多车自然多人试玩仍待后续。未提交。

## 2026-11-08 M26-V7 每车远端朝向/车灯/破损/烟雾

**范围**：多车辆不再只有当前绑定车拥有完整视觉。服务器按速度更新并复制八向载具朝向，停车时保留最后方向；客户端为所有车辆分别绘制已开启车灯、程序化破损叠加和低车况烟雾。只改复制/表现，不改碰撞、推挤、车况、命令、协议或存档 schema。

**实现**：`ServerVehicleRuntime` 新增 `visualDirection`，`SyncServerVehicleToEcs` 用 `VehicleVisualDirectionFor` 更新并写入 `SpriteRef`；读档时从已有精灵恢复。客户端 `m_vehicleVisualDirections` 按 StableId 缓存方向，本地绑定车继续平滑朝向/滞回，其他车从复制精灵与速度恢复；`DrawVehicleHeadlights` 遍历视野内全部开灯车辆；`m_vehicleDamageVisuals` 让每个载具 DrawItem 叠加自己的划痕/凹陷；`m_vehicleSmokeRuntimes` 按 StableId 独立计时，软粒子烟雾全局上限 96。切换绑定/网络绑定切换时从当前精灵恢复朝向，避免回退南向。

**测试/证据**：新增 `M26-V7 VehicleVisualDirectionVector：朝向单位向量` **1/1（26 assertions）**、`M26-V7 服务器载具朝向：移动后复制并在停车后保持` **1/1（21 assertions）**；`M26*` **9/9（206 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **46/46（613 assertions）**、`M25-T4* + M26*` **42/42（788 assertions）**；全量 Debug **766/766（133691 assertions）**、Release **766/766（133705 assertions）**、Python **89/89**；日志 `build/m26_v7_targeted.log`、`build/m26_v7_targeted_release.log`、`build/m26_v7_targeted_all.log`、`build/m26_v7_vehicle.log`、`build/m26_v7_debug.log`、`build/m26_v7_release.log`、`build/m26_v7_python.log`。Release 1280 重跑 V5 单机多车烟测 `build/m26_v7_v5_visual_regression_smoke/`，A/B 驾驶 4.02/5.02 格、停车漂移 0.00 格、10 张截图、stderr 为空。

**限制/下一步**：V7 未单独构造夜间车灯/低车况破损截图；破损仍是程序化划痕/凹陷占位，车灯光束无墙体遮挡/阴影；多辆远端车仍共用最近一辆的引擎音；未做多车自然多人视觉验收。下一步可补夜间/破损 1280 视觉烟测，或继续完整刚体动量/质量传递。未提交。

## 2026-11-07 M26-V6 单机停放车推挤与共用步进

**范围**：单机驾驶撞击停放车后，沿撞击方向把被撞车推开一次；单机与服务器共用分步推挤逻辑。只做一次性位置推挤与双方车况，不做质量/动量/持续冲量/乘员伤害。

**实现**：`VehicleSpike.h` 新增 `VehiclePushResult` 与 `VehiclePushBody`，按 `VehicleCollisionPushDistance` 结果以 0.05 格步进，每步检查目标格与第三辆车，方向自动归一化，非法/零方向原地返回。`WorldDemoState::PushLocalVehicleOnCollision` 在双方车损结算后更新目标车 `ecs::Position`，记录 `单机载具推挤` 日志；服务器 V4 推挤改为复用同一 helper，保持原行为并补第三辆车同层检查。协议/存档 schema 不变。

**测试/证据**：新增 `M26-V6 VehiclePushBody：分步推挤与阻挡` **1/1（13 assertions）**；`M26*` **7/7（159 assertions）**、`M25-T4*` **33/33（582 assertions）**、载具过滤 **45/45（587 assertions）**、`M25-T4* + M26*` **40/40（741 assertions）**；全量 Debug **764/764（133644 assertions）**、Release **764/764（133658 assertions）**、Python **89/89**；日志 `build/m26_v6_targeted.log`、`build/m26_v6_targeted_all.log`、`build/m26_v6_vehicle.log`、`build/m26_v6_debug.log`、`build/m26_v6_release.log`、`build/m26_v6_python.log`。

**限制/下一步**：V6 未新增 1280 碰撞烟测，自然驾驶路线受地图障碍影响；由纯逻辑测试 + 服务器碰撞集成用例 + V5 单机烟测回归覆盖。Release 1280 重跑 V5 多车烟测 `build/m26_v6_v5_regression_smoke/`，A/B 驾驶 2.59/2.42 格、停车漂移 0.00 格。非活动车仍不持续模拟移动/推挤，不做完整刚体动量/质量传递或碰撞乘员伤害。下一步优先补自然碰撞验收或每车远端车灯/破损表现。未提交。

## 2026-11-07 M26-V1/V2/V3/V4/V5 多车辆、碰撞反馈、推挤与单机多车

**范围**：服务器从单车运行时改为多车向量，接管存档中的全部载具并默认补足 3 辆；每辆车独立座位、燃料、车况、后备箱、冷却和撞击记录。单机本地也按 `vehicle.count` 生成多辆并支持步行换车；V2 已做载具间阻挡与双方车损，V4 补服务器位置推挤，V5 补单机多车生成/就近绑定/停放阻挡；完整刚体动量/质量传递、每车远端车灯与破损叠加暂不做。

**实现**：`ServerVehicleRuntime` 转为 `m_serverVehicles`，新增按 StableId 查找/就近查找/当前会话车辆查找；`EnsureServerVehicles` 读档接管全部 `ecs::Vehicle`，按 `vehicle.count` 在锚点附近找空地补足并保持车辆间距。上车/乘客/维修命令按 `payload.entityId` 路由，其余乘坐命令按 `drivingVehicleId` 路由；`Move` 只写当前车。`VehicleStableId()` 返回最小 StableId，`VehicleStableIds()` 升序。客户端新增 `FindNearestVehicle(range)`，非乘坐时只绑定 `enterRange` 内最近车辆用于上下车/维修/后备箱/HUD；乘坐第二辆/第三辆时仍按 `PlayerState.vehicleStableId` 绑定。ZLR1 仍按车辆实体复制，无新命令/`PlayerState`/存档 schema。

**V2 载具间碰撞**：`VehicleStep`/`VehiclePredictStep` 新增可选动态车身回调；服务器把同层其他载具作为障碍并权威结算双方车况，客户端驾驶预测用复制载具阻挡；相对速度按 `vehicleCollisionConditionDamageBase/PerSpeed/Max` 计算，`vehicleCollisionCooldownSeconds` 限频，服务器广播 `kVehicleRam` 并写双方驾驶员状态；V2 不做推挤/动量传递与乘员伤害，V4 后补位置推挤。

**V3 客户端反馈**：`ApplyNetworkWorldSounds` 处理 `kVehicleRam`，在撞击位置生成火花/金属碎片并记录“网络载具撞击”；`SpawnVehicleHitFeedback` 增加可选震动开关，只有本地驾驶且撞击点距本地车辆 8 格内才震动镜头，网络 `kVehicleHit` 复用同一门控，避免远端车辆碰撞抖动本地镜头。仅表现层，不改变服务器结算、协议或存档。

**V4 位置推挤**：`VehicleCollisionPushDistance` 按相对速度和 `vehicleCollisionPushPerSpeed/Max` 计算；服务器沿撞击方向以 0.05 格步进推开被撞车，检查目标格可走且不与第三辆车重叠，随后同步载具、驾驶员与乘客位置。复用碰撞冷却；不做质量、速度继承或持续冲量，客户端收到位置后校正。

**V5 单机多车**：`EnsureVehicleSpawned` 统计全部 `Position + Vehicle` 并按 `vehicle.count` 用 6/10/14 格候选偏移补足，`SpawnLocalVehicle` 统一创建车辆组件；`BindNearestLocalVehicle` 在非驾驶且背包关闭时绑定 `enterRange` 内最近车辆，切换前同步旧车状态。单机驾驶把其他车辆作为 `VehicleBodyBlocked` 障碍，完全阻挡时按 V2 参数对双方扣车况并使用同一冷却；V5 不推挤停放车辆，不做质量/动量/乘员伤害。

**测试/证据**：新增 `M26 多载具：两车独立驾驶与命令目标` **1/1（40 assertions）**、`M26-V2 VehicleStep：其他载具动态阻挡与相对冲击` **1/1（16 assertions）**、`M26-V2 服务器载具间碰撞：阻挡、双方车损与撞击音` **1/1（36 assertions）**、`M26-V4 VehicleCollisionPush：相对速度推挤距离` **1/1（7 assertions）**、`M26-V5 单机多车碰撞/存档` **2/2（47 assertions）**；`M26*` **6/6（146 assertions）**；`M25-T4*` **33/33（582 assertions）**、载具过滤 **44/44（574 assertions）**、`M25-T4* + M26*` **39/39（728 assertions）**；全量 Debug **763/763（133631 assertions）**、Release **763/763（133645 assertions）**、Python **89/89**；日志 `build/m26_v5_targeted.log`、`build/m26_v5_targeted_all.log`、`build/m26_v5_vehicle.log`、`build/m26_v5_debug.log`、`build/m26_v5_release.log`、`build/m26_v5_python.log`。Release 1280 双客户端烟测 `build/m26_v1_multi_vehicle_smoke/`：服务器生成 3 辆车（StableId 6/7/8），A 进入 6、B 进入 8；A/B 分别真实按键驾驶，A 首驶 4.03 格、B 驶 2.67 格、A 二次驶 2.55 格；A 驾驶时 B 位移 0、B 驾驶时 A 位移 0；两次下车成功，三份 stderr 为空；14 张截图与 `SUMMARY.md`，结果 `m26-v1-multi-vehicle-smoke-pass cars=3 drivers=2`。单机 V5 烟测 `build/m26_v5_single_player_multi_vehicle_smoke/`：新世界生成 3 辆，真实 WASD/Enter 分别驾驶 A/B，A 驶 3.15 格、B 驶 4.78 格，走回 A 漂移 0.00 格，10 张截图，结果 `m26-v5-single-player-multi-vehicle-smoke-pass`。

**限制/下一步**：单机已支持多车生成/换车，但同一时刻只保留一个活动驾驶运行时，非活动车不模拟移动/推挤；非当前绑定车辆只有基础复制精灵，无远端车灯/破损叠加；客户端只保留一个引擎音循环；V2/V4 不做完整刚体动量/质量传递或乘员伤害，慢速接触不扣车况；V3/V4 未新增 1280 碰撞烟测；1280 构造/单机烟测已通过，未做多车自然多人试玩验收。下一步优先做碰撞场景实机验收，再决定完整刚体动量或每车表现扩展。未提交。

## 2026-11-07 M25-T4 V27 双乘客位

**范围**：联机车辆从“驾驶员 + 单乘客”扩展为“驾驶员 + 最多 2 名乘客”；复用既有 `EnterVehiclePassenger=45` 自动占第一个空位，不新增命令、`PlayerState` 字段或存档字段。不做后排独立座位/偏移、座位持久化、多车辆、乘客攻击或正式多人平衡。

**实现**：`VehicleSpike.h` 增加 `kVehicleMaxPassengers=2`，`VehiclePassengerSeatAvailable`/`VehicleSeatForEnter`/`VehicleCanTakeDriverSeat` 接受第二乘客，新增 `VehicleCanGiveDriverSeatWithSecondPassenger`。服务器 `ServerVehicleRuntime` 增加 `secondPassengerStableId`，上下车/接管/让座均支持两名乘客，乘客下车后压缩到第一空位；`SyncServerVehiclePassengers` 统一校验/同步两名乘客。ZLR1 `VHCL` 从 25B 扩为 33B，追加第二乘客 StableId；`EntityReplication` 应用字段、客户端隐藏两名乘客，车外 HUD 只在两名乘客都满时显示“载具已满”。`PlayerState` v18 与存档 schema 不变，旧 25B 客户端不兼容。

**测试/证据**：新增 `M25-T4-V27 VehicleSeat：双乘客位进入选择` **1/1（11 assertions）**、`M25-T4-V27 三客户端载具双乘客位：占用、接管与让座` **1/1（60 assertions）**，V27 合计 **2/2（71 assertions）**；`M25-T4*` **33/33（582 assertions）**，载具过滤 **42/42（551 assertions）**；全量 Debug **757/757（133485 assertions）**、Release **757/757（133498 assertions）**、Python **89/89**；日志 `build/m25_t4_v27_targeted.log`、`build/m25_t4_v27_all.log`、`build/m25_t4_v27_vehicle.log`、`build/m25_t4_v27_v26_regression.log`、`build/m25_t4_v27_replication.log`、`build/m25_t4_v27_debug.log`、`build/m25_t4_v27_release.log`、`build/m25_t4_v27_python.log`。Release 1280 三客户端烟测 `build/m25_t4_v27_double_passenger_smoke/`：A 驾驶、B/C 上乘客位，C 下车重上，A 下车后 C 接管、再让座给 B；服务器日志含 3 次 `服务器上乘客位`、2 个不同乘客 StableId、`服务器乘客下车`/`服务器下车`/`服务器接管驾驶位`/`服务器让座`；20 张 1280 截图，四份 stderr 为空；结果 `m25-t4-v27-double-passenger-smoke-pass passengers=2`。

**限制/下一步**：只支持 2 名乘客，没有后排座位号 UI、每座位独立位置/朝向/碰撞、座位持久化、多车辆或乘客战斗；仍只允许乘客下车/快捷栏/背包/快捷使用/接管/让座；ZLR1 版本已扩展，旧客户端不能加入；构造三客户端烟测不替代自然多人试玩；正式音效仍待后续。未提交。

## 2026-11-07 M25-T4 V26 乘客快捷使用与装备

**范围**：联机乘客在车内无需打开背包即可按 `E` 使用背包中第一件可用消耗品、按 `R` 切换主手武器、按 `1–5` 选择快捷栏；驾驶员驾驶中仍不能打开背包或使用快捷栏。不新增命令、复制字段、`PlayerState`/ZLR1/存档 schema，不做攻击/换弹/建造、车内专属 UI、多乘客或物品所有权。

**实现**：乘客输入分支在背包关闭时处理 `1–5`/`E`/`R`，分别发送既有 `SelectHotbar`/`UseBag`/`EquipBag`；背包打开时仍由背包 UI 处理，避免与 F 接管/Enter 下车冲突。乘客 HUD 增加 `E 使用 · R 装备 · 1-5 快捷栏`。服务器乘客白名单在 V25 基础上放行 `UseBag`/`EquipBag`，仍拒绝驾驶输入、攻击、建造、交易等动作；驾驶员驾驶中继续被拒绝，`InventoryAction` 边界不变。

**测试/证据**：扩展双客户端用例为 `M25-T4-V22/V23/V24/V25/V26`，新增驾驶员 `UseBag` 被拒、乘客 `UseBag` 消耗罐头豆子（饥饿 1.00→0.65、状态含“使用”）、乘客 `EquipBag` 装备 `kitchen_knife`，现 **1/1（89 assertions）**；`M25-T4*` **31/31（508 assertions）**，载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133406 assertions）**、Release **755/755（133420 assertions）**、Python **89/89**；日志 `build/m25_t4_v26_targeted.log`、`build/m25_t4_v26_all.log`、`build/m25_t4_v26_all_release.log`、`build/m25_t4_v26_vehicle.log`、`build/m25_t4_v26_command.log`、`build/m25_t4_v26_debug.log`、`build/m25_t4_v26_debug_rerun.log`、`build/m25_t4_v26_release.log`、`build/m25_t4_v26_python.log`。Release 1280 双客户端烟测 `build/m25_t4_v26_quick_use_smoke/`：B 首连接后拾取出生点旁绷带（`stacks=1 items=2`）；A 驾驶、B 上乘客位，B 按 `E` 后探针 `items=2→1`，服务器日志 `服务器上乘客位`/`服务器乘客下车`，10 张截图，三份 stderr 为空；结果 `m25-t4-v26-quick-use-smoke-pass quickuse=1`。

**限制/下一步**：只支持单乘客；驾驶员仍不能打开背包或使用快捷栏；烟测使用演示地面绷带只验证 `E` 快捷使用，`R` 装备由双客户端集成测试覆盖；自动化数字键注入未稳定触发 `1–5` 选择，快捷栏选择未单独截图；构造烟测不替代自然多人试玩。未提交。

## 2026-11-07 M25-T4 V25 乘客车内背包与后备箱

**范围**：联机乘客在车内按 `I` 打开背包/车辆后备箱，通过既有服务器权威 `InventoryAction` 存取、整理或使用物品；驾驶员驾驶中不能打开背包的边界不变。不新增命令、复制字段、`PlayerState`/ZLR1/存档 schema，不做多乘客、物品所有权或座位持久化。

**实现**：客户端乘客输入分支不再强制关闭背包，打开后 `FindNearbyStorage` 将同位置车辆识别为后备箱，乘客 HUD 增加 `I 背包/后备箱`；背包打开时乘客输入不再触发 F 接管/Enter，关闭后恢复原逻辑。服务器 `ExecuteSessionCommand` 放行乘客 `InventoryAction`，`PlayerInventoryService` 继续做距离、容量、堆数与所有权校验；驾驶员驾驶中仍被拒绝。

**测试/证据**：扩展双客户端用例为 `M25-T4-V22/V23/V24/V25`，新增驾驶员取后备箱被拒、乘客 `TakeAll` 将 2 个绷带从车辆后备箱取入背包、状态断言，现 **1/1（80 assertions）**；`M25-T4*` **31/31（499 assertions）**；载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133397 assertions）**、Release **755/755（133411 assertions）**、Python **89/89**；日志 `build/m25_t4_v25_targeted.log`、`build/m25_t4_v25_all.log`、`build/m25_t4_v25_all_release.log`、`build/m25_t4_v25_vehicle.log`、`build/m25_t4_v25_debug.log`、`build/m25_t4_v25_release.log`、`build/m25_t4_v25_python.log`、`build/m25_t4_v25_net.log`。Release 1280 双客户端烟测 `build/m25_t4_v25_passenger_inventory_smoke/`：A 驾驶、B 乘客；B 按 I，探针记录 `inv=1 title=后备箱：皮卡（0/40 kg）（空）`，关闭后 Enter 下车；服务器日志 `服务器上乘客位`/`服务器乘客下车`，11 张截图，三份 stderr 为空；结果 `m25-t4-v25-passenger-inventory-smoke-pass title=后备箱`。

**限制/下一步**：只支持单乘客；驾驶员仍不能在驾驶中打开背包；烟测只验证打开/关闭后备箱，物品存取由集成测试覆盖，未做真实鼠标拖拽、多乘客、物品所有权或移动中后备箱自然体验；构造烟测不替代自然多人试玩。未提交。

## 2026-11-07 M25-T4 V24 驾驶员停车让座

**范围**：联机车辆停稳后，驾驶员可把驾驶位直接交给当前乘客，原驾驶员变为乘客；不做驾驶中换座、多乘客、让座给车外玩家或座位持久化。

**实现**：`VehicleSpike.h` 新增 `VehicleCanGiveDriverSeat(driver, passenger, requester, speed, maxSpeed)` 与默认上限 `kVehicleSeatHandoffMaxSpeed=0.10`、配置 `vehicle.seatHandoffMaxSpeed`；新增 `CommandType::GiveDriverSeat=47`。服务器仅驾驶位可发，校验乘客存在且车辆停稳；成功后交换 `driverStableId`/`passengerStableId`，清空车辆输入/速度，`SyncServerVehicleToEcs` 后同步新驾驶员与旧驾驶员，双方 status/stateDirty 更新。`PlayerState`/ZLR1/存档 schema 不变。客户端驾驶 HUD 在停车且存在乘客时显示 `F 让座`，按 F 发送命令；乘客端沿用 V23 的接管/下车逻辑。

**测试/证据**：新增 `M25-T4-V24 VehicleDriverHandoff` **1/1（11 assertions）**；扩展双客户端用例为 `M25-T4-V22/V23/V24`，新增乘客回座、让座、状态/复制、旧驾驶员越权与下车断言，现 **1/1（74 assertions）**；`M25-T4*` **31/31（493 assertions）**；载具过滤 **41/41（537 assertions）**；全量 Debug **755/755（133391 assertions）**、Release **755/755（133405 assertions）**、Python **89/89**；日志 `build/m25_t4_v24_targeted.log`、`build/m25_t4_v24_v22v23v24.log`、`build/m25_t4_v24_all.log`、`build/m25_t4_v24_vehicle_all.log`、`build/m25_t4_v24_debug.log`、`build/m25_t4_v24_release.log`、`build/m25_t4_v24_python.log`、`build/m25_t4_v24_net.log`。Release 1280 双客户端烟测 `build/m25_t4_v24_seat_handoff_smoke/`：A 驾驶、B 乘客；A 停车按 F 让座，B 成为驾驶员并驾驶 2.42 格，A 作为乘客跟随 1.10 格；服务器日志 `服务器让座`，14 张截图，三份 stderr 为空。

**限制/下一步**：只支持停稳后的驾驶位让座；不支持驾驶中换座、多乘客、让座给车外玩家、座位持久化、乘客受击/死亡/经验/任务或安全区；燃料/车况/车灯/后备箱不变；构造烟测不替代自然多人试玩。未提交。

## 2026-11-06 M25-T4 V23 乘客接管驾驶位

**范围**：驾驶员下车后，原联机乘客可在空驾驶位直接接管，不必先下车绕行；接管后沿用既有驾驶输入、预测、HUD、引擎音与车辆状态。不做驾驶中换座、多乘客、乘客专属权限或座位持久化。

**实现**：`VehicleSpike.h` 新增 `VehicleCanTakeDriverSeat(driver, passenger, requester)`；新增 `CommandType::TakeDriverSeat=46`。服务器只允许乘客位会话请求，校验驾驶位为空且请求者就是当前乘客；成功后清空 `passengerStableId`、把请求者写入 `driverStableId`，清空车辆输入/速度，调用 `SyncServerVehicleToEcs`/`SyncVehicleDriver` 并回 status；`PlayerState`/ZLR1/存档 schema 不变。客户端乘客 HUD 在空位时显示 `F 接管驾驶`，输入分支发送命令；接管后服务器回传 `driving=true`，复用既有驾驶路径。

**测试/证据**：新增 `M25-T4-V23 VehicleDriverSeat` **1/1（6 assertions）**；扩展 V22 双客户端用例为 `M25-T4-V22/V23`，新增接管、乘客位清空、复制与再次驾驶断言，现 **3/3（78 assertions）**；`M25-T4*` **30/30（468 assertions）**；载具过滤 **40/40（526 assertions）**；全量 Debug **754/754（133338 assertions）**、Release **754/754（133352 assertions）**、Python **89/89**；日志 `build/m25_t4_v23_targeted.log`、`build/m25_t4_v23_v22.log`、`build/m25_t4_v23_all.log`、`build/m25_t4_v23_vehicle_all.log`、`build/m25_t4_v23_debug.log`、`build/m25_t4_v23_release.log`、`build/m25_t4_v23_python.log`。Release 1280 双客户端烟测 `build/m25_t4_v23_seat_switch_smoke/`：A 驾驶、B 乘客；A 下车后 B 按 F 接管，乘客跟随 1.19 格、接管后驾驶 2.42 格；服务器日志 `服务器接管驾驶位`，14 张截图，三份 stderr 为空。

**限制/下一步**：只支持驾驶员下车后的空位接管；不支持驾驶中换座、驾驶员主动让座、多乘客、座位持久化、乘客受击/死亡/经验/任务或安全区；车辆损坏/无燃料仍沿用既有驾驶限制；构造烟测不替代自然多人试玩。未提交。

## 2026-11-06 M25-T4 V22 联机载具乘客位

**范围**：在单车/单驾驶位上增加一个联机乘客位；驾驶员与乘客都随服务器权威车辆移动，乘客不能驾驶/鸣笛/开关车灯；不做多乘客、乘客专属伤害/经验/任务、乘客引擎音或正式 PvP 平衡。

**实现**：`VehicleSpike.h` 新增 `VehicleSeat`/`VehicleSeatForEnter`/`VehiclePassengerSeatAvailable`；`EnterVehiclePassenger=45` 显式请求乘客位，`EnterVehicle=40` 保持驾驶位语义；`PlayerState` v18 增加与 `driving` 互斥的 `passenger`，ZLR1 `VHCL` 25B 追加 passenger id；服务器用 `passengerStableId`/`ecs::VehiclePassenger` 维护单乘客，`SyncVehiclePassenger`/`ReleaseServerVehicleSeat` 处理上下车、跟随、死亡/断线/接管；乘客命令白名单只允许下车/快捷栏；客户端 `m_riding`、自动乘客命令、乘客 HUD 与隐藏精灵恢复；PvP 撞击和车辆攻击继续排除 `VehicleOccupant`。

**测试/证据**：新增 `M25-T4-V22 VehicleSeat` **1/1（7 assertions）**、`M25-T4-V22 玩家状态协议` **1/1（9 assertions）**、`M25-T4-V22 双客户端载具乘客位` **1/1（53 assertions）**，V22 目标 **3/3（69 assertions）**；`M25-T4*` **29/29（453 assertions）**；载具过滤 **39/39（520 assertions）**；全量 Debug **753/753（133309 assertions）**、Release **753/753（133323 assertions）**、Python **89/89**；日志 `build/m25_t4_v22_targeted.log`、`build/m25_t4_v22_all.log`、`build/m25_t4_v22_vehicle_all.log`、`build/m25_t4_v22_debug.log`、`build/m25_t4_v22_release.log`、`build/m25_t4_v22_python.log`。Release 1280 双客户端烟测 `build/m25_t4_v22_passenger_smoke/`：A 驾驶、B 自动进入乘客位，B 跟随 1.19 格后下车；服务器日志 `服务器上乘客位`/`服务器乘客下车`，9 张截图，三份 stderr 为空。

**限制/下一步**：只支持一个驾驶员 + 一个乘客，不支持多乘客/换座/乘客驾驶、乘客受击/死亡/经验/任务、乘客引擎音或安全区；ZLR1/PlayerState 版本已提升，旧客户端不能加入；烟测为构造双客户端场景，不替代自然多人试玩。未提交。

## 2026-11-06 M25-T4 V21 联机 PvP 载具撞玩家

**范围**：仅 `combat.pvp=true` 的联机服务器，载具撞击驾驶员以外的存活车外玩家，服务器权威结算 Blunt 伤害/击退/硬直；不做单机玩家、乘客/多座位、友方 NPC、安全区、致死/击杀归属或正式 PvP 平衡。

**实现**：`VehicleRamTargetKind` 新增 `Player`，`VehicleRamTargetKindFor` 增加 `allowPlayer`（默认 `false`）；服务器 `ApplyServerVehicleRam` 遍历 `PlayerTag`，排除驾驶员、`VehicleOccupant` 与 `NoCombatTarget`，由 `m_pvpEnabled` 控制玩家分支，命中后复用现有载具撞击伤害/击退/硬直，伤害来源写驾驶员 StableId，日志为 `服务器载具撞玩家`/`服务器载具撞杀玩家`；不新增命令、复制字段、`PlayerState`、协议或存档 schema，PvP 关闭时不伤害玩家。

**测试/证据**：新增 `M25-T4-V21 VehicleRamTargetKind：PvP 玩家筛选` **1/1（10 assertions）**；新增双客户端服务器用例 `服务器载具撞击玩家` 与 `服务器载具撞击玩家受 PvP 开关限制` **2/2（49 assertions）**；`M25-T4*` **26/26（384 assertions）**；载具过滤 **38/38（513 assertions）**；全量 Debug **750/750（133240 assertions）**、Release **750/750（133254 assertions）**、Python **89/89**；日志 `build/m25_t4_v21_targeted.log`、`build/m25_t4_v21_all.log`、`build/m25_t4_v21_vehicle_all.log`、`build/m25_t4_v21_debug.log`、`build/m25_t4_v21_release.log`、`build/m25_t4_v21_python.log`。Release 1280 双客户端烟测 `build/m25_t4_v21_pvp_smoke/`：A 上车撞击 B，服务器日志 `服务器载具撞玩家`（伤害 8、速度 5.3），B 生命 100→92、车况 100%→99%；6 张截图 `v21_a_ready.png`、`v21_b_target.png`、`v21_a_driving.png`、`v21_b_before_hit.png`、`v21_a_after_hit.png`、`v21_b_after_hit.png`；三份 stderr 为空。

**限制/下一步**：只覆盖一个驾驶员/一个车外玩家，不覆盖乘客、致死/击杀归属、安全区或双客户端震屏；PvP 关闭时无玩家伤害；烟测为构造双客户端场景，不替代自然 PvP 试玩。未提交。

## 2026-11-06 M25-T4 V20 危险动物反击载具

**范围**：危险动物（`AnimalKind::Dangerous`）成功攻击驾驶位时，保留对驾驶员的原伤害，同时磨损车况并复用 V16/V18 反馈；单机与服务器权威同规则。不做被动/友方动物、玩家/乘客被撞、动物主动把车当目标、车辆爆炸/报废或物种专属伤害/经验/任务。

**实现**：`VehicleSpike.h` 新增 `VehicleAttackerKind` 与 `VehicleAttackerKindFor`；动物 AI 直接调用 `CombatSystem::ApplyDamage`，不进入玩家近战的 `CombatSystem::HitInfo`，所以 `AnimalSystem` 新增 `AttackInfo`/`GetAttacks()` 记录本 tick 成功攻击。`ApplyLocalVehicleZombieAttacks`/`ApplyServerVehicleZombieAttacks` 更名为 `ApplyLocalVehicleAttacks`/`ApplyServerVehicleAttacks`：先读动物攻击记录，再读 Combat 命中；目标是驾驶位且来源为危险动物/僵尸时扣 1.5% 车况，0.75s 全车冷却，复用 V16 火花/音效与 V18 镜头震动/金属碎片。动物分支日志为 `载具受动物攻击`/`服务器载具受动物攻击`；不新增命令、复制字段、`PlayerState`、协议或存档 schema，不改变动物对驾驶员伤害或 V9 僵尸爪/咬减伤。

**测试/证据**：新增 `M25-T4-V20 VehicleAttackerKind：僵尸/危险动物攻击来源筛选` **1/1（9 assertions）**；扩展服务器集成用例构造 `boar`，断言服务器扣血、车况下降与客户端 `vehicle_hit`（新增 5 assertions）；`M25-T4*` **23/23（325 assertions）**；载具过滤 **37/37（503 assertions）**；全量 Debug **747/747（133181 assertions）**、Release **747/747（133195 assertions）**、Python **89/89**；日志 `build/m25_t4_v20_targeted.log`、`build/m25_t4_v20_all.log`、`build/m25_t4_v20_vehicle_all.log`、`build/m25_t4_v20_debug.log`、`build/m25_t4_v20_release.log`、`build/m25_t4_v20_python.log`。Release 1280 烟测 `build/m25_t4_v20_animal_attack_smoke/`：4 只 `smoke_boar`（200 HP、危险、1 伤害、1.0s 攻击冷却），玩家 100000 HP，真实 Enter 上车后动物靠近攻击；5 次 `载具受动物攻击`，车况 100%→93%，5 次火花/音效并出现镜头震动/金属碎片；4 张截图 `v20_ready.png`、`v20_driving.png`、`v20_animal_attack.png`、`v20_vehicle_damaged.png`；`--audio.device=null`，`game.err.log` 为空。

**限制/下一步**：只支持危险动物攻击驾驶位，不覆盖被动/友方动物、乘客/车外玩家；动物攻击仍按原 `Blunt` 伤害结算给驾驶员，V9 仅对僵尸爪/咬减伤；车况磨损复用固定 1.5%/0.75s，不按物种质量或攻击伤害缩放；只有 `AnimalSystem` 攻击记录会触发；服务器测试仅单客户端，未做双客户端震屏/火花截图；烟测为自定义物种/传送起点，不替代自然动物围攻/平衡试玩。未提交。

## 2026-11-06 M25-T4 V19 载具撞击动物

**范围**：载具驾驶中撞击 `Animal` 实体，复用现有速度阈值、Blunt 伤害、击退、硬直与车况磨损；单机与服务器权威同规则，动物死亡继续走 `DeathSystem -> AnimalCorpse` 与既有剥取；不做玩家碾压、动物专属伤害/经验/任务归属、分部件撞击、血液/尸体物理或正式音效。

**实现**：`VehicleSpike.h` 新增 `VehicleRamTargetKind` 与 `VehicleRamTargetKindFor`，统一忽略玩家/友方/死亡/异层目标，同一实体同时带 `Animal` 与 `AiState` 时优先按动物处理。`ApplyLocalVehicleRam` 在既有 `AiState` 扫描后增加 `Animal + Position + Health` 扫描；`ApplyServerVehicleRam` 使用同一分类和权威扣血。动物受击后通过既有 `HitReaction` 逃跑/反击；`Vehicle` 车况磨损、`vehicle_ram` 音效、V16 火花与 V18 镜头震动/金属碎片复用。动物击杀不发 `Melee` 经验；不新增命令、复制字段、`PlayerState`、协议或存档 schema。

**测试/证据**：新增 `M25-T4-V19 VehicleRamTargetKind：僵尸/动物目标筛选` **1/1（9 assertions）**；扩展服务器载具集成用例，构造 `deer` 并断言服务器扣血、客户端 `Animal`/`Health` 复制（新增 7 assertions）；`M25-T4*` **22/22（311 assertions）**；载具过滤 **36/36（494 assertions）**；全量 Debug **746/746（133167 assertions）**、Release **746/746（133181 assertions）**、Python **89/89**；日志 `build/m25_t4_v19_targeted.log`、`build/m25_t4_v19_all.log`、`build/m25_t4_v19_vehicle_all.log`、`build/m25_t4_v19_debug.log`、`build/m25_t4_v19_release.log`、`build/m25_t4_v19_python.log`。Release 1280 烟测 `build/m25_t4_v19_animal_smoke/`：临时 `animals.json` 加载 24 只低感知 `smoke_deer`，车辆生成于 (69,7)，真实 Enter 上车后 W+D 行驶约 3.6s，命中/撞杀各 2 只；日志 2 次 `载具撞动物`/2 次 `载具撞杀动物`，车况 100%→95%→90%，含金属碎片/镜头震动/受击火花；5 张截图 `v19_ready.png`、`v19_driving.png`、`v19_animal_hit.png`、`v19_animal_corpse.png`、`v19_animal_after.png`；`--audio.device=null`，`game.err.log` 为空。

**限制/下一步**：不支持玩家/乘客被撞；动物与僵尸共用整车单值伤害/磨损，没有按物种质量、撞击部位或速度阈值分级；动物击杀不发经验、不单独归属任务；烟测使用低感知自定义物种与传送起点，未做自然动物分布、双客户端动物尸体/剥取截图或正式血液/尸体物理；玩家碾压、正式音效和更完整自然试玩待后续。未提交。

## 2026-11-06 M25-T4 V18 载具受击镜头反馈与金属碎片

**范围**：僵尸攻击扣车况、载具撞僵尸扣车况、撞墙造成车况损失时，本地驾驶客户端叠加短促世界视图震动并生成金属碎片。只改表现层，不改变伤害、车况、动力、碰撞、协议或存档；不做真实凹坑贴图、裂纹玻璃、爆炸/报废演出或正式音效。

**实现**：`VehicleSpike.h` 新增 `VehicleScreenShake`、`VehicleHitShakeStrength`、`VehicleCrashShakeStrength`、`VehicleShakeDuration`、`VehicleScreenShakeOffset`、`VehicleMetalDebrisCount`、`VehicleMetalDebrisLifetime`；受击沿用 5% 磨损满强度，撞墙按 3~10 格/秒映射 0.25~1.0，震动 0.18~0.36s/最大 5px、碎片 2~6 片/0.45~0.90s。`WorldDemoState::SpawnVehicleHitFeedback` 在既有火花/音效路径上生成碎片，并在驾驶中触发震动；`ApplyLocalVehicleRam` 的车况磨损与 `UpdateVehicle` 的碰撞受损路径复用；`DrawWorld` 只偏移世界视图矩阵，HUD/UI 不抖；碎片在车辆火花后绘制并按寿命上浮淡出。联机服务器仍只复制车况，客户端收到既有 `vehicle_hit` `WorldSound` 时生成碎片，仅本地驾驶震屏；`PlayerState`/ZLR1/协议/存档不变。

**测试/证据**：新增 `M25-T4-V18 VehicleImpactFeedback：镜头震动与金属碎片` **1/1（28 assertions）**；`M25-T4*` **21/21（295 assertions）**；载具过滤 **35/35（485 assertions）**；全量 Debug **745/745（133151 assertions）**、Release **745/745（133165 assertions）**、Python **89/89**；日志 `build/m25_t4_v18_targeted.log`、`build/m25_t4_v18_all.log`、`build/m25_t4_v18_vehicle_all.log`、`build/m25_t4_v18_debug.log`、`build/m25_t4_v18_release.log`、`build/m25_t4_v18_python.log`。Release 1280 烟测 `build/m25_t4_v18_vehicle_impact_smoke/`：真实 Enter 上车，探针生成 1 个僵尸，临时每击 25% 车况磨损/0.5s 冷却，4 次攻击使 100%→75%→50%→25%→0%；日志 4 次 `载具金属碎片：强度 1.00，pieces=6，寿命 0.90s` 与 4 次 `载具镜头震动：强度 1.00，时长 0.36s`，保留 `marks=3/6/10` 与 0% 报废烟雾；7 张截图 `v18_vehicle_pristine.png`、`v18_vehicle_attack_start.png`、`v18_impact_debris.png`、`v18_vehicle_scuffed.png`、`v18_vehicle_dented.png`、`v18_vehicle_wrecked.png`、`v18_vehicle_zero.png`；`--audio.device=null`，`game.err.log` 为空。

**限制/下一步**：震动是短暂世界视图偏移，不改变相机中心/鼠标拾取/调试叠层，截图无法完整表达体感；碎片是屏幕空间矩形占位，不随相机旋转/参与光照，不区分攻击部位与八向车身，也没有玻璃裂纹、凹坑贴图、爆炸或正式音效；撞墙反馈复用受击火花/撞击音，未单独做撞墙烟测；联机未单独做双客户端震屏截图；构造烟测，不替代自然尸群围攻平衡。未提交。

## 2026-11-06 M25-T4 V17 载具破损外观

**范围**：按车况 75%/50%/25% 三档在载具精灵上叠加划痕/凹陷/破损表现，让玩家不看 HUD 也能感知车辆状态。只改表现层，不改变耐久/动力/碰撞/协议/存档；不做真实贴图、分部件变形、爆炸/报废演出、镜头震动或正式音效。

**实现**：`VehicleSpike.h` 新增 `VehicleDamageVisualLevel`（None/Scuffed/Dented/Wrecked）、`VehicleDamageVisualLevelFor`（>75% 无、50~75% 划痕、25~50% 凹陷、≤25% 破损）、`VehicleDamageVisualMarkCount`（0/3/6/10）与标签；`WorldDemoState::UpdateVehicleDamageVisual` 在跨档时写 `载具破损外观：车况 X%，等级 ...（marks=N）`，`DrawVehicleDamageVisual` 找到载具 DrawItem 后在车辆精灵之后绘制深色描边+橙色划痕、深色软粒子凹陷与锈色芯。绘制按车辆排序位置插入，不覆盖后方物体；本地/网络客户端共用已复制车况，不写存档、不复制、不新增命令。

**测试/证据**：新增 `M25-T4-V17 VehicleDamageVisual：车况分级与痕迹数量` **1/1（16 assertions）**；`M25-T4*` **20/20（267 assertions）**；载具过滤 **34/34（457 assertions）**；全量 Debug **744/744（133123 assertions）**、Release **744/744（133137 assertions）**、Python **89/89**；日志 `build/m25_t4_v17_targeted.log`、`build/m25_t4_v17_all.log`、`build/m25_t4_v17_vehicle_all.log`、`build/m25_t4_v17_debug.log`、`build/m25_t4_v17_release.log`、`build/m25_t4_v17_python.log`。Release 1280 烟测 `build/m25_t4_v17_vehicle_damage_smoke/`：真实 Enter 上车，探针生成 1 个僵尸，临时每击 25% 车况磨损/0.5s 冷却，4 次攻击使 100%→75%→50%→25%→0%；日志确认 75% 划痕（marks=3）、50% 凹陷（marks=6）、25% 破损（marks=10）与 0% 报废烟雾；6 张截图 `v17_vehicle_pristine.png`、`v17_vehicle_attack_start.png`、`v17_vehicle_scuffed.png`、`v17_vehicle_dented.png`、`v17_vehicle_wrecked.png`、`v17_vehicle_zero.png`；`--audio.device=null`，`game.err.log` 为空；构造烟测，不替代自然尸群围攻平衡。

**限制/下一步**：划痕/凹陷是屏幕空间绘制，不随相机旋转/参与光照，不区分攻击部位与八向车身；25%/0% 共用“破损”档，0% 主要靠 V11/V13 报废烟雾区分；没有真实凹坑贴图、裂纹玻璃、金属碎片、爆炸或正式音效；联机未单独做双客户端破损截图；构造烟测，不替代自然尸群试玩。未提交。

## 2026-11-06 M25-T4 V16 载具受击音效与火花

**范围**：V15 僵尸攻击造成车况损耗时补短促受击音效与橙黄火花；单机直接播放，联机由服务器广播 `WorldSound` 给客户端统一播放/显示。不改变伤害、车况、驾驶防护、协议或存档；不做破损外观、爆炸/报废演出、镜头震动、正式音频或自然尸群调参。

**实现**：`VehicleSpike.h` 新增 `VehicleHitFeedbackStrength`（以 5% 磨损为满强度）、`VehicleHitSparkCount`（1.5% 默认 5 个、5% 8 个）与 `VehicleHitSparkLifetime`（0.50~0.90s）；`WorldDemoState` 新增 `VehicleHitSpark` 生命周期/绘制，1.5% 磨损寿命约 0.62s。单机 `ApplyLocalVehicleZombieAttacks` 扣车况后生成朝外扩散火花并用 `AudioEngine::Play3D` 播放 `vehicle_hit`，HUD 保留 `载具受击：车况 X%`；服务器 `ApplyServerVehicleZombieAttacks` 在权威扣车况后广播 `WorldSound`（`Impact`、`vehicle_hit`、半径 18），客户端 `ApplyNetworkWorldSounds` 播放音频并在事件位置生成火花。新增 `assets/audio/vehicle_hit.wav`（0.26s、22050Hz、单声道、金属短击占位）与 `audio::kVehicleHit`；不新增协议字段、存档字段或 `PlayerState` 字段。

**测试/证据**：新增 `M25-T4-V16 VehicleHitFeedback：磨损强度/火花数量与寿命` **1/1（15 assertions）**；`M25-T4*` **19/19（251 assertions）**；载具过滤 **33/33（441 assertions）**；服务器 V15 集成用例追加“客户端收到 `vehicle_hit` WorldSound”断言；`test_audio` null-device 加载并播放新 clip；全量 Debug **743/743（133107 assertions）**、Release **743/743（133121 assertions）**、Python **89/89**；日志 `build/m25_t4_v16_targeted.log`、`build/m25_t4_v16_all.log`、`build/m25_t4_v16_vehicle_all.log`、`build/m25_t4_v16_debug.log`、`build/m25_t4_v16_release.log`、`build/m25_t4_v16_python.log`。Release 1280 烟测 `build/m25_t4_v16_vehicle_hit_smoke/`：真实 Enter 上车，探针生成 1 个僵尸，5 次攻击车况 95%→90%→85%→80%→75%；日志 5 次 `载具受击火花`（sparks=8，寿命 0.90s）与 5 次 `载具受击音效：vehicle_hit`；第二击截图可见橙黄火花与 HUD `车况 90%`；4 张 `v16_near_vehicle.png`、`v16_zombie_attack.png`、`v16_vehicle_hit.png`、`v16_condition_damaged.png`。`--audio.device=null`，玩家 100000 HP、地图 28 个既有僵尸保留、关闭 spawner 补充；构造烟测，不替代自然尸群试玩。

**限制/下一步**：火花为屏幕空间软粒子占位，位置在车辆中心附近，不区分攻击部位；没有镜头震动、车身凹陷/破损贴图、金属碎片、正式混音/音量面板；联机只验证单客户端 WorldSound，未做双客户端受击截图；构造烟测不代表自然尸群围攻平衡。未提交。

## 2026-11-06 M25-T4 V15 僵尸攻击载具与车况损耗

**范围**：僵尸对驾驶位的成功爪/咬命中同时磨损车况；保留 V9 的 85% 驾驶位减伤。只改载具受击结算，不新增协议/存档/PlayerState；不做僵尸主动把车辆当目标、车辆被摧毁/爆炸、乘客或动物攻击载具、正式火花/音效。

**实现**：`VehicleSpike.h` 新增 `VehicleZombieAttackConditionDamage` / `VehicleApplyZombieAttackConditionDamage`、`kVehicleZombieAttackConditionDamage=0.015`、`kVehicleZombieAttackCooldownSeconds=0.75`，`VehicleTuning` 与客户端/服务器配置解析新增 `zombieAttackConditionDamage/zombieAttackCooldownSeconds`。单机 `WorldDemoState::ApplyLocalVehicleZombieAttacks` 在 `CombatSystem` 本 tick 命中记录中找“目标是驾驶中的本地玩家、来源带 `AiState` 且非友方 NPC”的爪/咬命中，扣 `VehicleMotion.condition` 并显示 `载具受击：车况 X%`；服务器 `ApplyServerVehicleZombieAttacks` 用同一命中记录和冷却权威扣 `ecs::Vehicle.condition`，回 `status`，客户端只读复制状态。动物/友方 NPC、未命中的攻击不扣车况。

**测试/证据**：新增 `M25-T4-V15 VehicleZombieAttack：车况损耗与钳制` 与 `M25-T4-V15 服务器载具受僵尸攻击扣车况`，合计 **2/2（25 assertions）**；`M25-T4*` **18/18（234 assertions）**；载具过滤 **32/32（426 assertions）**；全量 Debug **742/742（133089 assertions）**、Release **742/742（133103 assertions）**、Python **89/89**；日志 `build/m25_t4_v15_targeted.log`、`build/m25_t4_v15_all.log`、`build/m25_t4_v15_vehicle_all.log`、`build/m25_t4_v15_debug.log`、`build/m25_t4_v15_release.log`、`build/m25_t4_v15_python.log`（targeted 同时含单元与服务器集成 V15 用例）。Release 1280 烟测 `build/m25_t4_v15_vehicle_attack_smoke/`：真实 Enter 上车后探针在车旁生成 1 个僵尸（约 `83.9,20.5`，距离约 0.97 格），日志 `载具受僵尸攻击` 3 次，车况 95%→90%→85%，HUD 同步；4 张 `v15_near_vehicle.png`、`v15_zombie_attack.png`、`v15_vehicle_hit.png`、`v15_condition_damaged.png`。烟测临时使用 5% 磨损/0.5s 冷却；玩家 100000 HP、地图既有 28 个僵尸保留、关闭 spawner 补充；构造烟测，不替代自然尸群试玩。

**限制/下一步**：只有一个固定每击损耗与全车共用冷却，不区分攻击部位/僵尸变体/车身位置；只统计已成功进入 `CombatSystem` 的 AI 命中，不覆盖被闪避/未命中；动物/友方 NPC 不触发；不新增载具受击音效/火花/破损外观；车况归零沿用 V6 动力衰减，不触发爆炸/报废动画。正式音效和更完整自然试玩仍待后续。未提交。

## 2026-11-06 M25-T4 V14 载具喇叭与主动噪音

**范围**：驾驶中按 `H` 鸣笛，补足车辆主动制造噪音/引怪的操作；单机本地播放 `vehicle_horn` 并向 `OnSound` 投递 24 格载具噪音，联机由服务器权威限频、广播 `WorldSound` 并触发 AI 听觉。只改载具交互，不改车况/动力/碰撞/存档/PlayerState；不做多车、长按蓄力、独立键位面板、音量面板或正式音效。

**实现**：`VehicleSpike.h` 新增 `VehicleHornReady(condition, cooldown)`（有限值、车况 > 0、冷却 <= 0）、`kVehicleHornNoiseRadius=24`、`kVehicleHornCooldownSeconds=1.5`，`VehicleTuning`/客户端与服务器配置解析新增 `hornNoiseRadius/hornCooldownSeconds`。`WorldDemoState::HonkVehicleHorn` 在驾驶分支复用 `RepairStructure` 的 `H` 键，单机向事件总线发 `SoundEvent{type=Vehicle, clip=vehicle_horn, radius=24}`，联机发送 `CommandType::HonkHorn=44`；HUD 追加 `H 喇叭`。服务器 `HonkServerVehicleHorn` 校验驾驶位/车况/冷却，广播同一 clip 的 `WorldSound` 并投递 AI `OnSound`，冷却随车辆 tick 递减，无存档/复制字段。新增 `assets/audio/vehicle_horn.wav`（0.48s、22050Hz、单声道、双音占位）；旧服务器会拒绝未知命令，旧客户端不会主动发该命令。

**测试/证据**：新增 `M25-T4-V14 VehicleHorn：可用性与冷却` **1/1（7 assertions）**；`M25-T4*` **16/16（209 assertions）**；载具过滤 **31/31（416 assertions）**；全量 Debug **740/740（133064 assertions）**、Release **740/740（133078 assertions）**、Python **89/89**；日志 `build/m25_t4_v14_targeted.log`、`build/m25_t4_v14_targeted_all.log`、`build/m25_t4_v14_vehicle_all.log`、`build/m25_t4_v14_debug.log`、`build/m25_t4_v14_release.log`、`build/m25_t4_v14_python.log`。服务器载具集成用例扩展 `HonkHorn` 并断言收到 `vehicle_horn`；`test_audio` null-device 用例加载并播放该 clip。Release 1280 烟测 `build/m25_t4_v14_horn_smoke/`：真实 Enter 上车后探针生成 1 个目标（约 10.58 格），按 `H` 后日志 `载具喇叭：吸引半径 24 格`，4 秒内目标距离降到约 1 格；1.5s 内第二次 `H` 只产生 1 条喇叭日志；4 张截图 `v14_near_vehicle.png`、`v14_driving_horn.png`、`v14_horn_cooldown.png`、`v14_zombie_attracted.png`，HUD 显示 `H 喇叭`。玩家 100000 HP、地图 27 个既有僵尸保留、关闭 spawner，`--audio.device=off`；构造烟测，不替代自然试听。

**限制/下一步**：`H` 上下文复用维修键，未做独立键位面板项/冷却 UI/喇叭光环/车外声源；只有 0.48s 占位双音，不是正式喇叭音频；单机由 SoundManager 默认音量组播放，联机服务器权威但未单独做双客户端喇叭截图；只测 1 车 1 目标，多车/多人叠加与自然尸群体验未覆盖；烟测目标为探针生成且玩家高生命。正式音效和更完整自然试玩仍待后续。未提交。

## 2026-11-06 M25-T4 V13 软粒子烟雾

**范围**：把 V11 的屏幕空间矩形烟团升级为程序化软粒子，保留轻/重/报废三级与火星；只改表现，不改耐久/动力/碰撞/协议/存档，不做体积烟/风向/遮挡、焦黑车身、起火/爆炸或正式音效。

**实现**：`render::SpriteBatch` 初始化时生成独立 96×96 RGBA 径向 alpha 纹理（中心高 alpha、边缘渐隐、轻微角度扰动），不写图集/manifest；`VehicleSpike.h` 新增 `VehicleSmokeParticleScale`（0.70×→1.60×）与 `VehicleSmokeParticleAlpha`（轻/重/报废峰值 0.75/0.85/0.95，寿命最后 35% 淡出）。`WorldDemoState::DrawVehicleDamageSmoke` 每团绘制 outer/core 两层软精灵，报废级加暖色软火星；纹理不可用时回退 V11 矩形；本地/网络共用已复制车况/位置，不写存档、不复制、不改协议。

**测试/证据**：`M25-T4-V13 VehicleSoftSmoke：软粒子尺寸与淡出` **1/1（15 assertions）**；`M25-T4*` **15/15（198 assertions）**；载具过滤 **30/30（409 assertions）**；全量 Debug **739/739（133038 assertions）**、Release **739/739（133052 assertions）**、Python **89/89**；日志 `build/m25_t4_v13_targeted.log`、`build/m25_t4_v13_targeted_all.log`、`build/m25_t4_v13_vehicle_all.log`、`build/m25_t4_v13_debug.log`、`build/m25_t4_v13_release.log`、`build/m25_t4_v13_python.log`。Release 1280 软粒子烟测：构造起点，真实 Enter 上车，通过探针目标与 25%/5s 车况磨损参数依次得到 75%→50%→25%→0%，每档下车截图；5 张截图在 `build/m25_t4_v13_soft_smoke/`；日志含 `smoke level=1/2/3`、4 次 `撞击车况`、`载具烟雾：软粒子（程序纹理）`。玩家 100000 HP，地图既有 27 个僵尸保留，探针额外 1 个目标；关闭 spawner 补充/动物；构造烟测，不替代自然试玩。

**限制/下一步**：软纹理是屏幕空间 billboard，不随相机旋转/参与光照，仍有少量点状缩放；烟源固定在车头前方，不随风向漂移；轻烟在浅色车身/遮挡下较淡；无体积烟/遮挡、焦黑车身、起火/爆炸与正式音效；联机未单独双端烟雾截图。未提交。

## 2026-11-06 M25-T4 V12 八向皮卡外观

**范围**：把 V10 的四向皮卡升级为八向，补东南/西南/西北/东北四帧；只改表现，不改碰撞/动力/耐久/协议/存档，不做轮胎转向、16 向、正式手绘/车身损伤、软粒子或正式音效。

**实现**：`tools/gen_art_slice.py` 的 `_vehicle_axes` 增加四个对角方向，生成 160×128 八帧并更新 `assets/own/atlas_own.png/json`（188 帧）；manifest 新增 4 个 `object.vehicle_*` 逻辑名，`object.vehicle` 保持南向别名。`VehicleSpike.h` 把 `VehicleVisualDirection` 扩为 8 值；`VehicleVisualDirectionFor` 以 `atan2(vy,vx)` 落入 45° 扇区并保留 8° 额外滞回，零速度/非有限速度保持当前方向；`VehicleVisualSprite/Label` 覆盖八向。`WorldDemoState::UpdateVehicleVisual` 继续用本地/网络复制的 heading 更新 `SpriteRef`，生成/绑定/网络目标切换默认南向；不写存档、不复制、不新增命令。

**测试/证据**：`M25-T4-V12 VehicleVisual：八向扇区与切换滞回` **1/1（29 assertions）**；`M25-T4*` **14/14（183 assertions）**；载具过滤 **29/29（394 assertions）**；全量 Debug **738/738（133024 assertions）**、Release **738/738（133037 assertions）**、Python **89/89**；日志 `build/m25_t4_v12_targeted.log`、`build/m25_t4_v12_targeted_all.log`、`build/m25_t4_v12_vehicle_all.log`、`build/m25_t4_v12_debug.log`、`build/m25_t4_v12_release.log`、`build/m25_t4_v12_python.log`。Release 1280 八向烟测：探针把玩家放到开阔地皮卡旁（车辆偏移 13,-3，约 82.5,20.5），真实 Enter 上车后八个短脉冲依次验证东/东南/南/西南/西/西北/北/东北，日志完整出现八向；11 张截图在 `build/m25_t4_v12_vehicle_visual_smoke/`。关闭 spawner 补充与动物，地图既有 27 个僵尸保留，玩家 100000 HP 仅防干扰；未使用 `Zombies.Spawn`，构造起点不替代自然行走试玩。

**限制/下一步**：45° 扇区使部分斜向帧呈端视/窄侧视；8° 滞回在极慢斜向转向/贴墙时可能短暂保留上一方向；外观尺寸仍大于 0.65 碰撞半径；无轮胎转向、16 向、车身损伤分层和正式音效；联机外观只共用代码，未单独双端截图。未提交。

## 2026-11-06 M25-T4 V11 低车况/归零烟雾

**范围**：车况 50% 及以下由表现层分级冒烟，补足 V6 只有 HUD/日志“动力下降”的反馈缺口；不做真实粒子/起火/爆炸/焦黑贴图、软粒子、协议/存档或正式音效。

**实现**：`VehicleSpike.h` 新增 `VehicleDamageSmokeLevelFor`（50/25/0% → None/Light/Heavy/Critical）、`VehicleDamageSmokeInterval`（0.55/0.28/0.14s）、`VehicleDamageSmokeLifetime`（1.5/1.9/2.4s）。`WorldDemoState` 增加烟团列表与计时器：每帧老化/补充，烟团包含世界位置、寿命、尺寸和漂移；`DrawVehicleDamageSmoke` 在车辆后绘制三层半透明矩形并上浮，报废级加橙色火星。烟级变化日志 `载具烟雾…（smoke level=n）`；本地与网络客户端共用已复制车况/位置，不写存档、不复制、不新增命令。

**测试/证据**：新增 `M25-T4-V11 VehicleDamageSmoke：车况烟雾等级与间隔` **1/1（14 assertions）**；`M25-T4*` **14/14（167 assertions）**；载具过滤 **29/29（378 assertions）**；全量 Debug **738/738（133007 assertions）**、Release **738/738（133021 assertions）**、Python **89/89**；日志 `build/m25_t4_v11_targeted_all_final.log`、`build/m25_t4_v11_debug_final.log`、`build/m25_t4_v11_release_final2.log`、`build/m25_t4_v11_python_final.log`。Release 1280 烟雾烟测：默认出生点走到皮卡，构造 1 个僵尸撞两次，50% 轻烟截图、0% 黑烟/火星截图，日志 `smoke level=1`/`=3`；`build/m25_t4_v11_condition_smoke/` 4 张。默认游荡者/动物关闭，不替代自然试玩。

**限制/下一步**：矩形烟团是占位表现，不是软粒子/体积烟；50% 轻烟在遮挡时较淡；无焦黑车身、起火/爆炸、风向/遮挡和正式音效；联机烟雾只共用代码，未单独双端截图。首跑 Release 全量在 `M23-F2b-T6` 动物剥取状态超时，单独复跑和二次全量均通过，判断为既有测试时序波动，与本切片无直接关系。未提交。

## 2026-11-05 M25-T4 V10 皮卡四向外观

**范围**：把木箱占位替换为四向程序化等距皮卡外观，按世界速度主轴选帧；不改碰撞/动力/载具状态/协议/存档，不做八向、轮胎转向、正式手绘/车身损伤分层/音效。

**实现**：`tools/gen_art_slice.py` 新增 `make_vehicle` 与 `vehicle_north/east/south/west` 160×128 帧；manifest 新增四个 `object.vehicle_*` 资源，`object.vehicle` 保留 `vehicle_south` 兼容。`VehicleSpike.h` 新增 `VehicleVisualDirectionFor`（`|vx|/|vy|` 主轴 + 1.2 滞回）、`VehicleVisualSprite/Label`；`WorldDemoState::UpdateVehicleVisual` 在生成/绑定后按 heading 更新 `SpriteRef`，本地/网络客户端共用，初始/切换默认南向，不写存档、不复制、不新增命令。`SpriteScale` 1.7→1.0。

**测试/证据**：`M25-T4-V10` **1/1（13 assertions）**，`M25-T4*` **13/13（153 assertions）**；全量 Debug **737/737（132993 assertions）**、Release **737/737（133008 assertions）**、Python **89/89**；日志 `build/m25_t4_v10_targeted_final.log`、`build/m25_t4_v10_targeted_all_final.log`、`build/m25_t4_v10_debug_final.log`、`build/m25_t4_v10_release_final.log`、`build/m25_t4_v10_python_final.log`。Release 1280 外观烟测：默认出生点走到皮卡、Enter 后四向驾驶，日志 `外观朝向：东/南/西/北`，8 张截图在 `build/m25_t4_v10_vehicle_visual_smoke/`，驾驶中生命 96；为专注外观关闭默认游荡者/动物，不替代自然试玩。

**限制/下一步**：四向而非八向，斜向/贴墙仍可能按主轴翻向；外观尺寸大于 0.65 碰撞半径；无正式手绘、轮胎转向、车身损伤表现和正式音效；联机外观只共用代码，未单独双端截图；开阔地自动航点仍可能因建筑碰撞超时。未提交。

## 2026-11-05 M25-T4 V9 自然试玩与驾驶员防护

**范围**：用默认出生点、真实 WASD/Enter 和默认游荡者做自然驾驶，不传送/不刷僵尸；根据自然尸群压力补驾驶位爪/咬减伤与平衡收口。不做正式美术/音效、完整人工试玩、分部件耐久、多车或刚体。

**实现**：新增运行时 `ecs::VehicleOccupant{damageReduction}`，单机上车/服务器 `SyncVehicleDriver` 挂载，下车/载具消失/`ReleaseServerVehicleDriver` 移除；`config/game.json` 新增 `vehicle.driverDamageReduction=0.85`，`VehicleTuning`/两端解析钳制。`CombatSystem::ApplyDamage` 仅对爪/咬应用车辆减伤，与护甲取最大值、上限 95%；火焰/钝击/调试/PvP 不吃。驾驶 HUD 显示 `防护 85%`，服务器上车状态/日志同步；护甲磨损改为仅在实际护甲减伤时触发。修单机车辆消失时的防护清理。

**测试/证据**：`M25-T4-V9` **1/1（9 assertions）**，`M25-T4*` **12/12（140 assertions）**，T3 服务器上下车用例补 `VehicleOccupant` 挂载/移除断言；全量 Debug **736/736（132980 assertions）**、Release **736/736（132994 assertions）**、Python **88/88**；日志 `build/m25_t4_v9_targeted.log`、`build/m25_t4_v9_targeted_all.log`、`build/m25_t4_v9_debug.log`、`build/m25_t4_v9_release.log`、`build/m25_t4_v9_python.log`。Release 单机 1280×720 自然烟测：默认出生点走到皮卡，沿东侧开阔地驾驶，默认游荡者 id 10 追击；日志 `驾驶员防护 85%`、`撞击车况...剩余 84%`、`载具撞杀...+25 近战经验`，驾驶中剩 70.3 HP、3 次撞杀；6 张截图在 `build/m25_t4_v9_natural_smoke/`，脚本 `build/m25_t4_v9_natural_smoke.ps1`（临时）。

**限制/下一步**：75% 减伤自然跑仅剩约 3 HP，最终取 85%；仍可能被围困致死，不做完全免疫/护甲耐久/乘客保护；自然路径曾触发移动超时回退，说明车辆仍会被建筑/地形卡住；正式美术/音效、动物/玩家碾压、多车/刚体/乘客仍待后续。未提交。

## 2026-11-05 M25-T4 V8 远端载具引擎音

**范围**：联机非驾驶客户端听到远端载具引擎循环；本地驾驶仍优先，不新增正式音效、轮胎音、多车混音或协议。

**实现**：`VehicleEngineAudioSourceFor` 统一本地优先/远端/无源策略，车况归零不播放。`WorldDemoState::NetworkVehicleHasRemoteDriver` 从 ZLR1 `VHCL` 复制 actor 的 `vehicleDriverStableId` 判断远端驾驶员；`UpdateVehicleAudio` 在非本地驾驶但有远端驾驶员时复用同一条 `vehicle_engine` 3D 循环，位置/速度/车况来自已复制载具，音量/音高沿用 `VehicleEngineToneForSpeed`。远端下车、解绑/离开视野、断线、车况归零或本地重新驾驶时停止；绑定清除路径补 `StopVehicleAudio`。不新增命令、复制字段、协议或存档 schema。

**测试/证据**：新增 `M25-T4-V8 VehicleEngineAudioSource` 六条策略断言；目标 **35/35（645 assertions）**；全量 Debug **735/735（132968 assertions）**、Release **735/735（132982 assertions）**、Python **88/88**；日志 `build/m25_t4_v8_targeted_all.log`、`build/m25_t4_v8_debug.log`、`build/m25_t4_v8_release.log`、`build/m25_t4_v8_python.log`。双客户端 1280×720 烟测（客户端 B `audio.device=null`）：A 上车后 B 日志 `引擎音效启动（远端，...）`，A 下车后 B 日志 `引擎音效停止`；截图 `b_remote_engine.png`、`b_remote_stopped.png`、`a_driving.png`、`a_remote_stopped.png`，目录 `build/m25_t4_v8_remote_audio_smoke/`，脚本为临时早期停止版。

**限制/下一步**：只支持单服务器单车；不做多车实例化、远端撞击音重复播放、正式混音/遮挡/听感验收；远端引擎音依赖服务器复制频率；null device 只验证句柄/资源/状态，不代表实际听感；未提交。

## 2026-11-05 M25-T4 V7 撞击击杀经验归属

**范围**：载具撞杀僵尸后给驾驶员发放一次近战击杀经验；单机与服务器权威同规则。不做动物/玩家碾压、载具专属技能、撞击命中经验、任务专属协议或分部件结算。

**实现**：`ApplyLocalVehicleRam` 在 `ApplyDamage` 成功后读取目标生命，致死才调用 `GrantProgressionXp(Melee, progression.meleeXpPerKill, "载具撞击")` 并写 `载具撞杀... +x 近战经验`；非致死不发。服务器 `ApplyServerVehicleRam` 按 `driverStableId` 找驾驶员与 Session，调用 `AwardProgressionXp`，由既有 PlayerState 同步；客户端不重复发放。沿用默认 `meleeXpPerKill=25`，不新增技能/配置/协议/存档字段。任务 `Kill any` 与随机事件目标继续使用现有 `CombatSystem.kills`/死亡检测，不新增任务协议。

**测试/证据**：扩展 `M25-T4 服务器载具撞击/噪音与车灯复制`，在载具位置构造 1 HP 目标并断言驾驶员 Melee XP 上升；目标 **34/34（639 assertions）**；全量 Debug **734/734（132962 assertions）**、Release **734/734（132976 assertions）**、Python **88/88**；日志 `build/m25_t4_v7_targeted_all.log`、`build/m25_t4_v7_debug.log`、`build/m25_t4_v7_release.log`、`build/m25_t4_v7_python.log`。Release 单机 1280×720 构造烟测：临时 `ramBaseDamage=200`，1 个僵尸，速度 5.5 撞击日志 `伤害 200`、`载具撞杀：...+25 近战经验`、`撞击车况...剩余 96%`，HUD `XP 25/180`；4 张截图在 `build/m25_t4_v7_ram_xp_smoke/`，脚本 `build/m25_t4_v7_ram_xp_smoke.ps1`（临时）。

**限制/下一步**：只对僵尸生效；近战经验是临时归类，没有驾驶技能；不发放撞击命中经验，不处理载具击杀的任务专属归属/分配；烟测为构造单目标撞击，不是自然战斗平衡；未提交。

## 2026-11-05 M25-T4 V6 低车况动力衰减与失效反馈

**范围**：车况低于 75% 后按车况线性降低极速与加速，HUD/日志提示“动力下降”；车况归零时停止本地引擎循环音，保留“车辆损坏/按 E 维修”。不做分部件耐久、轮胎/悬挂、起火/爆炸/报废演出、正式音效。

**实现**：`VehicleConditionPerformanceScale` 返回 0..1 动力倍率（75% 以上 1.0，50% 0.85，25% 0.70，0% 0，0..75% 线性）；`VehicleStep` 把倍率乘到极速/加速，制动不变，`VehiclePredictStep` 复用同一规则。单机 `WorldDemoState` 首次跌破阈值显示“车况低：动力下降”并写 `[vehicle] 车况低：动力 x...`，HUD 追加“动力下降”；`UpdateVehicleAudio` 在车况归零时停止引擎循环，维修后恢复。服务器沿用同一 `VehicleStep`，不新增协议/复制字段/存档 schema；客户端预测与服务器同规则。新增 `m_vehicleLowConditionWarned` 仅用于单次提示，进入车辆时重置。

**测试/证据**：新增 `M25-T4-V6 VehicleCondition：低车况动力倍率与极速衰减`（100/75/50/25/0%、非法值、极速/加速上限、预测复用）；目标 **34/34（634 assertions）**；全量 Debug **734/734（132957 assertions）**、Release **734/734（132971 assertions）**、Python **88/88**；日志 `build/m25_t4_v6_targeted_all.log`、`build/m25_t4_v6_debug.log`、`build/m25_t4_v6_release.log`、`build/m25_t4_v6_python.log`。Release 单机 1280×720 构造烟测：临时把单次撞击磨损调到 60%，1 个僵尸，速度 5.5 撞击后 `剩余 40%`、`车况低：动力 x0.79（车况 40%）`，HUD `车况 39% · 路面 草地 · 动力下降`；4 张截图在 `build/m25_t4_v6_low_condition_smoke/`，脚本 `build/m25_t4_v6_low_condition_smoke.ps1`（临时）。

**限制/下一步**：动力衰减是整车单值且立即截断当前极速，不做分部件/渐进失速；75% 以上无手感变化；烟测为构造高磨损撞击，不是自然驾驶平衡；车况归零的引擎音停止为代码路径，未单独听感验收；未提交。

## 2026-11-05 M25-T4 V5 撞击车况磨损

**范围**：载具成功撞击僵尸后，按速度扣车况；单机与服务器权威同规则。不做动物/玩家碾压、撞击击杀 XP/任务归属、分部件耐久、车况归零后的起火/报废演出。

**实现**：`VehicleRamConditionDamage` 返回基础 1.5% + 超过 `ramMinSpeed` 的速度 × 0.8%/格，上限 12%；`VehicleApplyRamConditionDamage` 按当前车况夹取并返回实际扣减。单机 `WorldDemoState::ApplyLocalVehicleRam` 在命中后扣车况并显示 `载具撞击：车况 xx%`；服务器 `ApplyServerVehicleRam` 改为返回是否命中，权威扣车况并回 `status` “撞击：车况 xx%”。两端各用 `ramWearCooldown`（重置为 `ramCooldownSeconds`）限制同一窗口内多目标只扣一次。配置新增 `vehicle.ramConditionDamageBase/PerSpeed/Max`；沿用 `ecs::Vehicle.condition` 存档与 ZLR1，不新增协议/schema。同时校正 `VehicleRamStagger` 单测期望为超过阈值的速度项，与实际实现一致。

**测试/证据**：新增 `M25-T4-V5 VehicleRamCondition：速度映射与车况钳制`（阈值、基础/速度项、上限、NaN、车况夹取、空指针）；服务器集成用例追加撞击后 condition 下降断言。目标 **33/33（620 assertions）**；全量 Debug **733/733（132943 assertions）**、Release **733/733（132957 assertions）**、Python **88/88**；日志 `build/m25_t4_v5_targeted.log`、`build/m25_t4_v5_debug.log`、`build/m25_t4_v5_release.log`、`build/m25_t4_v5_python.log`。Release 单机 1280×720 构造烟测：12 个僵尸，`撞击车况：速度 2.1，扣车况 1.5%，剩余 98%`，HUD 98%；后续 2.7/4.2 速度命中共用冷却未重复扣；4 张截图在 `build/m25_t4_v5_ram_wear_smoke/`，脚本 `build/m25_t4_v5_ram_wear_smoke.ps1`（临时）。

**限制/下一步**：只对载具当前撞击目标（僵尸）生效，不含动物/玩家；不结算击杀 XP/任务归属；车况磨损是整车单值，无分部件；烟测为低速构造撞击，未覆盖车况归零、连续尸群、自然驾驶与正式平衡；未提交。

## 2026-11-05 M25-T4 V4 载具引擎与撞击占位音效

**范围**：驾驶时本地引擎循环音（音量/音高随速度）、撞击僵尸的短促撞击音；不做正式车辆音效、远端车辆引擎声、路面材质音、音量面板与真实设备听感验收。

**实现**：新增程序合成 `assets/audio/vehicle_engine.wav`（2s 循环）与 `vehicle_ram.wav`（0.3s）。`AudioEngine::Play3D` 增加 `allowPitch` 选项与 `SetVolume/SetPitch/SetPosition` 长音更新接口，默认音效仍禁用 pitch。`VehicleEngineToneForSpeed` 把速度映射到音量 0.18→0.62、音高倍率 x0.82→x1.28；`WorldDemoState::UpdateVehicleAudio` 在本地驾驶时启动/更新循环，下车、车辆失效、断线或退出时停止；单机撞击播放撞击音，服务器权威撞击后广播 `WorldSound`（`Impact` + `vehicle_ram` clip，半径 18）。配置新增 `vehicle.engineIdleVolume/engineMaxVolume/engineMinPitch/engineMaxPitch/ramSoundVolume`。

**测试/证据**：新增 `M25-T4-V4 VehicleAudio` 速度映射/钳制用例；扩展 AudioEngine null device 用例验证引擎循环、位置/音量/音高和撞击 clip；服务器集成用例验证客户端收到 `vehicle_ram` 声音。目标 **32/32（607 assertions）**；全量 Debug **732/732（132931 assertions）**、Release **732/732（132944 assertions）**、Python **88/88**；日志 `build/m25_t4_v4_targeted.log`、`build/m25_t4_v4_debug.log`、`build/m25_t4_v4_release.log`、`build/m25_t4_v4_python.log`。Release 单机 1280×720、`audio.device=null` 冒烟：草地/泥土路各完成上车→引擎启动→下车停止→再次上车启动，日志各 2 次启动/1 次停止，并复跑路面距离（草地 3.16 格、泥土路 5.63 格），6 张截图在 `build/m25_t4_v4_audio_smoke/`，脚本 `build/m25_t4_v4_audio_smoke.ps1`（临时）。

**限制/下一步**：引擎声仅本地驾驶者车辆，远端车辆无引擎循环；撞击音为合成占位，未做材质/速度分层、混响/遮挡和正式混音；null device 只验证句柄/资源/状态，不代表实际听感；正式车辆美术/音效与自然试玩仍待后续；未提交。

## 2026-11-05 M25-T4 V3 载具路面附着

**范围**：按车辆中心地面识别铺装/泥土路/草地，调整极速/加速/制动；不做四轮独立采样、真实漂移、坡道/湿度、正式路面美术。现有地图的 `tile.floor.dirt` 即泥土路。

**实现**：`VehicleSpike` 增加 `VehicleSurface`/`VehicleSurfaceProfile`/`VehicleSurfaceForFloor`/`VehicleSurfaceProfileFor`/`VehicleSurfaceLabel`，`VehicleStep`/`VehiclePredictStep` 接受可选路面参数，默认铺装保持旧行为。铺装 x1.0/x1.0/x1.0，泥土路 x0.92/x0.88/x0.92，草地 x0.62/x0.55/x0.72；`config/game.json` 新增 6 个倍率并钳制 0..1。单机 `WorldDemoState::VehicleSurfaceAt` 取中心格，驾驶 HUD 追加 `路面 ...` 并在变化时写日志；服务器 `ServerVehicleSurfaceAt` 用同一规则权威步进并写 `服务器路面 ...`；客户端预测使用同一分类。不新增命令/复制字段/存档 schema。

**测试/证据**：新增 `M25-T4-V3 VehicleSurface` 分类/倍率、极速/加速排序、制动/预测 3 个用例；目标 **29/29（576 assertions）**；全量 Debug **731/731（132913 assertions）**、Release **731/731（132927 assertions）**、Python **88/88**；日志 `build/m25_t4_v3_targeted.log`、`build/m25_t4_v3_debug.log`、`build/m25_t4_v3_release.log`、`build/m25_t4_v3_python.log`。Release 单机 1280×720 构造烟测：同方向各 1s，草地起点 (82.5,20.5) 移动 3.16 格、泥土路起点 (82.5,26.5) 移动 5.44 格；HUD/日志确认 `路面 草地`/`路面 泥土路`，6 张截图在 `build/m25_t4_v3_surface_smoke/`，脚本 `build/m25_t4_v3_surface_smoke.ps1`（临时）。

**限制/下一步**：按中心单格判定，跨路缘/斜向可能提前降速；进入低附着路面时极速上限立即截断；不改变玩家步行；烟测为构造起点直行，不是自然试玩；正式车辆美术/音效仍待后续（V4 已补占位引擎/撞击音）；自然试玩仍待后续；未提交。

## 2026-11-05 M25-T4 V2 载具车辆车灯

**范围**：只做皮卡驾驶中车灯开关、占位光束、存档与联机同步；不做真实光照/阴影、道路附着、正式车辆美术/音效。驾驶中 `L` 切换；非驾驶 `L` 保持任务焦点。

**实现**：`VehicleMotion`/`ecs::Vehicle` 增加 `headlights`；`Vehicle` 存档组件升 v2（`minimumVersion=1`，旧档缺失按 false），单机切换立即写 ECS。联机新增 `CommandType::ToggleHeadlights=43`，服务器校验驾驶位后权威切换并回 status，ZLR1 `VHCL` 从 16B 扩到 17B 复制车灯，客户端不本地切换。`WorldDemoState::DrawVehicleHeadlights` 以 18 条半透明射线、远端弧和双灯点绘制占位光束，夜间强度 1.0/白天 0.28，方向由速度保持；驾驶 HUD 显示 `L 车灯开/关`。

**测试/证据**：新增 `M25-T4-V2 车灯状态在驾驶步进后保留`；扩展存档、ZLR1 车辆复制、CommandPacket 与服务器载具集成用例。目标用例 **26/26（544 assertions）**；全量 Debug **728/728（132881 assertions）**、Release **728/728（132895 assertions）**、Python **88/88**；日志 `build/m25_t4_v2_targeted.log`、`build/m25_t4_v2_debug.log`、`build/m25_t4_v2_release.log`、`build/m25_t4_v2_python.log`。Release 单机 1280×720、`game.startHour=22` 构造烟测：5 张截图（`build/m25_t4_v2_headlights_smoke/`）与 `game.log`，HUD/日志确认 `车灯：开`/`车灯：关`，脚本 `build/m25_t4_v2_headlights_smoke.ps1`（临时）。

**限制/下一步**：光束是屏幕空间占位扇形，不改变世界亮度/阴影/碰撞；无正式车灯音效；路面附着由 V3 补齐；烟测为构造传送，不是自然试玩；未提交。

## 2026-11-05 M25-T4 V1 载具撞击僵尸与噪音引怪

**范围**：只做载具碾压/撞击僵尸与车辆噪音引怪，不做车灯、道路附着、动物/玩家碾压、正式美术/音效。

**实现**：`VehicleSpike` 新增撞击伤害/击退/硬直/重叠与配置；单机 `WorldDemoState` 在驾驶 tick 后对同层 `AiState+Health` 目标走 `CombatSystem::ApplyDamage`（Blunt），按目标 0.75s 冷却避免每 tick 重复命中；速度超过阈值后按 0.45s 间隔发送 `OnSound`（新增 `SoundType::Vehicle`，半径 12）引怪。联机 `ServerHost` 执行同一套规则并权威扣血，Health 走 ZLR1 复制；服务器广播 `WorldSound` 供客户端播放，客户端不本地扣血。`config/game.json` 增加 ram/noise 参数，单机与服务器启动解析。

**测试/证据**：`M25-T4 VehicleRam` 纯逻辑 + `M25-T4 服务器载具撞击僵尸、复制与噪音广播` 集成用例，共 **2/2（29 assertions）**；服务器测试验证撞击扣血复制、`worldSoundsSent` 广播与 AI `sinceHeard` 引怪。全量 Debug **726/726（132845 assertions）**、Release **726/726（132859 assertions）**、Python **88/88**；日志 `build/m25_t4_targeted.log`、`build/m25_t4_debug.log`、`build/m25_t4_release.log`、`build/m25_t4_python.log`。另做 Release 单机 1280×720 构造烟测：传送至皮卡旁、驾驶并脚本刷 12 个僵尸，日志 5 次 `载具撞僵尸`，4 张截图与 `game.log` 在 `build/m25_t4_vehicle_world_smoke/`，脚本 `build/m25_t4_smoke.ps1`。

**限制/下一步**：1280 烟测为传送/构造刷僵尸，不是自然试玩；`SoundType::Vehicle` 暂用 Impact 占位音效；撞击不计车辆耐久损伤、不额外结算击杀 XP/任务归属；车灯由 V2、路面附着由 V3 补齐，正式音效后续。未提交。

## 2026-11-05 M25-T3 V7 全量测试与文档收口

**范围**：补齐 DoD 中载具上下车距离、层级、座位占用、死亡/睡眠校验；执行 Debug/Release/Python 全量回归；回填 `docs/12`、`docs/10`、`docs/11`、`docs/01`、`docs/05`、README 与本文。

**实现/修复**：新增 `M25-T3-V7 载具上下车距离、层级、占用、睡眠与死亡校验` 集成用例，覆盖远距离上车拒绝、跨层上车拒绝、睡眠投票中车辆命令拒绝、A 驾驶时 B 抢座拒绝、A 驾驶中死亡释放座位且死亡状态上车拒绝；睡眠投票/睡眠中非睡眠命令现在写入 `PlayerState.status`（“睡眠投票或睡眠中无法行动”），此前只记录命令拒绝。

**测试/证据**：目标 **1/1（47 assertions）**；全量 Debug **724/724（132817 assertions）**、Release **724/724（132830 assertions）**、Python **88/88**；日志 `build/m25_t3_v7_targeted.log`、`build/m25_t3_v7_debug.log`、`build/m25_t3_v7_release.log`、`build/m25_t3_v7_python.log`。V6 21 张 1280 截图与双客户端烟测证据仍保留在 `build/m25_t3_vehicle_mp_smoke/`。

**限制/下一步**：30s 周期玩家档案同步保存仍可能阻塞心跳，V6 烟测临时关闭、正式服务器保持默认，后续异步化并补压力回归；V6 烟测使用临时存档/探针预装后备箱，不是自然多人试玩；用户以“继续下一步”确认 M25-T3 收口（2026-11-05）；M25-T4 V1 随后完成自动化+1280 构造烟测，V2 车灯、V3 路面附着、V4 引擎/撞击占位音效、V5 撞击车况磨损、V6 低车况动力衰减、V7 撞击击杀经验、V8 远端载具引擎音也已补齐；自然试玩/正式音效待后续；未提交。

## 2026-11-05 M25-T3 V6 双客户端 1280 联机烟测

**范围**：Release 服务器 + 两个 1280×720 客户端，A 从村庄出生点走到皮卡旁，先取后备箱预装物资再上车；B 近距离观察并尝试抢座。A 驾驶撞墙（车况 100%→0%）、下车维修（0%→100%）、加油（97%→100%），第二次打开后备箱存回剩余 5 件/3 堆；随后 A 在驾驶中触发服务器正常 shutdown，服务器释放座位并保存；A/B 自动重连，A 收到“驾驶位已释放”提示，车辆在服务器重启后恢复为燃料 97%、车况 100%、后备箱 3 堆，A 再次上车/下车成功。烟测关闭游荡者、动物与 POI 守军，临时单机存档生成器把维修包/燃料罐/绷带预装进车辆后备箱。

**修复**：联机下社交聊天的打开键原为 `ENTER`，会在车辆交互前消费按键；改为 `Y` 打开聊天、`ENTER` 发送，`ENTER` 保留上下车（`UiSystem`/`SocialPanel`/`social.rml`/`multiplayer.rml` 同步）。`ScriptApi::FindPlayer` 在无 `PlayerTag` 的网络本地镜像上回退到 `NetworkClient::PlayerStableId()`，使联机探针的 `Player.GetPosition`/`Inventory.GetState` 可读；服务器权威不变。

**测试/证据**：`m25-t3-v6-smoke-pass`；A 最终 HUD 燃料 96%/车况 100%，服务器重启恢复燃料 97%/车况 100%/后备箱 3 堆（5 件）；Run1/Run2 各 1 次载具恢复、0 次重复生成；21 张 1280 截图与 `server-run1.log`/`server-run2.log`/`a.log`/`b.log` 位于 `build/m25_t3_vehicle_mp_smoke/`，脚本 `build/m25_t3_v6_smoke.ps1`。全量 Debug **723/723（132770 assertions）**、Release **723/723（132784 assertions）**、Python **88/88**；日志 `build/m25_t3_v6_debug.log`、`build/m25_t3_v6_release.log`、`build/m25_t3_v6_python.log`。

**限制/下一步**：烟测使用临时存档预装后备箱与窗口消息/探针定位，仍不是自然多人试玩；服务端显式关闭 30s 周期档案保存以避免同步保存阻塞心跳，正式服务器仍默认 30s；A 最终 HUD 与服务器恢复燃料相差 1% 属快照/采样间隔。V7 做最终全量回归、DoD 收口与用户确认；未提交。

## 2026-11-05 M25-T3 V5 驾驶预测、重连与安全

**范围**：`WorldDemoState` 新增驾驶客户端预测：首个服务器副本初始化，之后按帧用 `VehiclePredictStep` 积分（不改燃料/车况），仅在 `Replication().LastAppliedTick()` 前进时用 `VehicleCorrectPosition` 校正（>1 格吸附、否则 0.25 混合）；预测位置同步车辆/驾驶员实体、相机和本地渲染。断线时停用预测、清理绑定，显示一次断开提示；重连收到 owner 状态后提示驾驶位已释放/已恢复步行。服务器 `Enter/Exit/Service` 状态提示、同身份接管释放座位、B 接管与驾驶中操作/非驾驶员 Move 隔离均补自动验证。

**测试/证据**：新增 `M25-T3-V5` 两用例，覆盖重复快照校正收敛且燃料/车况不变、A 驾驶时 B 上车被拒、A 驾驶中维修/攻击被拒且零副作用、B Move 不改车辆、A 同身份重连后步行/车辆位置/燃料/车况/后备箱保持、B 接管。目标 **2/2（93 assertions）**；全量 Debug **723/723（132769 assertions）**、Release **723/723（132783 assertions）**、Python **88/88**。日志 `build/m25_t3_v5_targeted.log`、`build/m25_t3_v5_debug.log`、`build/m25_t3_v5_release.log`、`build/m25_t3_v5_python.log`。

**限制/下一步**：真实 1280 双客户端画面、预测手感与断线/重启烟测在 V6；未提交。

## 2026-11-05 M25-T3 V4 车辆复制、客户端绑定与 HUD

**范围**：ZLR1 新增 `VHCL` 组件（燃料/车况/驾驶员 StableId）；服务器车辆同步 `Velocity`；客户端按 `PlayerState.vehicle`/首个车辆副本绑定 `m_vehicle`，同步位置、速度、燃料、车况、视觉缩放与碰撞体；车辆驾驶员精灵在本地/远端隐藏；`FindNearbyStorage` 允许联机车辆，`I` 打开服务器权威后备箱；联机 `Enter/Exit/ServiceVehicle` 通过命令提交，驾驶中只发 `Move`；HUD 显示服务器镜像燃料/车况与操作提示。驾驶预测/吸附仍留 V5。

**测试/证据**：新增 `M25-T3-V4` 复制单测与双客户端联机用例，覆盖燃料/车况/驾驶员 64 位 StableId、后备箱物品往返、A 驾驶后 B 收到位置/速度/燃料、A 下车后 driver=0、从车辆后备箱取物。目标 **2/2（57 assertions）**；全量 Debug **721/721（132676 assertions）**、Release **721/721（132690 assertions）**、Python **88/88**。日志 `build/m25_t3_v4_targeted.log`、`build/m25_t3_v4_debug.log`、`build/m25_t3_v4_release.log`、`build/m25_t3_v4_python.log`。

**限制/下一步**：客户端绑定/隐藏/HUD 尚未做 1280 实机烟测；驾驶预测、断线重连、服务提示和安全补测在 V5，双客户端画面证据在 V6。未提交。

## 2026-11-05 M25-T3 V3 服务器权威载具

**范围**：`ServerHost` 增加 `ServerVehicleRuntime`、唯一载具生成/读档接管、`Enter/Exit/ServiceVehicle` 命令、驾驶 `Move` 的屏幕→世界方向转换、服务器 tick 推进与玩家/物理体同步、碰撞/燃料状态、世界存档与重启恢复；断线/死亡/同身份接管释放座位。座位仍只存在运行时，车辆 StableId/位置/燃料/车况/后备箱随世界存档。

**测试**：新增服务器载具联调用例，覆盖上车/驾驶/下车/维修加油/缺物品回滚/非法下车/断线释放；重启用例覆盖 StableId/位置/燃料/车况/后备箱恢复、车辆不重复、座位为空、玩家在车外。全量 Debug **719/719（132619 assertions）**、Release **719/719（132633 assertions）**、Python **88/88**。日志 `build/m25_t3_v3_*.log`。

**限制/下一步**：ZLR1 车辆/驾驶员复制、客户端绑定/HUD/预测与双客户端烟测仍在 V4/V5/V6。未提交。

## 2026-11-05 M25-T3 V2 纯逻辑与协议

**范围**：新增载具座位/退出格/客户端预测/校正纯逻辑；新增运行时 `VehicleDriver`；ZLC1 增加 `EnterVehicle/ExitVehicle/ServiceVehicle`；`PlayerState` 升 v17，尾部增加 owner-only 车辆驾驶态（车辆 ID/驾驶中/燃料/车况），保留 v16 尾部兼容。

**验证**：`*Vehicle*` 18/18（233 assertions）、`*Command*` 5/5（269 assertions）、PlayerState 全量兼容用例通过；Debug **717/717（132539 assertions）**、Release **717/717（132553 assertions）**、Python **88/88**。日志 `build/m25_t3_v2_*.log`。

**限制/下一步**：尚未接入服务器权威、客户端复制/UI；V3 开始服务器车辆实体与命令。未提交。

## 2026-11-05 M25-T3 V1 载具联机设计

**范围**：新增 `docs/12-M25T3载具联机.md`，定义单服务器单车单驾驶位、服务器权威位置/速度/燃料/车况、上下车与驾驶输入命令、ZLR1 车辆/驾驶员复制、owner-only `PlayerState v17` 驾驶态、客户端位置预测/吸附校正、联机维修/加油/后备箱、断线重连与服务器重启恢复。座位不持久化，重连从步行开始；不做多座位/多车/车辆战斗/刚体物理。

**状态**：V1 设计待用户确认，确认后进入 V2；生产代码/测试未改动。未提交。

## 2026-11-05 M25-T2 V6 文档收口

**范围**：汇总 V1–V5 的范围、实现、测试计数、V5 1280 证据与已知限制；同步 `docs/11`、`docs/10`、`docs/01`、`docs/05`、README 与本文。生产代码与测试未再改动。

**最终基线**：Debug **712/712（132327 assertions）**、Release **712/712（132341 assertions）**、Python **88/88**；V5 烟测存档 `20261105-040025`，重启读档后车辆位置/车况/燃料/后备箱 5 件均恢复。

**已知限制**：单机单车；旧版本不保证读新档；驾驶中存档读档后从步行状态开始；占位美术；无僵尸攻击车辆/碾压/轮胎悬挂/乘客/联机同步。

**状态**：V6 文档已收口；用户以“继续下一步”完成最终验收（2026-11-05），M25-T2 关闭。未提交。

## 2026-11-05 M25-T2 V5 全量测试与实机烟测

**范围**：Release 1280×720 实测自然撞墙、维修、加油、后备箱存入/取出、快速存档、关闭进程、主菜单“继续”读档；未改产品代码。烟测用独立 `save.root` 与临时 Lua 探针（发维修包/燃料罐/样本物品、近车传送、后备箱动作、触发 `Save.QuickSave`）。

**结果/证据**：车辆生成 (82,20)，撞击速度 9.3 扣车况 50.1%，49%（日志 50%）→维修两次→100%（维修包 ×2）；燃料 99%→100%（燃料罐 ×1）；后备箱 0/40 kg→3/40 kg（5 堆），取出全部 5 堆后再存回 5 堆。快速存档 `20261105-040025`，重启“继续”日志 `载具恢复：格 (76.5, 18.7)，燃料 100%，车况 100%，后备箱 5 件`，读档后后备箱 UI 确认 5 堆并可取出。14 张截图与 `phase1.log`/`phase2.log` 位于 `build/m25_t2_vehicle_smoke_v5/`；脚本 `build/m25_t2_v5_smoke.ps1` 输出 `m25-t2-v5-smoke-pass savedLoot=5`。

**验证**：Debug **712/712（132327 assertions）**、Release **712/712（132341 assertions）**、Python **88/88**；日志 `build/m25_t2_vehicle_debug_v5.log`、`build/m25_t2_vehicle_release_v5.log`、`build/m25_t2_vehicle_python_v5.log`。

**限制/下一步**：V6 汇总文档、等待用户确认截图/实机；确认前不进入 M25-T3。未提交。

## 2026-11-05 M25-T2 V4 E 维修/加油、I 后备箱与 HUD

**范围**：`E` 在车辆 `enterRange` 内按车况优先维修/加油，消耗维修包/燃料罐；缺少对应物品时消费本次 E 并提示；`I` 优先打开车辆 40kg/6 堆后备箱，标题“后备箱：皮卡（x/40 kg）”；驾驶中关闭背包并禁止 E/I；HUD 显示燃料/车况、维修/加油/后备箱提示。新增 `PlanVehicleService` 纯逻辑。

**结果/证据**：全量 Debug **712/712（132327 assertions）**、Release **712/712（132341 assertions）**、Python **88/88**；日志 `build/m25_t2_vehicle_debug_v4.log`、`build/m25_t2_vehicle_release_v4.log`、`build/m25_t2_vehicle_python_v4.log`。1280 烟测 `build/m25_t2_vehicle_smoke_v4/`：临时构造车况 50%，HUD/截图 `v4_near_vehicle.png`、`v4_driving.png`、`v4_after_service.png`、`v4_trunk.png`；`E` 维修 50%→90%→100%，`E` 加油 95%→100%，`I` 后备箱标题正确。临时构造代码已还原。

**限制/下一步**：V5 补完整保存/读档、后备箱存取和全量实机烟测；未提交。

## 2026-11-05 M25-T2 V3 车辆持久化与后备箱数据

**范围**：新增 `game/VehicleComponents.h` 的 `ecs::Vehicle{condition,fuel}` 并注册为第 45 个存档组件；车辆实体不再过滤，保存位置/车况/燃料；读档按 `Vehicle` 绑定并补 `SpriteScale`/`Collider`；车辆增加 `Container` 40kg/6 堆 + `PlayerStorage` 后备箱；驾驶中存档路径临时恢复玩家 `SpriteRef`，读档从步行状态开始。`test_entity_save` 增加车辆/后备箱往返，组件数 44→45，相关测试同步更新。

**结果/证据**：全量 Debug **711/711（132322 assertions）**、Release **711/711（132336 assertions）**、Python **88/88**；日志 `build/m25_t2_vehicle_debug_v3.log`、`build/m25_t2_vehicle_release_v3.log`、`build/m25_t2_vehicle_python_v3.log`。停车保存→主菜单继续：`save_root/20261105-014438`，`load_after_continue.png`，日志 `载具恢复：格 (74.5,28.5)`；驾驶中退出自动存档→读档：`drive_exit_root/20261105-014623`，`v3_drive_save_load.png`，日志 `载具恢复：格 (80.3,22.7)，燃料 99%`，玩家精灵可见。

**限制/下一步**：F12 在驾驶中仍被 T1 驾驶输入屏蔽，驾驶存档走 `OnExit` 自动存档；V4 才接 E 维修/加油、I 后备箱命名/HUD。未提交。

## 2026-11-05 M25-T2 V2 纯逻辑、配置与单测

**范围**：`VehicleSpike.h` 新增 `VehicleTuning`、碰撞耐久/`crashLatched`、车况归零禁加速、维修/加油夹取；M25-T1 驾驶参数迁移到 `config/game.json` 的 `vehicle` 段；`WorldDemoState` 读取配置并使用统一 tuning。

**结果/证据**：`test_vehicle` 12/12（58 assertions）；全量 Debug **710/710（132299 assertions）**、Release **710/710（132314 assertions）**、Python **88/88**；日志 `build/m25_t2_vehicle_debug_v2.log`、`build/m25_t2_vehicle_release_v2.log`、`build/m25_t2_vehicle_python_v2.log`。1280 启动烟测确认新配置可加载并生成车辆（格 74,28）。

**限制/下一步**：V2 只接纯逻辑与配置，尚未接 `ecs::Vehicle` 存档、后备箱、E/I 交互和 HUD 车况显示；V3 待开始。未提交。

## 2026-11-05 M25-T2 V1 载具耐久/维修/加油/后备箱文档

**范围**：新增 `docs/11-M25T2载具耐久与后勤.md`，定义单机持久化车辆、碰撞车况、维修包/燃料罐、40kg/6 堆后备箱、E 维修/加油、I 后备箱与 4 张 1280 验收截图。**未写代码**，等待用户确认范围/键位/数值后从 V2 开始。

## 2026-11-05 M25-T1 载具驾驶 Spike（V2–V6 已完成）

**范围**：新增 `game/VehicleSpike.h` 纯逻辑（加速/制动/燃料/8 点圆形碰撞/轴滑动/3×3 生成搜索）与 7 个单测；`WorldDemoState` 单机生成一辆占位皮卡，`Enter` 上下车、`WASD` 屏幕方向驾驶、玩家精灵隐藏/位置跟随/物理体传送/下车恢复；车辆不写存档、无联机/耐久/座位/储物/僵尸碰撞。`object.vehicle` 复用 `crate_1`，新增运行时 `SpriteScale=1.7`；`Vehicle=ENTER` 写入默认键位与配置；HUD 复用 `actionHint`，不新增字段/RML。

**结果/证据**：Debug 1280 烟测生成于 (74,28)，距玩家 1.92 时上车，驾驶后下车 (76.5,30.5)；Release 1280 烟测生成于 (75,29)，截图 `build/m25_t1_vehicle_smoke/release_near2.png`、`release_driving2.png`、`release_moved2.png`，摘要 `build/m25_t1_vehicle_smoke/SUMMARY.md`。载具单测 7/7（31 assertions）；全量 Debug **705/705（132272 assertions）**、Release **705/705（132286 assertions）**、Python **88/88**；日志 `build/m25_t1_vehicle_debug.log`、`build/m25_t1_vehicle_release.log`、`build/m25_t1_vehicle_python.log`。

**限制/下一步**：占位木箱放大，不是正式车辆美术；车辆无物理体，僵尸可穿过；不存档、不联机、无耐久/储物/加油/音效。用户已确认进入 M25-T2；T2 设计见 `docs/11-M25T2载具耐久与后勤.md`，确认范围后再写代码。未提交。

## 2026-11-05 M24 收口（T6 用户确认）

**范围**：T6 自然试玩由用户实机确认通过（2026-11-05），用户明确不填写 `PLAYTEST.md` 逐段表；按用户意见不将构造烟测冒充自然记录，T6/C6 以用户确认签收。M24-T1–T7 全部完成，M24 收口。用户确认记录见 `build/m24_playtest/USER_CONFIRMATION.md`。

**证据**：构造路线 `m24-t6-route-smoke-pass`（35 击杀/2 分 25 秒/16 张 1280 截图）、4 个主要食物容器实际掉落烟测（单次样本 1.88 日，`build/m24_t6_food_loot/`）、Debug **698/698（132241 assertions）**、Release **698/698（132260 assertions）**、Python **88/88**。逐段耗时、食物/弹药/医疗收支明细未留存，原因是用户明确不要求填写；后续新增内容仍按 DoD 要求记录可复现证据。

## 2026-11-04 M24-T6 食物总量校准

**范围**：T6 首轮平衡后六个新 POI 全清食物期望 4.29 日，超出规划“6 个 POI 全清 1.5~2.5 日”上限。本轮只调 `assets/loot.json` 食品表：超市食品架 rolls 3→1、`canned_soup` 随机权重 3→2；仓库食品区/教堂救济物资 rolls 3→1；旅馆客房 rolls 3→2。单 POI 食物期望降为超市 0.63、旅馆 0.49、警局 0.15、消防站 0、教堂 0.53、仓库 0.70 日，全清合计 **2.50 日**；保底汤/水保留，警察局/仓库弹药与武器/医疗取值不变。

**验证**：`tools/tests/test_m24_balance.py` 新增全清 1.5~2.5 日断言，Python **88/88**；Release 构造路线按新掉落复跑 `m24-t6-route-smoke-pass`（35 击杀/2 分 25 秒/16 张 1280 截图，`build/m24_playtest/`）。Debug 全量 **698/698（132241 assertions）**、Release **698/698（132260 assertions）**、Python **88/88**，日志 `build/m24_t6_food_debug_full.log`、`build/m24_t6_food_release_full.log`、`build/m24_t6_food_python_full.log`。主构造路线只搜任务目标容器；追加 `build/m24_t6_food_loot_smoke.ps1` 实际搜索超市食品架/旅馆客房/教堂救济物资/仓库食品区，4 个容器均非空，单次样本 **1.88 日**（2.63 饥饿单位 / 1.4，期望 2.50），证据 `build/m24_t6_food_loot/SUMMARY.md` 与 4 张 1280 截图；该烟测仍使用 100000 HP/传送/F6，不是自然试玩。默认 100 HP、无探针的 30~60 分钟自然试玩仍需人工；T6/C6 未签收。

## 2026-11-04 M23 间歇网络测试稳定化

**范围**：定位长期偶发的 `M23-F1-T10`（联机远程武器）与 `M23-F2a-T6`（服务器权威觅食）失败并修正测试本身。F1-T10 的偶发失败是目标玩家被传送到射手 0.55 格后物理推挤导致朝向/距离漂移，子弹未命中；现改为 0.8 格并在每次开火前重新对齐目标、清零速度、以当前射手/目标位置计算方向。F2a-T6 的偶发失败是测试在旧规划列表选了节点后传送到节点中心，规划边界移动可能把该节点挤出列表；现改为传送后重选稳定节点，并把“同节点重复采集”改为断言服务器拒绝（耗尽或已不在规划范围）。未改生产逻辑/协议/存档。

**验证**：Release 定向循环 `M23-F1-T10` 50/50、`M23-F2a-T6` 100/100 通过；Debug 全量 **698/698（132242 assertions）**、Release 全量 **698/698（132260 assertions）**、Python **87/87**；全量日志 `build/m24_t7_debug_full_final.log`、`build/m24_t7_release_full_final.log`。T6 自然试玩仍待人工，T6/C6 未签收。

## 2026-11-04 M24-T7 联机、存档与全量回归

**范围**：复用 T2/T3/T4 已落地的服务器权威路径，补 T7 验收：双客户端旅馆后仓搜索/取物同步、双方完成任务且奖励各一次；A 完成空投、B 不重复计数；A 击杀尸潮事件僵尸、B 击杀进度不增加；A/B 重连后事件箱不重复生成、完成态与奖励不重复。空存档完成旅馆任务/搜索 1 只空投箱后重启，POI 容器 `searched`/剩余物品、任务完成态、奖励、空投箱数量/搜索状态与任务进度均恢复，守军不因完成态重刷。

**实现/修复**：内容指纹仍为 v13，KCP 握手对不一致指纹返回 `ContentMismatch`；`SaveWorld` 过滤无效 `Health` 实体，避免守军处于倒地 0.5 秒窗口时负 HP 导致整个世界快照失败。未改协议/存档 schema。

**验证**：Debug **698/698（132242 assertions）**、Release **698/698（132260 assertions）**、Python **87/87**；新增 4 个 T7 用例（双客户端、重启恢复、指纹拒绝、版本锁定）。Release 全量首轮命中既有间歇 `M23-F2a-T6`/`M23-F1-T10`，单跑/复跑通过。T7 完成后复跑 T6 Release 构造路线 `m24-t6-route-smoke-pass`，确认存档过滤与测试改动未破坏村庄→6 POI→撤离链路。T6 默认 100 HP、无探针自然试玩仍待人工，T6/C6 未签收。

## 2026-11-04 M24-T6 平衡与 30~60 分钟试玩

**范围**：新增 `tools/tests/test_m24_balance.py`，按真实物品/掉落/配置计算每 POI 食物期望、警察局/仓库弹药区间、警棍/大锤/警用背心/消防服取舍与医疗保底/权重；校准超市食品架 rolls 4→3、仓库食品区 rolls 4→3 且罐头汤保底 2→1、警察局枪柜 9mm 保底 8→6、仓库贵重品柜霰弹 max 8→6。结果：超市/仓库食物 1.22/1.37 日、警局 9mm 23.31、仓库霰弹 9.33；六个新 POI 全清期望食物 4.29 日（每 POI ≤1.5 日，总量留自然试玩判断）。

**路线/断点**：`build/m24_t6_route_smoke.ps1` 构造路线覆盖村庄电台→超市→警局→旅馆→消防站→教堂→仓库→撤离，完成 3 条支线、中继链、空投事件与消防服奖励；35 击杀、2 分 28 秒、16 张 1280 截图，日志/报告/人工路线在 `build/m24_playtest/`。发现撤离点先结束单机局会跳过 `relay_extract` 的完成判定；`WorldDemoState::RefreshHud` 改为 `UpdatePoiTasks → UpdateMissions → UpdateObjective`，现在先记录 `任务完成：准备撤离（relay_extract）` 再显示“成功撤离”。该修复不改变 HUD 字段或存档格式。

**验证**：Debug **694/694（132084 assertions）**、Release **694/694（132097 assertions）**、Python **87/87**；Release 路线烟测 `m24-t6-route-smoke-pass`。待人工：默认 100 HP、无探针、无 F6 的 30~60 分钟自然试玩，逐段记录耗时/伤害/食物/弹药/医疗与自然随机事件；完成前 T6 不标完成，C6 不签收。

## 2026-11-04 M24-T5 HUD、引导与日志

**实现**：新增 `HudState.eventGuide/eventTone`，`RefreshEventGuide` 选择最近进行中的随机事件，显示事件名、目标进度、指向未搜索事件箱/事件站点的方向距离与奖励；空投目标优先未搜索箱，事件结束后自动隐藏。事件触发、事件箱发现（空投补给箱/感染者藏匿箱）、事件完成分别写入中央 notice 与 `events` 日志；非随机任务完成提示追加首目标叙事。`ui::HudNoticeSuppressed` 在背包/交易/对话打开时抑制 HUD notice，空投箱搜索结果显示在背包状态行，避免遮挡。任务焦点新增 `MissionFocusPriority`/`MissionFocusLess`，按随机事件（当日时限）→主线/支线→每日，再按目标距离、链顺序、ID 排序，`L` 循环与自动选择共用。`PoiGuideText` 覆盖 6 个新 POI 的名称/威胁/方向/距离/任务叙事；native HUD、`core_ui` Lua/Python 与 `Hud.GetState` 同步事件字段。

**验证**：`test_missions` 新增事件指引与焦点排序，`test_poi` 扩展 6 个新 POI 叙事/范围/色调，`test_random_events` 覆盖事件箱名称；`test_ui` 覆盖双 HUD 长事件文本、1280/1920 布局与 notice 抑制规则，`test_script_ui`/`test_python_api` 覆盖 `eventGuide/eventTone`。Debug **694/694（132084 assertions）**、Release **694/694（132097 assertions）**、Python **84/84**。1280 五场景 `build/m24_t5_hud_smoke/`：事件触发通知、事件指引、完成通知、空投箱发现/背包打开抑制、寒潮天气均通过；`build/m24_t3_mission_smoke/` 在 31 任务下复跑通过。烟测为临时配置/传送，不替代 M24-T6 自然试玩。

## 2026-11-04 M24-T4 随机事件与遭遇

**实现**：`config/game.json` 新增 `event_wandering_horde`（第 3 天/0.50/8 杀）、`event_infected_house`（第 2 天/0.55/3 杀+污染箱）、`event_survivor_ambush`（第 4 天/0.40/2 名）、`event_cold_snap`（第 2 天/0.35/回村）；随机事件 4→8、任务 27→31、全局链 15→19。`event_supply_drop` 保持 `loot any×2`，生成 2 个 `event_supply_crate`。新增 `game/RandomEvents.*`：事件站点（尸潮按天轮换公路点、房屋/空投固定点、伏击按玩家位置）、`RandomEventStableId(mission,day,index)`、事件敌人/容器生成与幂等；`RandomEventActor` 记录事件 ID/index 并注册为第 44 个存档组件。单机在 `UpdateMissions` 激活后生成，服务器在 `TickServerMissions` 生成，客户端不本地生成；进度 >0/已完成跳过，跨日清理事件敌人。寒潮用 `WeatherSystem::SetOverride` 覆盖 15 游戏分钟；伏击因聚落 raider AI 只服务守卫战而降级为 Strong 僵尸变体。任务 HUD 对空投优先指向未搜索箱、对事件击杀/任意搜刮指向事件站点。

**验证**：`test_missions` 更新 31 条/19 链/8 随机并检查四事件 minDay/chance/奖励；新增 `test_random_events` 覆盖分类/站点/StableId、空投双箱幂等、旧进度跳过、尸潮/伏击/房屋数量与寒潮无世界内容；`test_weather` 覆盖寒潮覆盖到期恢复；`test_entity_save` 覆盖 `RandomEventActor` 往返，`test_spawn_lod` 覆盖事件敌人不被虚拟化；Python `test_mission_data` 更新 31/19/8 与事件叙事/`extraItem`。Debug **693/693（131995 assertions）**、Release **693/693（132008 assertions）**、Python **84/84**。1280 五场景 `build/m24_t4_events_smoke/`：空投两箱搜刮、尸潮 8 杀、感染房屋 3 杀+搜箱、伏击 2 杀、寒潮天气与回村全部完成，事件名/目标/方向/奖励可见。首轮 Release 全量命中既有 `M23-F2a-T6` 觅食联机间歇失败，单跑/复跑通过。

## 2026-11-04 M24-T3 支线、叙事与中继链

**实现**：`config/game.json` 追加 8 条任务，形成 `super_stock→super_return`、`police_case→police_return`、`motel_guest→motel_return` 三条 POI 支线和 `relay_recover→relay_extract` 中继链；任务 19→27、全局链 11→15、once 链 6→10。目标只复用 `poi/loot/collect/reach`；`relay_recover` 前置 `radio_repair`，要求搜索 `poi_warehouse_relay` 且持有 `relay_part`，`relay_extract` 到达撤离点 `(66.5,28.5,r1.5)`。仓库增加第 4 个中继货架容器 `(109.5,88.5)` 和 `poi_warehouse_relay` 掉落表（保底 `relay_part`×1），掉落表总数 44→45；10 条叙事写入 7 个 POI 的 `task.description` 与 3 条任务目标描述。`MissionGuideText` 增加可选方向提示，`WorldDemoState` 按当前未完成目标解析 POI marker、匹配 loot 表容器、reach 坐标或电台坐标，追加“目标<方位> <距离>格”。

**验证**：`test_missions` 更新 27 条/15 链/8 条新任务前置、奖励与完成判定；`test_entity_save` 新增旧档 `radio_repair` 完成/未完成两种 MissionLog 存档往返；`test_poi`/`test_loot`/Python `test_poi_data` 覆盖仓库第 4 容器与中继表；Python `test_mission_data` 更新 27 条/10 once 链并校验叙事、目标、奖励与撤离点可达性；`test_mod_content` 基线 27→28。Debug **686/686（131858 assertions）**、Release **686/686（131872 assertions）**、Python **84/84**。1280 任务 HUD 烟测 `build/m24_t3_mission_smoke/` 显示超市“目标西北 27格”和警局“目标东北 23格”；超市支线端到端 `build/m24_t3_sidequest_smoke/` 完成 `super_stock`、接取并完成 `super_return`，奖励经验与罐头汤/方便面实际入包。首轮全量 Debug 命中既有 `M23-F2a-T6` 觅食联机间歇失败，单跑及复跑通过。

## 2026-11-04 M24-T1 内容密度包基座

**实现**：`assets/items.json` 增加 20 件物品（警棍、大锤、警用防弹背心、消防防护服、工具箱、缝纫包、罐头汤、方便面、水果罐头、蛋白棒、急救包、草药膏、胡萝卜种子/产物、鸡肉/山羊肉、中继零件）；`assets/recipes.json` 增加 10 个配方（烤/炖胡萝卜、烤/炖鸡肉、烤/炖山羊肉、急救包、草药膏、大锤、消防防护服）；`assets/crops.json` 增加胡萝卜（5 作物）；`assets/animals.json` 增加鸡/山羊（5 动物）及 `animal_chicken` / `animal_goat` 掉落表；新增鸡/山羊占位轮廓（鸡冠/山羊角）和体型缩放，复用 `corpse.placeholder`。新增 M24 物品、配方、作物、动物与联机掉落断言，更新受影响硬编码。

**验证**：`test_items`/`test_recipes`/`test_crops`/`test_animals`/`test_art_slice`/`test_crop_data`/`test_f5_balance`/`test_animal_data` 通过；`test_network_integration` 动物剥取接受鸡/山羊生肉。Debug **682/682（130899 assertions）**、Release **682/682（130913 assertions）**、Python **81/81**；内容指纹版本仍为 v13（未改 schema），数据变化会改变实际指纹。

## 2026-11-04 M24-T2 六个新 POI 与地图 v5

**实现**：`tools/gen_map.py` 升至 v5，新增超市 `(40,6,12,8)`、警察局 `(86,6,11,9)`、消防站 `(48,34,11,9)`、汽车旅馆 `(4,36,12,8)`、教堂 `(50,104,10,12)`、仓库 `(100,84,12,10)`，复用现有墙/门/家具图集并避开村庄/道路/池塘/旧 POI。`config/game.json` 的 `pois.defs` 扩到 11 个，新 POI 各 3 个容器、独立目标表与任务奖励；超市/旅馆 threat 1，警察局/消防站/教堂 threat 2 且各 1 重甲，仓库 threat 3/8 strong/2 重甲。`assets/loot.json` 新增 18 张 POI 表和 `event_supply_crate`/`event_infected_cache` 两张事件表（T4 再接任务）。重新生成 `village_demo`：134 个非空 chunk、814 对象、48 房间出现；两次临时生成 135 个文件 SHA-256 全一致，旧 chunk 变化仅在 6 个新 POI 清场/入口 apron 范围。

**验证**：`test_poi_data` 扩到 8 用例（11 POI/版本 5/ID/3 容器/威胁梯度/18+2 表/BFS 路由）；`test_poi` 新增新 POI 契约，`test_loot` 新增 18+2 表引用完整性，`test_maploader` 更新 134/814/48 基线。Debug **684/684（131709 assertions）**、Release **684/684（131723 assertions）**、Python **83/83**。首轮 Debug/Release 全量曾命中既有 `M23-F1-T10` 联机远程武器间歇失败，单跑与复跑均通过；不影响上述最终计数。1280 构造烟测已传送至超市/仓库并截图（`build/m24_t2_poi_smoke/`），日志确认 11 POI、两处 3 容器、超市 5 堆/9 件与仓库 4 堆/36 件掉落；村庄步行可达由 BFS 覆盖。

## 2026-11-04 尸体/世界容器“全部取出”

**问题**：尸体容器原来只能逐堆拖拽或右键拾取，用户容易只看到“已剥取”提示而没有把战利品放进背包。

**修改**：新增 `game::TakeAllLootContainerItems`（按背包容量循环取堆，保留剩余）；`InventoryState.lootCanTakeAll` 在单机尸体/世界 `LootContainer` 有物品时为真；背包面板对尸体显示“全部取出”（仓库仍显示存入/取出/整理，尸体只显示取出）；`take_all` 动作携带 `fromEntityId`，单机走尸体/世界容器，联机隐藏该入口。Lua/Python `Inventory.GetState()` 同步暴露 `lootCanTakeAll`。

**验证**：`test_loot` 新增“容器全部取出按背包容量移动并保留剩余”（满包/无空间边界）。H2 单野猪流程剥取 4 堆/8 件后点击“全部取出”，日志 `容器全部取出：8 件，剩余 0 堆`，背包 5 堆、尸体“（空）0 堆”；截图 `build/f2b_h2_takeall/h2l-takeall-1280.png`。Debug **680/680（130560 assertions）**、Release **680/680（130569 assertions）**、Python **81/81**。

## 2026-11-04 H2 真实行走单野猪（构造起点）

**构造**：临时 `animals.json` 只保留农场仓野猪，`maxPerChunk=1`、全局 `maxLive=1`；玩家 100 HP，携带消防斧/厨刀。探针只把起点放在农场外 `(84,56)`，之后不再传送，使用实际 WASD 按视野方向走向野猪。

**结果**：玩家从 `(84,56)` 走到 `(81.8,49.3)`，野猪进入警觉并反击 4 次，玩家 100→60 HP；斧击击杀后按 `E` 剥取实体 40，得到 3 堆/8 件，按 `I` 打开尸体容器确认生野猪肉/兽皮；无战斗传送。截图 `build/f2b_h2_walk_single/h2w-approach-1280.png`、`h2w-counter-1280.png`、`h2w-container-1280.png`，日志同目录 `app.log`。该用例仍使用构造起点，不替代从默认出生点自然行走与多野猪人工复验；未新增代码/测试，Debug **680/130555**、Release **680/130569**、Python **81/81**。

## 2026-11-04 H2 双野猪 100 HP 同场构造

**构造**：临时 `animals.json` 只保留农场仓野猪，`maxPerChunk=2`、全局 `maxLive=2`、`minPlayerDistance=4`；玩家 100 HP，携带消防斧/厨刀。探针先传送到农场仓触发生成，再在 2 秒后传送到野猪旁；战斗与剥取均为实际键鼠输入。

**结果**：两只野猪同时反击，玩家 100→40 HP 后以斧击击杀两只；分别按 `E` 剥取实体 50/51，得到 3 堆/8 件与 2 堆/7 件，容器均正常显示。截图 `build/f2b_h2_multiboar/h2m-counter-1280.png`、`h2m-after-attack-1280.png`、`h2m-harvest-1280.png`，日志同目录 `app.log`。该用例仍是构造生成/传送，不替代自然多野猪与真实行走的人工复验；未新增代码/测试，Debug **680/130555**、Release **680/130569**、Python **81/81**。

## 2026-11-04 H4 服务器重启双客户端回归

**流程**：复制 `build/t8_h4_run`（含已剥取野猪尸体与 H4A/H4B 档案）到 `build/f2b_h4_run`，当前 Release 启动同一个 `server.saveId=20261031-053814`，两个 1280 客户端连接 7810 端口；服务器读档后截图，A/B 分别按 `E` 尝试重复剥取。

**结果**：服务器日志 `已读取存档 '20261031-053814'：实体 88，玩家 0`，随后 `H4A`/`H4B` 均重连，会话=2；截图 `build/f2b_h4_reload/p1-h4-reload-a-1280.png`、`p1-h4-reload-b-1280.png` 仍显示同一具野猪尸体。自动窗口注入的重复 `E` 未触发（PostMessage 焦点/扫描码限制），因此本次只确认重启持久化与双客户端可见性；重复剥取判定仍以原 H4 构造日志（A 剥取 4 堆/11 件、B 重复 E 无第二份）和 T6 网络测试为准。未新增代码/测试，Debug 679/130541、Release 679/130555、Python 81/81。

## 2026-11-04 H2 单野猪 100 HP 低伤害战斗与剥取

**构造**：临时 `animals.json` 只保留农场仓野猪（鹿/兔生成权重 0），`--animals.spawn.maxLive=1 --animals.spawn.minPlayerDistance=4`；玩家 100 HP，携带消防斧/厨刀。探针先把玩家传到农场仓触发生成，再在 2 秒后传到野猪旁；战斗、受击、`E` 剥取和 `I` 容器均为实际游戏输入。

**结果**：野猪反击 2 次，玩家 100→80 HP；斧击击杀后 `E` 剥取 4 堆/8 件（生野猪肉×3、生野猪肉×3、兽皮×1、生野猪肉×1），厨刀磨损 98%，容器标题“尸体：野猪”，提示“剥取了野猪尸体：…（战利品在尸体中，按 I 取走）”。截图 `build/f2b_h2_lowdamage_single/h2l-counter-1280.png`、`h2l-container-1280.png`，日志同目录 `app.log`。`--animals.spawn.logAnimals=true` 现在也输出危险动物（野猪）坐标，便于定位；仅调试日志，不影响生成/战斗。

**验证**：Debug **679/679（130541 assertions）**、Release **679/679（130560 assertions）**、Python **81/81**。该用例仍是“构造生成/传送 + 真实战斗”，不替代自然多野猪与真实行走的人工复验。

## 2026-11-03 H2 战利品存档排查与剥取提示

**反馈**：用户报告“存档后读取，获得的野猪战利品没有保存下来”。

**排查**：用临时观察 mod 重新加载 `build/h2_manual_saves/20261103-140948`，存档含 1 具已剥取野猪尸体，尸体容器仍有 `raw_boar_meat`、`animal_hide`、`bone`；玩家背包 4 件物品中没有野猪战利品，日志也没有任何 `拾取（容器）` 记录。`20261103-135903` 同样把 `animal_hide`/`bone` 留在尸体容器（该次剥取 2 堆/4 件，随机结果未出肉）。结论：战利品本身已存档，剥取只把战利品写入尸体容器，不会自动进背包；需要靠近尸体按 `I` 再取走。

**修改**：剥取完成提示追加“（战利品在尸体中，按 I 取走）”；读档时新增 `读档动物尸体：N 具（已剥取 N），战利品 N 件` 与 `读档玩家背包：N 件` 日志；`test_entity_save` 新增动物尸体容器、`harvested` 状态与玩家背包战利品的存档往返用例。

**验证**：Debug **679/679（130541 assertions）**、Release **679/679（130560 assertions）**、Python **81/81**；未提交。

## 2026-11-03 H3 尸体战利品取入背包后的存档往返

**流程**：复制 H2 构造存档（已剥取野猪尸体 3 堆：骨头×2/生野猪肉×2/兽皮×2），读档后打开尸体容器，把 3 堆全部取入背包（背包 1→4 堆），按 `F12` 快速存档，关闭进程后从主菜单“继续”。

**结果**：重载日志 `读档动物尸体：1 具（已剥取 1），战利品 0 件`、`读档玩家背包：4 件`；镜头中背包仍有厨刀/骨头×2/生野猪肉×2/兽皮×2，尸体显示“尸体：野猪（空）0 堆”，无重复拾取或重复掉落。截图 `build/f2b_h3_loot_roundtrip/h3r-taken-1280.png`、`h3r-reload-1280.png`，日志 `app1.log`/`app2.log`。该用例为 1000 HP 构造存档，仍不替代自然流程/低伤害平衡的人工复验。

## 2026-11-03 M23-F2b H2 动物尸体靠近高亮

**范围**：`src/game/states/WorldDemoState.h/.cpp` 新增 `DrawAnimalCorpseHighlight`。玩家进入 `inventoryRange` 且最近未剥取动物尸体存在时，在 World view 绘制脉冲菱形：野猪橙色、鹿/兔黄色；背包/菜单/角色面板打开时隐藏。只影响表现，不改剥取判定、存档或联机复制。

**验证**：Release 构建通过；`ZomboidLikeTests.exe` **678/678（130537 assertions）**；Release 1280 构造烟测 `build/f2b_h2_corpse_highlight/h2c-corpse-highlight-1280.png`（999 伤害/1000 HP 仅为快速留下尸体）。自然 H2 低伤害复验仍待用户执行。

## 2026-11-03 H1 自然狩猎手动复验（用户确认，无截图）

**范围**：按 `docs/07` P1-H H1 流程，新档自然拾取村庄地面木材、到商人处购买手枪/9mm、装备并换弹后远程击杀动物。用户手动操作后确认功能正常，未提供截图或日志。

**结论**：H1 自然流程验收通过；口头确认不替代截图证据，自动观察截图仍见 `build/p1-h1-hunt-deer-1280.png` 与 `build/f2b_h1_natural_hunt/`。H2–H4 自然流程/低伤害平衡仍待复验。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。未提交。

**自动化补充**：`build/f2b_h1_natural_ranged_recon.ps1` 此前因 PowerShell 5.1 按 ANSI 读取无 BOM 脚本，Lua 中的“木材”比较串变成乱码，且 `E` 先被求助者对话拦截。改用 `\u{6728}\u{6750}` 比较并从商人侧接近（路点 `75.9,26.9`）后，Release 1280 自动通过：`拾取（地面）：木材 x12`、`商人交易：pistol`、`商人交易：ammo_9mm`、`装备：手枪`、`换弹完成：手枪，装填 8 发，剩余弹药 4`；截图 `build/f2b_h1_natural_ranged_recon/h1r-recon-wood-1280.png`、`h1r-recon-trade-1280.png`、`h1r-recon-equipped-1280.png`，日志同目录 `app.log`。脚本只到换弹，未自动击杀，H1 自然击杀仍以用户手动确认为准。

## 2026-10-16 M0 环境与工程骨架

**完成**
- 工具链：VS2022 Community（MSVC 14.44）+ CMake 4.4.3 + Ninja 1.13.2 + Git 2.56 + vcpkg（`E:\Dev\vcpkg`，`vcpkg.json` 锁定 baseline `3cd21e48`）。
- 依赖（vcpkg manifest，21 包）：bgfx 1.129 / bullet3 3.25 / entt 3.16 / lua 5.5.1 / sol2 3.5 / spdlog 1.17 / glfw3 3.5.1 / glm / stb / nlohmann-json / doctest / sqlite3 / miniaudio；Python 后端走 `python` feature（默认 OFF）。
- 工程：`CMakePresets.json`（x64-debug / x64-release）、根 `CMakeLists.txt`、`ZL_ENABLE_PYTHON` 开关、目录树（src/15 模块 + assets/scripts/mods/shaders/tests）。
- 代码：GLFW 窗口（关闭/缩放/按键回调）→ bgfx（D3D11 后端，vsync 清屏 + debug 文本 FPS/draw）→ spdlog 日志（控制台彩色 + `logs/` 轮转，`ZL_INFO/WARN/ERROR/ASSERT` 宏，启动打印版本与构建时间）。
- 验证：`.\tools\dev.ps1 cmake --preset x64-debug` → `cmake --build` → 运行 6 秒：窗口存活、FPS 60、draw 1、中文日志正常、taskkill 优雅退出。

**踩坑**
1. **CMake+Ninja 找不到 MSVC**：普通 PowerShell 里 PATH 有 LLVM clang，CMake 会误选它（lld-link 缺 Windows SDK 库直接失败）；即使指定 `cl` 也因不在 PATH 而报错。Ninja 生成器不会自动做 VS 环境探测 → 新增 `tools/dev.ps1`：vswhere 定位 VS → `Enter-VsDevShell`（x64）→ 兜底设置 `VCPKG_ROOT` → 执行传入命令；preset 里 `CMAKE_CXX_COMPILER=cl` 强制 MSVC（与 vcpkg 依赖 ABI 一致）。README「快速开始」已同步。
2. **PS 5.1 脚本编码**：无 BOM 的 UTF-8 .ps1 被按 GBK（ANSI）解析，中文注释乱码报语法错 → 项目脚本一律纯 ASCII。
3. **spdlog 缓冲**：强杀进程时文件日志为空 → 调试构建 `flush_on(debug)`；另 `SetConsoleOutputCP(CP_UTF8)` 解决中文控制台乱码。
4. **日志宏参数顺序**：`"[{}] " __VA_ARGS__, module` 的拼法会把 module 放到参数列表末尾，占位符错位 → 改为 `(module, fmt, ...)` + `"[{}] " fmt, module __VA_OPT__(, __VA_ARGS__)` 形式（ZL_ASSERT 用 `__VA_OPT__(__VA_ARGS__ " | ")` 把用户消息拼在前、固定占位符在后）。
5. **vcpkg 首次安装**：约 8 分钟（bullet3 3 分钟最慢），已入本机二进制缓存。

**遗留 / 下阶段风险**
- M0 验收第 3 条「换台机器跑通」未验证（需要外部机器）。
- M1 起进入核心框架（GameLoop 固定 tick / GameTime / TaskScheduler / EventBus / Config / Input / Window 封装），先用 SandboxState 占位。

---

## 2026-10-16 M1 核心框架

**完成**
- `GameLoop`：累加器固定 60Hz tick、倍速（0x~3x）、单帧 tick 上限防死亡螺旋、FPS/TPS 统计、帧超预算告警（1s 节流）。
- `GameTime`：年月日时分日历 + tick→游戏分钟换算（默认现实 60 分钟 = 游戏 1 天）、倍速档位（暂停/1x/2x/3x）、白天/夜晚判定。
- `TaskScheduler`：线程池（默认 核数-2，本机 14 线程，Windows `SetThreadDescription` 命名）；`SubmitResult` 产出进结果队列，主线程 `ProcessResults` 按 FIFO 消费。
- `EventBus`：字符串键 + `std::variant` 载荷；同步 `Emit` 与排队 `EmitQueued`（tick 末 flush）；优先级 + 派发中安全（退）订阅；事件名对齐 docs/03 §5（OnGameStart/OnTick）。
- `Config`：`config/game.json` + 代码内默认值 + 命令行 `--a.b=value` 覆盖 + `Reload()` 热重载。
- `Input` + `Window` + `KeyNames`：GLFW 状态轮询 → Action 映射（配置可改键位）、Pressed/Down/Released 三态、鼠标位置/滚轮；窗口回调接 bgfx reset。
- `GameApp` + `AppState/AppStateMachine`（延迟切换）+ `SandboxState` 占位场景 + `EventProbe` 解耦演示模块。
- 验证（日志证据）：F2→2x、F1 暂停（暂停期间零 tick）/恢复；改 `config/game.json` 键位重启生效（F4）；运行中改配置按 F5 热重载立即生效；100 个任务 240ms 内全部回投、主线程按序消费；EventProbe 跨模块收到按键/倍速/tick 事件。

**踩坑**
1. `glfwGetKey` 轮询范围从 0 开始会刷 "Invalid key" 错误（GLFW 有效键码从 32 起）→ 从 `GLFW_KEY_SPACE` 轮询；修复前单次运行日志 2 万行。
2. 事件宏/日志参数顺序：见 M0 踩坑 4（本次 `fmt` 编译期检查在类型未定义时给出误导性的 `C7595` 报错，根因还是头文件未包含具体类型）。
3. SendKeys 模拟按键依赖窗口焦点、不可靠；改用 `PostMessage` 直投 WM_KEYDOWN/UP 验证输入链路（VK code + scancode 进 lParam）。

**下阶段风险（M2）**
- bgfx 的 shaderc/texturec 工具链要单独搞定（vcpkg 的 bgfx 不含 tools；预编译 shader 入库策略见 docs/02 §2.1）。
- 等距排序与 IsoMath 需要第一批 doctest 单元测试；先建 tests 目标。

---

## 2026-10-16 M2 渲染地基

**完成**
- **工具链**：vcpkg 的 `bgfx[tools]` 以 host 依赖装入（`{"name":"bgfx","features":["tools"],"host":true}`，装出 shaderc/bin2c/texturec）；`tools/build_shaders.ps1` 一键编译 `shaders/*.sc` → `src/render/shaders_generated/*.bin.h`（dx11 s_5_0 + glsl120 双变体，bin2c 数组直接编进 exe，shader 产物入库）。
- **render 模块**：`Renderer`（bgfx 初始化/View 0/1/2/视口/统计）、`Texture2D`+`TextureManager`（stb_image，POINT+CLAMP）、`Atlas`+`AtlasManager`（pz-atlas-v1 解析，uv 预计算）、`SpriteBatch`（顶点色 tint/alpha、按纹理分批、调试线/矩形）、`Camera`（WASD/右键拖拽平移、0.5x~3x 挡位、边界夹取）、`IsoMath`（toScreen/fromScreen/PickCell/SpriteDrawRect）。
- **游戏侧**：`AssetProvider`（逻辑名→图集帧，provider 切换 + own 回退）、`assets/asset_manifest.json`、`tools/gen_placeholder_atlas.py` 自制占位图集（与 pz-atlas-v1 同格式）、`RenderDemoState`（32×32 随机地图 + 树 + 草丛叠加 + 画家算法排序 + 网格/坐标轴/悬停高亮 + F9 开关）。
- **验收**：IsoMath 测试 8 用例 17820 断言全绿；悬停高亮与拾取反解一致（日志链路 + 截图）；128×128 地图 0.5x 缩放 13063 精灵同屏 60fps（Release，drawcall 2）；placeholder_pz 与 own 两套资源零代码切换。

**绘制对齐约定（画布枢轴规则，实测校准）**
- `toScreen(x,y,z) = ((x−y)*32, (x+y)*16 − z*96)`，是格菱形的**中心**（= 地面接触点 = 拾取点）。
- 精灵绘制：`drawTL = toScreen − (fx/2, fy − 16) + (ox, oy)`（即画布枢轴 (fx/2, fy−16) 对准格中心）；对 PZ 64×128 地面块自动得到「菱形中心重合格中心」；对 192×256 JUMBO 树得到「树干底部 ≈ 格中心+14px」，与 PZ 观感一致。
- 所有换算走 `IsoMath`，禁止手写公式。

**踩坑（重要，全是真 bug）**
1. **PIL 多边形光栅化排除了右/下边界像素** → 自制菱形地面块边缘缺 1px，贴图间出现暗缝。修复：遮罩按"像素中心在理想菱形内"精确判定生成（可证明无缝平铺）。
2. **相机偏移被应用两次**：`ViewMatrix` 平移 −center 的同时 `ProjMatrix` 又以 center 为中心做 ortho。世界是周期性格子（368px 偏移=23×16px 网格周期的整数倍），肉眼几乎不可见，但悬停高亮被推出屏幕才暴露。修复：ortho 以相机为原点；教训——**周期图形会掩盖位置 bug，必须用非周期标定物（高亮/坐标轴）验证**。
3. **同一帧对同一动态 VB 多次 `bgfx::update` 语义不可靠**（多图集交错时 896 次/帧 → 世界整体消失）。修复：`SpriteBatch` 改为 **transient 顶点/索引缓冲**（bgfx 专为一帧多次小提交设计）；单图集场景本来就 1~2 批，不受影响。
4. **PZ 图集里有 1×1 占位子图**（如 `floors_exterior_natural_01_3`）：pack 用它表示"该变体缺失"。manifest 要挑 `w/h > 8` 的真实帧，否则随机抽到占位变体的格子会变透明黑洞。
5. `bgfx::Stats::cpuTimeFrame` 单位是 QPC 计数（不是微秒，也不是 `gpuTimerFreq` 计数，两者不同源）；FPS 用 `bx::getHPFrequency()/cpuTimeFrame`。

**已知问题 / 下阶段（M3）**
- 多图集交错时 drawcall 爆炸（placeholder_pz：地皮 Tiles1x0 + 草丛 Erosion2 + 树 JumboTrees1x0 三图集按画家顺序交错 → ~810 批）。M3-T5 按 chunk 收集 + PZ 式"状态桶/相邻合并"压回 <100；自测数据已记（own 单图集 = 2 批）。
- Debug 构建在 13k 精灵时偏慢，性能验收统一用 Release 构建。

---

## 2026-10-16 M3-T1 世界数据结构定稿

**结构（src/world/）**
- `WorldCoords.h`：`FloorDiv/FloorMod` + `CellOf/ChunkOf/SquareLocalOf/ChunkLocalOf` 坐标分解唯一入口（支持负坐标；恒等式有属性测试）。
- `GridSquare`：x/y/z + 5 个分层对象数组（Floor/Wall/Furniture/Item/Mark，枚举值即绘制顺序）；`RoomId`（-1=室外）；地面便捷接口（`SetFloor` 覆盖语义，供 chunk 重载）。
- `Chunk`：10×10×kMaxZ 柱块（800 格，`[z][ly][lx]` 布局与"逐层收集渲染"遍历一致）；状态机 `Unloaded→Loading→Loaded→Unloading→Unloaded`（另允许 `Loading→Unloaded` 解析失败回退）；**不变量：进入 Unloaded 自动清空数据**（chunk 复用池安全）。
- `Cell`：30×30 chunk 槽位稀疏（unique_ptr 按需创建）；世界格坐标直达查询。
- `World`：uint64 打包 (cx,cy) 的 cell 稀疏哈希网格 + `RemoveEmptyCells()` 回收；正负坐标区域都支持。

**决策**
- **一个 (cx,cy) 一个 Chunk（含全部 z 层）**：状态机/流式/存档单位与 z 解耦，渲染时逐层遍历；z 维并入 chunk 内部数组。
- 坐标分解只允许走 WorldCoords：C++ 整数除法朝零截断，`-1/10 == 0` 对坐标分解是错的（应为 -1），这个坑用 FloorDiv 一次性根治。

**顺手修掉的基础设施坑**
- `ZL_ASSERT` 宏此前从未被实例化过（M1 写下后没人用）：`__VA_OPT__(__VA_ARGS__ " | ")` 会把格式参数粘进字符串字面量导致编译错误；改为 `" | " __VA_ARGS__` 前缀拼接。
- CMakePresets 缺 `testPresets`：docs/02 §8 约定的 `ctest --preset x64-debug` 之前不可用，已补 debug/release 两个测试预设。

**测试**：`tests/test_world.cpp` 8 用例（含 6 万次坐标往返属性检查）；全量 16 用例 / 115509 断言全绿（`ctest --preset x64-debug`）。

---

## 2026-10-16 M3-T2 地图格式 v1 + 生成器

**格式定稿（docs/mapformat.md）**
- 目录：`assets/maps/<name>/map.json`（元数据/legend/bounds）+ `chunks/c<cell>/chunk_<cx>_<cy>.json`；
- **稀疏设计**：缺失 chunk 文件 = 默认地形 + 无对象（流式加载的快速路径）；`floors` 用 legend 字符 10×10 网格；`objects`/`rooms` 稀疏列表；坐标一律 chunk 局部 `[0,10)`；`cx/cy` 与文件名一致性做加载门禁；房间跨 chunk 时在相交的每个文件中出现（id 相同）。

**生成器 `tools/gen_map.py`**（纯 Python、无三方依赖）
- 内容：泥土补丁 + 横竖路 + 村庄（砖墙/木地板/南侧门/房间）+ 森林斑块/散布树 + 草丛；
- **确定性**：同 seed + 同参数 → 逐字节相同输出（132 文件 md5 全量对比验证通过）；
- `village_demo` 12×12 chunks（131/144 落盘）已入库；`stress_100x100` 100×100 chunks（3693/10000 落盘，~1.2 万对象）供 T4/T7 压测，gitignore 可重建。

**占位资源扩展**
- `gen_placeholder_atlas.py` 新增 `floor_wood_1/2`、`wall_brick_1`、`wall_door_1`；
- 墙的画布约定（已写进 `test_isomath` 锁定）：画布 64×224、内容 64×112，底带 = 格中心 +16、顶 = 一格楼高（`kFloorPx`）——墙对象放在所属地面格（z=0），精灵自然向上遮挡一格；
- manifest 增加 `tile.floor.wood` / `wall.brick` / `wall.door`（仅 own；`placeholder_pz` 下自动回退 own，验收时可见两套混合渲染）。

---

## 2026-10-16 M3-T3 / M3-T4 地图加载与流式 chunk

**M3-T3 MapLoader（src/world/ChunkData + MapLoader）**
- `ChunkData` = 与运行时 `Chunk` 分离的纯数据（后台产出 → 主线程 `ApplyChunkData` 挂接，搬移字符串零拷贝挂接）；
- 冒烟过程中发现并修掉生成器 bug：**房间矩形跨 chunk 未裁剪**（loader 严格校验 chunk 内矩形），改为生成器按 chunk 裁剪（文件自包含），mapformat.md 同步；
- 解析策略：硬错误（打不开/JSON 坏/结构非法/cx 与路径不一致）→ `false + error`；条目级问题（越界/未知层/未知 legend 字符）→ warn + 跳过或取默认；
- 测试覆盖：元数据、稀疏缺失、真实 chunk、ApplyChunkData、全图 131 文件/334 对象一致性、坏文件各路径。

**M3-T4 ChunkManager 流式加载**
- 相机 chunk 差集（Chebyshev 半径，默认 3 → 7×7）；相机未跨 chunk 时空操作；
- 状态机全流程：请求即建 Chunk（Loading 挂世界）→ TaskScheduler 后台解析纯数据 → 主线程 `ProcessResults` 挂接 → Loaded；相机离开 → Loaded 即时卸载（Reset+RemoveChunk+空 cell 回收）；
- **每帧预算**（实测 Debug 下一次性建 25 个 Chunk ≈ 37ms 尖峰）：`Update` 每帧最多派发 4 个、`ProcessResults` 每帧最多挂接 8 个；巡航测试 Update 峰值 < 10ms；
- 取消/重试：解析完成时相机已离开 → 丢弃（totalCancelled）；失败最多 3 次 → give-up（离开范围自动解除）；
- 结果队列自带（非 SubmitResult）：后台任务经 `shared_ptr` 持有队列与作业，manager 先析构不写悬空指针；
- 生命周期 debug 日志齐全（Loading/Loaded+耗时/取消/卸载），T7 用。
- 测试 4 用例：7×7 窗口全挂接+数据抽查；远跳驻留恒定（49 chunk/1 cell）；高速巡航 61 步不阻塞主线程且终态恰 25 chunk；坏文件重试放弃不崩溃。

---

## 2026-10-16 M3-T5 世界渲染接入

**WorldDemoState（config `game.startState=world` 为默认）**
- 每帧：相机中心 → 世界格坐标 → `ChunkManager.Update/ProcessResults` → 可见范围四角反解 → chunk 网格 → **只读 Loaded chunk** 逐格收集（全 SquareLayer）→ 排序 → SpriteBatch；
- **两段式收集**（对齐 PZ SpriteRenderer）：stage0 = 地面批（地面块之间互不遮挡，按纹理 1~3 个 drawcall）；stage1 = 其余层全局画家序（墙/树/草互相遮挡必须保序）；
- 变体选取：`HashPick(x,y,layer)` 稳定哈希（同格每帧同变体，不闪烁）；
- 悬停拾取：新增 `IsoMath::PickCellUnbounded`（无限世界 hit 恒真，不再受 map 尺寸限制）；
- HUD：实例/四边形/批次/drawcall、chunk 驻留/加载中/可见/缺失、累计加载/卸载/取消；日志每 3s 一条世界统计。

**实测（Debug，1280×720，相机对准北侧村庄，半径 3）**
- `own` provider：49 chunk 驻留、实例 4998、四边形 2098、**drawcall 3**、59fps；
- `placeholder_pz`：真实 PZ 地面 + own 墙自动回退混合渲染正常，**drawcall 244**（Tiles1x 地面 + Erosion 草丛 + JumboTrees + own 墙四来源按画家序交错）——记录进 T7 baseline；优化候选：运行期图集合并（PZ 的 sprite pages 思路）/ chunk 级状态桶；
- 室内被外墙遮挡是 PZ 的正常行为（看室内靠上层透明度/剖切，T6 处理）。

---

## 2026-10-16 M3-T6 多 z 层绘制

**数据（格式 v1 无需扩展，用既有字段表达）**
- 楼上地面 = `objects` 里 `layer:"floor"` + `z=1`（`floors` 网格只覆盖 z=0）；楼上墙 = `z=1` 的 `wall`；
- 房间矩形带可选 `z`（楼上房间 z=1，RoomId 写到 z=1 的格）；
- 生成器：每村第一栋房子变两层小楼（楼上=同 footprint 砖墙+木地板+楼上房间）；**楼上层不消耗 rng**
  （复用地面门位置）——保证加层不改变其余布局，已有测试期望只按增量更新；
- 顺手修掉生成器命名 bug：房名此前绑定 room id（楼上额外占 id 导致名字错位），改为独立房号计数。

**渲染（WorldDemoState）**
- 逐 z 收集（z=0..kMaxZ-1，空层快速跳过）；两段式的"地面批"限定为 **z=0 的 Floor 层**，z≥1 一切内容
  进全局画家序；
- **上层透明度**：z≥1 精灵统一 35% 不透明（`kUpperZColor`），楼上呈现"幽灵层"能看到室内；
  正式方案（楼层剖切/室内模式）留后续；
- 跨层排序规则写入 docs/00 §5.3（z 优先、前景大树被上层轻覆的已知损耗）。

**实测**：默认相机下两层小楼（house_1，chunk 7_2）呈半透明上层；实例 4998→5034、drawcall 仍 3、60fps。
测试：新增 z=1 往返用例（解析 14 面楼上墙/16 块楼上地板/楼上房间 → 挂接进 z=1 格）；全量 28 用例绿。

---

## 2026-10-16 M3-T7 基线压测

**新增能力**
- `ChunkManager::LoadMsPercentile`（解析耗时分位数）；WorldDemoState 自动巡航（`world.autoCruise`，
  圆心/半径/速度/时长可配，F8 手动开关）+ 每 5s 一条 perf 日志（FPS/实例/P50/P95/max/工作集，psapi 采样）。

**巡航结果（stress_100x100，见 docs/perf-baseline.md）**
- Debug 5min：7253 块加载 / 7204 卸载 / 0 取消 0 失败 / 驻留恒定 49；解析 P95 0.84ms；FPS 中位 60（突发时 38~42）；
- Release 1min：P95 0.37ms、max 0.89ms；FPS 中位 60 / P5 58；工作集 138.2→139.0MB（+0.6MB，基本持平）。

**压测顺手抓出的大 bug（ChunkFilePath 的 cell 目录解析）**
- 现象：stress 巡航实例数恒等于"49×100 地面"，一棵树都没有——数据太整齐反而可疑；
- 定位：`ChunkFilePath` 用 `chunkX - 局部坐标` 当 cell 目录名，得到的是 cell 的**原点 chunk**（30）
  而不是 cell **序号**（1）→ `c1_1` 被写成 `c30_30`，**cell 0 以外所有非默认 chunk 文件静默降级成稀疏默认**；
- 为什么之前没暴露：village_demo 全在 cell 0（原点=0=序号），单测也只覆盖 cell 0；
- 修复：目录名用 `FloorDiv(chunkX, kChunksPerCell)`（负数同语义）；补测试：跨 cell（52,50)→c1_1）、
  边界（30,0)→c1_0、负数（-1,-1)→c-1_-1、(‑31,‑30)→c-2_-1，以及**真实文件加载回归用例**。

**M3 验收对照（docs/01 M3 验收四条全过）**
- 高速移动不卡顿：T4 巡航测试 Update 峰值 <10ms + 5min 巡航 0 取消 0 失败、日志齐全；
- 内存回落 ±20%：驻留恒定、Release +0.6MB/60s；
- 篡改/缺失地图不崩溃：坏 chunk 3 次重试后放弃记 error（单测覆盖）；稀疏缺失 = 默认地形是正常路径；
- MapLoader 单测全绿（31 用例 / 11.6 万断言）。

---

## 2026-10-16 M4-T1 ECS 骨架（EnTT 接入）

**新增**
- src/ecs/EcsWorld（registry 封装）：Create（自动附着 StableId + 双向映射）、QueueCreate（延迟创建）、
  Destroy（**一律延迟**，无效句柄/重复销毁安全）、Flush（先销毁后创建）、Update（flush → 系统链 → flush）；
- src/ecs/SystemScheduler：**注册顺序 = 执行顺序**、SetEnabled 调试开关、每系统 lastMs/avgMs 耗时统计；
- src/ecs/components/CommonComponents.h：公共组件定稿 Position / Velocity / Collider / SpriteRef / StableId；
  HashName（FNV-1a 32）把逻辑资源名压成稳定 id，SpriteRef 只存哈希（M4-T2 在 AssetProvider 侧建反查表）；
  碰撞分类位 kColliderStatic/Character/Item/Projectile（M4-T3 对齐 Bullet）；
- 接入：GameContext 增加 ecs::EcsWorld*，GameApp::Tick 内 m_ecs.Update(dt)（系统链），
  状态逻辑后再 m_ecs.Flush()（状态里入队的创建/销毁也 tick 末统一回收）。

**踩坑 / 决策**
- EnTT 3.16 的
egistry **没有 size()**（大量旧教程写
egistry.size() 会编译失败）→ AliveCount 自维护 O(1) 计数；
- Destroy 语义定为：Alive() **立即为假**（已入队），但内存延迟回收（本 tick 内
egistry.valid() 仍真，
  别的系统读到的是本帧最后状态）——测试锁定这个边界，提醒后续系统一律用 Alive() 判断；
- 固定更新链（docs/00 §6.1：Input→Ai→PathFollow→Movement→PhysicsSync→Perception→Combat→SpawnLod→RenderCollect）
  在 GameApp::Init 集中注册，T2~T7 逐项填入。

**测试**（tests/test_ecs.cpp，9 用例）：创建/ID 单调唯一/双向查找、延迟销毁、遍历中销毁+延迟创建、
QueueCreate 顺序与放弃、无效句柄与重复销毁、系统内销毁的可见性边界、Scheduler 顺序/开关/耗时统计、
HashName 固定值（与 Python 交叉验证）+组件默认值、**10000 实体×3 轮创建/销毁压力（storage 容量复用不增长）**。
合计 40 用例 / 116705 断言全绿；游戏冒烟启动无错误。

---

## 2026-10-16 M4-T2 空间索引 + 实体渲染收集

**新增**
- `src/ecs/SpatialIndex`：chunk 粒度均匀网格索引——每 tick 增量维护（扫 Position，
  新建入桶/跨 chunk 换桶/销毁清出）；查询为只读回调式（`ForEachInChunks` 闭区间，零分配）；
  z 不参与分桶，楼层/距离过滤由调用方做；
- `src/render/DrawList.h`：`DrawItem`/`HashPick`/`DrawItemLess` 从 WorldDemoState 抽出为公共类型
  （世界对象与实体共用一份列表 + 唯一排序入口）；DrawItem 新增 `tieBreaker`（实体用 StableId 决胜）；
- `src/game/RenderCollectSystem`：渲染帧在场景对象收集后，把可见实体的高度精灵追加进同一份
  DrawItem 列表（`kEntityLayerBase = kSquareLayerCount`，实体排在同格世界层之后）；
  `FillEntityDrawItem` 为纯函数（脚底锚点 = 格中心，z≥1 同世界规则压 35% 透明）便于单测；
- `AssetProvider::FramesByHash`：SpriteRef 只存 `HashName(逻辑名)`，provider 侧建反查表（含碰撞告警）；
- 占位资源：`gen_placeholder_atlas.py` 新增小人帧 `character_placeholder`（64x128，脚底 y=112），
  manifest 新增 `character.placeholder` 逻辑名；
- 演示：WorldDemoState 生成 40 个实体在盒内漂移（**脚手架**，M4-T3 MovementSystem 接管后删除），
  经空间索引 → 收集 → 统一排序渲染；HUD/日志加 ECS/索引计数。

**实测**（village_demo，Debug）：实例 5034→5074（+40 实体）、drawcall 仍 3、FPS 59~61；
实体与树/墙正确互相遮挡（截图 m4_entities.png）；世界统计含 ECS=40 索引=40。

**踩坑**
- **索引清理顺序**：先清已销毁、再扫 Position 会把「已入销毁队列但 registry 仍 valid」的实体
  重新加回桶 → 第二步必须按 `EcsWorld::Alive()` 跳过（实体语义死亡 ≠ registry 无效，测试锁定）；
- ViewRect 边界语义定为 `[min, max+1)` 闭合（含上/右边界格），测试锁定。

**测试**（tests/test_spatialindex.cpp，8 用例）：分桶与边界（10.0 属下一 chunk/负数 FloorDiv/
z 无关）、跨 chunk 换桶、销毁清出（含未 Flush）、范围查询与手工过滤对照、2 万实体全量一致、
DrawItemLess 全序、FillEntityDrawItem 字段、ViewRect 包含性。合计 48 用例 / 116774 断言全绿。

---

## 2026-10-16 M4-T3 移动与物理集成（Bullet）

**新增**
- `physics/PhysicsWorld`（pimpl 封装 `btDiscreteDynamicsWorld`）：坐标=游戏格坐标（z 高度，
  1.0=一层楼），重力 0、角色胶囊 `linearFactor(1,1,0)` 锁平面 + 禁旋转 + CCD 防穿墙；
  静态体按 chunk 批量生成（z=0 层任一 `solid` 对象 → 每格一个半格 Box）与移除；
  `btIDebugDraw` 收集线段 + `Step` 耗时（lastMs/EMA）；
- `ecs::RigidBody` 组件（非拥有 Bullet 句柄）与 `characters::MovementSystem`
  （有物理体：写速度交给 Bullet；无物理体：直接积分）；
- `physics::PhysicsSyncSystem`：Step → 体 transform 回读 Position → 实际速度回写 Velocity；
- `ChunkManager::SetChunkCallbacks`（挂接后 / 卸载前回调，主线程）——GameApp 接线：
  静态体随流式加载/卸载自动生成/移除（幂等）；
- `SquareObject::blocksMovement` + 地图 JSON `"solid"` 字段（生成器给
  wall.brick/wall.door/object.tree 标注；MapLoader 解析、非法类型跳过；mapformat.md 更新）；
- F10 物理线框（WorldDemoState 画到 Debug view）；演示实体加 `RigidBody`，位移交物理。

**实测**（village_demo，Debug）：静态 64 / 动态 40 / step 0.27~0.35ms；实体被墙/树正确阻挡；
village 与 stress 巡航（30s、754 块加载/705 卸载）静态体数稳定=驻留相关（49），无泄漏无错误。

**踩坑（线框三连）**
1. 默认休眠静态体线框是**绿色**（IMGUI 风格默认色）——和草地混在一起看不见 → `setCustomDebugColor`
   （静态=黄，角色=青）；
2. 默认胶囊线框是 **8×8 经纬网格**（单角色 156 条线！40 角色+64 盒 = 7008 条线段）→ 点步进四边形
   直接打爆 SpriteBatch 单帧配额（16384）→ 输出被截断到几乎只剩胶囊；重写 `drawCapsule` 为
   稀疏表示（1 轴线+4 竖线+上下双环 ≈13 条）→ 1288 条线段 / 7910 四边形（配额内）；
3. 线框四边形数还可能超预算 → 可见剔除 + 采样间距 10px + 帧上限 13500 兜底。

**测试**（tests/test_physics.cpp，10 用例）：停位精度（顶墙停在 4.2±0.15）、CCD 90 格/s 不穿墙、
沿墙滑动（x 挡 y 走）、顶墙反向可恢复、批量生成/幂等/卸载/跨 chunk 隔离、线框开关（盒恰 12 条棱）、
Movement 两分支、全链路（Movement+Sync 240 tick 后 Position 停墙前 + 速度回写归零）、
500 静态体+50 动态体步进预算（Debug 宽松上限）、MapLoader solid 解析。
合计 58 用例 / 116821 断言全绿。

---

## 2026-10-16 M4-T4 玩家控制（系统化）

**新增**
- `characters/components/CharacterComponents.h`：`PlayerTag` / `LocalControl`（walkSpeed 4.0 格/s）/
  `MoveIntent`（期望速度，格/秒）/ `Facing`（4 向）/ `AnimState`（frame/framesPerDir=2/fps=8）；
- `characters::InputSystem`：WASD（**屏幕语义**，W=屏幕上）→ 等距矫正（反解 ToScreen 线性部分，
  2:1 投影下屏幕方向 ≠ 格方向）→ 归一后写 MoveIntent；线性映射保方向，屏幕方向不变；
- `characters::AnimationSystem`：速度 → 朝向（屏幕主轴 sx=vx-vy, sy=vx+vy；|sy|≥|sx| 取上/下，
  否则左/右；45° 平局取上下轴，确定性）+ 行走帧推进（时间循环取模防漂移）；
- `MovementSystem` 增加 MoveIntent 消费（意图→Velocity，本 tick 生效；物理/非物理两分支不变）；
- `RenderCollectSystem::PickFrameIndex`（纯函数）：动画实体 index = dir×framesPerDir + frame，
  无动画走 HashPick 变体；帧数保护（模收敛）；
- 图集扩为 8 帧：`character_{down,left,right,up}_{0,1}`（左向手绘、右向镜像；帧序=dir 枚举序，
  dir: down=0,left=1,right=2,up=3，与 Direction/AnimState 对齐）；manifest 同步；
- WorldDemoState：玩家延迟生成（等目标 chunk 加载后螺旋找可走格）、演示实体挂 Facing/AnimState
  随漂移自动朝向动画；相机平移改方向键（PanUp/Down/Left/Right，WASD 归玩家）。

**实测**（village_demo）：玩家生成于 (70,18)（r=0，目标即可走格）；W 按住 2s 位移 6.0 格、
dir=3(Up)、行走帧 frame=1 捕捉成功；D 1.5s 位移 4.2 格 dir=2(Right)；松键后 intent=(0,0)、
位置在 1.5s 间隔的两张 settled 截图中完全一致（无卡键无漂移）；站立帧双腿并拢 vs 行走帧腿错位
（放大裁剪确认）；FPS 59~60、物理 step 0.34ms。

**踩坑**
1. 玩家生成时机：EnsurePlayerSpawned 在首帧跑，螺旋寻格时"未加载=阻挡"，而首帧只有相机外围少量
   chunk 已加载 → 玩家被丢到 25 格外的 (49,-3)（搜索半径 21）。修复：先等**目标格所在 chunk**
   加载完成再开始扫描；
2. **自动化输入坑（重要，供后续冒烟复用）**：PostMessage 发 WM_KEYDOWN/WM_KEYUP 时 lParam 必须带
   scancode，且 WM_KEYUP 必须置 KF_UP(0x8000)/KF_REPEAT(0x4000) 位——GLFW 用
   `HIWORD(lParam) & KF_UP` 判定动作，lParam=0 时 keyup 被当成再次按下 → 按键"卡死"；
   四方向键同时卡住时 MoveIntent 恰好互相抵消（W+D+S+A 向量和为零）→ 曾误判为"已松键"；
   lParam 正确后行为完全正常。另：ImageGrab 抓屏有 ~0.5-1s 滞后，键态断言一律用"松键后 ≥2s 的
   settled 截图"或间隔截图对比。

**测试**（tests/test_player_control.cpp，7 用例）：等距矫正 8 向往返一致 + W/D 具体值、InputSystem
写 MoveIntent（含速度/非玩家实体隔离/松键归零）、Movement 消费意图、朝向 4+4 向判定（含格轴平局）、
动画帧推进/站立复位/朝向保持/framesPerDir=1 不除零、PickFrameIndex（动画/变体/帧数保护）、
全链路（输入→物理→同步→动画：顶墙滑动 200 tick 停在 4.2、行走帧出现过、松键立停 + 站立帧）。
合计 65 用例 / 116886 断言全绿。

---

## 2026-10-16 M4-T5 相机跟随

**新增**
- `game/CameraFollow.h`（纯函数，可单测）：
  * `CameraFollowStep`：指数趋近 `c += (t-c)·(1-e^(-rate·dt))`——帧率无关（严格性质：
    两步 dt == 一步 2dt，指数可乘性），拖帧不追过头；rate<=0 瞬间到位、dt<=0 不动；
  * `MapPixelBounds`：地图格矩形 → 世界像素 AABB（菱形投影四角的紧包围盒）；
- WorldDemoState：
  * 跟随模式（默认，`camera.follow`/`camera.followRate` 可配）：平滑跟随玩家，
    `SetBounds(地图 AABB)` 边缘夹取（相机中心不出图，玩家贴边时人物离开画面中心）；
    刚生成/切回时"直接落位"不缓动（`m_followSnapped`）；
  * 自由相机（F4 切换，开发者模式）：无边界，方向键/右键拖拽平移；切回跟随即时就位；
  * 巡航（F8）与跟随互斥：巡航=无边界，结束/关闭时 `ApplyCameraMode()` 恢复；
    （否则 SetCenter 会被地图边界夹住——老压测的中心 (500,500) 在地图外，会静默失效）
- ChunkManager 暴露 `Map()`（meta 访问器，相机边界用）；绑定 `ToggleFreeCam=F4`。

**实测**（village_demo）：生成即就位 (70.5,18.4)；W 走 1.5s → camGrid 跟随至 (66,14)（chunk 随
相机流式 加载=56/卸载=7）；F4 自由 → 再走 1.5s player=(62.0,9.5) 而 camGrid 保持 (66,13)
（玩家离开画面中心，镜头不动）；F4 切回 → 日志"相机跟随就位：玩家格 (62.0, 9.6)"瞬间回中。
FPS 59~60、物理 step 0.35ms。

**测试**（tests/test_camera_follow.cpp，4 用例）：指数趋近（120 步单调收敛/帧率无关/退化输入）、
MapPixelBounds（四角紧包围盒 + village 120×120 具体值）、Camera::SetBounds 夹取（1.0x/1.5x 换算/
ClearBounds）、跟随集成（目标远在地图外 → 收敛到夹取点 (-3200, 360)）。
合计 69 用例 / 117047 断言全绿。

---

## 2026-10-16 M4-T6 A* 寻路（ECS 化）

**新增**（`src/ai/`）
- `Passability.h/.cpp`：`ChunkPassability`（10×10 位图，z=0 地面层，与物理静态体同源
  `SquareObject::blocksMovement`）+ `PathSnapshot` 只读快照（chunk 矩形 + 行位图；
  未填充 chunk / 区域外一律"阻挡"——未知区不可通行，贴地图边界自然封闭）；
- `AStar.cpp`：8 向 A*——Octile 启发式、**对角 √2 代价**（0.01 精度内实测）、
  **禁止穿角**（对角移动要求两个正交邻格可走）、展开预算（超限 BudgetExceeded）、
  重复节点惰性删除（closed 标记）；起点即使在阻挡格也允许（可走出被堵点），
  目标格阻挡直接 Unreachable。纯函数、无状态、线程安全；
- `PathfindingService`：passability **chunk 级缓存**（随 ChunkManager 回调加载构建/卸载删除，
  即"局部重建"）；`Submit` 主线程立即拷贝快照（起终点包围盒 + 1 chunk 边距、夹取地图范围、
  单轴 64 chunk / 总 2048 chunk 防守上限）→ `TaskScheduler` 后台线程 A* 只读快照；
  结果回投带锁队列（`TryTakeResult` FIFO）。跨线程状态全在共享 `Core`（shared_ptr，
  服务先于线程池析构也安全——在途任务持 Core，结果静默丢弃）。
  `tasks == nullptr` 时降级同步执行（单测用）；
- `components/AiComponents.h`：`PathRequest{goal, newRequest, queryId, state}`、
  `PathFollow{waypoints, index, speed, reachRadius}`、`PathStatus`（None/Pending/Following/
  Arrived/Failed）；
- `systems/PathFollowSystem`（固定链：**Input 之后、Movement 之前**）三阶段：
  1) 消费完成结果（queryId 匹配在途请求，实体已销毁/重发则丢弃）→ Found 写路点进入
  Following（速度 = LocalControl.walkSpeed 或默认 2.0）；失败清路点 + 意图清零；
  2) `newRequest` → Submit 后台查询；
  3) 沿路点写 `MoveIntent`（连续消费已到达路点，到达半径 0.35 格）；
  **手动优先**：本地控制实体本 tick 被 InputSystem 写入非零意图 → 取消路径（输入意图原样保留；
  在途结果按 queryId 失配丢弃）。到达/失败后不再触碰意图（避免覆盖读点击后的键盘输入）。

**接线**：GameApp 系统链改为 `SpatialIndex → Input → PathFollow → Movement → PhysicsSync →
Animation`；chunk 生命周期回调同链追加 `OnChunkLoaded/OnChunkUnloading`（与物理静态体同一回调）；
config `pathfinding.maxExpansions=200000`；GameContext 增 `pathfinding` 指针。
WorldDemoState **删除盒内漂移脚手架**，改为 40 个寻路游荡者：每 5s 给空闲实体
拒绝采样派发随机可走目标（未加载 chunk 按阻挡跳过），HUD 增 `path:` 统计行（查询/失败分解/
平均耗时/位图缓存/跟随数），"世界统计"日志增寻路段。

**测试**（tests/test_pathfinding.cpp，14 用例）：位图分层与 z 语义、快照未填充/区域外语义、
直线与对角 √2 代价、绕墙走缺口、禁止穿角（双角堵死不可达/单角可绕）、目标围死/越界/目标阻挡、
展开预算截断、**2270 格蛇形路径 2.09ms（Debug，验收 <5ms）**、服务缓存+同步模式+卸载失效、
越界即时失败、全链路（请求→A*→路点跟随：不穿墙、穿缺口、到达、意图清零）、不可达→Failed、
手动输入取消寻路、真实线程池异步查询。合计 **83 用例 / 117155 断言全绿**。

**冒烟**（village_demo，20s+）：40 游荡者；首轮 40 个查询全部后台完成（平均 ~1ms，最慢 3.7ms
322 展开）；持续多轮重派 → 最终 查询=78 完成=73 不可达=5（其中 1 个实体因出生在密闭房屋内
反复不可达——物理推出前会持续失败，记录为已知）；FPS 59~60（单帧 20.2ms 尖峰 1 次），
物理 step 0.55ms；HUD `path:` 与到达/失败日志链完整。

**教训**：蛇形性能测试第一版把奇数行连通口设计成"整列 7 格宽"，A* 直接走竖井捷径
（188 格）——测试地理设计错误会让 1000 格验收失真；改为左右交替单格连通口后为 2270 格。

---

## 2026-10-16 M4-T7 点击移动

**新增/行为**
- 左键点击世界 → `PickCellUnbounded` 拾取目标格 → **目标格校验**（顺序）：
  1) 地图范围（chunk bounds 闭区间）外 → 红字"超出地图范围"；
  2) 目标 chunk 未加载（通行未知）→ "目标区域未加载"；
  3) 目标格阻挡 → `ai::FindNearestWalkable`（Chebyshev 环 ≤4，dy/dx 升序确定性）就近校正，
     找不到 → "不可达"红字反馈 + 红标记；
  4) 通过 → 写 `PathRequest{goal, newRequest}`（沿用 T6 的提交/消费链路）。
- **反馈 UX**：目标格菱形标记——黄=寻路中/跟随中（常显）、绿=到达、红=不可达（限时 1.5/3s）；
  HUD 第 11 行限时提示文字（寻路中/已到达/不可达/校正）；`已到达/寻路失败/手动移动取消`
  均由玩家 `PathRequest` 状态跃迁驱动（只对玩家；漫游者不打扰 HUD）。
- **中断/重寻路**：
  - 手动优先扩展到"在途查询"：WASD 输入到达时，无论 Following 还是 Pending 都取消路径
    （在途结果按 queryId 失配丢弃），输入意图原样保留；
  - **卡住自动重寻路**：`PathFollow` 增加进度跟踪（anchorX/Y + stuckTimer），Following 期间
    每 1.2s 采样，窗口内位移 < 0.2 格 → `newRequest=true` 从当前位置重寻路（在途不重复提交）；
  - 点击新目标即重寻路（queryId 换新，旧结果丢弃）；到达后意图清零一次、此后不再触碰
    （不覆盖玩家随后的 WASD）。
- 玩家实体补挂 `PathRequest/PathFollow` 组件（T6 只给漫游者挂了——冒烟踩坑：点击静默无反应，
  `try_get` 为空提前返回；该分支现补 ZL_WARN 防回归）。
- `EnsurePlayerSpawned` 的螺旋找空格改用 `ai::FindNearestWalkable`（与点击校验同一实现）。

**测试**（tests/test_pathfinding.cpp，+2 用例）：FindNearestWalkable（自身命中/九宫格阻挡第二环
确定性首命中/半径不足）；卡住重寻路（原地 1.67s → 恰好触发 1 次重寻路且仍 Following；
持续前进 → 不触发）。合计 **85 用例 / 117166 断言全绿**。

**冒烟**（真实鼠标注入）：点击密闭房内格 (75,15) → `寻路失败 outcome=1` + 红字/红标记截图；
点击 (63,25) → 黄色目标标记 + `寻路到达：稳定 ID 41 终点格 (63, 25)`（HUD player=(63.8,25.4)）；
点击 (58,30) 后按 W → `手动移动取消寻路（稳定 ID 41）`。三条链路截图齐全。

**冒烟工具坑（重要，写入工具经验）**：
1) 点击移动**必须 `SetCursorPos` 移动真实光标**——GLFW 每次 PollEvents 都用 `GetCursorPos`
   覆盖鼠标位置，PostMessage 的 WM_MOUSEMOVE 会被立刻改回（按钮状态不受影响，PostMessage
   WM_LBUTTONDOWN/UP 可用）；
2) 窗口几何读取可能滞后/窗口可能被别的窗口遮挡 → click 前**回读校验**（GetCursorPos +
   ScreenToClient 与目标比对，相对误差校正重试），截图前 `SetWindowPos(HWND_TOPMOST)` 置顶
   （HWND_TOPMOST=-1 必须 `ctypes.c_void_p(-1)` 传参，否则 32 位截断失效）；
3) 漫游者卡在人群/角落会以 1.2s 周期反复"卡住重寻路"（DEBUG 日志节流前的预期行为，
   每次一条查询 ~0.2ms，先接受）。
4) 玩家会被漫游者物理顶开数格（拥挤推挤），点击后相机随玩家移动——冒烟坐标宁可相对
   相机/玩家动态计算或按日志实际目标格解读，不要假设静止。

---

## 2026-10-16 M4 验收（里程碑完成，tag v0.4.0）

两项遗留验收（此前为"预演"级测试，本次转为正式验收并补 Release 数据）：

- **500 静态体物理步进 < 2ms/帧**：`test_physics` 基准（5 chunk 全墙 500 静态体 + 50 随机
  速度角色体，60 tick 平均）——**Release 0.139 ms/帧**、Debug 1.066 ms/帧；断言改为
  Release 严格 <2ms（NDEBUG 分支）/ Debug <20ms 宽松上限，并打印 `[bench]` 行便于记录。
- **10000 实体创建/销毁无泄漏（storage 复用）**：`test_ecs` 3 轮 × 10000 实体（含
  Position/Velocity/Collider/SpriteRef），每轮存活与 StableId 归零、4 种 storage size 归零、
  第 2/3 轮 capacity == 第 1 轮（EnTT 池复用确认）。Debug + Release 双构建全绿。

数据写入 `docs/perf-baseline.md` 新增「M4 验收基线」节（含 M4-T6/T7 寻路数据：2270 格单测
2.09ms、运行时平均 ~0.9ms/查询、demo 59~60fps）。验收清单两项勾选，docs/01 状态表与
README 状态表 M4 → 已完成，打 tag `v0.4.0`。

**M4 阶段小结**：ECS（EnTT）+ SpatialIndex + Bullet 物理 + chunk 级 passability 位图 +
后台 A*（只读快照）+ PathFollow + 点击移动/卡住重寻路全部就位；系统链
Input → PathFollow → Movement → PhysicsSync → Animation 稳定。85 用例 / 117166 断言
（Debug 与 Release 双构建全绿）。

---

## 2026-10-16 M5-T1 角色组件集 + 死亡流程

**组件定稿（characters/components/CharacterComponents.h）**
- `Health{current=100, max=100}`：current<=0 = 死亡（伤害管线 M5-T6 只改数值）；
- `Needs{hunger/thirst/fatigue}`：PZ 风格归一化 [0,1]（0=满足）；衰减/效果接后续里程碑；
- `Corpse{sourceId}`：尸体标记，sourceId = 死者稳定 ID（未来搜刮/存档溯源）；
- `Facing/AnimState` 沿用 M4-T4；`Equipment` 与物品体系强耦合，按 M6-T2 的
  “ItemInstance + Container/Equipment”一并定义（docs/01 M5-T1 行已注明）。

**DeathSystem（characters/systems/DeathSystem.cpp，固定链 PhysicsSync → Death → Animation）**
- 判定收集（view 外再处理）→ 实体带 RigidBody 时**先 DestroyBody**（Bullet 不随实体回收，
  这是 M4 起就存在的泄漏点，死亡是第一条真实销毁路径）→ `Destroy(e)`（延迟）+ 尸体经
  `QueueCreate`（与销毁同 tick 入队、tick 末统一 Flush，沿用 M4-T1 的生成回合制语义）；
- 尸体 = Position（原格 + z 保留，楼上死亡楼上留尸）+ SpriteRef("corpse.placeholder") +
  Corpse：无 Collider/RigidBody/Velocity（不阻挡、不参与物理与积分）；
- 死亡日志在 Flush 时输出：`实体死亡：稳定 ID N（格 (x,y) z=z）→ 尸体（稳定 ID M）`。

**占位资源**：图集新增 `corpse_1/corpse_2`（横躺 + 身下血泊，两方向变体；own 图集 27 帧）；
manifest 注册 `corpse.placeholder`（帧选择走 HashPick(stableId)，尸体无 AnimState/Facing）。

**demo 接入（WorldDemoState）**
- `SpawnCharacter` 挂 Health/Needs（玩家与 40 游荡者同套组件）；HUD player 行加 hp；
- **F6 演示击杀**：最近存活游荡者 HP 归零 → 下一 tick DeathSystem 转尸体（HUD 提示 +
  日志）；玩家死亡时清引用自动重生（尸体留在原地，`EnsurePlayerSpawned` 兜底）。

**踩坑与整改：bgfx debug 字体只支持 ASCII/Latin-1** —— HUD 中文提示（M4-T7 的点击反馈）
实际一直渲染为乱码（冒烟放大截图才发现）。整改：HUD 提示文案全部改英文
（"demo kill: id 19 -> corpse"、"unreachable: target (75,15)" 等），中文只进日志文件。

**测试**：tests/test_death.cpp 新增 4 用例（组件集共存/默认值/平凡可拷贝；死亡流转
——物理体先回收、尸体位置与来源 ID、尸体无碰撞无物理；多死者同 tick、二次运行不重复、
z 保留、稳定 ID 不复用；MovementSystem+PhysicsSync+Death 全链路——致死一击后物理体
归零、尸体留在死亡瞬间位置）。合计 **89 用例 / 117208 断言全绿**。

**冒烟（真实按键 F6）**：村庄内连杀 3（ID 29/35/22 → 尸体 42/43/44，动态体 41→39→38）；
带玩家走到开阔地再杀 2（ID 20/19 → 尸体 42/43），截图确认尸体（含血泊）无遮挡显示、
HUD 提示可读、FPS 60 / drawcall 3 不变。

---

## 2026-10-16 M5-T2 僵尸 AI（AiState + AiSystem 转移表）

**组件（ai/components/AiComponents.h 追加）**
- `AiStatus{Idle,Wander,Chase,SearchLastKnown,Attack,Dead}` + `AiState{status, stateTime,
  actionTimer, idleSeconds, targetId(StableId), lastKnownX/Y, attackCooldown, attackPulses}`；
- 目标信息来源约定：感知系统（T3）/演示桩维护 `targetId`（0=当前未感知），
  `lastKnown` 由 AiSystem 在"目标实体可查到"时每 tick 刷新 —— 目标丢失后 SearchLastKnown
  自然落到最后已知位置。

**AiSystem（ai/systems/AiSystem.cpp，链位 Input → **Ai** → PathFollow → Movement → …）**
- 结构对齐"转移条件集中可读"：先算 per-entity facts（dead / targetVisible / pathBusy /
  targetDist），再进一个 switch —— 每个状态的出口条件一行一条；副作用统一走 `EnterState`
  （要路 / 停步 / 计时重置）。
- 转移表：Idle --时长到--> Wander；Idle/Wander --感知到--> Chase；Wander --路径完成--> Idle；
  Chase --贴身(≤1.0)--> Attack；Chase --目标丢失--> SearchLastKnown；Chase --目标移动>1.2
  或路径空闲且节流到期--> 重寻路（在途查询不重复提交）；SearchLastKnown --再见--> Chase /
  --6s 超时--> Idle（期间每 2s 在最后已知位置 ±4 格挑巡点）；Attack --目标丢失--> Search /
  --拉开>1.5（迟滞）--> Chase（贴身停步 + 朝向目标 + 0.8s 节拍脉冲，M5-T6 伤害挂脉冲）；
  任意状态 --Health<=0--> Dead（冻结，尸体由 DeathSystem 处理）。
- 只操作组件（PathRequest/MoveIntent），游荡/巡点选点用 `FindNearestWalkable` 就近校正，
  可走谓词由 GameApp 注入（"未加载 = 不可走"）；不碰物理/服务 → 可无头单测。
- 随机全部走 StableId+盐 的确定性哈希（splitmix64 尾段），测试可复现。
- Stats：六态分布（每帧快照）+ swings（攻击脉冲累计）；HUD 第 10 行显示。

**工程配套**
- 新增 `game/WorldQueries.h`（FindChunkInWorld / IsSquareBlocked）——WorldDemoState 的
  FindChunk/IsSolidSquare 与 GameApp 的 AI 谓词共用，删掉两处重复实现；
- WorldDemoState：删除 M4-T6 的"定时重派目标"脚手架（RetargetDemoEntities/PickWanderTarget/
  m_demoEntities/m_retargetTimer 全删）；游荡者挂 AiState 由状态机自治；F6 击杀改按
  view<AiState,Health,Position> 找最近；OnTick 只剩 **DemoSightUpdate**（距离 ≤8 格标记
  targetId 的临时感知桩，T3 落地 PerceptionSystem 后删除，代码内已注明）。

**测试**（tests/test_zombie_ai.cpp，+5 用例，无头全链 AiSystem+PathFollowSystem(同步)+Movement）：
Idle→Wander 自主循环（时长随机化/目标可走/走完回 Idle 重新计时）；Chase→Attack→Search→Idle
转移链（含攻击节拍 ≥2 脉冲、走出 keep 距离回 Chase、目标消失去最后已知位置、6s 超时回 Idle）；
Chase 全链真走路（贴到攻击距离 ≤1.6 且不偏航）；目标移动重定向（goal 跟随 lastKnown，
发现的问题：原实现"目标移动但路径在走"时不立即重定向 → 改为 goalStale 时在途也重发）；
Dead 冻结（不再写意图/请求）。合计 **94 用例 / 117253 断言全绿**。

**冒烟（40 僵尸实机）**：玩家出生在村中心 → 8 格内僵尸立即转 Chase、贴身转 Attack
（HUD 抓拍 `idle=27 wander=4 chase=6 attack=3 | swings=33`，尸群物理围堵玩家截图）；
点击远处 (79,9) 拉开距离 → HUD 抓拍 `idle=9 wander=20 chase=10 search=1 | attack=0`（丢失
目标进入 SearchLastKnown），10s 后 `idle=12 wander=18 chase=3 attack=7`（远处巡点超时回收 +
就近新目标再围上来）。FPS 59~61 / drawcall 3 不变。

---

## 2026-10-16 M5-T3 感知系统（视觉 + 听觉 + 警觉衰减）

**组件与类型**
- `ecs::PerceptionState{alertness, lastKnownX/Y, sinceSeen, sinceHeard}`（AiComponents.h）：
  感知系统的输出、AI 的输入。**LastKnownPos 从 AiState 迁到感知侧** —— 谁感知谁维护，
  AiSystem 只读（看见=玩家位置；听见=声源位置；丢失后 SearchLastKnown 围着它巡点）；
- `core::SoundEvent{x, y, radius, type} + SoundType`（core/SoundEvent.h）：事件总线载荷类型
  （M5-T4 投递，本任务先提供消费端 `PerceptionSystem::PushSound`）。

**PerceptionSystem（ai/systems/PerceptionSystem.cpp；链位 Input → **Perception** → Ai → …）**
- 视觉：距离 ≤ sightRadius → （dist ≤ closeRadius 全向 | 与 Facing 四向轴的视锥 fovDegrees）
  → 视线粗判（沿视线每 0.5 格步进查"不可走"，未加载=遮挡，与寻路语义一致）；
  命中 → targetId=玩家 ID、lastKnown=玩家位置、alertness=1、sinceSeen=0；丢失 → targetId=0；
- 听觉：`PushSound` 入队（主线程），Update 时对每个感知者按 dist ≤ radius 判定 →
  alertness += 0.6（clamp 1）、sinceHeard=0；看不见时才把可疑点设为声源（看得见时不覆盖）；
- 衰减：alertness 每秒 -1/alertDecaySeconds（默认 8s 归零）；
- 参数走 config `perception.*`（sightRadius 12 / closeRadius 2.5 / fovDegrees 110 /
  alertDecaySeconds 8）；Stats{perceivers, seeing, alerted} 供 HUD。

**AiSystem 接入**
- 新增听力反应：Idle/Wander 且 `sinceHeard < 1s` → SearchLastKnown（去声源查看），
  优先于 Wander/Idle 计时；
- 删除"AI 自己刷新 lastKnown"的过渡逻辑（改由感知唯一维护）。

**demo 清理**：删除 M5-T2 的 DemoSightUpdate 临时感知桩与 kDemoSightRadius；游荡者挂
PerceptionState（初始可疑点=出生位置）；HUD ai 行追加 `seeing=N alert=N`。

**测试**（tests/test_perception.cpp，+7 用例）：视距/视锥（背后看不见→回到前方又看见）；
近距全向；视线遮挡（加墙→移除墙恢复）；FOV=360° 参数；听觉（半径内外、警觉 0.6、衰减
4s≈0.1、6s 归零）；听见→AI 去声源查看→超时回 Idle；**M5 验收链**：看见→Chase→玩家消失
→SearchLastKnown（目标格=最后看见处）→无果回 Idle。test_zombie_ai 相应改为直接写感知输出
（聚焦 AI 逻辑）。合计 **101 用例 / 117289 断言全绿**。

**冒烟（实机 40 僵尸）**：尸群围堵时 HUD `seeing=6 alert=6 chase=1 attack=5`（同一场景
M5-T2 的 360°/8 格桩会全场 aggro —— 视锥 + 遮挡把仇恨限制在"看得见"的个体）；逃离时
chase↔search 随视线切换，开阔地新尸群重新锁定玩家（截图 `m5t2b_*`）。FPS 59 / drawcall 3。

---

## 2026-10-16 M5-T4 声音事件与传播（事件总线 OnSound + 隔墙衰减 + 开枪引怪）

**事件与载荷**
- `core::SoundEvent` 进 `EventBus::EventPayload` variant（新增 `SoundType{Gunshot,Impact,Generic}` +
  事件键 `kOnSound="OnSound"`，对齐 docs/03 §5 事件表）；发射方只填声源位置 + 基础半径 + 类型；
- 接线（GameApp）：`m_events.Subscribe(kOnSound, …)` → `PerceptionSystem::PushSound`
  （订阅 ID 随 Shutdown 退订，先于感知系统销毁）。发射方（demo/武器）只发事件，
  不依赖感知系统 —— 与 M11-T4 音频系统共用同一事件源。

**传播模型（PerceptionSystem::HearsSound）**
- 距离粗筛（> 基础半径直接不可闻）→ 沿"听者→声源"连线每 0.5 格采样（不含端点，与视线同法）数
  **不同阻挡格** → 有效半径 = 半径 × 0.6^墙数；随数墙即时评估，跌破距离提前判定不可闻；
- "未加载 = 阻挡"语义与寻路/视线一致；无 walkable 查询时降级为纯半径；
- 参数 `perception.wallAttenuation=0.6`（config 可调；M11-T4 音频听觉共用）；
- `Stats.heard` = 累计听见的听者次数（跨帧累计，HUD `heard=`）。

**demo**：F7 开枪 —— 玩家位置发射半径 30 格 Gunshot（`sounds.gunshotRadius` 可配），
HUD 提示 "gunshot! r=30 (walls attenuate)"；GameApp 绑定表补 `DemoGunshot=F7`
（上一版只加 config 忘了 kDefaultBindings，冒烟首跑 F7 无反应——已修）。

**测试**（tests/test_perception.cpp，+3 用例：104 用例 / 117307 断言全绿）
- 隔墙衰减：无墙（5≤10）可闻 → 1 墙有效半径 6 仍可闻 → 2 墙 3.6 不可闻（可疑点不被覆盖）→
  拆墙恢复可闻；事件总线 OnSound 投递（EmitQueued → FlushQueued 语义）→ 感知消费；
- **M5 验收**：枪声（事件总线全链）吸引 15 格外僵尸去声源查看（goal=声源格）；同距离隔 2 墙者
  听不见（30×0.6²=10.8 < 15）保持 Idle。
- 修 bug：HearsSound 首版采样方向写反（`lx - sound.x` 应为 `sound.x - lx`），单测抓住。

**冒烟（实机 40 僵尸，截图 m5t4_*）**：开枪前 `heard=0 idle=15 wander=19 chase=5 search=1`
→ F7 后 `heard=24 alert=12 search=12`（24/40 可闻，其余出界或隔墙衰减；idle/wander 集体转
去声源查看）→ 4s 后僵尸群回扑射击点 `chase=10 search=8 attack=6` → 最终扎堆 `attack=5`。
FPS 59~61 / drawcall 3 不变。

---

## 2026-10-16 M5-T5 群体行为（Boids + LOD 降频 + 虚拟化统计）

**BoidsSystem（ai/systems/BoidsSystem；链位 PathFollow → **Boids** → Movement）**
- 对僵尸的 `MoveIntent`（速度向量）叠加三种转向，再按 `PathFollow.speed`（或默认 2.0）截断：
  分离（<0.8 格推开，精确重叠时按"无序对 id 哈希 + 大小取反"给严格相背的确定性方向）；
  聚合/对齐（≤2.5 格邻域重心/平均速度）**仅作用于移动中实体**（不唤醒站立的僵尸）；
- **邻域查询的关键性能决策**：直接对 chunk 粒度（10 格）空间索引逐候选做 EnTT 组件查询，
  500 僵尸稠密场景候选被放大 ~16×（实测 Debug **242 ms/tick**，占总 99%）→ 改为每 tick
  一次候选快照（位置 + 组件指针）+ "格边长 = neighborRadius"的均匀哈希网格（3×3 格全覆盖，
  纯数组距离运算、桶容量跨帧复用）→ **4.3 ms/tick（Debug）**，降 56×；参数走 config boids.*。
  （类名对齐 docs/02 的 `ai/ BoidsSystem`；初版 FlockSystem 已废弃删除。）

**LOD 降频（ai/systems/LodSystem；链首，仅次空间索引）**
- 按实体到相机焦点距离写 `ecs::LodState{tier, updateThisTick, dtScale}`：
  全速 ≤25 格（每 tick）/ 30Hz ≤60 格（每 2 tick，dtScale=2）/ 10Hz（每 6 tick，dtScale=6）；
- 消费端门控：感知的**视觉**（听觉与警觉衰减不门控——声源队列逐 tick 即弃，门控会漏听）、
  AiSystem 转移与动作、PathFollow 提交/跟随/卡住检测、Boids；
  冻结 tick 的计时以 `dt × dtScale` 补齐（stateTime 与全速语义一致，单测锁定）；
- 计数/统计（状态分布、seeing/alerted）不受门控影响（快照含冻结实体）。

**虚拟化（ai/VirtualHorde.h/.cpp + GameContext/GameApp 接线）**
- `VirtualHorde{id, center, spread, count, speed}` + `VirtualHordeStore`：离屏僵尸只存统计，
  **不入 registry**；聚合推进（朝玩家漂移、驻留距离内停住，O(群落数/tick)）+ `TakeZombies`
  实体化扣减接口（M5-T7 SpawnLodSystem 消费）；
- demo：5 群落 × 40 = 200 虚拟僵尸种子在村庄外圈，HUD 新增 `lod: full/mid/far | virtual:
  hordes/zombies/nearest`（notice 行让位到 12 行）。

**踩坑**
- `int far = 0;` 编译失败——`far`/`near` 是 windef.h 的**空宏**（Windows 遗留）；
  LodState 统计字段改名 fullCount/midCount/farCount；
- 感知系统原先在每僵尸循环里构造 `view<PlayerTag, Position>`：500 僵尸 ×120 tick 下
  Debug 成本可观（也是排查基准时顺手优化点）→ 每 tick 收集一次目标列表缓存复用。

**测试**（tests/test_boids.cpp，+8 用例：104 → 112 用例 / 117363 断言全绿）
- 分离方向/严格相背/确定性（叉积不抖动）；聚合对齐仅移动中（站立不被唤醒、近邻触发向右上偏移）；
  速度截断；邻域外不误伤；LOD 三档划分/节奏/dtScale/焦点移动重评；AiSystem 门控补时等价；
  虚拟化统计（不入 registry/漂移/驻留/扣减/移除）；
- **基准**（M5 预验收口径）：500 僵尸 + 200 虚拟 —— Release **0.396 ms/tick**（预算 <4ms ✔）；
  Debug 10.2ms（分解 boids 4.3 / ai 3.3 / perception 1.1）。已记入 perf-baseline「M5 预验收」。

**冒烟（实机 40 僵尸，截图 m5t5_*）**：HUD `lod: full=32 mid=8`（拉远后 mid=11，焦点分档即时重评）、
`virtual: hordes=5 zombies=200 nearest=44→30→23`（统计层漂移）；F7 枪声回归无异常；
尸群不再"完全叠成一坨"，而是环绕玩家约 0.8 格间距的密集环（Boids 分离的视觉验证）。

---

## 2026-10-16 M5-T6 战斗系统（扇形判定 → 伤害管线 → 硬直/击退/倒地/尸体）

**组件**（`characters/components/CombatComponents.h` + `CharacterComponents.h`）
- `DamageType{Blunt/Bite/Debug}`；`DamageEvent{sourceId(稳定ID), type, amount, dir, knockback, stagger}`
  —— 管线输入结构：**来源/类型解耦**（Health 只认数值；M6 护甲按类型扩展减伤）；
- `AttackIntent{targetId, dir, range, arcDegrees, damage, knockback, stagger, type, pending}`
  —— 挥击申请（AI/玩家写 pending，CombatSystem 消费）；`HitReaction{staggerLeft, vx, vy, flashLeft}`
  —— 受击反应（AI 读"硬直中"；渲染读 SpriteRef.tint 闪红）；`Dying{remaining}` —— 倒地过渡。

**CombatSystem**（`characters/systems/CombatSystem`，链位 Boids → **Combat** → VirtualHorde → Movement）
1) 受击反应衰减：硬直中 MoveIntent 被击退速度接管（覆盖 AI/输入），速度每 tick ×(1−6dt) 衰减
   （3 格/秒击退总位移 ≈0.5 格）；闪红 0.15s → 恢复 tint；
2) 消费 AttackIntent：命中候选**每 tick 一次收集**（HP>0）→ 扇形判定（中心距 ≤ range 且
   与 dir 夹角 ≤ arc/2）→ 可多目标（尸堆里一刀扫多只）；
3) `ApplyDamage` 统一管线（tick/演示/测试/未来脚本共用）：扣血 → 硬直(取大)/击退(覆盖)/
   闪红 → 致死记 kills（结算交 DeathSystem）；**玩家被抓/击杀走 INFO 日志**（冒烟信号）。
- 阵营规则（M5-T6 临时）：AI 攻击者不误伤同类（僵尸 vs 僵尸豁免；阵营系统 M7+ 再抽象）。
- EnTT 坑：**空类型组件（PlayerTag 这类 tag）不能用 `registry.try_get<T>()`**（本版
  registry.hpp 对空类型返回 void 编译报错）→ 用 `registry.all_of<T>(e)`。

**AiSystem（M5-T6）**：攻击脉冲把参数写进 AttackIntent（targetId/dir=指向目标/damage/…，
配置 combat.zombie*）；受击硬直时**冻结整个状态机**（转移/计时/动作，击退位移由 Combat 接管）。

**DeathSystem 两段式（硬直→倒地→尸体 全表现）**：HP<=0 → 阶段 1 入 `Dying`（清意图 +
冻结速度 + 精灵换尸体帧，0.5s 倒地窗口；击退滑行仍生效）→ 阶段 2 销毁物理体 + 延迟销毁 +
生成尸体。`Update(world, dt)` 签名更新；玩家倒地期间感知不再把它当视觉目标（僵尸立即丢失追击）。

**玩家挥击（演示，SPACE）**：自动瞄准 3 格内最近僵尸（M6 接武器/朝向重做）→ 写 AttackIntent
（40 伤害/100° 扇形/1.7 格）；F6 演示击杀改走 ApplyDamage（999 Debug）——两条路径同一管线。
HUD 新增第 13 行 `combat: swings/hits/dmg/kills | staggered`；键位行加 SPACE。

**配置**：新增 `combat.*`（zombieDamage 8 / zombieKnockback 2 / zombieStagger 0.3 /
zombieRange 1.3 / zombieArc 120 / playerDamage 40 / playerRange 1.7 / playerKnockback 3 /
playerStagger 0.35 / deathFallSeconds 0.5）+ `Attack: SPACE`。

**测试**（tests/test_combat.cpp 新增 7 用例；test_death.cpp 适配两段式）
- 扇形判定（锥内命中/锥外/距离外/pending 单次消费）；伤害管线（解耦 + 硬直接管移动 +
  击退衰减 + 闪红恢复 + 致死不重复结算）；AiSystem 攻击脉冲落 AttackIntent + 硬直冻结
  （30 tick 无新脉冲、stateTime 不涨）；**全链路**（3 刀致死 → Dying（速度冻结/换尸体帧）→
  倒地中不再被命中 → 40 tick 后转尸体）；僵尸不误伤同类（同类豁免/玩家正常受击）；
  感知剔除死亡玩家。**119 用例 / 117456(Debug) / 117457(Release) 断言全绿**。

**冒烟（实机，截图 m5t6_*）**：开场即被村内僵尸围住撕咬（F7 前已 `attack=5`）——
`玩家受到伤害：8 点` 连续刷屏、hp 92→4 归零 → `倒地：ID 41` → 尸体 → 原地重生（M5-T1 流程
无回归）；空格 8 连击 `swings=95 hits=111（多目标扇形）dmg=1784 kills=13`，
`击杀：目标 ID 35（来源 ID 51，40 点 0 伤害）→ 倒地 → 0.5s 后尸体`；HUD `staggered=1`
（被撕咬硬直实时可见）、`virtual nearest=29`（M5-T5 无回归）。
**已知调优项**：≥5 只僵尸围咬时玩家被连续硬直难以脱身（PZ 式"被抓住"；平衡与逃生留 M6+
武器/推挤），重生点固定原地（安全点选择留后续）。

---

## 2026-10-16 M5-T7 刷怪与虚拟化（人口守恒：live ⇄ virtual 双向转换）

**SpawnLodSystem**（`ai/systems/SpawnLodSystem`，链位 Death → **SpawnLod** → Animation）
- 同一批僵尸三种形态、人口守恒：`live(registry) + virtual(群落统计) ≈ populationTarget`
  —— 转换只改形态不改总量；击杀才会减少总量，补给只补缺口（不会"分身"）；
- **密度目标（补给）**：总量低于 target 时按 refillPerSecond 把缺口补成虚拟僵尸，
  落在焦点周围 [refillRadius, +band] 环带（近距 AddOrMerge 归并既有群落），
  由 VirtualHordeSystem 的漂移逐步带近 —— "区域密度"由此自适应维持；
- **实体化**：群落中心进 materializeRadius（32 格）→ 按全局速率拆出实体入 registry：
  出生点 = 群落中心 ± spread 圆盘采样 + 可走格吸附（FindNearestWalkable）+
  **推离焦点至 minSpawnDistance（30 格）保证在弹出的视野外**；maxLive（320）封顶；
- **虚拟化（回差防抖）**：实体超 virtualizeRadius（48 格）→ despawn 钩子回收物理体 →
  销毁实体 → AddOrMerge 折回群落统计；32/48 的回差带消除边界来回转换抖动；
- 倒地（Dying）/已死亡实体不参与虚拟化（归 DeathSystem）；速率按帧累计（perSecond × dt）、
  单 tick 上限 8 个（倍速保护）；每 5s 一条 DEBUG 统计（冒烟时间序列证据）。

**VirtualHordeStore::AddOrMerge**：虚拟化回收与密度补给共用——最近群落中心 ≤ mergeRadius
则就地并入（统计粒度不需精确质心），否则新建。

**ZombieFactory**（`game/ZombieFactory`）：僵尸组件构成单一出处（AI/感知/寻路/战斗/LOD/物理
全套）；WorldDemoState 初始 40 只种子改为调工厂（净删 20 行），GameApp 的 SpawnLodSystem
实体化回调共用同一工厂 —— 演示种子与流式实体完全同构。

**接线**：config `spawner.*`（populationTarget 240 / maxLive 320 / 32/16 / 30 / 48/12 /
2 / 40+20 / 15）；HUD 新增第 14 行 `spawn: live/virtual/hordes | mat/virt/refill`；
focus 与 LodSystem 同源（相机中心）。

**冒烟（实机 + 3 张 HUD/日志证据）**
- 初始：`live=40 virtual=200 hordes=5 | mat=0 virt=0 refill=0`（demo 种子 40+200=240 恰好满员）；
- F4 自由相机左移 ~60 格（焦点远离）→ **虚拟化**：`live=44 virtual=196 hordes=10 | mat=40
  virt=36`，最近群落 49 格（回差带外），守恒 240；
- 相机移回 → **实体化**：`live=93 virtual=147 | mat=121 virt=68`，最近群落 31 格（持续出笼），守恒 240；
- 空格连杀 7 只 → **补给**：`refill=7`、`live=82 virtual=158`，守恒逐项对账精确
  （40 + 51mat − 2virt − 7kills = 82 ✓）—— HUD 行 14 截图 m5t7c_refill。
- 冒烟抓出并修复边缘 bug：**倒地窗口内残留的玩家挥击仍会"死人出刀"**（来源 ID 已倒地，
  击杀日志打脸）→ CombatSystem 消费 AttackIntent 时统一校验攻击者
  （Alive + 非 Dying + HP>0），新增单测锁定。

**测试**（tests/test_spawn_lod.cpp 新增 7 用例）：AddOrMerge 归并语义；实体化速率/扣减/
格中心吸附；maxLive 与单 tick 上限双封顶；虚拟化折回+归并+回差防抖；倒地/死亡不越权；
补给环形放置+到目标即停；守恒（实体化/虚拟化后 live+virtual 不变，焦点移动场景）。
CombatSystem 增补"倒地攻击者挥击忽略"用例。**127 用例 / Debug 117543 / Release 117544 断言全绿**。

---

## 2026-10-16 M5 验收（角色与僵尸 AI —— 全链复测 + 实机 500 压测 + tag v0.5.0）

**验收四项（docs/01 M5 验收）**
1. **视野内追击 → 跟丢搜索 → 放弃**：M5-T2/T3 验收链单测（test_perception：看见→追击→
   目标消失→去最后已知位置搜索→超时回 Idle）+ M5 实机冒烟（F7 后 chase/search 状态流转）；
2. **枪声吸引 + 隔墙衰减**：M5-T4 单测（事件总线全链 OnSound → 感知 → AI 去声源；隔墙
   ×0.6^n 有效半径）+ 实机 F7 冒烟（半径内僵尸闻声而来）；
3. **500 僵尸 + 200 虚拟 tick < 4ms**：基准升级为全链口径（+Combat+VirtualHorde 漂移+SpawnLod
   扫描）——Release **0.396 ms/tick**（boids 0.308 主导；新增两系统合计 0.019ms；稳态转换 0/0
   守恒断言 live=500/virtual=200）；Debug 12.37ms。另做**实机 Release 500 同屏压测**
   （populationTarget=500 命令行覆盖）：FPS 59、人口恒定 500（live 424~454 + virtual 46~76）、
   实体化累计 690 / 虚拟化累计 276——守恒逐项对账精确；
4. **击杀完整表现**：M5-T6 全链路单测（挥击→硬直→击退→致死→倒地窗口→尸体）+ 实机日志链。
**全部写入 docs/perf-baseline.md「M5 验收基线」**（场景 A 头less 基准 + 场景 B 实机压测）。

**里程碑数字**：M5 共 7 个任务；当前 **127 用例 / 117545+ 断言全绿**；
系统链从 9 个增至 13 个（Lod/Boids/Combat/VirtualHorde/SpawnLod 等）；僵尸从"漂移盒子"
进化为 感知-记忆-追击-群聚-LOD-战斗-倒地-尸体的完整闭环，且**同屏 500 只 60fps 可玩**。
下一步：M6 物品与容器（物品定义注册表 → 掉落/拾取/背包/丢弃）。

---

## 2026-10-16 M6-T1 物品定义注册表

**交付**：`src/items/`（`ItemDef.h/.cpp` 类型枚举与定义结构；`ItemRegistry.h/.cpp` 加载+校验）、
`assets/items.json`（7 条起步内容：绷带/罐头/消防斧/厨刀/破布/夹克/手提袋）、GameApp 启动加载
（`items.file` 配置键，失败不阻断启动、空表降级）、GameContext 注入 `items` 指针。

**格式**：JSON v1 根对象 `format="items-v1"` + `items` 数组；字段 id/name/icon/weight/maxStack/type。
严格校验（任一错误整体不加载、失败不覆盖旧数据）：未知字段（抓拼写错误）/
id 字符集与唯一性/weight 0~1000/类型枚举 5 种/结构错误全报。

**报错定位到行**：不依赖解析库 —— `ItemRegistry.cpp` 内置轻量 JSON 扫描器（SkipString/SkipValue/
FindTopLevelValue/FindArrayElement）在原文中定位：语法错误报「第 X 行 第 Y 列」（e.byte 换算）、
每个条目错误都精确报出该条目 `{` 所在行（重复 id 同时报首次定义行），例：
`test: 第 6 行 第 5 列: items[2]: id "dup" 重复（首次定义于第 4 行）`。

**测试**：新增 tests/test_items.cpp 8 用例（合法加载/语法行号/字段非法×10/结构错误×6/重复 id 两处
行号/失败不覆盖/内置文件内容守护/枚举往返）；Debug 135 用例 117668 断言全绿（Release 117669）。
**实机**：启动日志 `物品注册表已加载: 7 条定义 <- assets/items.json`；临时加 hammer 条目重启
→ `8 条定义`（M6 验收第 1 条「改 JSON 重启即生效」提前预验证），随后还原。

---

## 2026-10-16 M6-T2 物品组件与系统

**组件**（`src/items/ItemComponents.h`，zl::ecs）：`ItemInstance`（defId/count/condition，
地面实体与容器条目共用同一数据结构，地面物品 T3 复用）、`Container`（vector 条目内联 +
capacityKg 重量上限；玩家主背包/口袋/尸体容器共用）、`Equipment`（槽位数组：
PrimaryHand=仅武器、Body=仅衣物；扩展 = 加枚举值 + SlotAccepts 规则）。

**规则层**（`src/items/InventoryOps.h/.cpp`，纯函数无 ECS/物理依赖，全部可无头单测）：
- `AddItem`：堆叠合并（先填未满堆叠再开新堆叠；单次超过 maxStack 自动拆堆）+ 容量约束
  （kg，epsilon 防浮点误判）+ **允许部分加入**（返回实际数量；拾取到一半 = 地面留剩余，T3 直接用）；
- `RemoveItem`（返回实际移除数）/ `CountOf` / `TotalWeight`；未知 defId 一律告警不生效；
- `Equip`（类型-槽位校验 + 换装时旧装备移入 outSwapped）/ `Unequip` / `SlotAccepts`。

**接线**：玩家生成（WorldDemoState::EnsurePlayerSpawned）挂 Container（容量 config
`inventory.playerCapacityKg`=20kg）+ Equipment；config/game.json 新增 inventory 节。

**测试**：新增 tests/test_inventory.cpp 6 用例（堆叠合并/拆堆/maxStack=1 不合并、容量边界与
部分加入×4、未知与非法实例、移除计数、装备换装/拒装/卸下、重量与槽名）；141 用例全绿
（Debug 117757 / Release 117758）。实机冒烟：启动无错误日志，玩家两次生成均成功
（第二次 = 已知的出生点被围殴平衡问题）。

---

## 2026-10-16 M6-T3 地面物品实体（睡眠化）

**实体形态**（`src/game/GroundItems.h/.cpp`）：地面物品 = `Position + ItemInstance + SpriteRef`
（sprite = HashName(ItemDef.icon)，layer=-1 → 同格时画在角色之下；**无 Collider/RigidBody**：
不阻挡、不参与物理）。Spawning 走 `SpawnGroundItem`（未知定义/非法实例拒绝+告警）。

**睡眠化**：物品由 SpatialIndex 按 chunk 桶管理（Position 驱动，自动入桶/换桶/回收）；
渲染收集（RenderCollectSystem）只查询相机视口覆盖的 chunk → 远离相机的物品自然不进入
收集、0 额外 drawcall（实测 drawcall=3 不变）；无需每帧全量遍历。

**拾取链路**：`PickupNearestItem`（半径内最近同层物品 → `InventoryOps::AddItem` 并入
玩家 Container → 地面数量扣减，归零才销毁实体——支持**部分拾取**，装不下留在地面）。
F 键绑定（config `input.bindings.Pickup` + `inventory.pickupRadius`=1.6）；HUD 行 15
`bag: N item(s) W/Ckg | ground=M`；提示文案 ASCII（Nothing to pick up / Bag full），
中文名进日志。

**资源**：gen_placeholder_atlas.py 新增 7 个物品图标帧（绷带/罐头/斧/厨刀/破布/夹克/
手提袋，格中心带小图案）；manifest 新增 item.<id> 逻辑名（own 绑定，pz provider 下
自动回退 own）；重生成图集 34 帧。

**开局演示**：村庄附近固定 8 散落点（经 FindNearestWalkable 吸附 + chunk 就绪门控），
含绷带 x2 / 破布 x3 堆叠演示；状态退出时清理地面物品。

**测试**：新增 tests/test_ground_items.cpp 6 用例（实体构成/空间索引归属/最近搜索含 z
过滤/完整拾取+实体销毁/部分拾取+背包满/坏数据防御/丢弃往返一致）；147 用例全绿
（Debug 117812 / Release 117813）。

**实机冒烟**（Release；僵尸无害化覆盖参数便于走位）：散落 7 处（1 处无可走格跳过）→
HUD ground=7 → W 走位 → F 拾取 → `拾取：绷带 x2（地面剩 0 件；背包 2 件）` +
HUD `bag: 2 item(s) 0.2/20.0kg | ground=6` → 再按 F `Nothing to pick up` 负例；
截图 m6t3_1_scatter / m6t3_2_pickup / m6t3_3_empty。

---

## 2026-10-16 M6-T5 装备/使用闭环（T4 背包 UI 顺延至正式 UI）

**范围说明**：按用户决定，M6-T4（临时背包列表 UI）顺延——正式 UI 里程碑（M9）一并实现；
本任务用临时按键 + HUD 完成装备/使用闭环，并补 G 丢弃键凑齐验收第 2 条全链路。

**JSON v1 扩展**：物品可选 `"use"` 对象（heal 0~100 HP；hunger/thirst/fatigue 0~1，
正值 = 减少需求）；严格校验（未知键/类型/越界全报 + 行号）。items.json：绷带
`{heal:15}`、罐头豆子 `{hunger:0.35}`。

**规则层**：`UseFirstConsumable`（消费第一个有效果物品 1 件，返回效果与剩余）+
`ApplyUseEffect`（纯函数：HP 钳到上限并返回**实际**回血；需求钳 [0,1]；空指针安全）。

**接线**（WorldDemoState 临时按键，正式 UI 时迁移）：`E` 使用 / `R` 装备（主手武器
↔ 背包第一件武器，换装回流背包，放不下落地面）/ `G` 丢弃背包第一堆（整堆落地面）；
`F` 拾取（T3）。装备渲染：收集后、排序前追加一个物品图标 DrawItem（玩家旁 +
屏幕偏移、同格层 +1 画在角色之上；正式挂点/动画留后续）。HUD 行 16
`equip: primary=axe body=-`。

**演示调整**：出生点 (70,18) → **(70,24) 村庄南侧开阔地**（原点位在房屋覆盖层下
出生即被楼面遮挡 + 紧贴尸群）；散落点改以出生点为基准；新增 `health.playerMaxHp` /
`playerStartHp` 配置（默认 100/100，演示回血量可用命令行调低起始 HP）。

**测试**：+2 用例（use 字段解析与校验 7 种非法；ApplyUseEffect 钳制/满血/空指针 +
UseFirstConsumable 消耗语义，T5 版）；150 用例全绿（Debug 117857 / Release 117858）。

**实机冒烟**（Release）：
- 拾取×3 → `bag: 3 item(s) 3.2/20.0kg`；R → 日志`装备：消防斧 → 主手` + HUD
  `equip: primary=axe` + **手持斧图标截图**（放大验证：角色右手侧斧头）；
- E（满血）→ `使用：绷带（回血 0，HP 100/100；剩余 1 件）`（钳制正确）；
- E（40/100）→ **hp 40 → 55（+15）**、绷带消耗（3→2 件、3.2→3.1kg）；
- G → `Dropped bandage x1` 提示 + ground 5→6（地面/背包一致）。
截图：m6t5_*、m6t5b_hand（手持）、m6t5c_before_use/healed（回血前后）。

---

## 2026-10-16 M6 验收（物品与容器 —— 全链路单会话冒烟 + tag v0.6.0）

**验收三条（docs/01 M6 验收）**
1. **改物品 JSON 加新物品重启即生效**：临时加入 energy_drink（含 use 效果 heal10/thirst0.3）
   → 重启 → `物品注册表已加载: 8 条定义 <- assets/items.json`（零代码改动）→ 还原；
2. **拾取/丢弃/装备/使用全链路 + 地面背包一致**：单会话冒烟（Release；无害僵尸 +
   起始 40HP + 拾取半径 4）日志链——
   `拾取：绷带 x2 → 拾取：消防斧 x1 → 装备：消防斧 → 主手 → 使用：绷带（回血 15，HP 55/100）
   → 丢弃：绷带 x1（地面物品 7 件）→ 拾取：绷带 x1`；HUD 对账：boot `bag 0/ground 8`
   → 拾取后 `bag 3 (3.2kg)/ground 6` → 装备+使用后 `bag 1 (0.1kg)/ground 6, equip=axe, hp 55`
   → 丢弃后 ground 7 → 再拾取 ground 6（守恒）；
3. **容器容量与堆叠规则单测**：test_inventory/test_items 覆盖容量边界/部分加入/合并/拆堆/
   maxStack=1/装备槽类型规则/use 解析与落地钳制——150 用例全绿（Debug 117857 / Release 117858）。

**实机证据**：m6acc_1_boot / m6acc_2_picked / m6acc_3_equipped_used / m6acc_4_reconcile；
手持渲染另有 m6t5b_hand（放大：角色右手斧）；优雅退出（ESC）验证日志完整落盘。
**里程碑数字**：M6 共 5 任务（T4 顺延正式 UI）；测试从 127 → 150 用例；系统/内容：
物品定义注册表（JSON v1 严格校验+行号报错）、容器/装备规则、地面物品实体（空间索引睡眠化）、
拾取/丢弃/装备/使用临时按键闭环（正式 UI 后迁移）。下一步：M7 Lua 脚本系统（事件 + API + 热重载）。

---

## 2026-10-16 M7-T1 IScriptRuntime 抽象 + ScriptManager

**交付**：`src/core/ScriptEvent.h`（脚本事件 = 事件名 + 命名字段 variant）、
`src/script/IScriptRuntime.h`（ModInfo/ModError/后端接口）、`src/script/ScriptManager.h/.cpp`
（后端注册表 + mod 路由 + 派发 + 错误隔离）+ `tests/test_script.cpp`（8 用例，FakeRuntime 夹具）。

**接口设计（对 docs/03 §3 的落点说明）**：
- 派发接口落在 **ScriptManager 层**（跨后端按 loadOrder 全量派发）；runtime 侧提供
  `DispatchEventToMod(modId, ev) -> vector<ModError>`，使「错误 → 禁用该 mod」能精确归因
  （docs 原稿的 runtime.DispatchEvent(ev) 无法定位出错 mod）；
- runtime 只管「执行 + 捕获错误」，禁用/日志/路由策略全部在 manager（职责单一，便于
  T2 LuaRuntime 聚焦 sol2 细节）。

**错误隔离策略（本任务核心）**：
- LoadMod 失败（含脚本语法错误，错误串带回路径+行号）→ 该 mod 不进入已加载表；
- 派发期脚本错误 → `ZL_ERROR` 记 mod id + 事件名 + 错误（含路径行号）→ 标记该 mod 禁用，
  后续事件跳过（禁用≠卸载，IsModLoaded 仍为 true）；其它 mod 与游戏不受影响；
- 恢复路径：ReloadMod 成功即解除禁用（T6 热重载会复用）；重载失败按 docs/03 §8
  简单版策略保持禁用；
- 子目标全部可测：FakeRuntime 注入 加载/重载/派发 三类失败做黑盒验证。

**接线**：GameApp Init 1.6（`script.enabled` 配置键，默认 true）→ `InitializeAll()` +
就绪日志；Shutdown 在状态机销毁前 `ShutdownAll()`（逆序关闭 + 清 mod 记录，可重复调用）；
config/game.json 新增 `script.enabled/script.backend`（T2/T5 消费）。T1 无后端实例，
启动日志为 `脚本系统就绪（后端 0 个）`，Lua 后端 T2 注册。

**测试**：158 用例全绿（Debug 117938 / Release 117939 断言）；覆盖 注册/重名拒绝、
初始化失败隔离、加载路由（未注册后端/未就绪/运行时失败/重复加载）、按 loadOrder 派发 +
命名字段传递、错误禁用 + 跳过、重载恢复/失败保持、卸载、ShutdownAll。

---

## 2026-10-16 M7-T2 LuaRuntime（sol2）

**交付**：`src/script/LuaRuntime.h/.cpp`（IScriptRuntime 的 Lua 5.4 实现）+ `tests/test_lua_runtime.cpp`
（7 用例，真实临时 mod 文件 + spdlog 输出捕获）+ CMake 接入（`find_package(Lua)`/`sol2 CONFIG`，
两 target 链接 `sol2::sol2` + `unofficial::lua::lua`；vcpkg manifest 的 lua/sol2 此前已装）。

**实现要点**：
- **sol2 + 原生 C API 混合**：sol::state 管解释器；chunk 执行用 `luaL_loadbufferx`（强制文本模式拒
  bytecode）+ `lua_setupvalue` 注入 `_ENV` + `lua_pcall` 配 **traceback 消息处理器**
  （`luaL_traceback`）——错误串 = `文件:行号: 消息` + 完整调用栈（含函数名），满足 T4/T6 的
  「日志清晰（含 mod id 与堆栈）」；
- **每 mod 独立 _ENV 沙箱**：沙箱基础表 = 白名单 base 函数（assert/error/pairs/pcall/type/…，不含
  dofile/loadfile/load）+ string/table/math/coroutine/utf8；os/io/debug/package 从未加载，
  环境 `__index` 指向沙箱表（无全局回退）——mod 间全局变量互不可见（测试验证 a=42 / b=nil）；
  T5 将在此基础上正式化沙箱文档与测试；
- **print 重定向**：per-mod C 闭包（upvalue=mod id）→ `[script-lua] [mod-id] <参数以 \t 连接>`，
  多参数走 `luaL_tolstring`（尊重 __tostring）；
- **重载语义**：先在新环境执行成功才替换旧环境（失败保留旧版本，符合 docs/03 §8）；
  LoadMod 失败不登记；Shutdown 先释放环境引用再 lua_close；
- DispatchEventToMod 暂为空实现（无订阅机制），M7-T4 接通 Events.On。

**测试**：165 用例全绿（Debug 118000 / Release 118001 断言）。覆盖：初始化幂等/未初始化防护、
print 重定向（类型转换 + 前缀）、沙箱（白名单可用 / os、dofile 报错行号）、语法错误行号、
运行时错误 traceback（中文消息 + 函数名 + 栈）、重载成功换新/失败保留/修复恢复/未加载报错、
环境隔离、卸载幂等、缺失文件、重复加载。

**实机**：Release 启动日志 `注册脚本后端：lua → 脚本后端 lua 初始化完成 → 脚本系统就绪（后端 1 个）`。

**坑**：sol2 4.x 中 `sol::table::create` 静态重载被派生类的同名非静态成员隐藏（C2352）→
改用 `lua_createtable` + `sol::stack::pop<sol::table>`；`sol::state` 构造**不会**自动打开标准库，
必须显式 `open_libraries(...)`。

---

## 2026-10-16 M7-T3 ScriptApi 第一版（薄包装 + StableId 句柄）

**交付**：`src/script/ScriptApi.h/.cpp`（Host API 门面，不依赖任何脚本库）+ LuaRuntime 每 mod
API 表绑定 + 底层两个新查询（`PathfindingService::IsPassable`、`PhysicsWorld::TeleportBody`）
+ `tests/test_script_api.cpp`（5 用例）+ `test_lua_runtime.cpp` Lua 绑定端到端用例（脚本调用
→ 断言日志输出 + 副作用落地）。

**API 定稿（docs/03 §4，命名 PascalCase）**：
- `Game.GetTick() / GetPlayerId() / GetTime()（{year,month,day,hour,minute,totalMinutes}）`
- `Log.Info/Warn/Error(msg)`（模块名 script-mod，带 [mod-id] 前缀）
- `Player.GetPosition()→{x,y,z}|nil / Teleport(x,y,z) / GetHealth()→number|nil / AddItem(defId[,count])→加入数`
- `World.IsPassable(x,y) / SpawnItem(defId,x,y[,z])→bool`
- `Zombies.Spawn(x,y[,z])→handle / Count() / GetPosition(handle)→{x,y,z}|nil`
- `Items.Exists(defId) / GetName(defId)`（只读注册表）
- 实体一律 StableId（0=无效；跨存档有效）；`ScriptApi::kApiVersion = 1`（T5 加载器按 mod.json 校验）。
- 归 T4：`Events.On/Off`、`Game.SetTimer`（需要 tick 泵）；`World.GetSquare` 随后续扩充。

**实现要点**：
- ScriptApi 是「无状态门面」：所有改变游戏状态的方法都是 const（只经 GameContext 指针改世界），
  绑定层持 `const ScriptApi*` 即可；
- Teleport 三件套：ECS Position + Velocity 归零 + **物理体传送**（新增
  `PhysicsWorld::TeleportBody`：同步 motion state/interpolation、清速度与力；
  PhysicsSync 每 tick 从物理体回写位置，只改 ECS 会被下一 tick 覆盖）；
- `World.IsPassable` 走寻路服务的 chunk 位图缓存（新增查询方法；未加载 chunk =
  不可通行，与 PathSnapshot 语义一致）；
- 绑定层返回 `std::optional<sol::table>` → Lua 侧 nil（sol2 原生支持 optional→nil）；
  z 参数用 `sol::optional<int>` 缺省 0。

**测试**：171 用例全绿（Debug 118075 / Release 118076 断言）。Lua 端到端覆盖：读位置/加物品/
传送/血量/刷僵尸（handle≠0 → 查询位置）/物品名（中文回传）/SpawnItem 真/假/IsPassable/日期与
tick——脚本日志逐行断言 + 容器/位置/地面/僵尸数副作用对账。

**坑**：
- EnTT 单组件 view（`view<C>().each(fn)`）对「只收 entity 的 lambda」不适用（会走第三分支只传
  组件）→ lambda 写成双参 `(entt::entity, const C&)` 或省略 entity；
- 测试 target 缺 GameLoop/GameTime/ZombieFactory 源文件导致 LNK2019 → 已补进 CMake 测试源列表。

**实机**：Release 启动 `脚本系统就绪（后端 1 个，API v1）`。

---

## 2026-10-17 M7-T4 事件桥接（EventBus ↔ Events.On + SetTimer）

**交付**：
- `src/core/GameEvents.h`：事件表 v1 剩余 11 个键（OnPlayerMove/OnPlayerDeath/OnZombieSpawn/
  OnZombieDead/OnItemPickup/OnItemDrop/OnChunkLoaded/OnChunkUnloaded/OnSaveBegin/OnSaveEnd/
  OnModReload）+ 4 个多字段载荷结构（PlayerEvent/ZombieEvent/ItemEvent/ChunkEvent）；
  `EventBus::EventPayload` variant 按“只加类型”约定扩充（含 OnGameStart/OnTick/OnSound 共 14 键）。
- `src/script/EventBridge.h/.cpp`：订阅全表 14 键；`Translate` 把 EventBus 载荷翻成
  「事件名 + 命名字段」（id→int64、坐标→double、枚举→字符串；未知键/载荷类型不符→nullopt），
  再走 `ScriptManager::DispatchEvent`（错误隔离与禁用策略在 manager）。
- LuaRuntime：`Events.On(name, fn)` / `Events.Off(name[, fn])`（每 mod 独立注册表；Off 省略 fn =
  清空该事件订阅）；`Game.SetTimer(seconds, fn)` 一次性定时器（按**游戏秒**计时，OnTick 派发时泵；
  seconds=0 在下一次 tick 即触发）；`DispatchEventToMod` 事件参数为字段表、回调带 traceback。
- 生产端接线：僵尸工厂 `SpawnZombie(..., core::EventBus* events=nullptr)` 发 OnZombieSpawn
  （SpawnLod 实体化/演示种子/脚本 Spawn 全走同一处）；`DeathSystem(..., core::EventBus*)` 阶段 2
  发 OnZombieDead / OnPlayerDeath（有 PlayerTag 判玩家）；GameApp chunk 回调发
  OnChunkLoaded/Unloaded；WorldDemoState 拾取/丢弃/点击到达发 OnItemPickup/OnItemDrop/OnPlayerMove；
  `ScriptManager::SetEventBus` 后重载成功发 OnModReload（modId）。
- 语义：生产者一律 `EmitQueued`（tick 末统一派发；Flush 先换出队列→回调中再发不会当场递归，
  天然防“脚本刷僵尸”风暴）；调用脚本前先拷贝本轮函数列表 → 回调里 On/Off 不破坏遍历；
  重载 = 旧环境整体释放 → 订阅与定时器自动清空。

**已知边界（已注 docs/01）**：`OnZombieDead.killer` 暂无伤害来源追踪恒 0；
`OnPlayerMove` 仅点击移动到达路点（WASD 无路点概念）；`OnSaveBegin/End` 翻译就绪、生产端随存档系统；
`OnSound.z` 恒 0（SoundEvent 无 z 字段）。

**测试**：176 用例全绿（Debug 118165 / Release 118166）。
- 新 `tests/test_event_bridge.cpp`（4 用例）：14 键翻译逐字段断言 + 未知键/错类型防护；
  工厂 OnZombieSpawn（排队→Flush 到达 + events=nullptr 不发）；DeathSystem 双事件（僵尸/玩家 +
  events=nullptr 不发）；ReloadMod→OnModReload（FakeRuntime）。
- `test_lua_runtime.cpp` 增端到端用例：OnGameStart 发 3 绷带（副作用落地）；OnTick 字段 tick；
  SetTimer(0) 首 tick 泵出、SetTimer(30 游戏秒) 推进游戏钟后泵出；Events.Off 后不再派发；
  双 mod 错误隔离（bad 报错被禁用且日志含 mod id 与错误，good 不受影响）；工厂→桥接→脚本
  `zspawn=2 at 12,12`。夹具（TempModRoot/LogCapture）抽到 `tests/ScriptTestHelpers.h` 复用。

**坑**：
- sol 的 lambda 捕获 **const 引用**时闭包成员类型也是 const（mutable 救不回来）→ 需要写表的
  sol::table 一律按**值**传进绑定函数再捕获；
- doctest `REQUIRE(entity != entt::null)` 与 EnTT 比较符重载冲突（C2593）→ 先归约 bool 或
  辅助函数（沿用 test_ground_items 约定）。

**实机**：Release 启动 `事件桥接就绪（事件表 v1 共 14 个事件）`；569 tick 运行中 chunk 53 加载/
4 卸载事件过桥无异常，ESC 优雅退出。

---

## 2026-10-17 M7-T5 mod 加载器（mods/<id>/mod.json + 沙箱收口）

**交付**：
- `src/script/ModLoader.h/.cpp`：`ScanAndLoad(modsDir)` 扫描子目录（目录名排序 → 确定性），
  逐个解析 + 加载；`ParseModInfo(jsonText, modDir, defaultBackend, out, error)` 可单测/热重载复用。
  字段规则：`id` 缺省=目录名（与目录名不一致仅告警，以 id 为准）；`name` 缺省=id；
  `version` 缺省 0.0.0；`api` 缺省=当前 `ScriptApi::kApiVersion`（**过新拒绝**：如 api=99；
  偏旧告警尽力加载）；`backend` 缺省=config `script.backend`；`main` 缺省 `scripts/main.lua`；
  `loadOrder` 缺省 100（派发顺序由 manager 按 loadOrder 稳定排序）。
- 失败隔离：无 mod.json / JSON 坏 / 顶层非对象 / 字段类型不符 / api 过新 / 后端未注册 —— 单 mod
  记日志跳过，不阻断其它（与事件错误隔离同原则）；目录不存在 → 空结果不算错误。
- GameApp 1.6 接线（bridge 之后、`OnGameStart` 之前）：`script.modsDir`（默认 `mods`）。
- config/game.json 增 `script.modsDir`。
- 沙箱收口：T2 起 os/io/debug/package 从未加载、dofile/loadfile/load/require 不在白名单——
  本任务把测试扩展到面断言（脚本内 `tostring(debug)` 等全为 nil，string 等白名单可用）。
- `tests/ScriptTestHelpers.h` 增 `WriteManifest`；新 `tests/test_mod_loader.cpp`（2 用例）。

**坑（实机发现，已修复）**：Windows 编辑器（PS 5.1 `-Encoding UTF8`、旧记事本等）写出的
**UTF-8 BOM** 会让 Lua 词法层报 `unexpected symbol near '<\239>'` → LuaRuntime 读源文件时剥离
BOM（\xEF\xBB\xBF），新增对应用例；mod.json 侧 nlohmann 自带 BOM 容忍，无需处理。

**测试**：179 用例全绿（Debug 118219）。解析矩阵（完整/缺省/id 回退/旧 api/过新 api/坏 JSON/
错类型）+ 扫描集成（7 目录：加载 3 跳过 4；loadOrder=10 先于 90 派发，与目录序无关；
被跳过的 mod 脚本从未执行）+ BOM 容忍 + 沙箱面。

**实机**：真实 mod（mods/smoke_hello：OnGameStart 打玩家 ID、OnTick 第 60 拍打 `Zombies.Count`）——
`mod 扫描完成：1 个目录，加载 1，跳过 0` → `[smoke_hello] smoke mod loaded` → `game started`
→ `60 ticks, zombies=40, tick=59`（Host API 在真实游戏里生效）。注：OnGameStart 触发时玩家尚
未生成（玩家在 WorldDemoState 首 tick 生成，`player=0` 属预期）；冒烟 mod 已删除（示例 mod 归 T7）。

---

## 2026-10-17 M7-T6 热重载（mtime 轮询 + 主线程消费 + 失败恢复）

**交付**：
- `src/script/ModWatcher.h/.cpp`：后台线程按间隔（默认 1s，config `script.hotReloadSeconds`）
  扫描各 mod 根目录**递归**文件签名（文件数 + 最大 mtime；增删改都会反映）；
  签名变化 → 列入待处理并**立即推进基准**（同一变更只通知一次；修复文件再存盘会再次触发）；
  线程只收集通知、从不触碰 Lua（解释器只归主线程）；条件变量休眠，析构 stop+join 干净退出。
- 主循环消费（GameApp::MainLoop，ProcessResults 之后）：`PollChanges()` → `ScriptManager::ReloadMod`
  ——重载成功 manager 发 `OnModReload`（T4 已接）、失败禁用并在日志提示“修复文件再保存可恢复”。
- `ScriptManager::GetModInfo(id, out)`：热重载按 id 取 rootPath 的查询口（拷贝语义）。
- config：`script.hotReload`（默认 true）/ `script.hotReloadSeconds`（默认 1.0）；
  启动日志 `热重载监控已启动：N 个 mod（轮询 1.0s）`。
- 只监控启动时已加载的 mod（本版本无运行时新增 mod 入口）。

**语义对账（docs/03 §8）**：清订阅=重载时重建全新 `_ENV`（旧环境连订阅/定时器一并释放）；
失败保留旧环境（LuaRuntime 从 T2 起就是“临时环境成功才替换”）+ manager 层禁用（§8.4 简单版策略），
两者叠加：失败后该 mod 停止派发、修复重载成功后自动解除禁用。

**测试**：181 用例全绿（Debug 118242 / Release 118243）。`tests/test_hot_reload.cpp`：
① ModWatcher 单元（无初始通知、同一变更不重复、按 mod 隔离、二次修改再触发）；
② 端到端：v1 生效 → 改文件检测 → ReloadMod → OnModReload（modId）→ 仅 v2 生效（旧订阅消失）→
写语法错误 → 重载失败禁用且不再产生脚本日志（游戏不崩）→ 修复 → 重载成功解除禁用 → v3 生效。

**实机**（真游戏内改文件全循环，日志节选）：
`alive v1 tick=180` → 改文件 → `mod 重载成功：[smoke_hello]` + `alive v2` → 写坏 →
`mod 已禁用（重载失败：main.lua:1: syntax error near 'is'）` + 进程存活继续跑 → 修复 →
`mod 重载成功，已解除禁用` + `alive v3`。冒烟 mod 已删除（示例归 T7）。

---

## 2026-10-17 M7-T7 示例 mod ×2 + docs/modding.md + 死循环防护

**交付**：
- `mods/example_hello_lua`（最小入门）与 `mods/example_reactive_lua`（事件反应：5 杀奖励/
  拾取/枪声/玩家死亡 + SetTimer），均可直接热重载试改。
- `docs/modding.md`：mod 作者教程（快速开始 → mod.json 字段表 → Host API v1 参考 → 事件表 v1
  与注意事项 → 定时器 → 调试/错误隔离/死循环防护/沙箱边界 → 示例说明 → FAQ → v1 限制）。
- **死循环防护**（M7 验收条款②）：`lua_sethook` 指令计数——extraspace 存 Impl 指针，
  hook 每 1 万条指令检查；**单次外部调用**（chunk 执行/事件回调/定时器回调）预算约 500 万条，
  超限 `luaL_error("脚本指令数超限（疑似死循环）…")` → 走既有 pcall+traceback → ModError →
  归因 mod 并禁用；预算在每个入口装配、返回即解除（正常 mod 的大循环不受影响，调用间独立）。
  已知边界：脚本自己 pcall 吞错的对抗性循环不拦（"防手滑"定位，modding.md §6.4 声明）。
  `docs/03` §7 死循环一行同步更新为"已实现"。
- 示例可用性修正（实机发现）：`OnGameStart` 触发时玩家实体尚未生成 → `Player.AddItem` 返回 0；
  hello 示例与 modding.md 快速开始改为"等 `Game.GetPlayerId() ~= 0` 首次就绪再发"。

**测试**：182 用例全绿（Debug 118252 / Release 118253）。新增：加载期死循环（`while true do end`
→ LoadMod 失败含"指令数超限"）、派发期死循环（ModError 归因 mod + traceback）、
预算按调用重置（10 万次小循环不误伤）。

**实机（三条验收证据一次跑齐）**：3 个 mod 加载；`break_test` 的 OnTick 死循环被拦：
`mod [break_test] 脚本错误（事件 OnTick）：…指令数超限…` + `mods/break_test/scripts/main.lua:2: in
function <…>`（mod id + 路径行号 + 栈）+ `mod 已禁用`，进程存活继续跑；修复文件保存 →
`mod 重载成功，已解除禁用：[break_test]`；两示例输出 `starter items: bandage x2, axe x1`、
`tip: press F6 …`（SetTimer）、`survivor at 69.2, 23.7 (z=0) | health=84.0 | zombies=40`。
临时 break_test 已删除。

---

## 2026-10-17 M7 验收（Lua 脚本系统，tag v0.7.0）

**验收现场**：Release 实机单会话跑 5 个 mod（2 正式示例 + 验收临时 mod ×3），三条标准一次过：

1. **示例 mod 不改 C++ + 热重载立即生效**：运行中把 `mods/example_hello_lua/scripts/main.lua`
   的周期从 600 tick 改为 120、文案改 `survivor@v2` → 约 2 秒后
   `mod 重载成功：[example_hello_lua]` + 重执行日志 `hello mod loaded` +
   新的 `survivor@v2 at …` 立即出现；全程未改一行 C++；文件已 `git checkout` 还原。
2. **死循环/报错不崩 + 日志清晰**：
   - `bad_loop`（OnTick `while true do end`）→
     `mod [bad_loop] 脚本错误（事件 OnTick）：…脚本指令数超限（疑似死循环）…已中止` +
     `mods/bad_loop/scripts/main.lua:1: in function <…>`（mod id + 路径:行号 + 栈）+ `mod 已禁用`；
   - `bad_error`（调不存在的函数）→
     `attempt to call a nil value (global 'this_function_does_not_exist')` + traceback + `已禁用`；
   - 进程全程存活，其它 3 个 mod 与游戏本体零影响（`processAlive=True` 贯穿）。
3. **modding.md 可复现写出第三个 mod**：按文档 §1 结构从零写 `mods/night_watch`
   （周期哨所报告 `[watch] pos/…hp/…zombies/…kills`、击杀计数、`Game.SetTimer` 问候）——
   扫描加载、SetTimer 触发、300 tick 周期输出全部正常。

验收临时物（night_watch / bad_loop / bad_error）随后清理，仓库只保留两个官方示例。

**状态**：M7 完成（T1~T7 + 验收）；测试 182 用例全绿（Debug 118252 / Release 118253）。

---

## 2026-10-17 M8-T1 CMake 开关 + Python 依赖接入

**背景**：M0 骨架里已预埋 `option(ZL_ENABLE_PYTHON OFF)` + `VCPKG_MANIFEST_FEATURES "python"`
+ vcpkg feature（pybind11/python3）；本任务完成真正的 find_package 与链接接入。

**实现**：
- `CMakeLists.txt`：`if (ZL_ENABLE_PYTHON)` → `find_package(Python3 COMPONENTS Development.Embed REQUIRED)`
  + `find_package(pybind11 CONFIG REQUIRED)`；两 target（游戏 + 测试）加 `ZL_ENABLE_PYTHON=1`
  编译定义并链接 `pybind11::embed`。
- `CMakePresets.json`：新增 `x64-debug-python` / `x64-release-python`（与普通预设**共用同一
  binaryDir**——切开关即切 vcpkg 依赖集，不复制构建目录）。
- `src/script/PythonSupport.{h,cpp}`：`IsPythonBackendCompiled()` + `PythonBackendVersion()`
  （"CPython 3.14.8 / pybind11 3.1.0"）；用 `Py_GetVersion()`（无需初始化解释器）验证与
  CPython 的**真实链接**，而非只包含头文件。
- `GameApp` 脚本初始化处新增 debug 日志 `Python 后端：…`；`tests/test_python_backend.cpp`
  1 用例（按编译开关走两个分支断言）。

**实况/坑**：
- vcpkg 提供 python3 3.14.8（CPython 源码构建 + extensions，连 openssl/mpdecimal/zstd），
  pybind11 3.1.0；`Found Python3: …/python314.lib … found components: Development.Embed`——
  用的是 vcpkg 的 CPython，不是系统 Python。
- **首次 configure 超过 30 分钟被 shell 超时截断**（vcpkg 在建 CPython）；续跑命令走二进制缓存
  完成，总 configure 601s。教训：加 manifest 特性后第一次跑预留 ≥40 分钟。
- OFF→ON→OFF 往返验证：ON 构建 183 用例全绿 + 实机 `Python 后端：CPython 3.14.8 /
  pybind11 3.1.0`；切回 OFF 后 vcpkg 自动移除 17 个 python 相关包，`dumpbin /DEPENDENTS`
  确认 exe 无 python DLL，183 全绿。
- 小尾巴：vcpkg 移除 python3 后 `vcpkg_installed/.../tools/python3` 残留安装期 Python 自己
  写的 `.pyc`（vcpkg 文件清单追踪不到），手工清理；重建时会重新生成、无影响。

---

## 2026-10-17 M8-T2 PythonRuntime（pybind11 嵌入 + 沙箱）

**实现**：`src/script/PythonRuntime.{h,cpp}`（仅 ZL_ENABLE_PYTHON=ON 编入实现）：
- 嵌入 CPython：`PyConfig` + `py::initialize_interpreter(&config, …)`；GIL 纪律——所有脚本
  入口（加载/重载/卸载/派发）统一 `py::gil_scoped_acquire`，脚本只在主线程执行；
- 沙箱：每个 mod 独立 globals + 受限 builtins——`print` 重定向 `[script-py] [mod-id]`、
  `__import__` 白名单（math/random/json/re/time/datetime/…，其余 ImportError）、
  删除 open/eval/exec/compile/input 等；
- 错误隔离：compile(filename=真实路径)+`PyEval_EvalCode` 执行，`py::error_already_set`
  → errorOut（含 Python traceback 与 `File "路径", line N`）→ 上层记日志/禁用；
- 重载语义与 Lua 一致：先在新 globals 执行，成功才替换（失败保留旧版）。
- GameApp 在 ON 构建注册第二后端；ScriptManager 对"未注册的 python 后端"追加
  "本构建未启用 Python 后端"提示（M8 验收第 2 条）；ModLoader 默认入口按 backend 选
  `scripts/main.py` / `scripts/main.lua`。

**踩坑（重要）**：
1. **嵌入找不到标准库**：`Py_InitializeFromConfig` 报 `Failed to import encodings module`，
   加错误路径不清会自动变成崩溃。根因：vcpkg 的 python3 运行时+stdlib 在
   `vcpkg_installed/<triplet>/tools/python3/`，不在 dll 默认搜索路径。修复：显式
   `PyConfig.home`；查找顺序 env `ZL_PYTHON_HOME` → exe 旁 `python/` → 编译期默认。
2. **Debug 树没有可用运行时**：vcpkg python3 debug 只有 `python314_d.lib`，无 stdlib。
   统一改为两配置都链接 release CPython（`MAP_IMPORTED_CONFIG_DEBUG RELEASE`）+ 拷贝
   `python314.dll`、`z.dll`（python314.dll 依赖 zlib，applocal 只管 bin 目录，抓不到）。
3. **pybind11 3.x `py::exec` 只有 `str` 重载**：`py::exec(codeObject, globals)` 会隐式把
   code 转成 `"<code object …>"` 字符串再编译 → 永远 `SyntaxError: invalid syntax
   (<string>, line 1)`（连合法脚本都挂）。改用 `PyEval_EvalCode(code, globals, globals)`。
4. `py::kwargs` 参数必须**按值**声明（`const py::kwargs&` 不满足 kwargs 标记 trait）。
5. `py::is_initialized()` 在 pybind11 3.x 已移除 → 用 `Py_IsInitialized()`。
6. 测试进程**不要反复 finalize/initialize**（pybind11 内部状态易残留）：测试用共享
   runtime 单例（故意泄漏到进程结束），与游戏内单实例生命周期一致。

**验证**：ON 构建 187 用例全绿（新增 4 个 M8-T2 用例：加载+print 重定向（含中文）、
import 白名单/危险内建、语法/运行时错误归因（路径+traceback）、重载替换与失败保留）；
实机 smoke_py（backend=python）：`注册脚本后端：python（共 2 个）`、
`[script-py] [smoke_py] smoke_py loaded, pi=3.1416`、中文输出、`mod 已加载`，进程存活。
OFF 构建 184 用例全绿（PythonRuntime.cpp 空 TU、测试走占位分支）——默认构建仍零 Python 依赖。

---

## 2026-10-17 M8-T3 同一 API 薄绑定 + 事件（Python）

**实现**：`PythonRuntime` 为每个 mod 绑定 per-mod `types.SimpleNamespace`：
- `Game`（GetTick/GetPlayerId/GetTime/SetTimer）、`Log`（Info/Warn/Error）、
  `Player`（GetPosition/Teleport/GetHealth/AddItem）、`World`（IsPassable/SpawnItem）、
  `Zombies`（Count/Spawn/GetPosition）、`Items`（Exists/GetName）、`Events`（On/Off）——
  函数名、参数、默认值与 Lua 侧逐一对齐（mod.json api=1 无需区分后端）。
- 返回约定：位置/时间返回 dict（`pos['x']`、`t['year']`），无玩家/无效句柄返回 `None`；
  事件回调参数为字段 dict（`e['tick']`）。
- 事件语义与 Lua 完全一致：`Events.On` 追加、`Events.Off(name)` 清空该事件、
  `Events.Off(name, fn)` 按 **PyObject 同一性**移除；派发前拷贝本轮函数列表
  （回调里 On/Off 不影响当前遍历）；错误包 `事件 X 处理错误：<traceback>` 归因 mod。
- `Game.SetTimer` 与 Lua 同一泵机制：OnTick 派发前把到期定时器移出并调用，
  回调错误包 `SetTimer 回调错误：…`。
- 周期：每 mod 的订阅表/定时器随 ModEntry 生命周期（重载=旧表释放、新表重执行）。

**验证**：
- 新增 `tests/test_python_api.cpp` 4 用例：① API 全套断言（含 C++ 侧复核
  Teleport 位移 + AddItem 入包）；② On/Off（按函数移除 / Off(name) 清空）+ 事件字段；
  ③ SetTimer 按游戏时间泵出（0 秒当 tick、30 秒推进 1 游戏分钟后）；④ 回调抛
  ValueError → ModError 含 "事件 OnTick 处理错误："+ValueError+boom+main.py。
- ON 构建 191 用例全绿；实机 smoke_py：`tick=0/120/240, zombies=40`、
  `timer fired at totalMinutes=0.05`、`pi=3.14`，进程存活；冒烟 mod 已删。
- OFF 构建 185 用例全绿（Python 用例走占位分支），默认构建仍零 Python 依赖。

---

## 2026-10-17 M8-T4 示例 mod Python 版 + 切换验证

**示例结构改为"后端无关"**：`mods/example_hello_lua` / `example_reactive_lua` 重命名为
`example_hello` / `example_reactive`，`mod.json` 去掉 `backend`/`main`（由 `script.backend`
决定），目录内同时提供 `scripts/main.lua` 与 `scripts/main.py`（同 id、同行为）——
一个 mod 用 config 一键切换后端，不再需要两份目录。（历史 devlog/任务记录中的旧名仅指当时的
Lua 版。）

**Python 版内容**（与 Lua 逐行行为对齐）：
- hello：`import math` + 打印 pi → 等 `Game.GetPlayerId() != 0` 发开局物品 → 每 600 tick
  `survivor at …` 周期报告。
- reactive：`OnGameStart` 重置计数 + `SetTimer(5)` 提示 → 击杀计数与 5 杀奖励 →
  `OnItemPickup/OnSound(Gunshot)/OnPlayerDeath` 日志。

**实测抓到并修复的真 bug**：Lua 的 `function()` 回调会忽略派发器传入的事件表，但 Python 的
`def on_game_start():` 收到 1 个实参会直接 TypeError（`--script.backend=python` 实机首跑，
reactive 在 OnGameStart 被禁用）。修复：派发前判断 `__code__.co_argcount == 0 &&
!CO_VARARGS` → 无参调用，否则照常传事件 dict（对齐 Lua 语义）；测试补 `lambda: Log.Info
('noarg')` 用例。这个事故同时成了"报错不崩 + 归因清晰"的现场证据（日志含 mod id/路径行号/
traceback，进程存活）。

**实机对照**（ON 构建；对照清单见 docs/03 §10，已逐条勾）：
- 默认 lua：`2 个目录，加载 2`；starter items、tip、survivor 周期日志正常（重命名回归 ✓）。
- `--script.backend=python`：hello/reactive 均以 backend=python 加载；`hello mod loaded
  (pi=3.14)`、`starter items: bandage x2, axe x1`、`reactive mod ready …`、`tip: press F6 …`、
  `survivor at …` 全部与 Lua 版一致。
- 热重载：运行中改 `example_hello/scripts/main.py` 文案 → `mod 重载成功：[example_hello]` +
  重执行日志 + 新文案 `survivor@py-v2 at …` 立即出现（文件已还原）。
- 刷僵尸能力：临时 mod `verify_tick_py`（backend=python）tick 600 刷 1 只 →
  `tick-spawn: zombies=41 (handle=52)`；用后删除。
- 0 参回调修复后复跑：`reactive mod ready` / `tip:` 正常，无禁用。

**文档**：`docs/modding.md` 增加 §7.1「Python 后端」（启用方式：预设 `x64-debug-python` /
`-DZL_ENABLE_PYTHON=ON` + `script.backend=python`；API 与 Lua 同名；差异清单：dict 返回/
None/0 参回调/import 白名单/print 接日志）；`docs/03 §10` 对照清单填满。

**验证**：ON 构建 191 用例全绿（+0 新用例，M8-T3 用例扩展 0 参断言）；本任务 C++ 改动均在
`#ifdef ZL_ENABLE_PYTHON` 内，OFF（默认）构建行为不变（185 用例基线不受影响）。

---

## 2026-10-17 M8 验收（可选 Python 后端，tag v0.8.0）

**三条标准现场**：

1. **config 切 python 行为一致**：`config/game.json` 改 `script.backend: "python"` 后实机：
   3 个 mod 全加载，两示例以 python 版运行——`hello mod loaded (pi=3.14)`、
   `player ready, giving starter items...`、`starter items: bandage x2, axe x1`、
   `reactive mod ready: kill 5 zombies to earn a bandage`、`tip: press F6 …`（SetTimer）、
   约 10 秒 `survivor at 69.1, 23.6 (z=0) | health=76.0 | zombies=40`。逐条核对
   `docs/03 §10` 对照清单并全部勾选（含热重载改 .py 2 秒生效、OnTick 刷僵尸）。
2. **Python 报错不崩 + 未编译清晰报错**：
   - `break_py`（backend=python，OnTick 抛 RuntimeError）→
     `mod [break_py] 脚本错误（事件 OnTick）：事件 OnTick 处理错误：RuntimeError: acceptance boom`
     + `mods/break_py/scripts/main.py(2): bad` → `mod 已禁用`，进程存活，其余 mod 正常；
   - 切回 OFF 构建后同一 mod → `未注册的脚本后端：python（mod break_py）；本构建未启用
     Python 后端（CMake 选项 ZL_ENABLE_PYTHON，见 docs/03 §2）`，扫描 `加载 2，跳过 1`，
     两示例照常运行，进程存活。
3. **默认构建零 Python 依赖**：OFF reconfigure 后 vcpkg 移除 python3/pybind11
   （`vcpkg_installed/.../lib/python314.lib`、`share/pybind11` 均不存在），
   `dumpbin /DEPENDENTS ZomboidLike.exe` 无 python，exe 目录无 python*.dll（旧产物已清），
   184→185 用例全绿，启动日志 `Python 后端：未编译（ZL_ENABLE_PYTHON=OFF）`。

**状态**：M8 完成（T1~T4 + 验收）。测试基线：OFF 185 / ON 191（Debug）。
下一步 M9 UI 系统（最大里程碑：UiContext/控件/字体/HUD/背包/菜单；第一个验收点是
"全鼠标完成 拾取→装备→使用→丢弃"）。

---

## 2026-10-17 M9-T1 RmlUi 接入（bgfx 后端 + 输入 + 字体）

**框架决策落地**：vcpkg `rmlui[freetype]` 6.3（baseline 2026-10-15），CMake `find_package(RmlUi)`
→ `RmlUi::RmlUi`，两个 target 都链接。UI 独立走 bgfx View 1，不改现有世界渲染管线。

**实现**：
- `RmlBgfxRenderInterface`：`CompileGeometry` 保存顶点/索引，`RenderGeometry` 每 draw 灌
  transient buffer（顶点位置加 translation）；纹理管理用 handle 表（0=白纹理）；
  RmlUi 6 顶点色/生成纹理均预乘 alpha，混合用
  `BGFX_STATE_BLEND_FUNC_SEPARATE(ONE, INV_SRC_ALPHA, ONE, INV_SRC_ALPHA)`；直接复用
  `vs_sprite/fs_sprite`（`u_viewProj` + 正交投影），scissor 用 bgfx::setScissor；
- `RmlSystemInterface`：GetElapsedTime/日志桥接 spdlog/JoinPath；FileInterface 用默认实现；
- `UiSystem`：Rml::Initialise、上下文随 framebuffer 缩放、加载 `assets/ui/debug.rml`、
  每帧输入→`Context::Update`→`Render`；F11 显隐；Window 新增 GLFW char 回调，Input 新增
  原始键/鼠标边沿查询；CJK 字体 `assets/fonts/NotoSansSC-Regular.otf`（8.3MB，OFL，已入库）；
- 测试：`tests/test_ui.cpp`（键位映射/修饰键/边沿 + 无渲染器 RmlUi 文档加载与点击路由）。
  OFF 187 用例全绿，ON（Python）193 全绿。

**实机验证**：1280×720 下调试面板（标题/中文说明/按钮/输入框/滚动列表/滚动条）显示正常；
点击 `+` 计数 0→1；滚轮滚动列表到底部（内容被正确裁剪）；点击文本框后输入 `RmlUi123`
正常回显；F11 隐藏/显示生效；进程稳定，drawcall 35 左右。已知：UI 与游戏输入尚未互斥
（滚轮同时缩放相机、点击同时给玩家下移动指令）→ M9-T4/T5 处理。

**踩坑**（全部写进 docs/02 和本记录）：
1. **rgba() alpha 是 0~255**，不是 CSS 的 0~1：`rgba(18,24,30,0.92)` 被 atoi 解析为
   alpha=0 → 面板全透明（排查时用 `#ff00ff` 才定位到）；改用 8 位十六进制 `#12181eeb`。
2. **div 默认 inline**：RmlUi 默认样式没有 `div{display:block}`，标题/副标题挤在同一行、
   输入列表按词换行；补 `div { display: block; }`（具体控件用 class/id 覆盖 inline-block）。
3. **滚动条没有默认皮肤**：`overflow-y: scroll` 时未给 `scrollbarvertical { width: ... }`
   → 滚动内容区宽度算成 0（所有文字按单字换行）；补滚动条样式后恢复 370px。
4. **RCSS 不支持 `border`/`border-style` 简写**：解析报错会让整条规则失效 → 拆成
   `border-width`/`border-color`（仍不支持 style）；`background` 简写同样不用，改
   `background-color`。
5. **border-radius 背景消失**：RmlUi 6 的圆角背景走 clip mask/layer，我们的 RenderInterface
   还没实现 `RenderToClipMask`/`PushLayer` → 背景整块不画；T1 先方形直角，T2 补 clip mask/
   layer 后再启用圆角。
6. 键盘/鼠标短按（<1 帧）可能丢：当前用 Input 边沿轮询；实测按住 150ms 的点击可正常触发。
   后续如需要再做窗口回调事件队列。

---

## 2026-10-17 M9-T2 主题与基础控件

**主题**：新增 `assets/ui/theme.rcss`，定义面板/卡片/标题/按钮（primary/次要/禁用/悬停/按下/focus-visible）/
文本输入/滚动列表/滚动条/滑条皮肤；`debug.rml` 改为组件展示页（计数器、禁用切换、图片、
九宫格、文本框、滑条、滚动列表、Tab 提示）。新增 `ui_demo.png`（图片控件）与 `ui_theme.png`
（九宫格贴图，`@spritesheet` + `decorator: ninepatch`）。

**输入/交互**：`UiSystem` 增加禁用按钮切换（Set/RemoveAttribute）与滑条 `change` 事件
（`ElementFormControlInput::GetValue`，解析后取整回显）；Tab 焦点导航直接使用 RmlUi 内建
导航，`:focus-visible` 黄色描边验证通过。

**渲染**：`RmlBgfxRenderInterface` 增加 stencil clip mask：`EnableClipMask`/`RenderToClipMask`
用 8 位引用计数，Set 写引用、Intersect 读取，`BGFX_CLEAR_STENCIL` 每帧清理；遮罩几何走
transient buffer + `bgfx::setStencil`，配合 scissor 实现滚动列表的圆角裁剪。圆角背景本身
仍是直接几何，不依赖 mask。

**实机验证**：计数 0→2、按钮禁用/启用切换、滑条 65→88 且回显、Tab 焦点、滚轮滚到列表末尾；
`ui_t2_final.png` / `ui_t2_counter.png` / `ui_t2_toggle.png` / `ui_t2_slider2.png` /
`ui_t2_tabfocus.png` / `ui_t2_scroll.png`。测试 188 全绿（新增主题文档/圆角/Tab 用例）。

**踩坑**：
1. **RCSS 不接受 UTF-8 BOM**：RmlUi 会打印 warning 并**丢弃整份样式表**（面板突然变成
   全宽、没有背景）；编辑工具可能给旧文件加 BOM，资源写盘后要检查/去掉 BOM（docs/02 已记）。
2. **九宫格**需要 `@spritesheet` 定义外框/内框 sprite，再 `decorator: ninepatch(a, b, 1.0) border-box`；
   只注册 decorator 不定义 sprite 会静默不显示。
3. **滑条**类型类名是 `input.range`（不是属性选择器），内部子元素是非 DOM 的
   `slidertrack`/`sliderprogress`/`sliderbar`，必须按 `input.range xxx` 作用域写样式，
   否则宽高/颜色全无。`GetValue()` 返回 `"88.000000"`，回显前要解析格式化。
4. **面板 `overflow: hidden` + 内部圆角 clip mask 的嵌套**：当前简化状态机（RmlUi 的
   mask 状态缓存 + 引用计数）下会出现部分内容整块消失（滑条/列表/九宫格）。主题改为
   面板不裁切（内容都有内边距），滚动列表自身的圆角裁剪正常；面板级嵌套 mask 留到 T5
   做状态机/离屏层时再解决。
5. 调试 stencil 时确认 bgfx/D3D11 的 backbuffer 是 D24S8；mask 绘制需带
   `BGFX_STATE_DEPTH_TEST_ALWAYS` 与 `setStencil(front, back)`，且每帧 `BGFX_CLEAR_STENCIL`。

---

## 2026-10-17 M9-T3 字体与文本

**字体映射**：`zl_ui` 字族注册 Noto Sans SC Regular 400 与 Bold 700；Bold 由 Windows 自带
`NotoSansSC-VF.ttf` 用 fontTools instancer 固定 `wght=700` 生成（`assets/fonts/NotoSansSC-Bold.ttf`，
OFL），并修正 name 表。主题字号收敛为 11/13/15/18/22 五级，避免每个字号各生成一套图集；
`b`/`strong`/`.weight-bold` 统一映射 `font-weight: bold`。

**文本能力**：Typography 面板演示五级字号与 400/700 字重、中英混排（`word-break: break-word`
+ 1.5 行高，正文自动换行）、单行省略号（`white-space: nowrap` + `overflow: hidden` +
`text-overflow: ellipsis`）、富文本颜色与内联加粗。

**图集预算**：`RmlBgfxRenderInterface` 记录每个字体图集页的尺寸/字节（`GenerateTexture` 路径），
`UiSystem` 在首帧渲染后统计并写入 F11 面板：当前 **8 页 / 0.7 MB / 预算 12 页 / 48 MB**；
超预算打 warning。CJK 全量字体保留覆盖，运行时字形按需光栅化；测试用 1500 个连续 CJK 字符
压测，图集 ≤2 页 / ≤8MB，验证不会一次性展开全字库。

**布局调整**：bgfx `dbgText`（M3 调试文本）固定在窗口左上且永远在 UI 之上，Typography 面板
放到左下（left 16 / top 400 / width 840），避免被日志遮住；F11 面板右侧原有控件面板保持不动。

**验证**：实机字号/字重、混排换行、省略号、富文本颜色/加粗；测试 **190 OFF / 196 ON** 全绿
（新增 M9-T3 字体映射/混排/省略号/富文本与 CJK 图集预算两个用例）。截图：
`ui_t3_final.png`、`ui_t3_crop.png`。

**踩坑**：
1. `font-weight: bold` 不会自动伪加粗，必须提供独立 Bold face；只加载 Regular 时 RmlUi 会
   回退到同一字重，视觉上没有变化。
2. 图集页数按 (family, style, weight, size) 组合增长；统一字号表能显著降低页数（10 页 → 8 页）。
3. 本版 RmlUi 的 `ComputedValues::font_family()` 在链接时未导出；测试字族映射改用
   `Element::GetFontFaceHandle()`，粗体通过 face handle 与 Regular 不同来断言。
4. 用 fontTools 实例化 VF 后 `name` 表仍显示 Regular，需要手动改 name ID 1/2/4/6/16/17，
   否则资源排查时容易误判。
5. bgfx DbgText 是最终 overlay，无法被 RmlUi 覆盖；调试面板布局要避开左上角，或临时用
   更大窗口验证。

---

## 2026-10-17 M9-T4 HUD

**HUD**：新增常驻 `assets/ui/hud.rml` / `hud.rcss`：顶部第 N 天 + 游戏时间 + 倍速
（暂停态红色 pill），左下生命/体力条（体力 = `1 - Needs.fatigue`），底部 5 格快捷栏
（数字键 1~5 选择，显示中文物品名/数量，选中黄框），拾取提示浮字
（`拾取了 XX ×N`，上浮 + 淡出）。`UiSystem::SetHudState` 缓存文本/条宽/槽位，只在变化时
更新 DOM；notice 每帧更新 `bottom`/`opacity` 实现浮动。

**输入互斥**：`Input` 增加 `SetGameplayBlocked`/`IsGameplayBlocked`，动作/按键/鼠标/滚轮查询
在阻断时返回 false；`Input::NewFrame` 重置。`UiSystem::Update` 在 RmlUi 有焦点时阻断游戏输入；
HUD 元素不可聚焦、调试面板隐藏时焦点为空，因此默认完全透传。F11 改为只切换调试面板
（默认隐藏，HUD 常驻），隐藏时清除焦点避免残留阻断。

**其他**：拾取提示改用中文并交给 HUD（移除 bgfx `dbgText` 的 notice 行，避免中文字体乱码）；
新增快捷键 `Hotbar1..5` 默认绑定 1~5；`WorldDemoState::RefreshHud` 每帧构建 HUD 快照。

**验证**：实机 HUD 时间走动、生命条随僵尸伤害变化（44/100）、体力 100、5 格快捷栏显示
绷带/消防斧与选中态；`hud.rml` 文档结构与输入阻断测试通过。测试 **191 OFF / 197 ON**。
截图：`ui_t4_final.png`。

**踩坑**：
1. RmlUi inline-block 的 `width` 是 content-box，5 个 64px 槽 + 边框 + 间距超过 344px 时
   第 5 格会换行（只剩数字角标可见）；容器宽度放到 360px 解决。
2. 调试面板默认可见会盖住 HUD（尤其左下体力条/底部快捷栏）；T4 改为 HUD 常驻、F11 才显示
   调试面板，既保持调试能力又给 HUD 干净布局。
3. `Input::NewFrame` 必须先重置阻断标记，再让 UiSystem 处理键盘并重新设置；否则输入框在
   焦点期间会收不到键，游戏也会永久禁用。
4. HUD 没有可聚焦控件，`Context::GetFocusElement()` 默认返回 null；不要用“HUD 可见”作为
   阻断条件，否则无焦点时也会抢输入。

---

## 2026-10-17 M9-T5 背包界面

**背包 UI**：新增 `assets/ui/inventory.rml` / `inventory.rcss` 与 `ui::InventoryPanel`：5×5
背包网格、主手/躯干装备槽、附近地面三栏；打开时隐藏 HUD 与 bgfx 调试文本，遮罩铺满全屏，
X/遮罩/I/ESC 关闭。HUD 右下角新增“背包 I”按钮，全部操作可只用鼠标完成。

**交互**：RmlUi `drag: clone` 拖拽；同定义合并、不同定义交换、拖到装备槽装备、拖到地面格
丢弃、地面物品拖回背包拾取；右键菜单按物品类型显示使用/装备/卸下/丢弃/拾取。`InventoryState`
与 `InventoryAction` 把 UI 快照、动作与游戏逻辑解耦；`WorldDemoState::RefreshInventory`
每帧构建快照，动作落地到 `MoveStack/TakeAt/UseAt/PickupGroundItem`，拾取/丢弃继续发
`OnItemPickup`/`OnItemDrop`。附近地面使用 `inventory.lootRange`（默认 3m，F 键拾取半径仍为
1.6m）。面板底部状态行显示“使用了 … / 装备了 … / 丢弃了 …”，避免 HUD 隐藏后没有反馈。

**输入与模态**：背包打开时 `SetGameplayBlocked(true)`，所有游戏动作/鼠标/滚轮查询失效；
关闭、玩家死亡/重进状态自动恢复 HUD 与输入。HUD 打开背包时隐藏，关闭时恢复，避免 HUD
和面板争抢点击。

**验证**：实机全鼠标流程：HUD 背包按钮 → 附近地面右键拾取 → 拖拽装备消防斧 → 右键使用
绷带（+15 HP）→ 拖拽装备到地面丢弃 → X 关闭；打开背包 FPS 52–61；1280x720 截图 +
1920x1080 RmlUi 布局断言。测试 **197 OFF / 203 ON**。截图：`ui_t5_final.png`。

**踩坑**：
1. RmlUi `Context` 析构会遍历 `EventDispatcher::DetachAllEvents` 并调用 listener 的
   `OnDetach`；只清 `unique_ptr` 不先 `RemoveEventListener` 会解引用悬空指针（测试在
   `Rml::RemoveContext` 处 SIGSEGV）。UI 的事件绑定需保存 element/event/listener 三元组。
2. 自制全屏遮罩必须最后 `PullToFront` 且打开时隐藏 HUD；否则 HUD 仍盖在遮罩上，看起来
   像“没变暗”。同时 bgfx `dbgText` 永远在 UI 之上，背包打开时必须跳过绘制，否则会遮住
   面板标题/装备槽。
3. 拖拽目标为空位时容器没有“空槽条目”，`MoveStack` 将 `to >= size` 统一视为移到末尾；
   背包网格只负责显示前 25 堆。
4. 地面物品按距离快照，UI 用 `StableId` 指回实体；拾取后实体延迟销毁（`Alive` 立即为假），
   下一帧快照自然移除。
5. 玩家在背包打开期间仍会被僵尸伤害；死亡会关闭背包并重新生成空背包玩家（实机测试时
   看到背包突然关闭，需检查是否死亡而非 UI bug）。

---

## 2026-10-17 M9-T6 主菜单与暂停菜单

**主菜单**：新增 `assets/ui/menu.rml` / `menu.rcss` 与 `ui::MenuPanel`，同一份文档承载
主菜单、暂停菜单和设置页。主菜单含新游戏/继续/设置/退出；启动默认
`game.startState=menu`，`MainMenuState` 将游戏时间暂停并显示菜单，新游戏通过 `AppState`
切换到 `WorldDemoState`。没有存档时“继续”置灰（M10 接入存档后可真正续档）。

**暂停与继续**：`WorldDemoState` 的 Esc 不再退出进程，而是暂停游戏并打开暂停菜单
（继续/设置/返回主菜单/退出）。返回主菜单用 **保留世界状态的 UI 覆盖层**，不销毁当前
WorldDemo 会话；“继续”恢复原世界与暂停前倍速（F1 已暂停时恢复为 1x）。打开菜单隐藏
HUD、bgfx 调试文本和世界调试叠层，关闭后恢复；菜单打开时 `SetGameplayBlocked(true)`。

**设置页**：主音量 0–100 + 10 项键位（移动/攻击/拾取/使用/装备/丢弃/暂停）点击改键，
按“应用”后通过 `Config::Set + Save` 写回 `config/game.json` 并调用 `ApplyInputBindings`
立即生效。`Config::Save` 用临时文件 + Windows `MoveFileEx(MOVEFILE_REPLACE_EXISTING)`
替换，失败不清空原文件。音量目前只保存配置，M11 音频系统接入后消费 `audio.masterVolume`。

**关键修复（M9-T4/T5 遗留）**：RmlUi 在没有显式焦点时 `Context::GetFocusElement()` 返回
**body**，不是 null；旧逻辑把 body 当焦点，导致 HUD/世界状态下所有键盘输入被永久阻断。
`UiSystem::HasInputFocus()` 现在把 body/html 视为无焦点，并在关闭背包/菜单时 `Blur()`
点击过的按钮 div。修复后 Esc、F1、WASD 等游戏输入恢复正常。

**验证**：实机 主菜单 → 新游戏 → 世界 HUD → Esc 暂停 → 设置页改键/应用（配置写入
`MoveUp=Q`，随后恢复）→ 返回主菜单（“继续”可用）→ 继续回到原世界（HP/时间保持）
→ Esc → Esc 继续；1280×720 菜单/暂停/设置截图。测试 **199 OFF / 205 ON**（新增菜单与 Config
Save 测试）。截图：`ui_t6_main.png`、`ui_t6_pause_final.png`、`ui_t6_settings.png`。
调试启动世界可加 `--game.startState=world`。

**踩坑**：
1. `GetFocusElement()==body` 被误判为输入焦点；这是 T4 “UI 无焦点透传”失效的根因，
   也解释了 T5 关闭背包后键盘/鼠标动作可能失效。
2. RmlUi 点击 div 会把焦点留在 div 上；隐藏文档不会自动清焦点，必须显式 `Blur()`。
3. bgfx `dbgText` 和世界 Debug 叠层在 RmlUI 之上/之下都要单独处理：菜单打开时分别跳过
   `DrawDebugText` 与调试浮层，否则暂停面板上会透出路径标记。
4. 设置写回的是完整 `m_data`（nlohmann JSON 键序会变成字母序）；如果只想改两项，建议
   M10 起改独立用户设置文件，避免 `config/game.json` 产生大 diff。
5. 暂停菜单保持世界状态属于有意的“覆盖层”设计，不是忘记切 AppState；切到
   `MainMenuState` 会销毁 WorldDemo，M10 存档前会让“继续”失效。

---

## 2026-10-17 M10-T1 序列化框架

**格式**：新增 `src/save/BinaryFormat.h`、`BinaryWriter`、`BinaryReader`。每段固定
16 字节小端段头：`magic:u32 + version:u16 + flags:u16 + payloadSize:u64`，写入时先占位，
`EndSegment()` 回填负载长度；`BinaryReader` 支持按段读取、版本/magic 校验、`SkipSegment()`
跳过未知段，以及 `EndSegment()` 检查段尾是否有未消费字节；顶层循环用 `AtEnd()` 区分
正常读完和段头截断。预置段 magic：`ZLSV`（存档头）、`ZLWD`（世界）、`ZLEN`（实体）、
`ZLPL`（玩家）。

**类型与限制**：基本整数/枚举（底层类型）/bool/float/double、UTF-8 字符串（u32 长度）、
`std::vector`、`std::array`、`std::map`、`std::optional`、`std::pair` 可递归序列化；容器
数量上限 100 万、字符串 16MB、单段负载 256MB，防止损坏长度导致超大分配。所有格式显式
按字节拼装，禁止直接写结构体（padding/ABI）。读取错误统一写入 `Error()`（包含偏移），
截断、非法 bool、超限、版本不匹配均测试为不越界、不抛异常。

**验证**：新增 `tests/test_serialization.cpp`，覆盖基础类型/中文 UTF-8/嵌套容器/多段长度回填/
未知段跳过/版本与 magic 拒绝/截断/恶意长度/非法 bool/段尾多余数据/API 状态错误。测试
**204/204 passed**（OFF，断言 118,769；ON 210/210）；下一步 M10-T2 世界 diff 存档。

---

## 2026-10-17 M10-T2 世界 diff 存档

**修改追踪**：`Chunk` 新增母地图基线（800 格状态哈希，含 roomId/持久层对象，Mark
临时层排除）与 `dirty`。非 const `Square()` 访问即置 dirty；`ChunkManager` 在
`ApplyChunkData` 之后、`Loaded` 之前调用 `CaptureBaseline()`。`IsSquareModified()`
用当前状态与基线哈希比较，因此即使修改发生在别处/改回原值，也不会误存或漏存。

**Diff**：新增 `save::ChunkDiff` / `ChunkDiffStore`。`CaptureChunkDiff` 只收集相对母地图
发生变化的格；每格保存 roomId + 4 个持久层的完整对象（sprite/offset/blocksMovement），
因此删墙、改地面、丢物品、改房间都能用同一 patch 表达。文件写入
`<saveRoot>/world/chunk_<x>_<y>.bin`（段 `ZLCD` v1）；无改动不写文件，旧 diff 会被删除。
`ApplyChunkDiff` 先校验坐标/格范围/重复格/Mark 层，再对目标格清空并恢复 patch。

**关键顺序**：读档必须 `ApplyChunkData → CaptureBaseline → ApplyChunkDiff`。diff 中的格
相对母地图仍然是“已修改”，所以下次保存会继续写回；若先应用 diff 再 capture，已有改动
会在下一次保存时丢失。T4 在 `onChunkUnloading`（数据清空前）调用 `SaveChunk`；T5 再接管
后台快照与原子写盘。

**验证**：新增 `tests/test_chunk_diff.cpp`，覆盖只捕获变化格、改回原值/Mark 不产生 diff、
编码往返、应用到母地图后再次 capture 仍有 diff、只写改动 chunk、未改动不落盘、负坐标、
损坏/截断文件、无基线、错误坐标、重复格与 Mark 层拒绝；`test_chunkmanager` 增加挂接后
基线断言。测试 **209/209 passed**（OFF，断言 118,859；ON 215/215），构建已恢复 OFF。

**踩坑**：`Chunk::Square` 非 const 是“可能修改”的入口，地图 apply 期间也会标 dirty，
必须在 apply 后统一 `CaptureBaseline()` 清除；只读路径（物理/寻路）都用 `const Chunk&`，
不会误标。`ChunkDiffStore` 当前直接写文件（T5 才做临时文件+改名与后台写盘）。

## 2026-10-18 M10-T3 实体与玩家持久化

**组件注册表**：新增 `save::ComponentRegistry`，组件用 u32 类型 ID + u16 版本注册，
codec 提供 has/write/read；内建 Position、Velocity、SpriteRef、Facing、Health、Needs、
PlayerTag、ItemInstance、Container、Equipment、Corpse。写入时按类型 ID 排序，保证快照确定性。

**实体快照**：新增 `save::EntityStore`。每个实体以 `StableId` 为键，快照段 `ZLEN` v1；
每个组件是嵌套标准段（可跳过未知类型，已知类型要求版本精确匹配，旧版本不静默迁移）。
`CapturePlayer` 要求全 world 恰好一个 `PlayerTag`；`Container` 保存容量、堆叠顺序与每件
defId/count/condition，`Equipment` 保存两个 optional 槽。`EcsWorld::CreateWithStableId`
用于读档重建并推进后续 ID 分配；`LoadEntities` 先建全部实体再应用，冲突/损坏时回滚本次创建。
T5 接线仍须为玩家/僵尸补物理、输入、AI、寻路等运行时组件，通用加载不是完整角色工厂。

**校验**：拒绝 StableId 为 0/最大值/重复、组件类型重复、组件版本不匹配、段头/长度不一致、
容器/物品/Health/Needs/Position 非法数值、截断与超限；未知组件跳过。测试覆盖玩家背包+装备
往返、Corpse 的 StableId 引用、全量快照排序、未知组件、重复 ID/组件、版本不兼容、非法物品、
截断、加载冲突回滚，以及 `CreateWithStableId`。**217/217 passed**（OFF，断言 119,050；
ON 223/223，断言 119,116），构建已恢复 OFF。

**踩坑**：EnTT 空组件（`PlayerTag`）的 view `.each` 回调只传 entity，不能声明第二参数；
模板 codec 也不能对空组件调用 `try_get`/`emplace(value)`，需 `if constexpr (is_empty_v<T>)`
分支。`CapturePlayer` 的查询因此单独处理。

## 2026-10-18 M10-T4 SaveManager + SQLite 元数据

**文件布局**：`SaveManager` 以 `save.root`（默认 `saves`）为根，每个存档目录包含
`meta.sqlite`、`save.bin`（段 `ZLSV` v1：gameVersion/mapId/createdAt）、`entities.bin`
（`ZLEN`）、`thumbnail.png`、`world/chunk_x_y.bin`。saveId 用 UTC 时间戳生成并只允许
`[A-Za-z0-9_-]`；所有 id 操作先校验路径，拒绝 `..`/分隔符。`Create` 可带玩家列表、
实体快照、RGBA 缩略图，失败清理半成品；`Save` 更新实体/缩略图/meta；`List` 按 updatedAt
倒序，`Delete` 删除整档；`ReadMetadata`/`ReadHeader`/`Load` 返回
`LoadResult{Ok,NotFound,Incompatible,Corrupt}`；`SaveChunk`/`LoadChunk` 委托
`ChunkDiffStore`。`SaveManager` 已挂到 `GameContext`，运行时存档 UI/请求接线留 T5。

**SQLite**：`meta.sqlite` schema `user_version=1`；单行 `meta` 表（save_id/name/
created_at/updated_at/save_version/game_version/map_id/thumbnail）和 `players` 表
（stable_id/name/is_local），写入走事务整体替换。旧 schema/存档段版本明确报不兼容；
损坏 DB 与 version=0 区分开，List 跳过并 warn。

**缩略图**：`Renderer` 实现 `bgfx::CallbackI::screenShot`，`requestScreenShot` 走平台
回读；`ConvertBgraToRgba` 处理 pitch/yflip；`stb_image_write` 编码 PNG，`SaveManager`
接收 `ThumbnailImage` 并写 `thumbnail.png`。主线程用 `Renderer::PollScreenshot` 取回，
回调队列有锁，不阻塞渲染线程。

**验证**：新增 `test_save_manager.cpp`（PNG、创建/列表/加载/删除、更新、chunk diff、
非法输入、损坏/版本、路径穿越）与 `test_screenshot.cpp`（BGRA 转换+ yflip/非法数据）；
测试 **224/224 passed**（OFF，断言 119,220；ON 230/230，断言 119,286），构建已恢复 OFF。

**踩坑**：`IsVersionError` 不能按“版本”二字判断（会把“读取元数据版本失败”误判为
Incompatible），改为匹配“不兼容”；SQLite `user_version=0` 按损坏处理。`Renderer` 持有
前向声明的截图回调 `unique_ptr` 时，构造/析构必须在 .cpp 定义，否则 GameApp 编译到
incomplete type。`EncodePng` 失败时不清空调用方 buffer，便于保留上一次结果。

## 2026-10-18 M10-T5 后台存档 + 原子提交 + 损坏恢复

**原子提交**：`SaveManager` 先写 `<root>/.tmp-<id>-<nonce>/`（更新档先复制旧 generation 的
world/chunk），全部 header/entities/thumbnail/meta/chunks 成功后，旧正式档整体 rename 到
`<root>/.backup/<id>/`，临时目录 rename 为正式档；rename 失败恢复旧档。`.tmp-*`/`.backup`
不是合法 saveId，`List` 忽略。`ChunkDiffStore` 单文件写改为 `WriteFileAtomic`（临时文件 +
`MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`）。

**损坏回退**：`Load`/`LoadChunk` 主档 Corrupt/NotFound 时尝试 `.backup/<id>`，Incompatible
不回退；成功返回 `LoadReport{usedBackup,warning}`，失败错误附“删除存档或使用备份”建议。
`List` 主目录损坏时列出 backup 元数据；`LoadChunkFromBackup` 供整档回退会话读取一致的
chunk diff。备份会话禁止回写旧主档，下一次保存新建 id，避免覆盖唯一可用备份。

**后台线程**：新增 `save::SaveService`，独占 `zl-save` 线程；主线程采集 `SaveRequest`
（EntitySnapshot/players/缩略图/ChunkDiff 副本）后投递，后台只调用 SaveManager，主线程每帧
`PollResults` 经 `GameContext.onSaveFinished` 回到当前状态。`SaveManager` 增加互斥锁，
所有列表/读写与后台写盘串行化；GameApp Shutdown 等 zl-save 收尾。

**运行时接线**：F12 快速存档（快照 → bgfx 截图 2s 超时则无缩略图 → 后台写盘）；主菜单
Continue 读最近存档；`WorldDemoState(loadId)` 读实体后补建玩家/僵尸的 Collider/RigidBody/
AI/寻路/输入等运行时组件。chunk 加载回调在基线与物理/寻路构建之间应用 diff；chunk 卸载
回调在数据清空前 SaveChunk。`save.autoSaveOnExit` 默认 true，退出时提交无缩略图存档。
失败经 HUD notice 提示；OnSaveBegin/OnSaveEnd 事件已发。

**验证**：新增 `test_save_service.cpp` 与 `test_save_manager.cpp` 的原子/备份/chunk 回退/
SaveOptions 用例；OFF **229/229 passed，119,331 assertions**，ON **235/235，119,397**，
构建已恢复 OFF。实机烟测：`--game.startState=world` 运行约 12s → F12（生成 thumbnail）→
Alt+F4；重启主菜单 Continue 读取同一档，恢复 51 实体（1 玩家 + 40 僵尸）并继续模拟，
退出 auto-save 更新主档且保留 backup。

**踩坑**：MSVC `std::filesystem::is_directory(nonexistent, ec)` 会设置 `ec`，新档提交误判
失败；先 `exists` 再查目录类型。`SubmitPendingSave` 不能在 move 后读 `m_pendingSave.id`。
UI 自动化点击要用 `ClientToScreen` 后坐标（窗口每轮位置不同）；截图超时/回读失败必须允许
无缩略图保存，不能卡住存档队列。

## 2026-10-18 M11 音频系统（T1~T4）

**设备与音量组**：新增 `audio/AudioEngine`（miniaudio 0.11.25，header-only），封装
`audio.device=default|null|off`、加载/播放/停止与 `Master → Music/SFX/Ambient` 四组音量。
`off`、设备初始化失败、测试 `null` 设备均安全 no-op；`GameApp` 在配置/存档服务之后初始化，
`SoundManager` 订阅 `OnSound`/`OnItemPickup`，退出时先退订再销毁 miniaudio engine。
音量组从 `audio.*Volume`（65/70/85/55）恢复，设置页应用时实时更新并 `Config::Save`。

**2D/3D 与声景**：听者取相机中心（`FromScreen(Camera::Center())`），
`ComputeSpatial` 做线性距离衰减 + 等距屏幕横向 pan；`SoundManager` 启动
`music_calm`（Music）与昼夜 `ambient_day/night`（Ambient）循环，僵尸低吼按听者附近存活
数量调节间隔（SFX），UI 点击/拾取为 2D。新增 `SoundEvent.clip`，脚步按地面 sprite 映射
`footstep_grass/dirt/wood`，步幅 `audio.footstepStride=0.9`；新增 11 个程序合成占位 wav
（见 `assets/audio/README.md`）。

**声音传播与 AI 共用**：新增 `core/SoundPropagation`（0.5 格采样、阻挡格计数、
`wallAttenuation` 有效半径/增益），`PerceptionSystem::HearsSound` 与音频
`ComputeSpatial` 调用同一实现；两边都读 `perception.wallAttenuation`。枪声/脚步只发
`OnSound`，音频订阅和感知订阅同时消费；低吼/环境/UI 不进入 AI 听觉。

**验证**：新增 `test_audio.cpp`（传播公式、pan/衰减、null 设备播放/停止、音量组、
OnSound/拾取/音乐/环境/低吼、`audio.device=off`）；UI 测试补四组音量应用与点击音效回调。
OFF **235/235 passed，119,381 assertions**，ON **241/241，119,447**，构建已恢复 OFF。
实机（默认设备）`--game.startState=world --game.startHour=8`：音乐/白天环境启动，W 行走
脚步计数增长，F7 枪声后音频 `枪声=1`、感知 `听` 从 6 跳到 32/55；`--audio.device=off`
静音运行且 FPS/模拟正常。

**踩坑**：`SoundManager.h` 只前向声明 `EventBus` 无法用于 `SubscriptionId` 成员，必须包含
`core/EventBus.h`；UI 设置面板从 1 行音量扩成 2×2 后，RmlUi 是 content-box（height 不含
padding/border），630px 会被 720 高度测试拒绝，改回 590px 并压缩行高/间距；miniaudio
无设备测试需 `noDevice + channels/sampleRate`，且每帧 `ma_engine_read_pcm_frames` 推进
声音节点。

## 2026-10-18 M12 打磨与联机命令层预留（T1~T4，可选范围）

**命令层**：新增 `net/Command`（type/payload/tick/playerStableId，实体引用只用 StableId）、
`CommandCodec`（小端定长包 `ZLC1` v1，单包 ≤64 条/4KB）、`LoopbackTransport`/`Client`/
`Server` 与 `CommandPipeline`。`net.mode=direct` 立即执行；`loopback` 在渲染帧末 Flush
编码发包，下一 tick 开头 Pump 解码执行并回 `ZLS1` 心跳快照（移动/攻击/拾取/使用/装备/
丢弃/背包动作/开枪/快捷栏全部命令化；暂停/相机/存档/调试仍为本地元操作）。`MoveInput`
（屏幕方向）由 InputSystem 转 MoveIntent，应用不再直接读键盘。

**命令接线踩坑**：命令必须在 GameApp 的 Tick 开头、ECS 之前 Pump，否则 loopback 会多等
一帧；Server 无命令时也要发心跳快照，否则统计把“最后一次命令”误判成当前延迟；UI 的
背包 action 在 direct 下仍立即执行，loopback 下下一 tick 生效，UI 到下一帧才刷新是预期。

**性能**：地面 stage 0 按纹理聚簇（不改变画家序）后，placeholder 场景 drawcall 244→71；
`ChunkManager` 增加 `world.chunkAttachBudgetMs=2.0` + `chunkAttachBudget=8` 双预算（首块
总执行，防饥饿）；启动耗时在 `GameApp::Run` 记录。Release 500 人口 + 大世界巡航 60s：
direct/loopback FPS 中位均 60（direct 最低 55、loopback 57），loopback 差异 <10%，命令
最多 1 tick 延迟；启动 461ms。

**内容管线**：`tools/pack_tileset.py`（PNG 目录 → shelf 图集页 + pz-atlas-v1 JSON，可选
`texturec` 生成发布 DDS）；`assets/items.json` 7→14 条，新增水/止痛药/能量棒/锤子/
螺丝刀/登山靴/登山背包及 7 个自制图标，asset_manifest 全走数据文件。Tiled 导入器与
配方系统当时尚未建模，二者已在后续扩展完成（Tiled 见 2026-10-21 条目）。

**验证**：`test_net.cpp`（协议全字段/非法包/快照往返/直连环回一致性/背压）、
`test_player_control`（MoveInput 与键盘一致、地面批纹理排序）、`test_items` 加载 14 条。
OFF **243/243 passed，119,550 assertions**，ON **249/249，119,616**，构建已恢复 OFF。
实机：`--net.mode=loopback` + W 移动 + F7 枪声 → 命令 3/3 执行、包 3/3、tick 延迟 1，
枪声后感知听计数上升；`placeholder_pz` drawcall 71、FPS 60。
## 2026-10-18 M0 换机验收预验（干净目录）

**目的**：M12 后只剩 M0“换台机器按 README 三步能跑起来”；本机先做一次排除构建产物的干净目录复验。

**结果**：把工作树复制到临时目录（排除 `build/.git/logs/saves/assets_placeholder_pz`），按 README 用 `VCPKG_ROOT=E:\Dev\vcpkg` 配置/构建/运行。vcpkg 命中二进制缓存，配置 100s；构建 203 个目标成功；`ZomboidLikeTests` **243/243、119,550 assertions**；启动窗口创建（hwnd 非 0），日志显示物品 14 条、音频设备、2 个示例 mod、UI 字体、进入主循环。

**顺手修正**：README 的 `vcpkg version` 在未加 PATH 时会失败，改为 `& "$env:VCPKG_ROOT\vcpkg.exe" version` 并给出 `VCPKG_ROOT` 设置命令；新对话提示里的绝对项目路径改为 `<项目根目录>（本机 …）`；快速开始注明首次依赖安装时间。

**剩余**：真正换一台 Windows 机器执行 README 三步；本机干净目录不能替代该验收，docs/01 仍未勾选。

## 2026-10-19 Debug 240 敌人帧率排查与修复

**现象**：Debug 构建人口增长到 240 live 后 FPS 长期 30（原 60），Release 正常。

**定位**：新增 `perf` 帧内分段日志与 SystemScheduler 系统耗时日志。240 敌人时
`CollectWorld` 单帧 ~10ms，系统总耗时 ~12ms；Debug 日志 600+ 行/s 且 `flush_on(debug)`
每行强制刷盘。Debug 帧耗时落在 17~25ms 时 vsync 直接减半到 30。

**修复**：
- `WorldDemoState::PushObject` 缓存相邻逻辑名的 `Frames` 指针；静态地面/对象 5k 次
  `unordered_map<string>` 查找降为几十次，世界收集 10ms → 2.5ms。
- `Chunk` 维护 `NonEmptyZMask`（载入/GetOrCreateSquare/ChunkDiff 写入时置位），
  CollectWorld 跳过空 z 层。
- 寻路成功/失败、逐 chunk 状态迁移日志从 debug 降为 trace；Debug 构建改为
  `flush_on(info)`（info 统计每 3s 落盘），避免高频 debug 日志逐条 fsync。
- 保留 `perf` 帧内耗时与系统耗时日志，后续可直接观察 CollectWorld/排序/系统预算。

**结果**：Debug 240 live 从 25~31 提升到多数 45~64（偶发 vsync 临界帧 30）；Release 240 live
稳定 60。OFF 测试 243/119554，Release 243/119555。Debug 仍不是性能构建，
日常游玩建议 `x64-release`。

## 2026-10-20 工作台制作范围

**实现**：
- `recipes-v3` 新增可选 `station`：世界对象逻辑名 `object.<station>` 标记站点（内置 `workbench` → `object.workbench`）；`RecipeRegistry` 兼容 v2/v1，v2 出现 station 会明确拒绝；station 做长度/字符集校验。
- `game::HasStationNearby` 按格中心距离扫描已加载 chunk 的 furniture/item 层；`crafting.stationRange` 默认 2.5 格（0~10 可调）。
- 制作开始与计时完成都检查工作台；离开范围后完成失败且不扣材料。背包快照 `recipes` 新增 `station/stationLabel/stationNearby`，`canCraft` 同时要求材料、工具、空间与工作台；Lua/Python `Inventory.GetState()` 同构。
- `core_ui` 配方行显示“缺工作台”状态与“需要附近工作台”说明；village_demo 出生点旁放置工作台（`object.workbench` 占位帧，新增自制图集帧）。
- `docs/mapformat.md` 记录站点用 sprite 逻辑名约定的方式，`docs/01/02/modding` 同步。

**验证**：Debug OFF **267/267（120028 assertions）**、Debug Python **276/276（120116）**、Release Python **276/276（120117）** 全绿。Debug Python 首跑路径敏感 A* 5.7ms 超 5ms，复跑 3.7ms 通过（既有负载敏感项）。实机 Lua/Python 用临时 mod 发放 `jacket×1 + rag×2 + hammer×1`：远处配方 `station=workbench near=false can=false`，C++ 拒绝“需要附近工作台”；传送回出生点工作台旁后 `near=true can=true`，开始 3.00s 制作并成功，背包为 `锤子x1,加固夹克x1`；Lua/Python 同构。截图确认“缺工作台”状态、制作中进度与工作台/加固夹克；临时 mod、junction、pycache 已清理。未提交。

## 2026-10-20 多制作站与篝火

**实现**：
- `crafting.stationLabels` 配置制作站显示名（`workbench`/`campfire`），`WorldDemoState::StationLabel` 统一解析并在缺省时回退 station id；`core_ui` 的缺站状态/提示从硬编码“缺工作台”泛化为“缺<stationLabel>”。
- village_demo `chunk_7_2` 增加 `object.campfire`（局部 `(4,5)`，世界 `(74,25)`，solid）；自制图集新增 `campfire_1` 帧。
- 新增 `cooked_beans`（烤豆子，饥饿 0.5）与 `campfire_beans`（罐头豆子→烤豆子，station=campfire，2.0s）；物品 17→18、配方 3→4。
- 更新 `docs/01/02/modding/mapformat`；背包 UI 仍展示前 4 条。

**验证**：Debug OFF **267/267（120048 assertions）**、Debug Python **276/276（120136）**、Release Python **276/276（120137）** 全绿；Debug Python 本轮 A* 3.5ms。实机 Lua/Python 在出生点：`campfire_beans:篝火 near=false can=false`，C++ 拒绝“需要附近篝火”；传送到 `(73.5,25.5)` 后 `near=true can=true`，2.00s 制作成功，背包出现 `烤豆子x1`；同一截图可见 4 条配方，玩家在篝火旁时“加固夹克”显示“缺工作台”，篝火配方显示材料状态。临时 mod、pycache 已清理，Debug 恢复 OFF、Release 保持 Python ON；未提交。

## 2026-10-20 配方蓝图解锁

**实现**：
- `items-v1` 新增可选 `unlocksRecipes` 数组（1~32 个配方 id）；`magazine_cooking`（烹饪杂志，maxStack=1）解锁 `campfire_beans`，出生点附近增加散落点；`RecipeRegistry` 交叉校验未知配方引用。
- 被任意物品引用的配方默认未学会；`WorldDemoState` 构建锁定集，玩家 `KnownRecipes` ECS 组件记录已学会 id（排序去重），`UseBlueprintItem` 消耗 1 件并学习，已全学会不消耗。`KnownRecipes` 注册为存档组件，旧存档缺失时回补空组件；开始/完成制作都校验已知状态。
- `Inventory.GetState().recipes[]` 新增 `known`，`core_ui`（Lua/Python）显示“未学会”和“阅读对应配方杂志解锁”，未学会时禁用制作；物品 `unlocksRecipes` 使右键“使用”可用。物品 18→19、图集 44→45 帧，更新 `docs/01/02/modding`。

**验证**：Debug OFF **269/269（120079 assertions）**、Debug Python **278/278（120167）**、Release Python **278/278（120168）** 全绿；Debug A* 4.9ms（<5ms）。`test_entity_save` 覆盖 `KnownRecipes` 两个 id 的存档往返。实机用 `mods_tmp`（仅 core_ui + 测试 mod）验证 Lua/Python：初始 `campfire_beans` 为 `known=false can=false`，背包有烹饪杂志；`Inventory.Action(use)` 后变为 `known=true can=true` 且杂志数量 0；在篝火旁 2.00s 制作成功，最终背包有 `烤豆子x1`。截图确认锁定行“未学会/阅读对应配方杂志解锁”，以及学会并制作后的 4 条配方/烤豆子。临时 mod、junction、pycache 已清理，Debug 恢复 OFF、Release 保持 Python ON；未提交。

## 2026-10-20 批量制作

**实现**：
- `Inventory.Action({type="craft", count=N})` 支持 1~99 件批量请求；命令 `payload.flags` 高 8 位携带件数（0 兼容旧命令），Lua/Python 均解析并钳制。
- 新增 `items::MaxCraftCount`：在容器副本上连续模拟制作，按材料、工具磨损/损坏和输出空间截断实际批次；请求超过上限时自动缩减，不修改真实背包。
- `WorldDemoState` 以同配方逐件队列推进：每件计时完成才原子扣料，随后重算工具效率/耗时并开始下一件；中途缺料、工具、制作站或空间时停止剩余批次并保留已完成产物。取消只丢弃当前计时件。
- 快照新增 `crafting.batchCount/completedCount`；`core_ui`（Lua/Python）在制作标题旁增加数量 `- N +`，进度显示“制作中: xxx ×N（当前/总数）”，支持取消剩余批次。更新 `docs/01/02/modding`。

**验证**：Debug OFF **270/270（120096 assertions）**、Debug Python **279/279（120185）**、Release Python **279/279（120186）** 全绿；`MaxCraftCount` 测试覆盖 5 破布→2 份绷带、工具即将损坏仍只允许 1 次且不修改原容器。实机 Lua/Python 用 `mods_tmp`（core_ui + batch_test）在篝火旁验证：界面数量调到 5 后点击制作，C++ 日志为“请求 5 件，实际 3 件”，批次 `batch=3`；每 2.00s 完成一件，`done` 依次 1→2→3，最终背包 3 个烤豆子、3 个罐头全部消耗、`active=false`；截图确认数量控件、`1/3` 进度、取消按钮与完成提示。临时 mod、junction、pycache 已清理，Debug 恢复 OFF、Release 保持 Python ON；未提交。

## 2026-10-20 跨配方制作队列

**实现**：
- `Inventory.Action({type="craft"})` 空闲时立即开始；已有任务时追加到 FIFO 制作队列（最多 8 项，可跨配方）。排队只校验已学会与制作站范围，材料/工具/空间在队首真正开始时重新校验，请求件数按当时上限截断。
- `WorldDemoState` 抽出 `StartCraftBatch` / `StartNextQueuedCraft`；当前批次完成后自动开始队首并播报；任一批次中断或 `cancel_craft` 清空剩余队列，已完成产物保留。
- `InventoryCrafting.queue` 暴露后续队列 `{recipeId, outputLabel, outputCount, count}`；`core_ui`（Lua/Python）制作中显示“加入”按钮、队列预览与“取消全部”。队列仅存在运行期，不写入存档。更新 `docs/01/02/modding`。

**验证**：Debug OFF **270/270（120096 assertions）**、Debug Python **279/279（120185）**、Release Python **279/279（120186）** 全绿；Lua/Python 快照测试覆盖 `crafting.queue` 字段。实机用 `mods_tmp`（core_ui + queue_test）在篝火旁先制作 2 份绷带，再排队 1 份烤豆子：日志 `QUEUE queued ... active=true batch=2 queue=1`，随后 `QUEUE state ... batch=1 queue=0`，最终 `QUEUE final active=false bandage=2 cooked=1`；截图确认“加入”按钮、`队列: 烤豆子×1`、`2/2` 进度、`取消全部` 与完成后的背包。临时 mod、pycache 已清理，Debug 恢复 OFF、Release 保持 Python ON；未提交。

## 2026-10-20 制作队列编辑

**实现**：
- `InventoryActionType` 新增 `QueueMoveUp / QueueMoveDown / QueueRemove`，Lua/Python `Inventory.Action` 支持 `queue_up / queue_down / queue_remove`；`fromIndex` 指向 `crafting.queue` 的 0-based 下标，命令管线同步校验类型边界。
- 调序只交换相邻队列项；移除只删除指定后续项；越界、队首上移、队尾下移均拒绝并提示。编辑不取消当前制作、不扣除/返还材料；本轮调序/移除不修改队列项请求件数。
- `core_ui`（Lua/Python）把原队列摘要行升级为选中编辑行：`队列 N/M: 配方×请求数`，`<` / `>` 切换选中项，`↑` / `↓` 调序，`×` 移除；空队列显示“队列: 空”，首尾按钮按边界禁用。选中项为 UI 本地状态，队列缩短后自动钳制。
- 更新 `docs/01/02/modding`；`docs/modding` 补充三个队列动作与选中预览语义。

**验证**：Debug OFF **270/270（120100 assertions）**、Debug Python **279/279（120193）**、Release Python **279/279（120194）** 全绿；Lua/Python 脚本测试覆盖三个新动作的解析与 `fromIndex`。实机 Lua/Python 用 `mods_tmp`（core_ui + queue_edit_test）在篝火旁验证：初始队列 `绷带,烤豆子`，`queue_down` 后为 `烤豆子,绷带`，`queue_remove` 后剩 `绷带`，再入队并 `queue_up` 后为 `烤豆子,绷带`；最终 `bandage=1 cooked=2 queue=0 active=false`，制作顺序与编辑结果一致。截图确认队列编辑行、选中位置、调序/移除按钮；临时 mod、pycache 已清理，Debug OFF、Release Python ON；未提交。

## 2026-10-20 队列请求数量调整

**实现**：
- `InventoryActionType` 新增 `QueueSetCount`，Lua/Python `Inventory.Action` 支持 `queue_count`；`fromIndex` 为队列下标，`count` 为新的请求件数（1~99）。动作只修改后续队列项数量，不影响当前制作、材料、工具或队列顺序；真正开始该项时仍按材料/工具/制作站/输出空间重新校验并截断。
- `core_ui`（Lua/Python）队列编辑行增加 `−` / `+` 按钮，按选中项请求件数调整，1 和 99 边界禁用；行摘要继续显示 `队列 N/M: 配方×请求数`。提示补充“−/+ 改数量”。
- 更新 `docs/01/02/modding`；`docs/modding` 补充 `queue_count` 语义与数量调整不影响当前任务/材料。

**验证**：Debug OFF **270/270（120102 assertions）**、Debug Python **279/279（120197）**、Release Python **279/279（120198）** 全绿；Lua/Python 脚本测试覆盖 `queue_count` 的 `fromIndex/count` 解析。实机 Lua/Python 用 `mods_tmp`（core_ui + queue_count_test）在篝火旁启动 2 份绷带、排队 1 份烤豆子后将请求改为 2：C++ 日志 `调整制作队列第 1 项数量：烤豆子 ×1 请求 1 -> 2 件`，随后 `开始队列下一项：烤豆子 ×1（请求 2 件，实际 2 件）`；最终 `bandage=2 cooked=2 beans=1 queue=0 active=false`。截图确认队列行 `队列 1/1: 烤豆子×2` 与 `−/+` 按钮；临时 mod、pycache 已清理，Debug OFF、Release Python ON；未提交。

## 2026-10-20 制作队列持久化

**实现**：
- 新增玩家 `CraftingJob` ECS 组件与存档组件 `CRFT` v1：活跃批次 `recipeId/elapsed/duration/toolEfficiency/batchTotal/batchCompleted` 及后续 FIFO 队列 `{recipeId,count}`；上限 8 项、请求数 1~99，写入/读取均校验状态一致性与浮点范围。
- `RequestQuickSave` 采集实体前调用 `SyncCraftingJobToEcs`；`RestoreCraftingJobFromEcs` 在实体内存恢复后重设计时/批次/队列，并重新校验配方存在与已学会，失效时取消任务并清空队列；制作站/材料留到继续计时完成或队首开始时校验（读档瞬间 chunk 可能尚未加载）；未知配方队列项跳过并记录存档警告。旧存档缺少组件时补建空任务。
- 更新 `docs/01/02/modding`；解除此前“队列仅运行期、读档丢弃”的限制。

**验证**：Debug OFF **271/271（120123 assertions）**、Debug Python **280/280（120218）**、Release Python **280/280（120219）** 全绿；`test_entity_save` 覆盖 `CraftingJob` 活跃进度/批次/队列往返，以及非法批次与超长队列拒绝。实机 Lua 用临时 `persist-mods-*` 与隔离 `save.root`：先制作 2 份绷带并排队 2 份后 `Save.QuickSave` 快存（`CraftingJob` 写入 `entities.bin`）；重启从主菜单 `Save.Load`，日志 `读档恢复制作：绷带 ×1（配方 field_bandage，进度 0.2/0.8, 已完成 0/2, 队列 1 项）`，恢复后完成当前批次并自动开始队首，最终 `bandage=4 active=false queue=0`。截图确认恢复后“制作中: 绷带 (2/2)”与完成后 4 份绷带；临时 mod、存档、pycache 已清理，Debug OFF、Release Python ON；未提交。

## 2026-10-20 配方列表分页

**实现**：
- `core_ui` 背包制作区固定 4 行改为分页：工具栏显示 `N 条 P/Q 页` 与 `‹`/`›`，页码越界时自动钳制；`craft-btn-N` 按当前页映射到 `recipes[(page-1)*4+N]`，避免第 5 条及之后的配方不可见或点错。
- Lua/Python `main` 同构实现 `craftPage` 状态、页码计算、上一页/下一页点击与按钮禁用；RML 增加 `craft-page-prev/count/indicator/next`，RCSS 增加分页按钮皮肤；提示补充“配方多时用 ‹ › 翻页”。
- 更新 `docs/01/02/modding`，勾选已完成且由 M9 覆盖的 `M6-T4`。

**验证**：临时 `recipes5.json`（5 条配方）+ `page-mods`（core_ui + page_auto）分别以 Lua/Python 后端启动世界，自动打开背包并加 4 份破布；点击 `›` 后截图显示 `5 条 2/2 页`，第 2 页仅显示第 5 条配方；再点击该页“制作”，C++ 日志为 `开始制作：绷带 ×1（配方 field_bandage_alt...）`，确认分页索引映射正确；Lua/Python 均通过，随后 `制作成功：绷带 ×1（配方 field_bandage_alt，批次 1/1）`。临时 mod、存档、日志、截图已清理；未提交。

**补充验收**：同一套临时环境用篝火配方 `campfire_test_bandage`（`time=6.0`）在 `(73.5,25.5)` 启动制作并快存，快照记录进度 0.033/6；重启读档日志 `读档恢复制作：绷带 ×1（配方 campfire_test_bandage，进度 0.033333335/6, 已完成 0/1, 队列 0 项）`，恢复后 `STATION restored active=true ... elapsed=0.07`，6 秒后 `制作成功`、`STATION final active=false queue=0 bandage=1`，确认制作站校验延迟到 chunk 加载后不会误取消。

## 2026-10-21 联机基础 M13-T1：KCP 传输与帧协议

**实现**：
- vcpkg 接入 kcp 2.1.1（MIT），新增 src/net/Kcp.{h,cpp}：Windows 非阻塞 UDP socket、KCP 客户端/服务器、多客户端会话（conv 键）、服务器端口 0 自动分配、会话超时和最大连接数、收发统计；KCP 参数为低延迟模式（nodelay=1, interval=10ms, resend=2, nc=1）。
- 新增 src/net/FrameCodec.{h,cpp}：8 字节长度前缀帧（magic ZLF1 + u32 payload size），处理 KCP 流式粘包/拆包，单帧上限 64KB；非法 magic/超长帧清空解码器并报错。
- 新增 tests/test_kcp.cpp：localhost 收发、服务器回包、8KB 大帧分片、双客户端隔离、非法帧丢弃后恢复、会话超时、未连接/解析失败错误路径；CMake 为应用与测试链接 kcp::kcp + ws2_32。
- M13 任务清单已建立；T1 尚未接入游戏主循环，CommandPipeline 继续使用 loopback。

**验证**：Debug **277/277（120202 assertions）**、Release **277/277（120203 assertions）** 全绿；--test-case="M13*" 6/6 通过。未提交。

## 2026-10-21 联机基础 M13-T2：会话协议与握手

**实现**：
- 新增 src/net/SessionProtocol.{h,cpp}：ZLM1 会话消息（Hello/Welcome/Reject/Disconnect/Data/Ping/Pong）；协议版本、magic、类型、长度、文本长度和 server sessionId 校验；Hello 带 client nonce + 内容 hash，Welcome 回 SessionId/serverTick/content hash/heartbeat。
- 新增 src/net/KcpSession.{h,cpp}：KcpSessionClient 状态机（Disconnected/Connecting/Connected/Rejected）、KcpSessionServer SessionId 分配与会话表；内容 hash 不一致、协议版本不匹配、服务器满、握手/会话超时都返回明确原因；Ping/Pong 心跳保活，握手完成前不产生 Connected 事件。
- 会话帧统一经 FrameCodec 再交给 KCP 传输；新增 KcpServer::Disconnect。所有错误路径不崩溃，客户端可读取 LastError/LastReason。
- 新增 tests/test_kcp_session.cpp：协议往返/版本/magic/长度校验、握手、双向 Data、内容 hash 拒绝、服务器满、协议版本拒绝、心跳保活、客户端断开、客户端/服务器握手超时；更新 M13 清单。

**验证**：Debug **286/286（120306 assertions）**、Release **286/286（120307 assertions）** 全绿；主程序 Debug 链接通过。尚未接入游戏主循环；未提交。

## 2026-10-21 联机基础 M13-T3：Headless ServerHost

**实现**：
- 新增 game::WorldSimulation：把世界加载、chunk 流式加载、物理、寻路、AI/感知/LOD/Boids/刷怪、战斗/死亡、ECS 固定链从 GameApp 抽出；只依赖 Config/EventBus/TaskScheduler/SaveManager，不依赖窗口/渲染/UI/输入/音频。
- GameApp 改为持有唯一 WorldSimulation 并把指针挂到 GameContext；WorldDemoState 的玩家生成/演示人口改走同一模拟，避免单机与服务器两套实体构成。
- 新增 server::ServerHost：无窗口组装物品/配方/存档、加载地图、固定 tick 驱动模拟，支持 --server 独立进程与 --server.ticks=N 冒烟/CI 退出；Ctrl+C 请求停止。支持可选读档。
- 新增 tests/test_server_host.cpp：无窗口初始化、系统链数量、chunk 加载、玩家生成、MoveInput→Velocity 推进；无效配置失败不崩溃。

**验证**：Debug **288/288（120322 assertions）**、Release **288/288（120323 assertions）** 全绿；ZomboidLike --server --server.ticks=180 无窗口运行并正常退出。单机命令/复制尚未接入，未提交。

## 2026-10-21 联机基础 M13-T4：实体复制层

**实现**：
- 新增 src/net/Replication.{h,cpp}：`ZLR1` v1 快照协议（32B 头 + 实体/组件记录），支持 Full/Delta、StableId/archetype/owner、组件级负载、移除列表；`ReplicationRegistry` 按 type/version/maxBytes 注册组件，编解码严格限制单包 60KB、4096 实体、32 组件/实体，并拒绝未知/重复组件、重复 StableId、非法 baseTick 与尾部垃圾。
- 新增 src/game/EntityReplication.{h,cpp}：登记 Position/Velocity/Health/SpriteRef/Facing/AnimState；producer 只读 ECS，按 AOI 半径/最大实体数抓取玩家和有 Health 的角色，维护每客户端全量基线，增量只发变化组件、新实体、AOI 离开/销毁移除，meta（owner/archetype）变化时发完整实体。
- 客户端副本按 StableId 应用 Full/Delta，校验 baseTick 并标记 NeedsFullSnapshot；远程位置支持 previous/current 插值；本地玩家支持输入积分轻量预测，服务器快照按误差阈值吸附或平滑校正。新增 `ecs::NetworkOwner`，玩家生成时 owner=自身 StableId。
- 新增 tests/test_replication.cpp：组件注册/全量往返、增量组件 diff、AOI 离开/返回、实体销毁移除、错误基线、损坏组件、重复/未知组件、插值与本地预测；ServerHost 测试补充玩家 NetworkOwner 校验。

**验证**：Debug **293/293（120459 assertions）**、Release **293/293（120460 assertions）** 全绿；`--test-case="M13 Replication*"` 5/5、135 assertions 通过。复制层尚未接入 ServerHost/GameApp 主循环（T5）；未提交。
## 2026-10-21 联机基础 M13-T5：双客户端垂直切片

**实现**：
- 新增 src/net/PlayerAssignment.{h,cpp}：`ZLA1` 玩家分配消息（sessionId/playerStableId/serverTick），保持 T2 Welcome 线格式不变；服务器在首个全量快照前通过 Data 通道发送。
- 新增 src/game/NetworkClient.{h,cpp}：封装 KcpSessionClient + ZLC1 命令编码 + ZLR1 快照应用；支持内容 hash、玩家分配、命令序列、自动重连、本地预测；断开/解码/应用失败会清空副本并重连。
- ServerHost 接入 KcpSessionServer：每 SessionId 生成权威玩家（`WorldSimulation::SpawnNetworkPlayer`）与独立 EntityReplicationProducer；连接后先分配玩家再全量，后续每 tick 增量；发送失败重置基线并在下一 tick 重发全量；断线销毁玩家，剩余客户端收到 removal。
- ServerHost 执行移动/点击移动/攻击命令：校验会话 playerStableId 与包序列，Move 写 MoveInput，MoveTo 写 PathRequest，Attack 选择最近/指定僵尸写 AttackIntent，由 CombatSystem 扣血；不支持的 UI/背包命令明确拒绝。
- `net.mode=client` 时 GameApp 跳过本地 WorldSimulation::Tick，WorldDemoState 把复制 Actor 同步为渲染用 ECS 实体、更新 SpatialIndex、相机跟随、本地输入预测与服务端校正；F6/快速存档在客户端禁用。服务器默认 `--server` 监听 UDP，`--server.network=false` 关闭。
- 新增 tests/test_network_integration.cpp：localhost 双客户端加入、互相可见、A 移动 B 看到位置变化、攻击僵尸 B 收到 Health 变化、B 离开后服务器清理且 A 收到 removal、新客户端重连获得新 StableId 并看到 A。

**验证**：Debug **294/294（120488 assertions）**、Release **294/294（120489 assertions）** 全绿；`--test-case="M13-T5*"` 1/1、29 assertions 通过（连续 3 次无抖动）。额外运行 `ZomboidLike --server --server.ticks=180` 网络服务器正常退出；图形客户端连接服务器后成功分配 StableId、相机跟随玩家且无错误日志。未提交。

## 2026-10-21 联机基础 M13-T6：命令安全与容错

**实现**：
- ServerHost 增加会话级命令策略：每 tick 最多 `net.maxCommandsPerTick`（默认 8）条命令；Attack 使用服务器 tick 冷却（`net.attackCooldownSeconds`，默认 0.35s）；`MoveTo` 目标距玩家超过 `net.maxMoveToDistance`（默认 64 格）拒绝；Attack 自动瞄准/指定目标距离超过 `net.maxAttackDistance`（默认 3 格）拒绝；快捷栏索引限定 0..4；所有命令继续校验会话 `playerStableId`。
- 命令入口对未登记会话、超 4KB 命令包、magic/长度/类型/序列非法包只拒绝并累计统计，不断开仍可工作的会话；未生成权威玩家时命令不执行。
- 新增 `ServerHost::NetworkStats`：数据包、畸形/超大/过期包、命令收/受/拒、限频/冷却/越权/距离/未实现、分配/快照成功与失败；`LogStatus` 每 5s 输出服务器网络汇总（含 KCP 数据报）。
- `NetworkClient` 暴露握手/心跳/重连参数（`net.handshakeTimeoutMs/heartbeatIntervalMs/heartbeatTimeoutMs`），服务器重启后自动清空副本、重连并等待新分配。
- 新增 tests/test_server_security.cpp：越权 StableId、超距 MoveTo、单包 12 条命令触发限频、连续两次 Attack 触发冷却、畸形包/超大包后会话仍存活；test_network_integration 增加延迟 150ms 命令与服务器同端口重启后的自动重连；test_kcp 增加客户端→服务器每 3 个 UDP 数据报丢 1 个的本地丢包代理，大帧经 KCP 重传完整送达。

**验证**：Debug **297/297（120548 assertions）**、Release **297/297（120549 assertions）** 全绿；`--test-case="M13-T6*"` 2/2、52 assertions 连续 3 次通过。未提交。

## 2026-10-21 M0 换机验收预验（M13-T6 快照）

**目的**：M13-T6 完成后再次验证 README「快速开始」三步，排除本机 build/logs/saves 等产物后的源码快照可独立配置、构建、运行。

**结果**：干净目录（排除 `build/.git/logs/saves/assets_placeholder_pz`）配置 115.9s（vcpkg 命中缓存）→ 构建 241/241 → `ZomboidLikeTests` **297/297（120548 assertions）**；启动 `ZomboidLike.exe` 6s，窗口句柄非 0、主菜单/UI/4 个 mod/音频/主循环日志正常。

**剩余**：真正的「换另一台 Windows 机器」验收仍需外部机器；本机干净目录不能替代 docs/01 的 M0 最后一项。
## 2026-10-21 M13-T7 服务器权威背包/世界交互

**目标**：把拾取、使用、装备、丢弃、维修、制作与枪声从“客户端本地执行”改成“服务器执行命令、客户端只发命令并展示 owner-only 状态”。

**落地**：新增 `game::PlayerInventoryService`（拾取距离/容器/装备/维修/制作批次与 FIFO 队列，制作状态存 `CraftingJob`）；`ServerHost` 接管 8 类背包/世界命令、每 tick 推进制作、枪声事件与网络统计；`ZLP1` 只向玩家所属会话下发背包/装备/已学配方/制作/提示，客户端镜像到本地视觉玩家；`ZLR1` 新增地面物品 `ItemInstance` 组件与 item archetype，丢落/拾取对所有客户端可见；服务器出生点复用单机演示散落物。

**验证**：`ZLP1`/`ZLR1` 严格编解码与截断测试、背包动作/制作单元测试、localhost 拾取→使用→丢弃→装备→制作→状态同步集成测试；Debug **304/304（120681 assertions）**、Release **304/304（120682 assertions）** 全绿。仍未提交；M0 跨机验收仍待另一台 Windows 机器。

**风险/后续**：T8 已补玩家断线与服务器重启持久化，但离线玩家不留在世界中、也没有掉包规则；装备外观仅复制装备槽状态，远程客户端还没有完整装备挂点（T10 已补图标/动作占位表现）；网络模式枪声目前只在服务器影响 AI，客户端本地音效反馈可后续补齐。

## 2026-10-21 M13-T8 联机玩家持久化

**目标**：让玩家身份、StableId、位置、背包、装备、配方、生命/死亡/复活跨断线与服务器重启保留；修正重连重新分配 StableId 的问题。

**落地**：
- `SessionProtocol` v2：Hello 增加 `playerIdentity`/`playerName`；`KcpSessionServer` Connected 事件透传身份，`ServerHost` 同身份新连接接管旧会话（旧连接由服务器断开）。
- 新增 `net/PlayerIdentity`：客户端生成 128-bit 身份并落盘 `saves/client_identity.txt`；`NetworkClient` 重连复用同一身份。
- 新增 `server/PlayerProfileStore`：按 identity 分配 `2^63` 以上 StableId，二进制原子写 `saves/multiplayer_players.bin`；保存完整 `save::SavedEntity` 快照、owner 状态与复活倒计时。
- `WorldSimulation` 支持指定 StableId 生成、按快照恢复、出生点复活；`EcsWorld` 高位 StableId 不推进普通实体分配游标。
- `ServerHost` 在断线/死亡/复活/定时/退出时保存；重连恢复同一实体；死亡后 `ZLP1.alive=false`，默认 5s 满血复活并保留背包；档案损坏时服务器启动失败，避免静默丢进度。
- `ZLP1` v2 增加 `health/healthMax/alive/respawnSeconds`；客户端 HUD 在视觉玩家暂时不存在时仍显示生命与“等待复活”。
- 测试：`PlayerProfileStore` 往返/损坏文件、身份文件复用、localhost 同身份重连、服务器重启恢复、同身份新连接接管未超时旧会话、死亡状态断线重连与复活。

**验证**：Debug **310/310（120789 assertions）**、Release **310/310（120790 assertions）** 全绿；`--test-case="M13-T8*"` 6/6、101 assertions 连续 3 次通过。未提交；M0 跨机验收仍待另一台 Windows 机器。
## 2026-10-21 M13-T9 联机入口与聊天/玩家列表

**目标**：把只能命令行启动的联机流程搬到主菜单，并提供服务器列表、内嵌主机和局内聊天/在线玩家列表。

**落地**：
- 主菜单增加“联机游戏”（原生 `menu.rml` 与 core_ui Lua/Python 同步），`ui::MultiplayerPanel` 提供玩家名称、地址/端口、`开始主机`、`加入游戏`、收藏服务器与刷新；菜单返回/ESC 关闭。
- `game::ServerListStore` 原子读写 `saves/servers.json`，最多 8 条，校验 host/port/name；主机/加入成功后自动更新列表。
- `game::EmbeddedServer` 后台线程驱动 `ServerHost`（监听 `0.0.0.0`、端口 0 自动分配、独立模拟和玩家档案），本机 `NetworkClient` 回环接入；`GameApp` 统一动态 start host/join/stop，返回主菜单会关闭客户端并停主机，保留本地命令管线供单机使用。
- 新增 `net/SocialProtocol`：`ZLH1` 聊天（UTF-8、≤200 字节、拒绝控制字符）与 `ZLS2` 社交状态（玩家列表 + 最近 16 条聊天）；服务器按 `net.chatCooldownSeconds` 限频并广播，客户端 `NetworkClient::SendChat` / `HasSocialState` 接入。
- `ui::SocialPanel`（`assets/ui/social.rml`）：右上在线玩家、左下聊天记录，Enter 打开输入、Enter 发送、Esc 取消；聊天输入焦点阻断游戏键位，暂停/背包/联机菜单时抑制显示。
- 新增测试：`test_social_protocol`、`test_server_list`、`test_server_social`、`test_embedded_server`、`test_ui` 联机入口/聊天面板、`test_script_ui` multiplayer 动作映射。

**验证**：Debug **319/319（120948 assertions）**、Release **319/319（120949 assertions）** 全绿；`--test-case="M13-T9*"` 9/9、154 assertions 连续 12 次通过。窗口实机冒烟：主菜单进入联机菜单，开始主机后 UDP 7777 监听、本机客户端进入 WorldDemo 并持续收到社交状态；加入无服务器地址时进入自动重连提示。未提交；M0 跨机验收仍待另一台 Windows 机器。

## 2026-10-21 M13-T10 远程表现与投射物复制

**目标**：把远程玩家从"会移动的占位小人"扩展到可见装备、动作与命中表现，并让投射物成为服务器权威、可复制的世界结果。

**落地**：
- `ZLR1` 复制表新增 `Equipment`（两槽 defId/count/condition）与 `ActionState`（Idle/Moving/Attacking/Crafting/Hit/Dead）；新增 Effect/Projectile archetype。`EntityReplication` 捕获投射物/瞬时特效并按普通实体增量同步。
- 新增 `ActionPresentationSystem`：固定链在 AnimationSystem 之后，按 Velocity/CraftingJob/Health 推导移动、制作、死亡；`CombatSystem` 在挥击/受击/致死时写 Attacking/Hit/Dead 短时状态。
- `WorldDemoState::AppendActorPresentation` 为所有复制玩家绘制主手武器/衣物图标；攻击按 Facing 画方向挥砍条，制作画三点火花；Effect/Projectile 直接由 `RenderCollectSystem` 渲染。占位资源新增 `projectile.bullet`/`effect.impact` 逻辑名，复用现有图集帧。
- 新增 `FireProjectile` 命令与演示键 `V`：服务器从玩家位置生成带 Velocity 的投射物，固定 tick 推进后检测已加载地图阻挡和角色半径，命中经 `CombatSystem::ApplyDamage` 结算并生成 `TransientEffect`；支持会话冷却、可选目标/朝向方向、生命周期和墙体/角色结果。配置 `net.projectileDamage/projectileSpeed/projectileLifetimeSeconds/projectileCooldownSeconds/maxProjectileDistance`。
- 客户端重连/全量快照会重建装备/动作/投射物/特效副本；投射物和特效使用服务器 StableId，离开 AOI 或到期后按 ZLR1 removal 清理。

**验证**：新增 `M13-T10 Replication`（装备/动作/Effect/Projectile 往返）与 `M13-T10 ActorAction`（攻击/受击/死亡动作）单元测试；M13-T5 双客户端增加装备与攻击动作复制检查，M13-T7 联机背包增加投射物命中、命中特效和投射物实体复制检查。Debug **321/321（121001 assertions）**、Release **321/321（121002 assertions）** 全绿。已提交 `967799f`；M0 跨机验收仍待另一台 Windows 机器。

**范围/后续**：远程表现仍是占位挂点/图标和几何挥砍，不含正式角色动画帧；投射物为无弹药/无弹道的通用直线投射物，PvP 命中默认关闭；装备外观只复制两槽，不含背包/手持动画同步。

## 2026-10-21 Tiled 地图导入器（M12-T4 后续）

**目标**：把 Tiled 编辑器产出的 TMX/TMJ 地图离线转换为 `zomboidlike-map-v1`，让地图制作不再依赖手写 chunk JSON。

**落地**：`tools/tiled_import.py` 支持 TMX/TSX 与 TMJ/TSJ、内嵌或外部 tileset、CSV/base64（zlib/gzip）图块数据；tile 的 `floor`/`sprite`/class 映射为 manifest 逻辑名（缺省严格报错，`--allow-unmapped` 可生成占位名）。图块层按 `zlayer` 或名称映射 ground/floor/wall/furniture/item/mark，最底层无名层自动作为地面；对象层支持矩形、图块对象、`solid`/`ox`/`oy`/`fill`，`class=room` 矩形转为跨 chunk 裁剪的房间。输出保持 10×10 chunk、默认地面稀疏落盘、UTF-8/LF/indent=1，并对非空目录覆盖做保护。

**验证**：`py -3 tools/tests/test_tiled_import.py -v` 6/6 通过（JSON 内嵌/外部 tileset、XML 外部 TSX、base64+zlib、无名地面层/图块对象、跨 chunk 房间、稀疏输出、确定性、未映射错误）；导入规则写入 `docs/tiled-import.md` 并回链 `docs/mapformat.md`。窗口冒烟：`demo.tmx` 导入为 `tiled_smoke`（2×1 chunks），日志显示“地图已加载”，门对象生成物理静态体，无导入/加载错误。

**提交**：Tiled 导入器改动已提交 `94625db feat(tools): add Tiled map importer (M12-T4)`。

## 2026-10-22 美术垂直切片 v1

**目标**：用一组同风格的**程序化像素稿**替换核心占位资源，先验证比例、锚点、色板和资源接口，避免直接进入全量手绘后返工。

**规范**：新增 `docs/art-pipeline.md`，固定：
- 2:1 等距、`64×32`/格；地面画布 `64×128`（内容底部 `64×32`，`oy=96`）、墙 `64×224`（内容底部 `64×112`）、角色/僵尸/尸体/道具 `64×128`（脚底 `y=112`）、树 `192×256`；
- 4 向 × 2 帧、`index=dir*2+frame`；当前攻击/受击/死亡仍由代码表现，正式动画 v2 需要片段/挂点/阴影/事件帧；
- 逻辑名、manifest `own` 帧表、图集页宽/重叠/页面边界、Tiled→manifest 工作流与验收清单；
- 色板、描边、光照与后续全量美术的非目标。

**落地**：
- 新增 `tools/gen_art_slice.py`：生成 61 帧 `assets/own/atlas_own.png`，包括 8 地面、3 砖墙、2 门、玩家 8 帧、僵尸 8 帧、尸体 2、树 3、草丛 3、工作台/篝火、9 个代表性物品图标、投射物/命中特效；未列入切片的图标仍复用占位帧。
- 新增 `character.zombie` 逻辑名与 4 向 × 2 帧；`ZombieFactory` 改用该资源，玩家仍用兼容名 `character.placeholder`。墙面增加 StableId 变体选择。
- 新增 `tools/tests/test_art_slice.py`（8 项）：所有 `own` 帧存在、矩形不重叠、页面边界、地面/墙/角色锚点、玩家/僵尸帧序、切片专用帧、物品图标与 manifest 解析。
- 地面保留逐格噪点/草叶/裂纹，但去掉逐格描边，实机确认相邻菱形无缝；僵尸与玩家现在有独立轮廓和色板。

**验证**：`py -3 tools/tests/test_art_slice.py -v` **8/8**；C++ Debug **321/321（121001 assertions）**；窗口 `village_demo` 冒烟加载新图集无缺帧，玩家/僵尸/地面/墙/树/工作台/篝火/物品图标同屏（尸体/木地板由图集预览与单测覆盖），截图 `build/art-slice.png`（构建产物不入库）。当前美术改动未提交；M0 跨机验收仍待另一台 Windows 机器。

**范围/后续**：程序化像素稿不是最终手绘；未完成正式角色动画 v2、全量物品/UI 皮肤、正式音频；下一步优先动画/挂点或生存内容循环。

## 2026-10-22 M14 可玩性垂直切片：任务清单与 T1 建筑遮挡

**目标**：把技术 demo 收敛成一局 15~30 分钟、无需调试键的生存流程；详细任务与验收已写入 `docs/01-里程碑与任务清单.md` 的 M14，按 T1→T9 顺序推进。

**T1 落地**：
- `RenderCollectSystem::FadeOccludingWalls`（纯函数、内联）：输入已排序的 DrawItem、玩家下标与玩家屏幕矩形，把画家序在玩家之后、z=0、`SquareLayer::Wall` 且与玩家矩形相交的墙/门 alpha 降到目标值；只改本帧颜色，不改排序、碰撞或存档。
- `WorldDemoState::ApplyWallOcclusion` 在排序后查找本机玩家 DrawItem，调用上述函数；`render.wallOcclusionAlpha` 默认 0.35（配置 `config/game.json`）。
- 处理了同 diag / 更大 gx 的相邻墙：按排序后的玩家下标判断，而不是只比较 `diag`，否则玩家站在墙角时会被右侧墙完整盖住。
- 单测 `M14-T1 FadeOccludingWalls` 覆盖：玩家之前/之后、非墙、已更透明、楼上、屏幕不相交；7 assertions。

**验证**：C++ Debug **322/322（121008 assertions）**；窗口 `village_demo` 用 F12 缩略图确认玩家透过淡出的砖墙可见，截图 `build/m14-t1-thumb2.png`（构建产物不入库）。当前只处理 z=0 墙/门，家具（工作台等）仍可能遮住角色，后续按需扩展。

## 2026-10-22 M14-T2/T3：角色动画 v2（片段/事件/挂点）

**T2 数据契约**：
- `assets/asset_manifest.json` 的角色 provider 新增 `animations`：`idle`（2 帧 @3fps，循环）、`walk`（2 帧 @5fps，循环，事件帧 1=footstep）、`attack`（3 帧 @10fps，一次性，事件帧 1=hit）、`hit`（1 帧，一次性）、`death`（2 帧 @6fps，一次性）；帧表固定按 `dir * framesPerDir + frame` 排列。
- `game/AnimationManifest` 解析并校验帧数/fps/循环/事件帧/帧名数量，且与 `AnimationSystem::Spec` 的运行时契约逐项一致；缺片段、事件越界、元数据漂移都会拒绝加载。
- `AssetProvider` 解析 `animations` 为 `AnimationSet`，按逻辑名/哈希提供；v2 缺失或旧 provider 时保留旧 `frames` 回退。图集从 61 帧扩到 125 帧，新增 idle/attack/hit/death 四向帧，walk 暂复用旧 8 帧。
- Python `test_art_slice.py` 增加 v2 片段/帧序/事件/资源存在性检查，并覆盖 `animations` 中的全部帧。

**T3 运行时**：
- `AnimClip`/`AnimEvent` 与 `AnimationSystem::{Spec,ParseClipName,SelectClip,Advance}`：生命归零 → Death，`Attacking` → Attack，`Hit` → Hit，移动 → Walk，否则 Idle；walk/attack 事件帧通过 `eventSerial` 通知表现层，循环片段回卷后允许下一轮再次触发。
- `RenderCollectSystem::PickClipFrameIndex` 按片段帧表选帧；无 v2 资源时把 3/4 帧索引夹到旧 8 帧的 0/1，避免错位。
- 死亡窗口改为保留角色资源播放 death，窗口结束才生成 corpse；`ActionPresentationSystem` 不再每 tick 清零 Dead 计时。
- `AnimState` 新增字段仅运行时使用，不进入存档和 M13 复制负载；网络客户端用复制到的速度/动作/生命重新推导片段，旧快照仍按原 `frame/framesPerDir/fps/time` 解码。主手武器图标改走 `PrimaryHandSocket`，攻击第 1 帧向前送。
- 脚步/命中音效改由事件帧驱动（伤害仍由 CombatSystem 权威判定，事件只负责表现时序）；`audio.footstepStride` 不再使用，配置已移除。

**验证**：C++ Debug **327/327（121123 assertions）**；Python **15/15**；窗口 `village_demo` 日志确认 `atlas 125 帧`、`动画集 2 项`、无 manifest 错误；暂停截图 `build/m14-v2-idle.png`、装备斧头后的挂点截图 `build/m14-v2-weapon.png`，动画帧接触表 `build/m14-animation-contact.png`（构建产物不入库）。

**范围/后续**：walk 仍复用旧 8 帧；武器/衣物挂点仍是图标叠加，不是骨骼蒙皮；没有阴影层；命中音效复用通用 impact，远端脚步不播音；正式手绘/动画 v2 精修留给后续美术。下一任务 M14-T4 Needs。

## 2026-10-22 M14-T4：Needs 衰减与后果

**实现**：
- 新增 `characters::NeedsSystem`，在固定链 `InputSystem` 之前按游戏日速率衰减玩家 `hunger/thirst/fatigue`；暂停时不推进，数值钳制 `[0,1]`，仅处理 `PlayerTag`（僵尸不受影响）。
- `Needs` 新增 `moveScale/damageScale/healthDrainPerSecond/warning` 运行时派生字段，不写存档、不进复制负载；读档后由下一 tick 重新计算。
- 阈值效果：hunger/fatigue 降低攻击伤害，thirst/fatigue 降低移动速度；hunger/thirst 达到危险阈值时直接扣血（不走受击闪红/硬直管线），随后由 `DeathSystem` 接管倒地。
- `MovementSystem` 对玩家应用 `moveScale`；`CombatSystem` 对攻击者应用 `damageScale`；数值全部来自 `config/game.json` 的 `needs.*`，`WorldSimulation` 统一读取。
- HUD 新增 `needs-status`：显示饥饿/口渴/疲劳百分比，警告/危险时加“警告/危险”前缀并着色；native RmlUi、Lua、Python 三条路径同步，网络客户端无需求副本时显示“状态 --”。
- 食物/饮水仍走 `items::ApplyUseEffect`，服务器背包动作 `UseBag` 已补集成测试，确认消耗品降低对应需求。

**验证**：C++ Debug **332/332（121164 assertions）**；Python **15/15**；窗口实机截图 `build/m14-t4-needs.png`（正常状态）与 `build/m14-t4-warning.png`（危险状态/持续掉血），日志确认 `NeedsSystem` 耗时约 0.03ms、警告等级切换与移速/伤害倍率变化。

**范围/后续**：需求当前只在服务器/单机权威侧计算，M13 客户端尚未复制 Needs，联机 HUD 显示“状态 --”；下一任务 M14-T5 Loot table 与可搜刮容器。

## 2026-10-22 M14-T5：Loot table 与可搜刮容器

**实现**：
- 新增 `assets/loot.json`（`loot-v1`）与 `items::LootTableRegistry`：固定根 seed、`guaranteed` 保底条目、加权随机条目；启动时与 `ItemRegistry` 交叉校验未知物品、重复表 id、未知字段、rolls/权重/数量越界。`RollLootTable` 用自实现 SplitMix64（不依赖实现定义的 `std::uniform_*`），同 seed 逐堆可复现。
- 新增 `ecs::LootContainer{tableId,rollSeed,searched}`，与 `Container`/`Position`/`SpriteRef` 组成容器实体；首次搜索按 `lootSeed + tableId + StableId` 派生并写回 `rollSeed`，重复搜索只读现有内容，空表/空容器不重 roll。注册 `LOOT` v1 存档 codec，容器物品与搜索状态随实体快照往返。
- `game::LootContainers` 提供生成、最近容器查询、首次搜索与拿取；拿取先在玩家背包副本上探测容量，满包/非法索引不修改源容器，部分拿取保留源堆叠。
- `WorldDemoState::RefreshInventory` 比较最近地面物品与最近容器距离；容器更近时标题切为“容器：木箱/储物柜”，打开背包自动搜索。`Pickup`/`Transfer`/`Equip` 动作携带容器 StableId 与槽位下标；地面物品路径保持不变。Lua/Python/native 三路 `InventoryState.lootTitle` 同步。
- 村庄出生点演示生成木箱/储物柜各 1 个；新增自制 `object.crate`/`object.locker` 帧，图集 125→127 帧。

**验证**：C++ Debug **337/337（121316 assertions）**；Python **18/18**；`--game.startState=world --game.startHour=14` 实机确认容器渲染、打开背包自动搜索、标题“容器：储物柜 3 堆”、容器物品可拖入背包/空容器状态；截图 `build/m14-t5-container.png`、`build/m14-t5-containers-world.png`（构建产物不入库）。测试 `tests/test_loot.cpp` 覆盖 loot 解析/固定 seed/重复搜索/空容器/容量失败/存档往返，`tools/tests/test_loot_data.py` 校验资源引用。

**范围/后续**：容器暂不参与物理阻挡，联机不复制 `LootContainer`（单机验收主线）；下阶段 M14-T6 把容器放进可进入房屋，并接无线电零件/目标链。

## 2026-10-22 M14-T6：室内 POI 与目标链

**实现**：
- 地图生成器新增 `--objective-poi`：`village_demo` 固定放置屋内 `object.radio`（72,20）与道路 `object.extraction`（66,28）；`wall.door` 改为 open-door 美术并从 `SOLID_SPRITES` 移除，房屋可通行。
- 新增 `house_workshop` loot 表（必出 `radio_part`）与 `radio_part` 物品/图标；`game::SpawnHouseLootContainers` 在屋内生成工具柜/厨房/木箱 3 个容器，读档按表 id + 位置去重。
- `ecs::Objective{step}` 挂玩家，状态 `FindRadioPart → RepairRadio → ReachExtraction → Complete`；`OBJV` v1 存档 codec；死亡时携带状态，`RepairRadio` 且零件不在世界时在电台旁补零件，避免软锁。
- 靠近电台按 E 修理并消耗 `radio_part`；HUD 新增 `objective/objectiveHint`，native/Lua/Python 三路同步；配置 `objective.*`。
- 新增 `object.radio`/`object.extraction`/`item.radio_part` 自制帧，图集 127→130。
- `test_objective.cpp` 覆盖状态推进、HUD 文本与存档往返；`test_loot`/`test_maploader`/`test_art_slice`/`test_loot_data` 更新 T6 内容。

**验证**：C++ Debug **339/339（121360 assertions）**；Python **18/18**；用户实机确认“功能完全没问题”；目标 HUD 截图 `build/m14-t6-objective.png`。测试覆盖房屋容器、固定种子、目标推进、E 修理、存档/死亡携带。

**范围/后续**：用户反馈进入房间后人物仍会被房间/上层遮挡，当前 T1 仅淡出 z=0 墙，楼层剖切/室内可见性需要单独专项优化；容器物理阻挡、联机复制仍未接入。下一任务 M14-T7 僵尸变体与威胁曲线。

## 2026-10-22 M14-T7：僵尸变体与威胁曲线

**实现**：
- 新增 `ecs::ZombieVariant{Normal,Fast,Strong}`（kind/speedScale/damageScale/visualScale）；`game/ZombieVariants.h` 定义三档生命/速度/伤害/缩放/色调，普通白、快速冷蓝、强壮橙红。
- `SpawnZombie` 统一写生命上限、碰撞半径、色调与变体组件；PathFollow 按速度缩放，AiSystem 按伤害缩放，RenderCollectSystem 以脚底为锚点缩放，不新增任何 PZ 资源。
- 生成权重按房屋 POI（x 67~79/y 16~28）与夜间 20:00~06:00 调整：基础 70/20/10，夜间快速增加，房区强壮增加，房区夜间 35/30/35；演示人口、SpawnLod 实体化与 `SpawnZombieAt` 共用确定性选择器。
- `ZVAR` v1 存档 codec 与 `ZLR1` 复制组件（含客户端 SyncNetworkReplicas 应用）保留变体；读档补建碰撞半径按视觉缩放。
- `test_zombie_variants.cpp` 覆盖权重、确定性、三档属性差异与存档往返；`test_replication` 注册表 10 项。

**验证**：C++ Debug **343/343（121398 assertions）**；Python **18/18**；日间实机截图 `build/m14-t7-variants.png` 可见强壮体型明显大于普通/快速，夜间权重截图 `build/m14-t7-night-full.png`。

**范围/后续**：变体是属性/色调/缩放层，不是独立骨骼或动作；联机客户端已复制变体参数，服务端权威仍以属性为准。下一任务 M14-T8 新游戏/死亡重开/结算。

## 2026-10-22 M14-T8：新游戏/死亡重开/结算

**实现**：
- `WorldDemoState::RunOutcome{None,Escaped,Dead}`：目标推进到 `Complete` 时 `FinishRun(true)`；单机玩家实体被 DeathSystem 回收后 `FinishRun(false)`，不再走 M5 的原地自动重生。结算立即暂停 `GameTime`、关闭背包并禁用本局退出自动存档。
- `UiSystem::ShowEndScreen` 打开 native `menu.rml` 新增的 `end-menu`，与脚本主菜单解耦；`MenuPanel` 显示生存天数/时间、击杀/伤害，按钮映射 `NewGame` / `ReturnToMainMenu`。结算页 ESC 只消费不绕过；`ReturnToMainMenu` 在结算状态切换到真实 `MainMenuState`，避免把已结束世界作为“继续”恢复；暂停菜单保留原世界与倍速。
- 主菜单“新游戏”沿用现有 `WorldDemoState` 新会话路径；结算“重新开始”复用同一路径生成新世界。联机死亡/复活不使用单机 `RunOutcome`，服务器权威路径不变。
- `tests/test_ui.cpp` 增加结算页显示、victory/death 样式、按钮动作与 ESC 不绕过断言。

**验证**：C++ Debug **343/343（121414 assertions）**；Python **18/18**。临时 Lua smoke mod（不提交）生成 8 只僵尸验证死亡结算，日志 `玩家实体已死亡，结束本局` → `单机局结束：死亡`；`build/m14-t8-death.png`。临时 teleport/addItem + E 维修 + 撤离验证成功结算，日志 `电台已修复，目标推进：reach_extraction` → `单机局结束：撤离成功`；`build/m14-t8-victory.png`。实机点击“重新开始”确认 `状态退出: WorldDemo` → `状态进入: WorldDemo`；点击“返回主菜单”确认 `状态退出: WorldDemo` → `状态进入: MainMenu`。另在默认 `core_ui` 加载下验证 native 结算页覆盖 HUD 正常。临时 mod、进程和临时存档已清理；未提交。

**范围/后续**：结算页只有重新开始/返回主菜单，不含本局统计历史/排行；返回主菜单后“继续”依赖既有最近存档。T9 端到端仍需从主菜单完整走一局并做联机冒烟。

## 2026-10-23 M14-T9 平衡调整：降低开局僵尸压力

**现象**：新游戏出生后容易立刻被多只僵尸围住；240 人口（40 实体 + 200 虚拟）对 15~30 分钟单局过于密集。

**调整**：
- `spawner.populationTarget` 240→80，`maxLive` 320→120，`materializePerSecond` 16→10，`refillPerSecond` 2→0.8。
- 演示实体人口改为配置化 `spawner.demoZombies`，默认 40，当前 16；虚拟群落 `demoHordes=3`、`demoZombiesPerHorde=20`，合计约 76 只（16 实体 + 60 虚拟）。
- 新增 `spawner.demoSafeRadius=14`：初始实体僵尸不在出生点 `(70,24)` 14 格内生成，避免落地即被围；后续补给仍保持 `minSpawnDistance=30`。
- `test_server_host` 的演示人口断言同步为 ≥16；C++ Debug **343/343（121414 assertions）**。

**验证**：新开局日志 `生成 16 个 AI 游荡者（出生安全半径 14）`、`虚拟群落种子：3 群 / 60 只`；空闲 12 秒无受伤日志；Release 已重建。联机服务器需重启后才会读取新配置。

## 2026-10-23 M14-T9 联机时间同步：HUD 天数一致

**现象**：双客户端各自用本机 `GameTime` 显示天数和时间；加入服务器的时间不同或本机时钟推进不同，会出现“第 1 天 / 第 2 天”不一致。

**实现**：
- `net::SocialState` 升至 v2，新增 `gameMinutes`（服务器从开局起经过的游戏分钟）；`ServerHost::FillSocialState` 写入 `m_gameTime->TotalGameMinutes()`。
- `core::GameTime::SetTotalGameMinutes` 按开局日期重建当前日/时/分；客户端在 `WorldDemoState::RefreshSocialState` 每帧应用最近一次 `ZLS2` 时间，HUD、目标提示与结算页统一使用服务器时间。
- `tests/test_social_protocol` 覆盖时间字段往返与跨日重建（含 23:30 + 60 分钟）；`test_server_social` 断言两客户端收到相同且非零的 `gameMinutes`。

**验证**：C++ Debug **344/344（121426 assertions）**。旧版客户端/服务器因 `ZLS2` 版本升级不兼容，联机双方需同时重启更新。

## 2026-10-23 M14-T9 端到端验收通过

**验收**：用户按 T9 流程实机确认：新存档无 F6/F7/控制台，完成搜刮工具柜→取得无线电零件→E 修电台→到达撤离点→结算；重新开始/返回主菜单正常；单机 15~30 分钟流程可走。联机双客户端（不同 `net.playerIdentity`）连接、移动/攻击、聊天正常，HUD 天数/时间一致，无同身份接管/资源缺失。

**自动化**：C++ Debug **344/344（121426 assertions）**、Release **344/344（121427 assertions）**，Python 资源/工具测试 **18/18**；Release 已重建。用户明确不补截图，验收记录以本条与 `docs/01` 勾选为准。

**收尾**：M14 全部任务与验收完成；未提交。剩余非阻塞项仅为 M0 跨机验收、`x64-debug-python` 构建验证与室内可见性后续打磨。

## 2026-10-23 M14-T10 室内剖切与房间可见性

**现象**：玩家进入房屋后，z≥1 楼板/楼上墙体以 35% 透明度整层覆盖在 z=0 角色之上；同时玩家前方的 z=0 外墙只淡出到 35%，角色仍被明显遮挡，屋内家具也不易辨认。

**实现**：
- `render::DrawItem` 增加 `roomId`：世界对象统一取所在格 **z=0 地面格**的 `RoomId`，上层楼板沿用地基房间，便于按当前房间精确剖切且不误伤邻栋；楼上边界墙的地基格本身无房间 ID，改由四邻接房间匹配。
- `WorldDemoState::PlayerRoomId/RoomIdAt` 查询玩家当前房间（只认已挂载 chunk）；`CutawayUpperLayersInRoom` 隐藏当前房间上方 z≥1 的全部 DrawItem；`CutawayRoomWalls` 隐藏画家序在玩家之后、且 3×3 邻域命中当前房间的 z=0 边界墙，并按同一邻接规则剖掉 z≥1 边界墙；`FadeOccludingWalls(..., 0)` 兜底与玩家矩形相交的墙/门。离开房间后每帧按查询结果恢复。
- 首版用四邻接判断，站在房间角落时房屋外角的对角墙格仍未被剖掉，会挡住角色；改为 3×3 邻域后解决对角墙。
- 仍有“最里面的角落”失效：角色贴墙时碰撞体把中心推到墙格（地面格 RoomId=-1），`PlayerRoomId` 直接返回 -1，整个剖切关闭。现于中心无房间时在 3×3 格内按点到格矩形距离 ≤0.75 取最近房间，覆盖贴墙/卡角落；实机确认角落不再被墙挡住。
- 配置 `render.roomCutaway`（默认 true）；不改碰撞、寻路、存档和网络负载。

**验证**：同一位置（72.5,21.5，关闭演示僵尸以隔离渲染）对比：`roomCutaway=false` 时上层楼板覆盖角色（`build/room-nocutaway-clean.png`）；开启后屋顶/前方边界墙剖掉，角色、电台和屋内容器均可见（`build/room-cutaway-clean.png`）。C++ Debug **345/345（121433 assertions）**、Release **345/345（121434 assertions）**；临时 teleport 测试 mod 与临时存档已删除，未提交。

## 2026-10-23 M15 规划：战斗与生存 2.0

**状态**：仅记录，不开始实现。按 `docs/01` 的 M15-T1→T6 顺序推进，每个任务验收后再进入下一个。

**内容**：武器数据化与耐久、伤口/流血/绷带、僵尸攻击前摇与命中反馈、医疗与疼痛、HUD/死亡结算、平衡与端到端验收。目标是让现有 15~30 分钟单局出现“武器取舍 → 受伤 → 止血 → 继续搜刮/撤离”的决策链。

**边界**：不新开地图/POI，不做基地建造、完整感染/疾病树、枪械系统或全量美术音频；联机只保证不回归。新增组件需兼容旧存档/旧客户端缺省值。

**前置**：M14 已提交（`48379fd`）；M0 跨机验收可并行。详细任务与验收见 `docs/01-里程碑与任务清单.md` 的 M15。

## 2026-10-23 M15-T1 武器数据化与装备驱动攻击

**内容**：`items-v1` 的 weapon 条目新增可选 `combat{damage,range,arc,cooldown,knockback,stagger,wear}`（子字段可缺省）；`ItemRegistry` 严格校验仅 weapon 可用、字段类型/范围/未知键，`items::ResolveWeaponAttack` 以 `combat.player*` 为缺省值补齐空手与旧数据。内置消防斧/厨刀/锤子/螺丝刀/简易长矛参数已区分（斧高伤慢速、刀快速短距、矛长距窄扇、锤强击退、螺丝刀轻量低伤）。

**攻击链**：`WorldDemoState::PlayerAttack` 与 `ServerHost` 都从主手 `Equipment` 解析攻击参数并写入 `AttackIntent`（新增 `weaponWearPerHit`）；自动瞄准仍为 3 格，单机冷却按固定 tick 递减，服务器每会话冷却按当前武器 cooldown 计算（`net.attackCooldownSeconds` 仅回退）。`CombatSystem` 在一次挥击至少命中一个目标后产生一条主手磨损请求，由 `WorldSimulation` 的 `WeaponWearSystem` 落到 `ItemInstance.condition`（多目标不叠加、空挥不磨损，且 `characters` 不反向依赖 `items`）；耐久为 0 时拒绝攻击并提示卸下/更换，维修包仍走 `items::Repair`。耐久随 `Equipment` 的 `ZLR1` 复制与 `ZLP1` 状态同步。

**验证**：`test_items` 覆盖 combat 解析/部分缺省/非 weapon 拒绝/越界与内置武器差异；`test_combat` 覆盖多目标单次磨损、空挥不磨损、耐久钳制到 0；`test_server_security` 覆盖服务器按斧参数结算 65 伤害并扣 0.035 耐久。Debug **347/347（121522 assertions）**、Release **347/347（121523 assertions）**、Python 18/18。未提交。

## 2026-10-23 M15-T2 伤口、流血与绷带

**数据**：新增 `ecs::Wounds`（最多 4 处伤口）与 `WoundEntry`（来源 Blunt/Claw/Bite、severity、起始失血 HP/s、剩余/总时长、已包扎）；`DamageType` 增加 `Claw`。`CombatSystem::ApplyDamage` 在扣血后统一写入伤口，Debug/非正伤害忽略；`characters::WoundSystem` 放在 CombatSystem 之后、DeathSystem 之前，按 `bleedRate × remaining/duration × dt` 线性衰减失血，流血致死仍由 DeathSystem 转倒地/尸体。

**包扎**：`items.json` 的 `use.bandage` 新增止血比例（绷带 0.7、破布 0.35，均保留原有回血/材料用途）；`characters::TreatWounds` 停止流血、按比例降低 severity 并移除 ≤0.05 的伤口。单机 `WorldDemoState` 的背包使用/快速使用与服务器 `PlayerInventoryService::UseBag` 都接入，提示会显示止血处数；HUD 新增 `woundStatus/woundTone` 与 `wound-status` 行，native/Lua/Python 三路同步，`Hud.GetState()` 暴露同名字段。

**持久化与联机**：`WNDS` v1 存档组件（旧档无组件 = 无伤），`ZLR1` 注册同名复制组件并在客户端写入视觉实体；服务器复活/重连重置生命时清空伤口。新增 `wounds.*` 配置；测试覆盖类型映射、流血衰减与停止、止血/严重度、四层上限、旧档兼容、存档/复制往返与服务器背包使用。

**验证**：Debug **353/353（121650 assertions）**、Release **353/353（121651 assertions）**、Python 18/18。未提交。

## 2026-10-23 M15-T3 僵尸攻击前摇与命中反馈

**攻击档案**：`AiSystem::AttackParams` 改为 normal/fast/strong 三套 `AttackProfile`（damage/knockback/stagger/range/arc/windup/cooldown/type），由 `WorldSimulation` 读取 `combat.zombieAttacks.*`；normal/fast 使用爪击，strong 使用撕咬。伤害直接由档案给出，不再乘 `ZombieVariant.damageScale`，避免双重缩放。

**前摇/冷却**：`AiState.attackWindupLeft` 记录蓄力剩余；进入 Attack 且冷却归零后先写 `ActionState::Attacking`（渲染层画红色蓄力条），前摇结束才落 `AttackIntent`，随后进入变体独立冷却。受击硬直会清空前摇并累计 `Stats.interrupted`，硬直结束必须重新完成完整前摇；目标脱离攻击距离同样取消，避免“贴身无预警连续命中”。

**受击反馈**：`CombatSystem::ApplyDamage` 向 `HitReaction` 写 `sourceDirX/Y`（受害者→攻击者）、`sourceType`、`hitSerial` 与 `restoreTint`；闪红结束恢复变体原 tint。`WorldDemoState::UpdateCombatFeedback` 按 `hitSerial` 去重播放 impact 音效，HUD 显示“受击：左/右/上/下（爪击/撕咬）”与对应屏幕边缘红条；字段 `threatStatus/threatDirection/threatRemaining/threatDuration` 在 native 与 Lua/Python HUD 三路同步。旧档恢复角色时补建缺失的 `ActionState`，避免前摇表现依赖存档字段。

**HUD 双份文件**：core_ui 脚本 HUD 实际加载 `mods/core_ui/ui/hud.rml`，native fallback 加载 `assets/ui/hud.rml`；两份都需同步，否则脚本路径下新增行/边缘提示会静默缺失。本次已同步 M15-T2 的 `wound-status` 与 M15-T3 的 `threat-*`，并让 `test_ui` 同时加载两份 HUD 检查元素与边缘尺寸。

**验证**：`test_combat` 覆盖前摇未结束不命中、冷却、硬直中断、变体伤害类型、来源方向/serial/tint 还原；`test_zombie_ai` 更新前摇/冷却节拍；`test_ui`/`test_script_ui` 覆盖两份 HUD 的威胁提示元素、边缘尺寸与脚本字段。Debug **356/356（121699 assertions）**、Release **356/356（121700 assertions）**、Python 18/18；无窗口服务器冒烟通过（18 个系统）。未提交。

## 2026-10-23 M15-T4 医疗与疼痛

**疼痛模型**：新增 `ecs::Pain` 与 `characters::PainSystem`（WoundSystem 之后、MovementSystem 之前）。伤口 severity 汇总后乘 `pain.perSeverity`，已包扎伤口乘 `pain.bandagedScale`，有效疼痛上限 `pain.maxLevel`；`moveScale`/`attackCooldownScale` 直接存进组件，默认满疼痛时移速 -35%、挥击冷却 +50%。`PainSystem` 每 tick 推进短时止痛；`ApplyPainRelief` 取较大强度、刷新时长、不叠加。

**医疗分工**：绷带/破布仍通过 `use.bandage` 只负责止血；止痛药不再回血，改为 `use.painRelief=0.65 / painReliefSeconds=90`（0.05kg、堆叠 5）。单机 `WorldDemoState` 与服务器 `PlayerInventoryService::UseBag` 都接入；无疼痛时使用被拒绝且不消耗，重复使用刷新时长。木箱/储物柜增加止痛药权重。

**玩法与表现**：`MovementSystem` 对 PlayerTag 应用 `moveScale`；单机挥击与服务器攻击冷却应用 `attackCooldownScale`，服务器仍为权威。HUD 新增 `pain-status` 与 `painStatus/painTone`，显示“疼痛：轻微/中度/严重 xx%（移速 -x%，挥击间隔 +x%）｜止痛 Ns”，native/Lua/Python 三路同步；`PAIN` v1 存档与 `ZLR1` 复制同步客户端。

**验证**：`test_combat` 覆盖疼痛汇总/止痛衰减/重复使用；`test_items` 覆盖止痛字段与越界；`test_inventory_service` 覆盖有效/无效目标与不消耗；`test_ui`/`test_script_ui` 覆盖 HUD 结构与脚本字段；`test_entity_save`/`test_replication` 覆盖存档/复制往返。Debug **362/362（121803 assertions）**、Release **362/362（121804 assertions）**、Python 18/18；无窗口服务器冒烟通过（19 个系统）。未提交。

## 2026-10-23 M15-T5 HUD、死亡结算与提示

**死因链路**：新增 `ecs::LastDamage{DamageCause}`。`CombatSystem` 在直接命中扣血后写 Blunt/Bite/Claw/Debug，`WoundSystem` 在持续失血时写 Bleed，`NeedsSystem` 按饥饿/脱水实际失血速率写 Starvation/Dehydration；致死的直接命中不会再被同 tick 的流血覆盖（WoundSystem 对 HP≤0 直接跳过）。`WorldDemoState::UpdateRunStats` 每个固定 tick 读取玩家生命、`HitReaction.hitSerial` 与 `LastDamage`，累计存活时间、受伤次数、累计伤害和死因。

**结算页**：`game/RunStats.h` 纯函数生成结算文案，包含死因（撕咬/爪击/钝击/流血/饥饿/脱水/调试/未知）、存活时长+第 X 天 HH:MM、击杀、受伤次数/累计伤害；`FinishRun` 仍走 native `menu.rml` 结算页，主菜单/暂停菜单返回路径保持原行为并继续由现有测试覆盖。

**HUD 操作提示**：`HudState` 新增 `actionHint/actionTone`，`game/ActionHints.h` 统一文案：流血优先（有绷带提示 I 打开背包使用，无绷带提示寻找绷带/破布），其次疼痛/止痛药、附近容器 `I 搜刮`、地面物品 `F 拾取`，最后回退 `WASD/左键/SPACE/I`。native `hud.rml` 新增 `action-hint` 行，Lua/Python core_ui 同步读取 `Hud.GetState()` 同名字段；`assets/ui` 与 `mods/core_ui/ui` 两份 HUD 保持同结构。

**验证**：`test_combat` 覆盖直接命中/流血/致死死因、玩家死亡不计击杀，`test_needs` 覆盖脱水来源；`test_ui` 覆盖结算文案、操作提示优先序及两份 HUD 的 `action-hint` 元素，`test_script_ui` 覆盖 Lua 字段。Debug **364/364（121837 assertions）**、Release **364/364（121838 assertions）**、Python 18/18；无窗口服务器冒烟通过（19 个系统）。未提交。

**修复**：`CombatSystem` 原先把任何 HP 归零目标都计入 `kills`，玩家被僵尸打死也会让结算页显示“击杀 1 只僵尸”；现在 `PlayerTag` 目标只记录死亡日志，不计入击杀。HUD 的撕咬/爪击来自攻击档案：normal/fast 僵尸为 `claw`，strong 僵尸为 `bite`，由 `AiSystem` 写入 `AttackIntent.type`，`CombatSystem` 落到 `HitReaction.sourceType`（受击提示）与 `LastDamage.cause`（死因）。

## 2026-10-23 M16-T1/T2 基地建造与防御（实现与验收）

**资源与数据**：新增自制 `item_wood`/`object.barricade` 切片帧与 manifest 映射；`wood` 物品 0.4kg、堆叠 20，进入木箱/工坊搜刮与 demo 地面（12 个）。`config/game.json` 新增 `building.*`（范围 3.5、木材 id、僵尸伤害 12/1s、三种建造项）与 `BuildMode=B`。`BuildingRules.h` 提供可建造定义、按 sprite 查找与格子合法性（必须有地面、无阻挡对象）。

**建造交互**：单机 `WorldDemoState` 增加建造模式、滚轮选项、半透明红/绿预览、左键放置、B/ESC 退出；放置前校验距离、chunk 已加载、ECS 实体占用与木材，成功后扣除木材、写入 `Furniture` 层 `SquareObject`，并对阻挡物重建 chunk 物理静态体与 A* passability。篝火/工作台复用现有 `HasStationNearby`，无需制作系统改动。联机模式拒绝建造。

**防御与反馈**：`m_barricades` 记录已放置栅栏 HP（重载回满）；追击/攻击/搜索状态的僵尸在玩家 10 格内、距栅栏 1.5 格内按冷却攻击，播放 impact 并写 `ActionState::Attacking`；HP≤0 移除对象、重建碰撞/寻路并提示。`ActionHintState` 新增 `barricadeHp/barricadeMaxHp`，HUD 优先显示最近栅栏血量，建造模式显示当前选项、木材数量、合法性原因与操作提示。攻击目标选择/伤害/冷却/摧毁抽到 `BarricadeDefense.h`，`test_building` 直接驱动 ECS 实体回归。T2 验收期曾加单机 `F11` 临时刷怪入口，T3 完成后已移除。

**左键被 UI 吞掉**：实机发现按 B 后预览为绿色但左键不能放置。日志显示点击帧 `gameplayBlocked=true`，根因是 core_ui 全屏 HUD 文档把世界点击的 body 背景事件也标记为 `scriptConsumed`，导致同一次左键在世界逻辑前被消费。修复两处：`ScriptUiHost` 不再把 body/文档背景点击算作 UI 消费；`Input` 增加 GLFW 鼠标回调按下锁存（避免低帧率下按下/松开同帧轮询丢失），建造模式在光标未悬停可交互 UI 时可用 raw 按下边沿绕过 UI 阻断。`UiSystem::IsPointerOverUi()` 区分 HUD 背景与真实控件。新增 `test_ui` 锁存回归与 `test_script_ui` 背景点击不消费断言。

**验证**：Debug **369/369（121905 assertions）**、Release **369/369（121906 assertions）**、Python 18/18、无窗口服务器冒烟通过（19 个系统）。`test_building` 覆盖默认建造数据、格子规则、真实 config/items、HUD 文案，以及 T2 的最近栅栏选择、玩家 10 格过滤、伤害、每僵尸冷却和摧毁标记。2026-10-23 实际验收：B 进入建造模式、左键放置、僵尸拆栅栏、HUD 掉血与摧毁提示正常；三种建筑随 chunk 存档重载、阻挡/制作站正常（栅栏 HP 按已知限制回满）；联机模式拒绝建造且不破坏同步；用户确认功能正常，M16 关闭。

## 2026-10-23 M17 基地守夜循环需求稿

**背景**：M16 已能建造/拆栅栏，但 HP 重载回满、没有维修/拆除、没有夜间事件，基地循环不闭合。M15-T6 平衡验收暂缓，先完成基地循环。

**需求文档**：新增 `docs/04-M17-基地守夜循环需求.md`，并在 `docs/01` 增加 M17 索引/任务/验收/风险；当前状态为规划中，未改代码。

**范围**：T1 结构 HP/玩家建造标记持久化（`SquareObject`/`ChunkDiff` v2，兼容 v1）；T2 `H` 维修（1 木材 +30）与建造模式 `X` 拆除（返还 50%）；T3 22:00 预警 + 3 波夜间尸潮（最大 12 存活、HUD、结束/中止、读档取消）；T4 30~60 分钟闭环验收。

**待确认**：`H`/`X` 键位、维修/拆除费用、守夜是否每晚触发、是否允许守夜中维修、是否给奖励。评审通过后再开始实现。

## 2026-10-23 M17-T1 建筑耐久持久化

**数据模型**：`SquareObject` 末尾增加 `float hp = -1`（<0 无耐久）和 `bool playerBuilt = false`；`Chunk` 格哈希纳入两个字段，HP 变化会进入 chunk diff。

**存档**：`ChunkDiff` 段升 v2，对象追加 HP/玩家建造标记；解码支持 v1–v2。v1 耐久字段缺失时按 -1 读入，diff 内 Furniture 迁移为玩家建造，首次 `TrackNearbyBarricades` 回填 maxHp 并写回对象。

**运行时**：建造统一走 `MakeBuiltObject`（玩家建造、栅栏满 HP）；僵尸命中后 `SyncBarricadeHp` 写回对象；`TrackNearbyBarricades` 只收集玩家建造结构并从对象恢复 HP；拆除只删除玩家建造对象。

**验证**：新增 `test_building` 耐久/迁移辅助测试、`test_chunk_diff` v2 往返/v1 兼容/HP 变化/摧毁不复活测试。Debug **372/372（121967 assertions）**、Release **372/372（121968 assertions）**、Python 18/18、无窗口服务器冒烟 19 系统。2026-10-24 用户实机验收通过（快速存档/重载与 chunk 卸载重载）。

## 2026-10-23 M17-T2 维修与拆除

**配置/输入**：`building.actionRange=2.5`、`actionCooldownSeconds=0.5`、`repairWoodCost=1`、`repairAmount=30`、`demolishRefundRatio=0.5`；新增 `RepairStructure=H`、`DemolishStructure=X`。

**维修**：`H` 优先找 2.5 格内最近受损己方栅栏，扣 1 木材、恢复 30 HP（不超过 maxHp），同步对象与 `m_barricades`；满血/无木材/无目标/目标未加载均给 HUD notice 与日志。

**拆除**：仅建造模式 `X` 生效；找 2.5 格内最近己方结构（含篝火/工作台），移除对象、清理 `m_barricades` 元数据，阻挡结构重建碰撞/A*；按 `ceil(woodCost * 0.5)` 返还木材，背包满时走 `ReturnOrDropItem` 落地。

**HUD**：受损栅栏显示 `H 维修（1 木材 +30）`；建造模式靠近己方结构追加 `X 拆除（返还 N 木材）`；动作共享 0.5 秒冷却。

**验证**：`test_building` 新增维修上限、费用/返还、非己方对象与 HUD 文案测试；配置/键位测试同步。Debug **374/374（121990 assertions）**、Release **374/374（121991 assertions）**、Python 18/18、无窗口服务器冒烟 19 系统。2026-10-24 用户实机验收通过（H/X 与 HUD）。

## 2026-10-23 M17-T3 夜间基地尸潮

**状态机**：新增 `game/BaseSiege.h`，Idle/Warning/Waves/Finished 四阶段；仅单机、每晚一次、22:00 后触发、05:00 强制结束。默认预警 30 秒（暂停冻结）、3 波 4/6/8、波间隔 45 秒、最大同时存活 12。

**生成**：以预警时玩家位置为基地中心，在 10~14 格环形范围寻找已加载可站立格，复用 `WorldSimulation::SpawnZombieAt`；事件僵尸记录 StableId，设置 Chase/目标玩家/最后已知位置，并持续刷新感知指向玩家。事件结束/中止按 StableId 清理，不影响原有僵尸。

**HUD/中止**：新增 `siege-status`，预警显示倒计时与结构数，波次显示存活/栅栏；全毁提示基地失守，结束显示损失栅栏数。死亡、返回主菜单、读档、离开 25 格、快速/退出存档均先中止并清理事件僵尸；保存不写入事件僵尸或波次状态。

**配置**：`baseEvent.enabled/minStructures/startHour/endHour/warningSeconds/waveIntervalSeconds/waves/maxAlive/triggerRadius/spawnRadiusMin/spawnRadiusMax/leaveRadius`。

**验证**：新增 `test_base_siege` 覆盖触发、暂停、每晚防重、波次/最大存活、05:00、离开、基地失守、损失数与中止清理；`test_ui` 同步双份 HUD 元素。Debug **382/382（122081 assertions）**、Release **382/382（122082 assertions）**、Python 18/18、无窗口服务器冒烟 19 系统。2026-10-24 用户实机验收通过（22:00 预警、三波生成、栅栏受击与中止清理）。

## 2026-10-24 M17-T4 闭环验收

**实机验收**：用户确认 M17 基地守夜闭环通过：搜刮木材 → 建造栅栏/篝火 → 22:00 预警与三波僵尸 → 栅栏受击/摧毁 → `H` 维修/`X` 拆除 → 快速存档/重载保留 HP 且摧毁不复活；事件中保存会中止并清理事件僵尸，读档不恢复波次；联机模式按 `B` 仍拒绝建造。

**LOD 收口**：事件僵尸新增 `BaseSiegeZombie` 标记，`SpawnLodSystem` 跳过其人口统计与虚拟化，避免事件僵尸被回收或扰动演示人口；`test_spawn_lod` 覆盖事件僵尸远离焦点仍保留、普通僵尸正常虚拟化。

**验证**：Debug **382/382（122081 assertions）**、Release **382/382（122082 assertions）**、Python **18/18**、无窗口服务器冒烟通过（19 个系统）。文档已同步：M17 关闭，下一项为暂缓的 M15-T6 平衡与端到端验收。


## 2026-10-24 M18-F1 玩家储物

**实现**：新增可建造木箱（4 木材、50kg、6 堆）。仓库使用 ECS 实体（`Position/SpriteRef/Container/PlayerStorage`），实体 StableId 作为稳定标识，避免与 tile/chunk 生命周期耦合；新增 `PlayerStorage` 存档组件。靠近仓库时背包右侧显示仓库内容，支持拖拽、全部存入、全部取出、右键拾取/装备；拆除时内容优先回背包、剩余掉地，再销毁实体并返还木材。联机模式拒绝仓库建造与存取。

**坑/取舍**：仓库不能直接挂 `SquareObject`，chunk 卸载会丢失内容；改为 ECS 实体 + StableId。F1 限制 6 堆，避免 UI 只显示前 6 堆导致隐藏物品；另提供“整理”按物品类型/defId/状态排序。

**验证**：Debug **388/388（122160 assertions）**、Release **388/388（122156 assertions）**、Python **18/18**、无窗口服务器 19 系统。新增 `test_storage` 6 个用例（生成/查找、容量/堆数、全部转移、拆除清空、实体存档、整理排序），`test_ui` 覆盖仓库按钮，Lua/Python `Inventory` 覆盖 `store_all/take_all/sort_storage` 解析。未提交。

## 2026-10-24 M18-F2 饮水、烹饪与食物保鲜

**实现**：新增 `empty_bottle`/`dirty_water`，复用 `water_bottle` 作为净水（均 1L、`maxStack=1`）；`use.replaceWith` 让饮水后原位回收空瓶。新增可建造“雨水收集器”（5 木材，`object.locker` 占位，station `locker`）；`fill_bottle` 装脏水、`boil_water` 在篝火烧成净水。脏水恢复 0.25 口渴并扣 4 HP，净水恢复 0.4。

**保鲜**：`ItemDef.spoilDays` 与 `ItemInstance.condition` 复用为新鲜度；`cooked_beans` 为 1 天。`WorldSimulation` 每累计 1 游戏分钟推进所有容器与地面物品，暂停不衰减，`food.spoilScale` 可调；新鲜度 ≤0.05 为变质，使用仍恢复饥饿但扣 `food.spoiledDamage`。背包/仓库显示“新鲜 %”/“[变质]”，HUD 在背包有将坏食物时追加“食物将坏×N”。新鲜度复用现有 condition 存档/复制字段，旧存档缺省 1。

**验证**：`test_food` 4 个用例（字段解析、空瓶回收、腐败推进/变质使用、装水/烧水链）；`test_world` 覆盖 `locker` 站点；Python `test_food_data` 覆盖资源契约。Debug **392/392（122240 assertions）**、Release **392/392（122243 assertions）**、Python **21/21**、无窗口服务器 23 物品/6 配方/19 系统。未提交。

## 2026-10-24 M18-F3 睡眠与休息

**实现**：新增可建造“床”（4 木材、`object.bed` 单格家具、不阻挡移动，室内外均可），并补 `bed_1` 自制切片与 manifest。靠近床按 E 打开睡眠选择，滚轮在 1~8 小时与“睡到天亮”间切换，E 开始、ESC 取消；单机专用，联机隐藏/拒绝。

**时间与 Needs**：新增 `SleepSession` 独立记录起点/目标/已睡分钟；固定 tick 继续运行，游戏时间按 `sleep.gameMinutesPerSecond`（默认 120 分钟/秒）跳转，结束时精确落到目标。睡眠期间 `ApplySleepNeeds` 推进饥饿/口渴、按 `sleep.fatigueRecoveryPerHour`（默认 0.15/小时）恢复疲劳；饥饿或口渴达到 0.95 时以“饥饿或脱水”中断。

**中断**：生命下降（受伤，含需求伤害）、8 格内存活僵尸、守夜事件开始、ESC 主动取消、死亡。开始睡眠时若已有僵尸或守夜事件则拒绝；结束提示当前游戏时间、已睡/总小时、疲劳百分比、原因与损失 HP，并恢复入睡前倍速。

**存档**：`SaveMetadata` 新增 `gameMinutes`，meta schema v1→v2 自动迁移；读档恢复到保存时刻。新档进入 WorldDemo 时重置总游戏分钟，避免沿用上一局时间；睡前/睡后存档不再重复推进或回退时间。

**验证**：`test_sleep` 覆盖时间跳转、天亮计算、会话完成/停止、Needs 推进；`test_save_manager` 覆盖 gameMinutes 往返与 v1 迁移；Python `test_sleep_data`、`test_art_slice` 覆盖床/配置/图集。Debug **398/398（122305 assertions）**、Release **398/398（122306 assertions）**、Python **23/23**、无窗口服务器 23 物品/6 配方/19 系统。未提交。

## 2026-10-24 M18-F4 建造扩展：门、墙、地板与批量维修

**实现**：新增木墙（3 木材、200 HP、Wall 层）、木门（3 木材、120 HP、Wall 层、E 开关）、木地板（1 木材、Floor 层）；门/墙提供横向/竖向 sprite，`R` 在建造模式切换朝向。木门关闭时 `blocksMovement=true` 并进入僵尸攻击列表，打开时切到 open sprite 且可通行；开关、放置、拆除、摧毁都调用 `RebuildChunkCollision` 重建物理静态体与 A*。地图自带门/墙因 `playerBuilt=false` 不会被维修/拆除。

**存档**：门状态不新增布尔字段，关闭/打开用不同 sprite + `blocksMovement` 表达，复用 chunk diff 对象字段；木地板新增 `SquareObject.underlay` 记录覆盖前地面，拆除时恢复。`ChunkDiff` 段升 v3，兼容读取 v1/v2。`test_chunk_diff` 覆盖开门 sprite、HP、`blocksMovement` 与 `underlay` 往返。

**维修全部**：`J` 扫描 2.5 格内所有己方受损 Wall/Furniture 结构，先汇总数量与总木材；不足时直接拒绝，预检通过后一次扣料并逐个恢复，避免部分扣料。HUD 显示 `J 维修全部（N 个/M 木材）`。门/墙/栅栏继续共用 `H` 单个维修与 `X` 拆除（拆除仅建造模式）。

**建造菜单**：`building.types` 按“防御结构（栅栏/墙/门/地板）→ 设施（篝火/工作台/雨水收集器/床）→ 仓库（木箱）”排序；HUD 显示分类、序号、木材、朝向、合法性与 `R 旋转`。新增 `object.wall_h/v`、`object.door_closed_h/v`、`object.door_open_h/v` 自制切片与 manifest 映射。

**已知限制**：只做单格结构，不做多格墙连接；门无开关动画/音效；地板不可叠加；联机建造与门状态同步未开放。

**验证**：Debug **401/401（122349 assertions）**、Release **401/401（122350 assertions）**、Python **25/25**、无窗口服务器 23 物品/6 配方/19 系统。未提交。

## 2026-10-24 M18-F5 基地状态 HUD

**实现**：`WorldDemoState::UpdateBaseHud` 每 0.25 秒以玩家为中心、16 格半径扫描已加载 chunk 的玩家建造物，并汇总范围内 `PlayerStorage` 仓库；有任意己方结构/仓库时显示，离开范围自动隐藏。统计仓库数量与总重量/总容量、床位、篝火、工作台，以及 `maxHp > 0` 防御结构的完好/受损数；仓库重量和容量来自 `Container` + `ItemRegistry`。

**HUD**：`ui::HudState` 新增 `baseActive/baseStorageCount/baseStorageWeight/baseStorageCapacity/baseBeds/baseCampfires/baseWorkbenches/baseDefensesIntact/baseDefensesDamaged/baseStatus/baseTone`，`ui::BaseStatusText` 统一生成一行文案。任意受损结构为警告，受损数不少于完好数为危险，仓库 ≥ 90% 容量为警告。native `assets/ui/hud.rml` 与 `mods/core_ui/ui/hud.rml` 同步新增 `base-status`，顶部状态行按基地（74px）→守夜（98px）→受击提示（122px）排列。

**三路同步**：`LuaRuntime`/`PythonRuntime` 的 `Hud.GetState()` 暴露全部 base 字段；Lua/Python 控制器读取同名字段，缺失时回退空串/`info`；旧脚本不读取新字段继续运行。联机模式不显示基地状态。

**验证**：`test_base_hud` 覆盖文案/正常/警告/危险/隐藏；`test_ui` 覆盖两份 HUD 文档的 `base-status`；`test_script_ui`/`test_python_api` 覆盖 Lua/Python 字段。Debug **404/404（122368 assertions）**、Release **404/404（122369 assertions）**、Python **25/25**、无窗口服务器 23 物品/6 配方/19 系统。未提交，待实机验收。

## 2026-10-24 M19-F1 技能、经验与 Perk

**数据与配置**：新增 `ecs::Progression`（角色等级/XP、`perkPoints`、4 项技能等级/XP、已选 Perk）；新增 `PRGR` v1 存档组件，旧档缺组件时由玩家恢复逻辑补默认值。`config/game.json` 新增 `progression` 段：等级/技能曲线、近战命中/击杀、制作、建造、搜刮、生存小时经验、技能每级效果和 12 个 Perk；所有数值可配置，第一版不允许洗点。

**经验与效果**：`CombatSystem` 额外记录本 tick 实际命中（`HitInfo{attacker,target,damage,lethal}`），`ProgressionRules::AwardCombatHits` 只给玩家发经验，空挥/未命中/调试击杀不发；制作在 `items::Craft` 成功后按产物发；建造只在放置成功发；搜刮只在 `LootContainer.searched` 首次翻真发；生存按完整游戏小时发，暂停不计。技能效果：近战伤害、建造维修量、制作耗时、生存负重/搜刮经验/需求衰减；Perk 覆盖伤害/冷却/移速/负重/制作/维修/需求/经验。`NeedsSystem` 读取 `Progression` 的派生需求倍率；单机与服务器共用 `game::ProgressionRules`。

**升级与 UI**：角色每升一级 +1 Perk 点；`PerkChoiceIds` 从未拥有 Perk 中确定性三选一，`ChoosePerkByIndex` 校验点数/重复。HUD 左下 `hud-level` 显示等级/XP，`perk-prompt` 显示待选；`C` 打开 `character-panel` 查看技能、已选 Perk、候选。待选时 `1/2/3` 优先于快捷栏；ESC 关闭面板。native/Lua/Python 共用 `HudState.progression*`/`characterOpen` 字段。

**存档与联机**：玩家组件随实体存档；服务器档案 `PlayerState` 升至 v3，新增 `progressionLevel/progressionXp/progressionXpNext/perkPoints/skills/perks/perkChoices`，客户端只读镜像到本地玩家；`ChoosePerk` 命令由服务器校验并回收点数。旧 v2 状态仍可解码为默认成长。服务器 `TickProgression` 结算近战/制作/生存经验，`PlayerInventoryService` 制作耗时读取服务器技能乘数。联机建造/搜刮仍沿用现有禁用边界。

**验证**：新增 `tests/test_progression.cpp`（升级/上限/Perk 去重/效果/实际命中），`test_needs` 覆盖需求倍率；`test_entity_save` 覆盖 `Progression` 往返；`test_player_state` 覆盖 v3 字段与拒绝非法数据；`test_ui`/`test_script_ui`/`test_python_api` 覆盖 HUD/角色面板字段；`test_network_integration` 覆盖服务器命中经验与 ChoosePerk 命令。Debug **408/408（122509 assertions）**、Release **408/408（122510 assertions）**、Python **25/25**、无窗口服务器 23 物品/6 配方/19 系统；Release 实机启动与 `C` 角色面板截图检查通过。未提交，待实机完整验收。

## 2026-10-24 M19-F2 地图与 POI 扩展

**地图**：`tools/gen_map.py` 升至 v3，在 `village_demo`（12×12 chunks）村庄外固定生成 3 个 POI：废弃加油站、废弃诊所、废弃农场仓。三者复用现有 `wall.brick`/`wall.door`/`object.workbench`/`object.bed` 图集，房间名分别为 `poi_gas_station`/`poi_clinic`/`poi_farm_barn`；建筑外围清出 3 格树/草丛缓冲，门外铺泥地接入现有道路。新增 `tests` 外的 Python `test_poi_data.py` 校验房间、资源、容器/标记坐标与村庄到三个 POI 的 BFS 连通；`test_maploader` 确定性总数更新为 131 chunk / 471 对象 / 22 房间出现。

**配置与搜刮**：`config/game.json` 新增 `pois` 段（开关、120 格指引范围、3 个 `defs`）。每个 POI 独立配置威胁 1~3、地图标记、2 个容器、守军数量/变体/半径和任务；`game/Poi.cpp` 严格解析并校验任务目标表属于自身容器。`assets/loot.json` 新增 6 张表（每区一张主物资表 + 一张附属表），容量/权重/保底独立。

**运行时**：POI 目标 chunk 加载后，按“表 + 坐标”幂等生成 `LootContainer`，读档不会重复；守军通过 `WorldSimulation::SpawnZombieAt(x,y,variant)` 生成并带 `PoiGuard{poiId}`，`SpawnLodSystem` 对其跳过虚拟化，守军标记随存档保留。目标容器首次搜索且该 POI 守军清零后完成，玩家 `PoiTasks`（`POTS` v1）记录完成，按配置发放生存经验和物品（背包满落地）。旧档缺少 `PoiTasks`/`PoiGuard` 按未完成/无守军标记处理。

**指引**：`HudState` 新增 `poiGuide/poiGuideTone`；native `poi-guide`、Lua/Python `Hud.GetState()` 同步。HUD 显示最近未完成 POI 的威胁、方向/距离、任务进度；世界菱形标记按威胁着色（低绿/中黄/高红，完成灰），离开视野后夹到屏幕边缘，打开背包/角色面板/菜单时隐藏。

**验证**：`test_poi` 覆盖配置解析/任务规则/指引/完成去重；`test_spawn_lod` 覆盖 POI 守军不虚拟化；`test_entity_save` 覆盖 `PoiTasks`/`PoiGuard` 往返；`test_ui`/`test_script_ui`/`test_python_api` 覆盖 HUD 字段；Python `test_poi_data` 覆盖 3 个 POI、独立表、资源存在和路线连通。Debug **413/413（122672 assertions）**、Release **413/413（122673 assertions）**、Python **29/29**、无窗口服务器 23 物品/6 配方/19 系统；Debug 临时近 POI 启动 + 杀守军 + 开店搜刮冒烟确认任务完成与奖励，正式出生点已复原。未提交，待实机验收；联机 POI 内容暂未开放。

## 2026-10-24 M19-F3 任务与事件系统

**数据**：`config/game.json` 新增 `missions` 段（开关、随机种子、9 条 `defs`）。定义含 `id/name/chain/order/kind/source/prerequisite/minDay/chance/objectives/reward`；一次性链 3 条：电台（工具柜→修电台）、诊所（清剿+药柜→回营）、农场（清剿农场仓→工具柜）；另有每日巡逻和两个随机事件。目标类型 `collect/interact/kill/loot/poi/reach` 分别复用背包/电台交互/战斗击杀/首次搜刮/POI 完成/位置到达；来源 `radio/note/npc/base` 进入 HUD 文案。

**运行**：`WorldDemoState::UpdateMissions` 自动接取根任务和前置完成后的下一阶段；击杀按 `CombatSystem` 击杀增量、搜刮/修理按实际事件、POI/到达/物品按实时状态推进。每日任务在游戏日变化时清零重开；随机事件按 `randomSeed + day + id` 的 FNV 哈希确定性判定，跨日移除并重新 roll。任务完成只发一次奖励：技能/角色经验、现有物品或直接写入 `KnownRecipes` 的图纸；`farm_tools` 完成同时解除 `rain_collector` 的 `requiresMission` 建造门槛，形成基地设施解锁。

**存档与 UI**：玩家 `MissionLog`（`MSLG` v1）保存任务 id、各目标进度、状态、完成日、随机日与领奖标记；读档不会回退或重复发奖。HUD 目标框新增 `mission-guide`，显示来源/任务名/进度/奖励与 `L切换 K放弃`；`L` 循环焦点，`K` 将当前任务标为失败（一次性链永久锁定、每日/随机待下次刷新）。native/Lua/Python 共用 `missionGuide/missionTone`；完成/放弃/每日刷新/随机事件均走 notice。

**验证**：新增 `test_missions`（配置 9 条/3 链/来源/目标/随机判定/放弃）、`test_mission_data.py`（链、来源、目标引用、奖励、返回点可达、基地设施门槛）；`test_entity_save` 增加 `MissionLog` 往返；`test_ui`/`test_script_ui`/`test_python_api` 覆盖 `mission-guide`；`test_building` 校验雨水收集器任务门槛。Debug **417/417（122780 assertions）**、Release **417/417（122781 assertions）**、Python **35/35**、无窗口服务器 23 物品/6 配方/19 系统；Debug 实机按键 F6 连杀完成每日巡逻并发放奖励，HUD 截图确认 `任务` 行与 POI 指引同屏。未提交，待实机验收；任务来源暂为数据/文案层，无实体 NPC，联机不同步任务。

## 2026-10-24 M19-F4 装备与战斗扩展

**数据**：`assets/items.json` 的 clothing 新增可选 `armor{claw,bite,blunt,wear,moveScale,insulation,waterproof}`，weapon/clothing 新增 `repairEfficiency`（0.1~3.0）。新增砍刀/棒球棍/撬棍与皮夹克/雨衣/防暴背心，共 29 条物品；全部复用现有图标/占位资源。`insulation/waterproof` 暂只作 M20-F2 天气接口，枪械未开启。

**护甲运行**：`game/Armor.h` 从玩家躯干 `Equipment` 解析实际减伤，`WorldSimulation::ArmorSystem` 每 tick 重算 `ecs::DamageResist`。`CombatSystem::ApplyDamage` 在扣血/写伤口前按爪/咬/钝减伤，并按装备 `condition` 削弱；实际命中通过 `ArmorWearCallback` 调用 `items::WearEquippedArmor` 磨损躯干装备。重甲的 `moveScale` 由 `MovementSystem` 叠加到玩家速度，护甲损坏后减伤归零但重量惩罚仍保留。护甲组件是派生运行时数据，不新增存档字段，装备继续走 `Equipment`/`ZLR1`/`ZLP1`。

**武器与维修**：三把新武器形成快速（砍刀）、击退/硬直（棒球棍）、低磨损高维修效率（撬棍）的取舍；衣物进入维修流程，恢复量乘物品 `repairEfficiency`，破损到 0 后不可维修。背包/装备槽仍显示耐久百分比，护甲可先卸下后维修。

**UI 与验证**：HUD 新增 `armor-status`，显示爪/咬/钝减伤与移速，重甲/损坏走 warn/fail 色；native/Lua/Python 共用 `armorStatus/armorTone`。新增 `tests/test_armor.cpp` 覆盖装备解析/损坏/磨损；`test_combat` 增加类型减伤/实际伤害/磨损/移速；`test_items`/`test_recipes` 覆盖字段与衣物维修效率；`test_ui`/`test_script_ui`/`test_python_api` 覆盖 HUD；Python `test_gear_data.py` 校验武器取舍、护甲范围与掉落引用。Debug **424/424（122933 assertions）**、Release **424/424（122934 assertions）**、Python **38/38**、无窗口服务器 **29 物品 / 6 配方 / 20 系统**。未提交，待实机验收。

## 2026-10-24 M20-F1 感染与疾病

**规则**：`config/game.json` 新增 `infection` 段：爪击 6%、撕咬 20% 基础暴露，既有未包扎伤口按严重度提高概率（上限 75%）；病程 900 秒，30% 进入发热/疲劳、70% 进入重症、100% 死亡。未包扎伤口加速病程，发热增加疲劳，重症持续掉血；固定随机种子保证服务器/单机判定可复现。

**运行**：`ecs::Infection` + `characters::InfectionSystem` 位于 Pain 之后；`CombatSystem::ApplyDamage` 在写入新伤口前调用 `InfectionExposureCallback`，只对 PlayerTag 生效，避免首次命中把本次新伤口计入概率。感染死亡写 `DamageCause::Infection`，单机结算标题/副标题与死因显示“感染”。

**治疗**：新增 `antibiotics`（-35% 病程、50% 抑制 180s）和 `disinfectant`（-12%、25% 抑制 90s），诊所药柜与村庄储物柜保证抗生素；绷带保留止血并带 60s 弱抑制。无感染时治疗道具拒绝使用且不消耗；单机与服务器 `PlayerInventoryService` 共用同一效果路径。

**存档/联机**：新增 `INFC` v1 存档组件（旧档无组件 = 未感染）、`ZLR1` 复制组件；`ZLP1` 升至 v4，携带阶段/病程/倒计时/抑制窗口，旧 v2/v3 仍可解码；服务器 `WorldSimulation` 权威推进，客户端只读镜像并更新 HUD。

**UI/验证**：native 与 core_ui Lua/Python 新增 `infection-status`，显示阶段、病程、下一阶段/死亡倒计时和抑制剩余，严重阶段使用 fail 色。新增 `tests/test_infection.cpp`、Python `test_infection_data.py`，并扩展战斗/存档/复制/PlayerState/背包/网络/UI 测试；Debug **432/432（123079 assertions）**、Release **432/432（123080 assertions）**、Python **41/41**、无窗口服务器 **31 物品 / 6 配方 / 21 系统**。Release 实机启动加载 core_ui HUD，`InfectionSystem=0.00ms`，未发现脚本/运行时错误。未提交，待实机验收；随机种子固定但不随存档保存游标。

## 2026-10-24 M20-F2 天气、温度与潮湿

**目标**：用游戏时间驱动晴/雨/雾/寒潮，让衣物保温/防水、室内和篝火产生实际取舍；低体温影响疲劳/移速/生命，HUD 显示天气与体温/潮湿；天气只对僵尸感知/移动做轻量缩放。

**实现**：新增 `game/Weather.h/.cpp`、`game/Climate.h/.cpp`、`game/WeatherStatus.h`。`WeatherSystem` 只读取累计游戏分钟与 `weather.seed`，每 `periodMinutes` 确定性选择天气，首块固定晴，块边界在 `transitionMinutes` 内平滑混合环境温度/雨雾/视距/尸速；读档按 `metadata.gameMinutes` 重建同一状态，不保存额外随机游标。`ecs::Climate` 只持久化 `bodyC/wetness`（`CLMT` v1），`WorldSimulation` 的 `ClimateSystem` 每 tick 从天气、`IsIndoors` 房间 ID、附近 `object.campfire` 和躯干衣物 `insulation/waterproof` 重算体感/潮湿/移速；低体温增加疲劳、乘玩家移速并按 `DamageCause::Hypothermia` 扣血。`WeatherSystem` 在 Lod 之后、Needs 之前，`ClimateSystem` 在 Infection 之后；固定链从 21 升到 23 个系统。`PerceptionSystem`/`PathFollowSystem` 增加环境缩放接口，分别影响僵尸视距与非本地控制移速。`WorldDemoState` 的 HUD 新增 `weather-status`/`weather-overlay` 与 `weatherStatus/weatherTone/weatherClass`，core_ui Lua/Python 同步；雨天/雾/寒潮仅用半透明全屏调色占位。`PlayerState` 升至 v5，服务器 owner 镜像体温/潮湿，v4 旧负载回退 37°C/0%。配置新增 `weather.*`/`climate.*`。

**验证**：Debug **439/439（123146 assertions）**、Release **439/439（123147 assertions）**、Python **44/44**、无窗口服务器 **31 物品 / 6 配方 / 23 系统**。Release 实机启动 smoke 加载 core_ui HUD，`WeatherSystem=0.00ms`、`ClimateSystem=0.01ms`，无脚本/运行时错误；启动块为晴（日间 24°C/夜间 18°C）。未提交，待实机验收。

**限制/风险**：第一版没有新增雨滴/雾粒子或天气音效，仅 HUD 调色；天气状态通过已保存的游戏时间确定性重建，不单独保存天气块随机游标；Client 只镜像 owner 体温/潮湿，不把 Climate 放进 ZLR1 远程实体；睡眠期间的体温变化仍沿用现有睡眠独立时间推进边界。

## 2026-10-25 M20-F3 特殊僵尸与大规模尸潮

**目标**：在既有普通/快速/强壮变体上增加两种可读、可反制的特殊行为，让夜间事件随生存天数和基地规模成长，同时保留性能硬上限与联机快照兼容。

**实现**：新增 `ecs::ZombieSpecial{kind=Howler/Breaker,cooldown,windup,windupDuration,serial}`；`ai::ZombieSpecialSystem` 在 Ai 之后、PathFollow 之前运行。嚎叫者在玩家 12 格内蓄力 1.2 秒，完成时发 24 格 `ZombieGrowl` 声音并把半径内僵尸的可疑点改到玩家位置；硬直会打断，玩家离开 18 格取消，冷却 9 秒。破门者复用 `BarricadeDefense` 的玩家门/墙/栅栏耐久管线，2.2 格触及、0.9 秒蓄力、28 点伤害，蓄力条/预警环与音效提示，硬直会取消。`BaseSiege` 用生存天数（不是公历绝对日）和触发时结构数缩放每波数量，`maxWaveSize/maxAlive` 双层硬上限；第二波起按波次生成嚎叫者/破门者，`siegeStatus` 追加“特殊 嚎N 破N”和蓄力文本，事件僵尸继续跳过 LOD。`ZSPC` v1 存档与 `ZLR1` 复制组件同步 `kind/cooldown/windup/windupDuration/serial`，`ActionState` 追加 `Howling=6`。

**验证**：`test_base_siege` 覆盖缩放/上限/特殊波次/HUD，`test_building` 覆盖破门蓄力/伤害/冷却，`test_special_zombies` 覆盖嚎叫、引怪、冷却、硬直打断和脱战，`test_entity_save`/`test_replication` 覆盖 `ZSPC`。Release 100 实体压力 smoke FPS 412–894，`ZombieSpecialSystem≈0.00ms`。Debug **445/445（123231 assertions）**、Release **445/445（123232 assertions）**、Python **44/44**、无窗口服务器 **31 物品 / 6 配方 / 24 系统**。加速事件 smoke 已验证第一波 4 只普通、第二波 6 只含嚎叫者、第三波 8 只含破门者；HUD 截图见 `build/m20f3-siege.png`。未提交，待实机验收。

**限制/风险**：本版只做嚎叫者与破门者，不做冲锋；特殊行为只服务单机夜间事件，联机事件仍禁用但组件可复制；破门者只打玩家建造且进入 `m_barricades` 的墙/门/栅栏，不拆地图原生墙体；预警使用像素环/动作条/通知与声音，不新增正式美术或粒子。

## 2026-10-25 M20-F4 农业、电力、陷阱与 NPC 交易

**目标**：在 M18/M19 稳定的单机循环上补一组可持续的基地经营功能，同时保持每个系统可独立关闭，设施状态可存档、不复制。

**实现**：新增 `game/HomesteadComponents.h`、`game/Homestead.h/.cpp` 与 `tests/test_homestead.cpp`。`FarmPlot`（`FARM` v1）通过 E 播种/浇水/收获，按累计游戏分钟成长，默认 240 分钟成熟、360 分钟/瓶水、720 水上限、1440 分钟干旱枯萎，收获 2 份白菜并返还 1 份种子。`Generator`（`GENR` v1）用燃料罐开关，燃料按游戏分钟消耗，8 格半径给 `ColdStorage`（`COLD` v1）供电；`WorldSimulation::AdvanceFoodSpoilage` 遇到供电冷藏柜跳过腐败，冷藏柜复用仓库 30kg/4 堆 UI。`SpikeTrap`（`TRAP` v1）在夜间 `BaseSiege` 激活后对 `BaseSiegeZombie` 触发，默认 45 伤害/1 格/1.5 秒冷却/6 次使用。固定商人不参与 AI，`MerchantStock`（`MRCH` v1）保存 5 档每日库存，物物交换药品/图纸/种子/燃料；交易用 `action-hint` 面板，滚轮选择、1–5 购买、E 关闭，最近设施提示优先于固定顺序。

**数据/配置**：`config/game.json` 新增 `farming/power/traps/trading` 段，`building.types` 从 9 增至 13，新增 `object.farm_plot/generator/fridge/trap` 逻辑精灵别名；`assets/items.json` 从 31 增至 34，新增种子/白菜/燃料罐；农场/加油站搜刮表和 demo 地面补充种子/燃料。五个新组件登记到 `ComponentRegistry`，注册表从 26 增至 31；Lua/Python HUD 增加 `baseFarms/baseGenerators/baseGeneratorsOnline/baseFridges/baseFridgesPowered/baseTraps/baseTrapsArmed`。

**验证**：Debug **453/453（123377 assertions）**、Release **453/453（123378 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。Release 实机启动 smoke 无脚本/运行时错误，商人生成并显示“按 E 交易”提示；交易面板与商人截图见 `build/m20f4-trade2.png`。设施/农业/电力/陷阱的纯逻辑与组件往返由 `test_homestead`、`test_entity_save` 覆盖。未提交，待实机验收。

**限制/风险**：M20-F4 为单机垂直切片，设施状态不进入 `ZLR1` 联机快照；发电机“照明”以供电/HUD 状态表示，未做动态光照；商人是固定报价与库存，不做完整 NPC 模拟；读档不按离线时间补算农场/燃料；设施拆除暂只返还木材，不返还内部作物/燃料。

## 2026-10-25 X-F1 联机建筑只读同步（阶段 1）

**目标**：先让服务器存档里已有的玩家建筑在两个客户端上可见，不开放联机写入命令；为后续服务器权威放置/拆除准备地图补丁通道。

**实现**：服务器 `ServerHost::BroadcastStructureDiffs` 每 `net.structureSyncIntervalTicks`（默认 180 tick）遍历已加载 chunk，用 `CaptureChunkDiff` 取得玩家修改格，`SplitChunkDiffBySquare` 拆成单格 `ChunkDiff`，复用 `ZLCD` 段直接作为 KCP 数据消息下发。每格包含完整持久层状态，因此 HP、playerBuilt、木地板 underlay 和门开关 sprite 一并同步；周期重发覆盖晚连接与客户端 chunk 卸载/重载。客户端 `NetworkClient` 识别 `ZLCD` 并缓存，`WorldDemoState::ApplyNetworkChunkDiffs` 在目标 chunk Loaded 后 `ApplyChunkDiff`，未加载补丁暂存，按 `(chunkX,chunkY,z,lx,ly)` 去重后重建静态碰撞。

**验证**：`test_chunk_diff` 新增单格拆分/编码/逐格应用；`test_network_integration` 新增双客户端只读同步，服务器加载 chunk 后放入 `object.wall_h` 玩家建筑格，两个客户端均收到含 100 HP、playerBuilt 的补丁。Debug **455/455（123443 assertions）**、Release **455/455（123444 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：本阶段只读，客户端仍不能联机放置/拆除/维修，服务器不执行写入命令；结构删除不会主动发墓碑（下一阶段需要稳定结构 ID/removal 列表）；补丁按周期全量重发已修改格，尚未做增量哈希/带宽预算；客户端应用后只重建本地碰撞与寻路缓存，不把补丁写入存档。

## 2026-10-25 X-F1 联机建筑服务器权威写入（阶段 2a）

**目标**：在只读同步通道上开放放置/拆除/维修/开关门，让客户端只发命令、服务器扣木材并改世界，同时保持双客户端结构一致。

**实现**：`CommandType` 新增 `PlaceStructure/DemolishStructure/RepairStructure/ToggleDoor`；`PlaceStructure` 携带格坐标、`building.types` 索引和竖向 flags。`ServerHost` 校验会话/范围/已加载 chunk/格子占用/木材/`building.actionCooldownSeconds` 后执行：静态结构用 `MakeBuiltObject` 写地图；仓库用 `SpawnPlayerStorage`，农田/发电机/冷藏柜/陷阱用 Homestead 工厂生成 ECS；拆除返还 `demolishRefundRatio` 木材、掉落仓库存物；维修扣除 `repairWoodCost`；门开关校验门口活体阻挡。成功写入后 `MarkStructureSyncDirty` 让所有会话在本 tick 收到单格 `ZLCD`；设施/仓库通过新增 `kReplicationArchetypeProp` 复制位置/sprite。客户端建造模式在联机模式开放，维修/拆除/门/放置发送命令并显示服务器回传状态，不再本地改图或扣物。

**数据/协议**：新增共享 `game/BuildingRules.cpp::LoadBuildableDefs`，客户端与服务器共用目录解析，避免索引漂移；`CommandCodec` 只扩类型上限，线格式仍为 `ZLC1` v1 固定 56B；`EntityReplication` Prop archetype 不改变 `ZLR1` 版本，只复制 Position/Sprite。

**验证**：`test_net` 覆盖四条新命令的定长往返；`test_network_integration` 新增服务器权威流程：放墙→重复放置拒绝→维修→远距离拒绝→放门/开门→拆墙返还→放仓库并确认客户端收到 Prop→拆仓库。Debug **456/456（123532 assertions）**、Release **456/456（123533 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：仓库存取、农田/发电机/陷阱交互仍只单机；没有稳定结构 ID/所有权，任何玩家可拆任何玩家结构；删除没有墓碑/增量哈希，仍靠周期全量补丁；Prop 只有位置/sprite，无库存摘要/HP；并发拆建仅靠服务器串行执行与格子校验，未做多客户端实机压力验收。

## 2026-10-25 X-F1 稳定结构 ID / 所有权 / 删除墓碑（阶段 2b-1）

**目标**：让服务器权威建筑具备可持久化的结构身份与所有者，并修复“拆除后该格回到母地图状态，客户端永远收不到删除补丁”的同步漏洞。

**实现**：`SquareObject` 新增 `structureId`/`ownerStableId`；地图结构用 `game::MakeStructureId(wx,wy,z,layer)` 生成确定性 ID，`ZLCD` 升到 v4 编码身份字段并兼容 v1/v2/v3。设施用可存档 `ecs::StructureOwner`（`STRO` v1），Prop 复制携带 owner。服务器 `DemolishStructure/RepairStructure/ToggleDoor` 统一校验 owner：0 表示旧档无主结构，保持可操作；否则只允许建造者。拆除地图结构后记录短期墓碑，`CaptureChunkSquareDiff` 无条件采集当前完整四层状态，在 2 个同步周期内补发，避免客户端残留旧墙/门。

**验证**：`test_chunk_diff` 覆盖 v4 身份字段、v3 旧档默认值、无基线单格采集；`test_entity_save` 覆盖 `StructureOwner` 往返；`test_network_integration` 覆盖第二/第三客户端越权拆墙/拆仓库被拒、结构 ID/owner 到达客户端、拆墙后收到无墙完整格补丁。Debug **458/458（123604 assertions）**、Release **458/458（123605 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：仓库存取、农田/发电机/陷阱交互仍只单机；设施 owner 未在 UI 展示；墓碑无 per-session 确认，极端断线重连仍需验收；并发拆建与服务端重启后的跨存档恢复待 2b-2。

## 2026-10-25 X-F1 仓库存取与设施交互（阶段 2b-2a）

**目标**：在不新增 UI 协议的前提下，让联机客户端复用现有背包/仓库面板存取仓库，并把农田/发电机/陷阱的 E 交互改为服务器权威，同时避免非所有者读取仓库内容。

**实现**：`ZLR1` 新增 `PlayerStorage` 组件（maxStacks/capacity/最多 6 堆物品），服务器 producer 按会话 `viewerStableId` 过滤：仓库 owner 或 owner=0 的旧设施才收到内容；非 owner 仍能收到 Prop 的位置/sprite/owner 元数据但不泄露库存。客户端把该组件还原成本地 `PlayerStorage`/`Container`，因此现有仓库标题、重量、槽位和拖拽 UI 无需改协议。`PlayerInventoryService::Dispatch` 为 StoreAll/TakeAll/SortStorage、背包↔仓库拖拽和 `Pickup` 增加仓库分支：按 StableId 或 `inventory.lootRange` 最近仓库定位，校验 `StructureOwner` 后调用既有 `StorePlayerStorageItem`/`TakePlayerStorageItem`。冷藏柜同样走 `PlayerStorage`，联机存取自动可用。

`CommandType` 新增 `InteractFacility=20`，payload.entityId 为设施 StableId；`ServerHost::InteractFacility` 校验存活、同层、`building.actionRange`、owner 后执行农田播种/浇水/收获/清理、发电机加油/开关、陷阱布防/解除。`TickHomestead` 在服务器按累计游戏分钟推进 `FarmPlot`/`Generator`、按 tick 推进 `SpikeTrap` 冷却并刷新 `ColdStorage.powered`；`ZLR1` 新增 `FarmPlot/Generator/SpikeTrap` 状态组件，客户端 E 交互在联机模式只提交命令，提示与目标选择使用复制状态。

**验证**：`test_inventory_service` 覆盖 StoreAll/TakeAll/拖拽/owner 拒绝；`test_replication` 覆盖仓库内容 owner-only 过滤和设施组件；`test_network_inventory` 覆盖联机仓库 StoreAll/TakeAll、农田种植/浇水/收获、发电机加油和陷阱解除；`test_net` 覆盖 `InteractFacility` 往返。Debug **461/461（123694 assertions）**、Release **461/461（123695 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：并发拆建/维修冲突、chunk 卸载重载与服务端重启后的仓库/设施持久化仍需 2b-2b 端到端验收；陷阱伤害/触发仍只在单机（联机只同步布防状态）；商人交易、设施 owner UI、per-session 删除确认未做；仓库内容 6 堆上限复用单机规则。

## 2026-10-25 X-F1 并发拆建、chunk 重载与服务端世界存档（阶段 2b-2b）

**目标**：补上服务器重启后结构/仓库/设施丢失的问题，并用双客户端自动化覆盖同格并发放置/维修/拆除冲突、服务端 chunk 卸载/重载，以及重启后的客户端结构补丁。

**实现**：`ServerHost` 新增 `SaveWorld`，在正常退出且 `server.saveId` 非空、`save.autoSaveOnExit=true` 时执行。它先 `Flush`，用 `CaptureEntities` 采集实体并过滤 `PlayerTag`（玩家位置/背包继续由 `PlayerProfileStore` 独立保存），再遍历已加载 chunk 用 `CaptureChunkDiff` 采集结构 diff，通过 `SaveManager::Save` 原子写回同一存档；从备份读档的会话禁止回写主档。chunk 卸载仍由 `WorldSimulation` 的卸载回调先 `SaveChunk`，因此未加载 chunk 的修改不会丢。

重启时 `WorldSimulation::LoadSave` 先恢复实体快照，再在 chunk 加载回调叠加 `ZLCD` v4 diff；仓库/农田/发电机以 ECS 实体恢复 `PlayerStorage`/`Container`/`StructureOwner`/`FarmPlot` 等组件，地图墙由 chunk diff 恢复，客户端重连同 identity 后 owner 重新匹配并收到结构补丁。服务器仍单线程处理命令，同一格的并发放置/维修/拆除按 tick 顺序执行：第一个成功后，后续因占用、已满或目标消失被拒绝，不会重复扣/返木材。

**验证**：新增 `test_network_world_persistence`：双客户端同格并发放置/维修/拆除各只允许一次成功；服务端强制玩家离开使 chunk 卸载、返回后墙从存档恢复；正常退出后同一 saveId 重启，验证地图墙/owner、仓库 owner/bandage 内容、农田状态、客户端结构补丁和仓库 TakeAll。Debug **463/463（123776 assertions）**、Release **463/463（123777 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：世界保存只覆盖正常退出与 chunk 卸载，不含崩溃/强制终止的周期快照；未配置 `server.saveId` 的新服务器不会自动创建世界存档；并发安全依赖服务器单线程命令顺序，未实现多线程/跨进程锁；陷阱伤害/触发仍只在单机，商人交易与设施 owner UI 未接入联机。

## 2026-10-25 X-F1 联机陷阱伤害（阶段 2b-3）

**目标**：把尖刺陷阱从“联机只同步布防状态”推进到服务器权威伤害，确保僵尸扣血、陷阱次数/冷却/耗尽和客户端表现都由服务器结算。

**实现**：`ServerHost::Tick` 在 `TickHomestead` 之后调用 `TickTrapTriggers`。服务器遍历 `SpikeTrap + Position`，为每个可触发陷阱选择同层、半径内最近的可受伤 `AiState` 僵尸，排除 `PlayerTag`/`Dying`/已倒地目标；选目标与结算分离，避免在 ECS view 遍历中修改组件。命中通过 `CombatSystem::ApplyDamage` 扣血，再 `ConsumeTrapUse` 消耗一次、写入冷却，耗尽自动解除；伤害事件 sourceId 取 `StructureOwner`。命中生成 `TransientEffect` 光效，并向 12 格内会话下发剩余次数提示；陷阱用量/冷却/布防与目标 `Health` 继续走 `ZLR1` 增量复制，未新增命令或客户端预测。

单机 `WorldDemoState::UpdateTraps` 保持只对夜间 `BaseSiegeZombie` 触发；联机服务器当前没有 BaseSiege 夜袭事件，因此 2b-3 对权威 AI 僵尸触发，后续接入服务器尸潮时可收窄目标。

**验证**：`test_network_inventory` 新增 `X-F1 2b-3`：验证 45×3 伤害、1.5 秒冷却内不重复、次数耗尽解除、客户端收到陷阱次数/布防与目标血量、12 格状态提示。Debug **464/464（123801 assertions）**、Release **464/464（123802 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：联机陷阱对任意权威 AI 僵尸触发，不等同单机 BaseSiege；没有客户端本地音效/命中预测，反馈依赖服务器状态与复制光效；设施 owner UI、per-session 删除确认仍未接入。

## 2026-10-25 X-F1 联机商人交易（阶段 2b-4）

**目标**：把 M20-F4 的商人交易接入联机，商人库存、报价校验、扣料发奖和图纸学习全部由服务器权威执行，同时复用现有商人面板和背包 UI。

**实现**：`CommandType` 新增 `TradeOffer=21`，payload.entityId 为商人 StableId、index 为报价索引。`MerchantStock`（5 档库存 + restockDay）注册为 `ZLR1` 组件（`MRCH`，复制注册数 18→19）；`EntityReplication` 将其归入 Prop archetype，客户端 `SyncNetworkReplicas` 还原组件，`FindNearbyMerchant`/`TradePanelText` 直接复用。服务器 `EnsureServerMerchant` 在首个权威玩家生成后，以玩家 +2/+1 附近的安全格生成商人（玩家位置不可用时以 `trading.merchantX/Y` 回退），避免与建筑/角色重叠；`TickHomestead` 按生存日补货。

`ServerHost::PurchaseMerchantOffer` 校验商人存活、同层、`trading.range`、库存与背包容量；复制玩家 `Container`，先扣 cost、再发 reward，只有全部成功才写回，避免失败时扣物。图纸交易在确认未学会后写 `KnownRecipes`（排序去重），成功后扣库存并通过服务器状态提示。客户端联机 E 打开商人面板，1–5 只发送 `TradeOffer`，不本地扣物/加物；库存经 `ZLR1`、背包与图纸经 `PlayerState` 回收。单机交易路径保持不变。

**验证**：`test_network_inventory` 新增 `X-F1 2b-4`：验证商人自动生成、`MerchantStock` 复制、wood→bandage 交易、库存递减、物资不足拒绝，以及图纸写入 `KnownRecipes` 并同步客户端；`test_net` 覆盖 `TradeOffer` 往返，`test_replication` 注册数更新为 19。Debug **465/465（123836 assertions）**、Release **465/465（123837 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：固定报价、无移动/NPC 模拟；首个权威玩家决定商人落点，后续玩家共享同一商人与库存；设施 owner UI、per-session 删除确认仍未接入。

## 2026-10-25 X-F1 设施 owner UI 与拆除二次确认（阶段 2b-5）

**目标**：让联机玩家能看出设施/结构归属，避免误操作他人设施，并给拆除增加 per-session 二次确认；不改变既有服务器权威所有权校验。

**实现**：客户端 `SyncNetworkReplicas` 从复制 Prop 的 `ownerStableId` 还原 `StructureOwner`；`WorldDemoState` 增加统一 `LocalPlayerStableId`/`StructureOwnerLabel`/`StructureOwnerSuffix`/`CanEditLocalStructure`。农田/发电机/陷阱提示、结构维修/开门提示、建造模式拆除提示与仓库标题显示 `归属：你/玩家名/无主`，非所有者显示只读或仅所有者可操作；单机拆除入口也补 owner 校验。`HomesteadHint` 对非所有者不再提示 E 操作。

`Command.h` 新增拆除确认 flags：`kDemolishConfirmRequestFlag`（bit0）与 `kDemolishConfirmAcceptFlag`（bit1）。服务器 `SessionPlayer` 保存待确认目标的格/层/结构 ID 或设施 StableId，`ServerConfirmDemolish` 处理 request/accept：request 只写 3 秒（180 tick）待确认状态并回提示，accept 校验同格/同层/token 未过期且 owner 仍可编辑后才继续删除；换目标、过期、owner 变化均拒绝。`flags=0` 保持旧客户端立即拆除兼容。

客户端单机/联机统一维护 3 秒 `PendingDemolish`，第一次按 X 只记录目标并提示“再次按 X 确认拆除”，第二次同目标才执行；退出建造模式或玩家切换时清空。联机首次请求只发 request，第二次发 accept，服务端状态提示覆盖本地提示。

**验证**：`test_network_integration` 的拆除流程改为 request/accept：request 后断言墙体和木材不变，accept 后墙体移除并返还木材；既有越权拆墙/拆仓库、`test_network_inventory`/`test_net`/`test_replication` 继续通过。Debug **465/465（123840 assertions）**、Release **465/465（123843 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：owner 标签优先使用 `SocialState` 玩家名，名单未到前回退 `玩家 <StableId>`；非 owner 的仓库内容仍按 2b-2a owner-only 过滤，因此无主/自有仓库标题可显示归属，未复制的他人仓库无法显示完整标题；确认状态仅存于会话内存，服务端重启或断线后需重新确认；旧客户端 `flags=0` 仍立即拆除。

## 2026-10-25 X-F1 服务器守夜事件与陷阱收窄（阶段 2b-6）

**目标**：让服务器具备与单机一致的夜间 BaseSiege 事件，把联机陷阱从“任意权威 AI 僵尸”收窄到带 `BaseSiegeZombie` 标记的事件僵尸，并保持服务器权威结算。

**实现**：`ServerHost` 复用 `game::BaseSiege`，在 `Init` 中读取 `baseEvent.*` 并配置回调；`Tick` 顺序改为游戏时间推进 → `TickServerBaseSiege` → ECS 固定链，使新生成的事件僵尸同 tick 进入 AI/战斗。锚点优先取本地玩家，否则取已生成会话中 StableId 最小的存活玩家并缓存；玩家死亡、离开 `leaveRadius`、天亮或波次结束时由 `BaseSiege` 中止并清理事件僵尸。结构计数遍历已加载 chunk 的 Wall/Furniture 玩家结构，生成 Howler→Fast、Breaker→Strong，普通事件僵尸走默认变体，并附加 `BaseSiegeZombie`（继续跳过 LOD 回收）；预警/波次/结束通过会话状态广播并写 `siege` 日志。`TickTrapTriggers` 的候选查询改为 `BaseSiegeZombie + AiState + Health + Position`，不再对普通权威 AI 僵尸触发。

**验证**：`test_network_inventory` 新增 `X-F1 2b-6`：以 22:00、0 秒预警、单波和 4~6 格生成半径启动，先放置玩家墙使结构计数达标；连接玩家后断言生成 `BaseSiegeZombie`，将陷阱放到事件僵尸位置并同时放置无标签 AI 目标，验证事件僵尸扣血/陷阱消耗一次、无标签目标保持满血。2b-3 测试也改为事件目标并增加无标签目标断言。Debug **466/466（123859 assertions）**、Release **466/466（123860 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**。未提交，待实机验收。

**限制/风险**：联机暂无独立守夜 HUD，只有状态栏文案；多玩家共享一个锚点/基地中心，不支持每玩家独立基地；事件状态不写入世界存档，服务器关闭/玩家离开会中止并清理事件僵尸；陷阱仍无客户端本地音效/命中预测。

## 2026-10-25 M21-F1a 基地防御 2.0：夜袭导演

**目标**：把 M17 的“固定环形刷怪”升级成有主攻方向、有薄弱破口、有战后评级的单机守夜循环；不新增僵尸种类、不做完整动态光照，先完成 M21-F1a，联机 F1b 和电力 F2 后续推进。

**实现**：新增 `game/BaseSiegeDirector.h`，用八方向扇区、剩余 HP 比例、距基地中心距离和门标记评分，选择主攻/备用方向与薄弱破口；`MakeBaseSiegeAttackPlan` 保持确定性，并避免连续两晚选择完全相同方向。`BaseSiege` 保留 Idle/Warning/Waves/Finished 状态机，新增 `structureList` 回调在预警时锁定目标、波次中每 0.5 秒复核 HP；目标被摧毁后先在同一方向找次弱结构，找不到才最多切换一次备用方向。`WorldDemoState` 从已加载 chunk 收集玩家墙/门/栅栏，按当前方向扇区生成事件僵尸；`BaseSiegeResult` 记录破口、基地失守、击杀、完成波次和结构存活率，正常结束按 S/A/B/C 发一次木材/燃料/维修包/药品，`Abort`/保存/退出/死亡不结算。

**AI 收口**：新增 `ecs::SiegeAssaultTarget`，`DriveBaseSiegeZombies` 给 Breaker 写入破口外侧的可走点、给 Howler 写入后排点；`AiSystem` 在该组件激活时忽略可见玩家并固定 `lastKnown`，避免 `PerceptionSystem` 每 tick 把目标改回玩家。事件期间结构扫描半径扩到 16 格、破门玩家范围扩到 24 格，使 12 格线上破口可被攻击；普通事件僵尸仍按原逻辑追玩家，只从主攻扇区进入。`TickZombieBarricadeAttack` 和 `BarricadeDefense` 的原有伤害/冷却/蓄力逻辑不变。

**验证**：`test_base_siege` 新增 5 个 M21-F1a 用例，覆盖弱侧选择/方向与破口 HUD/主攻扇区生成/同方向换靶/备用方向切换/一次性奖励/无成型防线；`test_zombie_ai` 新增破口目标覆盖可见玩家。Debug **472/472（123924 assertions）**、Release **472/472（123925 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；`ZomboidLike` Debug/Release 已构建，未提交，待实机验收。

**限制/风险**：F1a 仅单机；服务器 2b-6 不提供 `structureList`，继续使用旧环形生成；联机 F1b 尚未同步方向/破口；攻击计划/破口/评级不写入存档，保存/退出/死亡仍中止事件；多玩家共享锚点；HUD 仍是一行状态文本，没有独立战报面板或方向箭头；破口目标路线依赖现有 A*，寻路失败时只能回退追玩家。

## 2026-10-25 M21-F2a 单机电力防御 2.0

**目标**：让发电机从“冷藏柜电源”升级为守夜资源：基础/设备负载决定燃料消耗，泛光灯提供夜间照明与事件诱饵，电网只在供电时对事件僵尸放电；F2b 联机放到后续，不阻塞单机。

**实现**：`PowerConfig` 增加 `baseLoad/coldStorageLoad/floodlightLoad/electricFenceLoad/floodlightRadius/attractRadius/electricFenceDamage/electricFenceCooldownSeconds/electricFenceStaggerSeconds/lowFuelWarningMinutes/deviceRequiresNight`。`ecs::Generator` 增加运行时 `load`（不入 `GENR`），`AdvanceGenerator` 按 `baseLoad + 设备负载` 消耗燃料；`WorldDemoState::UpdatePowerDevices` 每 tick 取最近运行发电机分配供电，燃料耗尽立即重算并停电，加燃料恢复。扫描限制 8 电源/12 格/256 电网格、0.5 秒节流。

**泛光灯**：新增 `floodlight` 建造项、`ecs::Floodlight{on,powered}`、`SpawnFloodlight` 与 `FLDL` 存档；E 开关，HUD 显示供电/关闭/待夜间/照明半径；夜间按 `attractRadius` 选最近供电灯作为 `SiegeAssaultTarget` 诱饵，已直接看见玩家的僵尸不覆盖其追击，不瞬移。光晕用等距圆环/放射线程序化绘制。

**电网**：新增 `electric_fence` 建造项（100 HP、阻挡、结构层），复用现有结构 HP/owner/维修/拆除/碰撞/存档；只在供电且夜间事件对 `BaseSiegeZombie` 走 `CombatSystem::ApplyDamage`，25 伤害/0.5 秒硬直/1.2 秒冷却，按格记录；无标签普通僵尸、断电/白天不触发，断电仍阻挡。

**HUD/验证**：`HudState`/`BaseStatusText` 与 Lua/Python 输出增加电力摘要、泛光灯/电网计数和低燃料/停电状态。扩展 `test_homestead`、`test_base_hud`、`test_building`、`test_entity_save` 与 Python 建造数据。Debug **475/475（123965 assertions）**、Release **475/475（123966 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；`ZomboidLike` Debug/Release 已构建，未提交，待实机验收。

**限制/风险**：仅单机 F2a；F2b 服务器权威供电/设备复制未实现，联机仍只有旧发电机/冷藏逻辑。泛光灯首版无独立耐久，不参与被摧毁/维修；电网已走结构层。真实动态光照未实现，仅光晕/玩法半径。负载数值需实机调参；设备扫描已有硬上限和节流，但更多发电机会线性增加扫描成本。

## 2026-10-25 M21-F1b 联机守夜状态广播

**目标**：让服务器沿用 F1a 的方向/破口导演，把同一场守夜的主攻方向、破口、波次和结束状态同步给所有客户端；客户端只读展示，不本地推进事件，也不新增独立网络包。

**实现**：`ServerHost` 补齐 `baseEvent.director.*` 配置和 `structureList`，新增 `ListServerPlayerStructuresNear` 从已加载 chunk 收集墙/门/栅栏；`BaseSiege` 服务器实例因此生成主攻/备用方向与破口。`FindBaseSiegeApproachPoint` 抽到 `BaseSiegeDirector.h`，单机与服务器共用；服务器 Breaker 写 `SiegeAssaultTarget` 去破口外侧、Howler 写后排点，普通事件僵尸回锚点，事件清理仍由服务器负责。

**状态广播**：`PlayerState` 升 v6，新增 `PlayerSiegeState`（phase、主/备/当前方向、破口、波次、色调、服务器 HUD 文本）；`FillPlayerState` 每 tick 填入，客户端 `WorldDemoState` 把文本/色调镜像到联机 HUD。旧 v5/v4/v3 解码回退 Idle；联机结束只广播评级/提示，暂不发每玩家物资奖励。

**验证**：`test_player_state` 覆盖 v6 字段与 v5/v4/v3 回退、非法方向/波次；`test_network_integration` 新增双客户端守夜测试，验证两端方向/波次/状态文本一致、服务器 Breaker 持有破口目标。Debug **476/476（124006 assertions）**、Release **476/476（124007 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；Debug/Release 已构建，未提交，待实机验收。

**限制/风险**：多玩家仍共享最小 StableId 的守夜锚点；联机不发每玩家奖励，无独立战报面板/地图破口高亮；`PlayerState` v6 需要客户端与服务端同版本；F2b 电力设备服务器权威/复制见下一条。

## 2026-10-26 M21-F2b 联机电力防御

**目标**：把 F2a 的单机电力设备接入服务器权威与复制链路，让双客户端看到同一供电、负载、泛光灯状态和电网伤害；不新增独立电力协议。

**服务器**：`ServerHost::TickServerPower` 复用单机规则：最多 8 个运行电源、每电源 12 格扫描、256 个电网格、0.5 秒节流，按 `power.baseLoad + 冷藏/泛光/电网负载` 扣燃料；燃料耗尽停电并重算，加燃料恢复。`TickElectricFences` 只在夜间 `BaseSiege` 且电网供电时对 `BaseSiegeZombie` 走 `CombatSystem::ApplyDamage`，普通 AI 不触发；电网被摧毁/拆除后从供电集合移除。`PlaceStructure` 支持泛光灯/电网，`InteractFacility` 支持泛光灯 E 开关；服务器泛光灯/发电机噪声作为事件诱饵，已直接看见玩家的僵尸优先追玩家。

**复制/客户端**：`EntityReplication` 组件版本升至 2，`Generator` 增加 `load`，新增 `ColdStorage`/`Floodlight` 组件；电网 HP/owner 继续走 `ZLCD`。`PlayerState` 升至 v7，新增 `PlayerPowerState`（燃料/负载/低燃料/停电）；客户端联机 HUD 读取服务器摘要，设备计数/供电标志来自复制实体，电网光晕只按复制电源/结构重建，不本地扣燃料或结算电击。旧 v6/v5/v4/v3 状态解码回退 0/无告警。

**验证**：`test_replication` 覆盖 Generator.load/ColdStorage/Floodlight 全量/增量；`test_player_state` 覆盖 v7、旧版本回退和非法电力字段；`test_network_integration` 覆盖联机放置发电机/泛光灯/电网、服务器供电负载、泛光灯开关，以及服务器电网对事件僵尸电击、普通僵尸不触发。Debug **479/479（124100 assertions）**、Release **479/479（124101 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；Debug/Release 已构建，未提交，待实机验收。

**限制/风险**：电力摘要是服务器全局值，不按玩家基地分别计算；客户端电网供电显示是只读重建，真实伤害/燃料仍完全由服务器决定；泛光灯仍无独立耐久；F2b 跨机、断线重连、chunk 卸载/服务端重启组合验收待实机。

## 2026-10-26 M21-F2b 组合场景自动化

**目标**：补齐 F2b 只覆盖“在线双客户端”之外的持久化路径，验证同一玩家重连、基地 chunk 被卸载/重载、服务器正常退出并用同一 saveId 重启后，电力设备不会回零或丢失。

**实现**：`tests/test_network_world_persistence.cpp` 新增单客户端端到端场景：放置发电机/泛光灯/电网并加燃料启动，确认 `PlayerState.power.load=2.5`、`Generator.load=1.5`、泛光灯 `on/powered`；关闭客户端后用同一 `playerIdentity` 重连，确认同一 `stableId`、电力摘要和复制实体恢复；把服务器玩家位置移到地图远角触发 base chunk 卸载（墙/电网格消失），再移回，确认发电机燃料、运行状态、泛光灯开关/供电、电网 HP/owner 和电力摘要从存档/复制恢复；最后关闭并 `Shutdown` 保存世界，新建 `ServerHost` 用同一 `saveId` 重启，确认设备与电力摘要再次恢复。测试同时验证客户端副本的 `generator.running`、`floodlight.on/powered`。

**验证**：Debug **480/480（124170 assertions）**、Release **480/480（124171 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；未提交，跨机/双客户端组合实机验收仍待完成。

**限制/风险**：该场景仍为 localhost 单客户端；没有覆盖跨机 NAT/丢包、双客户端同时重连、事件进行中重启和 chunk 卸载期间并发命令。电力摘要仍是服务器全局值，不按玩家基地分别计算。

## 2026-10-26 M21-F1b 联机每玩家奖励

**目标**：补齐 F1b 遗留的“联机结束只广播评级/提示”问题，让共享基地的每名在线存活玩家都拿到同一份战后物资，同时保证单机/联机奖励表一致、只发一次。

**实现**：把 S/A/B/C 奖励表抽到 `game/BaseSiegeRewardsFor`，B/C 木材沿用 `building.woodItem`；单机 `WorldDemoState::GrantBaseSiegeReward` 改为复用该表。服务器给 `BaseSiege.onResult` 接入 `ServerHost::GrantServerBaseSiegeRewards`：正常结束且满足奖励条件时，由 `BaseSiege::m_resultAwarded` 保证只调用一次；遍历在线、已生成且未死亡的会话玩家，每人发放同一组物品。物品通过 `ReturnServerItem` 优先进背包，装不下时掉到玩家脚下；发放后标记会话状态脏位，客户端下一帧即可看到背包变化。结束广播继续使用 `EndText()`，因此所有客户端看到相同评级和奖励文本。

**验证**：`test_base_siege` 新增奖励表各评级/自定义木材检查；`test_network_integration` 双客户端守夜测试在 Breaker 出现后清空事件僵尸，验证事件正常结束、两名玩家各获得一份 S 奖励（燃料罐/维修包/抗生素各 +1）、结束文本一致，并在后续 120 tick 内不重复发放。Debug **481/481（124201 assertions）**、Release **481/481（124202 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；未提交。

**限制/风险**：奖励按“所有在线存活会话”发放，不区分距离/参战贡献，适合当前共享基地模型；背包满时只保证掉在玩家脚下，不保证地面/容器容量；独立战报面板和地图破口高亮仍未实现。

## 2026-10-26 M21-F1c 独立战报面板与破口高亮

**目标**：补齐 F1a/F1b 遗留的战后反馈与地图可读性：结束提示不再只有一行文本，玩家能一眼看到评级、波次、击杀、结构损失、是否破口和奖励；预警/波次中的预计破口与实际破口在场景内可见。联机保持服务器权威，客户端只读。

**结束文本/面板**：`BaseSiege::EndText` 在原有“栅栏损失/评级/击杀/奖励/来袭方向”上补充波次完成数、结构损失与存活比例、防线完好/已破口（含破口方向）和基地失守。`ui::BuildSiegeReportLines` 按 ` | ` 拆行，评级单独放大；`HudState.siegeReportOpen/title/rating/tone/hint/lines[8]` 驱动 native 与脚本 HUD 的 `siege-report` 覆盖层。单机在 `ShowBaseSiegeNotice` 收到“守夜结束”时打开，联机在 `PlayerState.statusSequence` 变化且 status 以“守夜结束”开头时打开；`ESC` 关闭，新一晚预警自动收起。面板固定 8 行，脚本路径逐行 `SetText`，不拼接不可信 InnerRML。

**破口高亮**：`BaseSiege` 在首次破口时记录 `BreachPoint/BreachDirection`，并写入 `BaseSiegeResult`；单机预警/波次用橙黄菱形标记 `AttackPlan` 预计破口，破口后保留红色 X + 脉动圆环。联机 `PlayerSiegeState` 升到 v8，尾部追加 `targetValid/targetX/targetY/breachValid/breachX/breachY`；`ServerHost::FillPlayerState` 只在事件进行中或正常结束时填充，中途中止不残留标记。v7 及更旧负载解码后坐标无效，HUD/事件流程不受影响。

**验证**：`test_base_siege` 覆盖破口坐标/方向、结果结构计数与扩展结束文本；`test_base_hud` 覆盖结束文本解析与评级色调；`test_player_state` 覆盖 v8 坐标往返、v7/v6/v5/v4/v3 回退和非法坐标；`test_network_integration` 验证双客户端收到相同预计破口坐标；`test_ui` 验证 native/脚本 HUD 面板结构；Lua/Python API 烟测新增字段。Debug **483/483（124281 assertions）**、Release **483/483（124282 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；未提交，实机视觉/分辨率仍待验收。

**限制/风险**：战报面板是 HUD 覆盖层而非暂停菜单，只展示结束文本能提供的信息，没有逐波伤害/击杀明细；场景高亮只有菱形/X/圆环，没有小地图或方向箭头；联机坐标属于会话内状态，不写入存档，重连后不会再打开已结束事件的战报。

## 2026-10-26 M21-F2b 补充：泛光灯独立耐久与维修

**目标**：补齐 F2b 最后一个功能缺口：泛光灯不再只是“无敌诱饵”，事件僵尸能砸灯，玩家能维修；单机与服务器都保持权威一致。

**实现**：`floodlight.maxHp` 设为 80；`SpawnFloodlight` 附带 `Health` 与 `NoCombatTarget`，旧档由 `EnsureFloodlightHealth` 补满。新增 `TickZombieFloodlightAttacks`：只对 `BaseSiegeZombie` 生效，按僵尸独立冷却（普通 12/1.0s，Breaker 28/1.2s），只扣 HP 并播放攻击动作，不写伤口。单机在 `UpdateBuilding` 结算；服务器在 `Simulation::Tick` 后结算并广播“泛光灯被摧毁”，客户端通过 `ZLR1 Health` 只读同步。H 维修复用木材/冷却/owner 校验，单机同时支持结构与泛光灯，联机 `RepairStructure` 找不到地图结构时按格回退到泛光灯。通用战斗、投射物与 `DeathSystem` 跳过 `NoCombatTarget`，避免误伤/尸体化。

**验证**：`test_homestead` 覆盖专用伤害冷却、摧毁与旧档迁移；`test_entity_save` 覆盖 HP 存档；`test_replication` 覆盖 `ZLR1 Health` 全量/增量；`test_network_integration` 覆盖服务器权威维修；`test_network_world_persistence` 覆盖重连/chunk 重载/重启后 HP 保持；`test_building` 与 Python 建造数据检查 80 HP。Debug **484/484（124316 assertions）**、Release **484/484（124317 assertions）**、Python **46/46**、无窗口服务器 **34 物品 / 6 配方 / 24 系统**；未提交，实机视觉仍待验收。

**限制/风险**：泛光灯被摧毁后不返还木材、不生成残骸；维修一次固定 30 HP，不做分段/材料差异；僵尸只按距离砸灯，不检查墙体遮挡；多人同时维修仍受服务器 tick 冷却串行化。

## 2026-10-26 X-F2a Mod 内容文件与 World.GetSquare

**目标**：让 mod 不改 C++ 就能新增物品、烹饪配方和可建造仓库，并补一个只读世界格查询 API。

**实现**：新增 `game/ModContent`，在 `GameApp`/`ServerHost` 启动时按 `loadOrder` 扫描 `mods/*/content/` 的 items/recipes/buildings。items 使用 `items-v1`，recipes 兼容 `recipes-v3/v2/v1`，buildings 使用新 `buildings-v1`；items+recipes 以 mod 为事务，失败只跳过当前 mod。`BuildableDef` 抽出 `ParseBuildableDefs`/`AppendBuildableDefs`，重复 id 与非存储 sprite 冲突拒绝，存储项允许同 sprite 但容量/堆数必须不同；客户端 `StorageBuildable` 与服务器 `FindServerBuildEntityAt` 用容量/堆数区分。`GameContext::modBuildables` 缓存客户端内容，服务器 `m_modBuildables` 追加到建造目录，保证 PlaceStructure 索引顺序一致。`ScriptApi::GetSquare` 读取已加载格并绑定 Lua/Python 为 `{loaded,hasFloor,floor,wall,furniture,item}`。

**示例**：`mods/example_content` 添加 `camp_stew`、`cook_camp_stew`（篝火）与 `mod_supply_crate`（35kg/4 堆仓库），启动日志显示物品 +1/配方 +1/建造 +1。

**验证**：新增 `test_mod_content` 覆盖合并、重复物品 mod 回滚、后续 mod 继续、建造冲突和示例内容；`test_script_api` 覆盖 GetSquare 快照，Lua/Python API 烟测覆盖返回表/dict。Debug **487/487（124374 assertions）**、Release **487/487（124375 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；未提交，待实机验收。

**限制/风险**：内容只在启动时加载，改 JSON 需重启；暂不支持 mod 新贴图/图集、POI、搜刮表、任务、事件、HUD 字段或动态注册；联机双方必须安装相同内容 mod，内容哈希当时未纳入握手（已由 X-F2b-1 补齐）；新设施行为仍要 C++。

## 2026-10-26 X-F2b-1 自动内容指纹握手

**目标**：把 X-F2a 的“双方必须安装相同内容 mod”从文档约定变成启动时握手校验，避免物品/配方/建造 Index 或数值不一致造成联机错位。

**实现**：新增 `game::ComputeContentHash`，以版本盐 + 最终物品/配方/建造定义（全部字段与顺序）做确定性 FNV-1a，明确按小端写整数并归一化 +0/-0。`GameApp` 在内容合并后、`ServerHost` 在服务器建造目录构建后各自计算；`net.contentHash=0`（默认）时自动使用计算值，非 0 仍作为手动覆盖。自动值注入现有 KCP `Hello/Welcome` 的 contentHash，不一致由会话层拒绝。内嵌服务器新增 `ContentHash()` 供测试/入口读取，`test_embedded_server` 改为 `options.contentHash=0` 并以服务器返回值连接，同时按客户端加载路径重算指纹比对，验证自动链路。

**验证**：新增 `test_content_hash` 覆盖确定性、物品重量/配方时长变化、建造字段/顺序变化；`test_embedded_server` 覆盖自动指纹下启动、客户端路径哈希比对与连接。Debug **490/490（124390 assertions）**、Release **490/490（124391 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；未提交，待实机验收。

**限制/风险**：指纹当时只覆盖有效 items/recipes/buildings；脚本、图集/贴图、地图、POI、任务/事件内容未纳入，仍需双方使用同一份游戏内容（2026-10-28 内容指纹 v2 已纳入 loot/POI/任务）。FNV-1a 不是密码学校验，只用于一致性/误配检测。`net.contentHash` 手动覆盖时仍需客户端/服务器一致。

## 2026-10-26 X-F2b-2a Mod 图集与资源绑定

**目标**：允许 mod 在不改 C++ 的前提下声明新的逻辑资源名和图集，让新增物品/建造项使用自己的图标与精灵。

**实现**：新增 `game/AssetManifest` 的 `LoadMergedAssetManifest`：先加载基础 `assets/asset_manifest.json`，再按 mod 顺序合并 `mods/<id>/assets/asset_manifest.json`；只允许新增 logical 名称，与基础/更早 mod 重名或 binding/provider/frames 非法时跳过并告警。mod 清单中的 `atlas` 优先相对清单所在目录解析，找不到再回退根目录；`Atlas` 的 image 路径做 `lexically_normal`，避免 mod 通过 `..` 引用基础 PNG 时重复加载纹理。`LoadModContent` 收集资源清单（asset-only mod 也可应用），content 失败的 mod 不加入；`GameApp` 将路径放入 `GameContext::modAssetManifests`，`WorldDemoState`/`RenderDemoState` 传给 `AssetProvider::Init`。

**示例**：`mods/example_content` 新增 `assets/asset_manifest.json` 和本地 `atlas_example.atlas.json`，提供 `mod.camp_stew`、`mod.supply_crate`；物品 icon 与建造 sprite 改用新逻辑名；示例图集复用 `assets/own/atlas_own.png` 帧以控制仓库体积，实际 mod 可替换为自己的 PNG。

**验证**：新增 `test_mod_assets` 覆盖 mod 新逻辑名/本地图集路径、重复逻辑名、坏清单、asset-only 收集和示例资源；`test_mod_content` 校验示例图标/精灵与资源清单。Debug **493/493（124428 assertions）**、Release **493/493（124429 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；实际窗口启动日志确认基础图集与 `mods/example_content/assets/atlas_example.atlas.json` 加载（资源清单 1、新增绑定 2）；未提交，摆件/图标视觉与实机长时运行待验收。

**限制/风险**：资源只支持新增逻辑名，不能覆盖基础逻辑名；asset-only mod 无物品/配方/建造增量；资源不参与内容指纹（2026-10-28 起 mod 文件树纳入指纹，基础 assets 仍未纳入），服务器不加载贴图；资源修改需重启；图集加载路径未在无渲染单测中真实创建 GPU 纹理，需实机确认图标/建造精灵。

## 2026-10-26 X-F2b-2b Mod 搜刮表与 POI

**目标**：让 mod 在已有物品/配方/建造/图集之外，继续用纯数据追加搜刮表和 POI，复用现有容器、守军、任务奖励与 HUD 指引。

**实现**：`LootTableRegistry` 新增 `MergeFile`/`MergeFromString`：先用临时注册表完整解析 `loot-v1`，再检查与基础/更早 mod 的表 id 冲突；冲突时整份文件拒绝，不修改已有表，基础 `seed` 保持不变。`game::MergePoiFile` 读取 `pois-v1`，通过临时 `Config` 复用现有 POI 严格解析，再只追加新 id；基础 `enabled/guideRange` 不被 mod 覆盖。`LoadModContent` 收集 `content/loot.json`/`content/pois.json`，缺失/格式错误按现有 per-mod 规则跳过；`GameApp` 在物品加载后合并战利品表、加载基础 POI 后按 mod 顺序合并，并写入 `GameContext::poiConfig`；`WorldDemoState` 复制该配置，不再重复解析基础 `config`。

**示例**：`mods/example_content` 新增 `mod_camp_cache`（引用 mod 物品 `camp_stew`）与 `mod_depot`（坐标 54.5/28.5，容器使用 `mod.supply_crate`，任务奖励 `camp_stew`）。启动时自动生成容器与 1 名守军，HUD 复用现有 POI 指引。

**验证**：`test_loot` 覆盖 mod 表追加、基础 seed 保持、冲突整份拒绝；`test_poi` 覆盖 mod POI 追加、重复 id 跳过、错误 format 拒绝；`test_mod_content` 覆盖 loot/POI 文件收集、示例和实际合并。Debug **497/497（124492 assertions）**、Release **497/497（124493 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；实际窗口启动确认战利品表合并 1/1、POI 4 个区域，并生成 `mod_depot` 容器/守军；未提交，POI 视觉/搜刮交互待实机验收。

**限制/风险**：搜刮表/POI 当时不参与内容指纹，服务器不加载 POI/容器（2026-10-28 内容指纹 v2 已纳入 loot/POI）；POI 只追加定义，不生成地图房间，坐标与容器可达性由作者保证；mod POI 与基础 POI 使用同一套任务完成规则（目标表首次搜索 + 守军清空）；错误 format/语法会跳过该 mod 的整份内容文件，但战利品表语义错误不会回滚已加载的物品/配方/建造。

## 2026-10-26 X-F2b-2c Mod 任务与事件

**目标**：让 mod 用数据追加任务链、每日任务和随机事件，复用 M19-F3 已有的目标、奖励、HUD、存档与放弃逻辑。

**实现**：新增 `game::MergeMissionFiles`，读取 `mods/<id>/content/missions.json`（`missions-v1`）。每个文件先与基础 `missions.defs` 及同批次已接受任务合并到候选 `Config`，再复用 `LoadMissionConfig` 做完整解析和前置/成环校验；任务 id 重复、条目非法、前置缺失或成环时整份文件跳过，已有任务不变。基础 `enabled/randomSeed` 不变；`LoadModContent` 收集 `missionFiles`，`GameApp` 合并后写入 `GameContext::missionConfig`，`WorldDemoState` 复制该配置，未注入时仍回退基础解析。服务器只读取 mod 内容，不合并/运行任务。

**示例**：`mods/example_content` 新增 `mod_depot_supply`（`poi` 目标 `mod_depot`，奖励 `camp_stew`），启动任务数由 9 增至 10，并自动接取到 HUD。

**验证**：`test_missions` 覆盖同批次跨文件前置、重复 id、坏 format、坏前置与原子回滚；`test_mod_content` 覆盖 `missions.json` 收集、示例合并和非法 format 跳过。Debug **498/498（124524 assertions）**、Release **498/498（124525 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；实际窗口启动确认任务 10 条、`mod_depot_supply` 自动接取，POI 容器/守军正常生成；未提交，任务视觉/交互待实机验收。

**限制/风险**：任务只在基础 `missions.enabled=true` 时追加，且只在单机运行；前置只能引用基础或同批次更早文件/同文件任意任务，不能引用更晚 mod；任务当时不参与内容指纹（2026-10-28 v2 已纳入），服务器不执行；没有新增任务目标类型或 HUD 字段。

## 2026-10-26 X-F2b-2d Mod HUD 字段

**目标**：补齐 Mod API 的 HUD 扩展点，让 mod 不改 C++、不替换核心 HUD 就能向内置 HUD 追加自己的状态行，并让 Lua/Python 保持一致。

**实现**：`ui::HudState` 新增 `HudModField`（`modId/id/label/text/tone`）与 `modFields[8]/modFieldCount`；`GameContext::modHudFields` 保存字段定义和运行时值。`ScriptApi` 新增 `RegisterHudField/SetHudField/ClearHudField/RemoveHudFields`，Lua/Python 绑定为 `Hud.RegisterField/SetField/ClearField`，按 mod id 隔离、最多 8 个，`tone` 限定 `info/warn/ok/fail`；`Hud.GetState()` 始终从 `GameContext` 叠加最新字段，而不依赖上一帧 HUD 快照。`WorldDemoState::RefreshHud` 每帧复制字段；native `UiSystem` 与 `mods/core_ui` Lua/Python 都新增右侧 `mod-fields` 面板和 8 行固定槽位，空文本隐藏行，native 对文本做 RML 转义。mod 禁用时 GameApp 清掉其字段。

**示例/验证**：`example_content` 注册 `depot_status` 并显示“补给站 / 就绪”。新增 `test_script_api` 覆盖注册幂等、更新、非法 tone、未知字段、清空、8 个上限和按 mod 移除；Lua/Python 烟测覆盖三个接口及 `modFields` 快照；`test_ui` 校验 native/`core_ui` HUD 的 8 行结构。Debug **499/499（124611 assertions）**、Release **499/499（124612 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**；实际窗口启动确认 `example_content.depot_status` 注册、core_ui HUD 加载且无脚本错误，窗口截图确认右侧“模组 / 补给站 就绪”面板显示。

**限制/风险**：字段仅客户端会话 UI 状态，不写存档、不参与内容指纹/联机同步；不支持图标、进度条或交互控件，复杂 HUD 仍应使用 `UI` 文档；字段文本过长时由布局自然换行，未做截断；注册字段在 mod 正常卸载后可能保留到进程结束，只有禁用回调会主动清理。

## 2026-10-26 X-F2b-2e 动态内容（物品/配方/建造/POI/任务）

**目标**：在 X-F2a 启动期内容文件之外，给脚本提供单机运行时追加物品、配方、建造、POI 和任务定义的入口，同时保持注册表原子性、建造目录/POI/任务配置一致性和内容指纹可刷新。

**实现**：`ItemRegistry::MergeFromString`/`RecipeRegistry::MergeFromString` 保存最后一次成功加载的源文档，追加时复制源文档 + 新条目并完整重跑现有校验，成功才替换注册表；`game::MergePoiJson` 复用 POI 校验并只追加新 id；`MergeMissionFiles` 额外输出已接受任务源，`ScriptApi::RegisterMissions` 在源文档上追加候选并复用 `LoadMissionConfig` 校验前置/成环。`ScriptApi` 新增 `RegisterItems/RegisterRecipes/RegisterBuildings/RegisterPois/RegisterMissions`，Lua/Python 绑定为 `Content.RegisterItems/RegisterRecipes/RegisterBuildings/RegisterPois/RegisterMissions(json)`，接收完整对应格式文档字符串并返回新增数；建造项复用 `ParseBuildableDefs`/`AppendBuildableDefs` 的 id/sprite 冲突规则。成功注册累加 `GameContext::dynamicContentCount`；物品/配方/建造调用 `contentChanged` 刷新内容指纹，`WorldDemoState` 发现计数变化时重建建造目录并复制最新 POI/任务配置。动态入口只在 `net.mode=direct` 单机可用，建立客户端或主机连接后关闭，避免内容指纹不一致。

**验证**：`test_items`/`test_recipes` 覆盖动态追加、重复、坏引用、部分文档回滚；`test_missions` 覆盖已接受任务源输出；`test_script_api` 覆盖五个 `Content` 接口、回调、禁用模式、重复/坏前置和建造冲突；Lua 烟测覆盖动态物品注册；Debug **508/508（124911 assertions）**、Release **508/508（124924 assertions）**、Python **46/46**、无窗口服务器 **35 物品 / 7 配方 / 24 系统**。临时单机 mod 启动验证动态物品/配方/建造/POI/任务各 +1、内容指纹连续刷新；在 `OnTick` 注册建造后建造目录 15→16。

**限制/风险**：动态内容不写存档，禁用 mod 不回滚已注册定义；动态物品/建造的图标和 sprite 仍需资源清单可解析；动态 POI 不生成地图房间，动态任务复用现有任务系统；联机客户端/服务器不感知动态内容。

## 2026-10-26 M15-T6 自动化验收链路

**目标**：在实机 15~30 分钟验收之前，把战斗/生存主链路的自动化覆盖补成一条可重复的端到端用例，减少只靠分散单测拼装的盲区。

**实现**：`test_combat` 新增 M15-T6 验收链路，使用真实 `assets/items.json` 装备消防斧，覆盖命中/击杀与主手磨损；再用撕咬伤害写入伤口，推进 `WoundSystem` 验证流血；从背包使用绷带，经 `TreatWounds` 止血并确认后续 tick 不再掉血；最后以致死撕咬进入 `DeathSystem` 倒地窗口并生成尸体，同时检查 `LastDamage` 死因。

**验证**：Debug **508/508（124911 assertions）**、Release **508/508（124924 assertions）**、Python **46/46**、无窗口服务器 **180 tick 正常退出（35 物品 / 7 配方 / 24 系统）**。该用例只覆盖自动化逻辑链，不替代新存档无 F6/F7/控制台 15~30 分钟实机流程、截图和死亡重开观感验收。

## 2026-10-26 X-F3 性能基线：12/50/100 僵尸

**目标**：给 X-F3 质量债补一个可重复的低/中/高压力基线，避免只看 500 人口极限场景，无法判断常规战斗规模是否退化。

**实现**：`test_boids` 新增 `X-F3 性能基线：12/50/100 僵尸模拟 tick`，每档 120 tick，使用与 M5 500 人口基准相同的模拟链（空间索引、LOD、感知、AI、Boids、战斗、移动、刷怪/虚拟化扫描与 Flush），不含物理步进和寻路后台。Debug 用 40ms 宽松哨兵，Release 断言 <4ms。

**验证**：Release 12/50/100 分别约 **0.005–0.008 / 0.019–0.030 / 0.043–0.070 ms/tick**；Debug 约 **0.43–0.53 / 1.66–2.21 / 3.37–4.59 ms/tick**。Debug 100 僵尸可接近 4ms，仅作开发参考。全量 Debug **508/508（124911 assertions）**、Release **508/508（124924 assertions）**、Python **46/46**。

**限制/风险**：基准未固定 CPU 亲和性，Debug 数据受开发机负载影响；仓库数量与 chunk 卸载/重载的独立基线见下一节。

## 2026-10-26 X-F3 仓库/chunk 基线与旧档/复制审计

**目标**：补齐 X-F3 剩余两类独立基线，并审计“每次新增字段补旧档兼容、每次新增组件补复制/服务器测试”的执行情况。

**实现**：`test_storage` 新增 1/16/64 仓库基准（2000 次最近查找 + 30 轮全部仓库 TakeAll→StoreAll，每仓 6 栈×10 rag）；`test_chunkmanager` 新增 3 轮 49 chunk 卸载/稀疏加载/文件重载基准，分别记录同步 `Update` 卸载耗时与 `PumpUntil` 全窗口墙钟；`test_replication` 新增 FarmPlot/SpikeTrap/MerchantStock/ZombieVariant 全量往返，补齐此前未覆盖的复制类型。

**验证**：Release 64 仓库 nearest **0.0010–0.0014 ms/query**、transfer **0.47–0.68 ms/round**；chunk 卸载 **4.7–6.5 ms**、加载 **173–181 ms**。Debug 64 仓库 nearest 0.063–0.124 ms、transfer 20–29 ms；chunk 卸载 58–84 ms、加载 180–242 ms。全量 Debug **508/508（124911 assertions）**、Release **508/508（124924 assertions）**、Python **46/46**。

**审计结论**：旧档侧已有 v1 元数据迁移、未知组件跳过、Wounds/Pain/Climate/Infection/ZombieSpecial 旧档缺省、chunk diff v1/v3 兼容与同 saveId 重启持久化覆盖，未发现需补的缺口；复制侧 21/21 注册类型现均有全量往返覆盖。已知 `Generator::load` 为运行期重算字段，不写入存档，重启后由电力系统恢复，属于既有设计。

**限制/风险**：仓库/chunk 基准未固定 CPU 亲和性，chunk 加载包含 1ms 轮询与后台调度，不能当作单块解析耗时；Windows 调度抖动可能导致 Debug 数值波动。

## 2026-10-26 P1 视觉与交互验收清单

**目标**：把散落在 `docs/01`/`docs/05`/`docs/06` 的视觉验收点收成一份可执行的 P1 清单，明确 1280×720/1920×1080 双分辨率、native/Lua 双路径、长文本与组合状态，避免实机时只抽查“正常画面”。

**内容**：新增 `docs/07-视觉与交互验收.md`，覆盖基础 HUD、POI/任务、电力/泛光灯、守夜/破口/战报、Mod HUD、操作提示与归属确认；规定 Release 启动命令、隔离存档、截图命名、构造状态与自然流程的边界、失败记录格式和通过标准。README 文档索引、`docs/01` P1 小节、`docs/05` X-F3、`docs/06` 状态行已加入链接。

**验证**：仅新增文档与索引链接，未改动代码、资源或配置，未运行测试；P1 实机项全部待验。当前自动化只能证明字段/结构存在，不能替代窗口截图和人工操作。

**限制/风险**：HUD 使用固定像素定位，1280 下右侧目标区与 `mod-fields` 在长文本时可能重叠；战报 8 行长文本可能裁切；1920 下面板不会等比放大；native 与 Lua 必须分别启动验证。

## 2026-10-26 P1 HUD 1280/1920 自动布局断言

**目标**：把 P1 清单里的双分辨率、长文本、8 个 Mod 字段和 8 行战报从纯人工抽查变成可重复的布局断言，先覆盖最容易出问题的右侧目标区与 Mod 面板。

**实现**：`tests/test_ui.cpp` 新增 `P1 HUD 1280x720 与 1920x1080 布局`，分别加载 native `assets/ui/hud.rml` 与 `mods/core_ui/ui/hud.rml`。测试填充长目标/POI/任务文案、8 个 Mod 字段和 8 行战报，检查目标区与 Mod 面板不重叠、面板不越出视口、滚动高度不超客户区、战报卡片在视口内且末行不被卡片裁切。`assets/ui/hud.rml` 与 `mods/core_ui/ui/hud.rml` 的战报卡片补 `siege-report-card` id，便于稳定取布局盒。

**验证**：Debug **509/509（125280 assertions）**、Release **509/509（125293 assertions）**、Python **46/46**。1280 下目标区 bottom 约 155.2px、Mod 面板 top 160px，固定样本有约 4.8px 间隙；1920 下同样通过。该用例不替代真实字体/DPI/截图验收。

**限制/风险**：测试文案是固定样本，未覆盖系统 DPI 缩放、字体回退和任意长度 mod 文案；`base-status`/`siege-status` 的视觉压字仍需人工确认；1280 下目标区再增加一行就可能与 Mod 面板接触，后续实机若发现更长文案需调整布局。该 4.8px 风险已由下一节的同列流式布局修复。

## 2026-10-26 P1 右侧与顶部中央 HUD 流式布局

**问题**：原 `hud-objective` 固定 `top:12px`、`mod-fields` 固定 `top:160px`，1280 下长目标区 bottom 约 155px，只剩约 4.8px；目标再多一行或字体略宽就会重叠。中央 `base-status` 固定 700px 宽，与右侧目标区横向重叠；`base-status`/`siege-status`/`threat-status` 固定 74/98/122px，长文本换行后也会互相压住，A3/A6/E2 的“长文本组合”不可靠。

**实现**：两份 `hud.rml` 新增 `#hud-right` 与 `#hud-alerts` 两个透明列容器。`#hud-right` 绝对定位在右上、宽 332px、`min-height:136px`，目标区改为流内相对定位，Mod 面板 `margin-top:12px`、`margin-left:50px`；目标变高时 Mod 面板自动下移，目标隐藏时仍保持最小 top 160px。`#hud-alerts` 绝对定位在顶部中央、宽 700px，基地状态收窄到 560px，基地/守夜/威胁按 DOM 顺序流式排列；`perk-prompt` 下移到 top 200px，避免与状态列重叠。两个容器均显式 `display:block`（避免 RmlUi `div` 默认 inline 导致绝对定位子元素以 body 为包含块）并 `pointer-events:none`，右侧保留 16px 屏幕边距。

**验证**：`test_ui` P1 用例把目标文案加长到三行、Mod 值改为长文本，同时显示完整基地状态、守夜状态、受击状态和 Perk 提示；新增目标/Mod 间距至少 8px、右侧至少 15px 边距、中央状态互不重叠、Perk 位于状态列下方等断言。P1 用例 465 assertions；全量 Debug **509/509（125376 assertions）**、Release **509/509（125389 assertions）**、Python **46/46**。

**限制/风险**：右侧和中央面板会随内容高度下移，极长文案叠加 8 个长 Mod 字段时仍需实机确认不遮挡底部提示；`pointer-events:none` 只用于透明列容器，不承载交互；未覆盖 1024 宽或系统 DPI 缩放。

## 2026-10-26 P1 1280/1920 窗口烟测

**环境**：Release，Direct3D 11，`--game.startState=world --game.startHour=8 --audio.device=off --save.root=saves/p1_smoke`，桌面 1920×1080；截图保存在 `build/`（不入库）。

**结果**：1280×720 客户区分别启动 Lua core_ui 与 native HUD，右侧目标/Mod 面板均保留 16px 边距，目标与 Mod 面板无重叠，底部快捷栏和左侧状态栏完整；默认 core_ui 显示 1 行“补给站 / 就绪”。1920×1080 窗口化外框为 1936×1119，在 1080p 桌面底部被任务栏/标题栏裁切，顶部时钟、目标/Mod 面板和世界区域正常；1920 底部 HUD 未完成实机确认（后续全屏已补，见下节）。WorldDemo 左上调试文本常驻，`F9` 只切网格，不隐藏文本。

**限制/风险**：1920 验收需要桌面可用高度大于约 1120，或增加全屏/无边框启动；当前无独立调试文本开关；截图仍不替代 P1 的 POI/任务、电力/泛光灯、守夜/战报和交互抽查。

## 2026-10-26 全屏启动与 1920 客户区验收

**实现**：`Window::Desc` 新增 `fullscreen`，`config/game.json` 新增 `window.fullscreen`（默认 `false`）；启动时用主显示器全屏创建，失败自动回退窗口化。`Window::ToggleFullscreen()` 保存/恢复窗口位置与尺寸，`Alt+Enter` 在 `GameApp` 原始按键回调中切换；`Window::Create` 记录实际客户区尺寸，`ToggleFullscreen` 切换后同步 `m_width/m_height` 并触发 render/UI resize；过滤窗口创建/模式切换期间 Windows 发出的 0×0 尺寸回调，避免客户区尺寸被瞬时清零。全屏启动后 `Alt+Enter` 恢复窗口时，若原请求尺寸超过工作区则回退到 1280×720。

**验证**：Release 全屏 1920×1080 分别启动 Lua core_ui 与 native HUD，客户区均为 1920×1080，右侧目标/Mod 面板保留 16px 边距、无重叠，底部快捷栏和左侧状态栏完整；`Alt+Enter` 实测 1280×720 → 1920×1080 → 1280×720，日志依次记录 `窗口缩放至 1920x1080`、`全屏切换: 开 1920x1080`、`窗口缩放至 1280x720`、`全屏切换: 关 1280x720`；另测全屏启动后 `Alt+Enter` 恢复为 1280×720（请求 1920×1080 超过 1080p 工作区），未再出现 0×0 尺寸回调。截图：`build/p1-smoke-1920-fullscreen-lua.png`、`build/p1-smoke-1920-fullscreen-native.png`、`build/p1-toggle-02-fullscreen.png`。全量 Debug **509/509（125376 assertions）**、Release **509/509（125389 assertions）**、Python **46/46**。

**限制/风险**：全屏请求分辨率由 GLFW/显示器决定，实际尺寸以日志和客户区截图为准；1080p 桌面全屏可完成 1920 底部 HUD 验收，其他 DPI/多显示器组合仍需实机确认；`Alt+Enter` 在文本输入框中也会触发，后续若需要可改为菜单项或可配置键位。

## 2026-10-26 P1 调试文本开关

**目标**：让 P1 截图能隐藏左上角 `bgfx::dbgText` 统计文本，同时保留 POI/电力/破口等世界标记，避免截图时必须裁剪或保留大段调试信息。

**实现**：`WorldDemoState` 新增 `m_showDebugText`，进入世界时读取 `render.debugText`（默认 `true`）；`DrawDebugText()` 关闭时清空调试文本并直接返回，`DrawDebugOverlay()` 的世界标记不受影响。`config/game.json` 的 `render` 段新增 `debugText`，命令行可用 `--render.debugText=false`。该配置在进入世界时读取，运行中热重载不会切换；`F9` 仍只控制网格线。

**验证**：Release 全屏 1920×1080、Lua core_ui、`--render.debugText=false` 截图确认左上调试文本消失，右侧目标/Mod 面板、底部快捷栏、左侧生命/体力面板和 POI 菱形标记保持正常；该截图同时显示受击/流血 HUD，布局仍正常，路径 `build/p1-hud-1920-lua-debugtext-off.png`。全量 Debug **509/509（125376 assertions）**、Release **509/509（125389 assertions）**、Python **46/46**。

**限制/风险**：仍无运行中按键开关；系统通知可能叠加在屏幕截图上，正式验收应关闭系统通知以免遮挡；本次游戏内缩略图截图在房屋上方出现半透明楼层叠层，屏幕截图未复现，P1 布局证据统一用屏幕截图，不以缩略图代替。

## 2026-10-27 P1 左侧状态堆叠与底部提示布局

**问题**：`hud-level` 固定在 bottom 130px，`hud-vitals` 固定在 bottom 16px 并向上扩展；显示饥饿/流血/疼痛/护甲/感染/天气 6 条状态时，vitals 高度超过 180px，1280/1920 下都会压住等级面板。`notice` 固定 bottom 116px，长 `action-hint` 换行后两者也会重叠。

**实现**：两份 `hud.rml` 新增 `#hud-left` 容器，把等级面板与生命/体力面板改为同列流式排列，容器锚定 left 16px/bottom 16px，vitals 随状态行数向上扩展，等级面板保持 8px 间距；`#notice` 上移到 bottom 130px，避免压住两行操作提示。native 与 `mods/core_ui` 两份 HUD 结构/CSS 保持一致。

**验证**：`test_ui` 新增 `P1 HUD 左侧状态堆叠与底部提示布局`，在 1280×720/1920×1080 下同时显示 6 条长状态、长 `action-hint` 和两行 `notice`；检查等级/vitals 不重叠且间距至少 8px、状态面板不越界、操作提示不压快捷栏、notice 不压操作提示。P1 两项布局用例 562 assertions；全量 Debug **510/510（125473 assertions）**、Release **510/510（125486 assertions）**、Python **46/46**。1280 Lua/native 与 1920 全屏烟测截图：`build/p1-hud-left-1280.png`、`build/p1-hud-left-1280-native.png`、`build/p1-hud-left-1920.png`。

**限制/风险**：左侧容器按内容向上扩展，极长状态行仍可能把等级面板推到屏幕上方；当前只验证 6 条固定长文本，未覆盖系统 DPI 缩放和任意 mod 状态文案；底部 notice 与 action-hint 仍为固定像素定位，三行以上提示组合需实机复核。

## 2026-10-27 P1 世界标记压 HUD 修复

**问题**：1280×720 Release 实机截图中，高危农场仓红色 POI 菱形边缘标记压在左下“饥饿/口渴/疲劳”和天气状态文本上。原因是 POI/电力/破口/寻路标记在 Debug view 提交，而 RmlUi HUD 在 UI view，bgfx 按 view id 先 UI 后 Debug 合成；底边夹取也只考虑屏幕边距，没有避让左下状态面板和底部快捷栏。

**实现**：`WorldDemoState::DrawDebugOverlay` 拆成两段：POI/电力/破口/寻路标记改在 World view 提交，紧接世界精灵之后、RmlUi HUD 之前；Debug view 仅保留网格、悬停格和物理线框。POI 标记保留屏幕边缘夹取，并新增 HUD 安全位：底边避让左侧状态区、中央快捷栏/操作提示、右下背包；顶边避让时钟和右上目标/Mod 区；左右边避让底部状态区与右上目标区。

**验证**：Release 1280×720/1920×1080 全屏烟测，红色农场仓 POI 标记落到 x≈340 的底部安全位，仍在屏幕边缘可见，且不压左下状态文本、快捷栏和操作提示；RmlUi 无错误。截图：`build/p1-poi-edge-safe-1280.png`、`build/p1-poi-edge-safe-1920.png`、native `build/p1-poi-edge-native-1280.png`。全量 Debug **510/510（125473 assertions）**、Release **510/510（125486 assertions）**、Python **46/46**。

**限制/风险**：HUD 安全位按当前固定像素面板尺寸设定，若后续调整状态面板/快捷栏尺寸或加入更多底部提示，需要同步更新；标记在底部安全位会横向平移约 200px，方向仍保持边缘指引但不再严格等于原始夹取点。

## 2026-10-27 P1 A4 面板遮挡与输入恢复烟测

**结果**：Release 1280×720、Lua core_ui，无暂停自然流程。`I` 打开背包时 HUD、POI 世界标记和调试文本隐藏，关闭后 HUD/标记恢复；`C` 打开角色面板（580px 卡片 + 全屏 50% 半透明遮罩，HUD 在遮罩下仍可见），`ESC` 关闭；`ESC` 打开暂停菜单时 HUD 全部隐藏，关闭后恢复；关闭后 `F1` 仍能切换暂停/继续，未出现永久输入锁。截图：`build/p1-a4-inventory-1280.png`、`build/p1-a4-character-1280.png`、`build/p1-a4-pause-1280.png`、`build/p1-a4-input-restored-1280.png`。

**限制/风险**：角色面板不是全屏不透明模态，HUD 在 50% 遮罩下仍可见，是否符合最终观感留待用户确认。`F1` 时间暂停时打开 `C` 的 HUD 刷新限制已在后续 `OnUiFrame` 提交中修复。全量 Debug **510/510（125473 assertions）**、Release **510/510（125486 assertions）**、Python **46/46**（当次快照）。

## 2026-10-27 P1-A4 暂停态 HUD 帧刷新

**问题**：`F1` 暂停后模拟 tick 停止，core_ui 的 `refresh_hud` 只绑定 `OnTick`，因此暂停时按 `C` 虽已更新 HUD 缓存，角色面板仍不即时出现；暂停菜单/状态文本也停在旧值。

**实现**：新增事件 `OnUiFrame`，在 `WorldDemoState::OnFrame` 之后、`UiSystem::Render` 之前每个 UI 渲染帧派发；`EventBridge` 按无载荷事件桥接；core_ui Lua/Python 的 HUD 刷新由 `OnTick` 改为 `OnUiFrame`。正常游玩仍按帧刷新，暂停时 HUD 也能反映最新状态。

**验证**：`test_event_bridge` 增加 `OnUiFrame` 无字段翻译断言；Debug **510/510（125476 assertions）**、Release **510/510（125489 assertions）**、Python **46/46**。Release 1280×720 + `debugText=true` 实机复测：`F1` 暂停后 HUD speed=暂停，`C` 角色面板即时显示，`ESC` 关闭后 `F1` 恢复、输入正常；截图 `build/p1-a4-paused-hud-1280.png`、`build/p1-a4-paused-character-1280.png`、`build/p1-a4-input-restored-1280.png`。

## 2026-10-27 P1 A1/A2/A5 基础 HUD 烟测

**结果**：Release、`debugText=false`、独立存档。A1 1280×720 Lua 站桩 10 秒，顶部时钟、右侧目标/POI/任务/Mod、左下等级/生命/需求、底部操作提示/快捷栏/背包完整；A2 1920×1080 全屏同场景，四角锚点、面板间距与文字均保持，自然僵尸伤害导致生命 28，但不影响布局判定；A5 `--script.enabled=false` native HUD 在 1280/1920 下无空 `mod-fields` 面板，核心等级/生命/体力/需求/天气/目标/POI/任务/快捷栏/操作提示与 Lua 字段、色调一致。native 因关闭 `example_hello` 热键物品为空，属预期差异。

**截图**：`build/p1-a1-base-1280-normal.png`、`build/p1-a2-base-1920-normal.png`、`build/p1-a5-native-1280.png`、`build/p1-a5-native-1920.png`。

## 2026-10-27 P1 A3 + B1-B6 POI/任务 HUD 烟测

**结果**：Release、`debugText=false`、独立存档。A3 用临时长任务名/长目标文案在 1280 Lua 确认右侧目标区自动换行且不压 `mod-fields`；B1 用临时探针定位到加油站东南侧，`poi-guide` 显示威胁/方向/距离/清剿+搜索进度，绿色边缘标记可见；B2 出生点边缘标记夹在屏幕左侧且不压快捷栏。B3 在安全位清 3 名守军后进入加油站，脚本打开背包搜索目标容器：完成 notice、维修包奖励、指引切到诊所、加油站完成标记缩小变灰。B4 默认存档验证一次性任务链与每日任务；随机事件用临时 `dayLengthMinutes=1`、`minDay=1/chance=1` 配置在次日触发，显示 `[幸存者] 空投回收 · 随机事件 · 0/2 · 奖励... · L切换 K放弃`。B5 用 `L` 从电台切到诊所、`K` 放弃诊所并确认其他任务保留、再 `L` 切到农场。B6 用临时长任务名/长目标文案在 1920×1080 全屏确认右侧目标区换行且 `L/K` 未挤出。

**截图**：`build/p1-a3-longtext-1280.png`、`build/p1-b1-poi-guide-1280.png`、`build/p1-b2-poi-edge-1280.png`、`build/p1-b3-poi-complete-1280.png`、`build/p1-b4-mission-guide-1280.png`、`build/p1-b4-once-1280.png`、`build/p1-b4-daily-1280.png`、`build/p1-b5-mission-switch-1280.png`、`build/p1-b5-mission-abandon-1280.png`、`build/p1-b6-mission-1920.png`。

**限制/风险**：B1/B3 的定位/开背包使用临时 Lua 探针，B4 随机事件与 B6 长文案使用临时 config；默认参数下的自然随机、长文案仍应再跑一次。输入自动化在部分窗口切换时偶发丢键，截图前用标题栏点击恢复焦点；不影响游戏逻辑。

## 2026-10-27 P1 C1-C4 电力/泛光灯烟测

**结果**：Release、`debugText=false`、临时探针/建造。C1 在村庄南侧空地建造发电机/泛光灯/冷藏柜，站发电机旁按 `E` 加燃料启动，`base-status` 显示“燃料：479分 · 负载1.5 · 供电中”，底部提示同步为“发电机：供电中 · 负载1.5 · 燃料479分钟”。C2 用临时 `power.fuelMinutesPerCan=2` 验证低燃料黄色警告、耗尽红色“停电”及再次加燃料恢复“供电中”。C3 验证白天“供电中 · 待夜间”、`E` 关闭“泛光灯：关闭”，以及无发电机时“泛光灯：未供电”；照明半径和 HP 均显示。C4 以 `--game.startHour=21` 启动，夜间泛光灯显示可见照明圈和“照明中”提示。

**截图**：`build/p1-c1-power-1280.png`、`build/p1-c2-power-low-1280.png`、`build/p1-c2-power-outage-1280.png`、`build/p1-c2-power-recovery-1280.png`、`build/p1-c3-floodlight-prompt-1280.png`、`build/p1-c3-floodlight-off-1280.png`、`build/p1-c3-floodlight-unpowered-1280.png`、`build/p1-c4-floodlight-night-1280.png`。

**限制/风险**：C1–C3 的建造位置通过临时探针 mod 定位，C2 用临时电源参数；C4 只验证夜间光晕，泛光灯受损/维修/摧毁和 C5/C6 尚未实机。暂停时 `E` 启动后的 notice 与 base-status 存在短暂旧值，等待 2 秒后一致；HUD 刷新本身正常。

## 2026-10-27 P1 A6 组合 HUD 烟测

**结果**：Release、1280×720、Lua、`debugText=false`。临时 Mod 注册 8 个字段（电力/防御/庇护/储水/农田/交易/天气/小队），临时探针传送到村庄空地并建造木墙；临时 `baseEvent.minStructures=0` 使 22:00 直接进入守夜预警，避免首夜结构检查把当夜标记为已处理。截图同时显示顶部中央基地状态（防御 1 完好）与守夜预警（结构 1）、右侧目标/POI/任务和 8 行模组字段；目标区与模组面板无重叠，底部仅保留建造提示。进入波次后再次截图，守夜状态显示“第1/3波 · 存活4 · 栅栏1/0”。

**截图**：`build/p1-a6-combined-1280.png`、`build/p1-a6-siege-waves-1280.png`。

**限制/风险**：A6 使用临时 8 字段 Mod、临时探针/建造和临时 `minStructures=0`，只验证组合布局与可读性；守夜自然触发仍由默认 `baseEvent` 参数覆盖。

## 2026-10-27 P1 C4 泛光灯受损/维修/摧毁烟测

**结果**：Release、1280×720、Lua、`debugText=false`。临时探针传送至村庄空地，建造发电机 `(59,17)`、泛光灯 `(61,18)`、木墙 `(62,18)`；发电机 `E` 启动，泛光灯默认开启（不要再按 `E`，否则会关闭）。临时 `baseEvent.minStructures=0`、`warningSeconds=25`、`waves=[1]`、`maxAlive=1`、`spawnRadius=2`、`building.zombieDamage=2`，事件僵尸攻击泛光灯：夜间照明圈可见、`base-status` 负载 1.5；受损后按 `H` 出现“维修泛光灯：HP 72/80（-1 木材）”；继续攻击后泛光灯被摧毁，照明圈消失、负载回到 1.0。另用临时 `building.zombieDamage=4`、僵尸攻击伤害 0、发电机先启动再建泛光灯，截到受损态提示“泛光灯：供电中 · 照明中 · HP 76/80 · 照明6格 · E开关 · H维修”。

**截图**：`build/p1-c4-built-1280.png`、`build/p1-c4-damaged-1280.png`、`build/p1-c4-damaged-hp-1280.png`、`build/p1-c4-repaired-1280.png`、`build/p1-c4-destroyed-1280.png`。

**限制/风险**：用临时探针/建造与临时事件攻击参数；`building.zombieDamage=2/4` 和僵尸零伤害只为延长观察窗口，不代表默认战斗伤害。C4 已完成；C6 单机 owner=0 归属/只读仍需联机。
## 2026-10-27 P1 C6 归属/拆除二次确认烟测

**结果**：Release、1280×720、Lua、`debugText=false`。临时探针建造木墙 `(59,17)` 和木箱 `(60,19)`。木墙 `X` 首次提示“再次按 X 确认拆除 木墙（3 秒内）”；等待 4 秒后确认提示消失，再按 `X` 重新出现同样的确认提示，墙体仍在，验证 3 秒过期。仓库标题能显示 `仓库：木箱（0/50 kg）（空）`。单机结构 `ownerStableId=0`，`StructureOwnerSuffix` 按设计隐藏归属，因此木墙/仓库标题都未显示“归属：你/无主”；owner 文案与非 owner 只读需要联机双客户端验证。

**截图**：`build/p1-c6-owner-confirm-1280.png`、`build/p1-c6-expired-1280.png`、`build/p1-c6-reconfirm-1280.png`、`build/p1-c6-warehouse-1280.png`。

**限制/风险**：C6 只完成单机二次确认/3 秒过期/仓库标题；单机 owner=0 隐藏归属是有意行为，但无法覆盖“你/其他玩家/无主”和非 owner 只读，需联机补齐。
## 2026-10-27 P1 C5 电网供电/断电/拆除烟测

**结果**：Release、1280×720、Lua、`debugText=false`。临时探针在村庄空地建造发电机 `(59,17)` 与 3 面电网 `(59,16)`、`(61,18)`、`(62,19)`；发电机 `E` 启动后 HUD 显示 `电网 3/3`、负载 4.0。临时 `baseEvent.minStructures=0`、`warningSeconds=20`、`waves=[2]`、`maxAlive=2`、`spawnRadius=2`、`building.zombieDamage=1` 驱动夜间事件；事件僵尸触碰电网时出现可见电弧与“电网电击：夜间事件僵尸受到 25 伤害”，两只僵尸中一只死亡，电网显示 `1 完好/2 损坏`。关闭发电机后提示“停电：发电机已停止供电”，电弧消失、电网仍阻挡。另用关闭事件的临时配置单独验证拆除：建造模式按 `X` 两次拆除后 HUD 回到 `防御 0/0`、负载 1.0，无残留电网或电弧。

**截图**：`build/p1-c5-built-1280.png`、`build/p1-c5-powered-b-1280.png`、`build/p1-c5-off-1280.png`、`build/p1-c5-removed-1280.png`。

**限制/风险**：使用临时电网/事件配置和 3 面电网；电击伤害 25 为默认值，事件波次与僵尸伤害为测试参数。C4 已完成；C6 联机归属/只读仍未完成。

## 2026-10-27 P1 D1–D7 守夜/破口/战报烟测

**结果**：Release，1280×720（D7 1920×1080），Lua，`debugText=false`。临时探针建造木墙，并以临时 `game.dayLengthMinutes=1` 加速等待；D1 在 22:00 前建好木墙 `(61,18)`，预警 HUD 显示“守夜将至 | 来袭 北 | 预计破口 北侧木墙 200/200 | 结构 1”。D2 临时 `baseEvent.minStructures=0`、无玩家结构，HUD 显示“来袭 南 | 无成型防线 | 结构 0”，事件正常开始。D3/D4 建 `(61,18)`、`(58,17)` 两面木墙，东侧木墙被破口后 HUD 出现“已破口 / 新目标 200/200”，日志“尸潮转向：西北”，红色 X 与实际缺口一致；预警目标为橙黄菱形。D5 单墙守住，事件僵尸被击杀后 S 评级结束；`ESC` 关闭战报后时间继续至 04:39、1x，未暂停/结算。D6 让战报跨天保留，下一次预警出现时自动收起（本轮报告为 S；另验证 S/B/C 三评级颜色为绿/黄/红）。D7 用 1920×1080、基地失守的最长结束文本，战报 8 行（损失/波次/结构损失/破口/基地失守/击杀/奖励/来袭）完整可读，卡片不越界，`ESC` 提示可见。

**截图**：`build/p1-d1-siege-warning-1280.png`、`build/p1-d2-no-defense-1280.png`、`build/p1-d3-breach-1280.png`、`build/p1-d3-report-1280.png`、`build/p1-d4-warning-marker-1280.png`、`build/p1-d4-breach-marker-1280.png`、`build/p1-d5-report-1280.png`、`build/p1-d5-report-closed-1280.png`、`build/p1-d6-report-next-night-1280.png`、`build/p1-d7-report-1920.png`。

**限制/风险**：使用临时探针、加速时间与事件/建筑伤害参数；D3 的备用方向切换发生在同一波破口后，未再构造第三次切换（实现上限为一次）；D7 采用基地失守 C 评级的最长 8 行文本，不代表自然流程所有战报都有 8 行。C6 联机归属/只读仍未完成。

## 2026-10-27 P1 E1–E5 Mod HUD 烟测

**结果**：Release、1280×720（E2 另测 1920×1080 窗口化客户区）、Lua/`--script.enabled=false` 各一路、`debugText=false`。E1 默认 `mods` 加载 `core_ui`/`example_content` 等，右侧显示“模组 / 补给站 就绪”单行，目标/POI/任务未被遮挡，日志无脚本错误。E2 用只含 `core_ui`+`p1_e2_hud` 的临时 mods 目录注册 8 个长 label/text 字段，1280/1920 下 8 行全部可见，长行在 260px 面板内换行、未溢出右边界；配合临时长任务配置，右侧长目标/POI/任务与模组面板留有空隙、无重叠。E3 四字段 `info/warn/ok/fail` 分别显示白/黄/绿/红；非法 tone `bogus` 的 `SetField` 返回 false，INFO 文本保持原值。E4 临时 mods 目录排除 `example_content`，初始显示 3 行，`ClearField` 后右侧模组面板整体隐藏且无旧行，重新 `RegisterField`+`SetField` 后 3 行恢复、标签更新为“补给一恢复/…”。E5 `--script.enabled=false` 的 native HUD 无 `mod-fields` 和“模组”标题，无残留行。

**截图**：`build/p1-e1-mod-one-1280.png`、`build/p1-e2-mod-eight-1280.png`、`build/p1-e2-mod-eight-1920.png`、`build/p1-e3-mod-tones-1280.png`、`build/p1-e4-mod-before-1280.png`、`build/p1-e4-mod-empty-1280.png`、`build/p1-e4-mod-restored-1280.png`、`build/p1-e5-mod-native-1280.png`。

**限制/风险**：E2 的 1920 截图使用窗口化客户区，非全屏；E2/E3/E4 使用临时 mod，E4 用 `Game.SetTimer(300/600)` 游戏秒安排清空/恢复（默认 60 分钟/天，实际约 12/25 秒）；E1 默认 mods 还加载 example_hello/example_reactive/example_ui，不参与 HUD 字段断言。未改源码，未重跑测试计数。C6 联机归属/只读仍未完成。

## 2026-10-27 P1 F1–F4 操作提示与边界烟测

**结果**：Release，1280×720，Lua `core_ui`，`debugText=false`，临时探针/配置构造状态。F1 在村庄空地建仓库 `(62,18)`，站到 `(61.3,19.3)` 时提示“按 I 打开仓库：木箱”；泛光灯 E 提示与 C3 一致，受损泛光灯 H 提示与 C4 一致；随后让僵尸近身造成流血，提示切为“流血中：寻找绷带或破布止血”，验证流血优先。墙/门 H 未单独重跑，沿用 C4 受损泛光灯的 H 维修路径。F2 用临时每日巡逻任务、50 木材填满背包（实际 42 木材/3 堆），3 次 F6 击杀后能量棒掉在脚下；修复前 notice 没有掉落说明，修复后为“任务完成：每日巡逻 · 30 生存经验 + 能量棒×1（背包已满，奖励掉落脚下）”。F3 用临时 `game.dayLengthMinutes=1`、100% 需求衰减、寒潮固定 -6°C、`climate.bodyResponseSeconds=1`、1000 HP 与低伤咬击，同屏显示“危险 饥饿 100% 口渴 100% 疲劳 100%”“流血：4 处（3.2 HP/s）”“疼痛：严重 90%”“感染：潜伏 2%”“天气：寒潮 -6°C · 体温 20.0°C · 潮湿 2%”。F4 打开背包后按住 W 1.5 秒，探针位置保持 `69.20,24.34`；关闭背包后 W 移动到 `68.16,23.30`→`66.75,21.88`；暂停菜单下无 tick 位移，点击“继续游戏”后 W 继续到 `64.80,18.52`，输入恢复。

**修复**：F2 暴露出背包满时任务奖励已掉落但 notice 未说明。`WorldDemoState::ReturnOrDropItem` 增加 `bool* dropped` 输出；`GrantMissionReward` 返回是否掉落；`CompleteMission`、POI 完成和单机守夜奖励在装不下时追加“（背包已满，奖励掉落脚下）”。服务端守夜奖励文案未改。Debug **510/510（125476 assertions）**、Release **510/510（125489 assertions）**、Python **46/46**；无窗口服务器计数未重跑。

**截图**：`build/p1-f1-action-hint-1280.png`、`build/p1-f1-storage-1280.png`、`build/p1-f2-reward-drop-1280.png`、`build/p1-f3-status-stack-1280.png`、`build/p1-f4-inventory-open-1280.png`、`build/p1-f4-inventory-block-1280.png`、`build/p1-f4-menu-block-1280.png`、`build/p1-f4-restored-1280.png`；F1 的 E/H 复用 `build/p1-c3-floodlight-prompt-1280.png`、`build/p1-c4-damaged-hp-1280.png`。

**限制/风险**：F1 的墙/门 H 未单独构造，泛光灯 E/H 与仓库 I 已覆盖；F4 单机无社交面板，聊天输入框与 owner/只读仍待联机/补充项；F2 仅验证单机任务奖励，服务端守夜奖励 notice 未改。临时配置/探针不改变正式数值。C6 联机 owner 归属/只读仍未完成。

## 2026-10-27 M19-F2/F3 联机 POI 与任务同步

**结果**：服务器加载 `pois/missions/loot` 及 mod 合并，按玩家附近已加载 chunk 生成 POI 容器与 `PoiGuard` 守军；`LootContainer` 新增 `ZLR1` 复制组件（表/搜索状态/容量/最多 8 堆内容），`SearchLootContainer` 由服务器校验距离并首次 roll，客户端拖拽/拾取改走服务器 `InventoryAction`。服务器按玩家维护 `PoiTasks`/`MissionLog`，推进搜刮、击杀归属、POI 完成、收集、到达和电台交互，奖励由服务器发放；`ZLP1 v9` 增加 `poiProgress`、任务日志/日期和 `AbandonMission`/`InteractRadio` 命令，客户端只读镜像并恢复 `poi-guide`/`mission-guide`/世界标记。旧存档缺房屋容器只补缺失项，不叠加演示容器；旧 v2–v8 状态仍可解码。

**验证**：新增 `test_network_inventory` 两个联机场景（POI 容器/守军/搜索/取物/完成；房屋搜刮推进无线电任务链与电台修复，服务器任务/POI 奖励满包时追加掉落提示），`test_player_state` 覆盖 v9 POI/任务往返，`test_replication` 覆盖 `LootContainer` 复制。Debug **514/514（125533 assertions）**、Release **514/514（125546 assertions）**、Python **46/46**。联机实机和旧存档实机兼容仍待验收。
## 2026-10-27 M22-F1 联机睡眠与时间投票

**目标**：补齐 M18-F3 唯一仍单机专用的睡眠玩法，让联机时间跳过必须经过全队同意，避免任何玩家单方面推进服务器时间。

**实现**：服务器新增 `SleepRequest`、`SleepVote`、`SleepCancel` 命令。床边发起后记录提议时长/天亮目标与在线存活投票集合，默认 `sleep.voteSeconds=30`；所有在线存活会话同意才进入 `ServerSleepPhase::Sleeping`，拒绝、超时、发起者离开/死亡会取消。睡眠开始时重新计算目标时间，按 `sleep.gameMinutesPerSecond` 推进 `GameTime`，逐玩家 `ApplySleepNeeds`，并在受伤、范围内僵尸、守夜开始、饥饿/脱水、死亡时中断；投票/睡眠期间服务器拒绝同会话的非睡眠命令，客户端锁移动并显示服务器状态。`PlayerState` 升 v10：新增 `PlayerNeedsState`（联机 HUD 显示饥饿/口渴/疲劳）与 `PlayerSleepState`（phase/发起者/目标/投票数/进度/文本），旧 v9 及更旧负载回退默认；客户端 `WorldDemoState` 用 `Y/N` 投票、`ESC` 取消。`config/game.json` 新增 `sleep.voteSeconds` 与 `SleepVoteYes/No` 键位。

**测试**：`test_network_integration` 双客户端覆盖投票拒绝、重新同意、全队睡眠、服务器时间推进、完成、受伤中断、投票超时与取消；`test_player_state` 覆盖 v10 需求/睡眠往返、v9 兼容和非法状态；`test_net` 覆盖三个新命令的定长往返。Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**。

**限制**：投票集合在发起时固定，新加入玩家不参与本次投票；睡眠为会话运行时状态，不写入玩家存档；未做真实双客户端实机验收与断线重连中途睡眠组合。

## 2026-10-27 联机 HUD 补齐

**结果**：`UpdateBaseHud` 不再在网络模式清空快照，客户端按复制实体/chunk 过滤 16 格内玩家建造物、仓库、农田、发电机、冷藏、泛光灯和陷阱，生成只读基地汇总；联机 HUD 同时调用 `RefreshPoiGuide`/`RefreshMissionGuide`，恢复 `poi-guide`、`mission-guide` 与 `base-status`。服务器仍只下发复制状态/电力摘要，客户端不参与结算。

**验证**：Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**；真实双客户端 HUD 视觉验收待补。

## 2026-10-28 M14 联机主目标 HUD

**结果**：`PlayerState` 升 v11，新增 `PlayerObjectiveState{valid,step}`；服务器 `TickServerObjectives` 为每个会话维护 `ecs::Objective`，按零件、电台修复和撤离点推进，`FillPlayerState` 下发；客户端 `ApplyNetworkPlayerState` 只读镜像，`RefreshObjectiveHud` 单机/联机共用。v10 及更旧负载回退 invalid，客户端不再在联机模式隐藏目标行。

**测试**：`test_network_inventory` 断言主目标 0→1→2→3，`test_player_state` 覆盖 v11 往返与 v10 回退；Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**。

**限制**：真实双客户端视觉验收待补。

## 2026-10-28 联机撤离结算

**结果**：客户端 `ApplyNetworkPlayerState` 收到服务器主目标 `Complete` 后调用 `FinishRun(true)`；联机模式不暂停服务器时间，结算页显示“成功撤离”并提示其他幸存者仍在废土。`MenuPanel::ShowEndScreen` 新增 `showRestart`，联机结算隐藏“重新开始”（避免误开单机新局），只保留返回主菜单以退出会话。

**测试**：`test_network_inventory` 将服务器玩家传送到撤离点并断言 `PlayerState.objective.step == 3`；`test_ui` 断言联机结算隐藏 `end-restart`、保留 `end-main-menu`。Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**。

## 2026-10-28 搜刮容器 8 槽复制

**结果**：库存右侧搜刮区 `kInventoryLootSlots` 从 6 提升到 8，`assets/ui/inventory.rml` 与 `mods/core_ui/ui/inventory.rml` 各补 2 个槽位（仍为 2 行 4 列，不改变面板高度）；`ZLR1` 的 `LootContainer` 复制上限同步从 6 堆提升到 8 堆，超出仍由 UI/协议上限截断。`PlayerStorage` 的 6 堆仓库规则不变。

**测试**：`test_replication` 改为 8 堆内容往返并断言第 8 槽；`test_ui` 覆盖 native/core_ui 两份 inventory.rml 的 `loot-slot-7`，并在 1280×720/1920×1080 断言第 8 槽位于面板内；Lua/Python Inventory API 断言同步为 8。Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**。

## 2026-10-28 ModWatcher 内容指纹

**问题**：Release 全量偶发 `M7-T6` 失败，根因是热重载只比较文件数 + 最大 mtime；测试快速覆盖同尺寸脚本时 mtime 粒度可能不变，热重载漏报。

**修复**：`ModWatcher::Signature` 增加总字节和文件内容指纹；对 ≤256KB 的文件读取内容做 FNV-1a，路径/大小/mtime 一并混入且 XOR 组合，避免目录遍历顺序造成误报。大文件仍依赖 mtime/大小，不额外整文件哈希。

**测试**：`test_hot_reload` 新增“同尺寸同 mtime 覆盖仍触发”用例（写入后恢复原 mtime），并连续跑 20 次；Debug **519/519（125714 assertions）**、Release **519/519（125727 assertions）**、Python **46/46**。同时把 M21-F2b 电网测试的固定 RunForTicks(120) 改为 PumpHostUntil 等待异步 chunk/本地玩家（8s 上限），消除 Release 快速循环下玩家尚未生成就断言的竞态。Release 全量连续 5 次通过。

## 2026-10-28 内容指纹覆盖 loot/POI/任务

**目标**：让联机握手校验覆盖 mod 可追加的搜刮表、POI 与任务定义，避免双方物品/配方一致但掉落、POI 或任务不同导致联机错位。

**实现**：`ComputeContentHash` 升 v2，在 items/recipes/buildings 后追加 loot seed 与表定义（容量/rolls/guaranteed/entries/权重/min/max）、POI（启用/范围/容器/守军/任务奖励）、任务（启用/随机种子/链/目标/奖励/前置）。客户端改为在 loot/POI/任务全部加载后调用 `RefreshContentHash`，服务器在同一批内容加载后计算；动态 `Content.RegisterPois/RegisterMissions` 也触发单机指纹刷新。

**测试**：`test_content_hash` 新增 loot 权重、POI 威胁、任务目标数量变化用例；`test_embedded_server` 按客户端完整加载路径重建 loot/POI/任务并比对服务器指纹。Debug **520/520（125727 assertions）**、Release **520/520（125740 assertions）**、Python **46/46**。

## 2026-10-28 C6 非 owner 只读提示与客户端前置拦截

- `ActionHintState` 新增 `structureReadOnly`：非 owner 门提示改为“无法开门/关门”，受损结构显示“仅所有者”且不再显示 `H 维修`，仓库交互提示追加“（只读）”；联机不再显示 `J 维修全部`。
- `WorldDemoState` 将结构/门/仓库 owner 写入提示（`归属：你/玩家名/无主`）；`TryRepairNearestBarricade` 与 `TryToggleNearestDoor` 在发送 `RepairStructure`/`ToggleDoor` 前按本地 StableId 拦截并提示“只有所有者可…”，避免把必败命令发到服务器。
- 保持单机 `ownerStableId=0` 可操作；拆除继续走既有 request/accept 二次确认与服务器 owner 校验。
- 测试：新增 `C6 owner read-only hints hide edit actions`，覆盖非 owner 开门/关门、受损结构隐藏 H、仓库只读提示；`test_network_integration` 扩展非 owner 维修请求，断言服务器拒绝且墙体 HP 不变、状态提示“属于其他玩家”；Debug **521/521（125740 assertions）**、Release **521/521（125753 assertions）**、Python **46/46**。
- 限制/风险：这是无窗口 UI/规则自动化，未替代真实双客户端视觉验收；owner 名称来自 `SocialState`，未收到名单时仍回退 `玩家 <StableId>`。

## 2026-10-28 F1 受损门 H 维修提示

- `ActionHintText` 的门提示分支在 `canRepairBarricade=true` 时追加 `H 维修（木材 +维修量）`，受损门不再只显示 HP/开关；非 owner 只读门仍不显示 H，保留“仅所有者”。
- 测试：`M18-F4 door hint and repair all count` 新增受损门 H/cost 断言；Debug **521/521（125743 assertions）**、Release **521/521（125754 assertions）**、Python **46/46**。
- 限制/风险：自动化覆盖提示文本，未替代真实门受损后按 H 的实机视觉验收；若门与更近的其他受损结构同时存在，H 仍按“最近可修结构”选择目标。

## 2026-10-28 C6 非 owner 开门服务器回归

- `test_network_integration` 的 X-F1 阶段 2 增加非 owner `ToggleDoor`：将第二客户端移到门前，发送开门请求后断言 `ownershipRejected` 增长且门仍保持打开。
- 与已有非 owner 维修断言共同覆盖 C6 的服务器权威拒绝路径；客户端仍由 `WorldDemoState` 前置拦截。
- 验证：Debug **521/521（125747 assertions）**、Release **521/521（125760 assertions）**、Python **46/46**；Release 首次全量曾出现既有 KCP 丢包代理用例偶发失败，重跑通过。
- 限制/风险：仍是无窗口自动化，真实双客户端 owner 归属/只读视觉未替代。

## 2026-10-28 F4 聊天输入焦点阻断

- 抽出 `ui::IsInputFocusBlocking(Rml::Context*)`，`UiSystem::HasInputFocus` 复用；`M13-T9` 断言打开聊天后焦点为输入框、W 输入被 `SetGameplayBlocked` 清空，提交后焦点解除。
- 修复 `SocialPanel::OpenChat`：输入行从 `display:none` 切为可见后先 `Context::Update()` 完成布局再 `Focus()`；此前直接 Focus 失败，焦点落在 body，聊天期间游戏按键不会被阻断。
- 验证：Debug **521/521（125752 assertions）**、Release **521/521（125765 assertions）**、Python **46/46**。
- 限制/风险：自动化覆盖焦点与输入阻断，未替代真实联机聊天发送/输入视觉验收。

## 2026-10-28 C6 非 owner 仓库读取回归

- `test_network_integration` 在 owner 仓库放入 2 木材，将非 owner 客户端移到仓库旁并发送 `TakeAll`；断言状态提示“附近没有可访问的仓库”，仓库槽位/木材数量不变。
- 覆盖 C6 “仓库只读”的服务器权威路径；客户端仍由 `WorldDemoState` 的 `（只读）` 提示和本地 owner 校验共同约束。
- 验证：Debug **521/521（125758 assertions）**、Release **521/521（125771 assertions）**、Python **46/46**。
- 限制/风险：非 owner 仓库 UI 在真实双客户端中的视觉/拖拽反馈仍待验收。

## 2026-10-28 C6 非 owner 设施交互回归

- `test_network_inventory` 在 owner 农田/发电机/陷阱/泛光灯旁接入第二客户端，把玩家位置同步到设施并 `TeleportBody`，分别发送 `InteractFacility`。
- 断言四次请求都只增加 `ownershipRejected`，农田阶段、发电机运行状态、陷阱布防状态、泛光灯开关均不变；覆盖设施只读的服务器权威路径。
- 验证：Debug **521/521（125779 assertions）**、Release **521/521（125792 assertions）**、Python **46/46**。
- 限制/风险：真实双客户端设施只读提示/视觉仍待验收；测试未覆盖设施 owner 名称在 UI 中的显示。

## 2026-10-28 C6 非 owner 仓库写入回归

- `test_network_integration` 在非 owner `TakeAll` 被拒绝后，向非 owner 服务器背包注入 1 个绷带，先用非法 `UseBag` 刷新状态，再发送 `StoreAll`。
- 断言状态提示“附近没有可访问的仓库”，owner 仓库槽位/木材数量不变，非 owner 背包中的绷带仍在；覆盖只读仓库的写入方向。
- 验证：Debug **521/521（125787 assertions）**、Release **521/521（125800 assertions）**、Python **46/46**。
- 限制/风险：真实双客户端仓库拖拽/写入只读视觉仍待验收。

## 2026-10-28 C6 非 owner 仓库内容不复制回归

- `test_network_integration` 在非 owner 已收到 owner 仓库 Prop 后，检查复制 actor：`hasSprite=true`、`ownerStableId` 正确，但 `hasStorage=false`。
- 覆盖仓库内容只在 owner 视图下编码的隐私边界，避免非 owner 通过复制数据读取库存。
- 验证：Debug **521/521（125791 assertions）**、Release **521/521（125804 assertions）**、Python **46/46**。
- 限制/风险：真实双客户端仓库 UI 的只读视觉仍待验收；测试不覆盖所有复制组件过滤规则。

## 2026-10-28 C6 非 owner 单件拖拽回归

- `test_network_integration` 在 owner 仓库放入 2 木材后，由非 owner 发送 Loot→Backpack 的单件 `Transfer`。
- 断言提示“仓库距离太远或无权访问”，仓库槽位/木材不变，非 owner 背包不会获得木材；覆盖拖拽读路径与 `StorageAccessible` 校验。
- 验证：Debug **521/521（125796 assertions）**、Release **521/521（125809 assertions）**、Python **46/46**。
- 限制/风险：真实双客户端拖拽只读视觉仍待验收。

## 2026-10-28 X-F2b 内容指纹 v3：mod 文件树

- 新增 `game::ComputeTreeHash(script.modsDir)`：递归哈希 mods 文件树，按相对路径排序，哈希文件大小与内容；忽略 `.log/.tmp/.swp/.bak`、尾部 `~` 与 `.DS_Store`，缺失目录返回 0。
- `ComputeContentHash` 升 v3，追加 `extraHash`；`GameApp::RefreshContentHash` 与 `ServerHost` 使用同一 `script.modsDir` 文件树哈希，内嵌服务器测试按完整客户端路径重算并比对。
- 覆盖 mod 脚本/图集/地图/事件等文件变化；`test_content_hash` 断言文件树确定性、文件内容变化、缺失目录与 content hash 敏感性。
- 验证：Debug **522/522（125803 assertions）**、Release **522/522（125816 assertions）**、Python **46/46**。
- 限制/风险：基础 `assets` 图集/地图/事件仍未纳入；mods 文件变更需重启/重连握手，不触发热重载指纹刷新。

## 2026-10-28 C6 单机本地 owner 门控

- 抽出 `game::CanEditStructure(owner, localId)`：无主结构允许操作，有主结构只允许同一稳定 ID，本地 ID 为 0 时拒绝有主结构；客户端本地、`PlayerInventoryService` 与 `ServerCanEditStructure` 复用同一规则。
- `WorldDemoState` 单机仓库存取补门控：单件存入、取物、取物装备、全部存入/取出、整理都先校验仓库 owner；非 owner 只提示“该仓库属于…只有所有者可存取”，不改库存。
- 单机设施 E 交互按 owner 前置拦截；维修全部候选与受损结构计数排除非己方结构，避免绕过单件维修限制消耗木材。
- 测试：`test_building` 新增 owner 规则边界（无主/本人/他人/本地 ID 缺失），Debug **523/523（125808 assertions）**、Release **523/523（125821 assertions）**、Python **46/46**。
- 限制/风险：单机完整 UI 路径仍待实机验收；真实双客户端 owner 视觉仍待补。

## 2026-10-28 内容指纹 v4：忽略 mod 生成文件

- 实际 mods 下存在 `core_ui/scripts/__pycache__/main.cpython-39.pyc`；v3 会把 Python 版本相关字节码纳入指纹，导致相同 mod 源码在不同 Python/运行环境下握手失败。
- `ComputeTreeHash` 跳过 `.git/.svn/.hg/__pycache__/.pytest_cache/.mypy_cache/.ruff_cache/.vs/.idea/node_modules` 目录，并忽略 `.pyc/.pyo`、编辑器临时文件、OS 缩略图与 `~$` 临时文件；目录在递归迭代器中直接 `disable_recursion_pending`。
- 内容指纹版本升至 v4；`test_content_hash` 覆盖新增/修改 `.pyc`、`.git/index`、`.pytest_cache` 与 `Thumbs.db` 后指纹不变，真实脚本改动仍改变指纹。
- 验证：Debug **523/523（125810 assertions）**、Release **523/523（125823 assertions）**、Python **46/46**。
- 限制/风险：基础 `assets` 仍未纳入；mods 文件变更需重启或重新握手，v4 与旧 v3 自动指纹不兼容。

## 2026-10-28 C6 维修/开门目标避开非己方结构

- `FindNearestPlayerBuiltStructure`、`FindNearestFloodlight`、`FindNearbyDoor` 新增 `editableOnly` 过滤；H 维修、受损泛光灯维修和 E 开关门使用该过滤，优先选择己方/无主目标。
- 提示仍扫描最近任意归属结构以显示只读；仅当范围内没有己方可操作门时，开关门才回退显示归属提示，避免最近他人门抢占行动。
- 验证：Debug **523/523（125810 assertions）**、Release **523/523（125823 assertions）**、Python **46/46**；本地 UI 路径仍待实机验证。

## 2026-10-28 M23-F1-T1 远程武器数据契约

- `ItemDef` 新增 `RangedCombat` 可选对象：damage/range/spreadDegrees/pellets/cooldownSeconds/magazine/reloadSeconds/ammoDefId/noiseRadius/wearPerShot/projectileSpeed/falloffStart/falloffEnd/falloffScale；仅 weapon 可用，14 个字段全部必填。
- `ItemRegistry` 校验类型、范围、`pellets`/`magazine` 整数、`falloffStart <= falloffEnd <= range`、弹药 id 字符集，并在完整表加载后校验 `ammoDefId` 已注册；错误仍整体拒绝。
- `ContentHash` 升 v5，包含 ranged 存在标志与全部字段；`assets/items.json` 从 34 增至 38，新增 `pistol`/`shotgun`/`ammo_9mm`/`ammo_shells`（占位图标，弹药暂为 generic）。
- `test_items` 覆盖合法解析、缺省、非 weapon、未知/缺字段、越界、非整数、falloff 顺序与坏弹药引用；`test_content_hash` 覆盖 ranged 字段变化；Python `test_gear_data` 覆盖数据契约与引用。
- 验证：Debug **525/525（125882 assertions）**、Release **525/525（125895 assertions）**、Python **47/47**、无窗口服务器 **39 物品 / 7 配方 / 24 系统**。
- 限制：射击/换弹/噪声引怪/联机同步尚未接线；枪械与弹药尚未加入 loot、商人和任务奖励；v5 与旧 v4 自动指纹不兼容。

## 2026-10-28 M23-F1-T2 弹匣状态与旧档兼容

- `ecs::ItemInstance` 新增 `loadedAmmo`（0=空，非远程物品忽略）；`Valid()` 拒绝负数。`InventoryOps` 的加物品、取物、装备/卸下、堆叠移动均保留该字段，合并要求弹匣状态一致。
- Item/Container/Equipment 存档组件升 v2，物品负载追加 `int32 loadedAmmo`（0~200）；`ComponentCodec` 增加最低兼容版本，`EntityStore` 对这三个组件接受 v1，旧档按空弹匣读取，其他组件仍精确匹配。
- `test_inventory` 覆盖背包→取出→装备→卸下→放回背包的弹匣状态保留；`test_entity_save` 覆盖玩家背包/主手/地面三种状态往返，以及旧 v1 Item/Container/Equipment 默认 0。
- 验证：Debug **528/528（125942 assertions）**、Release **528/528（125960 assertions）**、Python **47/47**。
- 限制：枪械仍不可射击/装填；`PlayerState`/`ZLR1` 网络复制、服务器扣弹和 HUD 后续任务处理。

## 2026-10-28 M23-F1-T3 单机射击状态机

- 新增 `items::RangedWeaponRuntime`：开火前置检查、冷却递减、换弹计时/装填量锁定，以及切枪/损坏取消；运行时状态不入存档。
- 单机 `V` 朝鼠标格方向开火，`R` 在主手为远程武器时优先换弹；开火扣 `loadedAmmo`、按 `wearPerShot` 磨损 `condition`，并写窄扇区 `AttackIntent` 占位命中。
- 换弹从背包扣 `ammoDefId`，按缺口/存量/空间最小值装填；移动不打断，切枪/卸下/丢弃/损坏/受击打断；空仓不自动换弹。
- 新增 `test_ranged_weapon` 覆盖开火条件、冷却、换弹计时/装填量/中断；Debug/Release/Python 全绿。
- 验证：Debug **531/531（125982 assertions）**、Release **531/531（125995 assertions）**、Python **47/47**。
- 限制：T4 才接投射物、散布/多弹丸、距离衰减和表现；T5 枪声引怪、T8 HUD、T6/T7 服务器权威与联机复制尚未开始。

## 2026-10-28 M23-F1-T4 投射物与表现

- 单机开火从 `AttackIntent` 窄扇区占位改为本地 `ecs::Projectile`：按 `spreadDegrees` 生成 `pellets` 条弹道，复用 Position/Velocity 积分与 `projectile.bullet` 渲染；弹丸按 `range`/寿命/撞墙/首个目标销毁。
- `ecs::Projectile` 增加起点/最大距离/falloff 字段；`items::RangedDamageAtDistance` 按飞行距离插值伤害，`RangedSpreadOffsetDegrees` 用 shotSerial+pelletIndex 生成可复现散布。服务器投射物保留默认不启用衰减。
- 表现：枪口短闪光、枪声音频、命中/撞墙 impact 特效与音效；命中仍走 `CombatSystem::ApplyDamage`，无穿透、无弹壳。F7 降级为调试枪声，V 单机为正式开火、联机暂保留演示投射物。临时 Projectile/TransientEffect 不写入单机/服务器世界存档。
- 新增 T4 纯函数测试；Debug **532/532（125993 assertions）**、Release **532/532（126006 assertions）**、Python **47/47**。
- 限制：枪声未走 `OnSound`/AI 引怪（T5）；HUD、服务器权威/联机弹道（T6/T7）、loot/商人获取（T9）未接；移动散布、后坐力、弹壳未做。

## 2026-10-28 M23-F1-T5 声音引怪

- 新增 `game/RangedSound.h::MakeRangedGunshot`，从 `ranged.noiseRadius` 生成 `Gunshot` `SoundEvent`；单机开火经 `SoundManager::EmitWorldSound` 进入 `OnSound`，同时驱动音频与 `PerceptionSystem`，移除 T4 的直接枪声播放，避免双播。
- 复用既有 `CountBlockingCells`/`IsSoundAudible` 隔墙衰减；范围内 Idle/Wander AI 进入 `SearchLastKnown`，以声源为 lastKnown，已有视觉目标时保持 Chase。手枪 28、霰弹枪 40 均由数据驱动；消音器暂不支持。
- `test_ranged_weapon` 覆盖枪声事件类型/半径/坐标；`test_perception` 覆盖远程枪声经事件总线吸引僵尸搜索；既有隔墙衰减/全链测试继续通过。
- 验证：Debug **534/534（126002 assertions）**、Release **534/534（126015 assertions）**、Python **47/47**。
- 限制：单机本地事件；服务器权威枪声/弹道、联机复制、HUD、消音器和实际枪械获取（T9）未接。

## 2026-10-28 M23-F1-T6 服务器权威

- ZLC1 新增 `FireRanged`/`ReloadRanged`；旧 `FireProjectile` 命令号保留（T7 起安全转换为 `FireRanged`）。客户端网络模式 V 发送瞄准方向、R 发送换弹/装备请求；服务端不信任客户端弹匣、伤害或冷却。
- `ServerHost` 新增服务器侧 `RangedWeaponRuntime`，按主手物品校验耐久/`loadedAmmo`/换弹/冷却/方向，按 `pellets/spread/speed/range/falloff` 生成弹丸并扣弹/磨损；`TickServerRangedWeapons` 推进换弹，完成时从背包扣弹，切枪/卸下/死亡/损坏取消。
- `UpdateProjectiles` 增加最大距离与距离衰减；`combat.pvp` 默认 false，弹丸 `hitPlayers` 默认 false，开启后才会命中其他玩家。枪声用物品 `noiseRadius` 进入服务器 `OnSound`。
- 新增联机权威测试：拒绝无武器射击→装备手枪/弹药→命中且扣弹→冷却拒绝→默认 PvP 下玩家目标不受伤害→换弹回满并扣背包。
- 验证：Debug **535/535（126022 assertions）**、Release **535/535（126035 assertions）**、Python **47/47**。
- 限制：弹匣/换弹进度由 T7 补齐；客户端 HUD、旧负载回退、双客户端/服务器枪声广播待 T7/T10；测试为直接注入装备/背包。

## 2026-10-29 M23-F1-T7 联机复制与反作弊

- `PlayerState` 升 v12：装备槽尾部添加 `loadedAmmo`，新增 owner-only `PlayerRangedState`（弹匣/备弹/换弹进度/本次装填量/服务器发号）；服务器从主手 `Equipment`、`RangedWeaponRuntime` 和背包备弹填充，客户端写回装备镜像并缓存只读状态。
- 反作弊与兼容：负弹匣在 `CanFire` 拒绝；重复开火/超射速由 per-session 冷却拒绝；零方向 `FireRanged` 回退服务器 `Facing`，旧 `FireProjectile` 目标 StableId 转方向后仍走 `FireRanged`；旧 v11 `ZLP1` 缺尾时弹匣 0/远程状态 invalid。
- 投射物 `ownerStableId` 继续随 `ZLR1` 实体头复制；T7 联机测试捕获并校验 shooter owner。
- 新增测试：`test_network_inventory` 覆盖弹匣/备弹/换弹进度/换弹完成扣弹/投射物 owner/冷却/零方向回退；`test_player_state` 覆盖 v12 往返、非法弹匣/换弹字段与 v11 回退；`test_ranged_weapon` 覆盖负弹匣拒绝。
- 验证：Debug **537/537（126070 assertions）**、Release **537/537（126083 assertions）**、Python **47/47**。
- 限制：远程玩家弹匣仍为 owner-only，不进入 `ZLR1`；HUD 待 T8；网络枪声广播/双客户端/实机验收待 T10/T11。

## 2026-10-29 M23-F1-T8 HUD/输入/提示

- HUD 左下新增 `ranged-status`：显示武器名/弹匣/备弹，换弹时显示百分比进度，空仓提示 R 换弹，无备弹显示失败色；低弹匣/备弹耗尽切警告色。`ui::RangedStatusText` 统一文案和 tone。
- 单机从主手 `Equipment.loadedAmmo`、`RangedWeaponRuntime.reloadLeft` 与背包弹药数量刷新；联机从 T7 的 owner-only `m_networkRanged` 刷新，远程玩家仍不广播弹匣。
- `HudState` 新增 `rangedValid/rangedName/rangedLoaded/rangedMagazine/rangedReserve/rangedReloading/rangedReloadProgress/rangedReloadRemaining/rangedStatus/rangedTone`；native `UiSystem`、`mods/core_ui` 的 Lua/Python HUD 与 `Hud.GetState()` 同步。
- 输入提示：装备枪械时默认提示显示 V 开火/R 换弹；空仓显示“空仓：按 R 换弹”；建造模式 R 明确显示“建造优先，不换弹”，实际仍是建造旋转优先。
- 测试：`test_ui` 覆盖文案/tone、双 HUD 元素与 1280/1920 左侧 7 条状态布局；`test_script_ui`/`test_python_api` 覆盖脚本新字段。Debug **538/538（126091 assertions）**、Release **538/538（126104 assertions）**、Python **47/47**。
- 限制：仅文本行，无弹匣图标/进度条；实机视觉验收、枪械 loot/商人（T9）、网络枪声广播/双客户端验收（T10/T11）未完成。

## 2026-10-29 M23-F1-T9 搜刮/交易/平衡

- `loot.json` 新增枪械/弹药获取：加油站储物柜 `pistol` 权重 1 + 9mm 8–12 发，农场仓物资箱 `shotgun` 权重 1 + 霰弹 4–6 发；村庄储物柜、房屋工具柜、农场工具柜补充少量对应弹药。枪械单件、权重低于常用物资，以 loot 权重表达稀有度。
- 商人保留绷带/抗生素/烤豆子图纸，新增 9mm×12（2 木材、库存 2）与手枪×1（8 木材、库存 1）；种子/燃料改由农田、农场 POI 与加油站取得。`MerchantStock` 仍 5 档，`ClampMerchantStock` 在单机/服务器补货前把旧档库存限制到新报价上限，避免旧档索引映射导致手枪超库存。
- 加油站 POI 完成奖励改为 9mm×12，农场仓改为霰弹×6，描述与配置同步；诊所奖励不变。单机/服务器 POI 奖励复用同一字段，开局背包不生成枪械/弹药。
- 伤害/噪声沿用 `items.json`：手枪 18/28、霰弹枪 11×6/40；手枪商人价 8 木材、霰弹枪仅在农场 POI 随机掉落，避免默认开局送枪。
- 测试：`test_loot` 覆盖 loot 条目/权重/数量、枪械伤害/噪声与商人报价物品引用；`test_homestead` 覆盖新报价成本/库存与旧档库存钳制；`test_poi` 覆盖加油站/农场奖励。Debug **539/539（126181 assertions）**、Release **539/539（126194 assertions）**、Python **47/47**。
- 限制：枪械无保底获取，需多次搜刮/开新档；实机拾取/交易与双客户端验收留 T10/T11。

## 2026-10-29 M23-F1-T10 测试与验收

- 单机新增 T10 用例：手枪逐发磨损到损坏，第二发后 condition=0，开火与换弹都以“weapon broken”拒绝。
- 联机新增真实双客户端用例：`--combat.pvp=true`，A 装备 8 发手枪，B 为第二玩家；A 开火后服务器扣弹 8→7，B 服务器/客户端生命同步 100→82，第二发后 64；常驻投射物验证两个客户端都收到 owner 与 projectile archetype。
- 反作弊/重连：武器损坏后再开火不扣弹、不追加枪声/投射物；A 断线后同 identity 重连复用 StableId，恢复 `loadedAmmo=6`、`condition=0`、备弹 8，`profileRestores>0`。PvP 关闭路径继续由 T6 玩家目标不掉血用例覆盖。
- 测试：`test_ranged_weapon`、`test_network_inventory` 新增 T10 用例；Debug **541/541（126238 assertions）**、Release **541/541（126251 assertions）**、Python **47/47**。
- 限制：双客户端测试用物理传送保证弹道可重复；真实鼠标瞄准、拾取/交易、客户端枪声/枪口画面和 HUD 观感留 T11 实机验收。
## 2026-11-01 M23-F1-T11 文档与实机烟测

**文档**：README 新增远程武器/近战取舍；`docs/07` 新增 P1-K 两把枪的 1280 验收用例；`docs/05`/`docs/08`/`docs/01` 更新 F1-T11 状态与限制。

**截图**：Release 1280×720 隔离存档，临时探针添加并装备枪械、生成 1 名靶子僵尸；`build/p1-f1-pistol-1280.png` 显示 `手枪 7/8 · 备弹 8`、`fire: 手枪 7/8`，日志记录 1 弹丸/18 伤害/噪声 28；`build/p1-f1-shotgun-1280.png` 显示 `霰弹枪 4/5 · 备弹 7`、`fire: 霰弹枪 4/5`，日志记录 6 弹丸/11 伤害/噪声 40。日志：`build/f1-t11-pistol-os.out.log`、`build/f1-t11-shotgun-os.out.log`。

**取舍**：手枪 18 伤害/0.45 秒/8 发/噪声 28，中距离点射；霰弹枪 11×6/0.9 秒/5 发/噪声 40，近距爆发但散布/衰减大；两者换弹 2/3 秒、弹药有限且引怪。厨刀 42/0.32 秒、消防斧 65/0.7 秒无需弹药且安静，但必须贴身。

**验证**：单机日志确认装备、换弹完成、逐发扣弹、枪声计数与 HUD；不新增运行时逻辑，基线保持 Debug **638/638（129656 assertions）**、Release **638/638（129670 assertions）**、Python **78/78**。

**限制**：临时探针添加物品/生成僵尸，不是自然拾取、交易或战斗；未覆盖双客户端枪声/枪口/投射物画面；1920 未单独留证。未提交。

## 2026-10-29 M23-F2a-T1 觅食/钓鱼资源节点数据

- 新增 `assets/forage.json`（`forage-v1`）：森林/草地按 `tile.floor.grass` + 邻近 `object.tree` 区分，密度/间距/单区上限/刷新分钟/战利品表均可配；钓鱼点按 `waterFloors` 记录水格，并从岸边可走格生成。
- 新增 `game::ForageRegistry`：严格校验字段范围、重复 id、地块/邻近物体/POI/战利品表引用；支持 `content/forage.json` 原子追加，接入 `LoadModContent` 与 `GameApp` 启动加载。
- 新增确定性规划：`PlanForageNodes/PlanFishingSpots` 读取已加载 World，跳过房间/阻挡/水面自身，遵守周边物体、POI 半径、密度、间距和上限；节点不写地图母文件。
- 产出先行：新增树枝/石块/野果/野蘑菇/草药/小鱼/鱼饵/鱼竿/河漂杂物与 `forage_forest/forage_meadow/forage_shore/fish_water` 四张表；烹饪/腐败深化仍留 T4。当前地图没有 `tile.floor.water`，实际水岸留 T3。
- 测试：`test_forage` 覆盖内置数据、解析/校验、mod 追加、地形/树木/水面/房间/阻挡/POI 确定性规划；`test_mod_content` 覆盖 forage 文件进入结果；Python `test_forage_data` 交叉校验物品/表。Debug **546/546（126529 assertions）**、Release **546/546（126542 assertions）**、Python **52/52**。
- 限制：仅数据与规划，不含 E 搜索、耗尽/刷新、鱼竿/鱼饵、HUD、联机权威；内容指纹暂未覆盖 forage，联机一致性留 T6/T7。F1-T11 实机烟测按约定暂缓。

## 2026-10-29 M23-F2a-T2 觅食交互

- `forage-v1` 区域增加 `searchSeconds/survivalXp/toolDefId/toolWear`；基础森林 2.6 秒，草地 2.0 秒且需要一根 `forage_stick`（成功搜索磨损 0.02）。
- 新增 `ForageRuntime`：`ForageSearchSeconds` 叠乘生存技能、雨/雾/寒潮/夜晚修正；`ForageNodeReady/ForageMarkHarvested/ForagePrune` 用 `TotalGameMinutes` 管理节点耗尽与刷新；`ForageLootSeed` 提供节点确定性 roll 盐。
- `WorldDemoState` 每 1 秒或移动超过 1 格重规划玩家周围 10 格节点；E 在门/床/设施后、电台与使用背包前触发，搜索期间锁移动，E/攻击/受击/开背包/离开范围中断。完成时按 loot 表 roll，背包满则剩余掉落脚下，成功后发生存经验并存 `ecs::ForageState`（`FRGS` v1，最多 256 条）。
- 临近节点绘制绿色菱形，耗尽灰色、搜索目标黄色；HUD 提示可搜索/需要工具/已耗尽/进度百分比。搜索目标/节点不写地图母文件；独立 HUD 留 T5，联机权威留 T6。
- 测试：`test_forage` 新增耗时/天气/技能、耗尽/刷新/种子、提示文案；`test_entity_save` 新增 `ForageState` 往返；Python 数据测试校验新增字段与工具引用。Debug **550/550（126581 assertions）**、Release **550/550（126594 assertions）**、Python **52/52**。
- 限制：单机闭环；钓鱼水岸、鱼竿/鱼饵、烹饪联动、服务器重复领取/距离反作弊和产出率调参分别留 T3/T4/T6/T7/T8。

## 2026-10-29 M23-F2a-T3 钓鱼闭环

- `forage-v1` 钓鱼点增加 `trashTableId/waitSeconds/biteWindowSeconds/catchChance/survivalXp/rodDefId/rodWear/baitDefId/baitCost`；基础静水点等待 5 秒、咬钩窗口 2 秒、基础成功率 0.62，成功鱼获与 `fish_water_trash` 杂物分表。
- 新增 `FishingRuntime`：等待时间按生存技能与雨/雾/寒潮/夜晚修正；成功率按技能/天气/夜晚修正并钳制 0.05~0.95；`FishingLootSeed/FishingAttemptSucceeds` 提供确定性成败与鱼获 roll 盐。
- `WorldDemoState` 复用资源扫描缓存生成岸边钓鱼点；E 优先钓鱼再觅食，抛竿立即消耗鱼饵并锁移动，等待结束进入咬钩窗口，E 收杆结算；主动取消/攻击/受击/开背包/离开范围中断。成功发经验，失败从垃圾表取物或空手；鱼竿按 `rodWear` 磨损并在耗尽时移除。
- 地图新增 `tile.floor.water` 图集帧与村庄南侧池塘：`gen_map.py` 生成水面与可走泥岸，`ApplyChunkData` 将水面地块标记为阻挡，`PlanFishingSpots` 只选可走岸格。HUD 显示需要鱼竿/鱼饵、等待/咬钩进度，场景内蓝色菱形标记钓鱼点。
- 测试：`test_forage` 新增钓鱼数据、等待/成功率、种子/成败/水面校验、提示文案与真实池塘规划；`test_maploader` 新增池塘水面阻挡/泥岸可走；Python 校验新字段、鱼获/杂物分表与实际地图水面。Debug **556/556（126656 assertions）**、Release **556/556（126669 assertions）**、Python **53/53**。
- 限制：仅单机；钓鱼点耗尽/刷新（`respawnMinutes` 预留）、鱼/垃圾产出率、烹饪联动、联机服务器权威/重复领取与距离反作弊留 T4/T6/T7/T8。

## 2026-10-29 M23-F2a-T4 产物与烹饪

- `assets/items.json` 新增 `cooked_small_fish/cooked_wild_berries/cooked_wild_mushroom/herbal_tea`：熟食提升饥饿恢复并按 `spoilDays` 腐败；生蘑菇 2 点生命惩罚在烤熟后移除；草药茶消耗净水煮制，饮用回收空瓶并提供 90 秒弱感染抑制。
- `assets/recipes.json` 新增四个篝火配方与无站点 `craft_fishing_rod`（2 树枝 + 1 破布）、`craft_fish_bait`（1 小鱼 → 3 鱼饵）；`forage_meadow` 增加鱼饵掉落，闭合 T3 钓鱼的普通获取路径，小鱼可在食物与鱼饵之间取舍。
- 沿用现有 `ItemRegistry/RecipeRegistry/Crafting/Spoilage`，无新存档组件；重量、堆叠、腐败和空瓶回收全部由数据驱动。
- 测试：`test_food` 新增实际资源链（烹饪、茶、鱼竿、鱼饵、空瓶回收、重量/堆叠/腐败）；`test_items`/`test_recipes` 校验内置 51 物品/12 配方；Python `test_food_data` 与 `test_forage_data` 校验新成品、配方和草地鱼饵。Debug **557/557（126797 assertions）**、Release **557/557（126810 assertions）**、Python **54/54**。
- 限制：仅基础烹饪数据接入，未新增食物中毒/营养/批量烹饪（F5）；钓鱼点刷新与服务器权威留 T6/T7；产出率平衡留 T8。

## 2026-10-29 M23-F2a-T5 HUD/提示

- `HudState` 新增觅食/钓鱼结构化字段与 `forageStatus/forageTone`、`fishingStatus/fishingTone`；`ui::ForageStatusText`/`FishingStatusText` 统一“可觅食/觅食中/已耗尽”和“可钓鱼/钓鱼中/咬钩”文案、耗时、缺工具/鱼竿/鱼饵、耗尽刷新与咬钩剩余秒数。
- `WorldDemoState` 复用现有最近节点/钓鱼点评估和 `ForageAction`/`FishingAction`，活动状态优先；完成采集/钓鱼的收获、垃圾、背包满掉落、错过收杆继续由中央 notice 提示。
- native 与 core_ui 两份 `hud.rml` 增加 `forage-status`/`fishing-status`，RCSS 与 Lua/Python core_ui 控制器同步；Lua/Python `Hud.GetState()` 暴露同名字段，空状态隐藏。
- 测试：`test_ui` 新增文案/色调、双 HUD 元素与 1280/1920 九行左侧布局；`test_script_ui`/`test_python_api` 覆盖字段。Debug **558/558（126830 assertions）**、Release **558/558（126843 assertions）**、Python **54/54**。
- 限制：仅单机 HUD；联机 forage/fishing 状态留 T6；文本行未做图标/进度条，实机视觉验收未做。

## 2026-10-29 M23-F2a-T6 联机权威

- `ServerHost` 新增 `ForageRegistry` 加载（基础 + mod）与附近节点/钓点规划，使用全局 `ForageState` 管理耗尽/刷新；`ComputeContentHash` 升 v6，纳入 forage seed、区域和钓点全部字段。
- `PlayerState` 升 v13：owner-only `PlayerSurvivalState`（模式/阶段/坐标/进度/耗时/咬钩剩余）及最多 32 个觅食节点、12 个钓鱼点；v12 及更早回退空闲/空列表。客户端网络模式只镜像列表与进度，不本地 tick 动作。
- 新增 `ForageStart/ForageCancel/FishingStart/FishingReel/FishingCancel`。服务器校验规划身份、交互距离、工具/鱼竿/鱼饵、节点耗尽和重复预约；推进等待/咬钩，确定性 roll 产出，处理鱼饵消耗、工具/鱼竿磨损、背包满掉落与生存经验。双客户端同节点采集由预约+全局耗尽状态拒绝。
- 测试：`test_player_state` 覆盖 v13 往返与 v12/v11 回退；`test_network_integration` 覆盖双客户端真实地图觅食/重复拒绝/耗尽/距离拒绝与钓鱼扣饵/等待/咬钩/收杆；`test_content_hash` 校验 forage 纳入指纹，`test_embedded_server` 客户端按 forage 数据重算指纹。Debug **561/561（126886 assertions）**、Release **561/561（126899 assertions）**、Python **54/54**。
- 限制：服务器全局觅食耗尽状态尚未写入世界存档；钓鱼点仍未耗尽/刷新；远程玩家不显示采集动作；实机双客户端视觉验收未做。

## 2026-10-29 M23-F2a-T7 测试

- `WorldSimulation::LoadSave` 恢复存档 `gameMinutes` 到 `GameTime`，服务器重启后绝对刷新时间保持一致。服务器全局 `ecs::ForageState` 改由保留 StableId 的世界实体承载，`SaveWorld` 前同步、读档后恢复并清理过期节点；旧档没有该实体时创建空状态。
- 新增 `test_network_integration` T7：真实地图草地觅食检查工具磨损、节点耗尽，快进服务器时间后节点刷新；钓鱼覆盖等待中取消（扣饵不磨竿）、未咬钩收杆拒绝、错过咬钩自动中断并磨损鱼竿。
- 新增 `test_network_world_persistence` T7：真实采集后关闭服务器，全局耗尽节点、工具耐久与游戏时间随存档恢复；快进 200 分钟后同一节点重新可采。
- 节点确定性、节点耗尽/刷新、烹饪联动、双客户端和 v12/v11 旧状态继续由 T1/T2/T4/T6 与 `test_player_state` 覆盖。
- 验证：Debug **564/564（126961 assertions）**、Release **564/564（126974 assertions）**、Python **54/54**。
- 限制：钓鱼点自身仍未耗尽/刷新；远程玩家不显示采集动作；实机双客户端视觉验收未做。

## 2026-10-29 M23-F2a-T8 文档与平衡

- 按基础数据计算并记录产出率（游戏日 60 现实分钟、`needs.hungerPerDay=1.4`，不含行走/烹饪）：森林每次 2.6 秒搜索期望 2.85 件、1.00 件食物，生食 0.066 饥饿、熟制 0.172；草地 2.0 秒约 2.92 件、1.00 件食物，0.060/0.160；静水钓鱼每次尝试（5 秒等待 + 2 秒咬钩窗口）按 0.62 成功率约 0.93 条小鱼，生食 0.112、熟制 0.298，消耗 1 鱼饵、鱼竿磨损 0.03。
- 平衡结论：食物可持续但不提供工具/药品/弹药，钓鱼仍需鱼竿/鱼饵、觅食有工具与刷新限制；第一版不改数值，实机若过易优先降低浆果权重或鱼获 rolls，不改物品饥饿值。
- `docs/modding.md` 更新产出率前提与 T7 世界实体持久化说明；README 增加产出率摘要；`tools/tests/test_forage_data.py` 新增期望产出率回归并锁定上述值。
- 验证：无 C++ 逻辑变更；Debug **564/564（126961 assertions）**、Release **564/564（126974 assertions）**、Python **55/55**。

## 2026-10-29 M23-F3-T1 可燃数据与火源组件

- 新增 `assets/fire.json`（`fire-v1`）与 `game::FireRegistry`：材料按 sprite 唯一映射，`flammability` 定义 `ignitionThreshold/burnSeconds/smokePerSecond/heatRadius`；严格拒绝重复 id/sprite、越界值与未知字段，整份数据原子加载。`FindFireMaterial` 可按格层查询。
- 基础覆盖篝火 `object.campfire`、木墙横/竖、木地板 `tile.floor.wood`、树木 `object.tree` 与草丛 `decor.grass_tuft`；客户端与服务器启动加载 `fire.file`。
- 新增 `ecs::FireSource`（`FirePhase`、材料 id、点燃进度、剩余/总燃烧时长、烟量、热半径）与配置 `fire.file`；本阶段不创建活跃火源，状态机/蔓延/存档/联机分别留 T2/T3/T6。
- 测试：`test_fire` 覆盖数据契约、重复/非法字段、基础 sprite 覆盖、格层查询与组件字段；Python `test_fire_data` 覆盖 JSON 契约和默认材料差异。Debug **569/569（127040 assertions）**、Release **569/569（127053 assertions）**、Python **57/57**。
- 限制：无点燃/燃烧/熄灭、温度/伤害、蔓延、存档或联机；火焰数据暂未纳入内容指纹 v6。

## 2026-10-29 M23-F3-T2 单机火源状态机

- 新增 `game::FireSystem`：点燃进度按 `fire.ignitionSeconds × (1 + ignitionThreshold)`，燃烧扣燃料，耗尽后阴燃 `fire.smolderSeconds` 再熄灭；点燃失败/主动熄灭清空燃料，熄灭源可在同格重新点燃。
- 点燃率按 `1 - ignitionThreshold - wetness × wetnessPenalty` 计算，受 `minIgnitionChance` 约束；`WorldDemoState` 用当前天气湿度点火，雨天/潮湿降低成功率。配置新增 `fire.ignitionSeconds/wetnessPenalty/minIgnitionChance/smolderSeconds/seed`。
- 火源用 ECS `Position + FireAnchor + FireSource` 表示；Climate 只对燃烧/阴燃且位于 `heatRadius` 内的火源标记“火边”，点燃中/熄灭不升温。无 FireRegistry 的服务器路径仍回退旧篝火范围查询，T6 接权威火源。
- 单机 E 在门/床后切换最近篝火，点燃/燃烧/阴燃时按 E 熄灭；HUD 显示点燃、燃烧、阴燃与熄灭提示，联机不本地操作。
- 测试：4 个 T2 用例（51 assertions）覆盖点燃进度、燃料→阴燃→熄灭、潮湿点火率、热半径、熄灭失效与篝火源创建；另补 HUD 文案与篝火查询断言。全量 Debug **573/573（127099 assertions）**、Release **573/573（127112 assertions）**、Python **57/57**。
- 限制：无 T3 蔓延、T4 灭火物品/资源、T5 僵尸燃烧/伤害、T6 存档/联机；仅篝火可交互点燃，尚无燃料物品补给。


## 2026-10-29 M23-F3-T3 蔓延规则

- `FireSystem::Update` 对 `Burning` 源按 `FireSpreadInterval(burnSeconds)` 累计四邻接脉冲；间隔 `spreadIntervalSeconds × (0.5 + clamp(burnSeconds/60,0,3))`，木地板/木墙/树/草丛各不相同。只检查同层、已加载 chunk，不跨 chunk、不做垂直/斜向传播。
- 目标先按 `FindFireMaterial` 选材并进入 `Igniting`；整格默认选材改为 Furniture→Wall→Floor→Item，避免木地板遮住同格篝火/树。蔓延概率 `clamp((1 - ignitionThreshold - ambientWetness × wetnessPenalty) × spreadChanceScale,0,1)`，`WorldSimulation` 传入当前天气 `wetnessIntensity`。
- 每 tick 最多成功新建 `fire.maxSpreadsPerTick`（默认 2）个火源，活跃源上限 `fire.maxActiveSources`（默认 128）；达到上限提前停止扫描。候选按 `fire.seed` shuffle、同格去重，已熄灭源复用。新增 `ecs::FireSource.spreadAccumulator` 与 `FireSystem::ActiveSourceCount`。
- 配置新增 `fire.spreadIntervalSeconds/spreadChanceScale/maxActiveSources/maxSpreadsPerTick`。
- 测试：3 个 T3 用例（38 assertions）覆盖木地板/木墙间隔与概率差异、四邻接蔓延、每 tick 预算、活跃源上限、未加载 chunk 阻断；Python `test_fire_data` 增加蔓延配置边界断言。Debug **576/576（127139 assertions）**、Release **576/576（127152 assertions）**、Python **58/58**。
- 限制：火源不随 chunk 卸载清理、烧毁结构/掉落、雨天自动衰减、存档/联机权威蔓延均未接（T6/T7）；蔓延累计时间不存档；未做实机火焰视觉/性能验收。


## 2026-10-29 M23-F3-T4 灭火与火焰伤害

- `use.extinguish/extinguishWear` 接入 `items.json`；新增 `fire_extinguisher`（0.95/0.2）、`wet_rag`（0.55/0.5），净水/脏水 0.55/0.40 灭火并替换空瓶；新增 `craft_wet_rag`（破布 + 脏水），灭火器加入村庄/加油站储物柜。`UseEffect.Any()` 不包含灭火字段，避免普通使用误消耗。
- `FireSystem::FindNearestActiveFire` 与 `PlanFireExtinguish`/`ConsumeFireExtinguishItem`/`FireExtinguishSucceeds`：单机 E 优先灭火，选择最高成功率物品；失败也消耗物品，水替换空瓶、灭火器/湿布扣耐久；无物品徒手拍打按 `bareHandsChance` 判定，失败受 `bareHandsSelfDamage` 火焰伤害。
- 新增 `DamageType::Fire`/`DamageCause::Fire`；`ApplyFireDamage` 每 0.5 秒对燃烧源 1 格内所有 `Health` 走 `CombatSystem::ApplyDamage`，每秒 6 点，不写伤口；WorldSimulation 在 Fire 槽内累加结算，玩家/僵尸共用。
- HUD `ActionHintState` 新增火灾、灭火物品、成功率与徒手提示；配置新增 `fire.extinguishRange/bareHandsChance/bareHandsSelfDamage/fireDamagePerSecond/fireDamageRadius/fireDamageIntervalSeconds`。
- 测试：3 个 T4 用例（48 assertions）覆盖物品选择/消耗/耐久、最近火源与玩家/僵尸生命伤害；HUD 文案断言扩展；`test_items`/`test_recipes` 物品总数更新为 53，Python `test_fire_data` 新增灭火物品/配方/配置校验。Debug **579/579（127222 assertions）**、Release **579/579（127235 assertions）**、Python **59/59**。
- 限制：单机灭火/伤害；服务器权威、存档/联机在 T6；僵尸燃烧状态与 AI 在 T5；雨天衰减/结构烧毁在 T7；伤害暂不按护甲减伤；实机视觉验收未做。


## 2026-10-29 M23-F3-T5 僵尸燃烧与 AI

- 新增 `ecs::BurningZombie` 运行时状态：剩余燃烧、伤害累计、传播累计、恐慌剩余、恐慌速度倍率与追逐半径。`ApplyFireDamage` 只给燃烧源范围内带 `ZombieVariant` 的僵尸附加状态，玩家不获得。
- `ApplyBurningZombieDamage` 每帧推进燃烧/恐慌计时，按 `zombieBurnDamageIntervalSeconds` 用 `Fire` 伤害结算 `zombieBurnDamagePerSecond`；默认 3 HP/s、8 秒结束，离开火源仍会燃烧，但不会瞬间清空整群僵尸。
- `FireSystem::SpreadFromBurningZombies` 在燃烧僵尸脚下按间隔/概率调用 `IgniteAt`，继续受每 tick 新建上限和全局活跃源上限约束；燃烧僵尸会一路留下潜在火源。
- `AiSystem` 收集存活玩家，燃烧恐慌期内无视视野追逐 `panicChaseRadius` 内最近玩家；`PathFollowSystem`/`BoidsSystem` 应用 `panicSpeedScale` 加速；`RenderCollectSystem` 用橙色 tint 表示燃烧，受击闪红优先。
- 配置新增 `fire.zombieBurnSeconds/zombieBurnDamagePerSecond/zombieBurnDamageIntervalSeconds/zombieSpreadIntervalSeconds/zombieSpreadChance/zombiePanicSeconds/zombiePanicSpeedScale/zombiePanicChaseRadius`；Python 数据测试补边界断言。
- 测试新增 3 个 T5 用例（27 assertions）：燃烧附加/玩家排除/持续伤害/到期移除、脚下可燃格传播与上限、燃烧僵尸追逐玩家并加速。Debug **582/582（127249 assertions）**、Release **582/582（127262 assertions）**、Python **59/59**。
- 限制：燃烧状态、恐慌与传播仍为单机；T6 接存档/联机；T7 结构烧毁/掉落；T9 火焰视觉/音效；伤害不按护甲减伤。

## 2026-10-29 M23-F3-T6 火焰存档与联机

- 存档组件新增 `FireAnchor`/`FireSource`/`BurningZombie`：火源格、材料 id、阶段、点燃进度、剩余/总燃烧、烟量、热半径、蔓延累计和僵尸燃烧/恐慌计时均可往返；燃烧结束或手动熄灭的源保留为 `Extinguished`，作为已烧毁格标记；旧档无组件按无火/无燃烧处理。
- `ServerHost` 将 `FireRegistry` 注入 `WorldSimulation`，服务器权威推进点燃、四邻接蔓延、阴燃/熄灭、火焰伤害与燃烧僵尸传播；网络客户端保持不 tick 本地模拟。
- 新增 ZLC1 `IgniteFire`/`ExtinguishFire`：服务器校验附近篝火、目标格、层数、距离、背包容器与灭火物品/徒手判定，物品消耗/耐久/自伤/成功 roll 全在服务器结算；客户端网络模式只发送命令。
- `ZLR1` 新增 `FireSource`（含 FireAnchor）与 `BurningZombie`；复制生产器允许无 `SpriteRef` 的火源实体进入 AOI，客户端同步为只读 ECS 并绘制脉冲火焰菱形/熄灭格，HUD 显示复制火源的灭火提示。
- 测试：T6 存档往返 1 用例（FireSource 燃烧进度/熄灭格 + BurningZombie）、复制 1 用例（无 Sprite 火源 + 燃烧状态）、联机集成 1 用例（伪造目标被拒绝、服务器点火复制、服务器灭火、客户端只读相位）；全量 Debug **585/585（127332 assertions）**、Release **585/585（127345 assertions）**、Python **59/59**（Release 首次运行命中已知 KCP/网络时序 flake，定向复跑与全量重跑通过）。
- 限制：火烧结构删除/掉落、火源 chunk 卸载清理、雨天自动衰减、100 火源压力与实机视觉验收留 T7/T8/T9；火源复制按 AOI 快照全量比较，未做独立降采样。

## 2026-10-30 M23-F3-T7 火焰性能与安全

- `FireSystem::IgniteAt` 与蔓延/僵尸传播共用 `fire.maxActiveSources` 上限，满额返回新增 `FireIgnitionResult::AtCapacity`；单机/服务器都显示“活跃火源已达上限”。未加载 chunk 的火源在 `Update` 中冻结推进，避免离屏持续燃烧。
- `WorldSimulation` 的 chunk 卸载回调调用 `FireSystem::UnloadChunk`，删除该 chunk 内活跃火源并保留 `Extinguished` 烧毁格；卸载后火源上限和后续 tick 不再被远处火源占用。
- 新增 `fire.rainBurnDecayScale`（默认 1.0，0~10）。`AdvanceFire` 把当前 `wetnessIntensity` 乘入燃料消耗，雨天燃烧更快进入阴燃/熄灭，同时保留原有雨天降低点燃/蔓延概率。
- `FireMaterialDef` 新增可选 `charredDrops`（`{item,count}`，最多 8 项，重复/未知字段/越界拒绝）。自然燃尽后 `ResolveBurnedCell` 删除可燃结构：母地图地面保留、玩家木地板恢复 `underlay`、篝火设施保留；`WorldSimulation` 生成掉落并 `BuildChunkStatics + OnChunkLoaded` 重建碰撞/A*。主动灭火不触发删除/掉落。
- 默认掉落：木墙 2 木材，树木 2 木材 + 2 树枝，玩家木地板 1 木材，草丛/篝火无掉落；数据与掉落 item 由 Python 测试交叉校验。
- 测试新增 5 个 T7 用例（61 assertions）：直接点火上限、雨天加速衰减、chunk 卸载清理与未加载冻结、自然燃尽删除墙体/掉落、母地图地面/篝火/玩家地板 underlay 恢复。全量 Debug **590/590（127398 assertions）**、Release **590/590（127411 assertions）**、Python **59/59**。
- 限制：雨天只加速燃料消耗，不做独立灭火概率；只处理 `fire.json` 已知可燃 sprite，不做结构坍塌/多格建筑；100 火源压力测试与实机火焰/烟雾验收留 T8/T9。





## 2026-10-30 M23-F3-T8 火焰测试与验收

- 新增 `M23-F3-T8 100 火源压力：上限内点燃/推进/熄灭`：100 个已加载木地板格全部点燃，连续推进 100 帧后仍全部 `Burning`，再全部主动熄灭并检查活跃火源归零；Debug 实测约 50–70ms，测试阈值 2000ms。
- 新增 `M23-F3-T8 双客户端火焰：复制状态一致与权威灭火`：真实服务器启动两个客户端；服务器权威点燃篝火后两端收到同一稳定 ID/火源格/材料/阶段，第二客户端伪造远处目标被拒绝；第一客户端消耗灭火器后服务器与两端复制状态同步为 `Extinguished`。
- 点燃/熄灭/蔓延/伤害/存档继续由 T2–T7 用例覆盖：T2 状态机与潮湿、T3 四邻接/边界/上限、T4 灭火资源与火焰伤害、T5 僵尸燃烧/传播/恐慌、T6 存档组件与 ZLR1/权威命令、T7 自然燃尽结构与 chunk 安全。
- 新增 `M23-F3-T8 燃尽墙体经 ChunkDiff 重载后不复活`：含木墙母地图基线经 `ResolveBurnedCell` 燃尽删除后抓取 ChunkDiff，再应用回重载母地图，确认墙层为空。
- 3 个 T8 用例共 746 assertions；全量 Debug **593/593（128144 assertions）**、Release **593/593（128157 assertions）**、Python **59/59**（Release 首次运行再次命中已知 `M23-F1-T10` 网络时序 flake，定向复跑与全量重跑通过）。
- 限制：100 火源压力直接驱动 `FireSystem`，不做渲染/音效压力；双客户端走同机回环，真实网络抖动与实机视觉/烟雾验收留 T9。





## 2026-10-30 M23-F3-T9 文档与实机烟测（完成）

- 配置/数据文档：`docs/02` 记录 `fire.*` 全部运行参数及 `assets/fire.json` 的 `flammability/charredDrops`；`docs/modding.md` 说明 `fire.file` 当前直接加载、不参与 mod `content/` 合并；README 增加点火/灭火/蔓延/烟雾概要。

- 视觉：`WorldDemoState::DrawFireOverlay` 在原有橙色脉冲菱形/熄灭叉之外，按 `FireSmokePerSecond` 绘制最多 5 组灰色烟雾团（每组 2 圈），随时间上升、横向摆动并随 `smokePerSecond` 调整密度；同时把火焰阶段色和燃烧僵尸 tint 修正为 ABGR 橙色系，不引入粒子系统。

- 验收步骤已写入 `docs/07` G1/G2：使用临时 `build/t9_mods/t9_fire_probe` 传送至村庄篝火，按 `E` 点燃后截图；临时探针/存档不入库。

- 用户实机确认橙色火焰、灰色烟雾团与火灾/灭火提示可用（2026-10-30）；用户未提供截图，1920 未单独留证。
- 正式基线仍为 Debug **593/593（128144 assertions）**、Release **593/593（128157 assertions）**、Python **59/59**。

## 2026-10-30 M23-F2b-T1 动物组件与数据

**目标**：为狩猎循环先定动物数据契约、ECS 组件、尸体/掉落表与存档形态，不生成实体、不做 AI/交互/联机。

**实现**：
- 新增 `assets/animals.json`（`animals-v1`）与 `game::AnimalRegistry`：鹿/兔/野猪，严格校验物种 id/kind、生命/游荡/逃跑/警觉/听觉/逃跑时长、伤害/攻击距离/冷却、尸体 sprite/搜索耗时/工具磨损/战利品表；被动动物伤害字段必须为 0，危险动物必须为正，未知物品/战利品表整份拒绝且不替换旧表。
- 新增 `ecs::AnimalKind/Animal/Flee/AnimalCorpse`：`Animal` 只保存物种与 kind，数值运行时查注册表；`Flee` 保存逃跑方向/剩余/威胁点/警觉度；`AnimalCorpse` 保存物种与已剥取标记。`Animal/Flee/AnimalCorpse` 注册 `ANML/FLEE/ACRP` 存档段，内置组件 37→40。
- 新增 5 个动物产物（生鹿肉/生兔肉/生野猪肉/兽皮/骨头）与 3 张战利品表（`animal_deer/animal_rabbit/animal_boar`）；尸体搜索工具默认 `kitchen_knife`，供 T4/T5 使用。
- `GameApp` 与 `ServerHost` 在物品/战利品表之后加载 `animals.file`；`ComputeContentHash` 升 v7，纳入全部 AnimalDef 字段，客户端/服务器同路径加载并参与握手。
- 顺带修复 `LootTable::ParseEntry` 在调用方不传 error 指针时的空指针解引用（无效战利品数据现在返回 false，不崩溃）。

**验证**：
- `tests/test_animals.cpp` 覆盖内置数据契约、非法数据原子拒绝、Animal/Flee/AnimalCorpse 存档往返；`test_content_hash` 覆盖动物指纹；`tools/tests/test_animal_data.py` 校验 JSON/物品/战利品引用。
- T1 4 个 C++ 用例 72 assertions；全量 Debug **597/597（128278 assertions）**、Release **597/597（128291 assertions）**、Python **62/62**。Debug/Release 首轮均命中已知网络时序 flake（`test_network_integration.cpp:1858`），定向复跑与全量重跑通过。

**限制**：T1 只做数据/组件/存档/掉落表，无生成、游荡、感知、逃跑/反击、击杀、剥取、掉落生成或联机复制；T2 接生成与游荡，T3 接感知逃跑，T4 接尸体收获，T5 接烹饪/制作，T6 接联机。

## 2026-10-30 M23-F2b-T2 动物生成与游荡

**目标**：让鹿/兔/野猪按森林、草地和 POI 出现在单机世界，能游荡、远离玩家降频，并在 chunk 卸载/重载时保持状态；暂不接感知、战斗、剥取与联机。

**实现**：
- `AnimalDef` 新增 `spawn`（forest/meadow/poi 权重、maxPerChunk、poiIds），鹿偏森林、兔偏草地、野猪偏农场仓 POI；新增 `config/game.json` 的 `animals.spawn` 全局参数（enabled/maxLive/minPlayerDistance/poiRadius/seed），单机上限 36。
- 新增 `game::AnimalSystem`：chunk Loaded 后扫描室外草地/泥地可站立格，用 `object.tree` 判定森林、无树判定草地、POI marker 半径判定 POI；按 seed/区块/物种/类别/槽位确定性滚动，单区块 `maxPerChunk` 与全局 `maxLive` 双封顶，已生成区块不重复播种。
- 新增运行时 `ecs::AnimalWander`：动物选择 2~6 格目标、待机后重选；移动前检查脚下与四周地面/房间/阻挡，撞墙按 X/Y 轴回退，完全阻塞则放弃目标；`LodState` 非更新 tick 跳过，远距离按既有 30Hz/10Hz 降频。
- chunk 卸载时把动物物种/位置/生命/Flee 保存到 `AnimalSystem` 内存快照并销毁；重载原位恢复，已生成区块不重复生成。已加载动物随实体快照存档；未加载 chunk 的动物快照尚未落盘，留 T7。
- 新增 `animal.deer/animal.rabbit/animal.boar` 占位资源绑定（复用角色帧 + 物种 tint）；HUD 增加动物 live/seeded/pending/spawned/restored/far 调试行。服务器 `spawnAnimals=false`，T6 接权威生成/复制。

**验证**：
- `tests/test_animal_spawn.cpp` 4 个用例 45 assertions：森林/草地/POI 生成与重复加载去重、全局上限/确定性、chunk 卸载/重载恢复、LodState 远端降频；扩展 `test_animals` 的 spawn 契约与非法权重校验、`test_content_hash`、Python `test_spawn_contract`。
- 全量 Debug **601/601（128332 assertions）**、Release **601/601（128345 assertions）**、Python **63/63**；Release 首轮分别命中既有网络时序 flake（`test_network_inventory.cpp:672`、`test_network_integration.cpp:1858`），定向/全量复跑通过。
- Debug 窗口烟测 `--game.startState=world`：生成 36 只活跃动物，卸载 3 个 chunk 的动物快照待恢复；`AnimalSystem` 约 0.36ms/tick（Debug），无崩溃/错误日志。未做真实视觉截图，留 T8/用户验收。

**限制**：T2 不接感知、枪声/脚步、逃跑、野猪反击、击杀/尸体/剥取、动物间/玩家碰撞或联机复制；chunk 卸载快照只在当前进程内存，未落的 chunk 记录不跨游戏存档。占位动物复用角色帧，动画/专用美术留 T8。

## 2026-10-30 M23-F2b-T3 动物感知与逃跑

**实现**：
- `AnimalSystem` 新增玩家感知：`alertRadius` + 0.5 格阻挡采样视线，`fleeRadius` 作为近距离压迫半径；`WorldSimulation` 的 `OnSound` 同时推给僵尸感知与动物系统，动物用 `min(hearingRadius, SoundEvent.radius)` 和 `core::SoundPropagation` 隔墙衰减听枪声/脚步/撞击，忽略 UI 声音。
- 被动动物受惊或受击后进入 `Flee`，威胁反方向逃跑并在 `fleeSeconds` 后结束；野猪受击/看见/听见威胁后进入运行时 `AnimalWander.retaliating`，追击玩家并用 `CombatSystem::ApplyDamage` 按 `damage/attackCooldown` 反击，火焰伤害仍走逃跑。
- `CombatSystem` 的 AI 攻击候选跳过 `Animal`，僵尸 `Perception/Ai` 仍只认玩家；单机玩家近战自动瞄准和显式目标允许动物；读档时为动物补 `AnimalWander/Flee/LodState` 但不补 AI。
- HUD 动物行追加 `flee/retaliate/heard/attacks`；chunk 卸载保存 `Flee`，但 `retaliating/attackCooldown` 不保留。

**验证**：
- `tests/test_animal_spawn.cpp` 新增 4 个 T3 用例 44 assertions：视野发现与墙体遮挡、枪声逃跑方向及结束、受击逃跑/野猪反击、僵尸攻击隔离/玩家攻击命中。
- 全量 Debug **605/605（128376 assertions）**、Release **605/605（128389 assertions）**、Python **63/63**；Debug 窗口烟测 `--game.startState=world --audio.device=off` 运行 9 秒：36 只活跃动物、AnimalSystem 约 0.88ms/tick，无崩溃/错误日志。
- 未做真实视觉截图；野猪/动物攻击动画、攻击音效留 T8。

**限制**：T3 不生成尸体/掉落、不接击杀收获（T4）、不接联机复制（T6）；感知为全向，不含动物朝向视锥；声音事件没有来源 ID，仅按声源位置反应。

## 2026-10-30 M23-F2b-T4 动物击杀与收获

**目标**：把 T3 的动物生命链接到可剥取尸体、工具需求/磨损和肉/皮/骨头掉落，形成单机狩猎闭环；联机权威与并发剥取留 T6。

**实现**：
- `DeathSystem` 在动物倒地窗口结束后创建通用 `Corpse` 并附加 `AnimalCorpse{speciesId,false}`；动物不再派发 `OnZombieDead`，避免任务/统计误算僵尸击杀。`AnimalSystem` 每 tick 按 `AnimalDef.corpseSprite` 刷新尸体 `SpriteRef`，运行时和读档尸体都走同一路径。
- 新增 `game::AnimalHarvest`：检查 `corpseTool` 在背包或装备槽且 condition>0；按 `corpseSearchSeconds` 计时，完成后用尸体 StableId + `lootTableId` 派生 seed 确定性 roll 到尸体 `Container`，写 `LootContainer{searched=true}` 和 `AnimalCorpse.harvested=true`，按 `corpseToolWear` 磨损/损坏移除工具；重复剥取不重 roll。
- `WorldDemoState` 新增 E 剥取动作：范围选择最近的未剥取尸体，移动/受击/打开背包中断；HUD 显示“按 E 剥取（N 秒）”、缺工具/进行中进度；完成后打开背包看到“尸体：物种”和战利品堆叠，可拿入玩家背包。
- 尸体作为普通容器复用现有搜刮 UI；不自动把掉落塞入背包。`Container`/`LootContainer`/`AnimalCorpse` 均已有存档段，无新存档组件/内容指纹变化。

**验证**：
- 新增 `tests/test_animal_harvest.cpp` 3 个用例 46 assertions：DeathSystem 动物尸体/物种/来源 ID 且不派发僵尸击杀事件；确定性掉落、工具磨损、重复剥取拒绝；缺工具/工具耗尽移除与提示。
- 全量 Debug **608/608（128422 assertions）**、Release **608/608（128435 assertions）**、Python **63/63**；Release 首轮命中既有网络时序 flake（`test_network_inventory.cpp:672`），定向/全量复跑通过。
- 未做真实视觉截图；尸体 Sprite 当前仍是占位资源，专用动画/音效留 T8/用户验收。

**限制**：T4 仅单机；不接服务器权威生成/受伤/死亡/掉落/并发防剥取（T6）；工具只有可用/磨损，无独立品质等级；尸体肉/皮/骨头尚未进入烹饪/制作（T5）；打开背包后需手动拿取尸体容器物品。

## 2026-10-30 M23-F2b-T5 动物肉烹饪与皮革/骨头制作

**目标**：让 T4 获得的生肉、兽皮和骨头进入现有烹饪/制作面板，闭合“狩猎 → 剥取 → 熟食/装备”的单机循环；食物中毒/营养深化留 F5。

**实现**：
- `assets/items.json` 新增 `cooked_venison/cooked_rabbit_meat/cooked_boar_meat`，熟肉饥饿恢复为 0.42/0.30/0.55（生肉 0.12/0.08/0.18），重量按失水下调，保留 3 堆叠与 `spoilDays`；新增 `bone_spear`（46 伤害、2.15 距离、48° 扇形），复用 `item.makeshift_spear` 图标。
- `assets/recipes.json` 新增 `cook_venison/cook_rabbit_meat/cook_boar_meat`（篝火）与 `craft_leather_jacket`（3 兽皮 + 2 破布，厨刀 wear 0.05，工作台）和 `craft_bone_spear`（2 骨头 + 2 木材 + 1 破布，工作台）。所有配方继续走 `recipes-v3` 校验、制作站范围、队列、批次与工具磨损。
- 内置内容由 58 物品/13 配方变为 62/18；无新增 C++ 规则、存档组件或内容指纹版本，熟肉腐败继续复用 `Spoilage`。

**验证**：
- `tests/test_food.cpp` 新增 T5 用例 93 assertions：三种生熟肉恢复/重量/堆叠/腐败与篝火配方；皮革夹克扣料/厨刀磨损；骨制长矛材料与产出。`tests/test_items.cpp`/`tests/test_recipes.cpp` 更新内置物品数 62；Python `test_food_data.py` 新增生熟肉链与皮革/骨头配方检查。
- 全量 Debug **609/609（128595 assertions）**、Release **609/609（128608 assertions）**、Python **65/65**；Debug 首轮命中既有网络时序 flake（`test_network_integration.cpp:1858`），定向复跑后全量通过。

**限制**：未做食物中毒/营养/批量烹饪（F5）；熟肉只校验附近有篝火对象，不要求火焰正在燃烧；皮革夹克/骨制长矛无专用制作动画/音效；联机沿用服务器权威制作链，动物专属 T5 联机逻辑留 T6。

## 2026-10-30 M23-F2b-T6 动物联机

**目标**：把 T3/T4 的单机动物生成、受伤、死亡、掉落和剥取搬到服务器权威，客户端只读复制，并阻止双客户端重复剥取同一尸体。

**实现**：
- `ServerHost::Options` 新增 `spawnAnimals`；正式 `ServerHost::Run` 与 `EmbeddedServer` 默认开启，网络单测默认关闭，T6 双客户端测试显式开启。服务器 `WorldSimulation` 创建 `AnimalSystem`，按焦点生成/游荡/感知；客户端联机模式不 tick 动物 AI。
- `EntityReplication` 注册 26 个组件：新增 `Animal`（物种/危险类型）与 `AnimalCorpse`（物种/已剥取）。活体走 Character 原型，尸体走 Prop 原型；客户端 `SyncNetworkReplicas` 写入/移除对应组件，位置/生命/精灵/尸体战利品容器沿用现有复制。
- 服务器 `Attack` 的显式 StableId 和自动瞄准接受 `Animal`，伤害仍走 `CombatSystem::ApplyDamage`；`DeathSystem` 生成 `AnimalCorpse` 并刷新尸体外观，不派发僵尸击杀事件；投射物保持原生命/位置命中逻辑。
- 新增 `AnimalHarvestStart=37`（payload.entityId=尸体 StableId）与 `AnimalHarvestCancel=38`。服务器校验会话空闲、尸体未剥取、层级/距离（`inventory.lootRange`）、工具、会话尸体预约；按 `corpseSearchSeconds` 推进，移动/受击/攻击/死亡/离开范围/目标失效取消；完成调用 `HarvestAnimalCorpse` 磨损工具、写 `harvested` 和确定性尸体 `Container`。预约期间第二玩家收到“其他玩家”，已剥取后再次请求被拒绝。
- `PlayerState` 升 v14，新增 owner-only `PlayerAnimalHarvestState{active,corpseStableId,speciesId,name,x,y,z,progress,duration}`；`FillPlayerState` 推进进度，客户端网络模式映射到现有剥取 HUD，E/攻击/打开背包只发开始或取消命令，不本地结算。v13 及更旧负载跳过新尾部并保持空闲。

**验证**：
- `tests/test_network_integration` 新增 T6 双客户端场景：服务器生成且客户端看到 `Animal`、客户端 Attack 造成服务器伤害、死亡复制 `AnimalCorpse`、超距拒绝、第一玩家剥取、第二玩家预约拒绝、完成掉落/工具磨损、客户端 `LootContainer` 复制、重复剥取拒绝。
- `tests/test_player_state` 新增 v14 动物剥取状态往返与非法尸体 ID 拒绝；`tests/test_replication` 组件注册数 24→26。旧网络背包/远程武器测试改用位置拷贝，避免动物实体增长导致组件池重排后悬垂 `Position&`；该问题由 T6 的服务器动物生成暴露。
- T6 定向 2 用例 50 assertions；全量 Debug **611/611（128648 assertions）**、Release **611/611（128662 assertions）**、Python **65/65**。

**限制**：T6 不复制 Flee/AnimalWander 细节，客户端只显示位置/生命/精灵/尸体状态；服务器生成焦点为首个在线玩家，双人远距离时生成覆盖偏向前者；未加载 chunk 的动物快照/服务器重启持久化、动物数量上限压力和长时间双客户端狩猎留 T7；尸体掉落仍需打开容器手动拿取；正式服务器默认生成动物，轻量 `ServerHost` 测试需显式 `spawnAnimals=true`。

## 2026-10-31 M23-F2b-T7 动物测试与平衡

**目标**：补齐 T2 留下的“未加载 chunk 动物快照不落盘”缺口，并用自动化覆盖 36 只上限、墙体和更新预算，锁定狩猎肉量平衡；不新增动物行为或美术。

**实现**：
- 新增 `ecs::AnimalWorldState`（`AWST` v1），保存未加载 chunk 的动物物种/位置/生命/Flee 和已播种 chunk 标记；内置存档组件 40→41，旧档缺失时按空状态。状态写入稳定实体 `0x4D323342414E4D53`，单机快速存档与服务器 `SaveWorld` 调用 `AnimalSystem::StoreState`，`WorldSimulation::LoadSave` 与单机读档调用 `LoadState`；状态实体无 Position，不进入 `ZLR1` 复制。
- 快照上限 4096 chunk、单 chunk 16 条、全局 2048 条，超出部分下次加载按种子重新播种；这样既保留跨存档狩猎状态，也避免长时间巡航无限膨胀存档。`LoadState` 恢复后把 chunk 标记为已播种，重载不会重复生成。
- 测试补充：真实 EntityStore 编码/解码后恢复生命 7.5、Flee 4.25s，并验证重复 `OnChunkLoaded` 不增加动物；20 chunk/36 只上限与墙体阻挡；240 tick 性能；T6 双客户端服务器 `live<=36`；Python 锁定鹿/兔/野猪期望肉量与生熟肉收益。

**验证**：
- T7 2 用例 214 assertions；36 只 Debug 约 0.62ms/tick、Release 约 0.017ms/tick。
- 全量 Debug **613/613（128863 assertions）**、Release **613/613（128877 assertions）**、Python **66/66**。
- 首轮 Debug/Release 分别命中既有网络时序 flake（F2a 觅食与 F1 远程武器），定向/重跑后全量通过。

**限制**：`AnimalWorldState` 是截断快照，超出上限的远处未加载 chunk 会按种子重新播种；未做真实服务器重启后的长时间狩猎/双客户端视觉验收，留 T8；动物视觉与专用动画仍为占位。

## 2026-10-31 M23-F2b-T8 动物文档与烟测

**目标**：补齐动物/狩猎的玩家文档、Mod 契约、平衡口径和实机验收矩阵；修复首轮烟测暴露的占位辨识问题。

**文档**：
- `docs/modding.md` 更新动物数据契约：说明 T7 `AnimalWorldState`（AWST v1）已把未加载 chunk 快照与已播种区块写入单机/服务器存档，上限 4096 chunk、单 chunk 16、全局 2048；补充 `SPACE` 近战、`V` 远程、`E` 剥取/厨刀要求、`I` 尸体容器、枪声/树木遮挡与“无独立潜行键”。
- README 新增动物与狩猎段落，记录联机尸体预约、T7 持久化、占位动画、36 只上限/每区块每物种 1 只/18 格最小生成距离；`docs/07` 新增 P1-H H1–H4 矩阵（1280/1920/读档重启/双客户端）。
- 平衡按 `assets/loot.json` 的 rolls/权重/min 计算期望下限：鹿约 2.82 肉、兔 1.23、野猪 3.76；熟制饥饿约 1.19/0.37/2.07（日需求 1.4，约 0.85/0.26/1.48 日）。数值未改，Python 回归继续锁定。
- 更新 `docs/05`、`docs/08`、`docs/01` 状态：T8 文档完成，P1-H 截图/视频和用户视觉确认未完成，不提前勾选。

**视觉修补**：新增 `animal_deer/rabbit/boar_*` 独立占位轮廓（鹿角/兔耳/野猪吻部）与 `AnimalVisualScale` 物种缩放；`animal.deer/rabbit/boar` manifest 不再复用 `character_zombie_*` 帧，tint 保留资源接口但默认白。新增 T8 视觉缩放用例 1 个/6 assertions。

**验证**：Debug **614/614（128869 assertions）**、Release **614/614（128883 assertions）**、Python **66/66**；首轮网络时序 flake 复跑通过。未伪造截图/视频；自动观察截图见 `build/p1-h1-hunt-deer-1280.png`；动物视觉可辨性经自动观察通过；H2 野猪反击/剥取/1920 容器、H3 F12 读档和 H4 双客户端/服务器重启已补自动构造证据；自然流程/低伤害平衡仍建议按 P1-H 复验。

**自动观察（2026-10-31）**：Release 1280，临时 `build/t8_probe_mods/t8_probe` 在 OnTick 传送玩家到森林 `(62.5,40.5)`，临时 `minPlayerDistance=4`；日志 `live=36/已生成=52/种子区块=65`。首轮截图 `build/t8-hunt-close-08s/12s/16s.png` 显示复用僵尸帧的占位人形，H1 不通过；补独立轮廓后 `build/t8-hunt-natural-16s.png` 中鹿、兔、野猪已可辨（H1 视觉自动观察通过）。接近逃跑/击杀/尸体与 H2–H4 仍需人工；临时探针/存档未入库，默认生成距离仍为 18。

**H2/H3 自动构造（2026-10-31）**：临时 `build/t8_boar_probe_mods` 将玩家传送到农场仓 `(78.5,62.5)`，用 `--combat.playerDamage=999 --combat.playerRange=3 --combat.playerCooldownSeconds=0.1 --health.playerMaxHp=1000` 清掉 POI 守军并击杀野猪；日志 `build/t8-boar-h2-1920.out.log` 记录 HP 从 1000 降到 742、boar 剥取 4 堆/10 件，1920 截图 `build/p1-h2-boar-counter-1920.png`、`p1-h2-boar-harvest-1920.png`、`p1-h2-boar-container-1920.png` 中 HUD/尸体容器不越界。随后 F12 存档，退出进程并从主菜单继续 `20261031-042716`；日志 `build/t8-h3-load.out.log` 读取 68 实体/1 玩家，重载后动物 `live=33/已生成=0`，截图 `build/p1-h3-before-restart-1280.png`、`p1-h3-hunt-reload-1280.png`。这些都是构造状态，不替代 H4 真实双客户端、服务器退出重启和自然流程复验；已剥取容器“不重新掉落”仍需用户打开尸体确认。

**H4 双客户端与服务端重启（2026-10-31）**：用 `build/t8_h4_animals.json` 临时移除野猪剥取工具要求；服务器加载 H2 新档副本 `build/t8_h4_run`，启用 999 伤害/1000 HP，`H4A`/`H4B` 两个真实客户端连接同一 UDP 服务器。A 击杀并剥取 boar（4 堆/11 件），截图 `build/p1-h4-harvest-1280.png`；服务器正常退出 `SaveWorld` 写回 88 实体。同一 `server.saveId` 重启后读取 88 实体/0 玩家，两个客户端恢复并仍看到尸体，截图 `build/p1-h4-reload-a-1280.png`；B 再次按 E 未获得第二份战利品（客户端按 `harvested` 抑制，服务器拒绝重复交互）。证据在 `build/t8_h4_full_out/`；这是构造状态，不替代自然低伤害平衡/真实玩家手感复验。
## 2026-10-31 F5-T1 作物数据

**实现**：新增 `assets/crops.json`（`crops-v1`）与白菜/土豆/番茄/玉米 4 种作物；`CropRegistry` 严格校验物品引用、阶段阈值、种子唯一、id/数量上限，并支持 `content/crops.json` 原子追加。客户端 `GameApp` 与 `ServerHost` 在物品/mod/战利品之后加载基础/ mod 作物；单机/服务器农田种植从背包选择首个可用作物种子，浇水/生长/收获使用 CropDef 的 `waterMinutes/maxWaterMinutes/waterUsePerMinute/witherMinutes/harvestYield/seedYield`，旧 `farming.*` 仅作作物数据缺失回退。内容指纹升 v9 并纳入全部 CropDef 字段（含阶段）；新增土豆/番茄/玉米种子与产物、农场掉落，`mods/example_content` 追加南瓜示例。

**验证**：新增 `tests/test_crops.cpp` 6 用例与 `tools/tests/test_crop_data.py` 3 用例；`test_content_hash`/`test_embedded_server`/`test_mod_content` 覆盖作物哈希与 mod 追加。Debug **620/620（129014 assertions）**、Release **620/620（129028 assertions）**、Python **69/69**；未提交。

**限制**：T1 只做数据与基础农田接入；同一农田多个种子时按注册顺序自动选择，未做选择 UI；肥料/灌溉/病害实际结算/季节/HUD 阶段留 F5-T2，烹饪分类/批量/中毒与完整存档联机留 T3/T4。

## 2026-10-31 M23-F5-T2 农田扩展

**实现**：新增 `fertilizer`/`fungicide` 与农场掉落；空田 `Q` 循环可用种子，`T` 施肥/杀菌，E 继续播种/浇水/收获/清理。`FarmSeasonFromMonth` 把月份映射为春夏秋冬，`AdvanceFarmPlot` 接收季节/降雨上下文：雨天按 `rainWaterPerMinute` 自动补水，夏季病害阈值更低、冬季生长倍率更慢；缺水累计到 `witherMinutes × diseaseDryFraction` 后进入病害并降低产量，堆肥一次性增产，杀菌剂清除病害。`FarmPlot.diseased/fertilized` 写入 `FARM` v2（旧 v1 默认 false）与 `ZLR1` v3；服务器新增 `kFarmActionPlant`（`payload.index` 作物索引）和 `kFarmActionCare`，校验种子/阶段/物品/索引。HUD 状态行显示阶段、缺水/水分钟、施肥、病害与季节，基地摘要显示生长/成熟/缺水/病害计数，Lua/Python 同步 `baseFarm*`。

**验证**：`test_homestead` 增加季节/降雨/肥料/病害/治愈 2 用例；`test_base_hud` 增加农田摘要；`test_network_inventory` 覆盖服务器选种/施肥/杀菌与复制；`test_entity_save`/`test_replication` 覆盖新标记；Python 增加农场配置/物品契约。Debug **623/623（129080 assertions）**、Release **623/623（129094 assertions）**、Python **70/70**；未提交。

**限制**：病害为低水确定性触发（`diseaseChance` 调阈值，不做真实随机），无温室/喷灌设施；作物/病害状态已存档并复制，烹饪分类/批量/中毒与完整烹饪状态由 T3/T4 补齐。

## 2026-10-31 M23-F5-T3 烹饪扩展

**实现**：`assets/recipes.json` 升 `recipes-v4`，为全部配方补 `category`（cooking/preservation/medical/crafting/general），并新增可选 `cookware`（不消耗、需可用锅具）与 `fuel`（每次制作消耗）。新增烹饪锅、丰盛炖菜、熏野猪肉：炖菜需锅+木材，熏肉消耗 2 木材并延长保鲜。`CanCraft/Craft/MaxCraftCount` 校验并扣除锅具/燃料，工具效率纳入锅具耐久；背包制作面板增加分类循环筛选，保留批量数量、分页与 FIFO 队列；`InventoryRecipe` 与 Lua/Python 状态暴露 `category/categoryLabel/cookware/fuel`。

食物 `use` 新增 `nutrition`（0~1）与 `foodPoisonChance`（0~1）；新增运行时 `FoodState` 与 `game::FoodSafety`，营养降低中毒概率/饥饿增速/饥饿伤害，腐败食物保底高中毒概率，中毒按固定 tick 扣血并写入 `LastDamage` 死因。单机 Needs HUD 显示营养/中毒剩余秒数；服务器食用由 `PlayerInventoryService` 权威写入 `FoodState`。`HashUseEffect`/`HashRecipeDef` 纳入新字段并将 `kContentHashVersion` 升到 v10；旧 recipes/物品数据继续兼容。

**验证**：`test_recipes` 增加 v4 分类/锅具/燃料/批量用例；`test_food` 增加营养/中毒纯函数与炖菜锅具燃料链；`test_inventory_service` 覆盖服务器食用写入 FoodState；`test_python_api` 覆盖新背包字段；Python `test_food_data` 锁定 73 物品/recipes-v4/新配方。Debug **627/627（129203 assertions）**、Release **627/627（129218 assertions）**、Python **71/71**；未提交。

**限制**：营养只提供饥饿/伤害缓冲与中毒减免，没有独立营养条；食物状态由 F5-T4 接入存档/联机。

## 2026-10-31 M23-F5-T4 保存与联机

**实现**：`FoodState` 注册 `FOOD` v1 存档组件（营养/中毒剩余/每秒伤害/总时长），内置存档组件 41→42；单机快速存档、服务器世界存档和玩家档案实体快照均可保存/读档，旧档无组件按 0。`PlayerState` 升 v15，尾部新增 owner-only `PlayerFoodState{valid,nutrition,poisonRemaining,poisonDamagePerSecond}`；`FillPlayerState` 从服务器 ECS 读取，客户端 `ApplyNetworkPlayerState` 只读镜像到 `m_networkFood` 并驱动 Needs HUD，不在本地推进中毒。服务器 `FoodStateSystem`/`TickHomestead` 继续权威推进中毒与农田生长；服务器复活时清空 `FoodState`，避免带毒重生。

**验证**：`test_entity_save` 覆盖 `FOOD` 往返与旧档缺省；`test_player_state` 覆盖 v15 往返、v14 回退与非法营养/中毒；`test_player_profile` 覆盖档案食物状态；`test_network_inventory` 覆盖服务器食物状态复制与服务器权威农田成熟/复制。Debug **627/627（129224 assertions）**、Release **627/627（129238 assertions）**、Python **71/71**；未提交。

**限制**：营养只影响饥饿/伤害缓冲/中毒概率，无独立心情或营养 HUD；中毒仅走 owner-only PlayerState，不广播为 ZLR1 实体组件；真实双客户端长时间挂机/重启视觉验收未单独执行。

## 2026-10-31 M23-F5-T5 测试与平衡

**实现**：新增真实作物整周期测试，用 `assets/items.json`、`assets/crops.json` 与 `config/game.json` 的 `farming` 参数跑完白菜/土豆/番茄/玉米夏季与冬季周期：按作物水量反复浇水可按时成熟，夏季耗时接近 `growMinutes`，冬季约 4 倍且需更多次浇水，无意外病害，收获量等于 `harvestYield`。病害测试让番茄缺水越过阈值，验证停止生长、产量 3→2、病中拒绝施肥；杀菌后清零缺水、允许补肥并恢复至成熟收获 3。新增批量烹饪测试：3 份炖菜耗尽 3 木材、每份消耗 1 木材、锅具保留，燃料耗尽后 `CanCraft/MaxCraftCount` 归零。Python 新增 `test_f5_balance.py`，锁定作物速度/产量/病害/耗水取舍、炖菜/熏肉价值、腐败时间与中毒参数上界。

**平衡**：`hearty_stew` 从饥饿 0.7/营养 0.9/治疗 2/保鲜 1.5 天调整为饥饿 **0.85**/营养 **1.0**/治疗 **3**/保鲜 **2.5 天**，高于新鲜土豆+白菜的合计饥饿（0.75）、不低于营养合计（1.0），但仍消耗水、木材和锅具；熏肉继续优于烤野猪且无中毒概率。作物与安全参数未改。

**验证**：T5 新增 4 个 C++ 用例/126 assertions、3 个 Python 用例；全量 Debug **631/631（129350 assertions）**、Release **631/631（129364 assertions）**、Python **74/74**；存档/联机沿用 T4 用例。未提交。

**限制**：未做真实玩家长时间游玩与自然流程双客户端挂机/重启视觉验收；病害仍是确定性缺水阈值；批量测试使用合成背包，未重复覆盖 UI 队列交互。

## 2026-10-31 M23-F5-T6 文档

**实现**：`docs/modding.md` 的 mod 内容目录加入 `content/crops.json`，3.6.3.2 扩为作物字段表与生长/浇水/病害阈值公式、季节/施肥/收获规则；新增 3.6.3.3 说明 `recipes-v4` 的分类/锅具/燃料、批量燃料与锅具保留、食物营养/中毒/腐败、`FoodState` 与 `PlayerState v15` 存档联机边界。示例 mod 的 `mod_pumpkin` 现在通过 `pumpkin_soup`/`cook_pumpkin_soup` 接到锅具+木材烹饪，`mod_camp_cache` 保留南瓜种子，形成搜刮种子→种植→收获→烹饪闭环。

**验证**：`test_mod_content.cpp` 增加示例浓汤、营养、锅具/燃料断言；新增 `tools/tests/test_f5_mod_example.py` 校验示例 mod 物品/作物/配方/战利品引用和烹饪收益。全量 Debug **631/631（129359 assertions）**、Release **631/631（129373 assertions）**、Python **75/75**；未提交。

**限制**：示例只覆盖数据链，不替代真实模组制作与实机；作物/配方修改需重启，运行时 `Content` API 不注册作物。

## 2026-11-01 M23-F6-T1 地图与 POI 数据

**地图**：`tools/gen_map.py` 升至 v4，`village_demo` 新增圣玛丽医院 `(40,80,12,9)` 与军事检查站 `(100,18,10,7)`。医院南门进入双 ward 走廊，`x=44/47` 的隔墙在 `y=84` 留门洞，布置 6 张病床与南侧泥地通道；军事检查站为南门院落，四角 `object.barricade`、工作台与补给箱构成入口缓冲，`object.barricade` 纳入生成器 solid 集合。两处均复用现有图集，不新增 tile 集或 PZ 资源。

**配置与搜刮**：`config/game.json` 的 `pois.defs` 从 3 个扩到 5 个：医院威胁 2/6 名 mixed 守军、药房与物资箱、奖励抗生素×2；军事检查站威胁 3/8 名 strong 守军、军械柜与补给箱、奖励 9mm×18。`assets/loot.json` 新增 `poi_hospital_pharmacy/poi_hospital_supplies/poi_military_armory/poi_military_supplies` 四张独立表，军械表保底 12 发 9mm/6 发霰弹但受容量/权重约束，不提供无限弹药。

**地图统计与测试**：生成器输出 132 个非默认 chunk、555 个对象、26 个房间出现；`test_maploader` 计数、`test_poi` 的 5 POI 配置断言、Python `test_poi_data` 的医院隔墙/病床、军械路障、容器/标记 BFS 连通断言同步。全量 Debug **631/631（129451 assertions）**、Release **631/631（129465 assertions）**、Python **76/76**；全量首跑偶发已知 `M23-F1-T10` 网络时序 flake，隔离/重跑通过，非 F6 回归；未提交。

**限制**：医院感染风险物品/军械威胁细调留给 F6-T2；任务链/随机事件留给 F6-T4；未做真实实机截图。

## 2026-11-01 M23-F6-T2 搜刮与威胁

**感染物资**：`UseEffect`/`items-v1` 新增 `use.infectionRisk` 0~1，进入 `ContentHash` 与注册表校验；新增 `tainted_bandage`（污染绷带，`bandage=0.35`、`infectionRisk=0.35`），医院药房与物资箱产出。使用时有伤口才会按 `DeriveFoodPoisonSeed` 子种子调用 `characters::TryInfectWithRisk`，单机 UI/E 使用与服务器 `PlayerInventoryService` 共用；无伤口不触发，概率受 `infection.maxChance` 上限约束，满风险仍保证感染（测试用）。

**军械与威胁**：`poi_military_armory` 容量 24、rolls 4，保底手枪 1 + 9mm 16 + 霰弹 8，散落弹药受 item maxStack/容量约束，不提供无限弹药；医院守军 7 名 mixed（威胁 2），军事检查站 9 名 strong（威胁 3）。内置物品数 73→74。

**验证**：`test_items` 覆盖 `infectionRisk` 解析/越界；`test_infection` 覆盖污染风险确定性/满风险；`test_inventory_service` 覆盖服务器污染绷带使用并进入 Incubating；`test_loot` 覆盖医院污染物资、军械保底枪械与弹药上界；Python `test_infection_data/test_poi_data` 同步。全量 Debug **634/634（129498 assertions）**、Release **634/634（129512 assertions）**、Python **77/77**；未提交。

**限制**：污染绷带只对已有伤口生效，未做真实实机截图；任务链/随机事件留给 F6-T4。

## 2026-11-01 M23-F6-T3 独特敌人

**军械守卫**：`ZombieVariantKind` 追加 `Armored`（2.0 HP/0.62 速度/1.22 视觉，tint `0xFF9FB8C8`），复用现有 AI 并新增独立 `combat.zombieAttacks.armored` 咬击（14.5 伤害/3.0 击退/0.75s 前摇/1.35s 冷却）；`CombatSystem` 对爪/咬/钝减伤 35%，火焰/调试不减免，击退乘 0.35。减伤/抗击退由 kind 派生，不新增组件、存档或复制字段，旧档 kind 0~2 照常读取。

**生成**：`PoiEnemyDef.armoredCount` 指定前 N 名守军替换为重甲变体；军事检查站配置为 9 名 strong + `armoredCount=2`（2 精英 + 7 strong），单机与服务器共用。`armoredCount` 进入内容指纹；地图/搜刮不变。

**验证**：`test_combat` 覆盖减伤/抗击退/火焰与调试例外；`test_zombie_variants` 覆盖属性/生成/存档往返；`test_poi` 覆盖精英分配与内置配置；`test_content_hash` 覆盖精英数量进入指纹；Python `test_poi_data` 校验边界。全量 Debug **635/635（129529 assertions）**、Release **635/635（129543 assertions）**、Python **77/77**；未提交。

**限制**：不新增独立精灵/动画，仅缩放与 tint；未做真实实机截图；专属掉落与任务留 F6-T4/T5。

## 2026-11-01 M23-F6-T4 任务/事件

**任务链**：新增医院链 `hospital_supply`（医院 POI 完成 → 110 生存经验+止痛药×2）→ `hospital_quarantine`（搜索医院物资箱 + 返回村庄 → 80 生存经验+简易绷带图纸）；军事链 `military_armory`（检查站 POI 完成 → 150 近战经验+霰弹×8）→ `military_extract`（搜索检查站补给箱 + 返回村庄 → 90 近战经验+加固夹克图纸）。链首读取 `PoiTasks.completed`，链尾检测特定搜刮表并复用 `reach` 到 `(70,24)`。

**随机事件**：`event_hospital_outbreak`（第 2 天起、55%、击杀 5）奖励抗生素×1；`event_military_patrol`（第 4 天起、40%、击杀 6）奖励 9mm×12。任务总数 9→15，一次性链 3→5，随机事件 2→4；无新任务系统、存档字段或协议，复用 `UpdateMissions`/前置/奖励/每日随机刷新。

**验证**：`test_missions` 覆盖 15 条/5 链/4 随机与新链前置/目标；Python `test_mission_data` 同步数量、奖励引用与到达点可达性；`test_mod_content` 的 mod 任务基线更新为 15→16。全量 Debug **635/635（129563 assertions）**、Release **635/635（129577 assertions）**、Python **77/77**；未提交。

**限制**：随机事件仍为 `kill any`，无专属事件敌人；未做真实实机任务流程截图；完成/失败/旧档验收留 F6-T5。

## 2026-11-01 M23-F6-T5 测试与平衡

**失败/重试**：新增 `game::MissionCanActivate` 统一单机/服务器 once 接取；进行中/已完成不重复接取，前置必须完成，失败任务记录放弃日并只在次日重试，避免放弃后立即重接或任务链永久卡死。单机 K 放弃与服务器 `AbandonMission` 命令提示一次性任务“次日可重新接取”。

**完成/标记/旧档**：`test_missions` 覆盖失败当天/次日、前置解锁、每日随机不适用；`test_poi` 校验医院/检查站完成条件、`ok/warn` 提示、5 个标记坐标/间距；Python `test_poi_data` 继续校验标记可达与不阻挡；`test_entity_save` 覆盖旧日志空进度/短进度/已移除任务 id，缺失进度按 0 处理。

**平衡**：Python `test_mission_data` 锁定医院链 190 XP、军事链 240 XP（≤260）、单步 once 40–160 XP、随机事件 0.35–0.65 概率且 ≤6 击杀、每日巡逻 30 XP。

**验证**：Debug **638/638（129656 assertions）**、Release **638/638（129670 assertions）**、Python **78/78**；Debug/Release 应用构建通过，Debug 无窗口服务器 180 tick 通过。未提交。

**限制**：未做真实实机任务流程截图；F6-T6 仍需地图/POI/任务文档与截图。

## 2026-11-01 M23-F6-T6 文档与烟测

**地图文档**：`docs/mapformat.md` 补圣玛丽医院 `x=40..51,y=80..88`、南门 `(46,88)`、两道室内隔墙在 `y=84` 留门洞、6 张病床、标记 `(46.5,84.5)`；军事检查站 `x=100..109,y=18..24`、南门 `(105,24)`、四角路障/工作台/军械柜、标记 `(105.5,21.5)`。

**内容文档**：`docs/modding.md` 的 POI/任务段确认 5 个内置区域、armored 守军、15 条任务/5 链/4 随机与 F6 两段链；`README.md` 更新玩家说明为 5 条链、15 条任务/事件和 once 次日重试；`docs/07` 新增 P1-J 医院/检查站验收项。

**截图**：Release 1280×720，临时探针 `Player.Teleport` 到医院 `(46.5,85.5)`、检查站 `(105.5,21.5)`，用窗口消息按 `L` 切到“医院补给”/“军械回收”。`build/p1-f6-hospital-1280.png` 显示医院布局、POI 行和任务 0/1/奖励；`build/p1-f6-military-1280.png` 显示检查站院墙/路障/军械柜、POI 行和任务 0/1/奖励。日志 `build/f6-t6-hospital-os.out.log`、`build/f6-t6-military-os.out.log`。

**验证**：Debug/Release 测试 **638/638**、Python **78/78**；Debug 无窗口服务器 180 tick 通过；应用 Debug/Release 构建通过。截图使用隔离存档、关闭游荡者生成、1000 HP，属构造视觉状态。

**限制**：未走自然到达/战斗/搜刮/奖励流程；1920 未单独留证，沿用 B6 长文案布局结论；F6 整体实机验收由用户按 `docs/07` P1-J 确认。F6 全部任务完成，未提交。

## 2026-11-01 M23-F4-T1 聚落数据与生成

**数据**：新增 `assets/settlements.json`（`settlements-v1`）与 `game::SettlementConfig`；内置 `village_haven` 村庄营地，中心 `(75.0,26.5)`、安全半径 8 格，商人/守卫/求助者点位分别为 `(75.5,26.5)`/`(76.5,25.5)`/`(73.5,27.5)`，5 档库存 `[4,2,1,2,1]`、补货 1 天、4 条任务池。解析要求三类 NPC 齐全、点位在安全区内、库存/任务池合法；mod `content/settlements.json` 只追加新 id。

**生成**：单机/服务器商人优先使用聚落 merchant 点并应用聚落初始库存，`restockDays` 覆盖交易补货配置；无聚落时回退旧 `trading.merchantX/Y` 或玩家附近。`SettlementConfig` 同时挂到 `GameContext`，为后续 NPC 实体/日程/对话提供只读数据。

**指纹**：`kContentHashVersion` 升 v11，覆盖安全区、三类 NPC 点位、库存、补货日与任务池；`GameApp`/`ServerHost` 加载同一文件并比对，内嵌服务器测试补聚落路径。

**验证**：新增 `test_settlements` 3 用例与 Python `test_settlement_data` 3 用例；`test_content_hash`/`test_mod_content`/`test_embedded_server` 同步。Debug **643/643（129705 assertions）**、Release **643/643（129719 assertions）**、Python **81/81**；Debug 无窗口服务器 180 tick、Release 单机 1280 烟测通过，日志 `build/f4-t1-app-smoke.log` 显示聚落 1、商人 `(75,26)`。应用 Debug/Release 构建通过。未提交。

**限制**：当前只有数据和商人生成；守卫/求助者实体、日程、对话/任务入口、聚落防御与联机权威状态分别留 F4-T2–T6。

## 2026-11-01 M23-F4-T2 NPC 实体与日程

**组件**：新增 `game::SettlementNpc` 与 `game/SettlementNpc.*`。商人复用现有实体挂日程组件，守卫/求助者按 `settlements.json` 点位精确生成；三类 NPC 共用 Position/Velocity/SpriteRef/Facing/AnimState/ActionState/MoveIntent/PathRequest/PathFollow，头顶色条显示当前活动。未加载或阻挡格逐帧重试，不生成在错误位置。

**日程/威胁**：`SettlementNpcSystem` 在 AiSystem 之后、PathFollow 之前。守卫 08–18 巡逻、18–22 站岗、22–08 休息；商人/求助者 08–20 驻点、20–08 休息。巡逻使用确定性选点，到点停 2.5–5.5 秒。活僵尸进入聚落中心 `safeRadius+4` 时统一切 Shelter 并走安全区中心，威胁解除后保留 4 秒再恢复。NPC 距焦点超过 36 格清空路径/移动意图并冻结，回到范围后按当前时段重排。

**存档边界**：T2 不新增存档组件。单机/服务器世界快照排除守卫/求助者临时实体，商人继续保存 `MerchantStock`，读档后由 `EnsureSettlementNpcs` 重新挂日程，避免旧档重复 NPC。专用 NPC 状态、服务器权威交互与客户端只读复制留 F4-T6。

**验证**：新增 `test_settlement_npc` 4 用例 43 assertions（昼夜日程、威胁躲避/解除、远程冻结恢复、生成组件）。Debug **647/647（129748 assertions）**、Release **647/647（129762 assertions）**、Python **81/81**；Debug 无窗口服务器（网络关闭+本地玩家）180 tick 三类 NPC 就位（`build/f4-t2-server-local-smoke.log`），Release 单机 1280 截图 `build/f4-t2-settlement-1280.png`（`build/f4-t2-app-capture-release.log`）。应用 Debug/Release 构建通过。未提交。

**限制**：对话/任务入口、守卫战斗/死亡、正式美术与 NPC 状态存档/联机留 F4-T3–T6；截图使用关闭 POI/游荡者的构造场景。

## 2026-11-02 M23-F4-T3 聚落对话/交互 UI

**运行时**：新增 `game::Dialogue`（`DialogueTree/DialogueNode/DialogueOption/DialogueContext`）。条件支持 Always/MissionAvailable/MissionActive/MissionNone/ItemAtLeast（可取反），结果支持 Goto/Close/OpenTrade/AcceptSettlementMission/FocusSettlementMission；条件只读聚落 `missionPool`、`MissionLog`、天数和背包，不新增 ECS/存档字段。

**对话树/接入**：代码内置商人/守卫/求助者三棵树。商人“看看你的货”直接打开现有商人面板，“最近消息”进入子节点；守卫提供营地安全问答；求助者提供“看看我现在的委托/接一件委托/现在没有委托”，焦点复用 Mission HUD，接取走 `ActivateMission`。E 交互优先于门/床/篝火；对话中 1–5 直接选择、滚轮定位、E 确认、ESC 关闭，离开范围/结束一局/关闭时清理。native 与 core_ui Lua/Python 都渲染 `dialogue-panel`，`HudState`/脚本 API 新增 open/speaker/text/options/selected/hint。

**修复**：首次实现中 E 被篝火交互抢占，已改为 NPC 优先；对话卡片曾同时使用绝对定位 `hud-box` 导致宽度收缩，已移除并固定在热键栏上方。

**验证**：新增 `test_dialogue` 3 用例 45 assertions（树入口、任务/物品条件、选项过滤）；`test_ui` 在 native/core_ui、1280/1920 下校验面板宽度、屏幕边界、热键栏不重叠。Debug **650/650（129897 assertions）**、Release **650/650（129911 assertions）**、Python **81/81**；Release 1280 截图 `build/f4-t3-dialogue-1280.png`（商人对话，`build/f4-t3-dialogue-capture-release2.log`）与 `build/f4-t3-trade-1280.png`（选项 1 打开现有交易面板，`build/f4-t3-trade-capture.log`）。应用 Debug/Release 构建通过。未提交。

**限制**：对话树为代码内置，数据化/mod 扩展留 T8；任务接取受现有自动接取策略影响，任务类型/奖励/放弃/重复限制留 T4；联机权威交互与 NPC 状态存档留 T6。截图关闭 POI/游荡者。

## 2026-11-02 M23-F4-T4 聚落任务与奖励

**数据/链**：`MissionDef` 新增 `MissionTaskKind`（generic/help/escort/clear/supply）与 `autoActivate/timeLimitDays/retryDays`。`config/game.json` 新增 `settlement_help→settlement_escort→settlement_clear→settlement_supply` 四段 once 链：收集绷带×2 → 医院南门接应并回村 → 击杀 6 名感染者 → 搜索医院物资箱并回村；`assets/settlements.json` 的求助者 `missionPool` 指向这四条。四段均 source=npc、autoActivate=false、限时 2/3/2/3 天、retryDays=1，奖励 70/80/90/100 XP + 既有物资。

**接取/失败**：自动接取、每日刷新、随机事件循环均跳过 `autoActivate=false`；只有求助者对话 Accept 写入 MissionLog。聚落已有进行中委托时 `MissionAvailable` 返回 false，避免同时接多件；完成 once 后 `MissionCanActivate` 永假。`MissionDeadlineDay = 接取日 + timeLimitDays`，到期当天由 `MissionIsExpired` 判定失败，单机 `FailMission`/服务器 tick 都清焦点、把失败日写回 state.day；超时与 `K` 放弃都按 `retryDays` 冷却后重试。奖励继续复用 `MissionRewardDef`/`GrantMissionReward`，只在 Active→Completed 发放一次。

**UI/指纹**：任务 HUD 对非 generic 类型显示 `[求助]/[护送]/[清剿]/[物资]` 与 `限N天`；`MissionTaskKindName/Label` 提供稳定 token。`HashMissionDef` 纳入 task/autoActivate/timeLimitDays/retryDays，`kContentHashVersion` 升 v12；基础任务 15→19 条、一次性链 5→6 条。

**验证**：`test_missions` 更新 19 条并新增聚落手动接取/时限失败/重试冷却/完成防重/前置链/类型标签用例；`test_mod_content` 基线 19→20；`test_content_hash` 覆盖 task/autoActivate/timeLimitDays/retryDays；Python `test_mission_data`/`test_settlement_data` 同步。Debug **651/651（129967 assertions）**、Release **651/651（129981 assertions）**、Python **81/81**；应用 Debug/Release 构建通过。未提交。

**限制**：护送为接应点+回村路线，不生成跟随 NPC；联机委托接取/服务器权威奖励与 NPC 状态存档留 F4-T6，独立任务链截图与完整烟测留 F4-T7。Debug 网络/性能用例偶发时序或负载 flake，隔离与全量重跑通过。

## 2026-11-02 M23-F4-T5 聚落防御

**数据/触发**：`settlements-v1` 新增可选 `defense`，覆盖夜袭窗口、激活/生成半径、基础/每晚/上限人数、守卫/夜袭者生命、伤害、攻击距离/间隔、移动速度与 `guardBehavior=defend|retreat`；基础村庄显式开启，旧数据/未配置 mod 默认关闭。22:00–05:00、玩家进入营地 24 格内且当晚未触发时开战；从安全区外环生成 2 名起、每晚递增至 4 名，黎明或玩家远离后清理，当晚不重复。

**行为/失守**：守卫在 `defend` 下切 `Defend` 追击并近战，商人/求助者切 `Shelter` 撤到中心；`retreat` 时守卫也撤离。守卫新增独立 `Health`、`FriendlyNpc` 标记与战斗生命值，夜袭期间临时移除 `NoCombatTarget`，可被击倒并走尸体流程但不会计入僵尸击杀/任务清剿；战斗结束清除伤口/硬直并保留损失生命。存活守卫归零后聚落 `fallen`，清空夜袭者、停止后续夜袭、暂停交易/委托并阻止补生守卫。夜袭由 `SettlementNpcSystem` 独立维护，不读写 `BaseSiege`，与玩家基地守夜分别结算。

**数据/隔离**：`SettlementRaidZombie` 不进入世界快照；守卫/求助者仍按 T2 临时 NPC 处理，NPC 生命/失守状态与网络权威留 T6。客户端通用复制排除聚落临时 NPC，夜袭者作为普通角色复制；T5 未新增专用 HUD 行，通过中央提示、NPC 活动色条与交互提示表达状态。防御字段进入内容指纹 v13。

**验证**：新增 `test_settlement_defense` 4 用例 35 assertions（夜袭触发/分工、守卫击杀修复、守卫死亡失守、撤退与远离不触发）；`test_settlements` 增加防御数据与非法小时校验；`test_content_hash` 覆盖防御字段；Python `test_settlement_data` 校验防御契约。Debug **656/656（130015 assertions）**、Release **656/656（130030 assertions）**、Python **81/81**；应用 Debug/Release 构建通过；Release 无窗口服务器 1800 tick（`startDay=10/startHour=22`）日志 `build/f4-t5-server-raid-long-smoke.log` 显示守卫迎战并击杀 2 名夜袭者后正常结束。Release 首轮 A* 性能阈值偶发超时（隔离重跑 0.3ms），全量重跑通过。

**限制**：无独立 1280 夜袭截图与自然流程烟测，视觉验收并入 T7；守卫不使用武器/远程攻击，不治疗/补给；NPC/失守持久化与客户端权威交互留 T6。

## 2026-11-02 M23-F4-T6 聚落存档与联机

**存档**：新增 `SettlementState`（`STLS` v1，固定 StableId `0x4D32334634535441`），保存防御聚落的 `fallen/guardHealth/lastNightKey`。单机 F12 与服务器 `SaveWorld` 在实体快照前写入；`WorldSimulation::LoadSave` 与单机读档恢复实体后应用。守卫仍是临时实体，读档按保存生命重建，失守时不补生；夜袭中的感染者不入档，active 不跨档恢复，但保留当晚已触发标记，避免同晚重复夜袭。

**联机**：`ZLR1` 新增 `SettlementNpc`（聚落/类型/activity/home），商人、守卫、求助者都复制；客户端只显示，不调度日程/战斗。`PlayerState` 升 v16，追加 owner-only `PlayerSettlementProgress{id,fallen,raidActive,guardHealth,guardHealthMax}` 与聚落提示序号/文本/色调；客户端只读映射并据此拦截对话/交易。新增 `AcceptSettlementMission=39`，客户端只发求助者 StableId，服务器校验实体/类型/同层/距离/失守或夜袭/无进行中委托/可接任务后写服务器 MissionLog；`TradeOffer` 额外拒绝失守/夜袭聚落。

**保存覆盖**：商人库存继续由 `MerchantStock` 世界实体保存，任务进度继续由 `MissionLog` 进入单机世界快照与服务器玩家档案。服务器重启测试实际把守卫生命改为 61、商人首档库存改为 0，退出并同存档重启后恢复守卫生命、库存和进行中 `settlement_help`。

**验证**：新增 4 用例 81 assertions（`SettlementState` 组件经 `EntityStore` 往返与失守重建、`PlayerState v16` 往返、联机 NPC 复制与服务器权威接取、服务器同存档重启恢复）。`test_replication` 更新为 27 个组件，内置存档组件 43，`test_player_state` 增加 v15 旧负载回退。Debug **660/660（130108 assertions）**、Release **660/660（130122 assertions）**、Python **81/81**；应用 Debug/Release 构建通过；Release 无窗口服务器 180 tick 烟测通过。Release 首轮既有远程武器时序 flake，隔离重跑通过。

**限制**：夜袭进行中不跨存档恢复；守卫位置/巡逻目标不单独持久化；客户端聚落联机视觉/双客户端自然流程并入 F4-T7。

## 2026-11-02 M23-F4-T7 聚落测试与烟测

**自动化**：新增 `test_settlement_integration` 2 用例（内置四段委托链对话接取/完成/不重复、库存配置与夹紧），`test_settlement_npc` 1 用例（巡逻/躲避目标避开阻挡格），`test_settlement_defense` 1 用例（同晚读档不重复夜袭），`test_settlement_network` 1 用例（双客户端 NPC 复制、A 接取后重复拒绝、B 独立接取、失守交易拒绝）。T7 共 5 用例 167 assertions。

**回归/烟测**：全量 Debug **665/665（130275 assertions）**、Release **665/665（130290 assertions）**、Python **81/81**；应用 Debug/Release 构建通过；Release 无窗口服务器 180 tick 烟测通过。Release 1280 自动构造 `build/p1-f4-t7-dialogue-1280.png`、`build/p1-f4-t7-mission-1280.png`、`build/p1-f4-t7-raid-1280.png`；日志 `build/f4-t7-dialogue.log`、`mission.log`、`raid.log` 显示聚落夜袭触发、守卫迎战与 49/49 寻路查询完成、0 失败。临时探针/临时防御配置不入库。

**限制**：真实自然流程（无探针/无临时配置）、双客户端画面/声音和 1920 聚落布局仍建议人工复验；T7 不新增运行时/存档/协议字段。

## 2026-11-02 M23-F4-T8 聚落/NPC 文档

**文档**：新增 `docs/settlementformat.md`，记录 `settlements-v1` 完整字段/范围/默认值、文件合并与内容指纹、NPC 点位生成、日程/威胁、5 档交易与补货、`missionPool`/求助者对话、`defense` 触发/数量/结束公式、`STLS`/`ZLR1`/`PlayerState v16` 存档联机边界、mod 目录/示例/检查清单。

**扩展边界**：`docs/modding.md` 3.6.3.4 改为最小契约并链接完整文档；当时只有 `defs[0]` 主聚落生成 NPC/交易入口，追加聚落只有数据/指纹/防御状态、没有独立 NPC 或对话；对话树仍为代码内置，不提供对话 JSON；交易固定 5 档；守卫策略只有 `defend/retreat`（M33-F1-T2 已补多聚落生成）。

**回填**：README 文档索引/聚落段/M23 里程碑、docs/01、docs/05、docs/08 与本节同步；修正 T1 记录中错误的任务池引用（应为四段聚落链），并将“对话数据化留 T8”改为“T8 记录当前边界，不提供对话 JSON”。

**验证**：T8 仅文档变更，不新增运行时/存档/协议字段；沿用 T7 基线 Debug **665/665（130275 assertions）**、Release **665/665（130290 assertions）**、Python **81/81**；文档 BOM/CRLF/尾随空格/`git diff --check` 通过。

## 2026-11-02 X-F3-T1 内容文件监听

**监听**：`script::ModWatcher` 改为按 `scripts`+`mod.json`、`content`、`assets`、`other` 四类计算签名（文件数/最新 mtime/总字节/≤256KB 内容指纹）；脚本/metadata/other 变更继续立即触发脚本重载，content/assets 变更合并后等待防抖静默期，新增 `PollContentChanges()` 返回 `{modId,content,assets}`。扫描不可读时保留旧基线，避免编辑中途误触发。

**来源/接线**：`ModContentResult.sources` 暴露所有有效 mod 清单的 id/根目录，含仅 content、无脚本或当前内容解析失败的 mod；GameApp 用该列表创建监听（脚本禁用时也可监听），主循环消费 content/assets 通知并记录 `[content]` 日志。配置新增 `script.contentHotReloadDebounceSeconds`（默认 0.5s）。T1 不替换注册表、不刷新指纹；事务重载/失败回滚留 X3-T2，指纹/联机策略留 X3-T3/T4。

**测试/烟测**：新增 4 用例（content/assets 分类隔离、连续写入防抖合并、仅 content mod、sources 覆盖），热重载相关 7 用例 56 assertions；全量 Debug **669/669（130305 assertions）**、Release **669/669（130319 assertions）**、Python **81/81**；应用 Debug/Release 构建通过。Release 临时 `watch_mod` 烟测：启动日志显示监控轮询 0.2s/防抖 0.3s，修改 `content/items.json` 后输出 `检测到 mod 内容文件变更：[watch_mod] content=是 assets=否`；临时 mod/日志不入库。

**限制**：T1 只监听/分类/防抖，内容修改仍不改变游戏内注册表；assets 热替换、内容事务替换与失败回滚、指纹刷新、联机拒绝/重连和 UI 提示均待后续任务。新增 mod 目录不会动态加入监听。

## 2026-11-02 X-F3-T2 事务式内容重载

**候选事务**：新增 `game::ContentBundle`/`LoadContentBundle`，在独立 registries 重新加载基础+mod 的 items/recipes/buildings/loot/forage/animals/crops/POI/missions/settlements，并执行完整交叉引用校验；`LoadModContent` 增加基础错误与建造冲突计数，任一 mod 被跳过、任务文件未全部通过、POI/聚落定义被跳过均判失败。运行中注册表在候选成功前不触碰。

**提交/刷新**：`GameApp::ReloadContentFromDisk` 只在成功后 move 到成员注册表，更新 `missionSourceJson`/`modBuildables`，`contentReloadCount++` 并重算内容指纹；`WorldDemoState` 下一次 tick 重建建造目录、复制新 POI/任务/聚落配置并清理已删除任务焦点。失败日志为 `mod 内容热重载失败，保留旧内容：…`。

**守卫/边界**：联机客户端/主机、`dynamicContentCount>0` 时拒绝；`assets/` 仍只通知，整目录新增/删除仍要重启；已生成实体/容器/NPC 与旧档中已删除物品 id 不回溯迁移。

**测试/烟测**：新增 `tests/test_content_reload.cpp` 3 用例 86 assertions（全类型成功、成功替换并移除旧定义、items/recipes/buildings/loot/POI/missions 任一失败旧候选不变）；定向 X3-T1/T2/M7-T6 11 用例 148 assertions；全量 Debug **672/672（130391 assertions）**、Release **672/672（130405 assertions）**、Python **81/81**；应用 Debug/Release 构建通过。Release 临时 `watch_mod` 合法→非法→合法烟测：2 次重载成功、1 次失败保留旧内容、2 次世界状态刷新，失败时无指纹刷新；日志 `build/x3-t2-smoke.log`，临时文件不入库。

## 2026-11-03 M23-F2a-T5 自然行走觅食与房屋工具柜搜刮烟测

**范围**：新增 `build/f2a_t5_natural_forage_smoke.ps1`。从出生点开始，用实际窗口按键（W+D/D+S 组合 + 位置反馈）走到草地节点；不使用 `Player.Teleport`，仅用临时探针添加 `forage_stick`。

**结果**：玩家从 `(69.42,23.50)` 走到草地节点旁（约 `(72,18.2)`），HUD 显示“可觅食 · 草地（2.0 秒）”；按 `E` 后日志“开始觅食/完成觅食，获得 3 件”。随后按 `I` 打开实际“房屋工具柜”，日志无线电零件×1/螺丝刀×1、`take actions=12`，任务“无线电求救”完成（40 经验），容器清空、背包出现无线电零件/螺丝刀。截图 `build/f2a_t5_natural_forage/f2a-t5-natural-forage-approach-1280.png`、`f2a-t5-natural-forage-after-1280.png`、`f2a-t5-natural-forage-loot-1280.png`、`f2a-t5-natural-forage-loot-taken-1280.png`。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：工具由探针添加；鼠标拖拽已由 K10 覆盖，自然战斗已由 K11（高生命）与 K12（100 HP 低生命）覆盖（高生命；低伤害平衡待补）。未提交。

## 2026-11-03 M23-F1-T11 真实鼠标拖拽取物烟测

**范围**：新增 `build/f1_t11_drag_smoke.ps1`。临时探针仅把玩家传送到实际房屋工具柜旁 `(72.0,18.2)`、设 100000 HP，且不自动取物；按 `I` 打开容器后使用真实 OS 鼠标事件（`SetCursorPos` + `mouse_event`）完成拖放。

**结果**：先从容器槽 0（720,196）拖到背包槽 5（324,365），日志 `拾取（容器）：无线电零件 x1`，`lootStacks 2→1`、`bagStacks 0→1`；再把剩余物品拖到槽 6（394,365），日志 `拾取（容器）：螺丝刀 x1`，`lootStacks 1→0`、`bagStacks 1→2`。截图 `build/f1_t11_drag/f1-t11-drag-before-1280.png`、`f1-t11-drag-after-first-1280.png`、`f1-t11-drag-after-second-1280.png` 显示容器 2 堆→1 堆→空，背包出现两件；未调用 `Inventory.Action`。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：传送/高生命来自临时探针；此前的失败尝试使用 `PostMessage`，不会移动 OS 光标或更新 GLFW 轮询位置，真实鼠标事件才触发 RmlUi `dragstart/dragdrop`；自然战斗已由 K11（高生命）与 K12（100 HP 低生命）覆盖（高生命；低伤害平衡待补）。未提交。

## 2026-11-03 M23-F1-T11 实际地图僵尸自然战斗烟测

**范围**：新增 `build/f1_t11_natural_combat_smoke.ps1`。单机新档，窗口按键从出生点 `(69.42,23.50)` 走到实际地图僵尸 id 42 附近；不调用 `Player.Teleport` 或 `Zombies.Spawn`，临时探针只监听 `OnZombieSpawn/OnZombieDead` 与位置。启动参数保留一只 demo 僵尸、1000 HP、关闭动物；实际目标为地图僵尸。

**结果**：日志 5 次 `玩家挥击（SPACE）`，拳头伤害 40、距离约 1.0–1.5 格；`实体死亡：稳定 ID 42（格 33.9,11.9）→ 尸体（稳定 ID 43）`，探针收到 `OnZombieDead id=42`。截图 `build/f1_t11_natural_combat/f1-t11-natural-combat-spawn-1280.png`、`...-approach-1280.png`、`...-engage-1280.png`、`...-after-1280.png` 显示接近、近身交战与尸体。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：高生命与 demo 僵尸数量来自启动参数；实际目标为地图僵尸，不是 `Zombies.Spawn` 注入，也无传送；低伤害由后续 K12 的 100 HP 独立烟测补齐。未提交。

## 2026-11-03 M23-F1-T11 100 HP 低生命自然战斗烟测

**范围**：新增 `build/f1_t11_lowdamage_combat_smoke.ps1`，从 K11 脚本复制，仅把玩家生命改为 100、独立输出目录。仍从出生点仅用窗口按键走到实际地图僵尸附近，不调用 `Player.Teleport`/`Zombies.Spawn`，临时探针只监听 `OnZombieSpawn/OnZombieDead` 与位置；`demoZombies=1`、关闭动物。

**结果**：与实际地图僵尸 id 41/42 交战，日志记录玩家受到 6.4/12.4 伤害，生命 87.0→67.3 HP、流血 2 处；多次 `玩家挥击（SPACE）`（拳头 40 伤害）击杀 id 41/42，`实体死亡：稳定 ID 41→尸体 43`、`ID 42→尸体 44`，探针收到 `OnZombieDead id=42`。截图 `build/f1_t11_lowdamage_combat/f1-t11-lowdamage-combat-spawn-1280.png`、`...-approach-1280.png`、`...-engage-1280.png`、`...-after-1280.png` 显示低生命/流血 HUD、接近、交战与尸体。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：100 HP 与 demo 僵尸数量来自启动参数；实际目标为地图僵尸，无传送/探针生成；自然枪械画面与真实玩家手感仍待实机。未提交。

## 2026-11-03 M23-F2a-T5 实际水岸钓鱼烟测

**范围**：新增 `build/f2a_t5_fishing_smoke.ps1`。临时探针添加 `fishing_rod×1`/`fish_bait×5`，把玩家传送到实际地图静水岸边 `(81.5,30.2)`，按 `E` 抛竿；不直接发鱼获。

**结果**：HUD 从“可钓鱼 · 静水岸边（5.0 秒）”进入钓鱼中；咬钩截图显示“咬钩！按 E 收杆：静水岸边（1.9 秒）”，再按 `E` 后日志“完成钓鱼：still_water，成功=true，获得 2 件”。截图 `build/f2a_t5_fishing/f2a-t5-fishing-after-1280.png` 显示小鱼×2、鱼饵 5→4、生存经验 10。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：鱼竿/鱼饵由探针添加；未提交。

## 2026-11-03 M23-F2a-T5 真实草地节点烟测

**范围**：新增 `build/f2a_t5_forage_smoke.ps1`。临时探针添加 `forage_stick`，把玩家传送到实际地图草地节点 `(73,18)`，按 `E` 搜索；不生成节点、不直接发产出。

**结果**：日志“开始觅食：meadow_grass (73,18)，耗时 2.00s”“完成觅食：meadow_grass (73,18)，获得 3 件”；截图 `build/f2a_t5_forage/f2a-t5-forage-after-1280.png` 显示“采集了草地：野果×2、草药×1”和左侧“觅食 · 草地（已耗尽，约 180 分钟刷新）”。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：工具由探针添加；水岸钓鱼/咬钩已由下节补验。未提交。

## 2026-11-03 M23-F1-T11 实际地图开火链路烟测

**范围**：新增 `build/f1_t11_combat_smoke.ps1`。实际军事检查站军械柜（101.5,20.5）搜索后，用 `Inventory.Action` 取出手枪与 9mm，装备手枪，关闭背包按 `R` 换弹，再按 `V` 开火；9/9 守军在场，玩家 100000 HP。

**结果**：日志“装备（背包槽 0）：手枪”“换弹完成：手枪，装填 8 发”“远程开火：手枪 弹药 7/8，弹丸 1，伤害 18，射程 18.0，弹速 20.0，噪声半径 28”；截图 `build/f1_t11_combat/f1-t11-combat-equipped-1280.png`、`f1-t11-combat-fire-1280.png` 显示装备槽、HUD 7/8/备弹 8 与检查站守军交火/流血。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：传送与高生命来自临时探针；取物/装备走 `Inventory.Action`，不是鼠标拖拽；未覆盖自然探索/自然战斗流程。未提交。

## 2026-11-03 M23-F1-T11 实际军械柜烟测

**范围**：新增 `build/f1_t11_armory_loot_smoke.ps1`。探针把玩家传送到实际军事检查站军械柜 (101.5, 20.5) 旁的 (101.5, 21.5)，保留 9/9 守军并设 100000 HP；按 `I` 触发实际地图容器搜索，再用 `Inventory.Action` 取出物品并截图；不注入物品、不修改掉落表，脚本断言日志含“手枪”。

**结果**：日志 `搜索容器：表=poi_military_armory…结果 3 堆/54 件（手枪×1/9mm×26/霰弹×27）`；`Inventory.Action` 20 次后背包手枪×1/9mm×19，容器剩余 9mm×7/霰弹×27。截图 `build/f1_t11_armory_loot/f1-t11-armory-loot-1280.png`，日志 `build/f1_t11_armory_loot/app.log`。该用例证明实际地图容器可获得枪械；测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：传送与高生命来自临时探针；取物使用 `Inventory.Action`，不是鼠标拖拽；未覆盖自然探索/战斗；只取出 20 件，容器未完全清空。未提交。

## 2026-11-03 M23-F1-T11 实际地图容器烟测

**范围**：新增 `build/f1_t11_map_loot_smoke.ps1`。探针只把玩家传送到实际废弃加油站储物柜 (33.5, 20.5) 旁并设高生命，保留 3 名守军；靠近按 `I` 触发实际地图容器搜索，再用 `Inventory.Action` 取出物品并截图，不注入物品、不修改掉落表。

**结果**：日志 `搜索容器：表=poi_gas_station…结果 5 堆/5 件`；本次随机结果为维修包/燃料罐/防暴背心/灭火器/螺丝刀各 1，取出后容器“（空）0 堆”。截图 `build/f1_t11_map_loot/f1-t11-map-loot-1280.png`，日志 `build/f1_t11_map_loot/app.log`。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：传送与高生命来自临时探针；取物使用 `Inventory.Action`，不是鼠标拖拽；未覆盖自然探索/战斗。未提交。

## 2026-11-03 M23-F1-T11 搜刮容器 UI 烟测

**范围**：新增 `build/f1_t11_loot_ui_smoke.ps1` 与临时 mod。临时 POI 在空地 (78.5, 62.5) 放置固定 `pistol×1 + ammo_9mm×8` 的战利品容器；探针只传送玩家，不直接添加物品。靠近容器按 `I` 触发 UI 搜索，随后通过 `Inventory.Action` 从容器取出全部物品并截图。

**结果**：日志 `搜索容器：表=f1_t11_pickup_loot…结果 2 堆/9 件`；取出后容器显示“（空）0 堆”，背包手枪×1、9mm×8。截图 `build/f1_t11_loot_ui/f1-t11-loot-picked-1280.png`，日志 `build/f1_t11_loot_ui/app.log`。测试基线不变（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：K5 的 POI/容器为临时 mod 构造；实际地图容器另由 K6 覆盖。取物使用 `Inventory.Action`，不是鼠标拖拽。未提交。

## 2026-11-03 M23-F1-T11 商人交易 UI 烟测

**范围**：新增 `build/f1_t11_trade_ui_smoke.ps1` 与临时探针 mod。探针只提供 `wood×10` 并把玩家传送到村庄商人附近，不直接添加枪械；窗口按键 `E` 打开商人对话、`1` 进入交易、`5` 购买手枪、`4` 购买 9mm，最后关闭交易打开背包截图。

**结果**：日志记录 `商人交易：pistol，库存 0` 与 `商人交易：ammo_9mm，库存 1`；交易截图提示“交易完成：手枪 ×1”“交易完成：9mm 弹药 ×12”，木材 10→0，背包截图显示手枪×1、9mm×12。截图 `build/f1_t11_trade_ui/f1-t11-trade-*.png`，日志 `build/f1_t11_trade_ui/app.log`。该烟测不改变测试基线（Debug 678/130523、Release 678/130537、Python 81/81）。

**限制**：木材与传送来自临时探针，不是自然搜刮/探索；未覆盖搜刮容器拾取。未提交。

## 2026-11-03 M23-F1-T9 枪械交易获取回归

**范围**：扩展 `tests/test_network_inventory.cpp` 的 X-F1 2b-4 商人用例。在真实服务器/客户端会话中购买 `pistol×1`（8 木材）与 `ammo_9mm×12`（2 木材），同步到 owner `PlayerState` 后按背包索引装备手枪，再发 `ReloadRanged` 完成装填 8 发、备弹 4。

**结果**：商人库存、服务器背包、客户端背包/装备/弹匣逐项断言；该用例 33 assertions。全量 Debug **678/678（130523 assertions）**、Release **678/678（130537 assertions）**、Python **81/81**。证明服务器权威交易可获得枪械并直接进入装备/换弹链路；自然探索仍待实机。未提交。

## 2026-11-03 M23-F1-T11 双客户端网络枪声

**问题**：网络模式下客户端 `FireRanged` 只发命令，本地不预测枪声；服务器 `EmitGunshot` 只把 `OnSound` 投递给服务器 AI，没有网络广播，因此其他客户端听不到枪声，射手也没有声音回放。

**实现**：新增 `net/WorldSound` `ZLW1` v1 消息（34B 头 + clip ≤64B；来源 StableId、x/y/z、半径、type、clip），严格校验 magic/version/保留位/类型/有限坐标/半径/clip。`ServerHost::EmitGunshot` 在服务器 AI 事件后向所有已分配会话广播（含射手）；`NetworkClient` 解码入队，`WorldDemoState::ApplyNetworkWorldSounds` 播放 3D 声音并生成枪口 `effect.impact`。KCP 丢包时该次远端表现可缺失，服务器 AI 听觉不受影响。

**测试/烟测**：`test_net` 新增 1 用例 25 assertions；`test_network_inventory` T10 真实手枪双客户端断言双方收到同一 `source=射手`、位置/层/半径、`clip=gunshot`，服务器 `worldSoundsSent` 增加。全量 Debug **678/678（130512 assertions）**、Release **678/678（130526 assertions）**、Python **81/81**。Release 1280 双客户端 + 无窗口服务器，A 按 F7 三次；A/B 日志各 3 条 `网络枪声` 与脚本 `gunshot at`，截图 `build/f1_t11_dualsound/f1-t11-dual-*-1280.png`。

**限制**：只同步枪声，脚步/命中/低吼仍本地；ZLW1 不重发/不排序；F7 为调试枪声，真实枪械拾取/交易与自然战斗画面仍待人工。未提交。

## 2026-11-03 X-F3-T6 文档收口

**范围**：回填 `docs/modding.md` 的内容管线/热重载边界，`README.md` 的功能说明与 M23 状态，`docs/01/02/03/05/08` 的 X-F3 T5/T6 结果、677/130475 与 677/130489 基线及最终限制，并在 `docs/08` 增加 X3-T6 收口记录。

**最终边界**：仅单机 `net.mode=direct` 且无脚本动态内容时，`content/*.json` 可事务热重载；失败保留旧内容，成功记录旧→新指纹并刷新世界定义。`assets/*.json` 仍只通知并需重启；新增/删除整个 mod 目录需重启；联机/主机拒绝热重载并提示重启服务器+所有客户端重连；动态内容仅单机，联机 API/启动拒绝并显示 HUD 提示；旧档已删除 mod 物品保留 `defId`，但名称/重量/图标不可用。

**验证**：Debug **677/677（130475 assertions）**、Release **677/677（130489 assertions）**、Python **81/81**；文档 UTF-8 无 BOM、CRLF、链接、`config/game.json`、尾随空格与 `git diff --check` 检查通过。T6 无新增生产代码、协议或测试。

**结论**：X-F3 T1–T6 全部完成；联机动态内容、assets 热替换与旧档 id 迁移不在当前范围内。

## 2026-11-03 X-F3-T5 热重载、联机动态内容与旧档测试

**失败/原子性**：在合法 `hot` mod 基础上分别写入空 `forage-v1`、`crops-v1`、`settlements-v1` 文件；`LoadContentBundle` 均失败并保留旧候选的物品/POI/任务。移除非法文件后旧候选指纹恢复等于基线，随后相同字节重载得到相同指纹，锁定“失败不提交、未变内容指纹稳定”。注意 `ComputeTreeHash` 包含 mods 文件树，非法文件存在时它会变化，因此失败后指纹断言在清理文件后比较。

**动态内容联机**：`allowDynamicContent=false` 时五个 `Content.Register*` 全部返回 0，统一错误“动态内容仅支持单机；请移除动态注册后重启”；物品/配方/建造/POI/任务、`dynamicContentCount` 和 `contentChanged` 均不变。

**旧档**：构造 `hot_item×2 + rag×1` 的 `Container` 快照，删除 mod 后重新加载内容并解码/应用；`hot_item` 仍保留，`CountOf` 为 2，`TotalWeight` 对未知定义记 0kg 且只统计 `rag`。旧档不会崩溃或静默删除未知物品，但名称/重量/图标在缺失 mod 时不可用。

**测试**：新增 `X3-T5` 3 用例 72 assertions；X3-T1~T5 共 12 用例 200 assertions；全量 Debug **677/677（130475 assertions）**、Release **677/677（130489 assertions）**、Python **81/81**。无新增生产代码或协议；T6 文档收口见下条。

## 2026-11-03 X-F3-T4 动态内容联机策略

**决策**：保留“仅单机”边界，不实现“服务器注册、客户端校验”。运行时动态内容需要同步 JSON、引用资源、脚本状态与晚加入/重连回滚，成本和攻击面高于本阶段收益；联机双方继续依赖启动时内容指纹，必须安装相同静态内容。

**策略/提示**：新增 `game::DynamicContentBlockedReason` 与统一文案“动态内容仅支持单机；请移除动态注册后重启”。`Content.Register*` 在 `allowDynamicContent=false` 时返回 0；`StartClientOnly`/`StartHosting` 在 `dynamicContentCount>0` 时拒绝并记录“联机启动被拒绝”；脚本加载后若注册了动态内容，GameApp 记录“动态内容 N 项：仅单机可用；联机连接/主机启动将拒绝”。T3 的联机热重载拒绝不再只有日志：GameApp 写入 `GameContext::contentNotice`，`WorldDemoState::RefreshHud` 消费为 5 秒 fail 色 HUD notice，并记录“内容策略提示已显示”。

**测试/烟测**：新增 `X3-T4` 1 用例 4 assertions（单机允许、动态内容拒绝、联机热重载/动态提示文案统一），`test_script_api` 对齐同一文案；X3-T1~T4 共 9 用例 128 assertions；全量 Debug **674/674（130403 assertions）**、Release **674/674（130417 assertions）**、Python **81/81**；应用 Debug/Release 构建通过。联机客户端（无服务器）改 content 后日志出现拒绝与“内容策略提示已显示”，无成功热重载；单机临时 mod 调 `Content.RegisterItems` 后出现动态注册与“仅单机可用”范围提示。日志 `build/x3-t4-online-smoke.log`、`build/x3-t4-dynamic-smoke.log`，临时文件不入库。限制：不支持联机动态内容/服务器下发/回滚，assets 热替换、动态内容存档与旧档删除 id 迁移仍不在本任务内；T5/T6 待做。

## 2026-11-02 X-F3-T3 指纹刷新与联机策略

**指纹**：`GameApp::ReloadContentFromDisk` 在 move 提交前保存旧 `contentHash`，成功后经 `RefreshContentHash` 重算，并输出 `mod 内容指纹刷新：0x旧→0x新`；候选字节未变时允许指纹相同，但仍重建注册表。成功日志仍保留物品/配方/建造/战利品/POI/任务数量变化。

**策略**：新增 `game::ContentReloadBlockedReason`。仅 `net.mode=direct` 且 `dynamicContentCount==0` 返回空；联机客户端/主机返回“联机中不支持 mod 内容热重载；请重启服务器和所有客户端后重连”；存在脚本动态内容返回“脚本已动态注册内容，需重启游戏后应用 mod 内容变更”。主循环先检查并记录 `mod 内容变更未应用：…`，不构建候选、不替换注册表、不刷新指纹、不推进 `contentReloadCount`；`ReloadContentFromDisk` 保留同一守卫。

**测试/烟测**：新增 1 用例 8 assertions（候选内容变化改变指纹、单机允许、联机/主机拒绝、动态内容拒绝）；X3-T2/T3 共 4 用例 94 assertions；定向 X3-T1/T2/T3/M7-T6 12 用例 156 assertions；全量 Debug **673/673（130399 assertions）**、Release **673/673（130413 assertions）**、Python **81/81**；应用 Debug/Release 构建通过。单机 Release 合法→非法→合法烟测出现 2 次指纹旧→新、失败不刷新；联机客户端改 content 只出现 `mod 内容变更未应用：联机中不支持 mod 内容热重载；请重启服务器和所有客户端后重连`，无成功/刷新。日志 `build/x3-t3-single-smoke.log`、`build/x3-t3-online-smoke.log`，临时文件不入库。限制：T3 当时仅日志告警，无 HUD toast；T4 已补 HUD notice 并确认动态内容仅单机；T5 测试、T6 文档收口待做。

