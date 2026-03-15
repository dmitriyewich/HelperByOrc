local imgui = require("mimgui")
local ffi = require("ffi")
local wm = require("windows.message")
local vk = require("vkeys")
local encoding = require("encoding")
local paths = require("HelperByOrc.paths")
local funcs = require("HelperByOrc.funcs")
local mimgui_funcs = require("HelperByOrc.mimgui_funcs")
local HotkeyManager = require("HelperByOrc.hotkey_manager")
encoding.default = "CP1251"

local app = {}

local imgui_text_safe = mimgui_funcs.imgui_text_safe
local imgui_text_colored_safe = mimgui_funcs.imgui_text_colored_safe

-- модули будут загружены в main()
local SMIHelp
local Unwanted, myhooks, tags, binder, notepad, VIPandADchat, weapon_rp, toasts, correct
local projectConfig
local config_manager_ref
local event_bus_ref
local runtime_state = {
	active = false,
	binder_thread = nil,
	tags_thread = nil,
}
local modules_ref
local reload_in_progress = false

local function stopRuntimeThread(thread_handle)
	if not thread_handle then
		return
	end
	if type(thread_handle.status) ~= "function" or type(thread_handle.terminate) ~= "function" then
		return
	end

	local ok_status, status = pcall(thread_handle.status, thread_handle)
	if not ok_status then
		return
	end
	if status == "dead" then
		return
	end

	pcall(thread_handle.terminate, thread_handle)
end

local function bindModules(modules)
	if type(modules) ~= "table" then
		return
	end

	if type(modules.mimgui_funcs) == "table" then
		mimgui_funcs = modules.mimgui_funcs
		if type(mimgui_funcs.imgui_text_safe) == "function" then
			imgui_text_safe = mimgui_funcs.imgui_text_safe
		end
		if type(mimgui_funcs.imgui_text_colored_safe) == "function" then
			imgui_text_colored_safe = mimgui_funcs.imgui_text_colored_safe
		end
	end

	SMIHelp = modules.SMIHelp or SMIHelp
	Unwanted = modules.unwanted or Unwanted
	myhooks = modules.my_hooks or myhooks
	tags = modules.tags or tags
	binder = modules.binder or binder
	notepad = modules.notepad or notepad
	VIPandADchat = modules.VIPandADchat or VIPandADchat
	weapon_rp = modules.weapon_rp or weapon_rp
	toasts = modules.toasts or toasts
	correct = modules.correct or correct
	if correct and type(correct.setEnabled) == "function" then
		correct.setEnabled(projectConfig.correctEnabled ~= false)
	end
end

function app.attachModules(modules)
	modules_ref = modules
	bindModules(modules)
	config_manager_ref = modules.config_manager
	event_bus_ref = modules.event_bus
	if event_bus_ref then
		event_bus_ref.offByOwner("main_app")
	end
	if config_manager_ref then
		local data = config_manager_ref.register("main", {
			path = CONFIG_PATH_REL,
			defaults = CONFIG_DEFAULTS,
			normalize = function(loaded)
				if type(loaded.renderHotkeyWnd) ~= "boolean" then
					loaded.renderHotkeyWnd = false
				end
				if type(loaded.correctEnabled) ~= "boolean" then
					loaded.correctEnabled = true
				end
				loaded.openHotkey = normalizeHotkeyTable(loaded.openHotkey) or cloneKeys(defaultOpenHotkey)
				return loaded
			end,
		})
		-- Sync live data back to projectConfig
		for k, v in pairs(data) do
			projectConfig[k] = v
		end
	end
end

-- === FontAwesome ===
local ok2, fa = pcall(require, "fAwesome7")

-- === Интерфейсные переменные ===
local CONFIG_PATH_REL = "HelperByOrc.json"
local CONFIG_PATH = paths.dataPath(CONFIG_PATH_REL)
local hotkey_helpers = funcs.getHotkeyHelpers(vk, "[не назначено]")
local normalizeKey = hotkey_helpers.normalizeKey
local isKeyboardKey = hotkey_helpers.isKeyboardKey
local keysMatchCombo = hotkey_helpers.keysMatchCombo
local normalizeHotkeyTable = hotkey_helpers.normalizeHotkeyTable
local hotkeyToString = hotkey_helpers.hotkeyToString

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
local CONFIG_DEFAULTS = { renderHotkeyWnd = false, openHotkey = cloneKeys(defaultOpenHotkey), correctEnabled = true }

projectConfig = funcs.loadTableFromJson(CONFIG_PATH_REL, CONFIG_DEFAULTS)
local configSnapshot = funcs.deepCopyTable and funcs.deepCopyTable(projectConfig) or nil

