local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local HotkeyManager = require("HelperByOrc.hotkey_manager")

local ctx

-- Local state
local editHotkeyNav = { open = false, targetIdx = nil, direction = nil }
local open_combo_popup = false
local open_conditions_popup = false
local open_quick_conditions_popup = false

-- Попапы редактора
local combo_capture_hk = nil
local combo_capture = HotkeyManager.new({ enter_saves = false })

-- Подписи
local send_labels = {
	"Локально",
	"Клиенту SA-MP",
	"Серверу",
	"Без отправки",
	"Написать в чат и закрыть его",
	"Написать в чат",
	"В активное диалоговое окно",
	"Скопировать в буфер обмена",
	"В консоль SF и биндера",
	"В уведомления",
}
local send_labels_ffi = imgui.new["const char*"][#send_labels](send_labels)

local input_mode_labels = {
	"Только кнопки (список)",
	"Свой текст",
	"Кнопки (комбо)",
	"Кнопки + свой текст",
}
local input_mode_labels_ffi

local input_mode_values
local input_mode_to_index
local input_mode_from_index

function M.init(c)
	ctx = c

	local INPUT_MODE = ctx.INPUT_MODE
	input_mode_labels_ffi = imgui.new["const char*"][#input_mode_labels](input_mode_labels)
	input_mode_values = {
		INPUT_MODE.BUTTONS_LIST,
		INPUT_MODE.TEXT,
		INPUT_MODE.BUTTONS_COMBO,
		INPUT_MODE.BUTTONS_LIST_TEXT,
	}

	input_mode_to_index = function(mode)
		mode = ctx.normalize_input_mode(mode)
		for i = 1, #input_mode_values do
			if input_mode_values[i] == mode then
				return i - 1
			end
		end
		return 1 -- "Свой текст"
	end

	input_mode_from_index = function(index)
		local i = (tonumber(index) or 0) + 1
		local mode = input_mode_values[i]
		if not mode then
			return INPUT_MODE.TEXT
		end
		return mode
	end
end

-- === ВАЛИДАТОР ===
local function folderExistsByPath(path)
	if not path or #path == 0 then
		return false
	end
	local nodeList = ctx.folders
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
		t[#t + 1] = ctx._d.normalizeKey(k)
	end
	return table.concat(t, ",")
end

local function validateHotkeyEdit(hkEdit, idxSelf)
	local errs = {}
	local hotkeys = ctx.hotkeys
	local trim = ctx.trim
	local normalize_input_mode = ctx.normalize_input_mode
	local input_mode_uses_buttons = ctx.input_mode_uses_buttons

	if not hkEdit.editLabel or hkEdit.editLabel:gsub("%s+", "") == "" then
		errs[#errs + 1] = "Название бинда пустое"
	end

	local fpath = hotkeys[idxSelf] and hotkeys[idxSelf].folderPath or { ctx.folders[1].name }
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
			errs[#errs + 1] = "Интервал повтора должен быть числом >= 50 мс"
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
			if input_mode_uses_buttons(normalize_input_mode(input.mode)) then
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
	local normalize_input_mode = ctx.normalize_input_mode
	local normalize_input_key_ref = ctx.normalize_input_key_ref
	local clone_buttons = ctx.clone_buttons
	local normalize_multi_separator = ctx.normalize_multi_separator
	local ensure_bool = ctx.ensure_bool
	local cond_labels, cond_count = ctx.execution.getCondLabels()
	local quick_cond_labels, quick_cond_count = ctx.execution.getQuickCondLabels()

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
				key = normalize_input_key_ref(input.key),
				mode = normalize_input_mode(input.mode),
				buttons = clone_buttons(input.buttons),
				multi_select = input.multi_select == true,
				multi_separator = normalize_multi_separator(input.multi_separator),
				cascade_parent_key = normalize_input_key_ref(input.cascade_parent_key),
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
		hk._activeTab = 0
	end
end

local function syncMessagesToMulti(hk)
	local lines = {}
	for _, m in ipairs(hk.editMsgs or {}) do
		table.insert(lines, m.text or "")
	end
	hk.editMultiText = table.concat(lines, "\n")
	-- Синхронизируем bulk-настройки из первой строки (если есть)
	if hk.editMsgs and hk.editMsgs[1] then
		hk.editBulkMethod = hk.editMsgs[1].method or hk.editBulkMethod or 0
		hk.editBulkInterval = hk.editMsgs[1].interval or hk.editBulkInterval or "0"
	end
	-- Сброс буфера, чтобы переотрисовался мульти-ввод
	hk._multiBufText = nil
end

local function parseMultiTextToMessages(hk)
	local msgs = {}
	local text = hk and hk.editMultiText or ""
	local defaultInterval = hk and hk.editBulkInterval or "0"
	local defaultMethod = hk and hk.editBulkMethod or 0
	local oldMsgs = hk and hk.editMsgs or {}
	local lineIdx = 0
	for line in text:gmatch("[^\r\n]+") do
		if line ~= "" then
			lineIdx = lineIdx + 1
			-- Сохраняем per-line interval/method из существующих строк по позиции
			local old = oldMsgs[lineIdx]
			local interval = defaultInterval
			local method = defaultMethod
			if old and old.text == line then
				interval = old.interval or defaultInterval
				method = old.method or defaultMethod
			end
			table.insert(msgs, { text = line, interval = interval, method = method })
		end
	end
	return msgs
end

local function syncMultiToMessages(hk)
	hk.editMsgs = parseMultiTextToMessages(hk)
	ctx.markHotkeysDirty()
end

local function openComboPopupNow()
	imgui.OpenPopup("Назначить новую комбинацию")
	combo_capture:start()
end

local function drawKeyCapturePopup(hk)
	local fa = ctx.fa
	local _d = ctx._d
	local markHotkeysDirty = ctx.markHotkeysDirty

	combo_capture_hk = hk
	if
		imgui.BeginPopupModal(
			"Назначить новую комбинацию",
			nil,
			imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoMove
		)
	then
		-- ESC в onWindowMessage мог остановить захват — закрываем попап
		if not combo_capture:isActive() then
			imgui.CloseCurrentPopup()
			imgui.EndPopup()
			return true
		end
		_d.imgui_text_safe((fa.KEYBOARD or "") .. "\t" .. "Зажмите нужные клавиши")
		_d.imgui_text_safe(combo_capture:getDraftString())
		if imgui.Button((fa.XMARK or "X") .. " [CANCEL]") then
			combo_capture:stop()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button((fa.FLOPPY_DISK or "S") .. " [SAVE]") then
			local keys = combo_capture:getDraft()
			hk.editKeys = {}
			for _, k in ipairs(keys) do
				table.insert(hk.editKeys, k)
			end
			combo_capture:stop()
			imgui.CloseCurrentPopup()
			markHotkeysDirty()
		end
		imgui.EndPopup()
		return true
	end
	return false
end

local function drawConditionsPopup(hk)
	local fa = ctx.fa
	local _d = ctx._d
	local ensure_bool = ctx.ensure_bool
	local markHotkeysDirty = ctx.markHotkeysDirty
	local cond_labels, cond_count = ctx.execution.getCondLabels()

	if imgui.BeginPopup("conditions_popup") then
		_d.imgui_text_safe(fa.CHECK_DOUBLE .. " " .. "Условия активации")
		for i = 1, cond_count do
			hk._cond_bools[i] = ensure_bool(hk._cond_bools[i], hk.editConditions[i])
			if imgui.Checkbox(cond_labels[i], hk._cond_bools[i]) then
				for j = 1, cond_count do
					hk.editConditions[j] = hk._cond_bools[j][0] and true or false
				end
				markHotkeysDirty()
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
	local fa = ctx.fa
	local _d = ctx._d
	local ensure_bool = ctx.ensure_bool
	local markHotkeysDirty = ctx.markHotkeysDirty
	local quick_cond_labels, quick_cond_count = ctx.execution.getQuickCondLabels()

	if imgui.BeginPopup("quick_conditions_popup") then
		_d.imgui_text_safe(fa.BOLT .. " " .. "Появление в быстром меню")
		for i = 1, quick_cond_count do
			hk._quick_cond_bools[i] = ensure_bool(hk._quick_cond_bools[i], hk.editQuickConditions[i])
			if imgui.Checkbox(quick_cond_labels[i], hk._quick_cond_bools[i]) then
				for j = 1, quick_cond_count do
					hk.editQuickConditions[j] = hk._quick_cond_bools[j][0] and true or false
				end
				markHotkeysDirty()
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

local function drawInputsTabContent(hk)
	local fa = ctx.fa
	local _d = ctx._d
	local trim = ctx.trim
	local markHotkeysDirty = ctx.markHotkeysDirty
	local normalize_input_mode = ctx.normalize_input_mode
	local input_mode_uses_buttons = ctx.input_mode_uses_buttons
	local normalize_multi_separator = ctx.normalize_multi_separator
	local normalize_input_key_ref = ctx.normalize_input_key_ref
	local input_mode_title = ctx.input_mode_title
	local INPUT_MODE = ctx.INPUT_MODE

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
			when = b.when or "",
		}
	end

	local function clone_input(inp)
		local out = {
			label = inp.label or "",
			hint = inp.hint or "",
			key = inp.key or "",
			mode = normalize_input_mode(inp.mode),
			buttons = {},
			multi_select = inp.multi_select == true,
			multi_separator = normalize_multi_separator(inp.multi_separator),
			cascade_parent_key = normalize_input_key_ref(inp.cascade_parent_key),
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
				local when = b.when or ""
				lines[#lines + 1] = string.format(
					"%s | %s | %s | %s",
					escape_field(label),
					escape_field(text),
					escape_field(hint),
					escape_field(when)
				)
			end
			return table.concat(lines, "\n")
		end

		local function parse_buttons_ex(multiline)
			local res = {}
			local stats = {
				total = 0,
				used = 0,
				ignored = 0,
				extraPipes = 0,
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
					local c = trim(parts[3] or "")
					local d
					if #parts <= 4 then
						d = trim(parts[4] or "")
					else
						stats.extraPipes = stats.extraPipes + 1
						d = trim(table.concat(parts, "|", 4))
					end

					a = unescape_field(a)
					b = unescape_field(b)
					c = unescape_field(c)
					d = unescape_field(d)

					a = trim(a)
					if a == "" then
						a = "Кнопка " .. tostring(#res + 1)
					end

					res[#res + 1] = { label = a, text = b, hint = c, when = d }
				end
			end

			return res, stats
		end

		local function bulk_append_line(bulk, line)
			local cur = buf_get(bulk.buf)
			if cur ~= "" and cur:sub(-1) ~= "\n" then
				cur = cur .. "\n"
			end
			cur = cur .. (line or " |  |  | ")
			buf_set(bulk.buf, bulk.size, cur)
		end

		local function bulk_append_n_empty(bulk, n)
			for _ = 1, n do
				bulk_append_line(bulk, " |  |  | ")
			end
		end

	local function queue_inputs_save(force_now)
		markHotkeysDirty(force_now == true)
	end

	-- =========================
	-- UI top
	-- =========================

	imgui.TextDisabled("Используйте {{ключ}} в тексте сообщений")
	if imgui.Button(fa.SQUARE_PLUS .. " Добавить поле") then
		table.insert(hk.editInputs, {
			label = "",
			hint = "",
			key = "",
			mode = "text",
			buttons = {},
			multi_select = false,
			multi_separator = ", ",
			cascade_parent_key = "",
		})
		hk._inputsSel = #hk.editInputs
		queue_inputs_save()
	end
	imgui.SameLine()
	local helpIcon = (fa.CIRCLE_QUESTION and fa.CIRCLE_QUESTION ~= "") and fa.CIRCLE_QUESTION or "?"
	if imgui.Button(helpIcon .. "##inputs_help") then
		imgui.OpenPopup("inputs_help_popup")
	end
	if imgui.BeginPopup("inputs_help_popup") then
		imgui.TextDisabled("Режимы поля ввода")
		imgui.BulletText("Только кнопки (список): выбор только кнопкой, без своего текста.")
		imgui.BulletText("Свой текст: обычное поле ввода, только ручной ввод.")
		imgui.BulletText("Кнопки (комбо): старый режим с выбором через Combo + ручной ввод.")
		imgui.BulletText("Кнопки + свой текст: кнопки списком и возможность допечатать вручную.")
		imgui.Separator()
		imgui.TextDisabled("Подсказки")
		imgui.BulletText("Используйте {{KEY}} в сообщениях для подстановки значения поля.")
		imgui.BulletText("В списке кнопок работает поиск по названию, тексту и подсказке.")
		imgui.BulletText("Кнопки без текста не попадут в окно ввода.")
			imgui.BulletText("Мультивыбор: в режиме списка можно собрать несколько кнопок в одно поле.")
			imgui.BulletText("Каскад: укажите KEY родителя и when у кнопок дочернего поля.")
			imgui.BulletText("When: список значений через | или , (по label/text родителя).")
			imgui.BulletText("Перед отправкой доступен предпросмотр итогового текста бинда.")
		imgui.EndPopup()
	end
	imgui.SameLine()
	imgui.TextDisabled("Слева список, справа редактор")

		imgui.TextDisabled("Режим редактора: Расширенный")
	imgui.TextDisabled("Примеры: {{NICK}}, {{PHONE}}, {{COORDS}}")
	imgui.TextDisabled("В тексте бинда: Привет, {{NICK}} | Мой номер: {{PHONE}}")

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
			table.insert(hk.editInputs, {
			label = "",
			hint = "",
			key = "",
			mode = "text",
			buttons = {},
			multi_select = false,
			multi_separator = ", ",
			cascade_parent_key = "",
		})
			hk._inputsSel = 1
			queue_inputs_save()
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

	local issueEmpty, issueInvalid, issueDup = 0, 0, 0
	local firstIssueIndex = nil
	for i = 1, #hk.editInputs do
		local issues = get_key_issue(hk.editInputs[i])
		if issues.empty then
			issueEmpty = issueEmpty + 1
		end
		if issues.invalid then
			issueInvalid = issueInvalid + 1
		end
		if issues.dup then
			issueDup = issueDup + 1
		end
		if (issues.empty or issues.invalid or issues.dup) and not firstIssueIndex then
			firstIssueIndex = i
		end
	end
	if firstIssueIndex then
		_d.imgui_text_colored_safe(
			imgui.ImVec4(1.0, 0.75, 0.35, 1.0),
			string.format(
				"Проблемы ключей: пустые %d, недопустимые %d, дубликаты %d",
				issueEmpty,
				issueInvalid,
				issueDup
			)
		)
		imgui.SameLine()
		if imgui.SmallButton("Перейти к проблеме##inputs_issue_jump") then
			hk._inputsSel = firstIssueIndex
		end
	else
		imgui.TextDisabled("Ключи полей: без ошибок")
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
				local mode = input_mode_title(input.mode)
				_d.imgui_text_disabled_safe("Режим: " .. mode)
				local kraw = trim(input.key or "")
				if kraw == "" then
					imgui.TextDisabled("Ключ: (пусто)")
				else
					_d.imgui_text_disabled_safe("Ключ: " .. kraw)
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
					_d.imgui_text_disabled_safe("В тексте: {{" .. issues.norm .. "}}")
				end
				imgui.EndTooltip()
			end

			imgui.PopID()
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
		input.mode = normalize_input_mode(input.mode)
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
				multiSepBuf = imgui.new.char[64](""),
				multiSepSize = 64,
				cascadeBuf = imgui.new.char[128](""),
				cascadeSize = 128,

				lastLabel = nil,
				lastHint = nil,
				lastKey = nil,
				lastMultiSep = nil,
				lastCascade = nil,

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
		if not ui.multiSepBuf then
			ui.multiSepSize = ui.multiSepSize or 64
			ui.multiSepBuf = imgui.new.char[ui.multiSepSize]("")
			ui.lastMultiSep = nil
		end
		if not ui.cascadeBuf then
			ui.cascadeSize = ui.cascadeSize or 128
			ui.cascadeBuf = imgui.new.char[ui.cascadeSize]("")
			ui.lastCascade = nil
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
		input.multi_select = input.multi_select == true
		input.multi_separator = normalize_multi_separator(input.multi_separator)
		input.cascade_parent_key = normalize_input_key_ref(input.cascade_parent_key)
		local curMultiSep = input.multi_separator
		if ui.lastMultiSep ~= curMultiSep then
			buf_set(ui.multiSepBuf, ui.multiSepSize, curMultiSep)
			ui.lastMultiSep = curMultiSep
		end
		local curCascade = input.cascade_parent_key
		if ui.lastCascade ~= curCascade then
			buf_set(ui.cascadeBuf, ui.cascadeSize, curCascade)
			ui.lastCascade = curCascade
		end

		-- Header + quick actions
		_d.imgui_text_safe(string.format("Редактор поля #%d", idx))
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
				queue_inputs_save()
			end
			imgui.SameLine()
			if imgui.SmallButton(dnLbl .. "##r_dn") and idx < #hk.editInputs then
				hk.editInputs[idx], hk.editInputs[idx + 1] = hk.editInputs[idx + 1], hk.editInputs[idx]
				hk._inputsSel = idx + 1
				queue_inputs_save()
			end
			imgui.SameLine()
			if imgui.SmallButton("Дубл##r_dup") then
				local copy = clone_input(input)
				table.insert(hk.editInputs, idx + 1, copy)
				hk._inputsSel = idx + 1
				queue_inputs_save()
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
				queue_inputs_save()
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
		input.mode = normalize_input_mode(input.mode)
		input.buttons = input.buttons or {}

		imgui.Separator()

		-- Editor via Columns
		imgui.Columns(2, "input_fields", false)
		imgui.SetColumnWidth(0, 170)

		local function fieldRow(caption, render)
			_d.imgui_text_disabled_safe(caption)
			imgui.NextColumn()
			render()
			imgui.NextColumn()
		end

		fieldRow("Тип поля", function()
			imgui.PushItemWidth(-1)
			local modeBuf = imgui.new.int(input_mode_to_index(input.mode))
			if imgui.Combo("##input_mode", modeBuf, input_mode_labels_ffi, #input_mode_labels) then
				input.mode = input_mode_from_index(modeBuf[0])
				queue_inputs_save()
			end
			imgui.PopItemWidth()
		end)
		local modeValue = normalize_input_mode(input.mode)
		if input_mode_uses_buttons(modeValue) then
			fieldRow("Мультивыбор кнопок", function()
				local multiBuf = imgui.new.bool(input.multi_select == true)
				if imgui.Checkbox("##input_multi_select", multiBuf) then
					input.multi_select = multiBuf[0]
					queue_inputs_save()
				end
			end)

			if input.multi_select and (modeValue == INPUT_MODE.BUTTONS_LIST or modeValue == INPUT_MODE.BUTTONS_LIST_TEXT) then
				fieldRow("Разделитель мультивыбора", function()
					imgui.PushItemWidth(-1)
					if
						imgui.InputTextWithHint(
							"##input_multi_sep",
							", ",
							ui.multiSepBuf,
							ui.multiSepSize,
							_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
						)
					then
						local v = normalize_multi_separator(buf_get(ui.multiSepBuf))
						input.multi_separator = v
						ui.lastMultiSep = v
						buf_set(ui.multiSepBuf, ui.multiSepSize, v)
						queue_inputs_save()
					end
					imgui.PopItemWidth()
				end)
			end

			fieldRow("Каскад от поля (KEY)", function()
				imgui.PushItemWidth(-1)
				if
					imgui.InputTextWithHint(
						"##input_cascade_key",
						"Например DEPT",
						ui.cascadeBuf,
						ui.cascadeSize,
						_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
					)
				then
					local v = normalize_input_key_ref(buf_get(ui.cascadeBuf))
					input.cascade_parent_key = v
					ui.lastCascade = v
					buf_set(ui.cascadeBuf, ui.cascadeSize, v)
					queue_inputs_save()
				end
				imgui.PopItemWidth()
				imgui.TextDisabled("Пусто = без каскада")
			end)
		end

		fieldRow("Заголовок поля", function()
			imgui.PushItemWidth(-1)
			if imgui.InputTextMultiline("##input_label", ui.labelBuf, ui.labelSize, imgui.ImVec2(0, 64)) then
				local v = buf_get(ui.labelBuf)
				input.label = v
				ui.lastLabel = v
				queue_inputs_save()
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
					_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
				)
			then
				local v = buf_get(ui.hintBuf)
				input.hint = v
				ui.lastHint = v
				queue_inputs_save()
			end
			imgui.PopItemWidth()
		end)

		fieldRow("Код переменной ({{CODE}})", function()
			imgui.PushItemWidth(-1)
			if
				imgui.InputTextWithHint(
					"##input_key",
					"Например CALLSIGN",
					ui.keyBuf,
					ui.keySize,
					_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
				)
			then
				local raw = buf_get(ui.keyBuf)
				local norm = normalize_key(raw) -- авто-нормализация
				input.key = norm
				ui.lastKey = norm
				if raw ~= norm then
					buf_set(ui.keyBuf, ui.keySize, norm)
				end
				queue_inputs_save()
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
				_d.imgui_text_disabled_safe("В тексте: {{" .. k .. "}}")
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
		if input_mode_uses_buttons(normalize_input_mode(input.mode)) then
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
						table.insert(input.buttons, { label = "", text = "", hint = "", when = "" })
						ui.btnSel = #input.buttons
						queue_inputs_save()
					end
					imgui.SameLine()
					if imgui.Button("Дублировать##btn_dup") then
						local j = ui.btnSel or 1
						if j >= 1 and j <= #input.buttons then
							local copy = clone_button(input.buttons[j])
							table.insert(input.buttons, j + 1, copy)
							ui.btnSel = j + 1
							queue_inputs_save()
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
							_d.imgui_text_safe(t)
							imgui.TextDisabled("Подсказка:")
							_d.imgui_text_safe(h)
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
								whenBuf = imgui.new.char[256](""),
								whenSize = 256,

								lastLabel = nil,
								lastText = nil,
								lastHint = nil,
								lastWhen = nil,
							}
							ui.btnStates[bkey] = bs
						end
						if not bs.whenBuf then
							bs.whenSize = bs.whenSize or 256
							bs.whenBuf = imgui.new.char[bs.whenSize]("")
							bs.lastWhen = nil
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
						local bw = b.when or ""
						if bs.lastWhen ~= bw then
							buf_set(bs.whenBuf, bs.whenSize, bw)
							bs.lastWhen = bw
						end

						_d.imgui_text_safe(string.format("Кнопка #%d", j))
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
							queue_inputs_save()
						end
						imgui.SameLine()
						if imgui.SmallButton(bDn .. "##btn_dn") and j < #input.buttons then
							input.buttons[j], input.buttons[j + 1] = input.buttons[j + 1], input.buttons[j]
							ui.btnSel = j + 1
							queue_inputs_save()
						end
						imgui.SameLine()
						if imgui.SmallButton("Дубл##btn_dup2") then
							local copy = clone_button(b)
							table.insert(input.buttons, j + 1, copy)
							ui.btnSel = j + 1
							queue_inputs_save()
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
							queue_inputs_save()
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
									_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
								)
							then
								local v = buf_get(bs.labelBuf)
								b.label = v
								bs.lastLabel = v
								queue_inputs_save()
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
								queue_inputs_save()
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
									_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
								)
							then
								local v = buf_get(bs.hintBuf)
								b.hint = v
								bs.lastHint = v
								queue_inputs_save()
							end
							imgui.PopItemWidth()

							imgui.TextDisabled("Показывать когда (when)")
							imgui.PushItemWidth(-1)
							if
								imgui.InputTextWithHint(
									"##btn_when",
									"Например Полиция|Полицейский",
									bs.whenBuf,
									bs.whenSize,
									_d.flags_or(imgui.InputTextFlags.AutoSelectAll)
								)
							then
								local v = buf_get(bs.whenBuf)
								b.when = v
								bs.lastWhen = v
								queue_inputs_save()
							end
							imgui.PopItemWidth()
							imgui.TextDisabled("Пусто = кнопка видна всегда")
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
						bulk_append_line(bulk, " |  |  | ")
						bulk.lastPreview = nil
					end
					imgui.SameLine()
					if imgui.Button("+5##bulk_add5") then
						bulk_append_n_empty(bulk, 5)
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
						queue_inputs_save()

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
					imgui.TextDisabled("Формат: Название | Текст | Подсказка | When")
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
end

local function drawEditHotkey(idx)
	local hotkeys = ctx.hotkeys
	local editHotkey = ctx.State.editHotkey
	local fa = ctx.fa
	local _d = ctx._d
	local trim = ctx.trim
	local tags = ctx.tags
	local ext_hk = ctx.ext_hk
	local C = ctx.C
	local markHotkeysDirty = ctx.markHotkeysDirty
	local pushToast = ctx.pushToast
	local pathEquals = ctx.pathEquals
	local reset_edit_state = ctx.reset_edit_state
	local ensure_bool = ctx.ensure_bool
	local ensure_multi_buffer = ctx.ensure_multi_buffer
	local normalize_input_mode = ctx.normalize_input_mode
	local normalize_runtime_input_mode = ctx.normalize_runtime_input_mode
	local input_mode_uses_buttons = ctx.input_mode_uses_buttons
	local normalize_input_key_ref = ctx.normalize_input_key_ref
	local normalize_multi_separator = ctx.normalize_multi_separator
	local INPUT_MODE = ctx.INPUT_MODE
	local cond_labels, cond_count = ctx.execution.getCondLabels()
	local quick_cond_labels, quick_cond_count = ctx.execution.getQuickCondLabels()

	local hk = hotkeys[idx]
	if not hk then
		return
	end
	ensureEditBuffers(hk)

	-- Шапка
	local headerAvailW = imgui.GetContentRegionAvail().x
	local compactHeader = headerAvailW < 640
	imgui.BeginChild("edit_header", imgui.ImVec2(0, compactHeader and 62 or 30), false)
	if imgui.Button(fa.ARROW_LEFT .. " Назад", imgui.ImVec2(95, 0)) then
		reset_edit_state(hk)
		editHotkey.active = false
		return
	end
	imgui.SameLine()
	_d.imgui_text_colored_safe(imgui.GetStyle().Colors[imgui.Col.Text], fa.PEN .. " Редактирование бинда")
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

	local function disabled_button(label, enabled, size)
		if enabled then
			return imgui.Button(label, size)
		end
		if imgui.BeginDisabled then
			imgui.BeginDisabled(true)
			imgui.Button(label, size)
			imgui.EndDisabled()
		elseif imgui.PushStyleVarFloat then
			imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, imgui.GetStyle().Alpha * 0.5)
			imgui.Button(label, size)
			imgui.PopStyleVar()
		else
			imgui.Button(label, size)
		end
		return false
	end

	local prevIdx, nextIdx, totalInFolder = get_prev_next_idx(idx)
	if totalInFolder > 1 then
		if compactHeader then
			imgui.NewLine()
			local navGap = imgui.GetStyle().ItemSpacing.x
			local navAvail = imgui.GetContentRegionAvail().x
			if navAvail < 230 then
				if disabled_button(fa.ARROW_LEFT .. " Предыдущий", prevIdx ~= nil, imgui.ImVec2(-1, 0)) then
					editHotkeyNav.targetIdx = prevIdx
					editHotkeyNav.direction = "предыдущему"
					editHotkeyNav.open = true
				end
				if disabled_button("Следующий " .. fa.ARROW_RIGHT, nextIdx ~= nil, imgui.ImVec2(-1, 0)) then
					editHotkeyNav.targetIdx = nextIdx
					editHotkeyNav.direction = "следующему"
					editHotkeyNav.open = true
				end
			else
				local navBtnWidth = math.floor((navAvail - navGap) / 2)
				if navBtnWidth < 110 then
					navBtnWidth = 110
				end
				if disabled_button(fa.ARROW_LEFT .. " Предыдущий", prevIdx ~= nil, imgui.ImVec2(navBtnWidth, 0)) then
					editHotkeyNav.targetIdx = prevIdx
					editHotkeyNav.direction = "предыдущему"
					editHotkeyNav.open = true
				end
				imgui.SameLine()
				if disabled_button("Следующий " .. fa.ARROW_RIGHT, nextIdx ~= nil, imgui.ImVec2(navBtnWidth, 0)) then
					editHotkeyNav.targetIdx = nextIdx
					editHotkeyNav.direction = "следующему"
					editHotkeyNav.open = true
				end
			end
		else
			local navWidth = 280
			local navStartX = imgui.GetWindowContentRegionMax().x - navWidth
			if navStartX > imgui.GetCursorPosX() then
				imgui.SameLine(navStartX)
			else
				imgui.SameLine()
			end
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
	end
	imgui.EndChild()

	local navSwitched = false
	if editHotkeyNav.open then
		imgui.OpenPopup("binder_nav_confirm")
		editHotkeyNav.open = false
	end
	if imgui.BeginPopupModal("binder_nav_confirm", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local direction = editHotkeyNav.direction or "следующему"
		_d.imgui_text_safe(("Перейти к %s бинду?"):format(direction))
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

	local editorAvailW = imgui.GetContentRegionAvail().x
	local compactFooter = editorAvailW < 620
	local hasTagsButton = tags and tags.showTagsWindow
	local editStyle = imgui.GetStyle()
	local frameH = (imgui.GetFrameHeight and imgui.GetFrameHeight())
		or (imgui.GetTextLineHeight() + editStyle.FramePadding.y * 2)
	local footerRows = 1
	if compactFooter then
		footerRows = 2 -- сохранить + строка с отменой (и, возможно, переменными)
		if hasTagsButton and editorAvailW < 260 then
			footerRows = 3 -- переменные и отмена в разных строках
		end
	end
	local footerReserve = math.floor(
		footerRows * frameH
		+ math.max(0, footerRows - 1) * editStyle.ItemSpacing.y
		+ editStyle.ItemSpacing.y
		+ 4
	)
	imgui.BeginChild("edit_main", imgui.ImVec2(0, -footerReserve), true)

	local topStyle = imgui.GetStyle()
	local topSpacing = topStyle.ItemSpacing.x
	local topAvailWidth = imgui.GetContentRegionAvail().x
	local compactMeta = topAvailWidth < 620

	if compactMeta then
		_d.imgui_text_disabled_safe("Название")
		imgui.PushItemWidth(-1)
		local labelBuf = imgui.new.char[256](hk.editLabel)
		if imgui.InputText("##edit_label", labelBuf, ffi.sizeof(labelBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editLabel = ffi.string(labelBuf)
		end
		imgui.PopItemWidth()

		imgui.Spacing()
		_d.imgui_text_disabled_safe("Условия")
		local condBtnAvail = imgui.GetContentRegionAvail().x
		if condBtnAvail < 280 then
			if imgui.Button(fa.SLIDERS .. " Условия##open_conditions", imgui.ImVec2(-1, 0)) then
				open_conditions_popup = true
			end
			if imgui.Button(fa.BOLT .. " Быстрое меню##open_quick_conditions", imgui.ImVec2(-1, 0)) then
				open_quick_conditions_popup = true
			end
		else
			local condBtnGap = topStyle.ItemSpacing.x
			local condBtnWidth = (condBtnAvail - condBtnGap) * 0.5
			if imgui.Button(fa.SLIDERS .. " Условия##open_conditions", imgui.ImVec2(condBtnWidth, 0)) then
				open_conditions_popup = true
			end
			imgui.SameLine()
			if imgui.Button(fa.BOLT .. " Быстрое меню##open_quick_conditions", imgui.ImVec2(condBtnWidth, 0)) then
				open_quick_conditions_popup = true
			end
		end
	else
		local topRightWidth = math.floor(topAvailWidth * 0.34)
		if topRightWidth < 250 then
			topRightWidth = 250
		end
		if topRightWidth > 360 then
			topRightWidth = 360
		end
		local topLeftWidth = topAvailWidth - topRightWidth - topSpacing
		if topLeftWidth < 220 then
			topLeftWidth = 220
			topRightWidth = topAvailWidth - topLeftWidth - topSpacing
		end

		imgui.Columns(2, "edit_hotkey_meta_row", false)
		imgui.SetColumnWidth(0, topLeftWidth)

		_d.imgui_text_disabled_safe("Название")
		imgui.NextColumn()
		_d.imgui_text_disabled_safe("Условия")
		imgui.NextColumn()

		imgui.PushItemWidth(-1)
		local labelBuf = imgui.new.char[256](hk.editLabel)
		if imgui.InputText("##edit_label", labelBuf, ffi.sizeof(labelBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editLabel = ffi.string(labelBuf)
		end
		imgui.PopItemWidth()
		imgui.NextColumn()

		local condBtnGap = topStyle.ItemSpacing.x
		local condBtnAvail = imgui.GetContentRegionAvail().x
		local condBtnWidth = (condBtnAvail - condBtnGap) * 0.5
		if condBtnWidth < 100 then
			condBtnWidth = 100
		end
		if imgui.Button(fa.SLIDERS .. " Условия##open_conditions", imgui.ImVec2(condBtnWidth, 0)) then
			open_conditions_popup = true
		end
		imgui.SameLine()
		if imgui.Button(fa.BOLT .. " Быстрое меню##open_quick_conditions", imgui.ImVec2(condBtnWidth, 0)) then
			open_quick_conditions_popup = true
		end
		imgui.NextColumn()
		imgui.Columns(1)
	end

	imgui.Spacing()

	local keyStr = _d.hotkeyToString(hk.editKeys)
	local fieldsAvail = imgui.GetContentRegionAvail().x
	local compactFields = fieldsAvail < 640
	local trigBuf = imgui.new.char[256](hk.editTextTrigger or "")
	local cmdBuf = imgui.new.char[256](hk.editCommand)

	if compactFields then
		_d.imgui_text_disabled_safe("Клавиши")
		local keyBtnWidth = imgui.GetContentRegionAvail().x
		if keyBtnWidth > 220 then
			keyBtnWidth = 220
		end
		if imgui.Button(keyStr .. "##editkeys", imgui.ImVec2(keyBtnWidth, 0)) then
			open_combo_popup = true
		end
		do
			local extLabel = ext_hk:findLabel(hk.editKeys)
			if extLabel then
				_d.imgui_text_colored_safe(imgui.ImVec4(1, 0.6, 0.2, 1), "Занято: " .. tostring(extLabel))
			end
		end

		_d.imgui_text_disabled_safe("Активация по тексту в чате")
		imgui.PushItemWidth(-1)
		if imgui.InputText("##text_trigger", trigBuf, ffi.sizeof(trigBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editTextTrigger = ffi.string(trigBuf)
		end
		imgui.PopItemWidth()

		_d.imgui_text_disabled_safe("Команда")
		if imgui.Checkbox("Включить##cmd_enable", hk._bools.commandEnabled) then
			hk.editCommandEnabled = hk._bools.commandEnabled[0]
		end
		imgui.PushItemWidth(-1)
		if imgui.InputText("##edit_command", cmdBuf, ffi.sizeof(cmdBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editCommand = ffi.string(cmdBuf)
		end
		imgui.PopItemWidth()
	else
		local fieldsGap = topSpacing * 2
		local fieldsKeyWidth = 220
		local fieldsCmdWidth = 270
		local fieldsTriggerWidth = fieldsAvail - fieldsKeyWidth - fieldsCmdWidth - fieldsGap
		if fieldsTriggerWidth < 240 then
			fieldsTriggerWidth = 240
			local overflow = fieldsKeyWidth + fieldsCmdWidth + fieldsTriggerWidth + fieldsGap - fieldsAvail
			if overflow > 0 then
				local takeFromCmd = math.min(overflow, math.max(0, fieldsCmdWidth - 220))
				fieldsCmdWidth = fieldsCmdWidth - takeFromCmd
				overflow = overflow - takeFromCmd
			end
			if overflow > 0 then
				local takeFromKey = math.min(overflow, math.max(0, fieldsKeyWidth - 170))
				fieldsKeyWidth = fieldsKeyWidth - takeFromKey
				overflow = overflow - takeFromKey
			end
			if overflow > 0 then
				fieldsTriggerWidth = math.max(180, fieldsTriggerWidth - overflow)
			end
		end

		imgui.Columns(3, "edit_hotkey_primary_fields", false)
		imgui.SetColumnWidth(0, fieldsKeyWidth)
		imgui.SetColumnWidth(1, fieldsTriggerWidth)

		_d.imgui_text_disabled_safe("Клавиши")
		local keyBtnWidth = imgui.GetContentRegionAvail().x
		if keyBtnWidth < 110 then
			keyBtnWidth = 110
		end
		if imgui.Button(keyStr .. "##editkeys", imgui.ImVec2(keyBtnWidth, 0)) then
			open_combo_popup = true
		end
		do
			local extLabel = ext_hk:findLabel(hk.editKeys)
			if extLabel then
				_d.imgui_text_colored_safe(imgui.ImVec4(1, 0.6, 0.2, 1), "Занято: " .. tostring(extLabel))
			end
		end
		imgui.NextColumn()

		_d.imgui_text_disabled_safe("Активация по тексту в чате")
		imgui.PushItemWidth(-1)
		if imgui.InputText("##text_trigger", trigBuf, ffi.sizeof(trigBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editTextTrigger = ffi.string(trigBuf)
		end
		imgui.PopItemWidth()
		imgui.NextColumn()

		_d.imgui_text_disabled_safe("Команда")
		if imgui.Checkbox("##cmd_enable", hk._bools.commandEnabled) then
			hk.editCommandEnabled = hk._bools.commandEnabled[0]
		end
		if imgui.IsItemHovered() then
			_d.imgui_set_tooltip_safe("Включить команду")
		end
		imgui.SameLine()
		imgui.PushItemWidth(-1)
		if imgui.InputText("##edit_command", cmdBuf, ffi.sizeof(cmdBuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			hk.editCommand = ffi.string(cmdBuf)
		end
		imgui.PopItemWidth()
		imgui.NextColumn()
		imgui.Columns(1)
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

	imgui.Spacing()

	-- Чекбоксы и повтор
	hk._bools.quick = ensure_bool(hk._bools.quick, hk.editQuickMenu)
	hk._bools.rep = ensure_bool(hk._bools.rep, hk.editRepeatMode)
	local rbuf = imgui.new.char[32](hk.editRepeatInterval or "")
	local function draw_repeat_interval(width)
		imgui.PushItemWidth(width)
		local repeatChanged
		if imgui.InputTextWithHint then
			repeatChanged = imgui.InputTextWithHint(
				"##repInt",
				"Интервал, мс",
				rbuf,
				ffi.sizeof(rbuf),
				_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
			)
		else
			repeatChanged = imgui.InputText(
				"Интервал, мс##repInt",
				rbuf,
				ffi.sizeof(rbuf),
				_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
			)
		end
		if repeatChanged then
			local s = ffi.string(rbuf)
			if s == "" or tonumber(s) then
				hk.editRepeatInterval = s
			end
		end
		imgui.PopItemWidth()
	end

	local optionsAvail = imgui.GetContentRegionAvail().x
	local compactOptions = optionsAvail < 620
	if compactOptions then
		if imgui.Checkbox(fa.BOLT .. " Быстрое меню##quick_menu", hk._bools.quick) then
			hk.editQuickMenu = hk._bools.quick[0]
			markHotkeysDirty()
		end
		if imgui.Checkbox(fa.REPEAT .. " Повтор##repeat", hk._bools.rep) then
			hk.editRepeatMode = hk._bools.rep[0]
			markHotkeysDirty()
		end
		local repeatWidth = imgui.GetContentRegionAvail().x
		if repeatWidth > 180 then
			repeatWidth = 180
		end
		draw_repeat_interval(repeatWidth)
		if imgui.Checkbox("Активация по тексту##trigger_enable", hk._bools.triggerEnabled) then
			hk.editTriggerEnabled = hk._bools.triggerEnabled[0]
			markHotkeysDirty()
		end
		if imgui.Checkbox("Lua Pattern##trigger_pattern", hk._bools.triggerPattern) then
			hk.editTriggerPattern = hk._bools.triggerPattern[0]
			markHotkeysDirty()
		end
	else
		if imgui.Checkbox(fa.BOLT .. " Быстрое меню##quick_menu", hk._bools.quick) then
			hk.editQuickMenu = hk._bools.quick[0]
			markHotkeysDirty()
		end

		imgui.SameLine()
		if imgui.Checkbox(fa.REPEAT .. " Повтор##repeat", hk._bools.rep) then
			hk.editRepeatMode = hk._bools.rep[0]
			markHotkeysDirty()
		end

		imgui.SameLine()
		draw_repeat_interval(120)

		if imgui.Checkbox("Активация по тексту##trigger_enable", hk._bools.triggerEnabled) then
			hk.editTriggerEnabled = hk._bools.triggerEnabled[0]
			markHotkeysDirty()
		end
		imgui.SameLine()
		if imgui.Checkbox("Lua Pattern##trigger_pattern", hk._bools.triggerPattern) then
			hk.editTriggerPattern = hk._bools.triggerPattern[0]
			markHotkeysDirty()
		end
	end

	imgui.Separator()
	if imgui.BeginTabBar("edit_bind_tabs") then
		if imgui.BeginTabItem("Строки") then
			if hk._activeTab ~= 0 then
				-- При переключении из мульти-ввода — парсим текст обратно в строки
				if hk._activeTab == 1 then
					syncMultiToMessages(hk)
				end
				hk._activeTab = 0
			end

			local allMBuf = imgui.new.int(hk.editBulkMethod or 0)
			local allIBuf = imgui.new.char[16](tostring(hk.editBulkInterval or "0"))
			local bulkControlsW = imgui.GetContentRegionAvail().x
			local compactBulkControls = bulkControlsW < 560

			if compactBulkControls then
				_d.imgui_text_disabled_safe("Куда")
				imgui.PushItemWidth(-1)
				if imgui.Combo("##allm", allMBuf, send_labels_ffi, #send_labels) then
					hk.editBulkMethod = allMBuf[0]
					for _, m in ipairs(hk.editMsgs) do
						m.method = hk.editBulkMethod
					end
					markHotkeysDirty()
				end
				imgui.PopItemWidth()

				_d.imgui_text_disabled_safe("Интервал, мс")
				local bulkIntervalWidth = imgui.GetContentRegionAvail().x
				if bulkIntervalWidth > 130 then
					bulkIntervalWidth = 130
				end
				imgui.PushItemWidth(bulkIntervalWidth)
				if
					imgui.InputText(
						"##alli",
						allIBuf,
						ffi.sizeof(allIBuf),
						_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
					)
				then
					local s = ffi.string(allIBuf)
					if s == "" or tonumber(s) then
						hk.editBulkInterval = s
						for _, m in ipairs(hk.editMsgs) do
							m.interval = s
						end
						markHotkeysDirty()
					end
				end
				imgui.PopItemWidth()

				if imgui.Button(fa.SQUARE_PLUS .. " " .. "Добавить строку##add_line", imgui.ImVec2(-1, 0)) then
					table.insert(hk.editMsgs, { text = "", interval = "0", method = 0 })
					markHotkeysDirty()
				end
			else
				local bulkSpacing = topStyle.ItemSpacing.x * 2
				local bulkDelayW = 120
				local bulkActionW = 185
				local bulkTargetW = bulkControlsW - bulkDelayW - bulkActionW - bulkSpacing
				if bulkTargetW < 180 then
					bulkTargetW = 180
					local overflow = bulkTargetW + bulkDelayW + bulkActionW + bulkSpacing - bulkControlsW
					if overflow > 0 then
						local takeFromAction = math.min(overflow, math.max(0, bulkActionW - 150))
						bulkActionW = bulkActionW - takeFromAction
						overflow = overflow - takeFromAction
					end
					if overflow > 0 then
						bulkDelayW = math.max(90, bulkDelayW - overflow)
					end
				end

				imgui.Columns(3, "messages_bulk_controls", false)
				imgui.SetColumnWidth(0, bulkTargetW)
				imgui.SetColumnWidth(1, bulkDelayW)
				imgui.SetColumnWidth(2, bulkActionW)

				_d.imgui_text_disabled_safe("Куда")
				imgui.PushItemWidth(-1)
				if imgui.Combo("##allm", allMBuf, send_labels_ffi, #send_labels) then
					hk.editBulkMethod = allMBuf[0]
					for _, m in ipairs(hk.editMsgs) do
						m.method = hk.editBulkMethod
					end
					markHotkeysDirty()
				end
				imgui.PopItemWidth()
				imgui.NextColumn()

				_d.imgui_text_disabled_safe("Интервал, мс")
				imgui.PushItemWidth(-1)
				if
					imgui.InputText(
						"##alli",
						allIBuf,
						ffi.sizeof(allIBuf),
						_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
					)
				then
					local s = ffi.string(allIBuf)
					if s == "" or tonumber(s) then
						hk.editBulkInterval = s
						for _, m in ipairs(hk.editMsgs) do
							m.interval = s
						end
						markHotkeysDirty()
					end
				end
				imgui.PopItemWidth()
				imgui.NextColumn()

				_d.imgui_text_disabled_safe("Действия")
				if imgui.Button(fa.SQUARE_PLUS .. " " .. "Добавить строку##add_line", imgui.ImVec2(-1, 0)) then
					table.insert(hk.editMsgs, { text = "", interval = "0", method = 0 })
					markHotkeysDirty()
				end
				imgui.NextColumn()
				imgui.Columns(1)
			end

			imgui.TextDisabled("Подсказки: функциональные переменные из tags.lua, например [waitif(expr)]")
			imgui.Spacing()

			imgui.BeginChild("messages_list", imgui.ImVec2(0, 0), false)
			local msgGridTotalW = imgui.GetContentRegionAvail().x
			local msgDelayW = 90
			local msgMethodW = 170
			local msgActionsW = 34
			local msgSpacingW = topStyle.ItemSpacing.x * 3
			local msgTextW = msgGridTotalW - msgDelayW - msgMethodW - msgActionsW - msgSpacingW
			if msgTextW < 240 then
				msgTextW = 240
				local overflow = msgTextW + msgDelayW + msgMethodW + msgActionsW + msgSpacingW - msgGridTotalW
				if overflow > 0 then
					local takeFromMethod = math.min(overflow, math.max(0, msgMethodW - 130))
					msgMethodW = msgMethodW - takeFromMethod
					overflow = overflow - takeFromMethod
				end
				if overflow > 0 then
					local takeFromDelay = math.min(overflow, math.max(0, msgDelayW - 70))
					msgDelayW = msgDelayW - takeFromDelay
					overflow = overflow - takeFromDelay
				end
				if overflow > 0 then
					msgTextW = math.max(160, msgTextW - overflow)
				end
			end

			imgui.Columns(4, "messages_grid", false)
			imgui.SetColumnWidth(0, msgTextW)
			imgui.SetColumnWidth(1, msgDelayW)
			imgui.SetColumnWidth(2, msgMethodW)
			imgui.SetColumnWidth(3, msgActionsW)

			_d.imgui_text_disabled_safe("Сообщение")
			imgui.NextColumn()
			_d.imgui_text_disabled_safe("Задержка")
			imgui.NextColumn()
			_d.imgui_text_disabled_safe("Отправка")
			imgui.NextColumn()
			_d.imgui_text_disabled_safe(" ")
			imgui.NextColumn()
			imgui.Separator()

			local removeMsgIdx = nil
			local moveMsgSrc = nil
			local moveMsgDst = nil
			local moveHandleWidth = 22
			local moveHandleIcon = (fa.ARROWS_UP_DOWN and fa.ARROWS_UP_DOWN ~= "") and fa.ARROWS_UP_DOWN
				or ((fa.BARS and fa.BARS ~= "") and fa.BARS or "::")
			local trashIcon = (fa.TRASH and fa.TRASH ~= "") and fa.TRASH or "X"
			for i, m in ipairs(hk.editMsgs) do
				if removeMsgIdx or moveMsgSrc then
					break
				end
				imgui.PushIDStr("row" .. i)

				if imgui.Button(moveHandleIcon .. "##drag", imgui.ImVec2(moveHandleWidth, 20)) then
					-- drag handle only
				end
				if imgui.IsItemHovered() then
					_d.imgui_set_tooltip_safe("Перетащите, чтобы изменить порядок строки")
				end
				if imgui.BeginDragDropSource() then
					local payload = ffi.new("int[1]", i)
					imgui.SetDragDropPayload("BINDER_MSG_ROW", payload, ffi.sizeof(payload))
					_d.imgui_text_safe(string.format("Строка %d", i))
					local preview = trim(m.text or "")
					if preview == "" then
						preview = "(пусто)"
					elseif #preview > 80 then
						preview = preview:sub(1, 77) .. "..."
					end
					_d.imgui_text_disabled_safe(preview)
					imgui.EndDragDropSource()
				end
				imgui.SameLine()

				local msgInputWidth = imgui.GetContentRegionAvail().x
				if msgInputWidth < 70 then
					msgInputWidth = 70
				end
				imgui.PushItemWidth(msgInputWidth)
				local tbuf = imgui.new.char[256](m.text or "")
				if imgui.InputText("##t", tbuf, ffi.sizeof(tbuf), _d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
					m.text = ffi.string(tbuf)
					markHotkeysDirty()
				end
				imgui.PopItemWidth()

				local style = imgui.GetStyle()
				local charCountText = u8:decode(m.text or "")
				local charCountLabel = string.format("%d", #charCountText or "")
				local rectMin, rectMax = imgui.GetItemRectMin(), imgui.GetItemRectMax()
				local textSize = imgui.CalcTextSize(charCountLabel)
				local padding = style.FramePadding
				local overlayPos = imgui.ImVec2(rectMax.x - padding.x - textSize.x, rectMin.y + padding.y)
				local disabledColor = style.Colors[imgui.Col.TextDisabled]
				imgui.GetWindowDrawList():AddText(overlayPos, imgui.GetColorU32Vec4(disabledColor), charCountLabel)

				if imgui.BeginDragDropTarget() then
					local payload = imgui.AcceptDragDropPayload("BINDER_MSG_ROW")
					if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
						local delivered = payload.Delivery
						if delivered == nil and imgui.IsMouseReleased then
							delivered = imgui.IsMouseReleased(0)
						end
						if delivered then
							local srcIdx = ffi.cast("int*", payload.Data)[0]
							local dstIdx = i
							if dstIdx < 1 then
								dstIdx = 1
							end
							if dstIdx > (#hk.editMsgs + 1) then
								dstIdx = #hk.editMsgs + 1
							end
							if srcIdx >= 1 and srcIdx <= #hk.editMsgs then
								if dstIdx ~= srcIdx then
									moveMsgSrc = srcIdx
									moveMsgDst = dstIdx
								end
							end
						end
					end
					imgui.EndDragDropTarget()
				end
				imgui.NextColumn()

				imgui.PushItemWidth(-1)
				local ibuf = imgui.new.char[16](tostring(m.interval or "0"))
				if
					imgui.InputText(
						"##i",
						ibuf,
						ffi.sizeof(ibuf),
						_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
					)
				then
					local s = ffi.string(ibuf)
					if tonumber(s) then
						m.interval = s
					elseif s == "" then
						m.interval = "0"
					end
					markHotkeysDirty()
				end
				imgui.PopItemWidth()
				imgui.NextColumn()

				imgui.PushItemWidth(-1)
				local mbuf = imgui.new.int(m.method or 0)
				if imgui.Combo("##m", mbuf, send_labels_ffi, #send_labels) then
					m.method = mbuf[0]
					markHotkeysDirty()
				end
				imgui.PopItemWidth()
				imgui.NextColumn()

				local delWidth = imgui.GetContentRegionAvail().x
				if delWidth < 24 then
					delWidth = 24
				end
				if imgui.Button(trashIcon .. "##del", imgui.ImVec2(delWidth, 20)) then
					removeMsgIdx = i
				end
				imgui.NextColumn()
				imgui.PopID()
			end

			imgui.Columns(1)
			if moveMsgSrc and moveMsgDst and moveMsgSrc ~= moveMsgDst then
				local moved = table.remove(hk.editMsgs, moveMsgSrc)
				if moved then
					table.insert(hk.editMsgs, moveMsgDst, moved)
					markHotkeysDirty()
				end
			end
			if removeMsgIdx then
				table.remove(hk.editMsgs, removeMsgIdx)
				markHotkeysDirty()
			end
			imgui.EndChild()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem("Мульти-ввод") then
			if hk._activeTab ~= 1 then
				-- При переключении из строк — собираем текст из editMsgs
				if hk._activeTab == 0 then
					syncMessagesToMulti(hk)
				end
				hk._activeTab = 1
			end

			imgui.PushItemWidth(220)
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
					_d.flags_or(imgui.InputTextFlags.CharsDecimal, imgui.InputTextFlags.AutoSelectAll)
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
			local flags = C.INPUTTEXT_CALLBACK_RESIZE and _d.flags_or(C.INPUTTEXT_CALLBACK_RESIZE) or nil
			local changed
			local hintHeight = imgui.GetTextLineHeightWithSpacing()
			local availHeight = imgui.GetContentRegionAvail().y - hintHeight
			local multilineHeight = math.max(80, availHeight)
			local multilineSize = imgui.ImVec2(0, multilineHeight)
			local multiInputResizeCallbackPtr = ctx.input_dialog.getMultiInputResizeCallbackPtr()
			if multiInputResizeCallbackPtr and C.INPUTTEXT_CALLBACK_RESIZE then
				ctx.input_dialog.setCurrentMultiInputHK(hk)
				changed = imgui.InputTextMultiline(
					"##multi_text",
					buf,
					bufSize,
					multilineSize,
					flags,
					multiInputResizeCallbackPtr
				)
				ctx.input_dialog.setCurrentMultiInputHK(nil)
			else
				changed = imgui.InputTextMultiline("##multi_text", buf, bufSize, multilineSize)
			end
			local activeBuf = hk._multiBuf or buf
			if changed then
				local newText = ffi.string(activeBuf)
				hk.editMultiText = newText
				hk._multiBufText = newText
				if not (multiInputResizeCallbackPtr and C.INPUTTEXT_CALLBACK_RESIZE) then
					local needed = math.max(C.MULTI_BUF_MIN, #newText + C.MULTI_BUF_PAD)
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
			drawInputsTabContent(hk)
			imgui.EndTabItem()
		end

		--------------------------------------------------------------
		imgui.EndTabBar()
	end

	imgui.EndChild()

	-- Низ
	imgui.Separator()
	local footerStyle = imgui.GetStyle()
	local footerSpacing = footerStyle.ItemSpacing.x
	local footerAvail = imgui.GetContentRegionAvail().x
	local savePressed = false
	local cancelPressed = false

	local function center_cursor_x(width)
		local avail = imgui.GetContentRegionAvail().x
		if avail > width then
			imgui.SetCursorPosX(imgui.GetCursorPosX() + (avail - width) * 0.5)
		end
	end

	if compactFooter then
		local compactSaveW = math.min(220, math.max(120, footerAvail - 24))
		center_cursor_x(compactSaveW)
		savePressed = imgui.Button(fa.FLOPPY_DISK .. " Сохранить##edit_save", imgui.ImVec2(compactSaveW, 0))

		local compactSideW = math.min(140, math.max(110, footerAvail - 24))
		if hasTagsButton then
			local pairTotal = compactSideW * 2 + footerSpacing
			if pairTotal <= footerAvail then
				center_cursor_x(pairTotal)
				if imgui.Button(fa.TAGS .. " Переменные##open_tags", imgui.ImVec2(compactSideW, 0)) then
					tags.showTagsWindow[0] = true
				end
				imgui.SameLine()
				cancelPressed = imgui.Button(fa.XMARK .. " Отмена##edit_cancel", imgui.ImVec2(compactSideW, 0))
			else
				center_cursor_x(compactSideW)
				if imgui.Button(fa.TAGS .. " Переменные##open_tags", imgui.ImVec2(compactSideW, 0)) then
					tags.showTagsWindow[0] = true
				end
				center_cursor_x(compactSideW)
				cancelPressed = imgui.Button(fa.XMARK .. " Отмена##edit_cancel", imgui.ImVec2(compactSideW, 0))
			end
		else
			center_cursor_x(compactSideW)
			cancelPressed = imgui.Button(fa.XMARK .. " Отмена##edit_cancel", imgui.ImVec2(compactSideW, 0))
		end
	else
		local sideBtnWidth = 140
		local saveBtnWidth = 220
		local btnCount = hasTagsButton and 3 or 2
		local minNeeded = saveBtnWidth + sideBtnWidth * (btnCount - 1) + footerSpacing * (btnCount - 1)
		if footerAvail < minNeeded then
			local scale = footerAvail / minNeeded
			if scale < 0.75 then
				scale = 0.75
			end
			sideBtnWidth = math.floor(sideBtnWidth * scale)
			saveBtnWidth = math.floor(saveBtnWidth * scale)
		end

		local totalW = saveBtnWidth + sideBtnWidth + footerSpacing
		if hasTagsButton then
			totalW = totalW + sideBtnWidth + footerSpacing
		end
		center_cursor_x(totalW)

		if hasTagsButton then
			if imgui.Button(fa.TAGS .. " Переменные##open_tags", imgui.ImVec2(sideBtnWidth, 0)) then
				tags.showTagsWindow[0] = true
			end
			imgui.SameLine()
		end
		savePressed = imgui.Button(fa.FLOPPY_DISK .. " Сохранить##edit_save", imgui.ImVec2(saveBtnWidth, 0))
		imgui.SameLine()
		cancelPressed = imgui.Button(fa.XMARK .. " Отмена##edit_cancel", imgui.ImVec2(sideBtnWidth, 0))
	end

	if savePressed then
		if hk._activeTab == 1 then
			-- Сохраняем из мульти-ввода — парсим текст в строки
			hk.editMsgs = parseMultiTextToMessages(hk)
		end

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
			local key = normalize_input_key_ref(input.key)
			if key ~= "" then
				local mode = normalize_input_mode(input.mode)
				local buttons
				if input_mode_uses_buttons(mode) then
					buttons = {}
					for _, btn in ipairs(input.buttons or {}) do
						local text = trim(btn.text or "")
						if text ~= "" then
							buttons[#buttons + 1] = {
								label = btn.label or "",
								text = btn.text or "",
								hint = btn.hint or "",
								when = btn.when or "",
							}
						end
					end
					mode = normalize_runtime_input_mode(mode, buttons)
					if mode == INPUT_MODE.TEXT then
						buttons = nil
					end
				end
				table.insert(hk.inputs, {
					label = input.label or "",
					hint = input.hint or "",
					key = key,
					mode = mode,
					buttons = buttons,
					multi_select = input.multi_select == true,
					multi_separator = normalize_multi_separator(input.multi_separator),
					cascade_parent_key = normalize_input_key_ref(input.cascade_parent_key),
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
		markHotkeysDirty(true)
		pushToast("Бинд сохранен: " .. (hk.label or ""), "ok", 2.5)
		return
	end
	if cancelPressed then
		reset_edit_state(hk)
		editHotkey.active = false
		return
	end
end

-- Exports
M.drawEditHotkey = drawEditHotkey
M.validateHotkeyEdit = validateHotkeyEdit
M.getEditHotkeyNav = function() return editHotkeyNav end
M.getSendLabels = function() return send_labels, send_labels_ffi end
M.getComboCapture = function() return combo_capture end
M.input_mode_to_index = function(...) return input_mode_to_index(...) end
M.input_mode_from_index = function(...) return input_mode_from_index(...) end

return M
