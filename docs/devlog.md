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

