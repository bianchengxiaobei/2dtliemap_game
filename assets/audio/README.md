# 音频占位资源（M11）

当前 wav 均为程序合成的短占位音，随仓库分发，后续可替换为自制/授权素材：

- `footstep_grass.wav` / `footstep_dirt.wav` / `footstep_wood.wav`：按地面材质脚步。
- `ambient_day.wav` / `ambient_night.wav`：昼夜环境循环（6s）。
- `music_calm.wav`：Music 音量组的低音量循环（8s）。
- `zombie_growl.wav`：僵尸低吼。
- `gunshot.wav` / `impact.wav`：枪声/撞击。
- `vehicle_engine.wav`：载具引擎 2s 循环占位音，音量/音高随速度变化。
- `vehicle_ram.wav`：载具撞击僵尸的短促撞击占位音。
- `vehicle_horn.wav`：载具双音喇叭占位音，单机/联机鸣笛与引怪使用。
- `vehicle_hit.wav`：僵尸攻击载具的短促金属受击占位音，单机播放并由服务器广播给客户端。
- `ui_click.wav` / `pickup.wav`：UI 点击/拾取。

命名约定：`SoundEvent.clip` / `AudioEngine::Play*` 传逻辑名，不带 `.wav`；
音频根目录由 `config/game.json` 的 `audio.root` 指定（默认 `assets/audio`）。
