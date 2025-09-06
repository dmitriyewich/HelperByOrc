local imgui = require 'mimgui'
local ffi = require 'ffi'
local wm = require('windows.message')
local vkeys = require 'vkeys'
local vk = require 'vkeys'
local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8
local samp = require 'samp'
local bit = require 'bit'
local memory = require 'memory'
local sampev = require 'samp.events'
local ok_effil, effil = pcall(require, 'effil')
local ok_https, https = pcall(require, 'ssl.https')

local deps = {
    imgui = imgui,
    ffi = ffi,
    wm = wm,
    vkeys = vkeys,
    vk = vk,
    encoding = encoding,
    u8 = u8,
    samp = samp,
    bit = bit,
    memory = memory,
    sampev = sampev,
    effil = ok_effil and effil or nil,
    https = ok_https and https or nil
}

-- === FontAwesome ===
local ok2, fa = pcall(require, 'HelperByOrc.fAwesome6_solid')

-- === Интерфейсные переменные ===
local renderHotkeyWnd = imgui.new.bool(false)
local currentTab = 1 -- Индекс вкладки

imgui.OnInitialize(function()
    imgui.GetIO().IniFilename = nil
    if ok1 and mimgui_funcs and mimgui_funcs.Standart then mimgui_funcs.Standart() end
    if ok2 and fa and fa.Init then fa.Init() end
    if oksmihelp and SMIHelp and SMIHelp.Standart then SMIHelp.Standart() end
end)

-- === Главное окно ===
imgui.OnFrame(
    function() return renderHotkeyWnd[0] end,
    function()
        imgui.SetNextWindowSize(imgui.ImVec2(970, 560), imgui.Cond.FirstUseEver)
        imgui.Begin('HelperByOrc', renderHotkeyWnd)

        -- Левая панель: логотип + меню
        imgui.BeginGroup()
        if imgui.BeginChild('img##logo', imgui.ImVec2(128, 128), false) then
            if ok1 and mimgui_funcs and mimgui_funcs.logo then
                mimgui_funcs.drawOrcLogoZoom(mimgui_funcs.logo, currentTab, imgui.ImVec2(128, 128), 1.2)
            else
                imgui.Text("HelperByOrc")
            end
            imgui.EndChild()
        end

        if imgui.BeginChild('menu##vertical', imgui.ImVec2(128, 0), false) then
            local menuItems
            if ok2 and fa then
                menuItems = {
                    {fa.HOUSE.." Главная"},
                    {fa.KEYBOARD.." Биндер"},
                    {fa.NEWSPAPER.." СМИ Хелпер"},
                    {fa.BOOK.." Блокнот"},
                    {fa.CUBES.." Прочее"},
                    {fa.GEAR.." Настройки"},
                }
            else
                menuItems = {
                    {"Главная"},
                    {"Биндер"},
                    {"СМИ Хелпер"},
                    {"Блокнот"},
                    {"Прочее"},
                    {"Настройки"},
                }
            end
            if ok1 and mimgui_funcs and mimgui_funcs.customVerticalMenu then
                currentTab = mimgui_funcs.customVerticalMenu(menuItems, currentTab)
            else
                for i, v in ipairs(menuItems) do
                    if imgui.Selectable(v[1], currentTab == i) then
                        currentTab = i
                    end
                end
            end
            imgui.EndChild()
        end
        imgui.EndGroup()

        -- Правая основная часть
        imgui.SameLine()
        if imgui.BeginChild('main##content', imgui.ImVec2(0, 0), true) then
            if currentTab == 2 and okbinder and binder then
                binder.DrawBinder()
            elseif currentTab == 3 and oksmihelp and SMIHelp then
                imgui.TextColored(imgui.ImVec4(0.85,0.95,1,1), "СМИ Хелпер — настройки и шаблоны")
                imgui.Separator()
                SMIHelp.DrawSettingsUI()
            elseif currentTab == 4 and oknotepad and notepad and notepad.drawNotepadPanel then
                notepad.drawNotepadPanel()
            elseif currentTab == 5 then
                imgui.TextColored(imgui.ImVec4(0.8,0.8,1,1), "Прочее")
                imgui.Separator()
                if ltags and tags and tags.showTagsWindow then
                    if imgui.Button("Переменные") then
                        tags.showTagsWindow[0] = true
                    end
                end
				imgui.Separator()
				if okvipad and VIPandADchat then
					if imgui.Button("VIP/AD чат — настройки") then
						VIPandADchat.showSettingsWindow[0] = true
					end
				end
				if okvipad and VIPandADchat and VIPandADchat.showSettingsWindow and VIPandADchat.DrawSettingsWindow then
					VIPandADchat.DrawSettingsWindow()
				end

				imgui.Separator()

				-- Игнор-список
				if okunw and Unwanted then
					if imgui.Button("Игнорируемые сообщения — настройки") then
						Unwanted.showWindow[0] = true
					end
					-- рисуем окно, если открыто
					Unwanted.DrawWindow()
				end
            else
                imgui.TextColored(imgui.ImVec4(0.8,0.8,1,1), "HelperByOrc")
                imgui.Separator()
                imgui.Text("Вкладка в разработке. Тут будет контент.")
            end
            imgui.EndChild()
        end
        imgui.End()
    end
)

