# Example Scripted Gameplay

M35-F5-T1 的端到端 Lua 示例：用 `OnTick` 导演三波尸潮，用 `OnZombieDead(killer)` 计分，用 `Mod.Data` + `Json` 保存最佳成绩和当前 run 状态，用 `Hud` 字段显示实时面板。

## 玩法

- 进入世界后自动开始，4 秒准备；共 3 波，每波 30 秒，波次间隙 6 秒。
- 第 1/2/3 波分别生成 3/4/5 只，混编 `normal/fast/strong/armored`。
- 击杀 +10 分，连杀追加最多 +10；清场 +50；制作/建造事件也有加分。
- 完成或阵亡后显示结果，12 秒后自动重开；最佳分数跨读档/热重载保留。
- 波次开始/结束播放 2D 提示音，击杀按世界位置播放 3D 命中音（只使用现有 clip，设备关闭时静默）。
- 左上角显示脚本面板（波次/倒计时/本局/最佳/目标），与右上角内置 HUD 并存；点击“隐藏”关闭面板后挑战继续运行，HUD 字段仍保留。
- 有 3 木材时会在玩家旁边尝试放一扇木门，结束时切换/维修/拆除；有 2 破布时每波清场尝试制作简易绷带；低血时尝试使用背包物品。这些动作失败不会中断挑战。

## 文件

- `scripts/main.lua`：入口，加载配置/导演/挑战并绑定事件。
- `scripts/config.lua`：波次、计时、分值、治疗阈值。
- `scripts/challenge.lua`：状态机、计分、HUD、`Mod.Data` 持久化、热重载恢复。
- `scripts/wave/director.lua`：按玩家位置和波次配置生成僵尸。
- `ui/challenge.rml`、`ui/challenge.rcss`：脚本 UI 面板，`OnUiReady` 打开，非模态，`hud` 组。

## 覆盖的 API

`Entities.Query/Get/GetContainer`、`World.GetWeather`、`Player.GetNeeds/GetInventory/GetProgression`、`Player.EquipItem/UseItem`、`Craft.Start`、`Build.Place/Demolish/Repair/ToggleDoor`、`Player.GiveXp`、`Mod.Data`、`Json.Encode/Decode`、`Hud.RegisterField/SetField`、`UI.OpenDocument/SetText/SetProperty/CloseDocument`、`Audio.Play2D/Play3D`、Lua `require` 多文件。

事件：`OnWorldStart/OnWorldExit/OnTick/OnDamage/OnZombieDead/OnCraftFinished/OnBuildPlaced/OnPlayerDeath`。

## 验收记录

M35-F5-T5（2026-11-20）在 Release 1280×720 真实窗口完成一局：准备/第 1 波截图、真实鼠标点击“隐藏”、三波清场、得分 360、最佳成绩保留、面板关闭后自动重开第二局。证据：`build/m35_f5_t5_smoke/01-prep-1280.png` 至 `05-restart-1280.png`、`world.out.log`。同时修复了 core_ui 全屏 HUD 拦截脚本面板点击、点击 UI 同时触发世界点击移动、面板子元素按 inline 排版重叠三个集成问题。

## 限制

- 仅 Lua 后端；Python 仍是单入口 `main.py`，不使用 `require`。
- 单机示例；多人模式只在 HUD 显示“仅单机”。
- 脚本面板与 HUD 字段同时存在；音频使用现有 clip，自动验收确认设备初始化与调用路径，未做人工听感确认。
