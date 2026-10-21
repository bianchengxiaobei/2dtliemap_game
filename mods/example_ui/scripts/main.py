# Script UI 示例：与 Lua 版共用 ui/panel.rml。
# 打开/更新/关闭全部由脚本调用 UI API；C++ 不包含这个界面的逻辑。

DOC = "example_ui"


def setup_ui():
    if not UI.OpenDocument(DOC, "ui/panel.rml", False):
        Log.Error("panel.rml 加载失败")
        return
    UI.SetText(DOC, "script-title", "Python Script UI")
    UI.SetText(DOC, "script-status", "document opened by Python")


def on_ui_event(e):
    if e["modId"] != "example_ui" or e["document"] != DOC:
        return
    if e["element"] == "script-close" and e["type"] == "click":
        UI.CloseDocument(DOC, False)
    elif e["element"] == "script-refresh" and e["type"] == "click":
        name = UI.GetValue(DOC, "script-name")
        UI.SetText(DOC, "script-status", "hello, %s" % name)
    elif e["element"] == "script-name" and e["type"] == "change":
        UI.SetText(DOC, "script-status", "input: %s" % e["value"])


def on_mod_reload(e):
    if e["modId"] == "example_ui":
        setup_ui()


Events.On("OnUiReady", setup_ui)
Events.On("OnUiEvent", on_ui_event)
Events.On("OnModReload", on_mod_reload)
