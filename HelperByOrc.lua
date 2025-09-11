local imgui = require "mimgui"
local ffi = require "ffi"
local wm = require("windows.message")
local vk = require "vkeys"
local encoding = require "encoding"
encoding.default = "CP1251"
local u8 = encoding.UTF8

-- модули будут загружены в main()
local mimgui_funcs
local SMIHelp
local samp, Unwanted, myhooks, tags, binder, notepad, VIPandADchat

-- === FontAwesome ===
local ok2, fa = pcall(require, "HelperByOrc.fAwesome6_solid")

-- === Интерфейсные переменные ===
local renderHotkeyWnd = imgui.new.bool(false)
local currentTab = 1 -- Индекс вкладки
local miscPage = 0 -- 0 - меню, >0 - страницы настроек
local mainPos = imgui.ImVec2(10, 10)
local mainSize -- will init on first frame
local ImageButton_color = imgui.ImVec4(1, 1, 1, 1)
-- модули, загружаемые в main()

imgui.OnInitialize(
	function()
		if mimgui_funcs and mimgui_funcs.Standart then
			mimgui_funcs.Standart()
		end
		if ok2 and fa and fa.Init then
			fa.Init()
		end
	end
)

-- === Главное окно ===
imgui.OnFrame(
	function()
		return renderHotkeyWnd[0]
	end,
	function()
		local io = imgui.GetIO()
		if not mainSize then
			mainSize = imgui.ImVec2(930, 500)
		end
		if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
			mainPos, mainSize = mimgui_funcs.clampWindowToScreen(mainPos, mainSize, 5)
		end
		imgui.SetNextWindowPos(mainPos, imgui.Cond.Always)
		imgui.SetNextWindowSize(mainSize, imgui.Cond.Always)
		imgui.Begin(
			"HelperByOrc",
			renderHotkeyWnd,
			imgui.WindowFlags.NoMove + imgui.WindowFlags.NoTitleBar + imgui.WindowFlags.NoCollapse
		)
		mainPos = imgui.GetWindowPos()
		mainSize = imgui.GetWindowSize()

		-- Custom draggable title bar
		local style = imgui.GetStyle()
		local pad = style.WindowPadding
		local titleH = imgui.GetFontSize() + style.FramePadding.y * 2
		local winPos = imgui.GetWindowPos()
		local winSize = imgui.GetWindowSize()

		imgui.SetCursorPos(imgui.ImVec2(0, 0))
		imgui.InvisibleButton("##titlebar", imgui.ImVec2(winSize.x, titleH))
		imgui.SetItemAllowOverlap()
		local pmin = imgui.GetItemRectMin()
		local pmax = imgui.GetItemRectMax()
		local dl = imgui.GetWindowDrawList()
		local col = style.Colors[imgui.Col.TitleBg]
		local colActive = style.Colors[imgui.Col.TitleBgActive]
		dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(imgui.IsItemActive() and colActive or col))
		dl:AddText(
			imgui.ImVec2(pmin.x + pad.x, pmin.y + style.FramePadding.y),
			imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]),
			"HelperByOrc"
		)
		if imgui.IsItemActive() and imgui.IsMouseDragging(0) then
			local delta = io.MouseDelta
			mainPos = imgui.ImVec2(mainPos.x + delta.x, mainPos.y + delta.y)
		end

		-- Draw close button
		local closeSize = imgui.ImVec2(titleH - 6, titleH - 6)
		local closePos = imgui.ImVec2(pmax.x - pad.x - closeSize.x, pmin.y + (titleH - closeSize.y) / 2)
		imgui.SetCursorScreenPos(closePos)
		if mimgui_funcs and mimgui_funcs.close_window then
			imgui.PushStyleVarFloat(imgui.StyleVar.FrameBorderSize, 0.0)
			imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.00, 0.00, 0.00, 0.0))
			imgui.PushStyleColor(imgui.Col.ButtonHovered, imgui.ImVec4(0.0, 0.0, 0.0, 0.0))
			imgui.PushStyleColor(imgui.Col.ButtonActive, imgui.ImVec4(0.76, 0.76, 0.76, 1.00))
			if imgui.ImageButton(mimgui_funcs.close_window, closeSize, _, _, 1, imgui.ImVec4(0, 0, 0, 0), ImageButton_color) then
				renderHotkeyWnd[0] = false
			end
			if imgui.IsItemHovered() then
				ImageButton_color = imgui.ImVec4(1, 1, 1, 0.5)
			else
				ImageButton_color = imgui.ImVec4(1, 1, 1, 1)
			end
			imgui.PopStyleColor(3)
			imgui.PopStyleVar()
		else
			if imgui.Button("X", closeSize) then
				renderHotkeyWnd[0] = false
			end
		end

		imgui.SetCursorPos(imgui.ImVec2(pad.x, pad.y + titleH))

		-- Левая панель: логотип + меню
		imgui.BeginGroup()
		if imgui.BeginChild("img##logo", imgui.ImVec2(128, 128), false) then
			if mimgui_funcs and mimgui_funcs.logo then
				mimgui_funcs.drawOrcLogoZoom(mimgui_funcs.logo, currentTab, imgui.ImVec2(128, 128), 1.2)
			else
				imgui.Text("HelperByOrc")
			end
			imgui.EndChild()
		end

		if imgui.BeginChild("menu##vertical", imgui.ImVec2(128, 0), false) then
			local menuItems
			if ok2 and fa then
				menuItems = {
					{fa.HOUSE .. " Главная"},
					{fa.KEYBOARD .. " Биндер"},
					{fa.NEWSPAPER .. " СМИ Хелпер"},
					{fa.BOOK .. " Блокнот"},
					{fa.CUBES .. " Прочее"},
					{fa.GEAR .. " Настройки"}
				}
			else
				menuItems = {
					{"Главная"},
					{"Биндер"},
					{"СМИ Хелпер"},
					{"Блокнот"},
					{"Прочее"},
					{"Настройки"}
				}
			end
			if mimgui_funcs and mimgui_funcs.customVerticalMenu then
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
		if imgui.BeginChild("main##content", imgui.ImVec2(0, 0), true) then
			if currentTab == 2 and binder then
				binder.DrawBinder()
			elseif currentTab == 3 and SMIHelp then
				imgui.TextColored(imgui.ImVec4(0.85, 0.95, 1, 1), "СМИ Хелпер — настройки и шаблоны")
				imgui.Separator()
				if imgui.Button("Открыть редактор объявлений") then
					SMIHelp.OpenEditPreview()
				end
				imgui.Separator()
				SMIHelp.DrawSettingsUI()
			elseif currentTab == 4 and notepad and notepad.drawNotepadPanel then
				notepad.drawNotepadPanel()
			elseif currentTab == 5 then
				if miscPage == 0 then
					imgui.TextColored(imgui.ImVec4(0.8, 0.8, 1, 1), "Прочее")
					imgui.Separator()
					local items = {}
					if tags and tags.DrawSettingsPage then
						table.insert(items, {id = 1, name = "Переменные"})
					end
					if VIPandADchat and VIPandADchat.DrawSettingsInline then
						table.insert(items, {id = 2, name = "VIP/AD чат"})
					end
					if Unwanted and Unwanted.DrawWindowInline then
						table.insert(items, {id = 3, name = "Игнорируемые сообщения"})
					end

					local avail = imgui.GetContentRegionAvail().x
					local cardW, cardH = 200, 60
					local spacing = 16
					local cols = math.max(1, math.floor((avail + spacing) / (cardW + spacing)))
					local x0 = imgui.GetCursorScreenPos().x
					local y0 = imgui.GetCursorScreenPos().y
					for i, it in ipairs(items) do
						local x = x0 + ((i - 1) % cols) * (cardW + spacing)
						local y = y0 + math.floor((i - 1) / cols) * (cardH + spacing)
						imgui.SetCursorScreenPos(imgui.ImVec2(x, y))
						imgui.BeginGroup()
						local pmin = imgui.GetCursorScreenPos()
						local pmax = imgui.ImVec2(pmin.x + cardW, pmin.y + cardH)
						local dl = imgui.GetWindowDrawList()
						local hovered = imgui.IsMouseHoveringRect(pmin, pmax)
						local bg =
							hovered and imgui.GetStyle().Colors[imgui.Col.FrameBgHovered] or imgui.GetStyle().Colors[imgui.Col.FrameBg]
						dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(bg), 8)
						dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Border]), 8, 2)
						imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + 10, pmin.y + 22))
						imgui.Text(it.name)
						if hovered and imgui.IsMouseClicked(0) then
							miscPage = it.id
						end
						imgui.EndGroup()
					end
				elseif miscPage == 1 and tags and tags.DrawSettingsPage then
					imgui.BeginChild("misc_header", imgui.ImVec2(0, 40), false)
					if imgui.Button(fa.ARROW_LEFT .. " Назад") then
						miscPage = 0
					end
					imgui.SameLine()
					imgui.Text("Переменные")
					imgui.EndChild()
					imgui.BeginChild("misc_body", imgui.ImVec2(0, -42), true)
					tags.DrawSettingsPage()
					imgui.EndChild()
				elseif miscPage == 2 and VIPandADchat and VIPandADchat.DrawSettingsInline then
					imgui.BeginChild("misc_header", imgui.ImVec2(0, 40), false)
					if imgui.Button(fa.ARROW_LEFT .. " Назад") then
						miscPage = 0
					end
					imgui.SameLine()
					imgui.Text("VIP/AD чат")
					imgui.EndChild()
					imgui.BeginChild("misc_body", imgui.ImVec2(0, -42), true)
					VIPandADchat.DrawSettingsInline()
					imgui.EndChild()
				elseif miscPage == 3 and Unwanted and Unwanted.DrawWindowInline then
					imgui.BeginChild("misc_header", imgui.ImVec2(0, 40), false)
					if imgui.Button(fa.ARROW_LEFT .. " Назад") then
						miscPage = 0
					end
					imgui.SameLine()
					imgui.Text("Игнорируемые сообщения")
					imgui.EndChild()
					imgui.BeginChild("misc_body", imgui.ImVec2(0, -42), true)
					Unwanted.DrawWindowInline()
					imgui.EndChild()
				end
			else
				imgui.TextColored(imgui.ImVec4(0.8, 0.8, 1, 1), "HelperByOrc")
				imgui.Separator()
				imgui.Text("Вкладка в разработке. Тут будет контент.")
			end
			imgui.EndChild()
		end
		imgui.End()
	end
)

