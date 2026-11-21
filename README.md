# 2DTileMap Game

一个使用现代 C++ 开发的 2D 等距视角僵尸生存游戏。

玩家需要在开放区域中搜集物资、对抗感染者、建立基地并维持生存；支持单机与服务器权威联机，内容层使用 JSON + Lua / Python 脚本扩展。

## 项目特性

### 生存与探索

- 2D 等距视角，支持镜头跟随、世界地图、区域指引与多分辨率 HUD。
- 饥饿、口渴、疲劳、睡眠、体温、潮湿、伤口、流血、疼痛与感染系统。
- 近战、重击、格挡、弹反、闪避、远程武器、投掷、潜行与背刺。
- 昼夜、天气、火焰蔓延、燃烧、动物行为与尸群威胁。
- 大地图、多个兴趣点、任务链、随机世界事件、聚落与 NPC 日程。
- 室内等距可见性：按格剖切、方向墙体裁切、窗口扒看、屋顶归属、室外建筑遮挡与树冠压平。

### 基地与生产

- 建造木墙、木门、木地板、木箱、工作台、篝火、床、雨水收集器、泛光灯与电网。
- 拆建、维修、结构归属与多人协作。
- 农业、烹饪、食物保鲜、动物狩猎与资源采集。
- 储物、交易、捕兽夹/绊线警报/燃油陷阱布防、发电机供电与基地防御；陷阱有次数/冷却、拆除返还与 HUD 状态。
- 载具驾驶、载具耐久、维修、加油、后备箱与多人载具。

### 联机与扩展

- 专用服务器架构，移动、战斗、拾取、制作、建造与交易由服务器权威结算。
- 玩家档案、断线重连、服务器重启恢复与存档版本兼容。
- 内容驱动的物品、配方、建造、战利品、POI、任务、聚落、同伴、载具改装和陷阱。
- Lua / Python 双脚本后端，支持 `Mod.Data`、`Json`、`require` 与内容热重载。
- 可选 mod 内容包，支持内容指纹与客户端/服务器一致性校验。

## 技术栈

- C++20
- GLFW + bgfx
- Bullet Physics
- EnTT（ECS）
- Lua 5.4 + sol2；可选 Python 后端
- CMake + Ninja + vcpkg
- SQLite（存档元数据）

## 快速开始

### 环境要求

| 组件 | 说明 |
|---|---|
| Windows 10/11 x64 | 当前主要开发和验证平台 |
| Visual Studio 2022 | 安装“使用 C++ 的桌面开发”工作负载 |
| CMake 3.24+ | 建议加入 `PATH` |
| Ninja | 建议加入 `PATH` |
| Git | 任意较新版本 |
| vcpkg | 设置环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录 |

首次配置前，在 PowerShell 中确认工具链可用：

```powershell
cmake --version
ninja --version
git --version
$env:VCPKG_ROOT
```

如果 `VCPKG_ROOT` 为空，请先设置到本机 vcpkg 仓库根目录：

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

### 构建 Debug

```powershell
.\tools\dev.ps1 cmake --preset x64-debug
.\tools\dev.ps1 cmake --build --preset x64-debug --target ZomboidLike -j 12
.\build\x64-debug\ZomboidLike.exe
```

首次配置会下载并编译 vcpkg 依赖，耗时会明显长于后续构建。

### 构建 Release

```powershell
.\tools\dev.ps1 cmake --preset x64-release
.\tools\dev.ps1 cmake --build --preset x64-release --target ZomboidLike -j 12
.\build\x64-release\ZomboidLike.exe
```

实际游玩或进行性能验证时建议使用 Release；Debug 构建未优化，敌人较多时帧率会明显下降。

### 运行测试

```powershell
.\tools\dev.ps1 cmake --build --preset x64-release --target ZomboidLikeTests -j 12
.\build\x64-release\ZomboidLikeTests.exe --no-skip
```

Python 数据测试（如已安装 Python 3）：

```powershell
py -3 -m unittest discover -s tools/tests -p "test_*.py"
```

## 联机

先启动服务器：

```powershell
.\build\x64-release\ZomboidLike.exe --server
```

再启动客户端。同一台机器运行多个客户端时，每个客户端必须使用不同的 `net.playerIdentity`：

```powershell
.\build\x64-release\ZomboidLike.exe --net.mode=client --game.startState=world --net.playerIdentity=alice --net.playerName=Alice
.\build\x64-release\ZomboidLike.exe --net.mode=client --game.startState=world --net.playerIdentity=bob --net.playerName=Bob
```

服务器与客户端需使用一致的游戏内容与 mod；内容指纹不一致时会在握手阶段拒绝连接。

## 常用操作

