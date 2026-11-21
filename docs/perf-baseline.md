# 性能基线（每里程碑验收对照，M3 建立）

> 预算表见 `docs/02 §6`。本文件记录**每次里程碑验收**的实测数据；工具：游戏内 perf 日志
> （每 5s 一行：FPS/实例/解析耗时 P50/P95/max/工作集）+ PowerShell 外部采样工作集。

## 测试环境

| 项 | 值 |
|---|---|
| 机器 | 开发机（Win11，16 硬件线程 → 任务池 14 线程） |
| 构建 | **x64-debug（5 分钟巡航）** + **x64-release / RelWithDebInfo（1 分钟快测）** |
| 窗口 | 1280×720 / vsync on |
| 地图 | `stress_100x100`（100×100 chunks = 1M 格，3693 个非默认 chunk，17.7k 对象） |
| 巡航 | 圆心 (500,500) 半径 40 格、速度 30 格/s（`world.autoCruise=true`） |

## M3-T7 基线（2026-10-16，M3 验收）

**场景 1：自动巡航（持续加载/卸载 churn）**

| 指标 | Debug / 5min | Release / 1min | 预算 | 结论 |
|---|---|---|---|---|
| chunk 累计加载 / 卸载 | 7253 / 7204（取消 0，失败 0） | 1485 / 1436（取消 0，失败 0） | — | 状态机无泄漏、无卡死 |
| 驻留 chunk | 恒定 49（7×7 窗口） | 恒定 49 | — | 稳态内存不随移动增长 |
| 单块解析 P50 | 0.44 ms | 0.20 ms | — | — |
| 单块解析 **P95** | **0.84 ms** | **0.37 ms** | < 2 ms | ✔ |
| 单块解析 max | 5.05 ms（首个冷读） | 0.89 ms | — | ✔ |
| FPS | 中位 60 / P5 42 / min 38 | **中位 60 / P5 58 / min 57** | 60fps 目标 | ✔（Release 稳定 60） |
| 帧内 Update 峰值（主线程，不等解析） | < 10 ms（T4 巡航测试断言） | — | 无卡顿 | ✔ |
| 同帧 drawcall | 3（own 单图集） | 3 | < 100 | ✔ |
| 工作集内存 | 232.5 → 252.4 MB（+8.6% / 5min，Debug 堆行为） | **138.2 → 139.0 MB（+0.6MB / 1min）** | 卸载后回落基线 ±20% | ✔ |

**场景 2：静态取景（村庄）**

| 指标 | own provider | placeholder_pz |
|---|---|---|
| 实例（收集） | 5034 | 5034 |
| 四边形（绘制） | 2934 | 2884 |
| drawcall | **3** | **244**（⚠ 超预算，见下） |
| FPS（Debug） | 59~60 | 59~60 |

## M4 验收基线（2026-10-16，M4 验收）

**场景 1：物理步进（单测基准 `test_physics`：5 chunk 全墙 500 静态体 + 50 随机速度角色体，60 tick）**

| 指标 | Debug | Release | 预算 | 结论 |
|---|---|---|---|---|
| 平均步进 | 1.066 ms/帧 | **0.139 ms/帧** | < 2 ms/帧 | ✔（Release 余量 14×） |
| 运行时 demo（village 64 静态体 / 40 游荡者 + 玩家） | step 0.55 ms | — | — | ✔ |

**场景 2：ECS 压力（单测 `test_ecs`：10000 实体 × 3 轮 创建/销毁，实体含 Position/Velocity/Collider/SpriteRef）**

| 指标 | 结果 | 判定 |
|---|---|---|
| 每轮存活/StableId 数归零 | 10000 → 0，三轮一致 | ✔ 无泄漏 |
| 组件 storage size 归零（4 种组件） | 每轮后 0 | ✔ |
| EnTT storage 容量复用 | 第 2/3 轮 capacity == 第 1 轮 | ✔ 无增长 |
| Debug + Release 全量测试 | 85 用例 / 117166 断言全绿（两构建） | ✔ |

**场景 3：寻路（M4-T6/T7 单测 + 冒烟，Debug）**

| 指标 | 值 | 预算 | 结论 |
|---|---|---|---|
| 2270 格蛇形路径（单测，全展开） | 2.09 ms（后台线程） | < 5 ms | ✔ |
| 运行时查询（40 游荡者 + 点击移动，78 次） | 平均 ~0.9 ms，主线程无感知 | — | ✔ |
| demo 同屏（village 40 游荡者 + 点击移动） | FPS 59~60（Debug） | 60fps | ✔ |

