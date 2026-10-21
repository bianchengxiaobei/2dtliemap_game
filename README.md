# ZomboidLike（开发代号）

模仿 Project Zomboid 架构、从零用 C++ 实现的 2D 等距视角僵尸生存游戏。

- **技术栈**：C++20 · GLFW + bgfx · Bullet · EnTT（ECS）· Lua 5.4 + sol2（可选 Python 后端）· CMake + vcpkg
- **设计目标**：模块化、数据驱动、事件总线 + 脚本可 mod；单机优先，架构预留联机命令层
- **参考对象**：本机 Project Zomboid v41.78.16 反编译源码（`E:\Game\Project.Zomboid.v41.78.16\Project.Zomboid.v41.78.16\rebuild\src\zombie`）
  —— 只读学习**架构思路与算法**，严禁复制任何代码、贴图、地图、脚本文本

## 文档索引

| 文档 | 内容 | 何时看 |
|---|---|---|
| [docs/00-架构总览.md](docs/00-架构总览.md) | 总体设计、世界模型、渲染/AI/脚本/存档架构、PZ 类对照表、决策记录 | 动手前通读一遍 |
| [docs/01-里程碑与任务清单.md](docs/01-里程碑与任务清单.md) | ★ 主任务文档：M0~M13 全部任务、验收标准，边做边勾 | 每天都在用 |
| [docs/02-技术选型与开发规范.md](docs/02-技术选型与开发规范.md) | 依赖清单、目录结构、编码/线程/内存规范、Git 流程 | 建工程时 + 写代码前 |
| [docs/03-脚本系统设计.md](docs/03-脚本系统设计.md) | Lua/Python 双后端、Host API、事件桥接、mod 格式 | M7/M8 阶段 |

## 新对话启动指南（写代码从这里开始）

本 README 是整个项目的**唯一入口**。新开 AI 对话写代码时，把下面这段直接发给它：

```
项目在 <项目根目录>（本机 E:\Game\ZomboidLike）。请按顺序读：
README.md → docs/00-架构总览.md → docs/01-里程碑与任务清单.md → docs/02-技术选型与开发规范.md
（涉及脚本系统时再读 docs/03-脚本系统设计.md；涉及占位资源时读 assets_placeholder_pz/README.md）。
以 docs/01 的任务清单为唯一任务依据，M1–M12 已完成，当前从 M13 联机基础开始（T1 KCP 传输、T2 会话握手、T3 无窗口 ServerHost、T4 实体复制层、T5 双客户端垂直切片、T6 安全与容错、T7 服务器权威背包/世界交互、T8 玩家持久化、T9 联机入口与聊天/玩家列表、T10 远程装备/动作/投射物复制已完成）；写代码遵守 docs/02 的规范；
构建命令走 tools/dev.ps1（Ninja 需要 MSVC 环境，如 .\tools\dev.ps1 cmake --build --preset x64-debug）；
每完成一个任务把清单里对应项勾上，Git 提交信息带任务号（如 feat(render): sprite batch (M2-T3)）。
```

阅读顺序与用途：

1. **README.md（本文件）** —— 全局视图、入口
2. **docs/00-架构总览.md** —— 动手前通读（分层/世界模型/ECS/渲染/决策记录）
3. **docs/01-里程碑与任务清单.md** —— ★ 每天的任务依据（当前进度：M1–M12 已完成；M13-T1 KCP 传输、T2 会话握手、T3 无窗口 ServerHost、T4 实体复制层、T5 双客户端垂直切片、T6 安全与容错、T7 服务器权威背包/世界交互、T8 玩家持久化、T9 联机入口与聊天/玩家列表、T10 远程装备/动作/投射物复制已完成；M0 仅剩跨机验收）；**docs/modding.md** —— 玩家 mod 开发指南（API v1，Lua/Python）
4. **docs/02-技术选型与开发规范.md** —— 建工程、写代码前（依赖/目录/编码规范/占位资源策略）
5. **docs/03-脚本系统设计.md** —— M7/M8 阶段再读
6. **assets_placeholder_pz/README.md** —— 用占位资源、重新解包 PZ 纹理时读

## 开发环境要求（Windows）

| 组件 | 版本 | 说明 |
|---|---|---|
| Visual Studio 2022 | 17.8+ | 安装时勾选「使用 C++ 的桌面开发」 |
| CMake | 3.24+ | `winget install Kitware.CMake` |
| Ninja | 任意新版 | `winget install Ninja-build.Ninja` |
| Git | 任意新版 | `winget install Git.Git` |
| vcpkg | 最新 | `git clone https://github.com/microsoft/vcpkg E:\Dev\vcpkg`，然后运行 `bootstrap-vcpkg.bat`，设环境变量 `VCPKG_ROOT=E:\Dev\vcpkg` |