| 操作 | 默认按键 |
|---|---|
| 移动 | `W` `A` `S` `D` |
| 轻击 / 重击 | `Space` / `Left Shift` |
| 格挡 / 闪避 | 按住 `Left Ctrl` / `Z` |
| 远程攻击 / 投掷 | `V` |
| 潜行 | `N` |
| 交互 / 拾取 / 布防或解除陷阱 | `E` |
| 打开背包 | `I` |
| 建造模式 / 放置陷阱 | `B` + 鼠标左键 |
| 拆除陷阱 | `X`（建造模式） |
| 角色面板 | `C` |
| 世界地图 | `M` |
| 切换任务焦点 | `L` |
| 快速存档 | `F12` |
| 联机聊天 | `Enter` |

按键可在配置中调整；具体玩法提示会随当前装备、目标与界面状态变化。

## 室内可见性配置

以下键位于 `config/game.json` 的 `render` 下，进入世界时读取一次，修改后需重进世界：

| 键 | 默认 | 说明 |
|---|---|---|
| `roomCutaway` | `true` | 室内剖切总开关 |
| `cutawayPerSquare` | `true` | 按格剖切；`false` 回退房间级 |
| `cutawayLockMs` | `750` | 每格剖切状态锁 |
| `cutawayTrimMode` | `"edges"` | 墙裁切方式：`edges` / `ratio` |
| `buildingOcclusion` | `true` | 室外建筑遮挡 |
| `occluderBuildingsMax` | `8` | 每帧最多遮挡建筑数 |
| `occluderLockMs` | `750` | 建筑阻挡进入/退出迟滞 |
| `windowPeek` | `true` | 窗外朝窗扒看 |
| `windowPeekRange` | `0.9` | 扒看距离（格） |
| `roofCull` | `true` | 屋顶按建筑归属隐藏 |
| `foliageFlatten` | `true` | 树/高草遮挡时压平、淡化 |
| `foliageAlpha` | `0.4` | 植被压平后的 alpha 上限 |
| `foliageKeepRatio` | `0.55` | 植被压平后保留高度比例 |
| `cutawayDebug` | `false` | 输出剖切重算候选/射线/耗时摘要 |

排障时可单独关闭 `roomCutaway`、`buildingOcclusion`、`windowPeek`、`roofCull` 或 `foliageFlatten` 回退；这些只影响客户端绘制，不改碰撞、存档或联机复制字段。

## 项目结构

```text
src/        游戏运行时、状态、服务器、网络、存档、ECS、AI 与 UI
assets/     地图、物品、配方、UI、音频与占位资源
config/     游戏主配置
mods/       示例 mod 与内容扩展
tests/      C++ 自动化测试
tools/      地图生成、数据处理与开发辅助脚本
docs/       架构、开发规范、内容格式与开发日志
```

构建产物写入 `build/`，运行时存档写入 `saves/`，两者默认不纳入版本控制。

## 文档

- [架构总览](docs/00-架构总览.md)
- [里程碑与任务清单](docs/01-里程碑与任务清单.md)
- [技术选型与开发规范](docs/02-技术选型与开发规范.md)
- [Mod 开发指南](docs/modding.md)
- [地图格式](docs/mapformat.md)
- [Tiled 地图导入](docs/tiled-import.md)
- [开发日志](docs/devlog.md)

## 开发状态

项目仍在持续开发中。核心生存、战斗、建造、联机、存档与内容扩展链路已经可运行，后续继续补充内容、表现、平衡与平台适配。

M34 潜行、投掷与陷阱的代码和自动回归已完成；15–30 分钟自然玩法流程、陷阱触发/火焰/背刺/读档和双客户端服务器重启仍待人工验收。

M36 室内可见性（按格剖切、方向墙裁切、窗外扒看、屋顶归属、室外建筑遮挡、树冠压平）自动化已完成：Release/Debug 全量 **1178/1178**、Python **127/127**、双客户端冒烟通过；室内重算 P95 **0.111ms**，500 人口村庄巡航 FPS 中位 **60**。1280 十张验收截图、1920 抽检与用户实机确认仍待人工。

### M36 已知限制

- 剖切/窗口/屋顶/植被均为客户端表现近似，不做逐像素遮挡；判定使用格、屏幕矩形、朝向/视线与画家序。
- 树冠压平只改本帧绘制矩形与 alpha，不影响碰撞、采集、燃烧或存档。
- `render.cutawayTrimMode=ratio` 是旧观感回退，不等价于方向性裁切。
- 双客户端只验证了连接、快照与各自渲染无报错；A 室内/B 室外同屏的人工观感仍待验收。

### M34 已知限制

- 陷阱只能通过建造模式放置，不支持可携带陷阱、PvP 陷阱或拆陷阱小游戏。
- 投掷物只有短时本地飞行表现，不复制远端逐帧投掷物；伤害、火焰、回收和陷阱状态由服务器权威结算。
- 旧尖刺陷阱/电网保持原有行为；新放置陷阱的局部扫描按 chunk 粗粒度，同 chunk 内仍会遍历实体，候选超限时尾部目标本 tick 不触发。

## 说明

本项目为独立开发项目。仓库中的代码、配置与文档用于本项目开发；引入第三方素材时，以素材自身的授权与使用条款为准。