if type(projectConfig.renderHotkeyWnd) ~= "boolean" then
	projectConfig.renderHotkeyWnd = false
end

local openHotkey = normalizeHotkeyTable(projectConfig.openHotkey) or cloneKeys(defaultOpenHotkey)
projectConfig.openHotkey = cloneKeys(openHotkey)
local openHotkeyActive = false

if type(projectConfig.correctEnabled) ~= "boolean" then
	projectConfig.correctEnabled = true
end

local imgui_ready = false
local renderHotkeyWnd

local function isAnyProjectWindowOpen()
	if renderHotkeyWnd and renderHotkeyWnd[0] then
		return true
	end

	if imgui_ready then
		local io = imgui.GetIO()
		if io.WantCaptureKeyboard or io.WantTextInput or io.WantCaptureMouse then
			return true
		end
	end

	return false
end

renderHotkeyWnd = imgui.new.bool(projectConfig.renderHotkeyWnd)
local function saveProjectConfig()
	if config_manager_ref then
		config_manager_ref.markDirty("main")
	else
		funcs.saveTableToJson(projectConfig, CONFIG_PATH_REL)
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

if configSnapshot and funcs.tablesShallowEqual then
	local dirty = not funcs.tablesShallowEqual(projectConfig, configSnapshot)
	if dirty then
		saveProjectConfig()
	end
elseif not configSnapshot then
	-- deepCopyTable недоступен, fallback: сохраняем как раньше
	saveProjectConfig()
end
configSnapshot = nil
local correctEnabledBool = imgui.new.bool(projectConfig.correctEnabled ~= false)
local profileSelectIndex = imgui.new.int(0)
local newProfileNameBuf = imgui.new.char[64]()
local profileNames = {}
local profileNamesFfi = nil
local profileStatusText = ""
local profileStatusColor = imgui.ImVec4(0.8, 0.95, 1, 1)
local profileDeletePopup = { active = false, name = nil }

local function trimSpaces(s)
	s = tostring(s or "")
	s = s:gsub("^%s+", ""):gsub("%s+$", "")
	return s
end

local function setProfileStatus(text, color)
	profileStatusText = tostring(text or "")
	profileStatusColor = color or profileStatusColor
end

