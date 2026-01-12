local module = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
local mimgui_funcs = require("HelperByOrc.mimgui_funcs")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local vk = require("vkeys")
local vkeys = vk
local wm = require("windows.message")
local bit = require("bit")
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
	fa = setmetatable({}, {
		__index = function()
			return ""
		end,
	})
end

-- === Константы ===
local json_path = "moonloader/HelperByOrc/binder.json"
local DEBOUNCE_MS = 40 -- антидребезг
local MAX_BIND_DEPTH = 5 -- защита от рекурсии при внешних вызовах runBind*
local MAX_ACTIVE_HOTKEYS = 10 -- limit of active coroutines
local MULTI_BUF_MIN = 4096
local MULTI_BUF_PAD = 1024
local currentMultiInputHK = nil
local INPUTTEXT_CALLBACK_RESIZE = imgui.InputTextFlags and imgui.InputTextFlags.CallbackResize

local multiInputResizeCallbackPtr = nil

if INPUTTEXT_CALLBACK_RESIZE then
	local function multiInputResizeCallback(data)
		if not currentMultiInputHK or data.EventFlag ~= INPUTTEXT_CALLBACK_RESIZE then
			return 0
		end

		local hk = currentMultiInputHK
		local len = data.BufTextLen or 0
		local text = ffi.string(data.Buf, len)
		local desired = math.max(MULTI_BUF_MIN, len + 1 + MULTI_BUF_PAD)

		hk._multiBuf = imgui.new.char[desired](text)
		hk._multiBufSize = desired
		hk._multiBufText = text
		data.Buf = hk._multiBuf
		data.BufSize = desired

		return 0
	end

	multiInputResizeCallbackPtr = ffi.cast("int (*)(ImGuiInputTextCallbackData*)", multiInputResizeCallback)
end

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

local function trim(s)
	if type(s) ~= "string" then
		return ""
	end
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function clone_buttons(arr)
	local copy = {}
	for _, btn in ipairs(arr or {}) do
		copy[#copy + 1] = {
			label = btn.label or "",
			text = btn.text or "",
			hint = btn.hint or "",
		}
	end
	return copy
end

