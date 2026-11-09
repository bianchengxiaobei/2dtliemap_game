Log.Info("example_content loaded")
Hud.RegisterField("depot_status", "补给站")
Hud.SetField("depot_status", "就绪", "ok")


def on_game_start():
    Hud.SetField("depot_status", "就绪", "ok")


Events.On("OnGameStart", on_game_start)