local function refreshProfileList()
	local active = funcs.getActiveProfileName()
	local names = funcs.listProfiles()
	if type(names) ~= "table" or #names == 0 then
		names = { active }
	end

	profileNames = {}
	local activeIdx = 0
	for i, name in ipairs(names) do
		local clean = tostring(name or "")
		profileNames[#profileNames + 1] = clean
		if clean == active then
			activeIdx = i - 1
		end
	end

	if #profileNames == 0 then
		profileNames = { active }
		activeIdx = 0
	end

	profileSelectIndex[0] = activeIdx
	profileNamesFfi = imgui.new["const char*"][#profileNames](profileNames)
end

refreshProfileList()
local openHotkeyCapture = HotkeyManager.new({ on_save = function(keys) setOpenHotkey(keys) end })
local rootKeyTracker = HotkeyManager.newKeyTracker()
local currentTab = 1 -- Индекс вкладки
local miscPage = 0 -- 0 - меню, >0 - страницы настроек
local mainPos = imgui.ImVec2(10, 10)
local mainSize -- will init on first frame
local sidebarCollapsed = imgui.new.bool(false)
local SIDEBAR_W_EXPANDED = 128
local SIDEBAR_W_COLLAPSED = 44
local LOGO_SZ_EXPANDED = 128
local LOGO_SZ_COLLAPSED = 44

local _imguiSubs = {}
_imguiSubs[#_imguiSubs + 1] = imgui.OnInitialize(function()
	imgui_ready = true
	if mimgui_funcs and mimgui_funcs.Standart then
		mimgui_funcs.Standart()
	end
end)

-- === Главное окно ===
_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
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
		imgui.WindowFlags.NoMove
			+ imgui.WindowFlags.NoTitleBar
			+ imgui.WindowFlags.NoCollapse
			+ imgui.WindowFlags.NoScrollbar
			+ imgui.WindowFlags.NoScrollWithMouse
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
	local closeIcon = (ok2 and fa and fa.XMARK and fa.XMARK ~= "") and fa.XMARK or "X"
	local closeTextSize = imgui.CalcTextSize(closeIcon)
	local closeSide = math.max(titleH - 6, math.ceil(math.max(closeTextSize.x, imgui.GetFontSize()) + 4))
	local closePos = imgui.ImVec2(pmax.x - pad.x - closeSide, pmin.y + (titleH - closeSide) * 0.5)
	imgui.SetCursorScreenPos(closePos)
	imgui.InvisibleButton("##close_main_window", imgui.ImVec2(closeSide, closeSide))
	local closeHovered = imgui.IsItemHovered()
	local closeActive = imgui.IsItemActive()
	local closeMin = imgui.GetItemRectMin()
	local closeMax = imgui.GetItemRectMax()
	if closeHovered or closeActive then
		local closeBase = closeActive and style.Colors[imgui.Col.ButtonActive] or style.Colors[imgui.Col.ButtonHovered]
		local closeAlpha = closeActive and 1.0 or 0.85
		local closeBg = imgui.ImVec4(closeBase.x, closeBase.y, closeBase.z, math.min(1, closeBase.w * closeAlpha))
		local closeRound = math.max(3, style.FrameRounding)
		dl:AddRectFilled(closeMin, closeMax, imgui.GetColorU32Vec4(closeBg), closeRound)
	end
	local closeTextColor = (closeHovered or closeActive) and style.Colors[imgui.Col.Text] or style.Colors[imgui.Col.TextDisabled]
	local closeTextPos =
		imgui.ImVec2(closeMin.x + (closeSide - closeTextSize.x) * 0.5, closeMin.y + (closeSide - imgui.GetFontSize()) * 0.5)
	dl:AddText(closeTextPos, imgui.GetColorU32Vec4(closeTextColor), closeIcon)
	if imgui.IsItemClicked(0) then
		setRenderHotkeyWnd(false)
	end
	if closeHovered then
		imgui.SetTooltip("Закрыть")
		if imgui.SetMouseCursor then
			imgui.SetMouseCursor(imgui.MouseCursor.Hand)
		end
	end

	local sidebarW = sidebarCollapsed[0] and SIDEBAR_W_COLLAPSED or SIDEBAR_W_EXPANDED
	local logoSz = sidebarCollapsed[0] and LOGO_SZ_COLLAPSED or LOGO_SZ_EXPANDED

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
	end
	imgui.EndChild()

	if imgui.BeginChild("menu##vertical", imgui.ImVec2(sidebarW, 0), false) then
		if sidebarCollapsed[0] and ok2 and fa then
			local icons = { fa.HOUSE, fa.KEYBOARD, fa.NEWSPAPER, fa.BOOK, fa.CUBES, fa.GEAR }
			local tips = {
				"Главная",
				"Биндер",
				"СМИ Хелпер",
				"Блокнот",
				"Прочее",
				"Настройки",
			}
			local itemH = 44
			local dl = imgui.GetWindowDrawList()
			local style = imgui.GetStyle()

			for i = 1, #icons do
				imgui.PushIDInt(i)
				imgui.SetCursorPosX(0)

				local p = imgui.GetCursorScreenPos()
				imgui.InvisibleButton("##tab", imgui.ImVec2(sidebarW, itemH))
				local hovered = imgui.IsItemHovered()
				local clicked = imgui.IsItemClicked(0)
				local selected = (currentTab == i)

				if hovered or selected then
					local bg = selected and style.Colors[imgui.Col.HeaderActive]
						or style.Colors[imgui.Col.HeaderHovered]
					dl:AddRectFilled(p, imgui.ImVec2(p.x + sidebarW, p.y + itemH), imgui.GetColorU32Vec4(bg), 6)
				end

				local icon = icons[i]
				local ts = imgui.CalcTextSize(icon)
				dl:AddText(
					imgui.ImVec2(p.x + (sidebarW - ts.x) * 0.5, p.y + (itemH - imgui.GetFontSize()) * 0.5),
					imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]),
					icon
				)

				if hovered then
					imgui.BeginTooltip()
					imgui_text_safe(tips[i])
					imgui.EndTooltip()
				end

				if clicked then
					currentTab = i
				end

				imgui.PopID()
			end
		else
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
		end
	end
	imgui.EndChild()

	do
		local toggleIcon = fa and (sidebarCollapsed[0] and fa.ARROW_RIGHT_FROM_LINE or fa.ARROW_LEFT_TO_LINE) or ""
		if toggleIcon == "" then
			toggleIcon = sidebarCollapsed[0] and ">" or "<"
		end

		local toggleW = titleH - 8
		local toggleH = titleH - 8
		local toggleX = winPos.x + pad.x + sidebarW - toggleW - 2
		local toggleY = winPos.y + pad.y + titleH + 6
		local cursorPos = imgui.GetCursorPos()

		imgui.SetCursorScreenPos(imgui.ImVec2(toggleX, toggleY))
		imgui.InvisibleButton("##sidebar_toggle", imgui.ImVec2(toggleW, toggleH))

		local hovered = imgui.IsItemHovered()
		local clicked = imgui.IsItemClicked(0)

		local rmin = imgui.GetItemRectMin()
		local dl = imgui.GetWindowDrawList()

		local txtCol = hovered and style.Colors[imgui.Col.Text] or style.Colors[imgui.Col.TextDisabled]
		local textSize = imgui.CalcTextSize(toggleIcon)
		local textPos =
			imgui.ImVec2(rmin.x + (toggleW - textSize.x) * 0.5, rmin.y + (toggleH - imgui.GetFontSize()) * 0.5)
		dl:AddText(textPos, imgui.GetColorU32Vec4(txtCol), toggleIcon)

		if clicked then
			sidebarCollapsed[0] = not sidebarCollapsed[0]
		end

		imgui.SetCursorPos(cursorPos)
	end
	imgui.EndGroup()

	-- Правая основная часть
	imgui.SameLine()
	if imgui.BeginChild("main##content", imgui.ImVec2(0, 0), true) then
		if currentTab == 2 and binder then
			binder.DrawBinder()
		elseif currentTab == 3 and SMIHelp then
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
					imgui_text_safe(it.name)
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
			imgui_text_colored_safe(imgui.ImVec4(0.9, 0.9, 0.6, 1), hotkeyToString(openHotkey))
			imgui.Separator()
			if openHotkeyCapture:isActive() then
				HotkeyManager.drawCaptureUI(openHotkeyCapture, "open_hotkey", imgui_text_colored_safe)
			else
				if imgui.Button("Изменить комбинацию") then
					openHotkeyCapture:start()
				end
				imgui.SameLine()
				if imgui.Button("Сбросить на Ctrl + Z") then
					setOpenHotkey(defaultOpenHotkey)
				end
				imgui.Text(
					"Комбинация используется для открытия/закрытия окна HelperByOrc."
				)
			end
			imgui.Separator()
			imgui.Text("Профили (все JSON-файлы хранятся в Profiles/<имя_профиля>):")
			if binder and binder.getQuickMenuHotkeyDisplay then
				local quickMenuCapture = binder.getQuickMenuHotkeyCapture and binder.getQuickMenuHotkeyCapture() or nil
				imgui.Text("Хоткей открытия быстрого меню:")
				imgui.SameLine()
				imgui_text_colored_safe(imgui.ImVec4(0.9, 0.9, 0.6, 1), binder.getQuickMenuHotkeyDisplay())
				imgui.Separator()
				if quickMenuCapture and quickMenuCapture:isActive() then
					HotkeyManager.drawCaptureUI(quickMenuCapture, "binder_quick_menu_hotkey", imgui_text_colored_safe)
				else
					if imgui.Button("Изменить хоткей быстрого меню") then
						binder.startQuickMenuHotkeyCapture()
					end
					imgui.SameLine()
					if imgui.Button("Сбросить на XBUTTON1") then
						binder.resetQuickMenuHotkey()
					end
					imgui.Text("Комбинацию нужно удерживать для открытия быстрого меню binder.")
				end
				imgui.Separator()
			end
			imgui.Text("Профили (все JSON-файлы хранятся в Profiles/<имя_профиля>):")
			local activeProfileName = funcs.getActiveProfileName()
			imgui.SameLine()
			imgui_text_colored_safe(imgui.ImVec4(0.9, 0.9, 0.6, 1), activeProfileName)

			if profileNamesFfi and #profileNames > 0 then
				imgui.PushItemWidth(260)
				imgui.Combo("Выбранный профиль", profileSelectIndex, profileNamesFfi, #profileNames)
				imgui.PopItemWidth()
			end

			if imgui.Button("Обновить список профилей") then
				refreshProfileList()
				setProfileStatus("Список профилей обновлён.", imgui.ImVec4(0.7, 0.9, 1, 1))
			end
			imgui.SameLine()
			if imgui.Button("Применить профиль и перезагрузить") then
				local selected = profileNames[profileSelectIndex[0] + 1]
				if selected and selected ~= "" then
					local ok = funcs.setActiveProfileName(selected)
					if ok then
						setProfileStatus("Профиль применён, выполняю перезагрузку...", imgui.ImVec4(0.6, 1, 0.6, 1))
						refreshProfileList()
						local reloaded = requestScriptReload(function()
							setProfileStatus(
								"Профиль применён. Перезагрузите скрипт вручную.",
								imgui.ImVec4(0.9, 0.85, 0.45, 1)
							)
						end)
						if not reloaded then
							setProfileStatus(
								"Профиль применён. Перезагрузите скрипт вручную.",
								imgui.ImVec4(0.9, 0.85, 0.45, 1)
							)
						end
					else
						setProfileStatus("Не удалось применить профиль.", imgui.ImVec4(1, 0.6, 0.6, 1))
					end
				else
					setProfileStatus("Выберите профиль.", imgui.ImVec4(1, 0.6, 0.6, 1))
				end
			end

			imgui.PushItemWidth(260)
			imgui.InputText("Имя нового профиля", newProfileNameBuf, ffi.sizeof(newProfileNameBuf))
			imgui.PopItemWidth()

			if imgui.Button("Создать профиль") then
				local requested = trimSpaces(ffi.string(newProfileNameBuf))
				if requested == "" then
					setProfileStatus("Введите имя нового профиля.", imgui.ImVec4(1, 0.6, 0.6, 1))
				else
					local ok, createdName = funcs.createProfile(requested, { copy_from = false })
					if ok then
						imgui.StrCopy(newProfileNameBuf, "")
						refreshProfileList()
						for i, name in ipairs(profileNames) do
							if name == createdName then
								profileSelectIndex[0] = i - 1
								break
							end
						end
						setProfileStatus("Профиль создан: " .. tostring(createdName), imgui.ImVec4(0.6, 1, 0.6, 1))
					else
						setProfileStatus("Не удалось создать профиль.", imgui.ImVec4(1, 0.6, 0.6, 1))
					end
				end
			end
			imgui.SameLine()
			if imgui.Button("Скопировать профиль в новый") then
				local requested = trimSpaces(ffi.string(newProfileNameBuf))
				local sourceProfile = profileNames[profileSelectIndex[0] + 1] or activeProfileName
				if requested == "" then
					setProfileStatus("Введите имя нового профиля.", imgui.ImVec4(1, 0.6, 0.6, 1))
				else
					local ok, createdName = funcs.createProfile(requested, { from = sourceProfile, copy_from = true })
					if ok then
						imgui.StrCopy(newProfileNameBuf, "")
						refreshProfileList()
						for i, name in ipairs(profileNames) do
							if name == createdName then
								profileSelectIndex[0] = i - 1
								break
							end
						end
						setProfileStatus(
							"Профиль создан копированием: " .. tostring(createdName),
							imgui.ImVec4(0.6, 1, 0.6, 1)
						)
					else
						setProfileStatus("Не удалось скопировать профиль.", imgui.ImVec4(1, 0.6, 0.6, 1))
					end
				end
			end
			imgui.SameLine()
			if imgui.Button("Удалить профиль") then
				local selected = profileNames[profileSelectIndex[0] + 1]
				if selected and selected ~= "" then
					profileDeletePopup.name = selected
					profileDeletePopup.active = true
				else
					setProfileStatus("Выберите профиль для удаления.", imgui.ImVec4(1, 0.6, 0.6, 1))
				end
			end

			if profileDeletePopup.active then
				imgui.OpenPopup("profile_delete_confirm")
				profileDeletePopup.active = false
			end
			if imgui.BeginPopupModal("profile_delete_confirm", nil, imgui.WindowFlags.AlwaysAutoResize) then
				local deleteName = tostring(profileDeletePopup.name or "")
				imgui_text_safe(('Удалить профиль "%s"?'):format(deleteName))
				if deleteName == activeProfileName then
					imgui_text_colored_safe(
						imgui.ImVec4(1, 0.7, 0.45, 1),
						"Активный профиль удалить нельзя. Сначала переключитесь на другой."
					)
				end
				if deleteName == funcs.getDefaultProfileName() then
					imgui_text_colored_safe(
						imgui.ImVec4(1, 0.7, 0.45, 1),
						'Профиль "Standard" удалить нельзя.'
					)
				end

				if imgui.Button("Удалить##profile_confirm", imgui.ImVec2(120, 0)) then
					local ok, reason = false, "unknown"
					ok, reason = funcs.deleteProfile(deleteName, { keep_default = true, forbid_active = true })
					if ok then
						setProfileStatus("Профиль удалён: " .. deleteName, imgui.ImVec4(0.6, 1, 0.6, 1))
						profileDeletePopup.name = nil
						refreshProfileList()
					else
						local msg = "Не удалось удалить профиль."
						if reason == "active_profile" then
							msg = "Нельзя удалить активный профиль."
						elseif reason == "default_profile" then
							msg = 'Нельзя удалить профиль "Standard".'
						elseif reason == "not_found" then
							msg = "Профиль не найден."
						elseif reason == "remove_failed" then
							msg = "Ошибка удаления файлов профиля."
						end
						setProfileStatus(msg, imgui.ImVec4(1, 0.6, 0.6, 1))
					end
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button("Отмена##profile_confirm", imgui.ImVec2(120, 0)) then
					profileDeletePopup.name = nil
					imgui.CloseCurrentPopup()
				end
				imgui.EndPopup()
			end

			if profileStatusText ~= "" then
				imgui_text_colored_safe(profileStatusColor, profileStatusText)
			end

			local profilesRootPath = funcs.getProfilesRootPath()
			if type(profilesRootPath) == "string" and profilesRootPath ~= "" then
				imgui_text_colored_safe(imgui.ImVec4(0.75, 0.75, 0.85, 1), profilesRootPath)
			end

			if correct then
				imgui.Separator()
				if imgui.Checkbox("Автокорректор включён##correct_module_enabled", correctEnabledBool) then
					projectConfig.correctEnabled = correctEnabledBool[0]
					saveProjectConfig()
					if type(correct.setEnabled) == "function" then
						correct.setEnabled(correctEnabledBool[0])
					end
				end
				if correct.DrawSettingsInline then
					correct.DrawSettingsInline()
				end
			end

			if toasts and toasts.DrawSettingsInline then
				toasts.DrawSettingsInline()
			end
		elseif currentTab == 1 then
			imgui.TextColored(imgui.ImVec4(0.8, 0.95, 1, 1), "HelperByOrc")
			imgui.Separator()
			imgui.Spacing()
			imgui_text_safe("Многофункциональный помощник для SA-MP.")
			imgui_text_safe("Содержит инструменты для удобной игры: биндер, помощник СМИ, блокнот, фильтры чата и автокорректор.")
			imgui.Spacing()
			imgui.Separator()
			imgui.TextColored(imgui.ImVec4(0.9, 0.9, 0.6, 1), "Разделы:")
			imgui.Spacing()
			local home_sections = {
				{ "Биндер",       "горячие клавиши для быстрого ввода текста в чат" },
				{ "СМИ Хелпер",   "шаблоны и инструменты для репортёров" },
				{ "Блокнот",      "личные заметки прямо в игре" },
				{ "Прочее",       "теги, VIP/AD чат, фильтры сообщений, оружие RP" },
				{ "Настройки",    "профили, хоткей открытия, автокорректор" },
			}
			for _, row in ipairs(home_sections) do
				imgui.Bullet()
				imgui.SameLine()
				imgui_text_colored_safe(imgui.ImVec4(0.75, 0.9, 1, 1), row[1])
				imgui.SameLine()
				imgui_text_safe("— " .. row[2])
			end
			imgui.Spacing()
			imgui.Separator()
			imgui.TextColored(imgui.ImVec4(0.9, 0.9, 0.6, 1), "Основные хоткеи:")
			imgui.Spacing()
			local home_hotkeys = {
				{ hotkeyToString(openHotkey), "открыть / закрыть главное окно" },
				{ "Esc",                      "закрыть окно" },
			}
			for _, row in ipairs(home_hotkeys) do
				imgui_text_colored_safe(imgui.ImVec4(0.6, 1, 0.7, 1), "[ " .. row[1] .. " ]")
				imgui.SameLine()
				imgui_text_safe("— " .. row[2])
			end
		else
			imgui.TextColored(imgui.ImVec4(0.8, 0.8, 1, 1), "HelperByOrc")
			imgui.Separator()
			imgui.Text("Вкладка в разработке. Тут будет контент.")
		end
	end
	imgui.EndChild()
	imgui.End()
end)