-- === Toasts ===
local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local function pushToast(text, kind, dur)
	toasts[#toasts + 1] = { text = tostring(text or ""), kind = kind or "ok", t = os.clock(), dur = dur or 3.0 }
end
local active_coroutines = {} -- { hk, co, state, wake }
local activeInputDialog = nil
local startHotkeyCoroutine -- forward declaration for dialog handlers

local function log_error(err)
	print("[binder] " .. tostring(err))
	pushToast(err, "err", 5.0)
end

local scheduler = coroutine.create(function()
	while true do
		local now = os.clock() * 1000
		for i = #active_coroutines, 1, -1 do
			local item = active_coroutines[i]
			local state = item.state
			if state.stopped then
				item.hk.is_running = false
				item.hk._co_state = nil
				item.hk._awaiting_input = false
				table.remove(active_coroutines, i)
			elseif now >= item.wake then
				if state.paused then
					item.wake = now + 50
				else
					local ok, wait_ms = coroutine.resume(item.co)
					if not ok then
						log_error(wait_ms)
						item.hk.is_running = false
						item.hk._co_state = nil
						item.hk._awaiting_input = false
						table.remove(active_coroutines, i)
					elseif coroutine.status(item.co) == "dead" then
						item.hk.is_running = false
						item.hk._co_state = nil
						item.hk._awaiting_input = false
						table.remove(active_coroutines, i)
					else
						item.wake = now + (wait_ms or 0)
					end
				end
			end
		end
		coroutine.yield()
	end
end)

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

local function cancelInputDialog()
	if not activeInputDialog then
		return
	end
	if activeInputDialog.hk then
		activeInputDialog.hk._awaiting_input = false
	end
	activeInputDialog = nil
end

local function openInputDialog(hk, delay_ms)
	local fields = {}
	for _, input in ipairs(hk.inputs or {}) do
		local key = trim(input.key or "")
		if key ~= "" then
			local mode = input.mode == "buttons" and "buttons" or "text"
			local buttons
			if mode == "buttons" then
				buttons = {}
				for _, btn in ipairs(input.buttons or {}) do
					local text = trim(btn.text or "")
					if text ~= "" then
						buttons[#buttons + 1] = {
							label = btn.label or "",
							text = btn.text or "",
							hint = btn.hint or "",
						}
					end
				end
				if #buttons == 0 then
					mode = "text"
					buttons = nil
				end
			end
			fields[#fields + 1] = {
				label = input.label or "",
				hint = input.hint or "",
				key = key,
				mode = mode,
				buttons = buttons,
			}
		end
	end
	if #fields == 0 then
		return false
	end
	activeInputDialog = {
		hk = hk,
		delay = delay_ms,
		fields = fields,
		buffers = {},
		button_selected = {},
		open = imgui.new.bool(true),
		focus_requested = true,
	}
	hk._awaiting_input = true
	return true
end

local INPUT_BUF_SIZE = 2048

local function ensureDialogTables(dialog)
	dialog.buffers = dialog.buffers or {}
	dialog.combo_selected = dialog.combo_selected or {} -- int[1] на поле
	dialog.combo_cache = dialog.combo_cache or {} -- кеш списка для Combo на поле
end

local function ensureDialogBuffer(dialog, idx)
	local buf = dialog.buffers[idx]
	if not buf then
		buf = imgui.new.char[INPUT_BUF_SIZE]()
		dialog.buffers[idx] = buf
		imgui.StrCopy(buf, "", INPUT_BUF_SIZE)
	end
	return buf
end

local function countCharsForUI(text)
	-- В SA:MP часто удобнее считать "символы" через u8:decode, как у тебя в примере
	if u8 and u8.decode then
		local decoded = u8:decode(text or "")
		return #(decoded or "")
	end
	return #(text or "")
end

local function calcAutoHeight(dialog, text, widthForWrap)
	local style = imgui.GetStyle()
	local lineH = imgui.GetTextLineHeight()

	if not dialog._charW then
		dialog._charW = imgui.CalcTextSize("0").x
	end

	local usableW = math.max(80, (widthForWrap or 320) - style.FramePadding.x * 2 - 6)
	local charPerLine = math.max(10, math.floor(usableW / dialog._charW))

	local lines = 0
	for line in (text .. "\n"):gmatch("(.-)\n") do
		local len = #line
		local wraps = 1
		if len > 0 then
			wraps = math.max(1, math.ceil(len / charPerLine))
		end
		lines = lines + wraps
	end

	lines = math.max(3, math.min(12, lines))
	return lines * lineH + style.FramePadding.y * 2 + 10
end

local function getComboCache(dialog, idx, field)
	local buttons = field.buttons or {}

	-- Сигнатура, чтобы пересобирать только если реально поменялись пункты
	local sigParts = { tostring(#buttons) }
	for j = 1, #buttons do
		sigParts[#sigParts + 1] = trim(buttons[j].label or "")
	end
	local sig = table.concat(sigParts, "|")

	local cache = dialog.combo_cache[idx]
	if cache and cache.sig == sig then
		return cache
	end

	local item_list = {}
	item_list[1] = "Свой текст"
	for j, btn in ipairs(buttons) do
		local label = trim(btn.label or "")
		if label == "" then
			label = ("Вариант %d"):format(j)
		end
		item_list[#item_list + 1] = label
	end

	local ImItems = imgui.new["const char*"][#item_list](item_list)

	cache = { sig = sig, im = ImItems, count = #item_list }
	dialog.combo_cache[idx] = cache
	return cache
end

local function drawCharCountOverlay(countStr, rectMin, rectMax)
	local style = imgui.GetStyle()
	local textSize = imgui.CalcTextSize(countStr)
	local padding = style.FramePadding

	local pos = imgui.ImVec2(rectMax.x - padding.x - textSize.x, rectMin.y + padding.y)

	local col = style.Colors[imgui.Col.TextDisabled]
	imgui.GetWindowDrawList():AddText(pos, imgui.GetColorU32Vec4(col), countStr)
end

local function drawInputMultilineWithCounter(id, buf, bufSize, height)
	local style = imgui.GetStyle()

	local text = ffi.string(buf)
	local countStr = tostring(countCharsForUI(text))
	local countW = imgui.CalcTextSize(countStr).x
	local reserved = countW + style.FramePadding.x * 2 + 8

	local fullW = imgui.GetContentRegionAvail().x
	local inputW = fullW - reserved
	if inputW < 120 then
		inputW = math.max(80, fullW)
	end

	imgui.PushItemWidth(inputW)
	local changed =
		imgui.InputTextMultiline(id, buf, bufSize, imgui.ImVec2(0, height), imgui.InputTextFlags.NoHorizontalScroll)
	imgui.PopItemWidth()

	local rectMin = imgui.GetItemRectMin()
	local rectMax = imgui.GetItemRectMax()

	-- добиваем строку невидимым блоком, чтобы справа было место (и переносы/ниже-виджеты были ровнее)
	if inputW < fullW - 1 then
		imgui.SameLine(0, 0)
		imgui.Dummy(imgui.ImVec2(fullW - inputW, height))
	end

	drawCharCountOverlay(countStr, rectMin, rectMax)

	return changed
end

-- ===================== MAIN =====================

local function drawInputDialog()
	local dialog = activeInputDialog
	if not dialog then
		return
	end

	local hk = dialog.hk
	if not hk or #(dialog.fields or {}) == 0 then
		cancelInputDialog()
		return
	end

	ensureDialogTables(dialog)

	if not dialog.open then
		dialog.open = imgui.new.bool(true)
		dialog.focus_requested = true
	end

	if not dialog.pos_set then
		local centerX, centerY = 0, 0
		if imgui.GetMainViewport then
			local vp = imgui.GetMainViewport()
			centerX = vp.Pos.x + vp.Size.x * 0.5
			centerY = vp.Pos.y + vp.Size.y * 0.5
		else
			local io = imgui.GetIO()
			centerX = io.DisplaySize.x * 0.5
			centerY = io.DisplaySize.y * 0.5
		end

		local pos = imgui.ImVec2(centerX, centerY)
		local pivot = imgui.ImVec2(0.5, 0.5)
		local ok = pcall(imgui.SetNextWindowPos, pos, imgui.Cond.Appearing, pivot)
		if not ok then
			imgui.SetNextWindowPos(pos, imgui.Cond.Appearing)
		end
		dialog.pos_set = true
	end

	imgui.SetNextWindowSize(imgui.ImVec2(460, 0), imgui.Cond.Appearing)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowMinSize, imgui.ImVec2(420, 160))

	if imgui.Begin("Заполните данные##binder_input", dialog.open, imgui.WindowFlags.NoCollapse) then
		if hk.label and hk.label ~= "" then
			imgui.Text((fa.KEYBOARD or "") .. " " .. hk.label)
			imgui.Separator()
		end

		for idx, field in ipairs(dialog.fields) do
			if field.label and field.label ~= "" then
				imgui.TextWrapped(field.label)
			end

			local buf = ensureDialogBuffer(dialog, idx)

			-- Combo быстрых вариантов
			local hasButtons = field.mode == "buttons" and field.buttons and #field.buttons > 0
			local combo = dialog.combo_selected[idx]

			if hasButtons then
				if not combo then
					combo = imgui.new.int(0)
					dialog.combo_selected[idx] = combo
				end

				local cache = getComboCache(dialog, idx, field)

				imgui.PushItemWidth(0)
				local before = combo[0]
				imgui.Combo("##dialog_combo" .. idx, combo, cache.im, cache.count)
				imgui.PopItemWidth()

				if combo[0] ~= before and combo[0] > 0 then
					local btn = field.buttons[combo[0]]
					imgui.StrCopy(buf, (btn and btn.text) or "", INPUT_BUF_SIZE)
				end

				-- Хинт выбранного варианта (очень легкий, без тултипов)
				if combo[0] > 0 then
					local btn = field.buttons[combo[0]]
					if btn and btn.hint and btn.hint ~= "" then
						imgui.PushTextWrapPos()
						imgui.TextDisabled(btn.hint)
						imgui.PopTextWrapPos()
					end
				end

				imgui.Spacing()
			end

			-- Хинт поля
			if field.hint and field.hint ~= "" then
				imgui.PushTextWrapPos()
				imgui.TextDisabled(field.hint)
				imgui.PopTextWrapPos()
				imgui.Spacing()
			end

			-- Фокус в первое поле при открытии
			if dialog.focus_requested and idx == 1 then
				imgui.SetKeyboardFocusHere()
				dialog.focus_requested = false
			end

			-- Авто-высота + счетчик внутри
			local text = ffi.string(buf)
			local approxW = imgui.GetContentRegionAvail().x
			local h = calcAutoHeight(dialog, text, approxW)

			local changed = drawInputMultilineWithCounter("##dialog_input" .. idx, buf, INPUT_BUF_SIZE, h)

			-- Если печатают руками, сбрасываем Combo в "Свой текст"
			if changed and combo then
				combo[0] = 0
			end

			imgui.Spacing()
		end

		-- Кнопки действия
		if imgui.Button((fa.PAPER_PLANE or fa.CHECK or "") .. " Отправить") then
			local values = {}
			for i, field in ipairs(dialog.fields) do
				local key = field.key
				local b = dialog.buffers[i]
				local value = b and ffi.string(b) or ""

				if key and key ~= "" then
					values[key] = value
					values[key:lower()] = value
					values[key:upper()] = value
				end
				values[tostring(i)] = value
			end

			if startHotkeyCoroutine and startHotkeyCoroutine(hk, dialog.delay, values) then
				cancelInputDialog()
			elseif startHotkeyCoroutine then
				pushToast("Не удалось запустить бинд", "err", 3.0)
			end
		end

		imgui.SameLine()
		if imgui.Button((fa.XMARK or "X") .. " Отмена") then
			cancelInputDialog()
		end
	end

	imgui.End()
	imgui.PopStyleVar()

	if dialog.open and not dialog.open[0] then
		cancelInputDialog()
	end

	if not module.binderWindow[0] then
		drawToasts()
	end
end

-- === Состояние модуля ===
module.binderWindow = imgui.new.bool(false)
module.showQuickMenu = imgui.new.bool(false)
module.quickMenuOpen = false
local quickMenuTabIndex = 1
local quickMenuScrollQueued = 0
local quickMenuSelectRequest = nil

imgui.OnInitialize(function()
	if fa and fa.Init then
		fa.Init()
	end
	math.randomseed(os.time())
end)

-- === Данные ===
local folders = { { name = "Основные", children = {}, parent = nil, quick_conditions = {}, quick_menu = true } }
local selectedFolder = folders[1]
local hotkeys = {}
local labelInputs = setmetatable({}, { __mode = "k" })

-- кэш булевых для imgui
local function ensure_bool(buf, val)
	if not buf then
		buf = imgui.new.bool(val and true or false)
	else
		buf[0] = val and true or false
	end
	return buf
end

local function reset_multi_buffer(hk)
	if hk then
		hk._multiBuf, hk._multiBufSize, hk._multiBufText = nil, nil, nil
	end
end

local function reset_edit_state(hk)
	if not hk then
		return
	end
	hk.editMsgs, hk.editLabel, hk.editKeys, hk.editCommand, hk.editConditions, hk.editCommandEnabled =
		nil, nil, nil, nil, nil, nil
	hk.editRepeatMode, hk.editQuickMenu, hk.editRepeatInterval, hk.editQuickConditions = nil, nil, nil, nil
	hk.editBulkMethod, hk.editBulkInterval, hk.editMultiline, hk.editMultiText = nil, nil, nil, nil
	reset_multi_buffer(hk)
	hk.editTextTrigger, hk.editTriggerEnabled, hk.editTriggerPattern = nil, nil, nil
	hk.editInputs = nil
end

local function ensure_multi_buffer(hk)
	local text = hk.editMultiText or ""
	local len = #text
	local desired = math.max(MULTI_BUF_MIN, len + MULTI_BUF_PAD)
	local currentSize = hk._multiBufSize or 0

	if not hk._multiBuf or currentSize < desired then
		currentSize = desired
		hk._multiBuf = imgui.new.char[currentSize](text)
		hk._multiBufSize = currentSize
	else
		if hk._multiBufText ~= text then
			imgui.StrCopy(hk._multiBuf, text, currentSize)
		end
	end

	hk._multiBufText = text
	return hk._multiBuf
end

local editHotkey = { active = false, idx = -1 }
local editHotkeyNav = { open = false, targetIdx = nil, direction = nil }

-- Попапы редактора
local combo_recording = false
local combo_keys = {}
local open_combo_popup = false
local open_conditions_popup = false
local open_quick_conditions_popup = false

-- Подписи
local send_labels = { "В чат", "Клиенту", "Серверу", "В пустоту" }
local send_labels_ffi = imgui.new["const char*"][#send_labels](send_labels)

function module.getSendTargets()
	return send_labels, send_labels_ffi
end
local input_mode_labels = { "Поле ввода", "Кнопки" }
local input_mode_labels_ffi = imgui.new["const char*"][#input_mode_labels](input_mode_labels)

local function hotkeyFolderString(hk)
	if not hk or type(hk.folderPath) ~= "table" then
		return ""
	end
	return table.concat(hk.folderPath, "/")
end

local function refreshHotkeyNumbers()
	for i, hk in ipairs(hotkeys) do
		hk._number = i
	end
end

local function findHotkeyByNumberInScope(num, folderLower)
	if type(num) ~= "number" then
		return nil
	end
	num = math.floor(num)
	if num < 1 then
		return nil
	end
	refreshHotkeyNumbers()
	if folderLower and folderLower ~= "" then
		folderLower = tostring(folderLower):lower()
		local count = 0
		for _, hk in ipairs(hotkeys) do
			local fstr = hotkeyFolderString(hk):lower()
			if fstr:find(folderLower, 1, true) then
				count = count + 1
				if count == num then
					return hk
				end
			end
		end
		return nil
	end
	return hotkeys[num]
end

-- JSON
local config = { hotkeys = {}, folders = {} }

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
		quick_menu = folder.quick_menu ~= false,
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
		quick_menu = tbl.quick_menu ~= false,
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
	refreshHotkeyNumbers()
	config.hotkeys, config.folders = {}, {}
	for _, f in ipairs(folders) do
		table.insert(config.folders, serializeFolder(f))
	end
	for idx, hk in ipairs(hotkeys) do
		local msgs = {}
		for _, m in ipairs(hk.messages or {}) do
			table.insert(msgs, { text = m.text, interval = m.interval, method = m.method })
		end
		local inputs = {}
		for _, input in ipairs(hk.inputs or {}) do
			local mode = input.mode == "buttons" and "buttons" or "text"
			local buttons
			if mode == "buttons" then
				buttons = {}
				for _, btn in ipairs(input.buttons or {}) do
					local text = trim(btn.text or "")
					if text ~= "" then
						buttons[#buttons + 1] = {
							label = btn.label or "",
							text = btn.text or "",
							hint = btn.hint or "",
						}
					end
				end
				if #buttons == 0 then
					buttons = nil
				end
			end
			table.insert(inputs, {
				label = input.label or "",
				hint = input.hint or "",
				key = input.key or "",
				mode = mode,
				buttons = buttons,
			})
		end
		table.insert(config.hotkeys, {
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
			text_trigger = hk.text_trigger,
			number = hk._number or idx,
			inputs = inputs,
		})
	end
	funcs.saveTableToJson(config, json_path)
end

local function newHotkeyBase()
	return {
		label = "Новый бинд",
		keys = {},
		messages = {},
		inputs = {},
		text_trigger = { text = "", enabled = false, pattern = false },
		repeat_mode = false,
		repeat_interval_ms = nil,
		conditions = {},
		quick_conditions = {},
		enabled = true,
		quick_menu = false,
		command = "",
		command_enabled = false,
		folderPath = { folders[1].name },
		is_running = false,
		_co_state = nil,
		_awaiting_input = false,
		lastActivated = 0,
		_bools = {},
		_cond_bools = {},
		_quick_cond_bools = {},
		_comboActive = false, -- лэтч комбо
		_debounce_until = nil,
	}
end

function module.registerHotkey(
	keys,
	messages,
	label,
	repeat_mode,
	conditions,
	command,
	folderPath,
	text_trigger,
	command_enabled,
	inputs
)
	local hk = newHotkeyBase()
	hk.keys = keys or {}
	hk.messages = messages or {}
	hk.inputs = {}
	for _, input in ipairs(inputs or {}) do
		local key = trim(input.key or "")
		if key ~= "" then
			local mode = input.mode == "buttons" and "buttons" or "text"
			local buttons
			if mode == "buttons" then
				buttons = {}
				for _, btn in ipairs(input.buttons or {}) do
					local text = trim(btn.text or "")
					if text ~= "" then
						buttons[#buttons + 1] = {
							label = btn.label or "",
							text = btn.text or "",
							hint = btn.hint or "",
						}
					end
				end
				if #buttons == 0 then
					buttons = nil
					mode = "text"
				end
			end
			table.insert(hk.inputs, {
				label = input.label or "",
				hint = input.hint or "",
				key = key,
				mode = mode,
				buttons = buttons,
			})
		end
	end
	hk.label = label or hk.label
	hk.repeat_mode = not not repeat_mode
	hk.conditions = conditions or {}
	hk.command = command or ""
	hk.command_enabled = not not command_enabled
	hk.folderPath = folderPath or { folders[1].name }
	hk.text_trigger = text_trigger or { text = "", enabled = false, pattern = false }
	hotkeys[#hotkeys + 1] = hk
	refreshHotkeyNumbers()
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
			folders =
				{ { name = "Основные", children = {}, parent = nil, quick_conditions = {}, quick_menu = true } }
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
				hk.folderPath or { folders[1].name },
				hk.text_trigger,
				hk.command_enabled,
				hk.inputs
			)
			local last = hotkeys[#hotkeys]
			last.enabled = hk.enabled == nil and true or hk.enabled
			last.quick_menu = hk.quick_menu or hk.fast_menu or false
			last.repeat_interval_ms = tonumber(hk.repeat_interval_ms) or nil
			last.quick_conditions = hk.quick_conditions or {}
			last.text_trigger = hk.text_trigger or { text = "", enabled = false, pattern = false }
			last.command_enabled = hk.command_enabled == nil and (hk.command ~= "") or hk.command_enabled
		end
		refreshHotkeyNumbers()
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

local InputManager = {
	-- Кэш состояний клавиш с временными метками
	key_states = {},
	key_timestamps = {},

	-- Группировка клавиш для быстрой проверки
	key_groups = {
		modifiers = { vk.VK_SHIFT, vk.VK_CONTROL, vk.VK_MENU },
		mouse = { vk.VK_LBUTTON, vk.VK_RBUTTON, vk.VK_MBUTTON, vk.VK_XBUTTON1, vk.VK_XBUTTON2 },
	},

	-- Дебаунс
	debounce_threshold = 0.1, -- 100ms
}

function InputManager:is_key_pressed(key_code)
	local state = self.key_states[key_code]
	local timestamp = self.key_timestamps[key_code] or 0

	if state and (os.clock() - timestamp) > self.debounce_threshold then
		return true
	end
	return false
end

function InputManager:is_modifier_pressed()
	for _, mod_key in ipairs(self.key_groups.modifiers) do
		if self.key_states[mod_key] then
			return true
		end
	end
	return false
end

function InputManager:update(msg, wparam, lparam)
	local key_code = normalizeKey(wparam)
	if not key_code or not isKeyboardKey(key_code) then
		return
	end

	local now = os.clock()

	if msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN then
		self.key_states[key_code] = true
		self.key_timestamps[key_code] = now
	elseif msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP then
		self.key_states[key_code] = false
	end
end

function InputManager:check_combo(combo_keys)
	if not combo_keys or #combo_keys == 0 then
		return false
	end

	for _, key in ipairs(combo_keys) do
		if not self.key_states[key] then
			return false
		end
	end

	local pressed_count = 0
	for _, pressed in pairs(self.key_states) do
		if pressed then
			pressed_count = pressed_count + 1
		end
	end

	return pressed_count == #combo_keys
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
local pressedKeysSet = InputManager.key_states -- k -> true
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

local function apply_input_values(text, values)
	if not text or text == "" then
		return text
	end
	if not values then
		return text
	end
	return text:gsub("{{([%w_]+)}}", function(key)
		local replacement = values[key] or values[key:lower()] or values[key:upper()]
		if replacement == nil then
			return ""
		end
		return replacement
	end)
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

module.doSend = doSend

local cond_labels, quick_cond_labels, cond_count, quick_cond_count

local ConditionSystem = {
	order = {
		"in_water",
		"dead",
		"in_air",
	},

	conditions = {
		-- Базовые условия
		in_water = {
			check = function()
				return isCharInWater(PLAYER_PED)
			end,
			priority = 1,
			message = "Нельзя использовать в воде",
			label = "Не сработает в воде",
			quick_label = "Скрывать если в воде",
		},
		dead = {
			check = function()
				return isCharDead(PLAYER_PED)
			end,
			priority = 2,
			message = "Нельзя использовать будучи мертвым",
			label = "Не сработает если игрок мертв",
			quick_label = "Скрывать если игрок мертв",
		},
		in_air = {
			check = function()
				return isCharInAir(PLAYER_PED)
			end,
			priority = 3,
			message = "Нельзя использовать в воздухе",
			label = "Не сработает в воздухе",
			quick_label = "Скрывать если в воздухе",
		},

		-- Динамически добавляемые условия
		custom = {},
	},

	register_condition = function(self, name, check_fn, priority, message, label, quick_label)
		self.conditions.custom[name] = {
			check = check_fn,
			priority = priority or 10,
			message = message or "Условие не выполнено",
			label = label,
			quick_label = quick_label,
		}

		table.insert(self.order, name)
		self:refresh_labels()
	end,

	refresh_labels = function(self)
		self.labels = {}
		self.quick_labels = {}

		for _, cond_name in ipairs(self.order) do
			local cond = self.conditions[cond_name] or self.conditions.custom[cond_name]
			if cond then
				table.insert(self.labels, cond.label or cond.message or cond_name)
				table.insert(self.quick_labels, cond.quick_label or cond.label or cond.message or cond_name)
			end
		end

		cond_labels = self.labels
		quick_cond_labels = self.quick_labels
		cond_count = #self.labels
		quick_cond_count = #self.quick_labels
	end,

	flags_to_names = function(self, flags)
		local names = {}
		if not flags then
			return names
		end

		for idx, cond_name in ipairs(self.order) do
			local value = flags[idx]
			local enabled = value
			if type(value) == "table" then
				enabled = value[0]
			end

			if enabled then
				table.insert(names, cond_name)
			end
		end

		return names
	end,

	check_all = function(self, condition_list, context)
		local results = {}

		for _, cond_name in ipairs(condition_list or {}) do
			local cond = self.conditions[cond_name] or self.conditions.custom[cond_name]
			if cond then
				local ok, result = pcall(cond.check, context)
				if ok and result then
					table.insert(results, {
						name = cond_name,
						failed = true,
						message = cond.message,
						priority = cond.priority or 0,
					})
				end
			end
		end

		-- Сортировка по приоритету
		table.sort(results, function(a, b)
			return (a.priority or 0) < (b.priority or 0)
		end)

		return results
	end,
}

ConditionSystem:refresh_labels()

local cond_labels = ConditionSystem.labels or {}
local cond_count = #cond_labels

-- Условия появления в быстром меню для биндов/папок
local quick_cond_labels = ConditionSystem.quick_labels or {}
local quick_cond_count = #quick_cond_labels

local function conditions_ok(conds, opts)
	local cond_names = ConditionSystem:flags_to_names(conds)

	local results = ConditionSystem:check_all(cond_names)
	if results and #results > 0 then
		if not (opts and opts.silent) then
			local top = results[1]
			if top and top.message then
				pushToast(top.message, "warn", 3.0)
			end
		end
		return false
	end

	return true
end

local check_conditions = conditions_ok
local function check_quick_visibility(conds)
	return conditions_ok(conds, { silent = true })
end

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

		local combined = table.concat(out)
		local final_str = apply_input_values(combined, state and state.inputs)
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

function startHotkeyCoroutine(hk, delay_ms, input_values)
	if not (hk.messages and #hk.messages > 0) then
		return false
	end
	if #active_coroutines >= MAX_ACTIVE_HOTKEYS then
		pushToast("Превышен лимит активных биндов", "warn", 3.0)
		return false
	end
	local state = { paused = false, idx = 1, stopped = false, inputs = input_values or {} }
	hk._co_state = state
	hk.is_running = true
	hk._awaiting_input = false
	local co = coroutine.create(function()
		if delay_ms and delay_ms > 0 then
			coroutine.yield(delay_ms)
		end
		module.sendHotkeyCoroutine(hk, state)
	end)
	table.insert(active_coroutines, { hk = hk, co = co, state = state, wake = 0 })
	return true
end

function module.enqueueHotkey(hk, delay_ms)
	if hk.is_running or hk._awaiting_input or not hk.enabled then
		return
	end
	if not check_conditions(hk.conditions) then
		return
	end
	if hk.inputs and #hk.inputs > 0 then
		if activeInputDialog and activeInputDialog.hk ~= hk then
			pushToast(
				"Сначала завершите ввод данных для другого бинда",
				"warn",
				3.0
			)
			return
		end
		if openInputDialog(hk, delay_ms) then
			return
		end
	end
	if hk.messages and #hk.messages > 0 then
		startHotkeyCoroutine(hk, delay_ms, nil)
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
	if activeInputDialog and activeInputDialog.hk == hk then
		cancelInputDialog()
	end
	local state = hk._co_state
	if state then
		state.stopped = true
		hk.is_running = false
		hk._co_state = nil
	end
end

function module.stopAllHotkeys()
	cancelInputDialog()
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
			hk.quick_menu
			and pathEquals(hk.folderPath, folderFullPath(folder))
			and check_quick_visibility(hk.quick_conditions or {})
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
	refreshHotkeyNumbers()
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
				hk.quick_menu
				and pathEquals(hk.folderPath, folderFullPath(node))
				and check_quick_visibility(hk.quick_conditions or {})
			then
				local displayNumber = hk._number or i
				local visibleLabel = ICON_KEYB .. (hk.label or ("bind" .. displayNumber))
				local label = visibleLabel .. "##quick_bind" .. i
				local shortcut
				if hk.keys and #hk.keys > 0 then
					shortcut = hotkeyToString(hk.keys)
				else
					shortcut = ""
				end
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
		quickMenuSelectRequest = nil
	else
		local clampedIndex = math.min(math.max(quickMenuTabIndex, 1), visibleCount)
		if clampedIndex ~= quickMenuTabIndex then
			quickMenuTabIndex = clampedIndex
			quickMenuSelectRequest = clampedIndex
		end

		local scrollSteps = quickMenuScrollQueued
		quickMenuScrollQueued = 0

		if hoveredQuickMenu then
			scrollSteps = io.MouseWheel
		end

		if scrollSteps ~= 0 then
			local previousIndex = quickMenuTabIndex
			quickMenuTabIndex = quickMenuTabIndex + scrollSteps
			quickMenuTabIndex = ((quickMenuTabIndex - 1) % visibleCount) + 1
			if quickMenuTabIndex ~= previousIndex then
				quickMenuSelectRequest = quickMenuTabIndex
			end
		end
	end

	if imgui.BeginTabBar("##quickbinder_tabbar") then
		for idx, folder in ipairs(visibleFolders) do
			local tabFlags = 0
			local hasSelectFlag = mimgui_funcs and mimgui_funcs.TabItemFlags and mimgui_funcs.TabItemFlags.SetSelected
			if quickMenuSelectRequest == idx and hasSelectFlag then
				tabFlags = flags_or(tabFlags, mimgui_funcs.TabItemFlags.SetSelected)
			end
			local tabOpened = imgui.BeginTabItem(folder.name, nil, tabFlags)
			if tabOpened then
				if quickMenuTabIndex ~= idx then
					quickMenuTabIndex = idx
				end
				if quickMenuSelectRequest == idx then
					quickMenuSelectRequest = nil
				end
				drawRec(folder)
				imgui.EndTabItem()
			end
			if imgui.IsItemHovered() and imgui.IsMouseClicked(0) then
				if quickMenuTabIndex ~= idx then
					quickMenuTabIndex = idx
				end
				quickMenuSelectRequest = idx
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
	local folderLower = folder and tostring(folder):lower() or nil
	local numericName = tonumber(name)
	if numericName then
		local hkByNumber = findHotkeyByNumberInScope(numericName, folderLower)
		if hkByNumber then
			return hkByNumber
		end
	end
	local query = tostring(name):lower()
	local partial
	for _, hk in ipairs(hotkeys) do
		local inFolder = true
		if folderLower and folderLower ~= "" then
			local fstr = hotkeyFolderString(hk):lower()
			inFolder = fstr:find(folderLower, 1, true) and true or false
		end
		if inFolder and hk.label then
			local lbl = hk.label:lower()
			if lbl == query then
				return hk
			elseif not partial and lbl:find(query, 1, true) then
				partial = hk
			end
		end
	end
	return partial
end

local function resolveBindForExecution(name, folder)
	if name == nil then
		return nil
	end
	local numericName = tonumber(name)
	if numericName and (folder == nil or folder == "") then
		return findHotkeyByNumberInScope(numericName, nil)
	end
	return module.findBind(name, folder)
end

function module.startBind(name, folder)
	local hk = resolveBindForExecution(name, folder)
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
		hk.quick_menu = not not state
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
	local hk = resolveBindForExecution(name, folder)
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
	local recursive = not not opts.recursive
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
	_G.moveBindPopup = { active = false, hkidx = nil }
end
if not _G.deleteBindPopup then
	_G.deleteBindPopup = { active = false, idx = nil, from_edit = false }
end
if not _G.deleteFolderPopup then
	_G.deleteFolderPopup = { active = false, folder = nil }
end

local function utf8_trim_last_char(s)
	s = tostring(s or "")
	local len = #s
	while len > 0 do
		local byte = s:byte(len)
		len = len - 1
		if byte < 0x80 or byte >= 0xC0 then
			break
		end
	end
	return s:sub(1, len)
end

local function ellipsize_utf8(text, maxWidth)
	text = tostring(text or "")
	if maxWidth == nil then
		return text
	end
	if maxWidth <= 0 then
		return "..."
	end
	if imgui.CalcTextSize(text).x <= maxWidth then
		return text
	end
	local ell = "..."
	local ell_w = imgui.CalcTextSize(ell).x
	local available = maxWidth - ell_w
	if available <= 0 then
		return ell
	end
	local base = text
	if base:sub(-3) == ell then
		base = base:sub(1, -4)
	end
	while base ~= "" and imgui.CalcTextSize(base).x > available do
		base = utf8_trim_last_char(base)
	end
	if base == "" then
		return ell
	end
	return base .. ell
end

local VirtualizedGrid = {
	item_width = 138,
	item_height = 56,
	spacing_x = 16,
	spacing_y = 16,

	-- Кэш рендеринга
	render_cache = {},
	cache_version = 0,

	calculate_visible = function(self, scroll_y, window_height)
		local start_index = math.floor(scroll_y / (self.item_height + self.spacing_y))
		local visible_rows = math.ceil(window_height / (self.item_height + self.spacing_y)) + 2

		return {
			start = math.max(1, start_index),
			count = visible_rows,
		}
	end,

	get_card_position = function(self, index, columns)
		local row = math.floor((index - 1) / columns)
		local col = (index - 1) % columns

		local x = col * (self.item_width + self.spacing_x)
		local y = row * (self.item_height + self.spacing_y)

		return x, y, self.item_width, self.item_height
	end,
}

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

local bindsSelectedIndex = nil
local hotkeysDirty = true

local function drawBindsGrid()
	local useColumnsList = true
	local availWidth = imgui.GetContentRegionAvail().x
	local cardWidth, cardHeight = VirtualizedGrid.item_width, VirtualizedGrid.item_height
	local spacingX, spacingY = VirtualizedGrid.spacing_x, VirtualizedGrid.spacing_y
	local columns = math.max(1, math.floor((availWidth + spacingX) / (cardWidth + spacingX)))
	local x0 = imgui.GetCursorScreenPos().x
	local y = imgui.GetCursorScreenPos().y

	if hotkeysDirty then
		refreshHotkeyNumbers()
		hotkeysDirty = false
	end

	local cards = {}
	local curPath = folderFullPath(selectedFolder)
	for i, hk in ipairs(hotkeys) do
		if pathEquals(hk.folderPath, curPath) then
			table.insert(cards, { hk = hk, idx = i })
		end
	end

	if useColumnsList then
		local addLabel = (fa.SQUARE_PLUS ~= "" and (fa.SQUARE_PLUS .. " ") or "") .. "Добавить бинд##add_bind_cols"
		local addSize = imgui.CalcTextSize(addLabel)
		local rightX = imgui.GetCursorPosX() + imgui.GetContentRegionAvail().x - addSize.x
			- imgui.GetStyle().FramePadding.x * 2
		if rightX > imgui.GetCursorPosX() then
			imgui.SetCursorPosX(rightX)
		end
		if imgui.SmallButton(addLabel) then
			local hk = newHotkeyBase()
			hk.folderPath = folderFullPath(selectedFolder)
			table.insert(hotkeys, hk)
			hotkeysDirty = true
			module.saveHotkeys()
		end
		imgui.Spacing()
		imgui.Columns(5, "binds_cols", true)
		local tableMinX = imgui.GetCursorScreenPos().x
		local contentWidth = imgui.GetWindowContentRegionMax().x - imgui.GetWindowContentRegionMin().x
		local baseOffset = imgui.GetColumnOffset(0)
		local col1W = 28
		local col2W = 28
		local col3W = math.min(220, math.max(140, math.floor(contentWidth * 0.22)))
		local col5W = math.min(260, math.max(200, math.floor(contentWidth * 0.25)))
		local col4W = math.max(140, contentWidth - (col1W + col2W + col3W + col5W))
		imgui.SetColumnOffset(1, baseOffset + col1W)
		imgui.SetColumnOffset(2, baseOffset + col1W + col2W)
		imgui.SetColumnOffset(3, baseOffset + col1W + col2W + col3W)
		imgui.SetColumnOffset(4, baseOffset + col1W + col2W + col3W + col4W)
		imgui.TextDisabled("Актив")
		imgui.NextColumn()
		imgui.TextDisabled("Меню")
		imgui.NextColumn()
		imgui.TextDisabled("Запуск")
		imgui.NextColumn()
		imgui.TextDisabled("Бинд")
		imgui.NextColumn()
		imgui.TextDisabled("Действия")
		imgui.NextColumn()
		local headerLine = imgui.GetCursorScreenPos()
		local headerBorder = imgui.GetStyle().Colors[imgui.Col.Border]
		local headerBorderCol = imgui.ImVec4(headerBorder.x, headerBorder.y, headerBorder.z, headerBorder.w * 0.3)
		local headerU32 = imgui.GetColorU32Vec4(headerBorderCol)
		local dl = imgui.GetWindowDrawList()
		local y = headerLine.y
		imgui.PushClipRect(imgui.ImVec2(tableMinX, y - 2), imgui.ImVec2(tableMinX + contentWidth, y + 2), false)
		dl:AddLine(imgui.ImVec2(tableMinX, y), imgui.ImVec2(tableMinX + contentWidth, y), headerU32, 1)
		imgui.PopClipRect()
		imgui.Dummy(imgui.ImVec2(0, imgui.GetStyle().ItemSpacing.y))

		local style = imgui.GetStyle()
		local rowStep = math.max(imgui.GetFrameHeight(), imgui.GetTextLineHeight()) + style.ItemSpacing.y
		local rowContentH = rowStep - style.ItemSpacing.y
		local clipper = imgui.ImGuiListClipper(#cards, rowStep)
		while clipper:Step() do
			for localIndex = clipper.DisplayStart, clipper.DisplayEnd - 1 do
				local card = cards[localIndex + 1]
				if not card then
					break
				end
				local hk, i = card.hk, card.idx
				local rowIndex = localIndex
				local rowStart = imgui.GetCursorScreenPos()
				local rowEnd = imgui.ImVec2(rowStart.x + contentWidth, rowStart.y + rowContentH)
				local clickedOnWidget = false
				local function mark_widget_clicked(clicked)
					if clicked or imgui.IsItemClicked(0) then
						clickedOnWidget = true
					end
				end
				imgui.SetCursorScreenPos(rowStart)

				local dl = imgui.GetWindowDrawList()
				local fullMin = imgui.ImVec2(tableMinX, rowStart.y)
				local fullMax = imgui.ImVec2(tableMinX + contentWidth, rowStart.y + rowContentH)
				local rowHovered = imgui.IsMouseHoveringRect(fullMin, fullMax)
				local rowClicked = rowHovered and imgui.IsMouseClicked(0)
				local rowDbl = rowHovered and imgui.IsMouseDoubleClicked(0)
				imgui.PushClipRect(fullMin, fullMax, false)
				if (rowIndex % 2) == 1 then
					local baseCol = imgui.GetStyle().Colors[imgui.Col.FrameBg]
					local zebra = imgui.ImVec4(baseCol.x, baseCol.y, baseCol.z, baseCol.w * 0.25)
					dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(zebra))
				end
				if bindsSelectedIndex == i then
					local selCol = imgui.GetStyle().Colors[imgui.Col.Header]
					local sel = imgui.ImVec4(selCol.x, selCol.y, selCol.z, selCol.w * 0.35)
					dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(sel))
				elseif rowHovered then
					local hoverCol = imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
					local hover = imgui.ImVec4(hoverCol.x, hoverCol.y, hoverCol.z, hoverCol.w * 0.2)
					dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(hover))
				end
				local borderCol = imgui.GetStyle().Colors[imgui.Col.Border]
				local border = imgui.ImVec4(borderCol.x, borderCol.y, borderCol.z, borderCol.w * 0.3)
				local lineY = rowStart.y + rowContentH
				dl:AddLine(
					imgui.ImVec2(tableMinX, lineY),
					imgui.ImVec2(tableMinX + contentWidth, lineY),
					imgui.GetColorU32Vec4(border),
					1
				)
				imgui.PopClipRect()

				local displayNumber = hk._number or i
				local bindName = hk.label or ("bind" .. displayNumber)
				local isEnabled = hk.enabled == nil and true or hk.enabled
				if not isEnabled then
					imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, imgui.GetStyle().Alpha * 0.75)
					local textCol = imgui.GetStyle().Colors[imgui.Col.Text]
					imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(textCol.x, textCol.y, textCol.z, textCol.w * 0.8))
					local disabledCol = imgui.GetStyle().Colors[imgui.Col.TextDisabled]
					imgui.PushStyleColor(
						imgui.Col.TextDisabled,
						imgui.ImVec4(disabledCol.x, disabledCol.y, disabledCol.z, disabledCol.w * 0.8)
					)
				end

				local buttonY = rowStart.y + (rowContentH - imgui.GetFrameHeight()) / 2
				local colPos = imgui.GetCursorScreenPos()
				imgui.SetCursorScreenPos(imgui.ImVec2(colPos.x, buttonY))
				local toggleOnIcon = (fa.TOGGLE_ON ~= "" and fa.TOGGLE_ON) or (fa.POWER_OFF ~= "" and fa.POWER_OFF) or fa.CHECK_CIRCLE or ""
				local toggleOffIcon = (fa.TOGGLE_OFF ~= "" and fa.TOGGLE_OFF) or (fa.BAN ~= "" and fa.BAN) or fa.TIMES_CIRCLE or ""
				local togglePos = imgui.GetCursorScreenPos()
				local toggleIcon = isEnabled and toggleOnIcon or toggleOffIcon
				local toggleHitW = math.min(22, rowContentH)
				imgui.InvisibleButton("##hit_toggle_" .. i, imgui.ImVec2(toggleHitW, rowContentH))
				if imgui.SetItemAllowOverlap then
					imgui.SetItemAllowOverlap()
				end
				local toggleClicked = imgui.IsItemClicked(0)
				local toggleHovered = imgui.IsItemHovered()
				local toggleTextH = imgui.CalcTextSize(toggleIcon).y
				local toggleTextY = togglePos.y + (rowContentH - toggleTextH) / 2
				local toggleCol = isEnabled and imgui.GetStyle().Colors[imgui.Col.Text]
					or imgui.GetStyle().Colors[imgui.Col.TextDisabled]
				dl:AddText(
					imgui.ImVec2(togglePos.x + 2, toggleTextY),
					imgui.GetColorU32Vec4(toggleCol),
					toggleIcon
				)
				mark_widget_clicked(toggleClicked)
				if toggleClicked then
					local nextEnabled = not isEnabled
					hk.enabled = nextEnabled
					if not nextEnabled then
						hk.quick_menu = false
					end
					module.saveHotkeys()
				end
				if toggleHovered then
					imgui.SetTooltip("Включить/выключить бинд")
				end
				imgui.NextColumn()
				local isQuickMenu = hk.quick_menu and true or false
				colPos = imgui.GetCursorScreenPos()
				imgui.SetCursorScreenPos(imgui.ImVec2(colPos.x, buttonY))
				local quickIcon = (fa.BOLT ~= "" and fa.BOLT) or (fa.STAR ~= "" and fa.STAR) or ""
				local quickPos = imgui.GetCursorScreenPos()
				local quickHitW = math.min(22, rowContentH)
				imgui.InvisibleButton("##hit_quick_" .. i, imgui.ImVec2(quickHitW, rowContentH))
				if imgui.SetItemAllowOverlap then
					imgui.SetItemAllowOverlap()
				end
				local quickClicked = imgui.IsItemClicked(0)
				local quickHovered = imgui.IsItemHovered()
				local quickTextH = imgui.CalcTextSize(quickIcon).y
				local quickTextY = quickPos.y + (rowContentH - quickTextH) / 2
				local quickCol
				if isEnabled and isQuickMenu then
					quickCol = imgui.GetStyle().Colors[imgui.Col.Text]
				else
					quickCol = imgui.GetStyle().Colors[imgui.Col.TextDisabled]
				end
				dl:AddText(
					imgui.ImVec2(quickPos.x + 2, quickTextY),
					imgui.GetColorU32Vec4(quickCol),
					quickIcon
				)
				mark_widget_clicked(quickClicked)
				if quickClicked and isEnabled then
					hk.quick_menu = not isQuickMenu
					module.saveHotkeys()
				end
				if quickHovered then
					imgui.SetTooltip("Показывать в быстром меню")
				end
				imgui.NextColumn()
				local hasActivation = false
				local activationWidth = imgui.GetColumnWidth()
				local usedWidth = 0
				local trig = hk.text_trigger
				if trig and trig.enabled and trig.text and trig.text ~= "" then
					local label = fa.COMMENT ~= "" and fa.COMMENT or "TXT"
					imgui.TextDisabled(label)
					if imgui.IsItemHovered() then
						imgui.SetTooltip("Триггер: " .. tostring(trig.text))
					end
					hasActivation = true
					usedWidth = usedWidth + imgui.CalcTextSize(label).x + imgui.GetStyle().ItemSpacing.x
				end
				if hk.command_enabled and hk.command and hk.command ~= "" then
					if hasActivation then
						imgui.SameLine()
					end
					local icon = fa.TERMINAL ~= "" and fa.TERMINAL or "CMD"
					local available = math.max(0, activationWidth - usedWidth)
					local cmdText = icon .. " " .. hk.command
					cmdText = ellipsize_utf8(cmdText, available)
					imgui.TextDisabled(cmdText)
					if imgui.IsItemHovered() then
						imgui.SetTooltip("Команда: " .. tostring(hk.command))
					end
					hasActivation = true
					usedWidth = usedWidth + imgui.CalcTextSize(cmdText).x + imgui.GetStyle().ItemSpacing.x
				end
				if hk.keys and #hk.keys > 0 then
					if hasActivation then
						imgui.SameLine()
					end
					local icon = fa.KEYBOARD ~= "" and fa.KEYBOARD or "KEY"
					local keysText = hotkeyToString(hk.keys)
					local available = math.max(0, activationWidth - usedWidth)
					local label = icon .. " " .. keysText
					label = ellipsize_utf8(label, available)
					imgui.TextDisabled(label)
					if imgui.IsItemHovered() then
						imgui.SetTooltip("Клавиши: " .. keysText)
					end
					hasActivation = true
					usedWidth = usedWidth + imgui.CalcTextSize(label).x + imgui.GetStyle().ItemSpacing.x
				end
				imgui.NextColumn()
				local dndStart = imgui.GetCursorScreenPos()
				imgui.InvisibleButton("##dnd_zone_" .. i, imgui.ImVec2(imgui.GetColumnWidth(), rowContentH))
				if imgui.SetItemAllowOverlap then
					imgui.SetItemAllowOverlap()
				end
				if imgui.BeginDragDropSource() then
					local payload = ffi.new("int[1]", i)
					imgui.SetDragDropPayload("BINDER_HOTKEY", payload, ffi.sizeof(payload))
					local dragLabelNumber = hk._number or i
					local dragLabel = hk.label or ("bind" .. dragLabelNumber)
					imgui.Text(dragLabel)
					imgui.TextDisabled(string.format("#%d", dragLabelNumber))
					imgui.EndDragDropSource()
				end
				if imgui.BeginDragDropTarget() then
					local payload = imgui.AcceptDragDropPayload("BINDER_HOTKEY")
					if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
						local delivered = payload.Delivery
						if delivered == nil and imgui.IsMouseReleased then
							delivered = imgui.IsMouseReleased(0)
						end
						if delivered then
							local src_idx = ffi.cast("int*", payload.Data)[0]
							local dst_idx = i
							if src_idx >= 1 and src_idx <= #hotkeys and src_idx ~= dst_idx then
								local moved = table.remove(hotkeys, src_idx)
								if dst_idx > src_idx then
									dst_idx = dst_idx - 1
								end
								table.insert(hotkeys, dst_idx, moved)
								hotkeysDirty = true
								module.saveHotkeys()
							end
						end
					end
					imgui.EndDragDropTarget()
				end
				imgui.SetCursorScreenPos(dndStart)
				imgui.AlignTextToFramePadding()
				local rowCount = #(hk.messages or {})
				local countText = " (" .. tostring(rowCount) .. ")"
				local countWidth = imgui.CalcTextSize(countText).x
				local nameWidth = imgui.GetColumnWidth() - countWidth - imgui.GetStyle().ItemSpacing.x
				if nameWidth < 0 then
					nameWidth = 0
				end
				if not hk._name_cache then
					hk._name_cache = {}
				end
				local cache = hk._name_cache
				if cache.text ~= bindName or cache.width ~= nameWidth then
					cache.text = bindName
					cache.width = nameWidth
					cache.output = ellipsize_utf8(bindName, nameWidth)
				end
				local displayName = cache.output or bindName
				imgui.Text(displayName)
				if displayName ~= bindName and imgui.IsItemHovered() then
					imgui.SetTooltip(bindName)
				end
				imgui.SameLine()
				imgui.TextDisabled(countText)
				imgui.NextColumn()
				colPos = imgui.GetCursorScreenPos()
				imgui.SetCursorScreenPos(imgui.ImVec2(colPos.x, buttonY))
				local function small_action_button(label, enabled, tooltip)
					local clicked = false
					if enabled then
						clicked = imgui.SmallButton(label)
					else
						if imgui.BeginDisabled then
							imgui.BeginDisabled(true)
							imgui.SmallButton(label)
							imgui.EndDisabled()
						else
							imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, imgui.GetStyle().Alpha * 0.5)
							imgui.SmallButton(label)
							imgui.PopStyleVar()
						end
					end
					if imgui.IsItemHovered() and tooltip then
						imgui.SetTooltip(tooltip)
					end
					return clicked
				end
				local canAction = isEnabled
				if not hk.is_running then
					local playClicked = small_action_button(fa.PLAY .. "##play_" .. i, canAction, "Воспроизвести")
					mark_widget_clicked(playClicked)
					if playClicked then
						module.enqueueHotkey(hk)
					end
				else
					if hk._co_state and hk._co_state.paused then
						local resumeClicked = small_action_button(fa.PLAY .. "##resume_" .. i, canAction, "Продолжить")
						mark_widget_clicked(resumeClicked)
						if resumeClicked then
							hk._co_state.paused = false
						end
					else
						local pauseClicked = small_action_button(fa.PAUSE .. "##pause_" .. i, canAction, "Пауза")
						mark_widget_clicked(pauseClicked)
						if pauseClicked then
							hk._co_state = hk._co_state or {}
							hk._co_state.paused = true
						end
					end
					imgui.SameLine()
					local stopClicked = small_action_button(fa.STOP .. "##stop_" .. i, canAction, "Стоп")
					mark_widget_clicked(stopClicked)
					if stopClicked then
						module.stopHotkey(hk)
					end
				end
				imgui.SameLine()
				local editClicked = small_action_button(fa.PEN .. "##edit_" .. i, true, "Редактировать")
				mark_widget_clicked(editClicked)
				if editClicked then
					editHotkey.active = true
					editHotkey.idx = i
				end
				imgui.SameLine()
				local delClicked = small_action_button(fa.TRASH .. "##del_" .. i, true, "Удалить")
				mark_widget_clicked(delClicked)
				if delClicked then
					_G.deleteBindPopup.idx = i
					_G.deleteBindPopup.from_edit = false
					_G.deleteBindPopup.active = true
				end
				imgui.SameLine()
				local ctxClicked = small_action_button(fa.BARS .. "##ctx_" .. i, true, "Меню")
				mark_widget_clicked(ctxClicked)
				if ctxClicked then
					imgui.OpenPopup("ctx_card_" .. i)
				end
				imgui.NextColumn()

				if imgui.BeginPopup("ctx_card_" .. i) then
					local dupClicked = imgui.MenuItemBool("Дублировать", false)
					mark_widget_clicked(dupClicked)
					if dupClicked then
						local newhk = cloneHotkey(hk)
						table.insert(hotkeys, i + 1, newhk)
						hotkeysDirty = true
						module.saveHotkeys()
					end
					local moveClicked = imgui.MenuItemBool("Переместить в...", false)
					mark_widget_clicked(moveClicked)
					if moveClicked then
						_G.moveBindPopup.active = true
						_G.moveBindPopup.hkidx = i
						imgui.CloseCurrentPopup()
					end
					imgui.EndPopup()
				end

				if rowClicked and not clickedOnWidget then
					bindsSelectedIndex = i
				end
				if rowDbl and not clickedOnWidget then
					editHotkey.active = true
					editHotkey.idx = i
				end
				imgui.SetCursorScreenPos(imgui.ImVec2(rowStart.x, rowStart.y + rowStep))

				if not isEnabled then
					imgui.PopStyleColor(2)
					imgui.PopStyleVar()
				end
			end
		end

		imgui.Columns(1)
	else
		local totalItems = #cards + 1 -- include "+" button
		local totalRows = math.max(1, math.ceil(totalItems / columns))
		local scrollY = imgui.GetScrollY()
		local windowHeight = imgui.GetWindowHeight()
		local visible = VirtualizedGrid:calculate_visible(scrollY, windowHeight)
		local startRow = math.max(0, (visible.start or 1) - 1)
		startRow = math.min(startRow, totalRows - 1)
		local endRow = math.min(totalRows - 1, startRow + (visible.count or totalRows))

		local contentHeight = totalRows * (cardHeight + spacingY) - spacingY
		if contentHeight < 0 then
			contentHeight = 0
		end

		local cursorBase = imgui.GetCursorScreenPos()
		imgui.Dummy(imgui.ImVec2(1, contentHeight))
		imgui.SetCursorScreenPos(cursorBase)

		local startIndex = startRow * columns + 1
		local endIndex = math.min(#cards, (endRow + 1) * columns)

		for idx = startIndex, endIndex do
			local card = cards[idx]
			local hk, i = card.hk, card.idx
			local offsetX, offsetY = VirtualizedGrid:get_card_position(idx, columns)
			local x = x0 + offsetX
			local yPos = y + offsetY

			imgui.SetCursorScreenPos(imgui.ImVec2(x, yPos))
			local pmin = imgui.GetCursorScreenPos()
			local pmax = imgui.ImVec2(pmin.x + cardWidth, pmin.y + cardHeight)
			local hovered = imgui.IsMouseHoveringRect(pmin, pmax)

			imgui.BeginGroup()

			local dl = imgui.GetWindowDrawList()
			local bgcol = hovered and imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
				or imgui.GetStyle().Colors[imgui.Col.FrameBg]

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
				local displayNumber = hk._number or i
				local numberLabel = string.format("#%d", displayNumber)
				local numberWidth = imgui.CalcTextSize(numberLabel).x
				local label = hk.label or ("bind" .. displayNumber)
				local labelMaxWidth = max_text_w - numberWidth - 6
				if labelMaxWidth < 0 then
					labelMaxWidth = 0
				end
				local numberX = bolt_x - numberWidth - 6
				if numberX < text_start then
					numberX = text_start
				end
				local textWidthLimit = numberX - text_start - 4
				if textWidthLimit < 0 then
					textWidthLimit = 0
				end
				local labelWidthLimit = math.min(labelMaxWidth, textWidthLimit)
				if labelWidthLimit < 0 then
					labelWidthLimit = 0
				end
				label = ellipsize_utf8(label, labelWidthLimit)
				imgui.SetCursorScreenPos(imgui.ImVec2(text_start, pmin.y + 7))
				imgui.TextColored(imgui.GetStyle().Colors[imgui.Col.Text], label)
				imgui.SetCursorScreenPos(imgui.ImVec2(numberX, pmin.y + 7))
				imgui.TextDisabled(numberLabel)
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
				local buttonCount = hk.is_running and 5 or 4
				local totalSpacing = spacing * (buttonCount - 1)
				local buttonW = (cardWidth - padX * 2 - totalSpacing) / buttonCount
				if buttonW < 0 then
					buttonW = 0
				end
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
					_G.deleteBindPopup.idx = i
					_G.deleteBindPopup.from_edit = false
					_G.deleteBindPopup.active = true
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
					hotkeysDirty = true
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
				local dragLabelNumber = hk._number or i
				local dragLabel = hk.label or ("bind" .. dragLabelNumber)
				imgui.Text(dragLabel)
				imgui.TextDisabled(string.format("#%d", dragLabelNumber))
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
						hotkeysDirty = true
						module.saveHotkeys()
					end
				end
				imgui.EndDragDropTarget()
			end
		end

		local addIndex = #cards + 1
		local addRow = math.floor((addIndex - 1) / columns)
		if addRow >= startRow and addRow <= endRow then
			local add_offset_x, add_offset_y = VirtualizedGrid:get_card_position(addIndex, columns)
			local add_x = x0 + add_offset_x
			local add_y = y + add_offset_y
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
				hotkeysDirty = true
				module.saveHotkeys()
			end
			imgui.EndGroup()
		end

		-- Popup перемещения
		if _G.moveBindPopup.active then
			imgui.OpenPopup("move_bind_popup")
			_G.moveBindPopup.active = false
		end
		if imgui.BeginPopup("move_bind_popup") then
			imgui.Text("Переместить бинд:")
			local function drawTree(node, path)
				path = path or { node.name }
				if imgui.Selectable(table.concat(path, "/"), false) then
					local idx = _G.moveBindPopup.hkidx
					if hotkeys[idx] then
						hotkeys[idx].folderPath = { table.unpack(path) }
						hotkeysDirty = true
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
				drawTree(folder, { folder.name })
			end
			if imgui.Button("Отмена") then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end
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
	table.sort(t, function(a, b)
		return a < b
	end)
	return table.concat(t, ",")
end

local function validateHotkeyEdit(hkEdit, idxSelf)
	local errs = {}

	if not hkEdit.editLabel or hkEdit.editLabel:gsub("%s+", "") == "" then
		errs[#errs + 1] = "Название бинда пустое"
	end

	local fpath = hotkeys[idxSelf] and hotkeys[idxSelf].folderPath or { folders[1].name }
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

	if hkEdit.editInputs then
		local keysSeen = {}
		for i, input in ipairs(hkEdit.editInputs) do
			local key = trim(input.key or "")
			if key == "" then
				errs[#errs + 1] = ("Поле ввода %d: не указан ключ подстановки"):format(
					i
				)
			elseif not key:match("^[%w_]+$") then
				errs[#errs + 1] = ("Поле ввода %d: ключ должен содержать только латиницу, цифры и _"):format(
					i
				)
			else
				local lower = key:lower()
				if keysSeen[lower] then
					errs[#errs + 1] = ("Поле ввода %d: ключ '%s' уже используется"):format(
						i,
						key
					)
				else
					keysSeen[lower] = true
				end
			end
			if (input.mode or "text") == "buttons" then
				local hasButtons = false
				for _, btn in ipairs(input.buttons or {}) do
					if trim(btn.text or "") ~= "" then
						hasButtons = true
						break
					end
				end
				if not hasButtons then
					errs[#errs + 1] = ("Поле ввода %d: добавьте хотя бы одну кнопку с текстом"):format(
						i
					)
				end
			end
		end
	end

	local myCombo = normalizeCombo(hkEdit.editKeys or {})
	if myCombo ~= "" then
		for j, other in ipairs(hotkeys) do
			if j ~= idxSelf and other.enabled then
				if normalizeCombo(other.keys) == myCombo then
					errs[#errs + 1] = "Дублируется комбинация клавиш с биндом: "
						.. (other.label or ("#" .. j))
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
			table.insert(
				hk.editMsgs,
				{ text = m.text or "", interval = tostring(m.interval or 0), method = m.method or 0 }
			)
		end
	end
	if not hk.editInputs then
		hk.editInputs = {}
		for _, input in ipairs(hk.inputs or {}) do
			table.insert(hk.editInputs, {
				label = input.label or "",
				hint = input.hint or "",
				key = input.key or "",
				mode = input.mode == "buttons" and "buttons" or "text",
				buttons = clone_buttons(input.buttons),
			})
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
		hk.editKeys = { table.unpack(hk.keys or {}) }
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
	hk._bools.quick = ensure_bool(hk._bools.quick, hk.editQuickMenu)
	hk._bools.rep = ensure_bool(hk._bools.rep, hk.editRepeatMode)
	hk._bools.triggerEnabled = ensure_bool(hk._bools.triggerEnabled, hk.editTriggerEnabled)
	hk._bools.triggerPattern = ensure_bool(hk._bools.triggerPattern, hk.editTriggerPattern)
	hk._bools.commandEnabled = ensure_bool(hk._bools.commandEnabled, hk.editCommandEnabled)
	if hk._activeTab == nil then
		hk._activeTab = hk.editMultiline and 1 or 0
	end
end

local function syncMessagesToMulti(hk)
	hk.editMultiline = true
	local lines = {}
	for _, m in ipairs(hk.editMsgs or {}) do
		table.insert(lines, m.text or "")
	end
	hk.editMultiText = table.concat(lines, "\n")
end

local function syncMultiToMessages(hk)
	if not hk.editMultiline then
		return
	end
	hk.editMultiline = false
	hk.editMsgs = {}
	local text = hk.editMultiText or ""
	local defaultInterval = hk.editBulkInterval or "0"
	local defaultMethod = hk.editBulkMethod or 0
	for line in text:gmatch("[^\r\n]+") do
		if line ~= "" then
			table.insert(hk.editMsgs, { text = line, interval = defaultInterval, method = defaultMethod })
		end
	end
	module.saveHotkeys()
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
		reset_edit_state(hk)
		editHotkey.active = false
		return
	end
	imgui.SameLine()
	imgui.TextColored(
		imgui.GetStyle().Colors[imgui.Col.Text],
		fa.PEN .. "	" .. "Редактирование бинда"
	)
	local function get_folder_hotkey_indices(path)
		local list = {}
		for i, item in ipairs(hotkeys) do
			if pathEquals(item.folderPath, path) then
				list[#list + 1] = i
			end
		end
		return list
	end

	local function get_prev_next_idx(currentIdx)
		local current = hotkeys[currentIdx]
		if not current then
			return nil, nil, 0
		end
		local list = get_folder_hotkey_indices(current.folderPath)
		local pos
		for i, v in ipairs(list) do
			if v == currentIdx then
				pos = i
				break
			end
		end
		return list[pos and pos - 1 or nil], list[pos and pos + 1 or nil], #list
	end

	local function disabled_button(label, enabled)
		if enabled then
			return imgui.Button(label)
		end
		if imgui.BeginDisabled then
			imgui.BeginDisabled(true)
			imgui.Button(label)
			imgui.EndDisabled()
		elseif imgui.PushStyleVarFloat then
			imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, imgui.GetStyle().Alpha * 0.5)
			imgui.Button(label)
			imgui.PopStyleVar()
		else
			imgui.Button(label)
		end
		return false
	end

	local prevIdx, nextIdx, totalInFolder = get_prev_next_idx(idx)
	if totalInFolder > 1 then
		imgui.SameLine()
		if disabled_button(fa.ARROW_LEFT .. " Предыдущий", prevIdx ~= nil) then
			editHotkeyNav.targetIdx = prevIdx
			editHotkeyNav.direction = "предыдущему"
			editHotkeyNav.open = true
		end
		imgui.SameLine()
		if disabled_button("Следующий " .. fa.ARROW_RIGHT, nextIdx ~= nil) then
			editHotkeyNav.targetIdx = nextIdx
			editHotkeyNav.direction = "следующему"
			editHotkeyNav.open = true
		end
	end
	imgui.EndChild()

	local navSwitched = false
	if editHotkeyNav.open then
		imgui.OpenPopup("binder_nav_confirm")
		editHotkeyNav.open = false
	end
	if imgui.BeginPopupModal("binder_nav_confirm", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local direction = editHotkeyNav.direction or "следующему"
		imgui.Text(("Перейти к %s бинду?"):format(direction))
		imgui.Separator()
		if imgui.Button("Перейти##bind_nav_ok", imgui.ImVec2(120, 0)) then
			local target = editHotkeyNav.targetIdx
			if target and hotkeys[target] then
				reset_edit_state(hk)
				editHotkey.idx = target
				navSwitched = true
			end
			editHotkeyNav.targetIdx = nil
			editHotkeyNav.direction = nil
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##bind_nav_cancel", imgui.ImVec2(120, 0)) then
			editHotkeyNav.targetIdx = nil
			editHotkeyNav.direction = nil
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
	if navSwitched then
		return
	end

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
	if imgui.BeginTabBar("edit_bind_tabs") then
		if imgui.BeginTabItem("Строки") then
			if hk._activeTab ~= 0 then
				hk._activeTab = 0
				if hk.editMultiline then
					syncMultiToMessages(hk)
				end
			end

			imgui.PushItemWidth(120)
			local allMBuf = imgui.new.int(hk.editBulkMethod or 0)
			if imgui.Combo("Куда##allm", allMBuf, send_labels_ffi, #send_labels) then
				hk.editBulkMethod = allMBuf[0]
				for _, m in ipairs(hk.editMsgs) do
					m.method = hk.editBulkMethod
				end
				module.saveHotkeys()
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
					for _, m in ipairs(hk.editMsgs) do
						m.interval = s
					end
					module.saveHotkeys()
				end
			end
			imgui.PopItemWidth()

			if imgui.Button(fa.SQUARE_PLUS .. " " .. "Добавить строку") then
				table.insert(hk.editMsgs, { text = "", interval = "0", method = 0 })
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

				local style = imgui.GetStyle()
				local spacing = style.ItemSpacing.x
				local charCountText = u8:decode(m.text)
				local charCountLabel = string.format("%d", #charCountText or "")
				local reservedWidth = 50 + 100 + 120
				local reservedSpacing = spacing * 4
				local childPadding = style.WindowPadding.x * 2
				local scrollbarWidth = imgui.GetScrollMaxY() > 0 and style.ScrollbarSize or 0
				local dynamicWidth = imgui.GetContentRegionAvail().x
					- reservedWidth
					- reservedSpacing
					- childPadding
					- scrollbarWidth
				if dynamicWidth < 50 then
					dynamicWidth = 50
				end

				imgui.PushItemWidth(dynamicWidth)
				local tbuf = imgui.new.char[256](m.text or "")
				if imgui.InputText("##t", tbuf, ffi.sizeof(tbuf), flags_or(imgui.InputTextFlags.AutoSelectAll)) then
					m.text = ffi.string(tbuf)
					module.saveHotkeys()
				end
				imgui.PopItemWidth()

				local rectMin, rectMax = imgui.GetItemRectMin(), imgui.GetItemRectMax()
				local textSize = imgui.CalcTextSize(charCountLabel)
				local padding = style.FramePadding
				local overlayPos = imgui.ImVec2(rectMax.x - padding.x - textSize.x, rectMin.y + padding.y)
				local disabledColor = style.Colors[imgui.Col.TextDisabled]
				imgui.GetWindowDrawList():AddText(overlayPos, imgui.GetColorU32Vec4(disabledColor), charCountLabel)

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
				if imgui.Button(fa.TRASH .. " Удалить##del", imgui.ImVec2(100, 20)) then
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
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem("Мульти-ввод") then
			if hk._activeTab ~= 1 then
				hk._activeTab = 1
				if not hk.editMultiline then
					syncMessagesToMulti(hk)
				end
			end

			imgui.PushItemWidth(120)
			local allMBuf = imgui.new.int(hk.editBulkMethod or 0)
			if imgui.Combo("Куда##allm_multi", allMBuf, send_labels_ffi, #send_labels) then
				hk.editBulkMethod = allMBuf[0]
			end
			imgui.PopItemWidth()
			imgui.SameLine()
			imgui.PushItemWidth(70)
			local allIBuf = imgui.new.char[16](tostring(hk.editBulkInterval or "0"))
			if
				imgui.InputText(
					"мс##alli_multi",
					allIBuf,
					ffi.sizeof(allIBuf),
					flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
				)
			then
				local s = ffi.string(allIBuf)
				if s == "" or tonumber(s) then
					hk.editBulkInterval = s
				end
			end
			imgui.PopItemWidth()

			imgui.PushItemWidth(-1)
			local buf = ensure_multi_buffer(hk)
			local bufSize = hk._multiBufSize or ffi.sizeof(buf)
			local flags = INPUTTEXT_CALLBACK_RESIZE and flags_or(INPUTTEXT_CALLBACK_RESIZE) or nil
			local changed
			local hintHeight = imgui.GetTextLineHeightWithSpacing()
			local availHeight = imgui.GetContentRegionAvail().y - hintHeight
			local multilineHeight = math.max(80, availHeight)
			local multilineSize = imgui.ImVec2(0, multilineHeight)
			if multiInputResizeCallbackPtr and INPUTTEXT_CALLBACK_RESIZE then
				currentMultiInputHK = hk
				changed = imgui.InputTextMultiline(
					"##multi_text",
					buf,
					bufSize,
					multilineSize,
					flags,
					multiInputResizeCallbackPtr
				)
				currentMultiInputHK = nil
			else
				changed = imgui.InputTextMultiline("##multi_text", buf, bufSize, multilineSize)
			end
			local activeBuf = hk._multiBuf or buf
			if changed then
				local newText = ffi.string(activeBuf)
				hk.editMultiText = newText
				hk._multiBufText = newText
				if not (multiInputResizeCallbackPtr and INPUTTEXT_CALLBACK_RESIZE) then
					local needed = math.max(MULTI_BUF_MIN, #newText + MULTI_BUF_PAD)
					if needed > (hk._multiBufSize or 0) then
						hk._multiBuf = imgui.new.char[needed](newText)
						hk._multiBufSize = needed
						hk._multiBufText = newText
						activeBuf = hk._multiBuf
					end
				end
				hk._multiBufSize = hk._multiBufSize or bufSize
			end
			imgui.PopItemWidth()
			imgui.TextDisabled(
				"Каждая строка будет отправлена отдельно. Пустые строки игнорируются."
			)
			imgui.EndTabItem()
		end
		--------------------------------------------------------------
		if imgui.BeginTabItem("Поля ввода") then
			hk._activeTab = 2

			local function MenuItemEnabled(label, enabled)
				return imgui.MenuItemBool(label, "", false, enabled and true or false)
			end

			local function buf_set(bufArr, bufSize, text)
				ffi.fill(bufArr, bufSize, 0)
				text = text or ""
				if text == "" then
					return
				end
				local bytes = #text
				if bytes > bufSize - 1 then
					bytes = bufSize - 1
				end
				ffi.copy(bufArr, text, bytes)
				bufArr[bytes] = 0
			end

			local function buf_get(bufArr)
				return ffi.string(bufArr)
			end

			local function normalize_key(s)
				s = trim(s or "")
				if s == "" then
					return ""
				end
				s = s:gsub("%s+", "_")
				s = s:gsub("[^%w_]", "")
				s = s:gsub("_+", "_")
				s = s:gsub("^_+", ""):gsub("_+$", "")
				s = s:upper()
				return s
			end

			local function key_is_valid(s)
				s = trim(s or "")
				if s == "" then
					return false
				end
				return s:match("^[A-Z0-9_]+$") ~= nil
			end

			local function can_clipboard()
				return type(imgui.SetClipboardText) == "function"
			end

			local function clone_button(b)
				return {
					label = b.label or "",
					text = b.text or "",
					hint = b.hint or "",
				}
			end

			local function clone_input(inp)
				local out = {
					label = inp.label or "",
					hint = inp.hint or "",
					key = inp.key or "",
					mode = (inp.mode == "buttons") and "buttons" or "text",
					buttons = {},
				}
				local btns = inp.buttons or {}
				for i = 1, #btns do
					out.buttons[i] = clone_button(btns[i])
				end
				return out
			end

			-- bulk helpers: "Название | Текст | Подсказка"
			local function escape_field(s)
				s = s or ""
				s = s:gsub("\\", "\\\\")
				s = s:gsub("\r\n", "\n")
				s = s:gsub("\n", "\\n")
				s = s:gsub("|", "\\|")
				return s
			end

			local function unescape_field(s)
				s = s or ""
				s = s:gsub("\\n", "\n")
				s = s:gsub("\\|", "|")
				s = s:gsub("\\\\", "\\")
				return s
			end

			local function split_pipe_escaped(line)
				local out, cur = {}, {}
				local i, n = 1, #line
				while i <= n do
					local ch = line:sub(i, i)
					if ch == "\\" and i < n then
						local nx = line:sub(i + 1, i + 1)
						cur[#cur + 1] = ch
						cur[#cur + 1] = nx
						i = i + 2
					elseif ch == "|" then
						out[#out + 1] = table.concat(cur)
						cur = {}
						i = i + 1
					else
						cur[#cur + 1] = ch
						i = i + 1
					end
				end
				out[#out + 1] = table.concat(cur)
				return out
			end

			local function serialize_buttons(buttons)
				local lines = {}
				buttons = buttons or {}
				for j = 1, #buttons do
					local b = buttons[j]
					local label = trim(b.label or "")
					if label == "" then
						label = "Кнопка " .. j
					end
					local text = b.text or ""
					local hint = b.hint or ""
					lines[#lines + 1] =
						string.format("%s | %s | %s", escape_field(label), escape_field(text), escape_field(hint))
				end
				return table.concat(lines, "\n")
			end

			local function parse_buttons_ex(multiline)
				local res = {}
				local stats = {
					total = 0,
					used = 0,
					ignored = 0,
					extraPipes = 0, -- когда полей больше 3 (часто забыли \|)
				}

				multiline = (multiline or ""):gsub("\r\n", "\n")
				for line in (multiline .. "\n"):gmatch("(.-)\n") do
					stats.total = stats.total + 1
					local raw = trim(line)
					if raw == "" or raw:match("^#") then
						stats.ignored = stats.ignored + 1
					else
						stats.used = stats.used + 1
						local parts = split_pipe_escaped(raw)

						local a = trim(parts[1] or "")
						local b = trim(parts[2] or "")
						local c
						if #parts <= 3 then
							c = trim(parts[3] or "")
						else
							-- если кто-то написал лишние | без экранирования, не теряем текст
							stats.extraPipes = stats.extraPipes + 1
							c = trim(table.concat(parts, "|", 3))
						end

						a = unescape_field(a)
						b = unescape_field(b)
						c = unescape_field(c)

						a = trim(a)
						if a == "" then
							a = "Кнопка " .. tostring(#res + 1)
						end

						res[#res + 1] = { label = a, text = b, hint = c }
					end
				end

				return res, stats
			end

			local function bulk_append_line(bulk, line)
				local cur = buf_get(bulk.buf)
				if cur ~= "" and cur:sub(-1) ~= "\n" then
					cur = cur .. "\n"
				end
				cur = cur .. (line or " |  | ")
				buf_set(bulk.buf, bulk.size, cur)
			end

			local function bulk_append_n_empty(bulk, n)
				for _ = 1, n do
					bulk_append_line(bulk, " |  | ")
				end
			end

			-- =========================
			-- UI top
			-- =========================

			imgui.TextDisabled("Используйте {{ключ}} в тексте сообщений")
			if imgui.Button(fa.SQUARE_PLUS .. " Добавить поле") then
				table.insert(hk.editInputs, { label = "", hint = "", key = "", mode = "text", buttons = {} })
				hk._inputsSel = #hk.editInputs
				module.saveHotkeys()
			end
			imgui.SameLine()
			imgui.TextDisabled("Слева список, справа редактор")

			-- UI state
			hk._inputsSel = hk._inputsSel or 1
			hk._inputsLeftW = hk._inputsLeftW or 170

			-- кеши
			hk._uiInputs = hk._uiInputs or { inputs = {} }

			-- =========================
			-- Empty state (большая кнопка)
			-- =========================
			if #hk.editInputs == 0 then
				imgui.Spacing()
				imgui.Spacing()
				imgui.TextDisabled(
					"Пока нет полей. Создайте первое и настройте ключ {{KEY}}."
				)
				imgui.Spacing()
				if imgui.Button("Создать первое поле", imgui.ImVec2(0, 52)) then
					table.insert(hk.editInputs, { label = "", hint = "", key = "", mode = "text", buttons = {} })
					hk._inputsSel = 1
					module.saveHotkeys()
				end

				imgui.EndTabItem()
				return
			end

			-- =========================
			-- key problems (подсветка/индикаторы)
			-- =========================
			local keyCount = {}
			for i = 1, #hk.editInputs do
				local k = normalize_key(hk.editInputs[i].key or "")
				if k ~= "" then
					keyCount[k] = (keyCount[k] or 0) + 1
				end
			end

			local function get_key_issue(input)
				local k = normalize_key(input.key or "")
				local issues = {
					empty = (trim(input.key or "") == ""),
					invalid = (trim(input.key or "") ~= "" and not key_is_valid(trim(input.key or ""))),
					dup = (k ~= "" and (keyCount[k] or 0) > 1),
					norm = k,
				}
				return issues
			end

			-- =========================
			-- Root split view + resizable splitter
			-- =========================

			if hk._inputsSel > #hk.editInputs then
				hk._inputsSel = #hk.editInputs
			end
			if hk._inputsSel < 1 then
				hk._inputsSel = 1
			end

			local hintHeight = imgui.GetTextLineHeightWithSpacing()
			local availHeight = imgui.GetContentRegionAvail().y - hintHeight
			local rootH = math.max(240, availHeight)

			imgui.BeginChild("inputs_root", imgui.ImVec2(0, rootH), false)

			-- LEFT (clean list: only name)
			do
				local leftW = hk._inputsLeftW
				if leftW < 120 then
					leftW = 120
				end
				if leftW > 360 then
					leftW = 360
				end
				hk._inputsLeftW = leftW

				imgui.BeginChild("inputs_left", imgui.ImVec2(leftW, 0), true)
				imgui.TextDisabled("Поля")
				imgui.Separator()

				local mutated = false

				for i = 1, #hk.editInputs do
					local input = hk.editInputs[i]
					imgui.PushIDStr("inrow" .. i)

					local title = trim(input.label or "")
					if title == "" then
						title = string.format("Поле #%d", i)
					end

					local issues = get_key_issue(input)
					local hasWarn = issues.empty or issues.invalid or issues.dup

					-- только название, но если есть проблемы, добавим аккуратный маркер
					local shown = title
					if hasWarn then
						shown = "! " .. shown
					end

					local selected = (hk._inputsSel == i)
					if imgui.Selectable(shown .. "##sel", selected) then
						hk._inputsSel = i
					end

					-- tooltip с деталями (ключ/режим/проблемы)
					if imgui.IsItemHovered() then
						imgui.BeginTooltip()
						local mode = (input.mode == "buttons") and "Кнопки" or "Текст"
						imgui.TextDisabled("Режим: " .. mode)
						local kraw = trim(input.key or "")
						if kraw == "" then
							imgui.TextDisabled("Ключ: (пусто)")
						else
							imgui.TextDisabled("Ключ: " .. kraw)
						end
						if issues.dup then
							imgui.TextDisabled("! Дубликат ключа")
						end
						if issues.invalid then
							imgui.TextDisabled("! Недопустимые символы")
						end
						if issues.empty then
							imgui.TextDisabled("! Ключ не задан")
						end
						if not issues.empty then
							imgui.TextDisabled("В тексте: {{" .. issues.norm .. "}}")
						end
						imgui.EndTooltip()
					end

					-- ПКМ: вверх/вниз/удалить/дублировать
					if imgui.BeginPopupContextItem("ctx") then
						if MenuItemEnabled("Вверх", i > 1) then
							hk.editInputs[i], hk.editInputs[i - 1] = hk.editInputs[i - 1], hk.editInputs[i]
							hk._inputsSel = i - 1
							module.saveHotkeys()
							mutated = true
						end
						if MenuItemEnabled("Вниз", i < #hk.editInputs) then
							hk.editInputs[i], hk.editInputs[i + 1] = hk.editInputs[i + 1], hk.editInputs[i]
							hk._inputsSel = i + 1
							module.saveHotkeys()
							mutated = true
						end
						imgui.Separator()
						if imgui.MenuItemBool("Дублировать", "", false, true) then
							local copy = clone_input(input)
							table.insert(hk.editInputs, i + 1, copy)
							hk._inputsSel = i + 1
							module.saveHotkeys()
							mutated = true
						end
						imgui.Separator()
						if imgui.MenuItemBool("Удалить", "", false, true) then
							table.remove(hk.editInputs, i)
							if hk._inputsSel > #hk.editInputs then
								hk._inputsSel = #hk.editInputs
							end
							if hk._inputsSel < 1 then
								hk._inputsSel = 1
							end
							module.saveHotkeys()
							mutated = true
						end
						imgui.EndPopup()
					end

					imgui.PopID()
					if mutated then
						break
					end
				end

				imgui.EndChild()
			end

			-- SPLITTER (resize left panel)
			do
				imgui.SameLine()
				local splitterW = 6
				imgui.InvisibleButton("inputs_splitter", imgui.ImVec2(splitterW, -1))

				local io = imgui.GetIO and imgui.GetIO() or nil
				if io and imgui.IsItemActive() then
					hk._inputsLeftW = hk._inputsLeftW + io.MouseDelta.x
					if hk._inputsLeftW < 120 then
						hk._inputsLeftW = 120
					end
					if hk._inputsLeftW > 360 then
						hk._inputsLeftW = 360
					end
				end

				imgui.SameLine()
			end

			-- RIGHT (editor)
			do
				imgui.BeginChild("inputs_right", imgui.ImVec2(0, 0), true)

				if #hk.editInputs == 0 then
					imgui.TextDisabled("Полей ввода нет")
					imgui.EndChild()
					imgui.EndChild()
					imgui.EndTabItem()
					return
				end

				if hk._inputsSel > #hk.editInputs then
					hk._inputsSel = #hk.editInputs
				end
				if hk._inputsSel < 1 then
					hk._inputsSel = 1
				end

				local idx = hk._inputsSel
				local input = hk.editInputs[idx]
				if not input then
					imgui.TextDisabled("Выберите поле слева.")
					imgui.EndChild()
					imgui.EndChild()
					imgui.EndTabItem()
					return
				end

				-- нормализация mode/buttons
				input.mode = (input.mode == "buttons") and "buttons" or "text"
				input.buttons = input.buttons or {}

				-- per-input cached buffers
				local ikey = tostring(input)
				local ui = hk._uiInputs.inputs[ikey]
				if not ui then
					ui = {
						labelBuf = imgui.new.char[512](""),
						labelSize = 512,
						hintBuf = imgui.new.char[256](""),
						hintSize = 256,
						keyBuf = imgui.new.char[128](""),
						keySize = 128,

						lastLabel = nil,
						lastHint = nil,
						lastKey = nil,

						btnSel = 1,
						btnStates = {}, -- per button buffers
						bulk = {
							buf = imgui.new.char[16384](""),
							size = 16384,
							inited = false,
							lastPreview = nil,
						},
					}
					hk._uiInputs.inputs[ikey] = ui
				end

				-- sync cached buffers if changed externally
				local curLabel = input.label or ""
				if ui.lastLabel ~= curLabel then
					buf_set(ui.labelBuf, ui.labelSize, curLabel)
					ui.lastLabel = curLabel
				end
				local curHint = input.hint or ""
				if ui.lastHint ~= curHint then
					buf_set(ui.hintBuf, ui.hintSize, curHint)
					ui.lastHint = curHint
				end
				local curKey = input.key or ""
				if ui.lastKey ~= curKey then
					buf_set(ui.keyBuf, ui.keySize, curKey)
					ui.lastKey = curKey
				end

				-- Header + quick actions
				imgui.Text(string.format("Редактор поля #%d", idx))
				imgui.SameLine()

				local upLbl = fa.ARROW_UP
				if not upLbl or upLbl == "" then
					upLbl = "Up"
				end
				local dnLbl = fa.ARROW_DOWN
				if not dnLbl or dnLbl == "" then
					dnLbl = "Dn"
				end
				local trLbl = fa.TRASH
				if not trLbl or trLbl == "" then
					trLbl = "X"
				end
				local cpLbl = fa.COPY
				if not cpLbl or cpLbl == "" then
					cpLbl = "Copy"
				end

				if imgui.SmallButton(upLbl .. "##r_up") and idx > 1 then
					hk.editInputs[idx], hk.editInputs[idx - 1] = hk.editInputs[idx - 1], hk.editInputs[idx]
					hk._inputsSel = idx - 1
					module.saveHotkeys()
				end
				imgui.SameLine()
				if imgui.SmallButton(dnLbl .. "##r_dn") and idx < #hk.editInputs then
					hk.editInputs[idx], hk.editInputs[idx + 1] = hk.editInputs[idx + 1], hk.editInputs[idx]
					hk._inputsSel = idx + 1
					module.saveHotkeys()
				end
				imgui.SameLine()
				if imgui.SmallButton("Дубл##r_dup") then
					local copy = clone_input(input)
					table.insert(hk.editInputs, idx + 1, copy)
					hk._inputsSel = idx + 1
					module.saveHotkeys()
				end
				imgui.SameLine()
				if imgui.SmallButton(trLbl .. "##r_del") then
					table.remove(hk.editInputs, idx)
					if hk._inputsSel > #hk.editInputs then
						hk._inputsSel = #hk.editInputs
					end
					if hk._inputsSel < 1 then
						hk._inputsSel = 1
					end
					module.saveHotkeys()
				end

				if #hk.editInputs == 0 then
					imgui.Separator()
					imgui.TextDisabled("Полей ввода нет")
					imgui.EndChild()
					imgui.EndChild()
					imgui.EndTabItem()
					return
				end

				-- пересинхрон после действий
				if hk._inputsSel > #hk.editInputs then
					hk._inputsSel = #hk.editInputs
				end
				if hk._inputsSel < 1 then
					hk._inputsSel = 1
				end
				idx = hk._inputsSel
				input = hk.editInputs[idx]
				input.mode = (input.mode == "buttons") and "buttons" or "text"
				input.buttons = input.buttons or {}

				imgui.Separator()

				-- Editor via Columns
				imgui.Columns(2, "input_fields", false)
				imgui.SetColumnWidth(0, 170)

				local function fieldRow(caption, render)
					imgui.TextDisabled(caption)
					imgui.NextColumn()
					render()
					imgui.NextColumn()
				end

				fieldRow("Тип поля", function()
					imgui.PushItemWidth(-1)
					local mode = (input.mode == "buttons") and "buttons" or "text"
					local modeBuf = imgui.new.int(mode == "buttons" and 1 or 0)
					if imgui.Combo("##input_mode", modeBuf, input_mode_labels_ffi, #input_mode_labels) then
						input.mode = modeBuf[0] == 1 and "buttons" or "text"
						module.saveHotkeys()
					end
					imgui.PopItemWidth()
				end)

				fieldRow("Текст над полем", function()
					imgui.PushItemWidth(-1)
					if imgui.InputTextMultiline("##input_label", ui.labelBuf, ui.labelSize, imgui.ImVec2(0, 64)) then
						local v = buf_get(ui.labelBuf)
						input.label = v
						ui.lastLabel = v
						module.saveHotkeys()
					end
					imgui.PopItemWidth()
				end)

				fieldRow("Подсказка", function()
					imgui.PushItemWidth(-1)
					if
						imgui.InputTextWithHint(
							"##input_hint",
							"Например координаты",
							ui.hintBuf,
							ui.hintSize,
							flags_or(imgui.InputTextFlags.AutoSelectAll)
						)
					then
						local v = buf_get(ui.hintBuf)
						input.hint = v
						ui.lastHint = v
						module.saveHotkeys()
					end
					imgui.PopItemWidth()
				end)

				fieldRow("Ключ подстановки", function()
					imgui.PushItemWidth(-1)
					if
						imgui.InputTextWithHint(
							"##input_key",
							"Например CALLSIGN",
							ui.keyBuf,
							ui.keySize,
							flags_or(imgui.InputTextFlags.AutoSelectAll)
						)
					then
						local raw = buf_get(ui.keyBuf)
						local norm = normalize_key(raw) -- авто-нормализация
						input.key = norm
						ui.lastKey = norm
						if raw ~= norm then
							buf_set(ui.keyBuf, ui.keySize, norm)
						end
						module.saveHotkeys()
					end
					imgui.PopItemWidth()

					-- подсветка проблем ключа + мини-превью
					local issues = get_key_issue(input)
					local k = normalize_key(input.key or "")

					if issues.empty then
						imgui.TextDisabled(
							"! Ключ не задан. Подстановка в тексте не сработает."
						)
					elseif issues.invalid then
						imgui.TextDisabled(
							"! В ключе лишние символы. Допустимо: латиница, цифры, _"
						)
					elseif issues.dup then
						imgui.TextDisabled(
							"! Такой ключ уже используется в другом поле."
						)
					else
						imgui.TextDisabled("Ок.")
					end

					if k ~= "" then
						imgui.TextDisabled("В тексте: {{" .. k .. "}}")
						if can_clipboard() then
							imgui.SameLine()
							if imgui.SmallButton(cpLbl .. "##copy_key") then
								imgui.SetClipboardText("{{" .. k .. "}}")
							end
						end
					end
				end)

				imgui.Columns(1)

				-- =========================
				-- Buttons mode
				-- =========================
				if input.mode == "buttons" then
					imgui.Spacing()
					imgui.Separator()
					imgui.Text("Кнопки")
					imgui.TextDisabled(
						"Обычный: список слева + редактор справа. Списком: одна строка = одна кнопка."
					)

					local tabId = "btn_tabs##" .. tostring(ikey)
					if imgui.BeginTabBar(tabId) then
						-- ========= Обычный (двухпанельный) =========
						if imgui.BeginTabItem("Обычный") then
							-- top actions
							if imgui.Button(fa.SQUARE_PLUS .. " Добавить##btn_add") then
								table.insert(input.buttons, { label = "", text = "", hint = "" })
								ui.btnSel = #input.buttons
								module.saveHotkeys()
							end
							imgui.SameLine()
							if imgui.Button("Дублировать##btn_dup") then
								local j = ui.btnSel or 1
								if j >= 1 and j <= #input.buttons then
									local copy = clone_button(input.buttons[j])
									table.insert(input.buttons, j + 1, copy)
									ui.btnSel = j + 1
									module.saveHotkeys()
								end
							end

							if ui.btnSel > #input.buttons then
								ui.btnSel = #input.buttons
							end
							if ui.btnSel < 1 then
								ui.btnSel = 1
							end

							imgui.Spacing()

							-- two-panel layout inside right panel
							local btnLeftW = 210
							imgui.BeginChild("btns_root", imgui.ImVec2(0, 320), false)

							imgui.BeginChild("btns_left", imgui.ImVec2(btnLeftW, 0), true)
							imgui.TextDisabled("Список")
							imgui.Separator()

							for j = 1, #input.buttons do
								local b = input.buttons[j]
								imgui.PushIDInt(j)

								local title = trim(b.label or "")
								if title == "" then
									title = "Кнопка " .. j
								end

								local selected = (ui.btnSel == j)
								if imgui.Selectable(title .. "##btn_sel", selected) then
									ui.btnSel = j
								end

								-- tooltip preview
								if imgui.IsItemHovered() then
									imgui.BeginTooltip()
									local t = trim(b.text or "")
									local h = trim(b.hint or "")
									if t == "" then
										t = "(текст пустой)"
									end
									if h == "" then
										h = "(подсказка пустая)"
									end
									imgui.TextDisabled("Текст:")
									imgui.Text(t)
									imgui.TextDisabled("Подсказка:")
									imgui.Text(h)
									imgui.EndTooltip()
								end

								imgui.PopID()
							end
							imgui.EndChild()

							imgui.SameLine()

							imgui.BeginChild("btns_right", imgui.ImVec2(0, 0), true)

							if #input.buttons == 0 then
								imgui.TextDisabled("Кнопок нет. Нажмите Добавить.")
							else
								if ui.btnSel > #input.buttons then
									ui.btnSel = #input.buttons
								end
								if ui.btnSel < 1 then
									ui.btnSel = 1
								end

								local j = ui.btnSel
								local b = input.buttons[j]

								-- cache buffers per button
								local bkey = tostring(b)
								local bs = ui.btnStates[bkey]
								if not bs then
									bs = {
										labelBuf = imgui.new.char[256](""),
										labelSize = 256,
										textBuf = imgui.new.char[1024](""),
										textSize = 1024,
										hintBuf = imgui.new.char[256](""),
										hintSize = 256,

										lastLabel = nil,
										lastText = nil,
										lastHint = nil,
									}
									ui.btnStates[bkey] = bs
								end

								-- sync
								local bl = b.label or ""
								if bs.lastLabel ~= bl then
									buf_set(bs.labelBuf, bs.labelSize, bl)
									bs.lastLabel = bl
								end
								local bt = b.text or ""
								if bs.lastText ~= bt then
									buf_set(bs.textBuf, bs.textSize, bt)
									bs.lastText = bt
								end
								local bh = b.hint or ""
								if bs.lastHint ~= bh then
									buf_set(bs.hintBuf, bs.hintSize, bh)
									bs.lastHint = bh
								end

								imgui.Text(string.format("Кнопка #%d", j))
								imgui.SameLine()

								local bUp = fa.ARROW_UP
								if not bUp or bUp == "" then
									bUp = "Up"
								end
								local bDn = fa.ARROW_DOWN
								if not bDn or bDn == "" then
									bDn = "Dn"
								end
								local bTr = fa.TRASH
								if not bTr or bTr == "" then
									bTr = "X"
								end

								if imgui.SmallButton(bUp .. "##btn_up") and j > 1 then
									input.buttons[j], input.buttons[j - 1] = input.buttons[j - 1], input.buttons[j]
									ui.btnSel = j - 1
									module.saveHotkeys()
								end
								imgui.SameLine()
								if imgui.SmallButton(bDn .. "##btn_dn") and j < #input.buttons then
									input.buttons[j], input.buttons[j + 1] = input.buttons[j + 1], input.buttons[j]
									ui.btnSel = j + 1
									module.saveHotkeys()
								end
								imgui.SameLine()
								if imgui.SmallButton("Дубл##btn_dup2") then
									local copy = clone_button(b)
									table.insert(input.buttons, j + 1, copy)
									ui.btnSel = j + 1
									module.saveHotkeys()
								end
								imgui.SameLine()
								if imgui.SmallButton(bTr .. "##btn_del") then
									table.remove(input.buttons, j)
									if ui.btnSel > #input.buttons then
										ui.btnSel = #input.buttons
									end
									if ui.btnSel < 1 then
										ui.btnSel = 1
									end
									module.saveHotkeys()
								end

								if #input.buttons > 0 then
									if ui.btnSel > #input.buttons then
										ui.btnSel = #input.buttons
									end
									if ui.btnSel < 1 then
										ui.btnSel = 1
									end
									b = input.buttons[ui.btnSel]

									imgui.Separator()

									imgui.TextDisabled("Название")
									imgui.PushItemWidth(-1)
									if
										imgui.InputTextWithHint(
											"##btn_label",
											"Например Кнопка 1",
											bs.labelBuf,
											bs.labelSize,
											flags_or(imgui.InputTextFlags.AutoSelectAll)
										)
									then
										local v = buf_get(bs.labelBuf)
										b.label = v
										bs.lastLabel = v
										module.saveHotkeys()
									end
									imgui.PopItemWidth()

									imgui.TextDisabled("Текст для вставки")
									imgui.PushItemWidth(-1)
									if
										imgui.InputTextMultiline(
											"##btn_text",
											bs.textBuf,
											bs.textSize,
											imgui.ImVec2(0, 80)
										)
									then
										local v = buf_get(bs.textBuf)
										b.text = v
										bs.lastText = v
										module.saveHotkeys()
									end
									imgui.PopItemWidth()

									imgui.TextDisabled("Подсказка")
									imgui.PushItemWidth(-1)
									if
										imgui.InputTextWithHint(
											"##btn_hint",
											"Подсказка",
											bs.hintBuf,
											bs.hintSize,
											flags_or(imgui.InputTextFlags.AutoSelectAll)
										)
									then
										local v = buf_get(bs.hintBuf)
										b.hint = v
										bs.lastHint = v
										module.saveHotkeys()
									end
									imgui.PopItemWidth()
								else
									imgui.Separator()
									imgui.TextDisabled("Кнопок нет.")
								end
							end

							imgui.EndChild() -- btns_right
							imgui.EndChild() -- btns_root

							imgui.EndTabItem()
						end

						-- ========= Списком (идеальный) =========
						if imgui.BeginTabItem("Списком") then
							local bulk = ui.bulk

							if not bulk.inited then
								local txt = serialize_buttons(input.buttons)
								buf_set(bulk.buf, bulk.size, txt)
								bulk.inited = true
								bulk.lastPreview = nil
							end

							-- quick templates
							if imgui.Button(fa.SQUARE_PLUS .. " Строка##bulk_add1") then
								bulk_append_line(bulk, " |  | ")
								bulk.lastPreview = nil
							end
							imgui.SameLine()
							if imgui.Button("+5##bulk_add5") then
								bulk_append_n_empty(bulk, 5)
								bulk.lastPreview = nil
							end
							imgui.SameLine()
							if imgui.Button("Шаблон##bulk_ex") then
								bulk_append_line(bulk, "Принять | /accept | Быстро принять")
								bulk.lastPreview = nil
							end

							imgui.Spacing()

							if imgui.Button("Пересобрать из текущих##bulk_sync") then
								local txt = serialize_buttons(input.buttons)
								buf_set(bulk.buf, bulk.size, txt)
								bulk.lastPreview = nil
							end
							imgui.SameLine()
							if imgui.Button("Проверить##bulk_check") then
								local buttonsTmp, st = parse_buttons_ex(buf_get(bulk.buf))
								bulk.lastPreview = { st = st, count = #buttonsTmp }
							end
							imgui.SameLine()
							if imgui.Button("Нормализовать##bulk_norm") then
								local buttonsTmp = select(1, parse_buttons_ex(buf_get(bulk.buf)))
								local txt = serialize_buttons(buttonsTmp)
								buf_set(bulk.buf, bulk.size, txt)
								bulk.lastPreview = nil
							end
							imgui.SameLine()
							if imgui.Button("Применить##bulk_apply") then
								local buttonsNew = select(1, parse_buttons_ex(buf_get(bulk.buf)))
								input.buttons = buttonsNew
								module.saveHotkeys()

								-- красиво переписать обратно
								local txt = serialize_buttons(input.buttons)
								buf_set(bulk.buf, bulk.size, txt)
								bulk.lastPreview = nil

								-- подстраховка селектора
								if ui.btnSel > #input.buttons then
									ui.btnSel = #input.buttons
								end
								if ui.btnSel < 1 then
									ui.btnSel = 1
								end
							end

							imgui.Spacing()
							imgui.TextDisabled("Формат: Название | Текст | Подсказка")
							imgui.TextDisabled(
								"Перенос в тексте: \\n, символ |: \\|. Пустое название станет Кнопка N. Строки с # игнорируются."
							)

							local h = math.min(380, 22 * math.max(10, #input.buttons + 6))
							imgui.InputTextMultiline("##bulk_buttons", bulk.buf, bulk.size, imgui.ImVec2(0, h))

							-- preview info
							if bulk.lastPreview then
								local st = bulk.lastPreview.st
								imgui.Spacing()
								imgui.TextDisabled(
									string.format(
										"Проверка: всего строк %d, использовано %d, пропущено %d. Кнопок получится %d.",
										st.total,
										st.used,
										st.ignored,
										bulk.lastPreview.count
									)
								)
								if st.extraPipes > 0 then
									imgui.TextDisabled(
										"Есть строки с лишними |. Если это часть текста, экранируйте как \\|."
									)
								end
							end

							imgui.EndTabItem()
						end

						imgui.EndTabBar()
					end
				end

				imgui.EndChild() -- inputs_right
			end

			imgui.EndChild() -- inputs_root

			imgui.EndTabItem()
		end

		--------------------------------------------------------------
		imgui.EndTabBar()
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
		hk.keys = { table.unpack(hk.editKeys) }
		hk.messages = {}
		for _, m in ipairs(hk.editMsgs) do
			table.insert(hk.messages, {
				text = m.text,
				interval = tonumber(m.interval) or 0,
				method = tonumber(m.method) or 0,
			})
		end
		hk.inputs = {}
		for _, input in ipairs(hk.editInputs or {}) do
			local key = trim(input.key or "")
			if key ~= "" then
				local mode = input.mode == "buttons" and "buttons" or "text"
				local buttons
				if mode == "buttons" then
					buttons = {}
					for _, btn in ipairs(input.buttons or {}) do
						local text = trim(btn.text or "")
						if text ~= "" then
							buttons[#buttons + 1] = {
								label = btn.label or "",
								text = btn.text or "",
								hint = btn.hint or "",
							}
						end
					end
					if #buttons == 0 then
						buttons = nil
						mode = "text"
					end
				end
				table.insert(hk.inputs, {
					label = input.label or "",
					hint = input.hint or "",
					key = key,
					mode = mode,
					buttons = buttons,
				})
			end
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
			pattern = hk.editTriggerPattern,
		}

		reset_edit_state(hk)
		editHotkey.active = false
		module.saveHotkeys()
		pushToast("Бинд сохранен: " .. (hk.label or ""), "ok", 2.5)
		return
	end
	imgui.SameLine()
	if imgui.Button(fa.XMARK .. "[CANCEL]", imgui.ImVec2(120, 0)) then
		reset_edit_state(hk)
		editHotkey.active = false
		return
	end
	imgui.SameLine()
	if imgui.Button(fa.TRASH .. " " .. "[DEL]", imgui.ImVec2(120, 0)) then
		_G.deleteBindPopup.idx = idx
		_G.deleteBindPopup.from_edit = true
		_G.deleteBindPopup.active = true
	end
	if tags and tags.showTagsWindow then
		imgui.SameLine()
		if imgui.Button(fa.TAGS .. " Переменные##open_tags") then
			tags.showTagsWindow[0] = true
		end
	end
end

local function drawDeletePopups()
	if _G.deleteBindPopup.active then
		imgui.OpenPopup("binder_delete_bind")
		_G.deleteBindPopup.active = false
	end
	if imgui.BeginPopupModal("binder_delete_bind", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local idx = _G.deleteBindPopup.idx
		local hk = idx and hotkeys[idx]
		local label = hk and trim(hk.label or "")
		if not label or label == "" then
			label = string.format("#%d", idx or 0)
		end
		imgui.Text(('Удалить бинд "%s"?'):format(label))
		imgui.Separator()
		if imgui.Button("Удалить##bind_confirm", imgui.ImVec2(100, 0)) then
			if idx and hotkeys[idx] then
				table.remove(hotkeys, idx)
				hotkeysDirty = true
				module.saveHotkeys()
			end
			if _G.deleteBindPopup.from_edit then
				editHotkey.active = false
				editHotkey.idx = nil
			end
			_G.deleteBindPopup.idx = nil
			_G.deleteBindPopup.from_edit = false
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##bind_confirm", imgui.ImVec2(100, 0)) then
			_G.deleteBindPopup.idx = nil
			_G.deleteBindPopup.from_edit = false
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end

	if _G.deleteFolderPopup.active then
		imgui.OpenPopup("binder_delete_folder")
		_G.deleteFolderPopup.active = false
	end
	if imgui.BeginPopupModal("binder_delete_folder", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local folder = _G.deleteFolderPopup.folder
		local name = folder and folder.name or ""
		imgui.Text(('Удалить папку "%s"?'):format(name))
		imgui.TextDisabled("Будут удалены все дочерние папки.")
		imgui.Separator()
		if imgui.Button("Удалить##folder_confirm", imgui.ImVec2(100, 0)) then
			if folder then
				removeFolder(folder.parent and folder.parent.children or folders, folder)
				if selectedFolder == folder then
					selectedFolder = folder.parent or folders[1]
				end
				module.saveHotkeys()
			end
			_G.deleteFolderPopup.folder = nil
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##folder_confirm", imgui.ImVec2(100, 0)) then
			_G.deleteFolderPopup.folder = nil
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

-- === Вкладки папок (с условиями быстрого меню) ===
local function drawFolderTabs()
	local tabHeight = 22
	local tabPad = 2

	local function drawFolderRow(folder, isRoot)
		local list = isRoot and folders or (folder and folder.children or {})
		local itemWidth = 110 + tabHeight
		local itemsCount = #list + 1
		local availWidth = imgui.GetContentRegionAvail().x
		local columns = math.max(1, math.floor((availWidth + tabPad) / (itemWidth + tabPad)))
		local rows = math.max(1, math.ceil(itemsCount / columns))
		local childHeight = rows * (tabHeight + tabPad) + 4

		imgui.BeginChild("folders_row_" .. (folder and folder.name or "root"), imgui.ImVec2(0, childHeight), false)

		local startPos = imgui.GetCursorScreenPos()
		local function setItemPos(idx)
			local zeroBased = idx - 1
			local row = math.floor(zeroBased / columns)
			local col = zeroBased % columns
			local posX = startPos.x + col * (itemWidth + tabPad)
			local posY = startPos.y + row * (tabHeight + tabPad)
			imgui.SetCursorScreenPos(imgui.ImVec2(posX, posY))
		end

		for i, f in ipairs(list) do
			setItemPos(i)
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
						table.insert(
							f.children,
							{ name = subName, children = {}, parent = f, quick_conditions = {}, quick_menu = true }
						)
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
				local quickMenuLabel = (fa.BOLT and fa.BOLT .. " " or "")
					.. "Быстрое меню##folder_quick_menu"
					.. tostring(f)
				f._quick_menu_bool = ensure_bool(f._quick_menu_bool, f.quick_menu ~= false)
				if imgui.Checkbox(quickMenuLabel, f._quick_menu_bool) then
					f.quick_menu = f._quick_menu_bool[0]
					module.saveHotkeys()
				end

				local headerLabel = (fa.BOLT and fa.BOLT .. " " or "")
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

				imgui.Separator()
				local canDelete = not (isRoot and f.name == "Основные")
				if canDelete then
					if imgui.SmallButton(fa.TRASH .. " Удалить папку") then
						_G.deleteFolderPopup.folder = f
						_G.deleteFolderPopup.active = true
						imgui.CloseCurrentPopup()
					end
				else
					imgui.TextDisabled(fa.TRASH .. " Удалить папку")
				end
				imgui.EndPopup()
			end
		end

		setItemPos(itemsCount)
		if
			imgui.Button(
				fa.SQUARE_PLUS .. "##add_sub" .. (folder and folder.name or "root"),
				imgui.ImVec2(itemWidth, tabHeight)
			)
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
					table.insert(
						list,
						{ name = name, children = {}, parent = folder, quick_conditions = {}, quick_menu = true }
					)
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
		local hovered = io.MousePos.x >= rect_min.x
			and io.MousePos.x <= rect_max.x
			and io.MousePos.y >= rect_min.y
			and io.MousePos.y <= rect_max.y
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

	drawDeletePopups()
	drawToasts()
end

-- === События и ввод ===
addEventHandler("onWindowMessage", function(msg, wparam, lparam)
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
		InputManager:update(msg, wparam, lparam)
		if isKeyboardKey(wparam) then
			rebuildPressedList()
		end
	elseif msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP then
		InputManager:update(msg, wparam, lparam)
		if isKeyboardKey(wparam) then
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
end)

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
						hk._lastRepeatPressed = { table.unpack(pressedKeysList) }
					elseif now - (hk.lastActivated or 0) >= sec then
						module.enqueueHotkey(hk)
						hk.lastActivated = now
						hk._lastRepeatPressed = { table.unpack(pressedKeysList) }
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
	local wasOpen = module.quickMenuOpen
	module.quickMenuOpen = isOpen
	if isOpen and not wasOpen then
		quickMenuSelectRequest = quickMenuTabIndex
	elseif not isOpen and wasOpen then
		quickMenuSelectRequest = nil
	end
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
imgui.OnFrame(function()
	return module.binderWindow[0]
end, function()
	module.DrawBinder()
end)

imgui.OnFrame(function()
	return module.quickMenuOpen
end, function()
	module.DrawQuickMenu()
end)

imgui.OnFrame(function()
	return activeInputDialog ~= nil
end, function()
	drawInputDialog()
end)

-- Автозагрузка
pcall(module.loadHotkeys)

return module
