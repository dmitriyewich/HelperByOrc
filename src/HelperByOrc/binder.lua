local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local module = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
local funcs
local paths = require("HelperByOrc.paths")
local HotkeyManager = require("HelperByOrc.hotkey_manager")
local toasts_module = {
	push = function() end,
	draw = function() end,
}
encoding.default = "CP1251"
local u8 = encoding.UTF8
local vk = require("vkeys")
local vkeys = vk
local wm = require("windows.message")
local bit = require("bit")
local tags
local trim
local ctx
-- 12 зависимостей/хелперов в одной таблице вместо 12 отдельных local
local _d = {}  -- _d.mimgui_funcs, _d.hotkey_helpers, hotkey fns, imgui safe wrappers

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	_d.mimgui_funcs = mod.mimgui_funcs or _d.mimgui_funcs or require("HelperByOrc.mimgui_funcs")

	_d.hotkey_helpers = funcs.getHotkeyHelpers(vk, L("binder.text.key"))
	_d.hotkeyToString = _d.hotkey_helpers.hotkeyToString
	_d.normalizeKey = _d.hotkey_helpers.normalizeKey
	_d.isKeyboardKey = _d.hotkey_helpers.isKeyboardKey
	_d.isHotkeyKey = _d.hotkey_helpers.isHotkeyKey or _d.hotkey_helpers.isKeyboardKey
	_d.keysMatchCombo = _d.hotkey_helpers.keysMatchCombo

	_d.flags_or = funcs.flags_or
	trim = funcs.trim

	_d.imgui_text_safe = _d.mimgui_funcs.imgui_text_safe
	_d.imgui_text_wrapped_safe = _d.mimgui_funcs.imgui_text_wrapped_safe
	_d.imgui_text_disabled_safe = _d.mimgui_funcs.imgui_text_disabled_safe
	_d.imgui_text_colored_safe = _d.mimgui_funcs.imgui_text_colored_safe
	_d.imgui_set_tooltip_safe = _d.mimgui_funcs.imgui_set_tooltip_safe

	tags = mod.tags or tags
	module.samp = mod.samp or module.samp
	toasts_module = mod.toasts or toasts_module
end

local config_manager_ref
local event_bus_ref

syncDependencies()

-- Иконки (безопасный фолбэк)
local ok_fa, fa = pcall(require, "fAwesome7")
if not ok_fa or type(fa) ~= "table" then
	fa = setmetatable({}, {
		__index = function()
			return ""
		end,
	})
end

-- === Внешние зарезервированные комбинации клавиш ===
local ext_hk = {
	list = {},
	keysEqual = function(a, b)
		if type(a) ~= "table" or type(b) ~= "table" or #a ~= #b then
			return false
		end
		local s = {}
		for _, k in ipairs(a) do s[k] = true end
		for _, k in ipairs(b) do if not s[k] then return false end end
		return true
	end,
	findLabel = function(self, keys)
		if type(keys) ~= "table" or #keys == 0 then return nil end
		for _, e in ipairs(self.list) do
			if self.keysEqual(e.keys, keys) then return e.label end
		end
		return nil
	end,
}

function module.registerExternalHotkey(keys, label)
	if type(keys) == "table" and #keys > 0 then
		table.insert(ext_hk.list, { keys = keys, label = label or L("common.placeholder_unknown") })
	end
end

function module.unregisterExternalHotkey(keys)
	if type(keys) ~= "table" then return end
	for i = #ext_hk.list, 1, -1 do
		if ext_hk.keysEqual(ext_hk.list[i].keys, keys) then
			table.remove(ext_hk.list, i)
		end
	end
end

local QUICK_MENU_FALLBACK_HOTKEY = { vk.VK_XBUTTON1 }
local QUICK_MENU_ACTIVATION_MODE_HOLD = "hold"
local QUICK_MENU_ACTIVATION_MODE_TOGGLE = "toggle"
local quickMenuHotkey = nil
local quickMenuActivationMode = QUICK_MENU_ACTIVATION_MODE_HOLD