-- === Глобальный хоткей для вызова главного окна ===
local rootHotkeyHandlerBound = false
local WM_KILLFOCUS = wm.WM_KILLFOCUS or 0x0008
local WM_SETFOCUS = wm.WM_SETFOCUS or 0x0007
local WM_ACTIVATE = wm.WM_ACTIVATE or 0x0006
local WM_ACTIVATEAPP = wm.WM_ACTIVATEAPP or 0x001C
local WM_SYSKEYDOWN = wm.WM_SYSKEYDOWN or 0x0104
local WM_SYSKEYUP = wm.WM_SYSKEYUP or 0x0105
local WA_INACTIVE = 0
local WA_ACTIVE = 1
local WA_CLICKACTIVE = 2
local HOTKEY_FOCUS_DEBOUNCE_SEC = 0.35
local suppressHotkeysUntil = 0

local function loWord(value)
	value = tonumber(value) or 0
	return value % 0x10000
end

local function clearRootPressedKeys()
	rootKeyTracker:reset()
	openHotkeyActive = false
end

local function resetInputStateForFocus(reason)
	clearRootPressedKeys()
	if mimgui_funcs and type(mimgui_funcs.resetIO) == "function" then
		pcall(mimgui_funcs.resetIO)
	end
	if type(binder) == "table" and type(binder.resetInputState) == "function" then
		pcall(binder.resetInputState, reason)
	end
	if type(correct) == "table" and type(correct.resetInputState) == "function" then
		pcall(correct.resetInputState, reason)
	end
