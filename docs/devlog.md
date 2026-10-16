# 开发日志（devlog）

> 每个里程碑结束写一段小结：学了什么 / 踩了什么坑 / 下阶段风险（见 docs/01 附录）。

---

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
