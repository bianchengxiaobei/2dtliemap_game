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