end

local function onRootWindowMessage(msg, wparam)
	local now = os.clock()
	local activateState = loWord(wparam)
	local lostFocus = msg == WM_KILLFOCUS
		or (msg == WM_ACTIVATEAPP and tonumber(wparam) == 0)
		or (msg == WM_ACTIVATE and activateState == WA_INACTIVE)
	local gainedFocus = msg == WM_SETFOCUS
		or (msg == WM_ACTIVATEAPP and tonumber(wparam) ~= 0)
		or (msg == WM_ACTIVATE and (activateState == WA_ACTIVE or activateState == WA_CLICKACTIVE))

	if lostFocus then
		suppressHotkeysUntil = now + HOTKEY_FOCUS_DEBOUNCE_SEC
		resetInputStateForFocus("focus_lost")
		return
	end

	if gainedFocus then
		suppressHotkeysUntil = now + HOTKEY_FOCUS_DEBOUNCE_SEC
		resetInputStateForFocus("focus_gain")
		return
	end

	local isKeyDownMsg = msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN
	local isKeyUpMsg = msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP
	if not (isKeyDownMsg or isKeyUpMsg) then
		return
	end
	if now < suppressHotkeysUntil then
		return
	end

	local keyCode = normalizeKey(wparam)
	if not isKeyboardKey(keyCode) then
		return
	end

	-- Alt+Tab не должен попадать в систему биндов/захвата.
	if (msg == WM_SYSKEYDOWN or msg == WM_SYSKEYUP) and keyCode == vk.VK_TAB then
		resetInputStateForFocus("alttab")
		return
	end

	-- Захват новой комбинации (hold-based)
	local cwm = type(consumeWindowMessage) == "function" and consumeWindowMessage or nil
	if openHotkeyCapture:onWindowMessage(msg, wparam, cwm) then
		return
	end

	if keyCode == vk.VK_ESCAPE then
		local anyMimguiOpen = isAnyProjectWindowOpen()

		if anyMimguiOpen and isKeyDownMsg and cwm then
			cwm(true, false)
		end

		if renderHotkeyWnd[0] and isKeyUpMsg then
			setRenderHotkeyWnd(false)
			openHotkeyActive = false
		end

		if anyMimguiOpen then
			return
		end
	end

	rootKeyTracker:onWindowMessage(msg, wparam)

	local comboNow = HotkeyManager.comboMatchOrdered(rootKeyTracker:getOrdered(), openHotkey)
	if comboNow and not openHotkeyActive then
		toggleRenderHotkeyWnd()
		openHotkeyActive = true
	elseif not comboNow and openHotkeyActive then
		openHotkeyActive = false
	end
