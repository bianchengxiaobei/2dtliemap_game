Log.Info("scripted gameplay loaded")

local config = require("config")
local director = require("wave.director")
local challenge = require("challenge")

challenge.setup(config, director)
challenge.bind_events()
challenge.resume_or_show()
