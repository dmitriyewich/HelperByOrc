local module = {}
local imgui = require "mimgui"
local ffi = require "ffi"
local encoding = require "encoding"
local mimgui_funcs = require "HelperByOrc.mimgui_funcs"
encoding.default = "CP1251"
local u8 = encoding.UTF8
local vk = require "vkeys"
local vkeys = vk
local wm = require "windows.message"
local bit = require "bit"
local funcs, tags
local bor = bit and bit.bor or function(a, b)
		return a + b
	end
local quickMenuPos = imgui.ImVec2(0, 0)
local quickMenuSize = imgui.ImVec2(260, 280)

function module.attachModules(mod)
	funcs = mod.funcs
	tags = mod.tags
end

-- Иконки (безопасный фолбэк)
local ok_fa, fa = pcall(require, "HelperByOrc.fAwesome6_solid")
if not ok_fa or type(fa) ~= "table" then
	fa =
		setmetatable(
		{},
		{__index = function()
				return ""
			end}
	)
end

-- === Константы ===
local json_path = "moonloader/HelperByOrc/binder.json"
local DEBOUNCE_MS = 40 -- антидребезг
local MAX_BIND_DEPTH = 5 -- защита от рекурсии при внешних вызовах runBind*
local MAX_ACTIVE_HOTKEYS = 10 -- limit of active coroutines

-- === Утилиты ===
local function flags_or(...)
	local sum = 0
	for i = 1, select("#", ...) do
		local f = select(i, ...)
		if f then
			sum = bor(sum, f)
		end
	end
	return sum
end