end

local function dispatchModuleWindowMessage(module_ref, msg, wparam, lparam)
	if type(module_ref) ~= "table" or type(module_ref.onWindowMessage) ~= "function" then
		return false
	end
	local ok, consumed = pcall(module_ref.onWindowMessage, msg, wparam, lparam)
	if not ok then
		print("[HelperByOrc] onWindowMessage error: " .. tostring(consumed))
		return false
	end
	return consumed == true
end

local function onWindowMessageDispatcher(msg, wparam, lparam)
	if not runtime_state.active then
		return
	end
	if dispatchModuleWindowMessage(binder, msg, wparam, lparam) then
		return
	end
	if dispatchModuleWindowMessage(correct, msg, wparam, lparam) then
		return
	end
	onRootWindowMessage(msg, wparam, lparam)
end

local function requestScriptReload(on_fail)
	if reload_in_progress then
		return true
	end
	reload_in_progress = true

	local function failReload(err)
		reload_in_progress = false
		if type(on_fail) == "function" then
			pcall(on_fail, err)
		end
		print("[HelperByOrc] reload failed: " .. tostring(err))
	end

	local function doReload()
		pcall(app.onTerminate, "manual_reload")
		if type(modules_ref) == "table" and type(modules_ref.terminateAll) == "function" then
			pcall(modules_ref.terminateAll, { reason = "manual_reload" })
		end

		local ok_reload, err_reload = pcall(function()
			thisScript():reload()
		end)
		if not ok_reload then
			failReload(err_reload)
		end
	end

	if lua_thread and type(lua_thread.create) == "function" and type(wait) == "function" then
		local ok_thread = pcall(lua_thread.create, function()
			wait(0)
			doReload()
		end)
		if ok_thread then
			return true
		end
	end

	doReload()
	return true