-- === Глобальный хоткей (X) для вызова главного окна ===
addEventHandler('onWindowMessage', function(msg, wparam, lparam)
    if msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN then
        if wparam == vk.VK_Z and isKeyDown(vk.VK_CONTROL) then
            renderHotkeyWnd[0] = not renderHotkeyWnd[0]
        end
    end
end)

-- === onTick: обработка быстрого меню биндер-модуля ===
function main()
	while not isSampAvailable() do wait(1000) end
	
	-- === Модули проекта ===
	lsamp, samp_module = pcall(function() return require('HelperByOrc.samp')(deps) end)
	okunw, Unwanted = pcall(function() return require('HelperByOrc.unwanted')(deps) end)
	okmyhooks, myhooks = pcall(function() return require('HelperByOrc.my_hooks')(deps) end)
	lfuncs, funcs = pcall(function() return require('HelperByOrc.funcs')(deps) end)
	ltags, tags = pcall(function() return require('HelperByOrc.tags')(deps) end)
	-- print(ltags, tags)

	ok1, mimgui_funcs = pcall(function() return require('HelperByOrc.mimgui_funcs')(deps) end)
	okbinder, binder = pcall(function() return require('HelperByOrc.binder')(deps) end)
	oknotepad, notepad = pcall(function() return require('HelperByOrc.notepad')(deps) end)
	oksmihelp, SMIHelp = pcall(function() return require('HelperByOrc.SMIHelp')(deps) end)
	okvipad, VIPandADchat = pcall(function() return require('HelperByOrc.VIPandADchat')(deps) end)
	-- print(oksmihelp, SMIHelp)
	print(okbinder, binder)
	-- print(okvipad, VIPandADchat)
	-- в
	if okmyhooks then myhooks.init() end

    -- Запуск потоков для работы интерфейса и тегов
    lua_thread.create(function()
        if okbinder and binder and binder.OnTick then
            binder.loadHotkeys()
            while true do
                if wasKeyPressed(vk.VK_XBUTTON1) then
                    if ok1 and mimgui_funcs and mimgui_funcs.resetIO then
                        mimgui_funcs.resetIO()
                    end
                end
                binder.OnTick()
                wait(0)
            end
        end
    end)

    lua_thread.create(function()
        if ltags and tags and tags.RenderTagsWindow then
            while true do
                tags.RenderTagsWindow()
                wait(0)
            end
        end
    end)

    lua_thread.create(function()
        if okvipad and VIPandADchat then
            while true do
                VIPandADchat.showFeedWindow[0] = true
                wait(0)
            end
        end
    end)

    wait(-1)
end

function onScriptTerminate(script, quit)
	if script == thisScript() and not quit then
		-- myhooks.deinit()
	end
end