-- === Toasts ===
local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local function pushToast(text, kind, dur)
	toasts[#toasts + 1] = {text = tostring(text or ""), kind = kind or "ok", t = os.clock(), dur = dur or 3.0}
end
local active_coroutines = {} -- { hk, co, state, wake }

local function log_error(err)
	print("[binder] " .. tostring(err))
	pushToast(err, "err", 5.0)
end

local scheduler =
	coroutine.create(
	function()
		while true do
			local now = os.clock() * 1000
			for i = #active_coroutines, 1, -1 do
				local item = active_coroutines[i]
				local state = item.state
				if state.stopped then
					item.hk.is_running = false
					item.hk._co_state = nil
					table.remove(active_coroutines, i)
				elseif now >= item.wake then
					local ok, wait_ms = coroutine.resume(item.co)
					if not ok then
						log_error(wait_ms)
						item.hk.is_running = false
						item.hk._co_state = nil
						table.remove(active_coroutines, i)
					elseif coroutine.status(item.co) == "dead" then
						item.hk.is_running = false
						item.hk._co_state = nil
						table.remove(active_coroutines, i)
					else
						item.wake = now + (wait_ms or 0)
					end
				end
			end
			coroutine.yield()
		end
	end
)

local function runScheduler()
	if coroutine.status(scheduler) ~= "dead" then
		local ok, err = coroutine.resume(scheduler)
		if not ok then
			log_error(err)
		end
	end
end

module.runScheduler = runScheduler

local function drawToasts()
	if #toasts == 0 then
		return
	end
	local now = os.clock()
	-- фолбэк: если нет GetMainViewport (старый mimgui)
	local vpPosX, vpPosY, vpW, vpH = 0, 0, nil, nil
	if imgui.GetMainViewport then
		local vp = imgui.GetMainViewport()
		vpPosX, vpPosY, vpW, vpH = vp.Pos.x, vp.Pos.y, vp.Size.x, vp.Size.y
	else
		local io = imgui.GetIO()
		vpPosX, vpPosY, vpW, vpH = 0, 0, io.DisplaySize.x, io.DisplaySize.y
	end
	local pad = 8
	local x = vpPosX + vpW - 350 - pad
	local y = vpPosY + pad
	for i = #toasts, 1, -1 do
		local toast = toasts[i]
		if now - toast.t > toast.dur then
			table.remove(toasts, i)
		else
			imgui.SetNextWindowPos(imgui.ImVec2(x, y), imgui.Cond.Always)
			imgui.SetNextWindowSize(imgui.ImVec2(350, 0), imgui.Cond.Always)
			local col
			if toast.kind == "err" then
				col = imgui.ImVec4(0.35, 0.05, 0.05, 0.95)
			elseif toast.kind == "warn" then
				col = imgui.ImVec4(0.35, 0.25, 0.05, 0.95)
			else
				col = imgui.ImVec4(0.1, 0.25, 0.1, 0.95)
			end
			imgui.PushStyleColor(imgui.Col.WindowBg, col)
			imgui.Begin(
				"##toast" .. i,
				nil,
				imgui.WindowFlags.NoDecoration + imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoInputs
			)
			imgui.TextWrapped(toast.text)
			imgui.End()
			imgui.PopStyleColor()
			y = y + 46
		end
	end
end

-- === Состояние модуля ===
module.binderWindow = imgui.new.bool(false)
module.showQuickMenu = imgui.new.bool(false)
module.quickMenuOpen = false
local quickMenuTabIndex = 1
local quickMenuScrollQueued = 0

imgui.OnInitialize(
	function()
		if fa and fa.Init then
			fa.Init()
		end
		math.randomseed(os.time())
	end
)

-- === Данные ===
local folders = {{name = "Основные", children = {}, parent = nil, quick_conditions = {}, quick_menu = true}}
local selectedFolder = folders[1]
local hotkeys = {}
local labelInputs = setmetatable({}, {__mode = "k"})

-- кэш булевых для imgui
local function ensure_bool(buf, val)
	if not buf then
		buf = imgui.new.bool(val and true or false)
	else
		buf[0] = val and true or false
	end
	return buf
end

local editHotkey = {active = false, idx = -1}

-- Попапы редактора
local combo_recording = false
local combo_keys = {}
local open_combo_popup = false
local open_conditions_popup = false
local open_quick_conditions_popup = false

-- Подписи
local send_labels = {"В чат", "Клиенту", "Серверу", "В пустоту"}
local send_labels_ffi = imgui.new["const char*"][#send_labels](send_labels)

local cond_labels = {
	"Не сработает в воде",
	"Не сработает если игрок мертв",
	"Не сработает в воздухе"
}
local cond_count = #cond_labels

-- Условия появления в быстром меню для биндов/папок
local quick_cond_labels = {
	"Скрывать если в воде",
	"Скрывать если игрок мертв",
	"Скрывать если в воздухе"
}
local quick_cond_count = #quick_cond_labels

-- JSON
local config = {hotkeys = {}, folders = {}}

-- === Папки / пути ===
local function folderFullPath(folder)
	local path = {}
	while folder do
		table.insert(path, 1, folder.name)
		folder = folder.parent
	end
	return path
end

local function pathEquals(a, b)
	if not a or not b then
		return false
	end
	if #a ~= #b then
		return false
	end
	for i = 1, #a do
		if a[i] ~= b[i] then
			return false
		end
	end
	return true
end

local function sanitizeFolderName(s)
	s = tostring(s or ""):gsub("%.", "_")
	s = s:gsub("[\r\n]", " ")
	return s
end

local function folderNameUnique(parentArr, name)
	for _, c in ipairs(parentArr) do
		if c.name == name then
			return false
		end
	end
	return true
end

local function serializeFolder(folder)
        local node = {
                name = folder.name,
                children = {},
                quick_conditions = folder.quick_conditions or {},
                quick_menu = folder.quick_menu ~= false
        }
	for _, child in ipairs(folder.children) do
		table.insert(node.children, serializeFolder(child))
	end
	return node
end

local function deserializeFolder(tbl, parent)
        local node = {
                name = sanitizeFolderName(tbl.name),
                children = {},
                parent = parent,
                quick_conditions = tbl.quick_conditions or {},
                quick_menu = tbl.quick_menu ~= false
        }
	for _, child in ipairs(tbl.children or {}) do
		local c = deserializeFolder(child, node)
		table.insert(node.children, c)
	end
	return node
end

local function removeFolder(arr, node)
	for i, v in ipairs(arr) do
		if v == node then
			table.remove(arr, i)
			break
		end
	end
end

-- === JSON save/load ===
function module.saveHotkeys()
	config.hotkeys, config.folders = {}, {}
	for _, f in ipairs(folders) do
		table.insert(config.folders, serializeFolder(f))
	end
	for _, hk in ipairs(hotkeys) do
		local msgs = {}
		for _, m in ipairs(hk.messages or {}) do
			table.insert(msgs, {text = m.text, interval = m.interval, method = m.method})
		end
		table.insert(
			config.hotkeys,
			{
				label = hk.label,
				keys = hk.keys,
				repeat_mode = hk.repeat_mode,
				repeat_interval_ms = hk.repeat_interval_ms,
				enabled = hk.enabled or false,
				quick_menu = hk.quick_menu or false,
				messages = msgs,
				conditions = hk.conditions,
			       quick_conditions = hk.quick_conditions,
			       command = hk.command or "",
			       command_enabled = hk.command_enabled or false,
			       folderPath = hk.folderPath,
			       text_trigger = hk.text_trigger
			}
		)
	end
	funcs.saveTableToJson(config, json_path)
end

local function newHotkeyBase()
	return {
		label = "Новый бинд",
		keys = {},
		messages = {},
		text_trigger = {text = "", enabled = false, pattern = false},
		repeat_mode = false,
		repeat_interval_ms = nil,
		conditions = {},
		quick_conditions = {},
		enabled = true,
	       quick_menu = false,
	       command = "",
	       command_enabled = false,
	       folderPath = {folders[1].name},
		is_running = false,
		_co_state = nil,
		lastActivated = 0,
		_bools = {},
		_cond_bools = {},
		_quick_cond_bools = {},
		_comboActive = false, -- лэтч комбо
		_debounce_until = nil
	}
end

function module.registerHotkey(keys, messages, label, repeat_mode, conditions, command, folderPath, text_trigger, command_enabled)
	local hk = newHotkeyBase()
	hk.keys = keys or {}
	hk.messages = messages or {}
	hk.label = label or hk.label
	hk.repeat_mode = not (not repeat_mode)
	hk.conditions = conditions or {}
	hk.command = command or ""
	hk.command_enabled = not not command_enabled
	hk.folderPath = folderPath or {folders[1].name}
	hk.text_trigger = text_trigger or {text = "", enabled = false, pattern = false}
	hotkeys[#hotkeys + 1] = hk
end

function module.loadHotkeys()
	local tbl = funcs.loadTableFromJson(json_path)
	if type(tbl) == "table" then
		hotkeys, folders = {}, {}
		if tbl.folders and #tbl.folders > 0 then
			for _, f in ipairs(tbl.folders) do
				local folder = deserializeFolder(f, nil)
				table.insert(folders, folder)
			end
		else
                       folders = {{name = "Основные", children = {}, parent = nil, quick_conditions = {}, quick_menu = true}}
		end
		selectedFolder = folders[1]
		for _, hk in ipairs(tbl.hotkeys or {}) do
		       module.registerHotkey(
			       hk.keys,
			       hk.messages,
			       hk.label,
			       hk.repeat_mode,
			       hk.conditions,
			       hk.command,
			       hk.folderPath or {folders[1].name},
			       hk.text_trigger,
			       hk.command_enabled
		       )
		       local last = hotkeys[#hotkeys]
		       last.enabled = hk.enabled == nil and true or hk.enabled
		       last.quick_menu = hk.quick_menu or hk.fast_menu or false
		       last.repeat_interval_ms = tonumber(hk.repeat_interval_ms) or nil
		       last.quick_conditions = hk.quick_conditions or {}
		       last.text_trigger = hk.text_trigger or {text = "", enabled = false, pattern = false}
		       last.command_enabled = hk.command_enabled == nil and (hk.command ~= "") or hk.command_enabled
		end
	end
end

-- === Комбо и клавиши ===
local function hotkeyToString(keys)
	local t = {}
	for _, k in ipairs(keys or {}) do
		t[#t + 1] = vkeys.id_to_name and vkeys.id_to_name(k) or tostring(k)
	end
	return #t > 0 and table.concat(t, " + ") or "[KEY]"
end

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

-- Живое состояние нажатых клавиш (без сканирования 0..255)
local pressedKeysSet = {} -- k -> true
local pressedKeysList = {} -- список актуальных
local function rebuildPressedList()
	pressedKeysList = {}
	for k, v in pairs(pressedKeysSet) do
		if v then
			table.insert(pressedKeysList, k)
		end
	end
end

-- === Отправка сообщений ===
local function change_tags_ignore_colors(text)
	if not (tags and tags.change_tags) then
		return text
	end
	local colors = {}
	local idx = 0
	text = text:gsub("{%x%x%x%x%x%x}", function(code)
		idx = idx + 1
		local token = "__COLOR" .. idx .. "__"
		colors[token] = code
		return token
	end)
	text = tags.change_tags(text)
	for token, code in pairs(colors) do
		text = text:gsub(token, code)
	end
	return text
end

local function doSend(msg, method)
	local s = msg
	if tags and tags.change_tags then
		if method == 0 then
			s = change_tags_ignore_colors(s)
		else
			s = tags.change_tags(s)
		end
	end
	s = u8:decode(s)
	if method == 0 then
		sampAddChatMessage(s, 0x00DD00)
	elseif method == 1 then
		sampProcessChatInput(s)
	elseif method == 2 then
		sampSendChat(s)
	elseif method == 3 then
	-- в пустоту
	end
end

-- === Условия ===
local cond_funcs = {
	function()
		return isCharInWater(PLAYER_PED)
	end,
	function()
		return isCharDead(PLAYER_PED)
	end,
	function()
		return isCharInAir(PLAYER_PED)
	end
}

local function safe_call(fn)
	local ok, res = pcall(fn)
	return ok and not (not res)
end

local function conditions_ok(conds)
	for idx, v in ipairs(conds or {}) do
		if v and cond_funcs[idx] and safe_call(cond_funcs[idx]) then
			return false
		end
	end
	return true
end

local check_conditions = conditions_ok
local check_quick_visibility = conditions_ok

-- Проверка видимости папки с учётом ВСЕХ предков
local function isFolderChainVisible(folder)
        local node = folder
        while node do
                if node.quick_menu == false then
                        return false
                end
                if not check_quick_visibility(node.quick_conditions or {}) then
                        return false
                end
                node = node.parent
        end
	return true
end

-- === Поиск папок/биндов ===
local function findFolderNodeByPath(path)
	if not path or #path == 0 then
		return nil
	end
	local nodeList = folders
	local node = nil
	for _, name in ipairs(path) do
		local found
		for _, f in ipairs(nodeList) do
			if f.name == name then
				found = f
				break
			end
		end
		if not found then
			return nil
		end
		node = found
		nodeList = found.children
	end
	return node
end

local function pathFromString(s)
	if not s or s == "" then
		return nil
	end
	local t = {}
	for part in tostring(s):gmatch("[^/]+") do
		t[#t + 1] = part
	end
	return #t > 0 and t or nil
end

local function collectBindsInFolder(pathTbl, recursive)
	local res = {}
	local function matchPath(hk)
		if not pathTbl or #pathTbl == 0 then
			return true
		end
		if not hk.folderPath then
			return false
		end
		if recursive then
			if #hk.folderPath < #pathTbl then
				return false
			end
			for i = 1, #pathTbl do
				if hk.folderPath[i] ~= pathTbl[i] then
					return false
				end
			end
			return true
		else
			return pathEquals(hk.folderPath, pathTbl)
		end
	end
	for _, hk in ipairs(hotkeys) do
		if hk.enabled and matchPath(hk) then
			table.insert(res, hk)
		end
	end
	return res
end

-- === Короутина отправки ===
function module.sendHotkeyCoroutine(hk, state)
	local messages = hk.messages

	for idx, msg in ipairs(messages) do
		if state.stopped then
			return
		end

		local text = msg.text or ""

		-- [waitif(...)]
		local pos = 1
		local out = {}
		while pos <= #text do
			local s, e, expr = text:find("%[waitif%((.-)%)%]", pos)
			if s then
				if s > pos then
					table.insert(out, text:sub(pos, s - 1))
				end
				local fn = load("return (" .. expr .. ")")
				if fn then
					while true do
						local ok, res = pcall(fn)
						if ok and res then
							break
						end
						if state.stopped then
							return
						end
						while state.paused do
							if state.stopped then
								return
							end
							coroutine.yield(50)
						end
						coroutine.yield(50)
					end
				end
				pos = e + 1
			else
				table.insert(out, text:sub(pos))
				break
			end
		end

		local final_str = table.concat(out)
		if final_str and final_str:match("%S") then
			doSend(final_str, msg.method or 0)
		end

		if idx < #messages then
			local interval = tonumber(msg.interval) or 0
			if interval < 50 then
				interval = 50
			end
			while state.paused do
				if state.stopped then
					return
				end
				coroutine.yield(50)
			end
			coroutine.yield(interval)
		end
	end
end

function module.enqueueHotkey(hk, delay_ms)
	if hk.is_running or not hk.enabled then
		return
	end
	if not check_conditions(hk.conditions) then
		return
	end
	if hk.messages and #hk.messages > 0 then
		if #active_coroutines >= MAX_ACTIVE_HOTKEYS then
			pushToast("Превышен лимит активных биндов", "warn", 3.0)
			return
		end
		local state = {paused = false, idx = 1, stopped = false}
		hk._co_state = state
		hk.is_running = true
		local co =
			coroutine.create(
			function()
				if delay_ms and delay_ms > 0 then
					coroutine.yield(delay_ms)
				end
				module.sendHotkeyCoroutine(hk, state)
			end
		)
		table.insert(active_coroutines, {hk = hk, co = co, state = state, wake = 0})
	end
end

function module.onServerMessage(text)
	local nowMs = os.clock() * 1000
	for _, hk in ipairs(hotkeys) do
		local trig = hk.text_trigger
		if trig and trig.enabled and trig.text and trig.text ~= "" then
			local matched
			if trig.pattern then
				matched = text:match(trig.text) ~= nil
			else
				matched = text == trig.text
			end
			if matched then
				if not hk._debounce_until or nowMs >= hk._debounce_until then
					module.enqueueHotkey(hk)
					hk._debounce_until = nowMs + DEBOUNCE_MS
				end
			end
		end
	end
end

function module.onPlayerCommand(cmd)
	local nowMs = os.clock() * 1000
	local handled = false
        for _, hk in ipairs(hotkeys) do
                -- если бинд уже выполняется, не перехватываем повторно его команду
                if hk.command_enabled and hk.command and hk.command ~= "" and not hk.is_running then
                        local len = #hk.command
                        if cmd:sub(1, len) == hk.command and (cmd:len() == len or cmd:sub(len + 1, len + 1) == " ") then
                                if not hk._debounce_until or nowMs >= hk._debounce_until then
                                        module.enqueueHotkey(hk)
                                        hk._debounce_until = nowMs + DEBOUNCE_MS
                                        handled = true
                                end
                        end
                end
        end
	return handled
end

function module.stopHotkey(hk)
	local state = hk._co_state
	if state then
		state.stopped = true
		hk.is_running = false
		hk._co_state = nil
	end
end

function module.stopAllHotkeys()
	for i = 1, #active_coroutines do
		local info = active_coroutines[i]
		info.state.stopped = true
		info.hk.is_running = false
		info.hk._co_state = nil
	end
end

-- совместимость со старым API
module.launchHotkeyThread = module.enqueueHotkey
module.stopAllThreads = module.stopAllHotkeys

-- === Быстрое меню (учёт условий папок по всей цепочке) ===
local function folderHasQuickBindsVisible(folder)
	if not isFolderChainVisible(folder) then
		return false
	end
	for _, hk in ipairs(hotkeys) do
		if
			hk.quick_menu and pathEquals(hk.folderPath, folderFullPath(folder)) and
				check_quick_visibility(hk.quick_conditions or {})
		 then
			return true
		end
	end
	for _, child in ipairs(folder.children or {}) do
		if folderHasQuickBindsVisible(child) then
			return true
		end
	end
	return false
end

function module.DrawQuickMenu()
	if not module.quickMenuOpen then
		return
	end
	local resX, resY = getScreenResolution()
	if quickMenuPos.x == 0 and quickMenuPos.y == 0 then
		quickMenuPos = imgui.ImVec2(resX / 2 - quickMenuSize.x / 2, resY / 2 - quickMenuSize.y / 2)
	end
	if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
		quickMenuPos, quickMenuSize = mimgui_funcs.clampWindowToScreen(quickMenuPos, quickMenuSize, 5)
	end
	imgui.SetNextWindowPos(quickMenuPos, imgui.Cond.Always)
	imgui.SetNextWindowSize(quickMenuSize, imgui.Cond.Always)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(8, 8))
	imgui.PushStyleVarVec2(imgui.StyleVar.FramePadding, imgui.ImVec2(4, 3))
	imgui.PushStyleVarVec2(imgui.StyleVar.ItemSpacing, imgui.ImVec2(4, 4))
	imgui.Begin("Быстрое меню биндер", nil, imgui.WindowFlags.NoCollapse)
	quickMenuPos = imgui.GetWindowPos()
	quickMenuSize = imgui.GetWindowSize()

        local ICON_FOLDER = (fa.FOLDER ~= "" and (fa.FOLDER .. " ") or "")
        local ICON_KEYB = (fa.KEYBOARD ~= "" and (fa.KEYBOARD .. " ") or "")
        local io = imgui.GetIO()

	local function quickMenuItem(label, shortcut, enabled)
		imgui.PushStyleVarVec2(imgui.StyleVar.SelectableTextAlign, imgui.ImVec2(0, 0.5))
		local clicked = imgui.MenuItemBool(label, shortcut, false, enabled)
		imgui.PopStyleVar()
		return clicked
	end

	local function quickBeginMenu(label, enabled)
		local startX = imgui.GetCursorPosX()
		imgui.SetCursorPosX(startX)
		imgui.PushStyleVarVec2(imgui.StyleVar.SelectableTextAlign, imgui.ImVec2(0, 0.5))
		local opened = imgui.BeginMenu(label, enabled)
		imgui.PopStyleVar()
		if not opened then
			imgui.SetCursorPosX(startX)
		end
		return opened, startX
	end

	local function drawRec(node)
		if not isFolderChainVisible(node) then
			return
		end
		for i, hk in ipairs(hotkeys) do
			if
				hk.quick_menu and pathEquals(hk.folderPath, folderFullPath(node)) and
					check_quick_visibility(hk.quick_conditions or {})
			 then
				local label = ICON_KEYB .. (hk.label or ("bind" .. i)) .. "##quick_bind" .. i
				local shortcut = (hk.keys and #hk.keys > 0) and hotkeyToString(hk.keys) or nil
				if quickMenuItem(label, shortcut, hk.enabled) then
					module.enqueueHotkey(hk)
				end
			end
		end
		for _, child in ipairs(node.children or {}) do
			if folderHasQuickBindsVisible(child) then
				local path = table.concat(folderFullPath(child), "/")
				local opened, startX = quickBeginMenu(ICON_FOLDER .. child.name .. "##quick_folder_" .. path, true)
				if opened then
					drawRec(child)
					imgui.EndMenu()
					imgui.SetCursorPosX(startX)
				end
			end
		end
	end

        local visibleFolders = {}
        for _, folder in ipairs(folders) do
                if folderHasQuickBindsVisible(folder) then
                        visibleFolders[#visibleFolders + 1] = folder
                end
        end

        local hoveredQuickMenu = imgui.IsWindowHovered((imgui.HoveredFlags and imgui.HoveredFlags.RootAndChildWindows) or 0)

        local visibleCount = #visibleFolders
        if visibleCount == 0 then
                quickMenuTabIndex = 1
        else
                if quickMenuTabIndex < 1 or quickMenuTabIndex > visibleCount then
                        quickMenuTabIndex = math.min(math.max(quickMenuTabIndex, 1), visibleCount)
                end

                local scrollSteps = quickMenuScrollQueued
                quickMenuScrollQueued = 0

                if hoveredQuickMenu then
                        local wheel = io.MouseWheel
                        if wheel ~= 0 then
                                local steps = math.max(1, math.floor(math.abs(wheel) + 0.5))
                                if wheel > 0 then
                                        scrollSteps = scrollSteps - steps
                                else
                                        scrollSteps = scrollSteps + steps
                                end
                        end
                end

                if scrollSteps ~= 0 then
                        quickMenuTabIndex = quickMenuTabIndex + scrollSteps
                        quickMenuTabIndex = ((quickMenuTabIndex - 1) % visibleCount) + 1
                end
        end

        if imgui.BeginTabBar("##quickbinder_tabbar") then
                for idx, folder in ipairs(visibleFolders) do
                        local tabFlags = 0
                        if idx == quickMenuTabIndex and mimgui_funcs and mimgui_funcs.TabItemFlags and mimgui_funcs.TabItemFlags.SetSelected then
                                tabFlags = flags_or(tabFlags, mimgui_funcs.TabItemFlags.SetSelected)
                        end
                        if imgui.BeginTabItem(folder.name, nil, tabFlags) then
                                if quickMenuTabIndex ~= idx then
                                        quickMenuTabIndex = idx
                                end
                                drawRec(folder)
                                imgui.EndTabItem()
                        end
                end
                imgui.EndTabBar()
        end
	imgui.End()
	imgui.PopStyleVar(3)

	drawToasts()
end

-- === API поиска/управления ===
function module.findBind(name, folder)
	if not name then
		return nil
	end
	name = tostring(name):lower()
	local folderLower = folder and tostring(folder):lower() or nil
	local partial
	for _, hk in ipairs(hotkeys) do
		local inFolder = true
		if folderLower and folderLower ~= "" then
			local fstr = table.concat(hk.folderPath or {}, "/"):lower()
			inFolder = fstr:find(folderLower, 1, true) and true or false
		end
		if inFolder and hk.label then
			local lbl = hk.label:lower()
			if lbl == name then
				return hk
			elseif not partial and lbl:find(name, 1, true) then
				partial = hk
			end
		end
	end
	return partial
end

function module.startBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk and not hk.is_running and hk.enabled then
		module.enqueueHotkey(hk)
		return true
	end
	return false
end

function module.stopBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk and hk.is_running then
		module.stopHotkey(hk)
		return true
	end
	return false
end

function module.disableBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk then
		hk.enabled = false
		module.saveHotkeys()
		return true
	end
	return false
end

function module.enableBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk then
		hk.enabled = true
		module.saveHotkeys()
		return true
	end
	return false
end

function module.pauseBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk and hk.is_running and hk._co_state then
		hk._co_state.paused = true
		return true
	end
	return false
end

function module.unpauseBind(name, folder)
	local hk = module.findBind(name, folder)
	if hk and hk.is_running and hk._co_state then
		hk._co_state.paused = false
		return true
	end
	return false
end

function module.isBindEnded(name, folder)
	local hk = module.findBind(name, folder)
	return not (hk and hk.is_running)
end

function module.setBindSelector(name, folder, state)
	local hk = module.findBind(name, folder)
	if hk then
		hk.quick_menu = not (not state)
		module.saveHotkeys()
		return true
	end
	return false
end

-- === Новые экспортируемые функции для «макросов» ===

-- Запустить бинд по имени с необязательной задержкой (мс)
-- opts: { delay_ms = number, _depth = number }
function module.runBind(name, folder, opts)
	opts = opts or {}
	local depth = tonumber(opts._depth or 0) or 0
	if depth > MAX_BIND_DEPTH then
		pushToast("runBind: превышена глубина (" .. MAX_BIND_DEPTH .. ")", "warn", 3.0)
		return false
	end
	local delay = tonumber(opts.delay_ms or 0) or 0
	local hk = module.findBind(name, folder)
	if not hk then
		pushToast(("Бинд не найден: %s (%s)"):format(tostring(name), tostring(folder or "")), "warn", 3.0)
		return false
	end
	if not hk.enabled then
		pushToast(("Бинд выключен: %s"):format(hk.label or "?"), "warn", 3.0)
		return false
	end
	-- при желании можно передавать глубину дальше; сейчас сам бинд не вызывает другие, так что ок
	module.enqueueHotkey(hk, delay)
	return true
end

-- Случайный бинд из папки/подпапок. folderPathString = "A/B/C"
-- opts: { recursive = bool, delay_ms = number, _depth = number }
function module.runBindRandom(folderPathString, opts)
	opts = opts or {}
	local depth = tonumber(opts._depth or 0) or 0
	if depth > MAX_BIND_DEPTH then
		pushToast("runBindRandom: превышена глубина (" .. MAX_BIND_DEPTH .. ")", "warn", 3.0)
		return false
	end
	local recursive = not (not opts.recursive)
	local delay = tonumber(opts.delay_ms or 0) or 0
	local p = pathFromString(folderPathString)
	local pool = collectBindsInFolder(p, recursive)
	if #pool == 0 then
		pushToast(("Нет биндов в папке: %s"):format(folderPathString or "(все)"), "warn", 3.0)
		return false
	end
	local target = pool[math.random(1, #pool)]
	module.enqueueHotkey(target, delay)
	return true
end

-- === UI: карточки ===
if not _G.moveBindPopup then
	_G.moveBindPopup = {active = false, hkidx = nil}
end

local function drawQuickIndicator(dl, pos_min, enabled)
	local r = 5
	local pad = 8
	local cx = pos_min.x + 138 - pad - r
	local cy = pos_min.y + pad + r
	local col = enabled and imgui.ImVec4(0.95, 0.75, 0.1, 1.0) or imgui.ImVec4(0.35, 0.35, 0.35, 1.0)
	dl:AddCircleFilled(imgui.ImVec2(cx, cy), r, imgui.GetColorU32Vec4(col), 12)
end

local function cloneHotkey(hk)
	local copy = funcs.deepcopy(hk)
	copy.is_running = false
	copy._co_state = nil
	copy.lastActivated = 0
	copy._bools = {}
	copy._cond_bools = {}
	copy._quick_cond_bools = {}
	copy._comboActive = false
	copy._debounce_until = nil
	return copy
end

local function drawBindsGrid()
	local availWidth = imgui.GetContentRegionAvail().x
	local cardWidth, cardHeight = 138, 56
	local spacingX, spacingY = 16, 16
	local columns = math.max(1, math.floor((availWidth + spacingX) / (cardWidth + spacingX)))
	local x0 = imgui.GetCursorScreenPos().x
	local y = imgui.GetCursorScreenPos().y

	local cards = {}
	local curPath = folderFullPath(selectedFolder)
	for i, hk in ipairs(hotkeys) do
		if pathEquals(hk.folderPath, curPath) then
			table.insert(cards, {hk = hk, idx = i})
		end
	end

	for n, card in ipairs(cards) do
		local hk, i = card.hk, card.idx
		local x = x0 + (((n - 1) % columns) * (cardWidth + spacingX))
		local yPos = y + (math.floor((n - 1) / columns)) * (cardHeight + spacingY)

		imgui.SetCursorScreenPos(imgui.ImVec2(x, yPos))
		local pmin = imgui.GetCursorScreenPos()
		local pmax = imgui.ImVec2(pmin.x + cardWidth, pmin.y + cardHeight)
		local hovered = imgui.IsMouseHoveringRect(pmin, pmax)

		imgui.BeginGroup()

		local dl = imgui.GetWindowDrawList()
		local bgcol =
			hovered and imgui.GetStyle().Colors[imgui.Col.FrameBgHovered] or imgui.GetStyle().Colors[imgui.Col.FrameBg]

		dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(bgcol), 8)
		dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Border]), 8, 2)

		drawQuickIndicator(dl, pmin, hk.quick_menu)

		if not hovered then
			local dot_pad, dot_r = 8, 5
			local dot_cx = pmin.x + cardWidth - dot_pad - dot_r
			local text_start = pmin.x + 11
			local bolt_w = hk.quick_menu and imgui.CalcTextSize(fa.BOLT).x or 0
			local bolt_x = dot_cx - dot_r - 4 - bolt_w
			local max_text_w = bolt_x - text_start - 4
			local label = hk.label or ("bind" .. i)
			if imgui.CalcTextSize(label).x > max_text_w then
				local ell = "..."
				local ell_w = imgui.CalcTextSize(ell).x
				local t = label
				while #t > 0 and imgui.CalcTextSize(t).x > (max_text_w - ell_w) do
					t = t:sub(1, -2)
				end
				label = t .. ell
			end
			imgui.SetCursorScreenPos(imgui.ImVec2(text_start, pmin.y + 7))
			imgui.TextColored(imgui.GetStyle().Colors[imgui.Col.Text], label)
			if hk.quick_menu then
				imgui.SetCursorScreenPos(imgui.ImVec2(bolt_x, pmin.y + 7))
				imgui.TextColored(imgui.GetStyle().Colors[imgui.Col.Text], fa.BOLT)
			end
			imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + 11, pmin.y + 25))
			imgui.TextDisabled(fa.LIST_UL .. " " .. tostring(#(hk.messages or {})))
			if hk.command and hk.command ~= "" then
				imgui.SameLine()
				imgui.TextDisabled(fa.TERMINAL .. " " .. hk.command)
			end
			imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + 11, pmin.y + 39))
			if #hk.keys > 0 then
				imgui.TextDisabled(fa.KEYBOARD .. " " .. hotkeyToString(hk.keys))
			end
		else
			local padX = 6
			local spacing = imgui.GetStyle().ItemSpacing.x
			local buttonW = (cardWidth - padX * 2 - spacing * 3) / 4
			local buttonH = cardHeight - 16
			local btnY = pmin.y + 8
			imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + padX, btnY))
			if imgui.Button(fa.PEN .. "##edit" .. i, imgui.ImVec2(buttonW, buttonH)) then
				editHotkey.active = true
				editHotkey.idx = i
			end
			imgui.SameLine(0, spacing)
			if not hk.is_running then
				if imgui.Button(fa.PLAY .. "##play" .. i, imgui.ImVec2(buttonW, buttonH)) then
					module.enqueueHotkey(hk)
				end
			else
				if hk._co_state and hk._co_state.paused then
					if imgui.Button(fa.PLAY .. "##resume" .. i, imgui.ImVec2(buttonW, buttonH)) then
						hk._co_state.paused = false
					end
				else
					if imgui.Button(fa.PAUSE .. "##pause" .. i, imgui.ImVec2(buttonW, buttonH)) then
						hk._co_state = hk._co_state or {}
						hk._co_state.paused = true
					end
				end
				imgui.SameLine(0, spacing)
				if imgui.Button(fa.STOP .. "##stop" .. i, imgui.ImVec2(buttonW, buttonH)) then
					module.stopHotkey(hk)
				end
			end
			imgui.SameLine(0, spacing)
			if imgui.Button(fa.TRASH .. "##del" .. i, imgui.ImVec2(buttonW, buttonH)) then
				table.remove(hotkeys, i)
				module.saveHotkeys()
				imgui.EndGroup()
				goto after_card
			end
			imgui.SameLine(0, spacing)
			if imgui.Button(fa.BARS .. "##ctx" .. i, imgui.ImVec2(buttonW, buttonH)) then
				imgui.OpenPopup("ctx_card_" .. i)
			end
		end

		if imgui.BeginPopup("ctx_card_" .. i) then
			if imgui.MenuItemBool("Дублировать", false) then
				local newhk = cloneHotkey(hk)
				table.insert(hotkeys, i + 1, newhk)
				module.saveHotkeys()
			end
			if imgui.MenuItemBool("Переместить в...", false) then
				_G.moveBindPopup.active = true
				_G.moveBindPopup.hkidx = i
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		imgui.EndGroup()

		imgui.SetCursorScreenPos(pmin)
		imgui.InvisibleButton("##card_area" .. i, imgui.ImVec2(cardWidth, cardHeight))
		if imgui.BeginDragDropSource() then
			local payload = ffi.new("int[1]", i)
			imgui.SetDragDropPayload("BINDER_HOTKEY", payload, ffi.sizeof(payload))
			imgui.Text(hk.label or ("bind" .. i))
			imgui.EndDragDropSource()
		end
		if imgui.BeginDragDropTarget() then
			local payload = imgui.AcceptDragDropPayload()
			if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
				local src_idx = ffi.cast("int*", payload.Data)[0]
				local dst_idx = i
				if src_idx >= 1 and src_idx <= #hotkeys and src_idx ~= dst_idx then
					local moved = table.remove(hotkeys, src_idx)
					if dst_idx > src_idx then
						dst_idx = dst_idx - 1
					end
					table.insert(hotkeys, dst_idx, moved)
					module.saveHotkeys()
				end
			end
			imgui.EndDragDropTarget()
		end

		::after_card::
	end

	-- Кнопка "+"
	local add_x = x0 + ((#cards % columns) * (cardWidth + spacingX))
	local add_y = y + (math.floor((#cards) / columns)) * (cardHeight + spacingY)
	imgui.SetCursorScreenPos(imgui.ImVec2(add_x, add_y))
	imgui.BeginGroup()
	local pmin = imgui.GetCursorScreenPos()
	local pmax = imgui.ImVec2(pmin.x + cardWidth, pmin.y + cardHeight)
	local dl = imgui.GetWindowDrawList()
	dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBg]), 8)
	dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Border]), 8, 2)
	imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + (cardWidth - 32) / 2, pmin.y + (cardHeight - 32) / 2))
	if imgui.Button(fa.SQUARE_PLUS .. "##add", imgui.ImVec2(32, 32)) then
		local hk = newHotkeyBase()
		hk.folderPath = folderFullPath(selectedFolder)
		table.insert(hotkeys, hk)
		module.saveHotkeys()
	end
	imgui.EndGroup()

	-- Popup перемещения
	if _G.moveBindPopup.active then
		imgui.OpenPopup("move_bind_popup")
		_G.moveBindPopup.active = false
	end
	if imgui.BeginPopup("move_bind_popup") then
		imgui.Text("Переместить бинд:")
		local function drawTree(node, path)
			path = path or {node.name}
			if imgui.Selectable(table.concat(path, "/"), false) then
				local idx = _G.moveBindPopup.hkidx
				if hotkeys[idx] then
					hotkeys[idx].folderPath = {table.unpack(path)}
					module.saveHotkeys()
				end
				imgui.CloseCurrentPopup()
			end
			for _, child in ipairs(node.children or {}) do
				local cp = {}
				for _, v in ipairs(path) do
					cp[#cp + 1] = v
				end
				cp[#cp + 1] = child.name
				drawTree(child, cp)
			end
		end
		for _, folder in ipairs(folders) do
			drawTree(folder, {folder.name})
		end
		if imgui.Button("Отмена") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

-- === ВАЛИДАТОР ===
local function folderExistsByPath(path)
	if not path or #path == 0 then
		return false
	end
	local nodeList = folders
	local node = nil
	for i, name in ipairs(path) do
		local found = nil
		for _, f in ipairs(nodeList) do
			if f.name == name then
				found = f
				break
			end
		end
		if not found then
			return false
		end
		node = found
		nodeList = found.children
	end
	return true
end

local function normalizeCombo(keys)
	local t = {}
	for _, k in ipairs(keys or {}) do
		t[#t + 1] = normalizeKey(k)
	end
	table.sort(
		t,
		function(a, b)
			return a < b
		end
	)
	return table.concat(t, ",")
end

local function validateHotkeyEdit(hkEdit, idxSelf)
	local errs = {}

	if not hkEdit.editLabel or hkEdit.editLabel:gsub("%s+", "") == "" then
		errs[#errs + 1] = "Название бинда пустое"
	end

	local fpath = hotkeys[idxSelf] and hotkeys[idxSelf].folderPath or {folders[1].name}
	if not folderExistsByPath(fpath) then
		errs[#errs + 1] = "Целевая папка не существует: " .. table.concat(fpath, "/")
	end

	if hkEdit.editMsgs then
		for i, m in ipairs(hkEdit.editMsgs) do
			local v = tonumber(m.interval)
			if m.interval ~= "" and (not v or v < 0) then
				errs[#errs + 1] = ("Строка %d: неверный интервал"):format(i)
			end
		end
	end

	if hkEdit.editRepeatMode and hkEdit.editRepeatInterval and hkEdit.editRepeatInterval ~= "" then
		local v = tonumber(hkEdit.editRepeatInterval)
		if not v or v < 50 then
			errs[#errs + 1] = "Интервал повтора должен быть числом ≥ 50 мс"
		end
	end

	if hkEdit.editTriggerEnabled and (not hkEdit.editTextTrigger or hkEdit.editTextTrigger:gsub("%s+", "") == "") then
		errs[#errs + 1] = "Текст триггера пустой"
	end
	if hkEdit.editCommandEnabled and (not hkEdit.editCommand or hkEdit.editCommand:gsub("%s+", "") == "") then
		errs[#errs + 1] = "Команда пустая"
	end

	local myCombo = normalizeCombo(hkEdit.editKeys or {})
	if myCombo ~= "" then
		for j, other in ipairs(hotkeys) do
			if j ~= idxSelf and other.enabled then
				if normalizeCombo(other.keys) == myCombo then
					errs[#errs + 1] = "Дублируется комбинация клавиш с биндом: " .. (other.label or ("#" .. j))
					break
				end
			end
		end
	end

	return #errs == 0, errs
end

-- === Редактор бинда ===
local function ensureEditBuffers(hk)
	if not hk.editMsgs then
		hk.editMsgs = {}
		for _, m in ipairs(hk.messages or {}) do
			table.insert(hk.editMsgs, {text = m.text or "", interval = tostring(m.interval or 0), method = m.method or 0})
		end
	end
	if not hk.editLabel then
		hk.editLabel = hk.label or ""
	end
	if not hk.editCommand then
	       hk.editCommand = hk.command or ""
	end
	if hk.editCommandEnabled == nil then
	       hk.editCommandEnabled = hk.command_enabled or false
	end
	if not hk.editKeys then
		hk.editKeys = {table.unpack(hk.keys or {})}
	end
	if not hk.editConditions then
		hk.editConditions = {}
		for i = 1, cond_count do
			hk.editConditions[i] = hk.conditions and hk.conditions[i] or false
		end
	end
	if hk.editRepeatMode == nil then
		hk.editRepeatMode = hk.repeat_mode or false
	end
	if hk.editQuickMenu == nil then
		hk.editQuickMenu = hk.quick_menu or false
	end
	if hk.editRepeatInterval == nil then
		hk.editRepeatInterval = hk.repeat_interval_ms and tostring(hk.repeat_interval_ms) or ""
	end
	if hk.editTextTrigger == nil then
		hk.editTextTrigger = hk.text_trigger and hk.text_trigger.text or ""
	end
	if hk.editTriggerEnabled == nil then
		hk.editTriggerEnabled = hk.text_trigger and hk.text_trigger.enabled or false
	end
	if hk.editTriggerPattern == nil then
		hk.editTriggerPattern = hk.text_trigger and hk.text_trigger.pattern or false
	end
	if not hk.editQuickConditions then
		hk.editQuickConditions = {}
		for i = 1, quick_cond_count do
			hk.editQuickConditions[i] = hk.quick_conditions and hk.quick_conditions[i] or false
		end
	end
	if hk.editBulkMethod == nil then
		hk.editBulkMethod = hk.editMsgs[1] and hk.editMsgs[1].method or 0
	end
	if hk.editBulkInterval == nil then
		hk.editBulkInterval = hk.editMsgs[1] and hk.editMsgs[1].interval or "0"
	end
	if hk.editMultiline == nil then
		hk.editMultiline = false
	end
	if not hk.editMultiText then
		local lines = {}
		for _, m in ipairs(hk.editMsgs) do
			table.insert(lines, m.text or "")
		end
		hk.editMultiText = table.concat(lines, "\n")
	end
	hk._bools.multi = ensure_bool(hk._bools.multi, hk.editMultiline)
	hk._bools.quick = ensure_bool(hk._bools.quick, hk.editQuickMenu)
	hk._bools.rep = ensure_bool(hk._bools.rep, hk.editRepeatMode)
	hk._bools.triggerEnabled = ensure_bool(hk._bools.triggerEnabled, hk.editTriggerEnabled)
	hk._bools.triggerPattern = ensure_bool(hk._bools.triggerPattern, hk.editTriggerPattern)
	hk._bools.commandEnabled = ensure_bool(hk._bools.commandEnabled, hk.editCommandEnabled)
end

local function openComboPopupNow()
	imgui.OpenPopup("Назначить новую комбинацию")
	combo_recording = true
	combo_keys = {}
end

local function drawKeyCapturePopup(hk)
	if
		imgui.BeginPopupModal(
			"Назначить новую комбинацию",
			nil,
			imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoMove
		)
	 then
		imgui.Text(fa.KEYBOARD .. "	" .. "Нажмите нужные клавиши")
		imgui.Text(hotkeyToString(combo_keys))
		if imgui.Button(fa.XMARK .. " " .. "[CANCEL]") then
			combo_recording = false
			combo_keys = {}
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button(fa.FLOPPY_DISK .. " [SAVE]") then
			hk.editKeys = {}
			for _, k in ipairs(combo_keys) do
				table.insert(hk.editKeys, k)
			end
			combo_recording = false
			combo_keys = {}
			imgui.CloseCurrentPopup()
			module.saveHotkeys()
		end
		imgui.EndPopup()
		return true
	end
	return false
end

local function drawConditionsPopup(hk)
	if imgui.BeginPopup("conditions_popup") then
		imgui.Text(fa.CHECK_DOUBLE .. " " .. "Условия активации")
		for i = 1, cond_count do
			hk._cond_bools[i] = ensure_bool(hk._cond_bools[i], hk.editConditions[i])
			if imgui.Checkbox(cond_labels[i], hk._cond_bools[i]) then
				hk.editConditions[i] = hk._cond_bools[i][0]
				module.saveHotkeys()
			end
		end
		if imgui.Button(fa.CHECK .. " " .. "[OK]") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
		return true
	end
	return false
end

local function drawQuickConditionsPopup(hk)
	if imgui.BeginPopup("quick_conditions_popup") then
		imgui.Text(fa.BOLT .. " " .. "Появление в быстром меню")
		for i = 1, quick_cond_count do
			hk._quick_cond_bools[i] = ensure_bool(hk._quick_cond_bools[i], hk.editQuickConditions[i])
			if imgui.Checkbox(quick_cond_labels[i], hk._quick_cond_bools[i]) then
				hk.editQuickConditions[i] = hk._quick_cond_bools[i][0]
				module.saveHotkeys()
			end
		end
		if imgui.Button(fa.CHECK .. " " .. "[OK]") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
		return true
	end
	return false
end

local function drawEditHotkey(idx)
	local hk = hotkeys[idx]
	if not hk then
		return
	end
	ensureEditBuffers(hk)

	-- Шапка
	imgui.BeginChild("edit_header", imgui.ImVec2(0, 25), false)
	if imgui.Button(fa.ARROW_LEFT .. " Назад") then
		hk.editMsgs, hk.editLabel, hk.editKeys, hk.editCommand, hk.editConditions = nil, nil, nil, nil, nil
		hk.editRepeatMode, hk.editQuickMenu, hk.editRepeatInterval, hk.editQuickConditions = nil, nil, nil, nil
		hk.editBulkMethod, hk.editBulkInterval, hk.editMultiline, hk.editMultiText = nil, nil, nil, nil
		hk.editTextTrigger, hk.editTriggerEnabled, hk.editTriggerPattern = nil, nil, nil
		editHotkey.active = false
		return
	end
	imgui.SameLine()
	imgui.TextColored(imgui.GetStyle().Colors[imgui.Col.Text], fa.PEN .. "	" .. "Редактирование бинда")
	imgui.EndChild()

	imgui.BeginChild("edit_main", imgui.ImVec2(0, -52), true)
	-- Название
	imgui.PushItemWidth(260)
	local labelBuf = imgui.new.char[256](hk.editLabel)
	if
		imgui.InputText(
			"Название бинда##edit_label",
			labelBuf,
			ffi.sizeof(labelBuf),
			flags_or(imgui.InputTextFlags.AutoSelectAll)
		)
	 then
		hk.editLabel = ffi.string(labelBuf)
	end
	imgui.PopItemWidth()
	
	-- Комбо
	imgui.TextDisabled(fa.KEYBOARD .. " " .. "Комбинация:")
	imgui.SameLine()
	imgui.PushItemWidth(170)
	local keyStr = hotkeyToString(hk.editKeys)
	if imgui.Button(keyStr .. "##editkeys", imgui.ImVec2(150, 0)) then
		open_combo_popup = true
	end
	imgui.PopItemWidth()

	-- Быстрое меню
	hk._bools.quick = ensure_bool(hk._bools.quick, hk.editQuickMenu)
	if imgui.Checkbox(fa.BOLT .. " " .. "Быстрое меню##quick_menu", hk._bools.quick) then
		hk.editQuickMenu = hk._bools.quick[0]
		module.saveHotkeys()
	end
	
	imgui.SameLine()
	if imgui.Button(fa.SLIDERS .. " " .. "Условия") then
		open_conditions_popup = true
	end

	imgui.SameLine()
	if imgui.Button(fa.BOLT .. " " .. "Условия быстрого меню") then
		open_quick_conditions_popup = true
	end

	-- Попапы
	if open_combo_popup then
		openComboPopupNow()
		open_combo_popup = false
	end
	drawKeyCapturePopup(hk)
	if open_conditions_popup then
		imgui.OpenPopup("conditions_popup")
		open_conditions_popup = false
	end
	drawConditionsPopup(hk)
	if open_quick_conditions_popup then
		imgui.OpenPopup("quick_conditions_popup")
		open_quick_conditions_popup = false
	end
	drawQuickConditionsPopup(hk)

	-- Команда
	if imgui.Checkbox("##cmd_enable", hk._bools.commandEnabled) then
		hk.editCommandEnabled = hk._bools.commandEnabled[0]
	end
	imgui.SameLine()
	imgui.PushItemWidth(360)
	local cmdBuf = imgui.new.char[256](hk.editCommand)
	if
		imgui.InputText(
			fa.TERMINAL .. " " .. "Команда##edit_command",
			cmdBuf,
			ffi.sizeof(cmdBuf),
			flags_or(imgui.InputTextFlags.AutoSelectAll)
		)
	 then
		hk.editCommand = ffi.string(cmdBuf)
	end
	imgui.PopItemWidth()


	-- Повторный режим
	hk._bools.rep = ensure_bool(hk._bools.rep, hk.editRepeatMode)
	if imgui.Checkbox(fa.REPEAT .. " " .. "Повтор##repeat", hk._bools.rep) then
		hk.editRepeatMode = hk._bools.rep[0]
		module.saveHotkeys()
	end
	
	-- Интервал повтора
	imgui.SameLine()
	imgui.PushItemWidth(180)
	local rbuf = imgui.new.char[32](hk.editRepeatInterval or "")
	if
		imgui.InputText(
			"Интервал повтора, мс##repInt",
			rbuf,
			ffi.sizeof(rbuf),
			flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
		)
	 then
		local s = ffi.string(rbuf)
		if s == "" or tonumber(s) then
			hk.editRepeatInterval = s
		end
	end
	imgui.PopItemWidth()

	-- Триггер по тексту
	imgui.PushItemWidth(360)
	local trigBuf = imgui.new.char[256](hk.editTextTrigger or "")
	if
		imgui.InputText(
			"Текст триггера##text_trigger",
			trigBuf,
			ffi.sizeof(trigBuf),
			flags_or(imgui.InputTextFlags.AutoSelectAll)
		)
	 then
		hk.editTextTrigger = ffi.string(trigBuf)
	end
	imgui.PopItemWidth()
	imgui.SameLine()
	if imgui.Checkbox("По тексту##trigger_enable", hk._bools.triggerEnabled) then
		hk.editTriggerEnabled = hk._bools.triggerEnabled[0]
		module.saveHotkeys()
	end
	imgui.SameLine()
	if imgui.Checkbox("Lua-паттерн##trigger_pattern", hk._bools.triggerPattern) then
		hk.editTriggerPattern = hk._bools.triggerPattern[0]
		module.saveHotkeys()
	end

	imgui.Separator()
	imgui.Text(fa.LIST_UL .. "	  " .. "Сообщения")
	imgui.SameLine()
	if imgui.Checkbox("Мульти-ввод##multi_mode", hk._bools.multi) then
		hk.editMultiline = hk._bools.multi[0]
		if hk.editMultiline then
			local lines = {}
			for _, m in ipairs(hk.editMsgs) do
				table.insert(lines, m.text or "")
			end
			hk.editMultiText = table.concat(lines, "\n")
		else
			hk.editMsgs = {}
			for line in (hk.editMultiText or ""):gmatch("[^\r\n]+") do
				if line ~= "" then
					table.insert(hk.editMsgs, {text = line, interval = hk.editBulkInterval or "0", method = hk.editBulkMethod or 0})
				end
			end
			module.saveHotkeys()
		end
	end
	imgui.SameLine()
	imgui.PushItemWidth(120)
	local allMBuf = imgui.new.int(hk.editBulkMethod or 0)
	if imgui.Combo("Куда##allm", allMBuf, send_labels_ffi, #send_labels) then
		hk.editBulkMethod = allMBuf[0]
		if not hk.editMultiline then
			for _, m in ipairs(hk.editMsgs) do
				m.method = hk.editBulkMethod
			end
			module.saveHotkeys()
		end
	end
	imgui.PopItemWidth()
	imgui.SameLine()
	imgui.PushItemWidth(70)
	local allIBuf = imgui.new.char[16](tostring(hk.editBulkInterval or "0"))
	if
		imgui.InputText(
			"мс##alli",
			allIBuf,
			ffi.sizeof(allIBuf),
			flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
		)
	 then
		local s = ffi.string(allIBuf)
		if s == "" or tonumber(s) then
			hk.editBulkInterval = s
			if not hk.editMultiline then
				for _, m in ipairs(hk.editMsgs) do
					m.interval = s
				end
				module.saveHotkeys()
			end
		end
	end
	imgui.PopItemWidth()

	if hk.editMultiline then
		imgui.PushItemWidth(-1)
		local buf = imgui.new.char[4096](hk.editMultiText or "")
		if imgui.InputTextMultiline("##multi_text", buf, ffi.sizeof(buf), imgui.ImVec2(0, 200)) then
			hk.editMultiText = ffi.string(buf)
		end
		imgui.PopItemWidth()
	else
		if imgui.Button(fa.SQUARE_PLUS .. " " .. "Добавить строку") then
			table.insert(hk.editMsgs, {text = "", interval = "0", method = 0})
			module.saveHotkeys()
		end
		imgui.SameLine()
		imgui.TextDisabled("Подсказки: можно использовать [waitif(expr)]")

		imgui.Spacing()
		imgui.BeginChild("messages_list", imgui.ImVec2(0, 0), false)
		for i, m in ipairs(hk.editMsgs) do
			imgui.PushIDStr("row" .. i)
			imgui.BeginGroup()
			if imgui.Button(fa.ARROW_UP .. "##up", imgui.ImVec2(28, 20)) and i > 1 then
				hk.editMsgs[i], hk.editMsgs[i - 1] = hk.editMsgs[i - 1], hk.editMsgs[i]
				module.saveHotkeys()
			end
			imgui.SameLine()
			if imgui.Button(fa.ARROW_DOWN .. "##down", imgui.ImVec2(28, 20)) and i < #hk.editMsgs then
				hk.editMsgs[i], hk.editMsgs[i + 1] = hk.editMsgs[i + 1], hk.editMsgs[i]
				module.saveHotkeys()
			end
			imgui.SameLine()

			imgui.PushItemWidth(430)
			local tbuf = imgui.new.char[256](m.text or "")
			if imgui.InputText("##t", tbuf, ffi.sizeof(tbuf), flags_or(imgui.InputTextFlags.AutoSelectAll)) then
				m.text = ffi.string(tbuf)
				module.saveHotkeys()
			end
			imgui.PopItemWidth()

			imgui.SameLine()
			imgui.PushItemWidth(50)
			local ibuf = imgui.new.char[16](tostring(m.interval or "0"))
			if
				imgui.InputText(
					"мс##i",
					ibuf,
					ffi.sizeof(ibuf),
					flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
				)
			 then
				local s = ffi.string(ibuf)
				if tonumber(s) then
					m.interval = s
				elseif s == "" then
					m.interval = "0"
				end
				module.saveHotkeys()
			end
			imgui.PopItemWidth()

			imgui.SameLine()
			imgui.PushItemWidth(100)
			local mbuf = imgui.new.int(m.method or 0)
			if imgui.Combo("##m", mbuf, send_labels_ffi, #send_labels) then
				m.method = mbuf[0]
				module.saveHotkeys()
			end
			imgui.PopItemWidth()

			imgui.SameLine()
			if imgui.Button(fa.TRASH .. "##del", imgui.ImVec2(46, 20)) then
				table.remove(hk.editMsgs, i)
				module.saveHotkeys()
				imgui.EndGroup()
				imgui.PopID()
				goto continue_msgs
			end
			imgui.EndGroup()
			imgui.PopID()
			::continue_msgs::
		end
		imgui.EndChild()
	end

	imgui.EndChild()

	-- Низ
	imgui.Separator()
	if imgui.Button(fa.FLOPPY_DISK .. " [SAVE]", imgui.ImVec2(120, 0)) then
		local ok, errs = validateHotkeyEdit(hk, idx)
		if not ok then
			for _, e in ipairs(errs) do
				pushToast(e, "err", 4.0)
			end
			return
		end

hk.label = hk.editLabel
hk.command = hk.editCommand
hk.command_enabled = hk.editCommandEnabled
		hk.keys = {table.unpack(hk.editKeys)}
		hk.messages = {}
		for _, m in ipairs(hk.editMsgs) do
			table.insert(
				hk.messages,
				{
					text = m.text,
					interval = tonumber(m.interval) or 0,
					method = tonumber(m.method) or 0
				}
			)
		end
		hk.conditions = {}
		for i = 1, cond_count do
			hk.conditions[i] = hk.editConditions[i]
		end
		hk.repeat_mode = hk.editRepeatMode
		hk.quick_menu = hk.editQuickMenu
		local ri = tonumber(hk.editRepeatInterval)
		hk.repeat_interval_ms = ri and math.max(ri, 50) or nil
		hk.quick_conditions = {}
		for i = 1, quick_cond_count do
			hk.quick_conditions[i] = hk.editQuickConditions[i]
		end
		hk.text_trigger = {
			text = hk.editTextTrigger,
			enabled = hk.editTriggerEnabled,
			pattern = hk.editTriggerPattern
		}

hk.editMsgs, hk.editLabel, hk.editKeys, hk.editCommand, hk.editConditions, hk.editCommandEnabled = nil, nil, nil, nil, nil, nil
		hk.editRepeatMode, hk.editQuickMenu, hk.editRepeatInterval, hk.editQuickConditions = nil, nil, nil, nil
		hk.editBulkMethod, hk.editBulkInterval, hk.editMultiline, hk.editMultiText = nil, nil, nil, nil
		hk.editTextTrigger, hk.editTriggerEnabled, hk.editTriggerPattern = nil, nil, nil
		editHotkey.active = false
		module.saveHotkeys()
		pushToast("Бинд сохранен: " .. (hk.label or ""), "ok", 2.5)
		return
	end
	imgui.SameLine()
if imgui.Button(fa.XMARK .. "[CANCEL]", imgui.ImVec2(120, 0)) then
hk.editMsgs, hk.editLabel, hk.editKeys, hk.editCommand, hk.editConditions, hk.editCommandEnabled = nil, nil, nil, nil, nil, nil
hk.editRepeatMode, hk.editQuickMenu, hk.editRepeatInterval, hk.editQuickConditions = nil, nil, nil, nil
hk.editBulkMethod, hk.editBulkInterval, hk.editMultiline, hk.editMultiText = nil, nil, nil, nil
hk.editTextTrigger, hk.editTriggerEnabled, hk.editTriggerPattern = nil, nil, nil
editHotkey.active = false
return
end
	imgui.SameLine()
	if imgui.Button(fa.TRASH .. " " .. "[DEL]", imgui.ImVec2(120, 0)) then
		table.remove(hotkeys, idx)
		editHotkey.active = false
		module.saveHotkeys()
		return
	end
	if tags and tags.showTagsWindow then
		imgui.SameLine()
		if imgui.Button(fa.TAGS .. " Переменные##open_tags") then
			tags.showTagsWindow[0] = true
		end
	end
end

-- === Вкладки папок (с условиями быстрого меню) ===
local function drawFolderTabs()
	local tabHeight = 22
	local tabPad = 2

	local function drawFolderRow(folder, isRoot)
		imgui.BeginChild("folders_row_" .. (folder and folder.name or "root"), imgui.ImVec2(0, tabHeight + 6), false)
		local list = isRoot and folders or (folder and folder.children or {})
		for i, f in ipairs(list) do
			imgui.SameLine(0, tabPad)
			local isSel = (selectedFolder == f)
			if isSel then
				imgui.PushStyleColor(imgui.Col.Button, imgui.GetStyle().Colors[imgui.Col.FrameBgHovered])
			end
			if imgui.Button(fa.FOLDER .. " " .. f.name .. "##tab" .. tostring(f), imgui.ImVec2(110, tabHeight)) then
				selectedFolder = f
			end
			if imgui.BeginDragDropTarget() then
				local payload = imgui.AcceptDragDropPayload()
				if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
					local src_idx = ffi.cast("int*", payload.Data)[0]
					if hotkeys[src_idx] then
						hotkeys[src_idx].folderPath = folderFullPath(f)
						module.saveHotkeys()
					end
				end
				imgui.EndDragDropTarget()
			end
			if isSel then
				imgui.PopStyleColor()
			end
			imgui.SameLine(0, 0)
			if imgui.Button(fa.ELLIPSIS_VERTICAL .. "##gear" .. tostring(f), imgui.ImVec2(tabHeight, tabHeight)) then
				imgui.OpenPopup("popup_gear_" .. tostring(f))
			end
			if imgui.BeginPopup("popup_gear_" .. tostring(f)) then
				imgui.Text(fa.FOLDER .. " " .. f.name)
				imgui.Separator()
				-- Добавить подпапку
				imgui.Text(fa.SQUARE_PLUS .. " " .. "Добавить подпапку")
				local subBuf = labelInputs["addsub" .. tostring(f)] or imgui.new.char[256]()
				if
					imgui.InputText(
						"##new_sub" .. tostring(f),
						subBuf,
						ffi.sizeof(subBuf),
						flags_or(imgui.InputTextFlags.AutoSelectAll)
					)
				 then
					labelInputs["addsub" .. tostring(f)] = subBuf
				end
				imgui.SameLine()
				if imgui.SmallButton(fa.SQUARE_PLUS .. "##addsubok" .. tostring(f)) then
					local subName = sanitizeFolderName(ffi.string(subBuf))
					if #subName > 0 and folderNameUnique(f.children, subName) then
                                                table.insert(f.children, {name = subName, children = {}, parent = f, quick_conditions = {}, quick_menu = true})
						imgui.StrCopy(subBuf, "", ffi.sizeof(subBuf))
						module.saveHotkeys()
					end
				end
				imgui.Separator()
				-- Переименовать
				imgui.Text(fa.PEN .. " " .. "Переименовать")
				local renameBuf = labelInputs["ren" .. tostring(f)] or imgui.new.char[256](f.name)
				if
					imgui.InputText(
						"##ren" .. tostring(f),
						renameBuf,
						ffi.sizeof(renameBuf),
						flags_or(imgui.InputTextFlags.AutoSelectAll)
					)
				 then
					labelInputs["ren" .. tostring(f)] = renameBuf
				end
				imgui.SameLine()
				if imgui.SmallButton(fa.FLOPPY_DISK .. "##save_rename" .. tostring(f)) then
					local newName = sanitizeFolderName(ffi.string(renameBuf))
					if #newName > 0 and folderNameUnique(f.parent and f.parent.children or folders, newName) then
						f.name = newName
						module.saveHotkeys()
						imgui.CloseCurrentPopup()
					end
				end

                                imgui.Separator()
                                local quickMenuLabel =
                                        (fa.BOLT and fa.BOLT .. " " or "") .. "Быстрое меню##folder_quick_menu" .. tostring(f)
                                f._quick_menu_bool = ensure_bool(f._quick_menu_bool, f.quick_menu ~= false)
                                if imgui.Checkbox(quickMenuLabel, f._quick_menu_bool) then
                                        f.quick_menu = f._quick_menu_bool[0]
                                        module.saveHotkeys()
                                end

                                local headerLabel =
                                        (fa.BOLT and fa.BOLT .. " " or "")
                                        .. "Папка: условия быстрого меню##folder_quick_conditions"
                                        .. tostring(f)
                                imgui.SetNextItemOpen(false, imgui.Cond.Once)
                                if imgui.CollapsingHeader(headerLabel) then
                                        f._quick_cond_bools = f._quick_cond_bools or {}
                                        for ii = 1, quick_cond_count do
                                                local cur = (f.quick_conditions and f.quick_conditions[ii]) or false
                                                f._quick_cond_bools[ii] = ensure_bool(f._quick_cond_bools[ii], cur)
                                                if
                                                        imgui.Checkbox(
                                                                quick_cond_labels[ii] .. "##fq" .. ii .. tostring(f),
                                                                f._quick_cond_bools[ii]
                                                        )
                                                 then
                                                        f.quick_conditions = f.quick_conditions or {}
                                                        f.quick_conditions[ii] = f._quick_cond_bools[ii][0]
                                                        module.saveHotkeys()
                                                end
                                        end
                                end

				if not isRoot then
					imgui.Separator()
					if imgui.SmallButton(fa.TRASH .. " Удалить папку") then
						removeFolder(f.parent and f.parent.children or folders, f)
						if selectedFolder == f then
							selectedFolder = f.parent or folders[1]
						end
						module.saveHotkeys()
						imgui.CloseCurrentPopup()
					end
				end
				imgui.EndPopup()
			end
		end

		imgui.SameLine(0, tabPad)
		if
			imgui.Button(fa.SQUARE_PLUS .. "##add_sub" .. (folder and folder.name or "root"), imgui.ImVec2(tabHeight, tabHeight))
		 then
			imgui.OpenPopup("popup_add_sub_" .. (folder and folder.name or "root"))
		end
		if imgui.BeginPopup("popup_add_sub_" .. (folder and folder.name or "root")) then
			imgui.Text(fa.SQUARE_PLUS .. "Добавить подпапку")
			local bufkey = "quickadd_" .. (folder and folder.name or "root")
			local subBuf = labelInputs[bufkey] or imgui.new.char[256]()
			if
				imgui.InputText(
					"##input_quickadd_" .. (folder and folder.name or "root"),
					subBuf,
					ffi.sizeof(subBuf),
					flags_or(imgui.InputTextFlags.AutoSelectAll)
				)
			 then
				labelInputs[bufkey] = subBuf
			end
			imgui.SameLine()
			if imgui.SmallButton(fa.SQUARE_PLUS .. "##quickaddok" .. (folder and folder.name or "root")) then
				local name = sanitizeFolderName(ffi.string(subBuf))
                                local list = isRoot and folders or (folder and folder.children or {})
                                if #name > 0 and folderNameUnique(list, name) then
                                        table.insert(list, {name = name, children = {}, parent = folder, quick_conditions = {}, quick_menu = true})
					imgui.StrCopy(subBuf, "", ffi.sizeof(subBuf))
					module.saveHotkeys()
				end
			end
			imgui.EndPopup()
		end
		imgui.EndChild()
	end

	drawFolderRow(nil, true)
	local cur = selectedFolder
	while cur and #cur.children > 0 do
		drawFolderRow(cur, false)
		local found = false
		for _, c in ipairs(cur.children) do
			if selectedFolder == c then
				cur = c
				found = true
				break
			end
		end
		if not found then
			break
		end
	end

	local path = folderFullPath(selectedFolder)
	imgui.Text("Открыто: ")
	imgui.SameLine()
	local node = nil
	local style = imgui.GetStyle()
	local io = imgui.GetIO()
	for i, name in ipairs(path) do
		if i == 1 then
			node = nil
			for _, f in ipairs(folders) do
				if f.name == name then
					node = f
					break
				end
			end
		else
			for _, c in ipairs(node.children) do
				if c.name == name then
					node = c
					break
				end
			end
		end
		local pos = imgui.GetCursorScreenPos()
		imgui.GetWindowDrawList():AddText(pos, imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]), name)
		local text_size = imgui.CalcTextSize(name)
		local rect_min = pos
		local rect_max = imgui.ImVec2(pos.x + text_size.x, pos.y + text_size.y)
		local hovered =
			io.MousePos.x >= rect_min.x and io.MousePos.x <= rect_max.x and io.MousePos.y >= rect_min.y and
			io.MousePos.y <= rect_max.y
		if hovered then
			imgui.GetWindowDrawList():AddLine(
				imgui.ImVec2(rect_min.x, rect_max.y),
				imgui.ImVec2(rect_max.x, rect_max.y),
				imgui.GetColorU32Vec4(style.Colors[imgui.Col.ButtonHovered]),
				2
			)
			imgui.SetMouseCursor(imgui.MouseCursor.Hand)
			if imgui.IsMouseClicked(0) then
				selectedFolder = node
			end
		end
		imgui.SetCursorScreenPos(imgui.ImVec2(rect_max.x, pos.y))
		if i < #path then
			imgui.Text(" / ")
			imgui.SameLine()
		end
	end
end

-- === Главное окно ===
function module.DrawBinder()
	if not editHotkey.active then
		drawFolderTabs()
		imgui.Separator()
		imgui.BeginChild("binds_panel##main", imgui.ImVec2(0, 0), true)
		drawBindsGrid()
		imgui.EndChild()
	else
		drawEditHotkey(editHotkey.idx)
	end

	drawToasts()
end

-- === События и ввод ===
addEventHandler(
	"onWindowMessage",
	function(msg, wparam, lparam)
		-- Комбо-захват во время записи комбинации
		if combo_recording and (msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN) then
			if wparam == vk.VK_ESCAPE then
				combo_recording = false
				combo_keys = {}
				imgui.CloseCurrentPopup()
			elseif wparam == vk.VK_RETURN or wparam == vk.VK_NUMPADENTER then
				-- SAVE в попапе
			elseif wparam == vk.VK_BACK then
				combo_keys = {}
			elseif isKeyboardKey(wparam) then
				local nk = normalizeKey(wparam)
				local dup = false
				for _, kk in ipairs(combo_keys) do
					if normalizeKey(kk) == nk then
						dup = true
						break
					end
				end
				if not dup then
					table.insert(combo_keys, nk)
				end
			end
			consumeWindowMessage(true, true)
			return
		end

                if msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN then
                        if isKeyboardKey(wparam) then
                                pressedKeysSet[normalizeKey(wparam)] = true
                                rebuildPressedList()
                        end
                elseif msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP then
                        if isKeyboardKey(wparam) then
                                pressedKeysSet[normalizeKey(wparam)] = false
                                rebuildPressedList()
                        end
                elseif msg == wm.WM_MOUSEWHEEL then
                        if module.quickMenuOpen then
                                local delta = bit.rshift(bit.band(wparam, 0xFFFF0000), 16)
                                if delta >= 0x8000 then
                                        delta = delta - 0x10000
                                end
                                if delta ~= 0 then
                                        local steps = math.max(1, math.floor(math.abs(delta) / 120 + 0.5))
                                        if delta > 0 then
                                                quickMenuScrollQueued = quickMenuScrollQueued - steps
                                        else
                                                quickMenuScrollQueued = quickMenuScrollQueued + steps
                                        end
                                        consumeWindowMessage(true, true)
                                end
                        end
                end
        end
)

local function processHotkeys()
	local now = os.clock()
	local nowMs = now * 1000
	for _, hk in ipairs(hotkeys) do
		if #hk.keys > 0 and hk.enabled then
			local comboNow = keysMatchCombo(pressedKeysList, hk.keys)
			if hk.repeat_mode then
				if comboNow then
					local lastInterval = hk.repeat_interval_ms
					if not lastInterval and hk.messages and #hk.messages > 0 then
						lastInterval = math.max(hk.messages[#hk.messages].interval or 500, 50)
					end
					lastInterval = lastInterval or 500
					local sec = lastInterval / 1000
					if not hk._lastRepeatPressed or not keysMatchCombo(hk._lastRepeatPressed, hk.keys) then
						module.enqueueHotkey(hk)
						hk.lastActivated = now
						hk._lastRepeatPressed = {table.unpack(pressedKeysList)}
					elseif now - (hk.lastActivated or 0) >= sec then
						module.enqueueHotkey(hk)
						hk.lastActivated = now
						hk._lastRepeatPressed = {table.unpack(pressedKeysList)}
					end
				else
					hk._lastRepeatPressed = nil
				end
			else
				if comboNow and not hk._comboActive then
					if not hk._debounce_until or nowMs >= hk._debounce_until then
						module.enqueueHotkey(hk)
						hk._debounce_until = nowMs + DEBOUNCE_MS
					end
					hk._comboActive = true
				elseif not comboNow and hk._comboActive then
					hk._comboActive = false
				end
			end
		end
	end
end

-- Быстрое меню по боковой кнопке мыши
function module.CheckQuickMenuKey()
        local isOpen = isKeyDown(vk.VK_XBUTTON1) and true or false
        if not isOpen then
                quickMenuScrollQueued = 0
        end
        module.quickMenuOpen = isOpen
end

function module.OnTick()
	module.CheckQuickMenuKey()
	processHotkeys()
	runScheduler()
end

function module.OpenBinder()
	module.binderWindow[0] = true
end

-- Окна ImGui
imgui.OnFrame(
	function()
		return module.binderWindow[0]
	end,
	function()
		module.DrawBinder()
	end
)

imgui.OnFrame(
	function()
		return module.quickMenuOpen
	end,
	function()
		module.DrawQuickMenu()
	end
)

-- Автозагрузка
pcall(module.loadHotkeys)

return module