## M5 预验收基线（2026-10-16，M5-T5 后；正式验收在 M5 收尾 T7 后用实机场景复测）

**场景：500 registry 僵尸（全速 LOD 档）+ 200 虚拟僵尸（4 群落×50，统计层）的模拟 tick**
（单测 `test_boids` 基准；口径 = 主线程模拟链 索引/LOD/感知/AI/Boids/移动/虚拟群落，
不含物理步进与寻路后台）

| 指标 | Debug | Release | 预算 | 结论 |
|---|---|---|---|---|
| 平均模拟 tick | 10.2 ms | **0.396 ms** | < 4 ms | ✔（Release 余量 10×） |
| 分解（Release） | — | boids 0.321 / ai 0.024 / perception 0.029 / index+lod+move ≈ 0.02 ms | — | Boids 为主要成本 |
| 优化前（Boids 用 chunk 粒度索引逐候选查组件） | 242 ms/tick（Debug） | — | — | 见 devlog M5-T5（网格缓存后降 56×） |

## M5 验收基线（2026-10-16，M5-T7 后；全链口径 + 实机 500 复测）

**场景 A：500 registry 僵尸 + 200 虚拟僵尸的模拟 tick（全链口径）**
（单测 `test_boids` 基准；主线程模拟链 = 空间索引/LOD/感知/AI/Boids/**Combat**/虚拟群落漂移/
移动/**SpawnLod 扫描**，不含物理步进与寻路后台；稳态：无实体化/虚拟化转换）

| 指标 | Debug | Release | 预算 | 结论 |
|---|---|---|---|---|
| 平均模拟 tick | 12.37 ms | **0.396 ms** | < 4 ms | ✔（Release 余量 10×） |
| 分解（Release） | — | **boids 0.308** / perception 0.026 / ai 0.023 / combat 0.010 / spawnlod 0.009 / index+lod+move+virtual ≈ 0.02 ms | — | Boids（邻域网格）为主要成本；M5-T6/T7 新增两系统合计 0.019 ms |
| 守恒断言 | live=500 + virtual=200，转换 0/0（稳态），LOD full=500 | — | — | 无双计/漏计 |

**场景 B：实机（Release）500 同屏压测**（命令行覆盖 populationTarget=500 / maxLive=520 /
materializePerSecond=480 拉满）+ 密度守恒核对：

| 指标 | 实测 | 备注 |
|---|---|---|
| FPS | **59（帧 16.5ms）** | 图 ~5500 实例 / 3116 四边形；玩家被尸群围杀中仍在跑 |
| 稳定态人口 | live≈424~454 + virtual≈46~76 = **500 恒定** | 补给 260 一次补齐；实体化累计 690 / 虚拟化累计 276（玩家移动来回转换） |
| 病灶扫描 | SpawnLod 每 tick 扫描 500 实体 ≈ 0.009 ms（场景 A） | LOD 分档 full=141 / mid=292 随战况浮动 |

## M12 验收基线（2026-10-18，命令层 + 性能冲刺）

**场景 A：Release 500 人口 + 村庄大世界巡航 60s**（x64-release / RelWithDebInfo，1280×720，
vsync on，`audio.device=off`；`autoCruise=true`，圆心 (64,64)、半径 35、20 格/s；
`populationTarget=500 / maxLive=520 / materializePerSecond=480 / refillPerSecond=8`）

| 指标 | direct | loopback | 预算/结论 |
|---|---|---|---|
| FPS（11 个 5s 采样） | 中位 **60** / 最低 55 | 中位 **60** / 最低 57 | 60fps 目标；差异 <10% ✔ |
| 实例 / 四边形 / drawcall | 5039–5292 / 2900–3023 / 34–38 | 5044–5260 / 2861–3007 / 34–38 | <100 drawcall ✔ |
| 单块解析 P50 / P95 / max | 0.44–0.46 / 0.70–1.43 / 1.79 ms | 0.46 / 0.75–0.93 / 2.01 ms | P95 <2ms ✔ |
| chunk 累计加载 / 卸载 / 取消 / 失败 | 1102 / 1053 / 4 / 0 | 1102 / 1053 / 4 / 0 | 无泄漏 ✔ |
| 工作量（动态物理体峰值） | 417 | 251 | 500 人口含离屏虚拟层 |
| 工作集 | 173.6 → 179.0 MB | 173.6 → 177.8 MB | 基本持平 ✔ |
| 启动耗时（初始化完成） | 461 ms | — | 记录基线 ✔ |

- 500 人口核对（同配置 debug 采样）：`live=359 + virtual=141 = 500`（50s 时）；
  direct 性能run 动态体峰值 417，剩余为虚拟层/离屏。
- 命令层：loopback 60s 统计 `提交/执行=1/1`、`包发/收=1/1`、心跳快照 3611、tick 延迟 1；
  另做 W + F7 实机：`命令 提交=2 执行=2`，F7 触发 `枪声（F7）` 且感知 `听` 从 1 升到 21。
- **结论**：loopback 相对 direct 无可见帧率损失（采样中位相同，最低值受窗口调度影响）；
  命令最多 1 tick 延迟（16.7ms），移动/攻击/开枪烟测无额外延迟感。

**场景 B：地面批纹理排序（placeholder_pz，Release，村庄静态取景）**

| 指标 | 优化前（M5 基线） | M12 | 说明 |
|---|---|---|---|
| drawcall | 244 | **71** | stage 0 地面块按纹理聚簇，避免多图集交错 flush |
| FPS | 59–60 | 60 | 1280×720，实例约 5.1k |

**场景 C：chunk 主线程挂接预算（Debug 巡航）**

- `world.chunkAttachBudgetMs=2.0`、`chunkAttachBudget=8`；实测挂接帧 `lastAttachMs`
  0.62ms、预算命中 3 次，单帧未出现数百 ms 的挂接尖峰；至少执行 1 块避免饥饿。

## X-F3 质量基线（2026-10-26）

**场景：12/50/100 僵尸模拟 tick**

- 测试：`test_boids` 的 `X-F3 性能基线：12/50/100 僵尸模拟 tick`；每档 120 tick，焦点 (30,30)，僵尸在半径 1~14 格内追击玩家。
- 口径：空间索引 → LOD → 感知 → AI → 群体行为 → 战斗 → 移动 → 刷怪/虚拟化扫描 + Flush；**不含物理步进与寻路后台**，与 M5 500 人口基准同口径。
- 结果（同一台开发机，未固定 CPU 亲和性）：

| 僵尸数 | Debug / ms per tick | Release / ms per tick |
|---|---:|---:|
| 12 | 0.43–0.53 | 0.005–0.008 |
| 50 | 1.66–2.21 | 0.019–0.030 |
| 100 | 3.37–4.59 | 0.043–0.070 |

- 结论：Release 100 僵尸仍远低于 4ms 预算；Debug 100 僵尸约 3.4–4.6ms，仅作开发参考，不作为性能门槛。测试阈值：Debug <40ms 哨兵，Release <4ms。

**场景：1/16/64 仓库最近查找与存取**

- 测试：`test_storage` 的 `X-F3 性能基线：1/16/64 仓库最近查找与存取`；每档 2000 次 `FindNearestPlayerStorage`，再跑 30 轮“全部仓库 TakeAll→StoreAll”，每仓 6 栈×10 rag。
- 口径：最近查找平均每次耗时；存取为一轮内全部仓库的取空+存回，含容量/堆数校验，不含 UI。

| 仓库数 | Debug nearest / transfer | Release nearest / transfer |
|---:|---:|---:|
| 1 | 0.006–0.008 ms / 0.26–0.40 ms | 0.0000–0.0001 ms / 0.007–0.012 ms |
| 16 | 0.020–0.032 ms / 4.6–7.7 ms | 0.0003–0.0005 ms / 0.11–0.19 ms |
| 64 | 0.063–0.124 ms / 20–29 ms | 0.0010–0.0014 ms / 0.47–0.68 ms |

- 结论：仓库数量对最近查找近似线性，64 仓 Release 仍为微秒级；存取吞吐随仓库数线性增长。阈值：Debug nearest<10ms、transfer<100ms；Release nearest<1ms、transfer<10ms。

**场景：49 chunk 卸载、稀疏加载与文件重载**

- 测试：`test_chunkmanager` 的 `X-F3 性能基线：49 chunk 卸载、稀疏加载与文件重载`；3 轮从 village_demo (55,55) 跳到地图外 (505,505) 再返回。
- 口径：卸载为主线程 `Update` 同步移除 49 块；加载为 `PumpUntil` 到 49 块全部挂接的墙钟时间，含 1ms 轮询与后台调度，不是单块解析耗时。

| 指标 | Debug / ms | Release / ms |
|---|---:|---:|
| 卸载 49 块 | 58–84 | 4.7–6.5 |
| 稀疏窗口加载 49 块 | 172–201 | 173–180 |
| 文件窗口重载 49 块 | 180–242 | 178–181 |

- 结论：Release 卸载约 5–7ms、窗口加载约 180ms；Debug 卸载约 60–85ms、窗口加载约 180–240ms。阈值：Debug unload<200ms、load<3000ms；Release unload<20ms、load<1000ms。
- 功能性覆盖仍由 `test_storage` 的容量/堆数/StoreAll/TakeAll/拆除清空、`test_chunkmanager` 的 49 块驻留与卸载、`test_network_world_persistence` 的服务端强制卸载/重载与同 saveId 重启保证。

## M23-F2b-T7 动物更新基线（2026-10-31）

**场景：36 只动物（20 个已加载 chunk，玩家焦点固定）+ 240 tick**

- 测试：`test_animal_spawn` 的 `M23-F2b-T7 动物上限、墙体规避与更新预算`；生成上限 36，连续调用 240 次 `AnimalSystem::Update(0.016f)`，含感知收集/游荡/逃跑/反击/LOD 跳过与统计刷新；不含物理、渲染、复制。
- 口径：主线程动物系统 Update，Debug/Release 同机；墙体用例另用 1 个 chunk + 阻挡格验证位移不穿墙。

| 指标 | Debug | Release | 预算/结论 |
|---|---:|---:|---|
| 36 只平均更新 | 0.62–0.87 ms/tick | **0.017–0.022 ms/tick** | 远低于 4ms 模拟预算 ✔ |
| 全局上限 | 36 | 36 | 20 chunk 加载后仍恒定 ≤36 ✔ |
| 未加载快照 | 每 chunk 最多 16 条 | 同左 | 全局 2048 条上限，超出下次按种子重播种 |
| 存档恢复 | 1 chunk / 2 只 | 同左 | 生命 7.5、Flee 4.25s、已播种不重复生成 ✔ |

- 结论：动物系统在 36 只上限下 Release 约 0.02ms/tick，不是帧预算瓶颈；测试阈值 Debug <35ms、Release <4ms，主要用于捕获回归而不是硬件上限。

## M31-T0 大世界基线（2026-11-09）

**结论：保留 36×36 chunks，不降级到 24×24/30×30。** 当前生成骨架的解析、流式挂接、内存均在预算内；主要风险是同步保存的尾延迟，不是地图尺寸。

### 测试环境

| 项 | 值 |
|---|---|
| 机器 | Intel Core i7-10700（8 核 / 16 逻辑线程，2.90GHz），68GB RAM |
| 系统 | Windows 11 企业版 10.0.26200 |
| 构建 | x64-release（RelWithDebInfo）；无窗口 chunk 基准 |
| 地图 | 临时 `build/m31_t0/world_36`，36×36 chunks（360×360 格），seed 20261109，roads=4 |
| 地图内容 | 662/1296 个非默认 chunk，443,293 bytes，2,884 objects，100 rooms |
| 参数 | loadRadius=3（49 chunk/玩家），requestBudget=4，attachBudget=8，attachBudgetMs=2.0 |

> 基准工具是临时 `build/m31_t0/bench`（使用正式 `MapLoader`/`ChunkManager`/`ChunkDiffStore`，不修改存档格式/协议）。它只测 chunk 流式加载与挂接，不含渲染、物理、ECS 模拟和网络；不能替代 T3/T9 的实机 1280 验收。

### 单 chunk 解析

同一进程连续跑 3 轮，第一轮包含冷文件缓存，后两轮为热缓存。分位数按 nearest-rank。

| 口径 | 轮次 | P50 | P95 | max | 说明 |
|---|---:|---:|---:|---:|---|
| 全部 1296 chunk | 第 1 轮（冷） | 4.837 ms | 7.666 ms | 11.860 ms | 含 662 个文件读取 |
| 全部 1296 chunk | 第 2 轮（热） | 0.140 ms | 0.242 ms | 0.438 ms | 文件缓存已热 |
| 有文件 chunk（662） | 第 1 轮（冷） | 6.193 ms | 8.333 ms | 11.860 ms | 2,884 objects |
| 有文件 chunk（662） | 第 2 轮（热） | 0.157 ms | 0.273 ms | 0.438 ms | 2,884 objects |
| 有文件 chunk（662） | 第 3 轮（热） | 0.169 ms | 0.302 ms | 0.540 ms | 2,884 objects |

- 冷读 P95 约 8ms，必须留在后台线程；热解析 P95 约 0.3ms，说明当前 JSON chunk 格式本身不是瓶颈。
- T0 预算按**热解析 P95 <1ms**；冷读尖峰不进入主线程。

### 1 玩家流式加载/跨 20 chunk

焦点从 chunk (8,18) 向 +x 连续跨 20 chunks（200 格；2,000 个 60Hz 逻辑帧）。

| 指标 | 结果 |
|---|---|
| 初始加载 49 chunks | 140.99 ms 墙钟；62.5 ms 进程 CPU（one-core 44.3%）；private 7.86MB / working 12.64MB |
| 单块解析（ChunkManager 记录） | P50 0.0597ms / P95 0.5796ms |
| 跨越 20 chunks 主线程 chunk 工作/帧 | P50 0.0046ms / P95 0.0333ms / max 1.0941ms |
| Update 主线程/帧 | P95 0.0142ms / max 1.0466ms |
| attach 主线程/帧（非空） | P95 0.0133ms / max 0.4811ms |
| chunk 累计 | loaded 189 / unloaded 140 / cancelled 0；最终驻留 49 |
| 迁移后内存 | private 8.57MB / working 13.32MB |
| 总 CPU（初始+跨区） | 218.75ms / 24.76s 墙钟（含 1ms 轮询被 Windows 定时器拉长） |

- **帧工作**是主线程 `ChunkManager::Update + ProcessResults` 的耗时，不含渲染/物理；P95 0.033ms、max 1.09ms，远低于 100ms 卡顿门槛。
- 2,000 帧墙钟 24.6s 是基准脚本的 1ms sleep 被系统定时器放大，不是游戏帧时间；真实 60Hz 渲染需在 T3/T9 复测。

### 2 玩家分散

| 指标 | 结果 |
|---|---|
| 模型 | 两个独立 `World` + `ChunkManager` 共用一个 `TaskScheduler`；**当前服务器还没有多焦点，此值是上界/近似** |
| loaded chunks | 98（49×2） |
| 初始加载 A / B | 178.4ms / 136.0ms 墙钟 |
| 进程 CPU（两路加载+500 帧交错移动） | 218.75ms / 6.58s 墙钟 |
| 双路 chunk 工作/帧 | P95 0.0493ms |
| 内存 | private 15.27MB / working 20.14MB（加载前 private 3.15MB） |

- 按当前骨架内容，2 玩家分散的内存和主线程挂接成本都有充足余量；T8 的真实服务器多区域 AOI 仍需实测。

### 存档

用正式 `ChunkDiffStore`（原子写、每个 diff 一个文件）测 1000 个“4 格脏”chunk：

| 指标 | 结果 |
|---|---:|
| diff 大小（1/4/16/100 格） | 95 / 296 / 1,100 / 6,728 bytes |
| 1000 个 dirty chunk 总耗时 | 3,878.46 ms |
| 单文件写 P50 / P95 / max | 2.593 / 6.366 / 247.94 ms |
| 文件数 / 总字节 | 1,000 / 296,000 bytes |
| 平均每文件 | 3.878 ms / 296 bytes |

- 3.9s 总量可接受，但 **max 247.94ms** 证明原子写尾延迟不能放在主线程；T4/T9 必须用后台保存/快照，不能直接复用同步 `SaveChunk` 循环。
- 该测试只覆盖 chunk diff 部分；实际 `SaveManager` 还写 header/meta/sqlite、实体快照，T4 需要合并测量。

### T0 测试基线

| 构建 | 用例 | 断言 | 结果 |
|---|---:|---:|---|
| x64-release | 825 | 134,652 | 825/825 通过 |
| x64-debug | 825 | 134,638 | 825/825 通过 |
| Python (`tools/tests`) | — | 94 | 94/94 通过 |

- 首轮运行的是旧 Release 二进制，曾出现一次 `M30-T6` 时序偶发（821/822）；重建后 825/825，定向 `M30-T6*` 2/2 通过。后续以新二进制为准。
- 旧版 `docs/16 §2` 写的 822 是之前基线；M31-T0 复验后更新为 825。

### M31 预算初值（T0 校准）

| 指标 | T0 实测 | M31 预算 |
|---|---|---|
| 世界尺寸 | 36×36 chunks | **保留 36×36** |
| 每玩家驻留 | 49 chunks（loadRadius 3） | 不增加默认半径；resident 摘要另算 |
| 初始 49 chunk 加载 | 141ms 墙钟 / 62.5ms CPU | **≤300ms 墙钟、≤100ms CPU** |
| 热解析 P95 | 0.30ms（有文件） | ≤1ms |
| attach P95 | 0.013ms（非空帧） | ≤0.7ms；不阻塞主线程 |
| 主线程 chunk 工作/帧 | P95 0.033ms / max 1.09ms | P95 ≤1ms；实机无 >100ms 卡顿 |
| 2 玩家驻留/内存 | 98 chunks / private 15.3MB | loaded ≤128（含预测）；private ≤64MB（骨架内容） |
| 1000 dirty chunk 存档 | 3.88s，max 248ms/文件 | 后台总耗时 ≤8s；主线程单次快照 ≤1ms |
| chunk 文件数 | 662 | ≤1296；T9 视情况按 cell 打包 |

### 限制

1. 地图是生成骨架（2,884 objects），不是最终密度；T2/T10 内容增加后要复测解析、内存和存档。
2. 没有测渲染/物理/网络/ECS；真实 1280 帧时间在 T3/T9 用游戏内 perf 日志复测。
3. 2 玩家是“两个独立 World”的上界近似，不是当前服务器多焦点；T8 才能验证真实兴趣并集。
4. 冷读 P95 8ms 说明首访 IO 仍需后台；不能因热缓存数据把解析预算设得过低。
5. 存档尾延迟是当前最大风险；T4 不得同步写 1000 块。

### 复现

```powershell
py -3 tools/gen_map.py --name m31_t0_world --seed 20261109 --chunks 36x36 --roads 4 --out build/m31_t0/world_36

.\tools\dev.ps1 cmake -S build/m31_t0/bench -B build/m31_t0/bench-build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=E:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_INSTALLED_DIR=E:/Game/ZomboidLike/build/x64-release/vcpkg_installed

.\tools\dev.ps1 cmake --build build/m31_t0/bench-build

$env:PATH = "E:\Game\ZomboidLike\build\m31_t0\bench-build;E:\Game\ZomboidLike\build\x64-release;" + $env:PATH
.\build\m31_t0\bench-build\m31_t0_bench.exe --map build/m31_t0/world_36 `
  --out build/m31_t0/results.json --parse-rounds 3 --cross-chunks 20
```

> 临时基准源码/结果在 `build/m31_t0`（`build/` 已被 gitignore），不属于正式 M31 代码；T1 不依赖它。

## 已知问题与优化候选

1. **多图集交错 drawcall 高**（placeholder_pz）：M12 已把 stage 0 地面批按纹理聚簇，
   244→**71**（对象批仍受画家序约束，保持 38 左右）。进一步候选：
   - **运行期图集合并**（PZ 的 sprite pages 思路）：加载时把小图集重排进 1~2 张 2048² 页，纹理来源归一 → drawcall ≈ 2~3；
   - chunk 级"状态桶 + 相邻合并"（次选，收益有限）。
   - 当前 71 drawcall 对 60fps 无实际影响。
2. **Debug 构建 FPS 谷值 38~42**：出现在 chunk 加载突发/bgfx 纹理上传时（Debug 无优化）。
   **Release 巡航实测中位 60 / P5 58 / min 57**，无此问题。
3. Debug 工作集缓慢上涨（+8.6%/5min）：Release 同场景 60s 仅 +0.6MB（基本持平），
   判断为 Debug 堆行为（重分配无合并/调试填充），不是泄漏。

## 历次记录

| 日期 | 里程碑 | 场景 | 关键数据 |
|---|---|---|---|
| 2026-10-16 | M2 验收 | 128×128 地图 @0.5x，Release | 13063 精灵同屏 60fps（16.2ms），drawcall 3 |
| 2026-10-16 | M3-T7 | stress 100×100 巡航，Debug 5min / Release 1min | 7253 块加载 / P95 0.84ms（Release 0.37ms）/ 驻留 49 恒定 / Release FPS 60 稳定 |
| 2026-10-16 | M4 验收 | 物理 500 静态体基准（Debug/Release）+ ECS 10000×3 压力 | Release 0.139ms/帧（<2ms ✔）/ Debug 1.066ms；ECS storage 复用无泄漏 |
| 2026-10-16 | M5 验收 | 500 僵尸 + 200 虚拟全链模拟 tick + 实机 500 同屏压测 | Release **0.396ms/tick**（<4ms ✔，boids 0.308 主导）；实机 FPS 59、人口 500 恒定守恒 |
| 2026-10-18 | M12 验收 | Release 500 人口 + 大世界巡航 60s（direct/loopback）、placeholder 地面批、chunk 挂接预算 | 中位 60fps（direct 最低 55 / loopback 57，差异 <10%）；placeholder drawcall 244→71；挂接预算命中、启动 461ms |
| 2026-10-26 | X-F3 基线 | test_boids 12/50/100 僵尸模拟 tick（120 tick/档，无物理/寻路后台） | Release 0.005–0.008 / 0.019–0.030 / 0.043–0.070 ms/tick；Debug 0.43–0.53 / 1.66–2.21 / 3.37–4.59 ms/tick |
| 2026-10-26 | X-F3 基线 | 1/16/64 仓库 + 49 chunk 卸载/加载（3 轮） | Release 64 仓 0.0014ms/query、0.68ms/round；chunk unload 4.7–6.5ms、load 173–181ms |
| 2026-10-31 | F2b-T7 | 36 动物 / 20 chunk / 240 tick | Debug 0.62–0.87ms、Release 0.017–0.022ms/tick；上限 36 稳定；快照 1 chunk/2 只生命+Flee 恢复 |
| 2026-11-09 | M31-T0 | 临时 36×36、跨 20 chunk、1/2 玩家、1000 dirty chunk | 保留 36×36；热解析 P95 0.30ms；初始 49 chunk 141ms；主线程 chunk 工作 P95 0.033ms；2 玩家 private 15.27MB；1000 块写 3.88s、max 248ms |
| 2026-11-10 | M31-T2 | 正式 `village_demo_m31` 36×36，Release 1280 初始视角 | 1025/1296 非空 chunk、827,370 bytes、6381 objects/17 rooms；驻留 49 chunk；首帧约 445ms（T3 预测加载目标）；稳定 60fps；`mapHash=b00ef11346939918` |
| 2026-11-10 | M31-T3 | 正式 m31，Release 1280 出生 + 自动巡航；C++ 连续 13 chunk/快速跳跃 | 出生驻留 42（上边界裁掉越界位），首帧约 26ms，60fps；巡航保持 49 chunk、累计加载/卸载、失败 0；T3 定向 4/4（152 assertions），全量 Debug 837/136203、Release 837/136217、Python 98/98 |
| 2026-11-10 | M31-T4 | 正式 m31，Release 1280 F12 快速存档 → F8 自动巡航；manifest/区域桶/后台脏 chunk 写 | 累计加载 311 / 卸载 262 / 取消 5 / 失败 0；存档含 manifest + 4 个区域桶 + `.backup`；T4 定向 5/5（117 assertions），全量 Debug 842/136320、Python 98/98；Release 前次全量 842/136314，最终回归 841/842（既有 M30-T6 flake，隔离通过） |
| 2026-11-10 | M31-T5 | 正式 m31，Release 1280 启动 + WorldGraph 路线/桥/水查询 | 道路图 11 节点/10 边，出生点→checkpoint 车辆路线可达 492.0；出生 60fps，chunk 加载 42/卸载 0/失败 0；T5 定向 4/4（82 assertions），全量 Debug 846/136402、Release 846/136416、Python 98/98 |
| 2026-11-10 | M31-T6 | 正式 m31，Release 1280 启动 + 区域事件；F12 快速存档含事件状态 | 区域事件导演 10 区域，village_core 商队/巡逻生成 2 个；FPS 59–60，chunk 加载 42/卸载 0/失败 0；manifest 含 10 条事件；T6 定向 3/3（60 assertions），全量 Debug 849/136462、Release 849/136476、Python 98/98 |
| 2026-11-10 | M31-T7 | 正式 m31，Release 1280/1920 世界地图；M 打开、POI 点击导航；地图/HUD 截图归档 | 地图显示 10 区域/10 道路/桥梁河流/11 路标/12 POI；打开时已探索 POI 5/12；点击消防站生成 22 格驾车路线；地图打开采样 46–63（开图瞬间 46，稳定 60）、drawcall 约 698；T7 定向 3/3（72 assertions），全量 Debug 852/136534、Release 852/136548、Python 98/98 |
| 2026-11-10 | M31-T8 | Release 服务器多区域：2 玩家双区域并集、4 玩家分散压力、按会话结构 AOI、服务器 manifest/桶/事件重启 | 双区域并集 50 chunk、4 玩家分散并集 100 chunk，request/attach 预算不变；跨区实体互相移除、结构 diff 过滤计数 >0；服务器保存 `实体 2，chunk 13–15，桶 1，事件 10`；T8 定向 5/5（233 assertions），全量 Debug 857/136767、Release 857/136781、Python 98/98 |
| 2026-11-10 | M31-T9 | Release 1000+ 脏 chunk 后台写/读、36×36 加速 64.8 虚拟分钟 soak、1280 巡航 | 1024 chunk/4096 格：1027 文件/280,649 bytes，主线程快照 max 0.10ms、入队 0.11ms、后台写 3,805ms、单文件 max 13.66ms、读档 333ms；soak 1296/1296 chunk、max 驻留 49、失败 0、6 建造/38 战斗，tick P50/P95/max 0.58/1.39/3.59ms；2/4 玩家仍 50/100 chunk；T9 定向 2/2（36 assertions），全量 Debug 859/136803、Release 859/136817、Python 98/98 |
| 2026-11-21 | M36-T7 | Release 1280：室内剖切重算 + 500 人口村庄巡航 + 双客户端 | 室内候选 97–105，重算 P50/P95/max **0.072/0.111/0.111ms**（预算 ≤0.2）、射线 0/≤16；500 人口 42s 巡航 FPS 中位 **60**（关闭剖切对照 60）、chunk 失败 0；双客户端服务器 2 会话、快照 1079/失败 0、stderr 空；全量 Release 1178/145776、Debug 1178/145762、Python 127/127 |

**M31-T9 结论（2026-11-10）**：保留 36×36、loadRadius=3 与当前密度，不需要降级地图尺寸/半径/密度，也不需要按 cell 打包。1000+ 脏 chunk 仍是一 chunk 一文件；1024 个 4 格 diff 仅 1027 文件/280,649 bytes，低于 1296 文件目标。后台写总耗时 3.8s（≤8s），主线程单块快照 0.10ms、入队 0.11ms（≤1ms），单文件 max 13.66ms，证明尾延迟已移出主线程。64.8 虚拟分钟 soak 覆盖全部 1296 chunk、最多驻留 49、失败 0；Release tick P95 1.39ms，1280 巡航 FPS 58–60、1 次 32.1ms 帧告警、无 >100ms 卡顿。完整日志：`build/m31_t9_release_full.log`、`build/m31_t9_debug_full_final.log`、`build/m31_t9_smoke/world.out.log`。

| 日期 | 任务 | 场景 | 结果/证据 |
|---|---|---|---|
| 2026-11-10 | M31-T10 | Release 1280 单机默认 M31、世界地图、双客户端、主路线 | 单机 42 chunk、FPS 59–62、首帧 27.1ms；地图打开 FPS 59（UI 689 drawcall）；双客户端内容 hash 一致、快照 2441/失败 0、FPS 55–60；主路线图论 1810.35 格（步行 7.54/载具 3.02 分钟）；T10 4/4（225）、全量 Debug 863/137028、Release 863/137042、Python 98/98；截图 `build/m31_t10_slice/`、`build/m31_t10_dual/`，日志 `build/m31_t10_slice/world.out.log`、`build/m31_t10_dual/server.out.log` |

**M31-T10 结论（2026-11-10）**：默认单机 M31 在 1280 下保持 59–62 FPS、42 驻留 chunk，首帧 27.1ms，无 >100ms 长卡顿；世界地图 UI 打开时 59 FPS，未引入主线程长任务。双客户端在村庄同屏、内容 hash 一致、2441 次快照失败 0。主路线/内容预算支持 15–30 分钟自然流程，但**人工计时仍待用户验收**；专用服务器 M31 需显式地图/`serverEnabled`，不是服务器默认。

**场景 D：Debug 240 敌人帧率排查（2026-10-19）**

| 项目 | 优化前 | 优化后 | 说明 |
|---|---|---|---|
| 世界对象收集（Debug） | ~10 ms | **~2.5 ms** | 每帧对 5k 静态对象逐个查 `AssetProvider::Frames(string)`；改为相邻逻辑名缓存 |
| Debug 高频日志 | 600+ 行/s | **~10 行/s** | 寻路/逐 chunk 状态日志降到 trace；debug 构建不再每条 debug 日志强制 flush |
| Debug 240 敌人 FPS | 25~31 | 多数 45~64（偶发 30） | 240 live；Debug 出现帧耗时 17~25ms 时 vsync 会落到 30 |
| Release 240 敌人 FPS | 60 | **60** | RelWithDebInfo，实测 240 live 稳定 60（偶发 chunk 边界 44） |

- 结论：Debug 的世界对象资产查找与日志刷盘是真实瓶颈，已修；但 `/Od` 构建下系统总耗时
  仍高于 16.6ms，Debug 240 敌人不能保证全程 60。需要稳定 60 请运行 `x64-release`。
