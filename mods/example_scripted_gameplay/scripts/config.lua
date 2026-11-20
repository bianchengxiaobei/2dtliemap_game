return {
    ticksPerSecond = 60,
    startDelaySeconds = 4,
    waveSeconds = 30,
    intermissionSeconds = 6,
    restartSeconds = 12,
    scanIntervalTicks = 30,
    healThreshold = 0.35,
    healCooldownTicks = 5 * 60,
    score = {
        kill = 10,
        comboStep = 2,
        comboCap = 10,
        waveClear = 50,
        craft = 15,
        build = 20
    },
    waves = {
        {
            count = 3,
            spawnIntervalSeconds = 3.0,
            variants = { "normal", "normal", "fast" }
        },
        {
            count = 4,
            spawnIntervalSeconds = 2.5,
            variants = { "normal", "fast", "normal", "strong" }
        },
        {
            count = 5,
            spawnIntervalSeconds = 2.0,
            variants = { "fast", "normal", "strong", "fast", "armored" }
        }
    }
}