end

local function ensureRootHotkeyHandler()
	if rootHotkeyHandlerBound then
		return
	end
	if type(addEventHandler) ~= "function" then
		return
	end
	addEventHandler("onWindowMessage", onWindowMessageDispatcher)
	rootHotkeyHandlerBound = true
end


-- === onTick: обработка быстрого меню биндер-модуля ===
function app.onSampReady()
	runtime_state.active = true
	ensureRootHotkeyHandler()
	suppressHotkeysUntil = os.clock() + HOTKEY_FOCUS_DEBOUNCE_SEC
	resetInputStateForFocus("samp_ready")

	if weapon_rp and weapon_rp.start then
		weapon_rp.start()
	end

	if myhooks and myhooks.init then
		myhooks.init()
	end

	-- Инициализация tags модуля (запуск треда слежения за таргетом)
	if tags and tags.onSampReady then
		tags.onSampReady()
	end

	-- Запуск потоков для работы интерфейса и тегов
	if binder and binder.loadHotkeys then
		binder.loadHotkeys()
	end

	local ok_binder_thr, binder_thr_or_err = pcall(lua_thread.create, function()
		if binder and binder.OnTick then
			while runtime_state.active do
				if wasKeyPressed(vk.VK_XBUTTON1) then
					if mimgui_funcs and mimgui_funcs.resetIO then
						mimgui_funcs.resetIO()
					end
				end
				local ok_tick = pcall(binder.OnTick)
				if not ok_tick then
					wait(100)
				end
				wait(0)
			end
		end
	end)
	if ok_binder_thr then
		runtime_state.binder_thread = binder_thr_or_err
	end

	local ok_tags_thr, tags_thr_or_err = pcall(lua_thread.create, function()
		if tags and tags.RenderTagsWindow then
			while runtime_state.active do
				pcall(tags.RenderTagsWindow)
				wait(25)
			end
		end
	end)
	if ok_tags_thr then
		runtime_state.tags_thread = tags_thr_or_err
	end

	if VIPandADchat and VIPandADchat.showFeedWindow then
		VIPandADchat.showFeedWindow[0] = true
	end

	-- Запуск хоткеев correct и регистрация комбо в binder (независимо от binder)
	if correct and correct.start then
		correct.start()
	end
	if correct and binder and binder.registerExternalHotkey then
		local yKeys = correct.getYandexHotkey and correct.getYandexHotkey()
		local ltKeys = correct.getLanguageToolHotkey and correct.getLanguageToolHotkey()
		if yKeys and #yKeys > 0 then
			binder.registerExternalHotkey(yKeys, "Автокорректор (Yandex)")
		end
		if ltKeys and #ltKeys > 0 then
			binder.registerExternalHotkey(ltKeys, "Автокорректор (LT)")
		end
	end

end

function app.onTerminate(reason)
	runtime_state.active = false
	-- КРИТИЧНО: снимаем обработчик ДО разрушения Lua-состояния,
	-- иначе MoonLoader вызовет коллбэк в уже уничтоженном состоянии → краш lua51.dll
	if rootHotkeyHandlerBound and type(removeEventHandler) == "function" then
		pcall(removeEventHandler, "onWindowMessage", onWindowMessageDispatcher)
		rootHotkeyHandlerBound = false
	end
	resetInputStateForFocus("terminate")
	stopRuntimeThread(runtime_state.binder_thread)
	stopRuntimeThread(runtime_state.tags_thread)
	runtime_state.binder_thread = nil
	runtime_state.tags_thread = nil
	for i = #_imguiSubs, 1, -1 do
		local sub = _imguiSubs[i]
		if sub and type(sub.Unsubscribe) == "function" then
			pcall(sub.Unsubscribe, sub)
		end
		_imguiSubs[i] = nil
	end
	if event_bus_ref then
		event_bus_ref.offByOwner("main_app")
	end
	-- config_manager.flushAll() will be called by modules.terminateAll() after this
	if not config_manager_ref then
		pcall(saveProjectConfig)
	end
end

return app
