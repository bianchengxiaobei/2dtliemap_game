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
| [docs/01-里程碑与任务清单.md](docs/01-里程碑与任务清单.md) | ★ 主任务文档：M0~M12 全部任务、验收标准，边做边勾 | 每天都在用 |
| [docs/02-技术选型与开发规范.md](docs/02-技术选型与开发规范.md) | 依赖清单、目录结构、编码/线程/内存规范、Git 流程 | 建工程时 + 写代码前 |
| [docs/03-脚本系统设计.md](docs/03-脚本系统设计.md) | Lua/Python 双后端、Host API、事件桥接、mod 格式 | M7/M8 阶段 |

## 新对话启动指南（写代码从这里开始）

本 README 是整个项目的**唯一入口**。新开 AI 对话写代码时，把下面这段直接发给它：

```
项目在 E:\Game\ZomboidLike。请按顺序读：
README.md → docs/00-架构总览.md → docs/01-里程碑与任务清单.md → docs/02-技术选型与开发规范.md
（涉及脚本系统时再读 docs/03-脚本系统设计.md；涉及占位资源时读 assets_placeholder_pz/README.md）。
以 docs/01 的任务清单为唯一任务依据，当前为 M4 验收阶段（500 静态体步进、10000 实体压力）；写代码遵守 docs/02 的规范；
构建命令走 tools/dev.ps1（Ninja 需要 MSVC 环境，如 .\tools\dev.ps1 cmake --build --preset x64-debug）；
每完成一个任务把清单里对应项勾上，Git 提交信息带任务号（如 feat(render): sprite batch (M2-T3)）。
```

阅读顺序与用途：

1. **README.md（本文件）** —— 全局视图、入口
2. **docs/00-架构总览.md** —— 动手前通读（分层/世界模型/ECS/渲染/决策记录）
3. **docs/01-里程碑与任务清单.md** —— ★ 每天的任务依据（当前进度：M4-T7 已完成；剩 M4 验收压测；M0 仅剩跨机验收）
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

## 快速开始

> 工程骨架在 M0-T2 创建（见任务清单）。骨架就绪后的日常命令（Ninja 需要 MSVC 环境：普通 PowerShell 用 `tools/dev.ps1` 包装；已开「x64 Native Tools 命令提示符」时可直接跑 cmake）：

```powershell
.\tools\dev.ps1 cmake --preset x64-debug        # 配置（首次会自动装依赖，需要几分钟）
.\tools\dev.ps1 cmake --build --preset x64-debug
build\x64-debug\ZomboidLike.exe
```

## 里程碑状态

（每完成一个里程碑，更新此表并打 tag，规则见任务清单开头）

| 里程碑 | 内容 | 状态 | 完成日期 |
|---|---|---|---|
| M0 | 环境与工程骨架 | 进行中（仅剩跨机验收） | 2026-10-16（主体） |
| M1 | 核心框架（主循环/tick/事件/输入/配置） | 已完成 | 2026-10-16 |
| M2 | 渲染地基（等距瓦片可见） | 已完成 | 2026-10-16 |
| M3 | 世界模型与流式加载 | 已完成 | 2026-10-16 |
| M4 | ECS（EnTT）、物理与寻路 | 进行中（T1~T7 完成；剩验收压测） | |
| M5 | 角色与僵尸 AI | 未开始 | |
| M6 | 物品与容器 | 未开始 | |
| M7 | Lua 脚本系统 | 未开始 | |
| M8 | Python 脚本后端（可选开关） | 未开始 | |
| M9 | UI 系统 | 未开始 | |
| M10 | 存档系统 | 未开始 | |
| M11 | 音频系统 | 未开始 | |
| M12 | 打磨与联机命令层预留（可选） | 未开始 | |

## 许可与合规

- 本项目为个人学习项目。借鉴的是架构思想（分块世界、固定 tick、事件驱动、命令化输入），这些不受版权保护但**代码表达受保护**；
- 不得把 PZ 的反编译代码、贴图、地图数据、脚本、音效直接放入本项目；
- 开发期允许用本地 PZ 资源做**临时占位**（"先把游戏跑起来"）：全部放 `assets_placeholder_pz/`（自带 gitignore，**不入公开仓库、不分发**），获取方式见 `docs/02` §9.1 与该目录 README；**发布前必须全部替换为自制/授权资源**；
- 第三方库的许可证清单见 docs/02 的依赖总表。
