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

