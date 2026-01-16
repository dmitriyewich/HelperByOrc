local imgui = require("mimgui")
local ffi = require("ffi")
local wm = require("windows.message")
local vk = require("vkeys")
local encoding = require("encoding")
local funcs = require("HelperByOrc.funcs")
encoding.default = "CP1251"
local u8 = encoding.UTF8

-- модули будут загружены в main()
local mimgui_funcs
local SMIHelp
local samp, Unwanted, myhooks, tags, binder, notepad, VIPandADchat, weapon_rp

-- === FontAwesome ===
local ok2, fa = pcall(require, "HelperByOrc.fAwesome6_solid")

-- === Интерфейсные переменные ===
local CONFIG_PATH = getWorkingDirectory() .. "\\HelperByOrc\\HelperByOrc.json"
local function normalizeKey(k)
	if k == vk.VK_LSHIFT or k == vk.VK_RSHIFT then
		return vk.VK_SHIFT
	end
	if k == vk.VK_LCONTROL or k == vk.VK_RCONTROL then
		return vk.VK_CONTROL
	end
	if k == vk.VK_LMENU or k == vk.VK_RMENU then
		return vk.VK_MENU
	end
	return k
end

local function isKeyboardKey(k)
	if k >= vk.VK_LBUTTON and k <= vk.VK_XBUTTON2 then
		return false
	end
	return k >= 0 and k <= 255
end

local function keysMatchCombo(current, combo)
	if #combo == 0 then
		return false
	end
	for i = 1, #combo do
		local target = combo[i]
		local found = false
		for j = 1, #current do
			if normalizeKey(current[j]) == normalizeKey(target) then
				found = true
				break
			end
		end
		if not found then
			return false
		end
	end
	return true
end

