local M = {}

local offsets = {
    { 8, 0 },
    { -8, 0 },
    { 0, 8 },
    { 0, -8 },
    { 6, 6 },
    { -6, 6 },
    { 6, -6 },
    { -6, -6 },
    { 10, 2 },
    { -10, -2 }
}

function M.spawn(waveIndex, spawnIndex, config)
    local wave = config.waves[waveIndex]
    if wave == nil then
        return 0, "missing_wave"
    end
    local p = Player.GetPosition()
    if p == nil then
        return 0, "no_player"
    end
    local offset = offsets[((spawnIndex - 1) % #offsets) + 1]
    local variant = wave.variants[((spawnIndex - 1) % #wave.variants) + 1]
    local handle = Zombies.Spawn(p.x + offset[1], p.y + offset[2], 0,
                                 variant or "normal")
    if handle == 0 then
        return 0, "spawn_failed"
    end
    return handle, variant or "normal"
end

return M
