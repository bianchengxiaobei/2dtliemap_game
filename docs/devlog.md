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