-- === Константы ===
local C = {
	json_path                  = "binder.json",
	DEBOUNCE_MS                = 40,    -- антидребезг
	MAX_BIND_DEPTH             = 5,     -- защита от рекурсии при внешних вызовах runBind*
	MAX_ACTIVE_HOTKEYS         = 10,    -- limit of active coroutines
	HOTKEYS_SAVE_DEBOUNCE_SEC  = 0.35,
	MULTI_BUF_MIN              = 4096,
	MULTI_BUF_PAD              = 1024,
	INPUTTEXT_CALLBACK_RESIZE  = imgui.InputTextFlags and imgui.InputTextFlags.CallbackResize,
	INPUT_BUF_SIZE             = 2048,
	DIALOG_SEARCH_BUF_SIZE     = 256,
	DEFAULT_TEXT_CONFIRM_KEY   = 0x31,
	DEFAULT_TEXT_CANCEL_KEY    = 0x32,
	TEXT_CONFIRM_TIMEOUT_MS    = 2000,
}

-- === Утилиты ===
local function clone_buttons(arr)
	local copy = {}
	for _, btn in ipairs(arr or {}) do
		copy[#copy + 1] = {
			label = btn.label or "",
			text = btn.text or "",
			hint = btn.hint or "",
			when = btn.when or "",
		}
	end
	return copy
end