-- === Глобальный хоткей (X) для вызова главного окна ===
addEventHandler(
	"onWindowMessage",
	function(msg, wparam, lparam)
		if msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN then
			if wparam == vk.VK_Z and isKeyDown(vk.VK_CONTROL) then
				renderHotkeyWnd[0] = not renderHotkeyWnd[0]
			end
		end
	end
)

-- === onTick: обработка быстрого меню биндер-модуля ===
function main()
	while not isSampAvailable() do
		wait(1000)
	end

	local modules = require("HelperByOrc.init")
	if modules.loadHeavyModules then
		modules.loadHeavyModules()
	end

	mimgui_funcs = modules.mimgui_funcs
	SMIHelp = modules.SMIHelp
	samp = modules.samp
	Unwanted = modules.unwanted
	myhooks = modules.my_hooks
	tags = modules.tags
	binder = modules.binder
	notepad = modules.notepad
	VIPandADchat = modules.VIPandADchat

	if myhooks and myhooks.init then
		myhooks.init()
	end

	-- Запуск потоков для работы интерфейса и тегов
	lua_thread.create(
		function()
			if binder and binder.OnTick then
				binder.loadHotkeys()
				while true do
					if wasKeyPressed(vk.VK_XBUTTON1) then
						if mimgui_funcs and mimgui_funcs.resetIO then
							mimgui_funcs.resetIO()
						end
					end
					binder.OnTick()
					wait(0)
				end
			end
		end
	)

	lua_thread.create(
		function()
			if tags and tags.RenderTagsWindow then
				while true do
					tags.RenderTagsWindow()
					wait(0)
				end
			end
		end
	)

	lua_thread.create(
		function()
			if VIPandADchat then
				while true do
					VIPandADchat.showFeedWindow[0] = true
					wait(0)
				end
			end
		end
	)

	wait(-1)
end

function onScriptTerminate(script, quit)
	if script == thisScript() and not quit then
	-- myhooks.deinit()
	end
end