local function cloneKeys(list)
	local res = {}
	for _, k in ipairs(list or {}) do
		if type(k) == "number" then
			res[#res + 1] = normalizeKey(k)
		end
	end
	return res
end

local defaultOpenHotkey = { vk.VK_CONTROL, vk.VK_Z }
local CONFIG_DEFAULTS = { renderHotkeyWnd = false, openHotkey = cloneKeys(defaultOpenHotkey) }

local projectConfig = funcs and funcs.loadTableFromJson and funcs.loadTableFromJson(CONFIG_PATH, CONFIG_DEFAULTS)
	or CONFIG_DEFAULTS

if type(projectConfig.renderHotkeyWnd) ~= "boolean" then
	projectConfig.renderHotkeyWnd = false
end

local function normalizeHotkeyTable(tbl)
	local combo = {}
	if type(tbl) ~= "table" then
		return nil
	end
	for _, k in ipairs(tbl) do
		if isKeyboardKey(k) then
			local nk = normalizeKey(k)
			local dup = false
			for _, cur in ipairs(combo) do
				if cur == nk then
					dup = true
					break
				end
			end
			if not dup then
				combo[#combo + 1] = nk
			end
		end
	end
	if #combo == 0 then
		return nil
	end
	return combo
end

local openHotkey = normalizeHotkeyTable(projectConfig.openHotkey) or cloneKeys(defaultOpenHotkey)
projectConfig.openHotkey = cloneKeys(openHotkey)
local openHotkeyActive = false

local function isAnyProjectWindowOpen()
	if renderHotkeyWnd and renderHotkeyWnd[0] then
		return true
	end

	if imgui.GetFrameCount and imgui.GetFrameCount() > 0 and imgui.GetIO then
		local ok, io = pcall(imgui.GetIO)
		if ok and io and (io.WantCaptureKeyboard or io.WantTextInput or io.WantCaptureMouse) then
			return true
		end
	end

	return false
end

local renderHotkeyWnd = imgui.new.bool(projectConfig.renderHotkeyWnd)
local function saveProjectConfig()
	if funcs and funcs.saveTableToJson then
		funcs.saveTableToJson(projectConfig, CONFIG_PATH)
	end
end

local function setRenderHotkeyWnd(state)
	local normalized = not not state
	if renderHotkeyWnd[0] ~= normalized then
		renderHotkeyWnd[0] = normalized
		projectConfig.renderHotkeyWnd = normalized
		saveProjectConfig()
	end
end

local function setOpenHotkey(combo)
	local normalized = normalizeHotkeyTable(combo) or cloneKeys(defaultOpenHotkey)
	openHotkey = normalized
	projectConfig.openHotkey = cloneKeys(normalized)
	openHotkeyActive = false
	saveProjectConfig()
end

local function toggleRenderHotkeyWnd()
	setRenderHotkeyWnd(not renderHotkeyWnd[0])
end

saveProjectConfig()
local openHotkeyCapture = false
local openHotkeyDraft = {}
local pressedKeysSet = {}
local pressedKeysList = {}

local function rebuildPressedList()
	pressedKeysList = {}
	for k, v in pairs(pressedKeysSet) do
		if v then
			table.insert(pressedKeysList, k)
		end
	end
end

local function hotkeyToString(keys)
	local t = {}
	for _, k in ipairs(keys or {}) do
		t[#t + 1] = vk.id_to_name and vk.id_to_name(k) or tostring(k)
	end
	return #t > 0 and table.concat(t, " + ") or "[не назначено]"
end
local currentTab = 1 -- Индекс вкладки
local miscPage = 0 -- 0 - меню, >0 - страницы настроек
local mainPos = imgui.ImVec2(10, 10)
local mainSize -- will init on first frame
local sidebarCollapsed = imgui.new.bool(false)
local SIDEBAR_W_EXPANDED = 128
local SIDEBAR_W_COLLAPSED = 44
local LOGO_SZ_EXPANDED = 128
local LOGO_SZ_COLLAPSED = 44
local ImageButton_color = imgui.ImVec4(1, 1, 1, 1)
-- модули, загружаемые в main()

imgui.OnInitialize(function()
	if mimgui_funcs and mimgui_funcs.Standart then
		mimgui_funcs.Standart()
	end
	if ok2 and fa and fa.Init then
		fa.Init()
	end
end)

-- === Главное окно ===
imgui.OnFrame(function()
	return renderHotkeyWnd[0]
end, function()
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
		if
			imgui.ImageButton(
				mimgui_funcs.close_window,
				closeSize,
				_,
				_,
				1,
				imgui.ImVec4(0, 0, 0, 0),
				ImageButton_color
			)
		then
			setRenderHotkeyWnd(false)
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
			setRenderHotkeyWnd(false)
		end
	end

	local sidebarW = sidebarCollapsed[0] and SIDEBAR_W_COLLAPSED or SIDEBAR_W_EXPANDED
	local logoSz = sidebarCollapsed[0] and LOGO_SZ_COLLAPSED or LOGO_SZ_EXPANDED
	local toggleIcon = fa and (sidebarCollapsed[0] and fa.ARROW_RIGHT_FROM_LINE or fa.ARROW_LEFT_TO_LINE)
		or (sidebarCollapsed[0] and ">" or "<")
	local toggleSize = imgui.ImVec2(titleH - 6, titleH - 6)
	local togglePos = imgui.ImVec2(pmin.x + pad.x + sidebarW - (toggleSize.x / 2), pmin.y + pad.y + (titleH - toggleSize.y) / 2)
	imgui.SetCursorScreenPos(togglePos)
	if imgui.Button(toggleIcon, toggleSize) then
		sidebarCollapsed[0] = not sidebarCollapsed[0]
	end

	imgui.SetCursorPos(imgui.ImVec2(pad.x, pad.y + titleH))

	-- Левая панель: логотип + меню
	imgui.BeginGroup()
	if imgui.BeginChild("img##logo", imgui.ImVec2(sidebarW, logoSz), false) then
		if mimgui_funcs and mimgui_funcs.logo then
			local logoZoom = sidebarCollapsed[0] and 0.9 or 1.2
			mimgui_funcs.drawOrcLogoZoom(mimgui_funcs.logo, currentTab, imgui.ImVec2(logoSz, logoSz), logoZoom)
		else
			imgui.Text("HelperByOrc")
		end
		imgui.EndChild()
	end

	if imgui.BeginChild("menu##vertical", imgui.ImVec2(sidebarW, 0), false) then
		local menuItems
		if ok2 and fa then
			local labelHouse = sidebarCollapsed[0] and fa.HOUSE or (fa.HOUSE .. " Главная")
			local labelKeyboard = sidebarCollapsed[0] and fa.KEYBOARD or (fa.KEYBOARD .. " Биндер")
			local labelNewspaper = sidebarCollapsed[0] and fa.NEWSPAPER or (fa.NEWSPAPER .. " СМИ Хелпер")
			local labelBook = sidebarCollapsed[0] and fa.BOOK or (fa.BOOK .. " Блокнот")
			local labelCubes = sidebarCollapsed[0] and fa.CUBES or (fa.CUBES .. " Прочее")
			local labelGear = sidebarCollapsed[0] and fa.GEAR or (fa.GEAR .. " Настройки")
			menuItems = {
				{ labelHouse },
				{ labelKeyboard },
				{ labelNewspaper },
				{ labelBook },
				{ labelCubes },
				{ labelGear },
			}
		else
			menuItems = {
				{ sidebarCollapsed[0] and "H" or "Главная" },
				{ sidebarCollapsed[0] and "B" or "Биндер" },
				{ sidebarCollapsed[0] and "S" or "СМИ Хелпер" },
				{ sidebarCollapsed[0] and "N" or "Блокнот" },
				{ sidebarCollapsed[0] and "M" or "Прочее" },
				{ sidebarCollapsed[0] and "C" or "Настройки" },
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
			imgui.TextColored(
				imgui.ImVec4(0.85, 0.95, 1, 1),
				"СМИ Хелпер — настройки и шаблоны"
			)
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
					table.insert(items, { id = 1, name = "Переменные" })
				end
				if VIPandADchat and VIPandADchat.DrawSettingsInline then
					table.insert(items, { id = 2, name = "VIP/AD чат" })
				end
				if Unwanted and Unwanted.DrawWindowInline then
					table.insert(items, { id = 3, name = "Игнорируемые сообщения" })
				end
				if weapon_rp and weapon_rp.DrawSettingsInline then
					table.insert(items, { id = 4, name = "Оружие RP" })
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
					local bg = hovered and imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
						or imgui.GetStyle().Colors[imgui.Col.FrameBg]
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
				imgui.BeginChild("misc_header", imgui.ImVec2(0, 20), false)
				if imgui.Button(fa.ARROW_LEFT .. " Назад") then
					miscPage = 0
				end
				imgui.SameLine()
				imgui.Text("Переменные")
				imgui.EndChild()
				imgui.BeginChild("misc_body", imgui.ImVec2(0, 0), true)
				tags.DrawSettingsPage()
				imgui.EndChild()
			elseif miscPage == 2 and VIPandADchat and VIPandADchat.DrawSettingsInline then
				imgui.BeginChild("misc_header", imgui.ImVec2(0, 20), false)
				if imgui.Button(fa.ARROW_LEFT .. " Назад") then
					miscPage = 0
				end
				imgui.SameLine()
				imgui.Text("VIP/AD чат")
				imgui.EndChild()
				imgui.BeginChild("misc_body", imgui.ImVec2(0, 0), true)
				VIPandADchat.DrawSettingsInline()
				imgui.EndChild()
			elseif miscPage == 3 and Unwanted and Unwanted.DrawWindowInline then
				imgui.BeginChild("misc_header", imgui.ImVec2(0, 20), false)
				if imgui.Button(fa.ARROW_LEFT .. " Назад") then
					miscPage = 0
				end
				imgui.SameLine()
				imgui.Text("Игнорируемые сообщения")
				imgui.EndChild()
				imgui.BeginChild("misc_body", imgui.ImVec2(0, 0), true)
				Unwanted.DrawWindowInline()
				imgui.EndChild()
			elseif miscPage == 4 and weapon_rp and weapon_rp.DrawSettingsInline then
				imgui.BeginChild("misc_header", imgui.ImVec2(0, 20), false)
				if imgui.Button(fa.ARROW_LEFT .. " Назад") then
					miscPage = 0
				end
				imgui.SameLine()
				imgui.Text("Оружие RP")
				imgui.EndChild()
				imgui.BeginChild("misc_body", imgui.ImVec2(0, 0), true)
				weapon_rp.DrawSettingsInline()
				imgui.EndChild()
			end
		elseif currentTab == 6 then
			imgui.TextColored(imgui.ImVec4(0.8, 0.95, 1, 1), "Настройки")
			imgui.Separator()
			imgui.Text("Хоткей открытия главного окна:")
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.9, 0.9, 0.6, 1), hotkeyToString(openHotkey))
			imgui.Separator()
			if openHotkeyCapture then
				imgui.TextColored(
					imgui.ImVec4(0.8, 0.8, 1, 1),
					"Нажмите нужные клавиши, Enter — сохранить, Backspace — очистить, Esc — отмена"
				)
				imgui.Text("Текущая комбинация:")
				imgui.SameLine()
				imgui.TextColored(imgui.ImVec4(0.6, 1, 0.6, 1), hotkeyToString(openHotkeyDraft))
				if imgui.Button("Сохранить комбинацию") then
					if #openHotkeyDraft > 0 then
						setOpenHotkey(openHotkeyDraft)
					end
					openHotkeyDraft = {}
					openHotkeyCapture = false
				end
				imgui.SameLine()
				if imgui.Button("Отменить") then
					openHotkeyDraft = {}
					openHotkeyCapture = false
				end
			else
				if imgui.Button("Изменить комбинацию") then
					openHotkeyDraft = {}
					openHotkeyCapture = true
				end
				imgui.SameLine()
				if imgui.Button("Сбросить на Ctrl + Z") then
					setOpenHotkey(defaultOpenHotkey)
				end
				imgui.Text(
					"Комбинация используется для открытия/закрытия окна HelperByOrc."
				)
			end
		else
			imgui.TextColored(imgui.ImVec4(0.8, 0.8, 1, 1), "HelperByOrc")
			imgui.Separator()
			imgui.Text("Вкладка в разработке. Тут будет контент.")
		end
		imgui.EndChild()
	end
	imgui.End()
end)

-- === Глобальный хоткей для вызова главного окна ===
addEventHandler("onWindowMessage", function(msg, wparam)
	local isKeyDownMsg = msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN
	local isKeyUpMsg = msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP
	if not (isKeyDownMsg or isKeyUpMsg) then
		return
	end

	local keyCode = normalizeKey(wparam)
	if not isKeyboardKey(keyCode) then
		return
	end

	if openHotkeyCapture and isKeyDownMsg then
		if keyCode == vk.VK_ESCAPE then
			openHotkeyDraft = {}
			openHotkeyCapture = false
		elseif keyCode == vk.VK_RETURN or keyCode == vk.VK_NUMPADENTER then
			if #openHotkeyDraft > 0 then
				setOpenHotkey(openHotkeyDraft)
			end
			openHotkeyDraft = {}
			openHotkeyCapture = false
		elseif keyCode == vk.VK_BACK then
			openHotkeyDraft = {}
		else
			local nk = normalizeKey(keyCode)
			local dup = false
			for _, k in ipairs(openHotkeyDraft) do
				if k == nk then
					dup = true
					break
				end
			end
			if not dup then
				table.insert(openHotkeyDraft, nk)
			end
		end

		if type(consumeWindowMessage) == "function" then
			consumeWindowMessage(true, true)
		end

		return
	end

	if keyCode == vk.VK_ESCAPE then
		local anyMimguiOpen = isAnyProjectWindowOpen()

		if anyMimguiOpen and isKeyDownMsg and type(consumeWindowMessage) == "function" then
			consumeWindowMessage(true, false)
		end

		if renderHotkeyWnd[0] and isKeyUpMsg then
			setRenderHotkeyWnd(false)
			openHotkeyActive = false
		end

		if anyMimguiOpen then
			return
		end
	end

	pressedKeysSet[keyCode] = isKeyDownMsg
	rebuildPressedList()

	local comboNow = keysMatchCombo(pressedKeysList, openHotkey)
	if comboNow and not openHotkeyActive then
		toggleRenderHotkeyWnd()
		openHotkeyActive = true
	elseif not comboNow and openHotkeyActive then
		openHotkeyActive = false
	end
end)

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
	weapon_rp = modules.weapon_rp

	if weapon_rp and weapon_rp.start then
		weapon_rp.start()
	end

	if myhooks and myhooks.init then
		myhooks.init()
	end

	-- Запуск потоков для работы интерфейса и тегов
	lua_thread.create(function()
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
	end)

	lua_thread.create(function()
		if tags and tags.RenderTagsWindow then
			while true do
				tags.RenderTagsWindow()
				wait(0)
			end
		end
	end)

	lua_thread.create(function()
		if VIPandADchat then
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
