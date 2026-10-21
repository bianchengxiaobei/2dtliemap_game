-- Script UI 示例：与 Python 版共用 ui/panel.rml。
-- 打开/更新/关闭全部由脚本调用 UI API；C++ 不包含这个界面的逻辑。

local doc = "example_ui"

local function setup_ui()
    if not UI.OpenDocument(doc, "ui/panel.rml", false) then
        Log.Error("panel.rml 加载失败")
        return
    end
    UI.SetText(doc, "script-title", "Lua Script UI")
    UI.SetText(doc, "script-status", "document opened by Lua")
end

local function on_ui_event(e)
    if e.modId ~= "example_ui" then
        return
    end
    if e.document ~= doc then
        return
    end
    if e.element == "script-close" and e.type == "click" then
        UI.CloseDocument(doc, false)
    elseif e.element == "script-refresh" and e.type == "click" then
        local name = UI.GetValue(doc, "script-name")
        UI.SetText(doc, "script-status", "hello, " .. name)
    elseif e.element == "script-name" and e.type == "change" then
        UI.SetText(doc, "script-status", "input: " .. e.value)
    end
end

local function on_mod_reload(e)
    if e.modId == "example_ui" then
        setup_ui()
    end
end

Events.On("OnUiReady", setup_ui)
Events.On("OnUiEvent", on_ui_event)
Events.On("OnModReload", on_mod_reload)