设置并验证工具链（新开 PowerShell；`VCPKG_ROOT` 应指向 vcpkg 仓库根目录）：

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "E:\Dev\vcpkg", "User")
$env:VCPKG_ROOT = "E:\Dev\vcpkg"
cmake --version
ninja --version
git --version
& "$env:VCPKG_ROOT\vcpkg.exe" version
```

## 快速开始

> 工程骨架在 M0-T2 创建（见任务清单）。骨架就绪后的日常命令（Ninja 需要 MSVC 环境：普通 PowerShell 用 `tools/dev.ps1` 包装；已开「x64 Native Tools 命令提示符」时可直接跑 cmake）：

```powershell
.\tools\dev.ps1 cmake --preset x64-debug        # 配置；首次会下载/编译 vcpkg 依赖，可能 10~20 分钟
.\tools\dev.ps1 cmake --build --preset x64-debug
build\x64-debug\ZomboidLike.exe
```

> 首次配置前确保已按上节设置 `VCPKG_ROOT`；`dev.ps1` 会自动加载 VS2022 x64 环境（`cl`/`INCLUDE`/`LIB`）。

日常游玩/性能验证建议用优化构建（RelWithDebInfo，仍可附加调试器）：

```powershell
.\tools\dev.ps1 cmake --preset x64-release
.\tools\dev.ps1 cmake --build --preset x64-release
.\build\x64-release\ZomboidLike.exe
```

Debug（`x64-debug`）是 `/Od` 无优化构建，240+ 敌人时帧率会明显低于 Release；排查断点再用。

无窗口服务器冒烟（M13-T3；不带 `--server.ticks` 会持续运行，Ctrl+C 停止；默认监听 UDP，`--server.network=false` 可关闭）：

```powershell
.\build\x64-debug\ZomboidLike.exe --server --server.ticks=180
```

双客户端联机（M13-T5）：先启动服务器，再启动两个客户端（默认 `127.0.0.1:7777`，两侧 `net.contentHash` 必须一致）：

```powershell
.\build\x64-debug\ZomboidLike.exe --server
.\build\x64-debug\ZomboidLike.exe --net.mode=client --game.startState=world
.\build\x64-debug\ZomboidLike.exe --net.mode=client --game.startState=world
```

客户端只发送命令并渲染服务器快照；移动、攻击、离开清理与重连由服务器权威处理。拾取/使用/装备/丢弃/维修/制作同样提交服务器执行，背包面板展示服务器下发的 owner-only 状态；地面物品由 `ZLR1` 复制。`ZLP1` 状态默认每 6 tick 刷新，制作进度与动作提示会随之更新。

客户端首次运行会在 `saves/client_identity.txt` 生成持久身份（也可用 `net.playerIdentity` 固定）；独立服务器默认把玩家档案写到 `saves/multiplayer_players.bin`。同一身份重连（包括旧会话尚未超时的接管）会复用原 `StableId`、位置、背包、装备、配方和生命值；死亡后默认 5 秒在出生点复活（保留背包，生命回满）。档案每 30 秒、断线、死亡、复活和服务器退出时原子保存。

联机入口 UI（M13-T9）：主菜单点“联机游戏”后可填写玩家名称、服务器地址/端口并加入，或在本机“开始主机”（监听 `0.0.0.0`，端口填 0 自动分配）。加入/主机成功后本机自动进入世界；服务器地址可收藏到 `saves/servers.json`（主菜单服务器列表）。游戏内按 `Enter` 打开聊天、再按 `Enter` 发送、`Esc` 取消；右上角显示在线玩家列表，聊天记录显示在左下角。联机模式按 `V` 发射服务器权威投射物（演示用，无弹药消耗）。命令行 `--server` / `--net.mode=client` 仍保留用于独立服务器和自动化测试。

## 里程碑状态

（每完成一个里程碑，更新此表并打 tag，规则见任务清单开头）

| 里程碑 | 内容 | 状态 | 完成日期 |
|---|---|---|---|
| M0 | 环境与工程骨架 | 进行中（仅剩换机验收；本机干净目录预验通过） | 2026-10-16（主体） |
| M1 | 核心框架（主循环/tick/事件/输入/配置） | 已完成 | 2026-10-16 |
| M2 | 渲染地基（等距瓦片可见） | 已完成 | 2026-10-16 |
| M3 | 世界模型与流式加载 | 已完成 | 2026-10-16 |
| M4 | ECS（EnTT）、物理与寻路 | 已完成 | 2026-10-16 |
| M5 | 角色与僵尸 AI | 完成（v0.5.0，验收 500 僵尸 0.396ms/tick） | |
| M6 | 物品与容器 | 完成（v0.6.0；T4 背包 UI 顺延至正式 UI） | |
| M7 | Lua 脚本系统 | 完成（v0.7.0，验收 2026-10-17） | |
| M8 | Python 脚本后端（可选开关） | 完成（v0.8.0，验收 2026-10-17） | |
| M9 | UI 系统 | 完成（T1–T6，验收 2026-10-17） | |
| M10 | 存档系统 | 完成（T1–T5，验收 2026-10-18） | |
| M11 | 音频系统 | 完成（T1–T4，验收 2026-10-18） | |
| M12 | 打磨与联机命令层预留（可选） | 完成（T1–T4，验收 2026-10-18） | 2026-10-18 |
| M13 | 联机基础（KCP + 权威服务器） | 完成 T1–T10；M0 跨机验收待做 | |

## 许可与合规

- 本项目为个人学习项目。借鉴的是架构思想（分块世界、固定 tick、事件驱动、命令化输入），这些不受版权保护但**代码表达受保护**；
- 不得把 PZ 的反编译代码、贴图、地图数据、脚本、音效直接放入本项目；
- 开发期允许用本地 PZ 资源做**临时占位**（"先把游戏跑起来"）：全部放 `assets_placeholder_pz/`（自带 gitignore，**不入公开仓库、不分发**），获取方式见 `docs/02` §9.1 与该目录 README；**发布前必须全部替换为自制/授权资源**；
- 第三方库的许可证清单见 docs/02 的依赖总表。