local function cloneKeys(list)
	local copy = {}
	for i = 1, #(list or {}) do
		local key = list[i]
		if type(key) == "number" then
			copy[#copy + 1] = key
		end
	end
	return copy
end

local function normalizeQuickMenuHotkey(keys)
	local normalizeHotkeyTable = _d.hotkey_helpers and _d.hotkey_helpers.normalizeHotkeyTable
	if type(normalizeHotkeyTable) == "function" then
		local normalized = normalizeHotkeyTable(keys)
		if normalized then
			return HotkeyManager.normalizeComboForMode(normalized, HotkeyManager.MODE_MODIFIER_TRIGGER)
		end
		return nil
	end
	local copy = cloneKeys(keys)
	return #copy > 0 and copy or nil
end

local function getQuickMenuFallbackDisplay()
	return funcs.hotkeyToString(QUICK_MENU_FALLBACK_HOTKEY, vk, "XBUTTON1")
end

local function normalizeQuickMenuActivationMode(mode)
	mode = tostring(mode or ""):lower()
	if mode == QUICK_MENU_ACTIVATION_MODE_TOGGLE then
		return QUICK_MENU_ACTIVATION_MODE_TOGGLE
	end
	return QUICK_MENU_ACTIVATION_MODE_HOLD
end

local function setQuickMenuHotkeyValue(keys)
	local prev = quickMenuHotkey and cloneKeys(quickMenuHotkey) or nil
	local normalized = normalizeQuickMenuHotkey(keys)
	if prev and #prev > 0 then
		module.unregisterExternalHotkey(prev)
	end
	quickMenuHotkey = normalized and cloneKeys(normalized) or nil
	if quickMenuHotkey and #quickMenuHotkey > 0 then
		module.registerExternalHotkey(quickMenuHotkey, L("binder.text.text"))
	end
end

local function setQuickMenuActivationModeValue(mode)
	quickMenuActivationMode = normalizeQuickMenuActivationMode(mode)
end

local INPUT_MODE = {
	TEXT = "text",
	BUTTONS_LIST = "buttons_list",
	BUTTONS_LIST_TEXT = "buttons_list_text",
}

local normalize_input_mode

local function input_mode_uses_buttons(mode)
	mode = normalize_input_mode(mode)
	return mode == INPUT_MODE.BUTTONS_LIST
		or mode == INPUT_MODE.BUTTONS_LIST_TEXT
end

normalize_input_mode = function(mode)
	mode = tostring(mode or "")
	if mode == "buttons" or mode == "buttons_combo" then
		return INPUT_MODE.BUTTONS_LIST_TEXT
	end
	if mode == INPUT_MODE.BUTTONS_LIST or mode == INPUT_MODE.BUTTONS_LIST_TEXT then
		return mode
	end
	return INPUT_MODE.TEXT
end

local function normalize_runtime_input_mode(mode, buttons)
	mode = normalize_input_mode(mode)
	if input_mode_uses_buttons(mode) then
		local hasButtons = type(buttons) == "table" and #buttons > 0
		if not hasButtons then
			return INPUT_MODE.TEXT
		end
	end
	return mode
end

local function input_mode_title(mode)
	mode = normalize_input_mode(mode)
	if mode == INPUT_MODE.BUTTONS_LIST then
		return L("binder.text.text_1")
	end
	if mode == INPUT_MODE.BUTTONS_LIST_TEXT then
		return L("binder.text.text_3")
	end
	return L("binder.text.text_4")
end

local function normalize_input_key_ref(key)
	key = tostring(key or "")
	if key == "" then
		return ""
	end
	key = key:gsub("%s+", "_")
	key = key:gsub("[^%w_]", "")
	key = key:gsub("_+", "_")
	key = key:gsub("^_+", ""):gsub("_+$", "")
	key = key:upper()
	return key
end

local function normalize_multi_separator(sep)
	sep = tostring(sep or "")
	if sep == "" then
		return ", "
	end
	return sep
end

local function normalize_text_confirmation_key(key, fallback_key)
	local nk = tonumber(key)
	if nk ~= nil then
		nk = _d.normalizeKey(nk)
	end
	if nk ~= nil and _d.isHotkeyKey and _d.isHotkeyKey(nk) then
		return nk
	end
	return fallback_key or C.DEFAULT_TEXT_CONFIRM_KEY
end

local function clone_text_confirmation(cfg)
	cfg = type(cfg) == "table" and cfg or {}
	return {
		enabled = cfg.enabled == true,
		key = normalize_text_confirmation_key(cfg.key, C.DEFAULT_TEXT_CONFIRM_KEY),
		cancel_key = normalize_text_confirmation_key(cfg.cancel_key, C.DEFAULT_TEXT_CANCEL_KEY),
		wait_for_resolution = cfg.wait_for_resolution ~= false,
	}
end

local function text_confirmation_key_label(key)
	return _d.hotkeyToString({ normalize_text_confirmation_key(key) })
end

local function normalize_runtime_input(input)
	local key = normalize_input_key_ref(input and input.key)
	if key == "" then
		return nil
	end

	local mode = normalize_input_mode(input.mode)
	local buttons = nil
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

	return {
		label = input.label or "",
		hint = input.hint or "",
		key = key,
		mode = mode,
		buttons = buttons,
		multi_select = input.multi_select == true,
		multi_separator = normalize_multi_separator(input.multi_separator),
		cascade_parent_key = normalize_input_key_ref(input.cascade_parent_key),
	}
end

local function normalize_runtime_inputs(inputs)
	local normalized = {}
	for _, input in ipairs(inputs or {}) do
		local entry = normalize_runtime_input(input)
		if entry then
			normalized[#normalized + 1] = entry
		end
	end
	return normalized
end

-- === Toasts ===
local function pushToast(...)
	if event_bus_ref then
		event_bus_ref.emit("toast", ...)
	else
		toasts_module.push(...)
	end
end

-- === Состояние модуля ===
module.binderWindow = imgui.new.bool(false)
module.showQuickMenu = imgui.new.bool(false)
module.quickMenuOpen = false

local _imguiSubs = {}
_imguiSubs[#_imguiSubs + 1] = imgui.OnInitialize(function()
	math.randomseed(os.time())
end)

-- === Данные ===
local folders = { { name = L("binder.text.text_5"), children = {}, parent = nil, quick_conditions = {}, quick_menu = true } }
local hotkeys = {}
local labelInputs = setmetatable({}, { __mode = "k" })
local nextFolderId = 1
local perf_state = {
	hotkeys_revision = 0,
	binds_grid_cache = {
		revision = -1,
		hotkeys_count = -1,
		folder_key = "",
		query = "",
		cards = {},
	},
	hotkey_runtime_cache = {
		revision = -1,
		by_len = {},
		indexed = {},
	},
	active_combo_hotkeys = {},
	active_combo_hotkeys_set = setmetatable({}, { __mode = "k" }),
}

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
	hk.editHotkeyMode = nil
	hk.editRepeatMode, hk.editQuickMenu, hk.editRepeatInterval, hk.editQuickConditions = nil, nil, nil, nil
	hk.editBulkMethod, hk.editBulkInterval, hk.editMultiline, hk.editMultiText = nil, nil, nil, nil
	reset_multi_buffer(hk)
	hk.editTextTrigger, hk.editTriggerEnabled, hk.editTriggerPattern = nil, nil, nil
	hk.editTriggerConfirmEnabled, hk.editTriggerConfirmKey = nil, nil
	hk.editTriggerConfirmCancelKey, hk.editTriggerConfirmWait = nil, nil
	hk.editInputs = nil
end

local function ensure_multi_buffer(hk)
	local text = hk.editMultiText or ""
	local len = #text
	local desired = math.max(C.MULTI_BUF_MIN, len + C.MULTI_BUF_PAD)
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

-- === State (разделяемые скалярные значения) ===
local State = {
	selectedFolder = folders[1],
	hotkeysDirty = true,
	editHotkey = editHotkey,
	quickMenuOpen = false,
	quickMenuTabIndex = 1,
	quickMenuScrollQueued = 0,
	quickMenuSelectRequest = nil,
	quickMenuReopenBlocked = false,
	quickMenuToggleLatch = false,
}

-- === Пути / папки ===
local function hotkeyFolderString(hk)
	if not hk or type(hk.folderPath) ~= "table" then
		return ""
	end
	return table.concat(hk.folderPath, "/")
end

local function pathKey(pathTbl)
	if type(pathTbl) ~= "table" or #pathTbl == 0 then
		return ""
	end
	return table.concat(pathTbl, "/")
end

perf_state.invalidateHotkeyCaches = function()
	perf_state.hotkeys_revision = perf_state.hotkeys_revision + 1
	perf_state.binds_grid_cache.revision = -1
	perf_state.hotkey_runtime_cache.revision = -1
	for i = #perf_state.active_combo_hotkeys, 1, -1 do
		local hk = perf_state.active_combo_hotkeys[i]
		if hk then
			hk._comboActive = false
			hk._lastRepeatPressed = nil
		end
		perf_state.active_combo_hotkeys[i] = nil
	end
	for hk in pairs(perf_state.active_combo_hotkeys_set) do
		perf_state.active_combo_hotkeys_set[hk] = nil
	end
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
local config = {
	hotkeys = {},
	folders = {},
	quickMenuHotkey = {},
	quickMenuActivationMode = QUICK_MENU_ACTIVATION_MODE_HOLD,
}

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

local function copyPath(path)
	local out = {}
	for i = 1, #(path or {}) do
		out[i] = path[i]
	end
	return out
end

local function pathStartsWith(path, prefix)
	if type(path) ~= "table" or type(prefix) ~= "table" then
		return false
	end
	if #prefix > #path then
		return false
	end
	for i = 1, #prefix do
		if path[i] ~= prefix[i] then
			return false
		end
	end
	return true
end

local function replacePathPrefix(path, oldPrefix, newPrefix)
	if not pathStartsWith(path, oldPrefix) then
		return copyPath(path)
	end
	local out = {}
	for i = 1, #newPrefix do
		out[#out + 1] = newPrefix[i]
	end
	for i = #oldPrefix + 1, #path do
		out[#out + 1] = path[i]
	end
	return out
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

local function isProtectedRootFolder(folder)
	return folder ~= nil and folder.parent == nil and folders[1] == folder
end

local function assignFolderId(node)
	if not node._id then
		node._id = nextFolderId
		nextFolderId = nextFolderId + 1
	end
end

local function assignFolderTree(node)
	assignFolderId(node)
	for _, child in ipairs(node.children or {}) do
		assignFolderTree(child)
	end
end

local function ensureFolderIds(list)
	if not module._folderIdSeq then
		module._folderIdSeq = 1
	end
	for _, f in ipairs(list or {}) do
		if not f._id or f._id == 0 then
			f._id = module._folderIdSeq
			module._folderIdSeq = module._folderIdSeq + 1
		end
		ensureFolderIds(f.children)
	end
end

local function createFolder(name, parent)
	local node = {
		name = name,
		children = {},
		parent = parent,
		quick_conditions = {},
		quick_menu = true,
	}
	assignFolderId(node)
	return node
end

for _, f in ipairs(folders) do
	assignFolderTree(f)
end

-- Нормализация условий: sparse/string-keyed таблица → плотный массив bool
-- Решает проблему: JSON сериализует {[3]=true} как {"3":true},
-- а при десериализации ключ "3" (строка) не совпадает с flags[3] (число).
local function normalizeConditions(raw)
	if type(raw) ~= "table" then
		return {}
	end
	local out = {}
	local maxIdx = 0
	for k, v in pairs(raw) do
		local idx = tonumber(k)
		if idx and idx >= 1 then
			idx = math.floor(idx)
			if idx > maxIdx then maxIdx = idx end
			out[idx] = v and true or false
		end
	end
	-- Заполняем пропуски false, чтобы массив был плотным
	for i = 1, maxIdx do
		if out[i] == nil then
			out[i] = false
		end
	end
	return out
end

local function serializeFolder(folder)
	local node = {
		name = folder.name,
		children = {},
		quick_conditions = normalizeConditions(folder.quick_conditions),
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
		quick_conditions = normalizeConditions(tbl.quick_conditions),
		quick_menu = tbl.quick_menu ~= false,
	}
	assignFolderId(node)
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

local function remapHotkeysFolderPrefix(oldPath, newPath)
	local changed = 0
	for _, hk in ipairs(hotkeys) do
		if pathStartsWith(hk.folderPath, oldPath) then
			hk.folderPath = replacePathPrefix(hk.folderPath, oldPath, newPath)
			changed = changed + 1
		end
	end
	return changed
end

local function moveHotkeysFromFolderPath(fromPath, toPath)
	local changed = 0
	for _, hk in ipairs(hotkeys) do
		if pathStartsWith(hk.folderPath, fromPath) then
			hk.folderPath = copyPath(toPath)
			changed = changed + 1
		end
	end
	return changed
end

-- === JSON save/load ===
function module.saveHotkeys()
	refreshHotkeyNumbers()
	config.hotkeys, config.folders = {}, {}
	config.quickMenuHotkey = cloneKeys(quickMenuHotkey)
	config.quickMenuActivationMode = quickMenuActivationMode
	for _, f in ipairs(folders) do
		table.insert(config.folders, serializeFolder(f))
	end
	for idx, hk in ipairs(hotkeys) do
		local msgs = {}
		for _, m in ipairs(hk.messages or {}) do
			table.insert(msgs, { text = m.text, interval = m.interval, method = m.method })
		end
		local inputs = normalize_runtime_inputs(hk.inputs)
		table.insert(config.hotkeys, {
			label = hk.label,
			keys = hk.keys,
			hotkey_mode = hk.hotkey_mode or HotkeyManager.MODE_MODIFIER_TRIGGER,
			repeat_mode = hk.repeat_mode,
			repeat_interval_ms = hk.repeat_interval_ms,
			enabled = hk.enabled or false,
			quick_menu = hk.quick_menu or false,
			messages = msgs,
			conditions = normalizeConditions(hk.conditions),
			quick_conditions = normalizeConditions(hk.quick_conditions),
			command = hk.command or "",
			command_enabled = hk.command_enabled or false,
			folderPath = hk.folderPath,
			text_trigger = hk.text_trigger,
			text_confirmation = clone_text_confirmation(hk.text_confirmation),
			number = hk._number or idx,
			inputs = inputs,
		})
	end
	if config_manager_ref then
		local reg_data = config_manager_ref.get("binder")
		if reg_data then
			for k in pairs(reg_data) do reg_data[k] = nil end
			for k, v in pairs(config) do reg_data[k] = v end
		end
		config_manager_ref.markDirty("binder")
	else
		local ok = funcs.saveTableToJson(config, C.json_path)
		if not ok then
			pushToast(L("binder.text.text_6"), "err", 4.0)
		end
	end
end

local flushHotkeysDirty
flushHotkeysDirty = function(force)
	if not module._hotkeysDirty then
		return
	end
	local shouldSave = force == true
	if not shouldSave then
		local dirtyAt = module._hotkeysDirtyAt or 0
		shouldSave = (os.clock() - dirtyAt) >= C.HOTKEYS_SAVE_DEBOUNCE_SEC
	end
	if shouldSave then
		module.saveHotkeys()
		module._hotkeysDirty = false
	end
end

local function markHotkeysDirty(force)
	perf_state.invalidateHotkeyCaches()
	State.hotkeysDirty = true
	module._hotkeysDirty = true
	module._hotkeysDirtyAt = os.clock()
	if force then
		flushHotkeysDirty(true)
	end
end

local function newHotkeyBase()
	return {
		label = L("binder.text.text_7"),
		keys = {},
		hotkey_mode = HotkeyManager.MODE_MODIFIER_TRIGGER,
		messages = {},
		inputs = {},
		text_trigger = { text = "", enabled = false, pattern = false },
		text_confirmation = clone_text_confirmation(),
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
		_comboActive = false,
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
	inputs,
	hotkey_mode
)
	local hk = newHotkeyBase()
	hk.hotkey_mode = HotkeyManager.normalizeMode(hotkey_mode)
	hk.keys = HotkeyManager.normalizeComboForMode(keys, hk.hotkey_mode) or {}
	hk.messages = messages or {}
	hk.inputs = normalize_runtime_inputs(inputs)
	hk.label = label or hk.label
	hk.repeat_mode = not not repeat_mode
	hk.conditions = normalizeConditions(conditions)
	hk.command = command or ""
	hk.command_enabled = not not command_enabled
	hk.folderPath = folderPath or { folders[1].name }
	hk.text_trigger = text_trigger or { text = "", enabled = false, pattern = false }
	hotkeys[#hotkeys + 1] = hk
	refreshHotkeyNumbers()
	State.hotkeysDirty = true
	perf_state.invalidateHotkeyCaches()
end

function module.loadHotkeys()
	local actual_json_path = funcs.resolveJsonPath(C.json_path)
	if doesFileExist and not doesFileExist(actual_json_path) then
		pushToast(L("binder.text.text_8"), "warn", 3.0)
	end
	local tbl
	if config_manager_ref then
		tbl = config_manager_ref.get("binder")
	else
		tbl = funcs.loadTableFromJson(C.json_path)
	end
	if type(tbl) == "table" then
		setQuickMenuHotkeyValue(tbl.quickMenuHotkey or tbl.quick_menu_hotkey)
		setQuickMenuActivationModeValue(tbl.quickMenuActivationMode or tbl.quick_menu_activation_mode)
		nextFolderId = 1
		-- Очистка in-place (сохраняет ссылки в ctx)
		for i = #hotkeys, 1, -1 do hotkeys[i] = nil end
		for i = #folders, 1, -1 do folders[i] = nil end
		if tbl.folders and #tbl.folders > 0 then
			for _, f in ipairs(tbl.folders) do
				local folder = deserializeFolder(f, nil)
				table.insert(folders, folder)
			end
		else
			table.insert(folders, createFolder(L("binder.text.text_5"), nil))
		end
		for _, f in ipairs(folders) do
			assignFolderTree(f)
		end
		State.selectedFolder = folders[1]
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
				hk.inputs,
				hk.hotkey_mode
			)
			local last = hotkeys[#hotkeys]
			last.enabled = hk.enabled == nil and true or hk.enabled
			last.quick_menu = hk.quick_menu or false
			last.repeat_interval_ms = tonumber(hk.repeat_interval_ms) or nil
			last.quick_conditions = normalizeConditions(hk.quick_conditions)
			last.text_trigger = hk.text_trigger or { text = "", enabled = false, pattern = false }
			last.text_confirmation = clone_text_confirmation(hk.text_confirmation)
			last.command_enabled = hk.command_enabled == nil and (hk.command ~= "") or hk.command_enabled
		end
		refreshHotkeyNumbers()
		State.hotkeysDirty = true
		perf_state.invalidateHotkeyCaches()
	end
end

-- === Подмодули ===
local execution = require("HelperByOrc.binder.execution")
local input_dialog = require("HelperByOrc.binder.input_dialog")
local edit_form = require("HelperByOrc.binder.edit_form")
local popups = require("HelperByOrc.binder.popups")
local quick_menu = require("HelperByOrc.binder.quick_menu")
local grid_ui = require("HelperByOrc.binder.grid_ui")

-- === ctx (контекст для подмодулей) ===
ctx = {
	-- Shared data (by reference)
	hotkeys = hotkeys,
	folders = folders,
	perf_state = perf_state,
	State = State,

	-- Module reference
	module = module,
	getQuickMenuHotkey = function()
		return quickMenuHotkey
	end,
	getQuickMenuActivationMode = function()
		return quickMenuActivationMode
	end,

	-- Dependencies
	_d = _d,
	fa = fa,
	trim = trim,
	funcs = funcs,
	tags = tags,
	ext_hk = ext_hk,
	C = C,
	INPUT_MODE = INPUT_MODE,

	-- Helper functions
	pushToast = pushToast,
	ensure_bool = ensure_bool,
	reset_edit_state = reset_edit_state,
	ensure_multi_buffer = ensure_multi_buffer,
	normalize_input_mode = normalize_input_mode,
	normalize_runtime_input = normalize_runtime_input,
	normalize_runtime_inputs = normalize_runtime_inputs,
	normalize_runtime_input_mode = normalize_runtime_input_mode,
	input_mode_uses_buttons = input_mode_uses_buttons,
	normalize_input_key_ref = normalize_input_key_ref,
	normalize_multi_separator = normalize_multi_separator,
	normalize_text_confirmation_key = normalize_text_confirmation_key,
	input_mode_title = input_mode_title,
	clone_buttons = clone_buttons,
	clone_text_confirmation = clone_text_confirmation,
	text_confirmation_key_label = text_confirmation_key_label,
	normalizeConditions = normalizeConditions,

	-- Path/folder functions
	folderFullPath = folderFullPath,
	pathKey = pathKey,
	pathEquals = pathEquals,
	copyPath = copyPath,
	pathStartsWith = pathStartsWith,
	hotkeyFolderString = hotkeyFolderString,
	findHotkeyByNumberInScope = findHotkeyByNumberInScope,
	refreshHotkeyNumbers = refreshHotkeyNumbers,
	markHotkeysDirty = markHotkeysDirty,
	flushHotkeysDirty = flushHotkeysDirty,
	newHotkeyBase = newHotkeyBase,

	-- Folder management
	ensureFolderIds = ensureFolderIds,
	sanitizeFolderName = sanitizeFolderName,
	folderNameUnique = folderNameUnique,
	isProtectedRootFolder = isProtectedRootFolder,
	createFolder = createFolder,
	removeFolder = removeFolder,
	remapHotkeysFolderPrefix = remapHotkeysFolderPrefix,
	moveHotkeysFromFolderPath = moveHotkeysFromFolderPath,

	-- UI shared
	labelInputs = labelInputs,

	-- Submodule references (set before init)
	execution = execution,
	input_dialog = input_dialog,
	edit_form = edit_form,
	popups = popups,
	quick_menu = quick_menu,
	grid_ui = grid_ui,
}

-- Init submodules
execution.init(ctx)
input_dialog.init(ctx)
edit_form.init(ctx)
popups.init(ctx)
quick_menu.init(ctx)
grid_ui.init(ctx)

-- Cross-module wiring (после init, чтобы экспорты были доступны)
ctx.combo_capture = edit_form.getComboCapture()
ctx.text_confirm_capture = edit_form.getTextConfirmCapture()
ctx.doSend = execution.doSend
ctx.preview_method_label = input_dialog.preview_method_label
ctx.enqueueHotkey = execution.enqueueHotkey
ctx.startHotkeyCoroutine = execution.startHotkeyCoroutine
ctx.requestBindLinesPopup = popups.requestBindLinesPopup
perf_state.buildQuickMenuFrameState = execution.buildQuickMenuFrameState

-- === Публичный API (делегация в подмодули) ===

function module.onWindowMessage(msg, wparam, lparam)
	return execution.onWindowMessage(msg, wparam, lparam)
end

function module.resetInputState(reason)
	execution.resetInputState(reason)
end

function module.CheckQuickMenuKey()
	execution.CheckQuickMenuKey()
end

function module.OnTick()
	execution.OnTick()
	-- sync quickMenuOpen to module for OnFrame visibility check
	module.quickMenuOpen = State.quickMenuOpen
	-- Flush dirty saves even when binder window is closed
	flushHotkeysDirty(false)
end

function module.OpenBinder()
	module.binderWindow[0] = true
end

function module.DrawBinder()
	grid_ui.DrawBinder()
end

function module.DrawQuickMenu()
	quick_menu.DrawQuickMenu()
end

function module.getSendTargets()
	return edit_form.getSendLabels()
end

function module.getQuickMenuHotkey()
	return quickMenuHotkey and cloneKeys(quickMenuHotkey) or nil
end

function module.getQuickMenuHotkeyDisplay()
	if quickMenuHotkey and #quickMenuHotkey > 0 then
		return _d.hotkeyToString(quickMenuHotkey)
	end
	return getQuickMenuFallbackDisplay()
end

function module.getQuickMenuActivationMode()
	return quickMenuActivationMode
end

function module.getQuickMenuHotkeyCapture()
	return execution.getQuickMenuHotkeyCapture()
end

function module.isQuickMenuHotkeyCaptureActive()
	local capture = execution.getQuickMenuHotkeyCapture()
	return capture and capture:isActive() or false
end

function module.startQuickMenuHotkeyCapture()
	return execution.startQuickMenuHotkeyCapture(module.getQuickMenuHotkey())
end

function module.setQuickMenuHotkey(keys)
	setQuickMenuHotkeyValue(keys)
	module.resetInputState("quick_menu_hotkey_changed")
	module.saveHotkeys()
end

function module.resetQuickMenuHotkey()
	setQuickMenuHotkeyValue(nil)
	module.resetInputState("quick_menu_hotkey_reset")
	module.saveHotkeys()
end

function module.setQuickMenuActivationMode(mode)
	setQuickMenuActivationModeValue(mode)
	module.resetInputState("quick_menu_activation_mode_changed")
	module.saveHotkeys()
end

function module.openBindLinesPopup(name_or_hk, folder)
	if type(name_or_hk) == "table" then
		return popups.requestBindLinesPopup(name_or_hk)
	end
	local hk = execution.findBind(name_or_hk, folder)
	return popups.requestBindLinesPopup(hk)
end

-- Bind control API
module.findBind = function(...) return execution.findBind(...) end
module.startBind = function(...) return execution.startBind(...) end
module.stopBind = function(...) return execution.stopBind(...) end
module.disableBind = function(...) return execution.disableBind(...) end
module.enableBind = function(...) return execution.enableBind(...) end
module.pauseBind = function(...) return execution.pauseBind(...) end
module.unpauseBind = function(...) return execution.unpauseBind(...) end
module.isBindEnded = function(...) return execution.isBindEnded(...) end
module.setBindSelector = function(...) return execution.setBindSelector(...) end
module.runBind = function(...) return execution.runBind(...) end
module.runBindRandom = function(...) return execution.runBindRandom(...) end
module.enqueueHotkey = function(...) return execution.enqueueHotkey(...) end
module.stopHotkey = function(...) return execution.stopHotkey(...) end
module.stopAllHotkeys = function(...) return execution.stopAllHotkeys(...) end
module.onIncomingTextMessage = function(...) return execution.onIncomingTextMessage(...) end
module.onOutgoingChatInput = function(...) return execution.onOutgoingChatInput(...) end
module.onOutgoingCommandInput = function(...) return execution.onOutgoingCommandInput(...) end
module.onServerMessage = function(...) return execution.onServerMessage(...) end
module.onPlayerCommand = function(...) return execution.onPlayerCommand(...) end
module.sendHotkeyCoroutine = function(...) return execution.sendHotkeyCoroutine(...) end
module.getThisbindTagValue = function(...) return execution.getThisbindTagValue(...) end
module.executeBindTagAction = function(...) return execution.executeBindTagAction(...) end
module.runScheduler = function() return execution.runScheduler() end
module.doSend = function(...) return execution.doSend(...) end

-- === Lifecycle ===

function module.attachModules(mod)
	syncDependencies(mod)
	config_manager_ref = mod.config_manager
	event_bus_ref = mod.event_bus
	if event_bus_ref then
		event_bus_ref.offByOwner("binder")
	end
	if config_manager_ref then
		config_manager_ref.register("binder", {
			path = C.json_path,
			defaults = {
				hotkeys = {},
				folders = {},
				quickMenuHotkey = {},
				quickMenuActivationMode = QUICK_MENU_ACTIVATION_MODE_HOLD,
			},
			normalize = function(loaded)
				loaded.hotkeys = type(loaded.hotkeys) == "table" and loaded.hotkeys or {}
				loaded.folders = type(loaded.folders) == "table" and loaded.folders or {}
				loaded.quickMenuHotkey = normalizeQuickMenuHotkey(
					loaded.quickMenuHotkey or loaded.quick_menu_hotkey
				) or {}
				loaded.quickMenuActivationMode = normalizeQuickMenuActivationMode(
					loaded.quickMenuActivationMode or loaded.quick_menu_activation_mode
				)
				loaded.quick_menu_hotkey = nil
				loaded.quick_menu_activation_mode = nil
				return loaded
			end,
		})
	end
	-- Update ctx refs that depend on syncDependencies
	ctx.trim = trim
	ctx.funcs = funcs
	ctx.tags = tags
end

function module.onTerminate(reason)
	for i = #_imguiSubs, 1, -1 do
		local sub = _imguiSubs[i]
		if sub and type(sub.Unsubscribe) == "function" then
			pcall(sub.Unsubscribe, sub)
		end
		_imguiSubs[i] = nil
	end
	if event_bus_ref then
		event_bus_ref.offByOwner("binder")
	end
	execution.resetInputState("terminate")
	module.binderWindow[0] = false
	pcall(flushHotkeysDirty, true)
	execution.deinit()
	input_dialog.deinit()
end

-- === Окна ImGui ===
_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
	return module.binderWindow[0]
end, function()
	grid_ui.DrawBinder()
end)

_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
	return module.quickMenuOpen
end, function()
	quick_menu.DrawQuickMenu()
end)

_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
	return popups.isBindLinesPopupActive()
end, function()
	popups.drawBindLinesPopup()
end)

_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
	return input_dialog.getActiveInputDialog() ~= nil
end, function()
	input_dialog.drawInputDialog()
end)

-- Автозагрузка
pcall(module.loadHotkeys)

module.QUICK_MENU_ACTIVATION_MODE_HOLD = QUICK_MENU_ACTIVATION_MODE_HOLD
module.QUICK_MENU_ACTIVATION_MODE_TOGGLE = QUICK_MENU_ACTIVATION_MODE_TOGGLE

return module
