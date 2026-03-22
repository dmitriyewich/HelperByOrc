local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local module = {}

local imgui = require("mimgui")

local ffi = require("ffi")

local paths = require("HelperByOrc.paths")

local ok_encoding, encoding = pcall(require, "encoding")

local u8 = function(text)
	return tostring(text or "")
end

if ok_encoding and encoding and encoding.UTF8 then
	encoding.default = "CP1251"
	u8 = encoding.UTF8
end
local mimgui_funcs
local funcs

local ok_fa, fa = pcall(require, "fAwesome7") -- необязательно, UI работает и без иконок
local vk_module
local vk_module_tried = false
local game_keys_module
local game_keys_module_tried = false

local samp
local binder_module
local toasts_module = {
	push = function()
	end,
}
local imgui_text_safe
local imgui_text_colored_safe


local function syncDependencies(mod)
	mod = mod or {}

	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")

	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")

	samp = mod.samp or samp
	binder_module = mod.binder or binder_module
	toasts_module = mod.toasts or toasts_module

	imgui_text_safe = mimgui_funcs.imgui_text_safe
	imgui_text_colored_safe = mimgui_funcs.imgui_text_colored_safe
end

local config_manager_ref
local event_bus_ref

syncDependencies()

-- ========== КОНФИГ / ХРАНИЛИЩА / НАСТРОЙКИ ==========
local CONFIG_PATH_REL = "tags.json"

-- пользовательские переменные + кэш парсинга
local custom_vars, parse_cache = {}, {}

-- FIFO-кэш с лимитом
local PARSE_CACHE_MAX = 200
local parse_cache_order = {}
local parse_cache_scope_seq = 0
local parse_cache_scope_key = "0"

-- буферы ввода для UI
local cvar_bufs = {}

local function rebuild_cvar_buffers()
	if not (imgui and imgui.new) then
		return
	end
	for k in pairs(cvar_bufs) do
		cvar_bufs[k] = nil
	end
	for k, v in pairs(custom_vars) do
		cvar_bufs[k] = imgui.new.char[256](tostring(v or ""))
	end
end

-- настройки модуля
local settings = {
	show_target_notice = true,
	allow_unsafe = true, -- разрешать [call(...)]
	show_target_blip = false, -- маркер над педом при выборе цели
}

-- базовые пользовательские переменные по умолчанию
local builtin_custom_vars = {
	myorg = L("tags.text.text"),
	myorgrang = L("tags.text.text_1"),
}

-- состояние таргета
local target = {
	current_ped = nil,
	current_id = nil,
	last_id = nil,
	_notice_id = nil,
	_blip_ped = nil, -- ped, для которого сейчас стоит маркер
}
local active_key_holds = {}
local KEY_EMULATE_TAP_MS = 35

local function resolve_binder()
	if binder_module then
		return binder_module
	end
	local ok, mod = pcall(require, "HelperByOrc.binder")
	if ok and mod then
		binder_module = mod
		return binder_module
	end
	return nil
end

local function get_vk_module()
	if vk_module or vk_module_tried then
		return vk_module
	end
	vk_module_tried = true
	local ok, mod = pcall(require, "vkeys")
	if ok and mod then
		vk_module = mod
	end
	return vk_module
end

local function get_game_keys_module()
	if game_keys_module or game_keys_module_tried then
		return game_keys_module
	end
	game_keys_module_tried = true
	local ok, mod = pcall(require, "game.keys")
	if ok and mod then
		game_keys_module = mod
	end
	return game_keys_module
end

-- ========== УТИЛИТЫ ==========
local function strip_tag(nick)
	return nick and nick:gsub("^%b[]", "") or nick
end

local function log_chat(msg, color)
	local kind = "ok"
	if color == 0xAA3333 then
		kind = "err"
	elseif color == 0xAA8800 then
		kind = "warn"
	end
	local dur = kind == "err" and 4.0 or 3.0
	if event_bus_ref then
		event_bus_ref.emit("toast", tostring(msg), kind, dur)
	else
		toasts_module.push(tostring(msg), kind, dur)
	end
end

-- гарантируем наличие папки перед записью файла
local function ensure_parent_dir(file_path)
	local p = tostring(file_path or ""):gsub("/", "\\")
	local dir = p:match("^(.*)\\[^\\]+$") or ""
	if dir == "" then
		return
	end
	if type(doesDirectoryExist) == "function" and doesDirectoryExist(dir) then
		return
	end
	if type(createDirectory) == "function" then
		createDirectory(dir)
	end
end

local function save_config()
	if config_manager_ref then
		config_manager_ref.markDirty("tags")
		return
	end
	local data = { vars = custom_vars, settings = settings }
	local resolved = funcs.resolveJsonPath(CONFIG_PATH_REL)
	ensure_parent_dir(resolved)
	if funcs.saveTableToJson(data, CONFIG_PATH_REL) then
		return
	end
	local encoded = funcs.encodeJsonSafe(data, { prefer_neat = false, indent = true })
	funcs.writeFile(resolved, encoded, "w+")
end

local CONFIG_SAVE_DEBOUNCE_SEC = 0.35
local pending_config_save_at = nil

local function schedule_config_save()
	if config_manager_ref then
		config_manager_ref.markDirty("tags")
		return
	end
	pending_config_save_at = os.clock() + CONFIG_SAVE_DEBOUNCE_SEC
end

local function flush_scheduled_config_save(force)
	if config_manager_ref then
		if force then
			config_manager_ref.flush("tags", true)
		end
		return
	end
	if pending_config_save_at and (force or os.clock() >= pending_config_save_at) then
		save_config()
		pending_config_save_at = nil
	end
end

local function load_custom_vars()
	local tbl = funcs.loadJsonTable(CONFIG_PATH_REL)
	if type(tbl) == "table" then
		custom_vars = type(tbl.vars) == "table" and tbl.vars or {}
		settings = type(tbl.settings) == "table" and tbl.settings or settings
	end
	for k, v in pairs(builtin_custom_vars) do
		if custom_vars[k] == nil then
			custom_vars[k] = v
		end
	end
	if settings.show_target_notice == nil then
		settings.show_target_notice = true
	end
	if settings.allow_unsafe == nil then
		settings.allow_unsafe = true
	end
	if settings.show_target_blip == nil then
		settings.show_target_blip = false
	end
	rebuild_cvar_buffers()
end

load_custom_vars()

-- ========== КЭШ ПАРСИНГА ==========
local function begin_parse_scope()
	parse_cache_scope_seq = parse_cache_scope_seq + 1
	parse_cache_scope_key = tostring(parse_cache_scope_seq)
end

local function scoped_cache_key(key)
	return parse_cache_scope_key .. "|" .. tostring(key)
end

local function cache_set(key, val)
	local scoped = scoped_cache_key(key)
	if parse_cache[scoped] ~= nil then
		-- удалить старую позицию
		for i = 1, #parse_cache_order do
			if parse_cache_order[i] == scoped then
				table.remove(parse_cache_order, i)
				break
			end
		end
	end
	parse_cache[scoped] = val
	parse_cache_order[#parse_cache_order + 1] = scoped
	if #parse_cache_order > PARSE_CACHE_MAX then
		local old = table.remove(parse_cache_order, 1)
		parse_cache[old] = nil
	end
end
local function cache_get(key)
	return parse_cache[scoped_cache_key(key)]
end
local function clear_parse_cache()
	for k in pairs(parse_cache) do
		parse_cache[k] = nil
	end
	parse_cache_order = {}
	parse_cache_scope_key = tostring(parse_cache_scope_seq)
end


-- ========== TARGET: ЧТЕНИЕ И СОСТОЯНИЕ ==========
local function read_target_once()
	if not getCharPlayerIsTargeting then
		target.current_ped, target.current_id = nil, nil
		return
	end

	local res, ped = false, nil
	if rawget(_G, "PLAYER_HANDLE") ~= nil then
		local ok, r, p = pcall(getCharPlayerIsTargeting, PLAYER_HANDLE)
		if ok then
			res, ped = r, p
		end
	end
	if not res then
		local ok, r, p = pcall(getCharPlayerIsTargeting, 0)
		if ok then
			res, ped = r, p
		end
	end

	if res and type(ped) == "number" and ped ~= -1 then
		target.current_ped = ped
		if sampGetPlayerIdByCharHandle then
			local ok2, r2, id = pcall(sampGetPlayerIdByCharHandle, ped)
			if ok2 and r2 and type(id) == "number" and id >= 0 then
				target.current_id = id
				if target.last_id ~= id then
					target.last_id = id
					if settings.show_target_notice and target._notice_id ~= id then
						if event_bus_ref then
							event_bus_ref.emit("toast", (L("tags.text.tags_target_id_number")):format(id), "ok", 2.5)
						else
							toasts_module.push((L("tags.text.tags_target_id_number")):format(id), "ok", 2.5)
						end
						target._notice_id = id
					end
				end
				return
			end
		end
		target.current_id = nil
	else
		target.current_ped, target.current_id = nil, nil
	end
end
-- ========== МАРКЕР НАД ПЕДОМ (BLIP) ==========
local target_blip = nil -- хэндл текущего маркера

local function clear_target_blip()
	if target_blip ~= nil then
		if type(removeBlip) == "function" then
			pcall(removeBlip, target_blip)
		end
		target_blip = nil
	end
	target._blip_ped = nil
end

local function set_target_blip(ped)
	clear_target_blip()
	if not ped then return end
	if type(addBlipForChar) ~= "function" then return end
	local ok, blip = pcall(addBlipForChar, ped)
	if ok and blip then
		target_blip = blip
		target._blip_ped = ped
		if type(changeBlipColour) == "function" then
			pcall(changeBlipColour, target_blip, 0x00FF00FF) -- зелёный
		end
	end
end

local function update_target_blip()
	if not settings.show_target_blip then return end
	local new_ped = target.current_ped
	if new_ped ~= target._blip_ped then
		if new_ped then
			set_target_blip(new_ped)
		else
			clear_target_blip()
		end
	end
end

-- получить ник по ID через SAMP-обёртку
local function get_nick_by_id(id)
	if not id then
		return nil
	end
	local samp_module = samp
	if samp_module and samp_module.GetNameID then
		local ok2, name2 = pcall(samp_module.GetNameID, id)
		if ok2 and type(name2) == "string" and name2 ~= "" then
			return name2
		end
	end
	return nil
end

local function format_player_stat(value)
	value = tonumber(value)
	if value == nil or value ~= value or value == math.huge or value == -math.huge then
		return ""
	end
	return tostring(value)
end

local function get_health_and_armour_by_id(id)
	local samp_module = samp
	if not (samp_module and samp_module.GetHealthAndArmour) then
		return nil, nil
	end

	if id == nil then
		if not samp_module.Local_ID then
			return nil, nil
		end
		local ok_local, local_id = pcall(samp_module.Local_ID)
		if not ok_local or local_id == nil then
			return nil, nil
		end
		id = local_id
	else
		id = tonumber((tostring(id or "")):match("^%s*(.-)%s*$"))
		if id == nil then
			return nil, nil
		end
		id = math.floor(id)
	end

	local ok_stats, health, armour = pcall(samp_module.GetHealthAndArmour, id)
	if not ok_stats then
		return nil, nil
	end

	return health, armour
end

local function get_health_by_id(id)
	local health = get_health_and_armour_by_id(id)
	return format_player_stat(health)
end

local function get_armour_by_id(id)
	local _, armour = get_health_and_armour_by_id(id)
	return format_player_stat(armour)
end

-- ========== ЛИСТАБЕЛЬНЫЕ ПАРСЕРЫ ПАРАМЕТРОВ ==========
-- [tag(1 2 3 | ", ")]
local function split_param_list_with_delim(raw)
	local s = tostring(raw or "")
	local in_q = false
	local i_bar = nil
	for i = 1, #s do
		local c = s:sub(i, i)
		if c == '"' then
			in_q = not in_q
		end
		if c == "|" and not in_q then
			i_bar = i
			break
		end
	end
	local items_str, delim_str
	if i_bar then
		items_str = s:sub(1, i_bar - 1)
		delim_str = s:sub(i_bar + 1)
	else
		items_str = s
	end

	-- собрать quoted токены
	local quoted = {}
	for q in items_str:gmatch('"(.-)"') do
		quoted[#quoted + 1] = q
	end
	local s2 = items_str:gsub('"(.-)"', " ")
	local list = {}
	for _, q in ipairs(quoted) do
		if q ~= "" then
			list[#list + 1] = q
		end
	end
	for part in s2:gmatch("[^,%s]+") do
		list[#list + 1] = part
	end

	-- разделитель
	local delim = ", "
	if delim_str and delim_str:match("%S") then
		local dq = delim_str:match('"(.-)"')
		if dq then
			delim = dq
		else
			delim = delim_str:match("^%s*(.-)%s*$")
			if delim == "" then
				delim = ", "
			end
		end
	end
	return list, delim
end


local function make_listable(handler)
	return function(param, thisbind_value)
		local items, delim = split_param_list_with_delim(param)
		if #items <= 1 then
			return handler(param, thisbind_value)
		end
		local results = {}
		for _, it in ipairs(items) do
			local r = handler(it, thisbind_value)
			if r and r ~= "" then
				results[#results + 1] = r
			end
		end
		return table.concat(results, delim or ", ")
	end
end

local function exec_bind_tag_action(action, param, thisbind_value, opts)
	opts = opts or {}
	local binder = resolve_binder()
	if not binder then
		log_chat(L("tags.text.tags_bind_binder"), 0xAA3333)
		return opts.default or ""
	end
	if type(binder.executeBindTagAction) ~= "function" then
		log_chat(L("tags.text.tags_bind_executebindtagaction"), 0xAA3333)
		return opts.default or ""
	end

	local ok_call, ok_action, result, err =
		pcall(binder.executeBindTagAction, action, tostring(param or ""), thisbind_value)
	if not ok_call then
		log_chat(L("tags.text.tags_bind") .. tostring(ok_action), 0xAA3333)
		return opts.default or ""
	end
	if not ok_action then
		if not opts.silent_fail then
			log_chat(("[Tags] [%s(...)]: %s"):format(tostring(action), tostring(err or L("tags.text.text_2"))), 0xAA3333)
		end
		return opts.default or ""
	end
	return result
end

-- ========== ПРОЕКЦИИ НИКОВ / МАППЕРЫ ==========
local function map_nick_raw(id)
	id = tonumber(id)
	if not id then
		return ""
	end
	return get_nick_by_id(id) or ""
end
local function map_nick_ru(id)
	local n = map_nick_raw(id)
	return n and funcs.translite_name(strip_tag(n)) or ""
end
local function map_rpnick(id)
	local n = map_nick_raw(id)
	return n and strip_tag(n):gsub("_", " ") or ""
end
local function map_name(id)
	local n = map_nick_raw(id)
	return n and strip_tag(n):match("([^_]+)") or ""
end
local function map_name_ru(id)
	local nm = map_name(id)
	return nm and funcs.translite_name(nm) or ""
end
local function map_surname(id)
	local n = map_nick_raw(id)
	return n and strip_tag(n):match(".*_(.+)") or ""
end
local function map_surname_ru(id)
	local sn = map_surname(id)
	return sn and funcs.translite_name(sn) or ""
end

local safe_load_expr

local function yield_or_wait(ms)
	ms = tonumber(ms) or 0
	if ms < 0 then
		ms = 0
	end
	ms = math.floor(ms + 0.5)
	local ok_yield = pcall(coroutine.yield, ms)
	if ok_yield then
		return true
	end
	if type(wait) == "function" then
		local ok_wait = pcall(wait, ms)
		if ok_wait then
			return true
		end
	end
	return false
end

local function parse_integer_param(raw)
	local text = tostring(raw or ""):match("^%s*(.-)%s*$") or ""
	if text == "" then
		return nil
	end

	local sign, hex = text:match("^([+-]?)0[xX]([%da-fA-F]+)$")
	if hex then
		local value = tonumber(hex, 16)
		if value and sign == "-" then
			value = -value
		end
		return value
	end

	return tonumber(text)
end

local function split_semicolon_args(raw)
	local left, right = tostring(raw or ""):match("^(.-);(.*)$")
	if left == nil then
		return tostring(raw or ""), nil
	end
	return left, right
end

local function parse_virtual_key_code(raw)
	local key = parse_integer_param(raw)
	if key == nil then
		return nil, "invalid"
	end
	key = math.floor(key)
	if key < 0 or key > 255 then
		return nil, "range"
	end
	return key
end

local function parse_char_key_code(raw)
	local text = tostring(raw or ""):match("^%s*(.-)%s*$") or ""
	if text == "" then
		return nil, "invalid"
	end

	local quoted = text:match('^"(.*)"$') or text:match("^'(.*)'$")
	if quoted ~= nil then
		if #quoted ~= 1 then
			return nil, "char_expected"
		end
		return string.byte(quoted), nil
	end

	if #text == 1 then
		return string.byte(text), nil
	end

	return parse_virtual_key_code(text)
end

local function parse_game_key_state(raw)
	local state = parse_integer_param(raw)
	if state == nil then
		return nil, "invalid"
	end
	state = math.floor(state)
	if state < -32768 or state > 32767 then
		return nil, "range"
	end
	return state
end

local function parse_duration_ms(raw)
	local duration = parse_integer_param(raw)
	if duration == nil then
		return nil, "invalid"
	end
	duration = math.floor(duration)
	if duration < 1 then
		return nil, "range"
	end
	return duration
end

local function get_thisbind_state(thisbind_value)
	local hk = type(thisbind_value) == "table" and thisbind_value or nil
	return hk, hk and hk._co_state or nil
end

local function is_thisbind_stopped(thisbind_value)
	local _, state = get_thisbind_state(thisbind_value)
	return state and state.stopped or false
end

local function stop_thisbind(thisbind_value)
	local hk, state = get_thisbind_state(thisbind_value)
	if not hk then
		return false
	end
	if state then
		state.stopped = true
	end
	local binder = resolve_binder()
	if binder and type(binder.stopHotkey) == "function" then
		pcall(binder.stopHotkey, hk)
	end
	return true
end

local function release_emulated_key_hold(entry)
	if not entry or entry.released then
		return
	end
	entry.released = true

	local setter = entry.kind == "char" and setCharKeyDown or setVirtualKeyDown
	if type(setter) == "function" then
		pcall(setter, entry.code, false)
	end
end

local function begin_emulated_key_hold(kind, code, duration_ms, thisbind_value)
	local setter = kind == "char" and setCharKeyDown or setVirtualKeyDown
	local api_name = kind == "char" and "setCharKeyDown" or "setVirtualKeyDown"
	if type(setter) ~= "function" then
		return nil, "api_missing", api_name
	end

	code = math.floor(tonumber(code) or -1)
	duration_ms = math.max(1, math.floor((tonumber(duration_ms) or 0) + 0.5))

	for i = #active_key_holds, 1, -1 do
		local hold = active_key_holds[i]
		if hold.kind == kind and hold.code == code then
			release_emulated_key_hold(hold)
			table.remove(active_key_holds, i)
		end
	end

	local ok_call, err = pcall(setter, code, true)
	if not ok_call then
		return nil, "press_failed", err
	end

	local entry = {
		kind = kind,
		code = code,
		release_at = os.clock() * 1000 + duration_ms,
		source_hk = type(thisbind_value) == "table" and thisbind_value or nil,
		released = false,
	}
	active_key_holds[#active_key_holds + 1] = entry
	return entry
end

local function process_active_key_holds()
	local now_ms = os.clock() * 1000
	for i = #active_key_holds, 1, -1 do
		local hold = active_key_holds[i]
		local should_release = hold.released or now_ms >= (hold.release_at or 0)
		local hk = hold.source_hk
		if not should_release and hk then
			local state = hk._co_state
			should_release = (state and state.stopped) or (hk.is_running == false and hk._awaiting_input ~= true)
		end
		if should_release then
			release_emulated_key_hold(hold)
			table.remove(active_key_holds, i)
		end
	end
end

local function wait_for_emulated_key_hold(entry, thisbind_value)
	if not entry then
		return false, "invalid_entry"
	end

	while not entry.released do
		process_active_key_holds()
		if entry.released then
			break
		end
		if is_thisbind_stopped(thisbind_value) then
			process_active_key_holds()
			return false, "stopped"
		end

		local now_ms = os.clock() * 1000
		local remaining = math.max(0, (entry.release_at or now_ms) - now_ms)
		if remaining <= 0 then
			process_active_key_holds()
			break
		end

		local sleep_ms = remaining > 50 and 50 or remaining
		if not yield_or_wait(sleep_ms) then
			release_emulated_key_hold(entry)
			process_active_key_holds()
			return false, "no_yield"
		end
	end

	return true
end

local function release_all_key_holds()
	for i = #active_key_holds, 1, -1 do
		release_emulated_key_hold(active_key_holds[i])
		active_key_holds[i] = nil
	end
end

local function wait_for_dialog_open(timeout_ms, thisbind_value)
	if not (samp and samp.isDialogActive) then
		return false, "no_samp"
	end

	timeout_ms = math.max(0, math.floor((tonumber(timeout_ms) or 0) + 0.5))
	local deadline = os.clock() * 1000 + timeout_ms

	while (os.clock() * 1000) < deadline do
		local ok_active, active = pcall(samp.isDialogActive)
		if ok_active and active then
			return true
		end
		if is_thisbind_stopped(thisbind_value) then
			return false, "stopped"
		end

		local remaining = math.max(0, deadline - os.clock() * 1000)
		if remaining <= 0 then
			break
		end
		local sleep_ms = remaining > 50 and 50 or remaining
		if not yield_or_wait(sleep_ms) then
			return false, "no_yield"
		end
	end

	local ok_active, active = pcall(samp.isDialogActive)
	if ok_active and active then
		return true
	end
	return false, "timeout"
end

local function split_ifandor_param(raw)
	local s = tostring(raw or "")
	local qpos, cpos = nil, nil
	local d_round, d_square, d_curly = 0, 0, 0
	local quote, escaped = nil, false

	for i = 1, #s do
		local ch = s:sub(i, i)
		if quote then
			if escaped then
				escaped = false
			elseif ch == "\\" then
				escaped = true
			elseif ch == quote then
				quote = nil
			end
		else
			if ch == '"' or ch == "'" then
				quote = ch
			elseif ch == "(" then
				d_round = d_round + 1
			elseif ch == ")" and d_round > 0 then
				d_round = d_round - 1
			elseif ch == "[" then
				d_square = d_square + 1
			elseif ch == "]" and d_square > 0 then
				d_square = d_square - 1
			elseif ch == "{" then
				d_curly = d_curly + 1
			elseif ch == "}" and d_curly > 0 then
				d_curly = d_curly - 1
			elseif ch == "?" and not qpos and d_round == 0 and d_square == 0 and d_curly == 0 then
				qpos = i
			elseif ch == ":" and qpos and not cpos and d_round == 0 and d_square == 0 and d_curly == 0 then
				cpos = i
				break
			end
		end
	end

	if not (qpos and cpos) then
		return nil, nil, nil
	end
	local cond = s:sub(1, qpos - 1)
	local when_true = s:sub(qpos + 1, cpos - 1)
	local when_false = s:sub(cpos + 1)
	return cond, when_true, when_false
end

local function normalize_dialog_text(raw_text)
	local text = tostring(raw_text or "")
	text = text:gsub("\r\n", "\n"):gsub("\r", "\n")
	text = text:gsub("{......}", "")
	return text
end

local function to_utf8_safe(text)
	local src = tostring(text or "")
	local ok, res = pcall(function()
		return u8(src)
	end)
	if ok and type(res) == "string" then
		return res
	end
	return src
end


local function split_dialog_line_items(line)
	local text = tostring(line or ""):gsub("\t", " ")
	text = text:gsub("([%[%]%(%){}])", " %1 ")
	local out = {}
	for token in text:gmatch("%S+") do
		out[#out + 1] = token
	end
	return out
end

local function collect_dialogtext_items(raw_text)
	local flat, rows = {}, {}
	local idx = 0
	local text = normalize_dialog_text(raw_text)

	for line in text:gmatch("[^\n]+") do
		local row_tokens = split_dialog_line_items(line)
		if #row_tokens > 0 then
			local row = {}
			for _, token in ipairs(row_tokens) do
				local item = { index = idx, text = token }
				flat[#flat + 1] = item
				row[#row + 1] = item
				idx = idx + 1
			end
			rows[#rows + 1] = row
		end
	end

	return flat, rows
end

local function read_active_dialogtext_items(use_utf8)
	if not samp then
		return nil, nil, "no_samp"
	end
	if not (samp.isDialogActive and samp.isDialogActive()) then
		return nil, nil, "no_dialog"
	end
	if not samp.sampGetDialogText then
		return nil, nil, "no_reader"
	end

	local ok_text, raw_text = pcall(samp.sampGetDialogText)
	if not ok_text then
		return nil, nil, "read_fail"
	end

	local source_text = raw_text or ""
	if use_utf8 then
		source_text = to_utf8_safe(source_text)
	end

	local flat, rows = collect_dialogtext_items(source_text)
	return flat, rows, nil
end

local function get_dialogtext_item_by_index(index)
	local flat, _, err = read_active_dialogtext_items(true)
	if not flat then
		return nil, err, 0
	end
	local item = flat[(tonumber(index) or -1) + 1]
	if not item then
		return nil, "out_of_range", #flat
	end
	return item.text or "", nil, #flat
end

local function read_chat_line_by_index(index)
	if type(sampGetChatString) ~= "function" then
		return nil, "no_reader"
	end
	index = tonumber(index) or 99
	index = math.floor(index)

	local ok, text, prefix = pcall(sampGetChatString, index)
	if not ok then
		return nil, "read_fail"
	end

	local txt = to_utf8_safe(text or "")
	local pfx = to_utf8_safe(prefix or "")
	local line = (pfx ~= "" and txt ~= "") and (pfx .. " " .. txt) or (pfx ~= "" and pfx or txt)
	return tostring(line or ""), nil
end

local function normalize_chatwords_line(line)
	local s = tostring(line or "")
	s = s:gsub("\r\n", " "):gsub("\r", " "):gsub("\n", " ")
	s = s:gsub("{......}", "")
	s = s:gsub("%s+", " ")
	s = s:match("^%s*(.-)%s*$") or ""
	return s
end

local function split_chatwords_objects(line)
	local out = {}
	for token in tostring(line or ""):gmatch("%S+") do
		out[#out + 1] = token
	end
	return out
end

local function chatwords_take(objects, selector)
	selector = tostring(selector or ""):match("^%s*(.-)%s*$") or ""
	if selector == "" then
		return table.concat(objects, " ")
	end

	local n_plus = selector:match("^(%d+)%+$")
	if n_plus then
		local n = tonumber(n_plus) or 0
		if n < 1 then
			return ""
		end
		local start_idx = n + 1
		if start_idx > #objects then
			return ""
		end
		return table.concat(objects, " ", start_idx, #objects)
	end

	local n_minus = selector:match("^(%d+)%-$")
	if n_minus then
		local n = tonumber(n_minus) or 0
		if n <= 1 then
			return ""
		end
		local end_idx = n - 1
		if end_idx > #objects then
			end_idx = #objects
		end
		return table.concat(objects, " ", 1, end_idx)
	end

	local a, b = selector:match("^(%d+)%-(%d+)$")
	if a and b then
		local i1 = tonumber(a) or 0
		local i2 = tonumber(b) or 0
		if i1 < 1 or i2 < 1 then
			return ""
		end
		if i1 > i2 then
			i1, i2 = i2, i1
		end
		if i1 > #objects then
			return ""
		end
		if i2 > #objects then
			i2 = #objects
		end
		return table.concat(objects, " ", i1, i2)
	end

	local single = tonumber(selector)
	if single then
		single = math.floor(single)
		if single < 1 or single > #objects then
			return ""
		end
		return tostring(objects[single] or "")
	end

	return ""
end

local function resolve_chatwords(param)
	local raw = tostring(param or ""):match("^%s*(.-)%s*$") or ""
	if raw == "" then
		raw = "1"
	end

	local line_idx = 99
	local selector = raw
	local idx_part, selector_part = raw:match("^(.-);(.-)$")
	if idx_part and selector_part then
		local parsed_idx = tonumber((idx_part or ""):match("^%s*(.-)%s*$"))
		if parsed_idx ~= nil then
			line_idx = math.floor(parsed_idx)
		end
		selector = (selector_part or ""):match("^%s*(.-)%s*$") or ""
	end

	local line, err = read_chat_line_by_index(line_idx)
	if not line then
		if err == "no_reader" then
			log_chat(L("tags.text.tags_chatwords_sampgetchatstring"), 0xAA3333)
		elseif err == "read_fail" then
			log_chat(L("tags.text.tags_chatwords"), 0xAA3333)
		end
		return ""
	end

	line = normalize_chatwords_line(line)
	local objects = split_chatwords_objects(line)
	if #objects == 0 then
		return ""
	end
	return chatwords_take(objects, selector)
end

local function resolve_chatwordsex(param, thisbind_value)
	local hk = type(thisbind_value) == "table" and thisbind_value or nil
	if not hk then
		return ""
	end

	local pattern = tostring(hk._active_chat_trigger_pattern or "")
	local source = tostring(hk._active_chat_trigger_text or "")
	if pattern == "" or source == "" then
		return ""
	end

	local idx = tonumber(tostring(param or ""):match("^%s*(.-)%s*$") or "")
	idx = math.floor(idx or 1)
	if idx < 1 then
		idx = 1
	end

	local ok_match, captures = pcall(function()
		return { string.match(source, pattern) }
	end)
	if not ok_match then
		log_chat(L("tags.text.tags_chatwordsex") .. tostring(captures), 0xAA3333)
		return ""
	end
	if type(captures) ~= "table" or #captures == 0 then
		return ""
	end
	return tostring(captures[idx] or "")
end

local function get_active_command_params(thisbind_value)
	local hk = type(thisbind_value) == "table" and thisbind_value or nil
	if not hk then
		return nil
	end

	local input = tostring(hk._active_command_trigger_text or ""):match("^%s*(.-)%s*$") or ""
	local command = tostring(hk._active_command_trigger_command or hk.command or ""):match("^%s*(.-)%s*$") or ""
	if input == "" or command == "" then
		return nil
	end

	local command_len = #command
	if input:sub(1, command_len) ~= command then
		return nil
	end
	if #input > command_len and input:sub(command_len + 1, command_len + 1) ~= " " then
		return nil
	end

	local args_raw = input:sub(command_len + 1)
	args_raw = args_raw:match("^%s*(.-)%s*$") or ""
	return split_chatwords_objects(args_raw)
end

local function resolve_paramcmd(param, thisbind_value)
	local args = get_active_command_params(thisbind_value)
	if not args then
		return ""
	end

	local selector = tostring(param or ""):match("^%s*(.-)%s*$") or ""
	selector = selector:gsub("%s+", "")
	if selector == "" then
		return ""
	end

	local single = tonumber(selector:match("^(%d+)$"))
	if single then
		if single < 1 then
			return ""
		end
		return tostring(args[single] or "")
	end

	local from = tonumber(selector:match("^(%d+)%+$"))
	if from then
		if from < 1 or from > #args then
			return ""
		end
		return table.concat(args, " ", from, #args)
	end

	local upto = tonumber(selector:match("^(%d+)%-$"))
	if upto then
		if upto < 1 or #args == 0 then
			return ""
		end
		if upto > #args then
			upto = #args
		end
		return table.concat(args, " ", 1, upto)
	end

	local range_from, range_to = selector:match("^(%d+)%-(%d+)$")
	if range_from and range_to then
		range_from = tonumber(range_from)
		range_to = tonumber(range_to)
		if not range_from or not range_to or range_from < 1 or range_to < range_from then
			return ""
		end
		if range_from > #args then
			return ""
		end
		if range_to > #args then
			range_to = #args
		end
		return table.concat(args, " ", range_from, range_to)
	end

	log_chat(L("tags.text.tags_paramcmd_n_n_n_n_m"), 0xAA3333)
	return ""
end

local function normalize_dialogitem_search_text(raw_text)
	local text = tostring(raw_text or "")
	text = text:gsub("{......}", "")
	text = text:gsub("\t", " ")
	text = text:gsub("^%s*%[[^%]]-%]%s*", "")
	text = text:gsub("%s+", " ")
	text = text:match("^%s*(.-)%s*$") or ""
	return text
end

local function build_dialogitem_search_variants(raw_text)
	local out, seen = {}, {}

	local function push(value)
		value = tostring(value or "")
		if value == "" or seen[value] then
			return
		end
		seen[value] = true
		out[#out + 1] = value
	end

	local function push_normalized(value)
		local normalized = normalize_dialogitem_search_text(value)
		push(normalized)
	end

	local raw = tostring(raw_text or "")
	local utf = to_utf8_safe(raw)

	push(raw)
	push(raw:lower())
	push_normalized(raw)
	push_normalized(raw:lower())

	if utf ~= raw then
		push(utf)
		push(utf:lower())
		push_normalized(utf)
		push_normalized(utf:lower())
	end

	return out
end

local DIALOG_STYLE_TABLIST_HEADERS = 5

local function get_dialogitem_header_lines_to_skip()
	if samp and samp.GetCurrentDialogStyle then
		local ok_style, style = pcall(samp.GetCurrentDialogStyle)
		if ok_style and tonumber(style) == DIALOG_STYLE_TABLIST_HEADERS then
			return 1
		end
	end
	return 0
end

local function normalize_dialog_list_item_text(raw_text)
	local text = tostring(raw_text or "")
	text = text:gsub("{......}", "")
	text = text:gsub("\t", " | ")
	text = text:match("^%s*(.-)%s*$") or ""
	return text
end

local function collect_dialog_list_items(raw_text, header_lines_to_skip)
	local items = {}
	local header_lines = {}
	local skip = math.max(0, tonumber(header_lines_to_skip) or 0)
	local raw_line_index = 0
	local text = tostring(raw_text or ""):gsub("\r\n", "\n"):gsub("\r", "\n")

	for line in (text .. "\n"):gmatch("(.-)\n") do
		if raw_line_index < skip then
			local header_text = normalize_dialog_list_item_text(line)
			if header_text ~= "" then
				header_lines[#header_lines + 1] = header_text
			end
		else
			items[#items + 1] = {
				index0 = raw_line_index - skip,
				index1 = raw_line_index - skip + 1,
				text = normalize_dialog_list_item_text(line),
				raw_text = tostring(line or ""),
			}
		end
		raw_line_index = raw_line_index + 1
	end

	return items, table.concat(header_lines, " | ")
end

local function read_active_dialog_list_items(use_utf8)
	if not samp then
		return nil, nil, "no_samp"
	end
	if not (samp.isDialogActive and samp.isDialogActive()) then
		return nil, nil, "no_dialog"
	end

	local dialog_style = samp.GetCurrentDialogStyle and samp.GetCurrentDialogStyle() or nil
	if samp.isDialogListStyle and not samp.isDialogListStyle(dialog_style) then
		return nil, nil, "not_list"
	end
	if not samp.sampGetDialogText then
		return nil, nil, "no_reader"
	end

	local ok_text, raw_text = pcall(samp.sampGetDialogText)
	if not ok_text then
		return nil, nil, "read_fail"
	end

	local source_text = raw_text or ""
	if use_utf8 then
		source_text = to_utf8_safe(source_text)
	end

	local items, header_text = collect_dialog_list_items(source_text, get_dialogitem_header_lines_to_skip())
	if samp.GetCurrentDialogListboxItemsCount then
		local ok_count, count = pcall(samp.GetCurrentDialogListboxItemsCount)
		count = ok_count and tonumber(count) or nil
		if count and count >= 0 and count < #items then
			while #items > count do
				table.remove(items)
			end
		end
	end

	return items, header_text, nil
end

local function find_dialogitem_index_by_text(query, dialog_text, header_lines_to_skip)
	local needles = build_dialogitem_search_variants(query)
	if #needles == 0 then
		return nil
	end

	local skip = tonumber(header_lines_to_skip) or 0
	if skip < 0 then
		skip = 0
	end

	local raw_line_index = 0
	for line in tostring(dialog_text or ""):gmatch("[^\n]+") do
		local selectable_index = raw_line_index - skip
		if selectable_index >= 0 then
			local haystacks = build_dialogitem_search_variants(line)
			local matched = false

			for _, hay in ipairs(haystacks) do
				for _, needle in ipairs(needles) do
					if hay:find(needle, 1, true) then
						matched = true
						break
					end
				end
				if matched then
					break
				end
			end

			if matched then
				return selectable_index
			end
		end
		raw_line_index = raw_line_index + 1
	end

	return nil
end


local non_cache_multi_tags = {
	call = true,
	wait = true,
	waitif = true,
	math = true,
	SetPageSize = true,
	keyemulate = true,
	charkeyemulate = true,
	gamekeyemulate = true,
	keydown = true,
	dialogitem = true,
	dialogtext = true,
	dialogclose = true,
	dialogsettext = true,
	chatwords = true,
	chatwordsex = true,
	binddisable = true,
	bindenable = true,
	bindstart = true,
	bindstop = true,
	bindpause = true,
	bindunpause = true,
	bindfastmenu = true,
	bindunfastmenu = true,
	bindrandom = true,
	bindended = true,
	bindstopall = true,
	bindpopup = true,
	ifandor = true,
}

-- ========== MULTI-TAG HANDLERS ==========
local multi_tag_handlers = {
	-- строка в нижний регистр
	strlow = function(str)
		return funcs.string_lower(str)
	end,

	-- вычислить выражение
	math = function(param)
		local expr = tostring(param or ""):match("^%s*(.-)%s*$")
		if expr == "" then
			log_chat(L("tags.text.tags_math"), 0xAA3333)
			return ""
		end

		local chunk, err = safe_load_expr(expr, { chunk_name = "=tags.math" })
		if not chunk then
			log_chat(L("tags.text.tags_math_3") .. tostring(err), 0xAA3333)
			return ""
		end

		local ok, res = pcall(chunk)
		if not ok then
			log_chat(L("tags.text.tags_math_3") .. tostring(res), 0xAA3333)
			return ""
		end
		if res == nil then
			return ""
		end
		return tostring(res)
	end,

	-- ifandor(condition ? when_true : when_false)
	ifandor = function(param, thisbind_value, depth)
		local cond_raw, true_raw, false_raw = split_ifandor_param(param)
		if not cond_raw then
			log_chat(L("tags.text.tags_ifandor_conditionyatrue_false"), 0xAA3333)
			return ""
		end

		local cond_expr = module.change_tags(cond_raw, thisbind_value, depth)
		local chunk, err = safe_load_expr(cond_expr, { chunk_name = "=tags.ifandor" })
		local cond_ok = false
		if not chunk then
			log_chat(L("tags.text.tags_ifandor") .. tostring(err), 0xAA3333)
		else
			local ok, res = pcall(chunk)
			if not ok then
				log_chat(L("tags.text.tags_ifandor") .. tostring(res), 0xAA3333)
			else
				cond_ok = not not res
			end
		end

		local branch = cond_ok and true_raw or false_raw
		return module.change_tags(branch or "", thisbind_value, depth)
	end,

	-- выполнить Lua-код без вставки текста
	call = function(param)
		if not settings.allow_unsafe then
			log_chat(L("tags.text.tags_call"), 0xAA3333)
			return ""
		end
		local expr = tostring(param or "")
		local chunk, err = safe_load_expr(expr, {
			allow_call_api = true,
			allow_statements = true,
			chunk_name = "=tags.call",
		})
		if not chunk then
			log_chat(L("tags.text.tags_call_4") .. tostring(err), 0xAA3333)
			return ""
		end
		if lua_thread and lua_thread.create then
			lua_thread.create(function()
				local ok_exec, exec_err = pcall(chunk)
				if not ok_exec then
					log_chat(L("tags.text.tags_call_4") .. tostring(exec_err), 0xAA3333)
				end
			end)
		else
			local ok_exec, exec_err = pcall(chunk)
			if not ok_exec then
				log_chat(L("tags.text.tags_call_4") .. tostring(exec_err), 0xAA3333)
			end
		end
		return ""
	end,

	-- задержка в миллисекундах без вставки текста
	wait = function(param)
		local expr = tostring(param or ""):match("^%s*(.-)%s*$")
		if expr == "" then
			return ""
		end
		local ms = tonumber(expr)
		if not ms then
			local chunk, err = safe_load_expr(expr, { chunk_name = "=tags.wait" })
			if not chunk then
				log_chat(L("tags.text.tags_wait") .. tostring(err), 0xAA3333)
				return ""
			end
			local ok, res = pcall(chunk)
			if not ok then
				log_chat(L("tags.text.tags_wait") .. tostring(res), 0xAA3333)
				return ""
			end
			ms = tonumber(res)
		end
		if not ms then
			log_chat(L("tags.text.tags_wait_5"), 0xAA3333)
			return ""
		end
		if not yield_or_wait(ms) then
			log_chat(L("tags.text.tags_wait_6"), 0xAA3333)
		end
		return ""
	end,

	-- ждать, пока условие не станет истинным (таймаут 10 сек)
	waitif = function(param, thisbind_value, depth)
		local raw_expr = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw_expr == "" then
			return ""
		end
		local had_compile_error = false
		local deadline = os.clock() + 10.0
		while os.clock() < deadline do
			local expr = module.change_tags(raw_expr, thisbind_value, depth)
			local chunk, err = safe_load_expr(expr, { chunk_name = "=tags.waitif" })
			if not chunk then
				if not had_compile_error then
					log_chat(L("tags.text.tags_waitif") .. tostring(err), 0xAA3333)
					had_compile_error = true
				end
				if not yield_or_wait(50) then
					log_chat(L("tags.text.tags_waitif_7"), 0xAA3333)
					break
				end
			else
				had_compile_error = false
				local ok, res = pcall(chunk)
				if ok and res then
					break
				end
				if not ok then
					log_chat(L("tags.text.tags_waitif") .. tostring(res), 0xAA3333)
					break
				end
				if not yield_or_wait(50) then
					log_chat(L("tags.text.tags_waitif_7"), 0xAA3333)
					break
				end
			end
		end
		if os.clock() >= deadline then
			log_chat(L("tags.text.tags_waitif_10"), 0xAA3333)
		end
		return ""
	end,

	keyemulate = function(param, thisbind_value)
		local key, reason = parse_virtual_key_code(param)
		if not key then
			if reason == "range" then
				log_chat(L("tags.text.tags_keyemulate_0_255"), 0xAA3333)
			else
				log_chat(L("tags.text.tags_keyemulate"), 0xAA3333)
			end
			return ""
		end

		local hold, err_kind, err = begin_emulated_key_hold("virtual", key, KEY_EMULATE_TAP_MS, thisbind_value)
		if not hold then
			if err_kind == "api_missing" then
				log_chat(L("tags.text.tags_keyemulate_8") .. tostring(err), 0xAA3333)
			else
				log_chat(L("tags.text.tags_keyemulate_9") .. tostring(err), 0xAA3333)
			end
			return ""
		end

		local ok_wait, wait_err = wait_for_emulated_key_hold(hold, thisbind_value)
		if not ok_wait and wait_err == "no_yield" then
			log_chat(L("tags.text.tags_keyemulate_10"), 0xAA3333)
		end
		return ""
	end,

	charkeyemulate = function(param, thisbind_value)
		local key, reason = parse_char_key_code(param)
		if not key then
			if reason == "char_expected" then
				log_chat(L("tags.text.tags_charkeyemulate"), 0xAA3333)
			elseif reason == "range" then
				log_chat(L("tags.text.tags_charkeyemulate_0_255"), 0xAA3333)
			else
				log_chat(L("tags.text.tags_charkeyemulate_11"), 0xAA3333)
			end
			return ""
		end

		local hold, err_kind, err = begin_emulated_key_hold("char", key, KEY_EMULATE_TAP_MS, thisbind_value)
		if not hold then
			if err_kind == "api_missing" then
				log_chat(L("tags.text.tags_charkeyemulate_12") .. tostring(err), 0xAA3333)
			else
				log_chat(L("tags.text.tags_charkeyemulate_13") .. tostring(err), 0xAA3333)
			end
			return ""
		end

		local ok_wait, wait_err = wait_for_emulated_key_hold(hold, thisbind_value)
		if not ok_wait and wait_err == "no_yield" then
			log_chat(L("tags.text.tags_charkeyemulate_14"), 0xAA3333)
		end
		return ""
	end,

	gamekeyemulate = function(param)
		if type(setGameKeyState) ~= "function" then
			log_chat(L("tags.text.tags_gamekeyemulate_setgamekeystate"), 0xAA3333)
			return ""
		end

		local key_raw, state_raw = split_semicolon_args(param)
		local key = parse_integer_param(key_raw)
		if key == nil then
			log_chat(L("tags.text.tags_gamekeyemulate"), 0xAA3333)
			return ""
		end
		key = math.floor(key)

		local state = 32767
		if state_raw ~= nil and state_raw:match("%S") then
			local parsed_state, reason = parse_game_key_state(state_raw)
			if parsed_state == nil then
				if reason == "range" then
					log_chat(L("tags.text.tags_gamekeyemulate_32768_32767"), 0xAA3333)
				else
					log_chat(L("tags.text.tags_gamekeyemulate_15"), 0xAA3333)
				end
				return ""
			end
			state = parsed_state
		end

		local ok_call, err = pcall(setGameKeyState, key, state)
		if not ok_call then
			log_chat(L("tags.text.tags_gamekeyemulate_16") .. tostring(err), 0xAA3333)
		end
		return ""
	end,

	keydown = function(param, thisbind_value)
		local key_raw, duration_raw = split_semicolon_args(param)
		if duration_raw == nil or not duration_raw:match("%S") then
			log_chat(L("tags.text.tags_keydown_key_milliseconds"), 0xAA3333)
			return ""
		end

		local key, key_reason = parse_virtual_key_code(key_raw)
		if not key then
			if key_reason == "range" then
				log_chat(L("tags.text.tags_keydown_0_255"), 0xAA3333)
			else
				log_chat(L("tags.text.tags_keydown"), 0xAA3333)
			end
			return ""
		end

		local duration, duration_reason = parse_duration_ms(duration_raw)
		if not duration then
			if duration_reason == "range" then
				log_chat(L("tags.text.tags_keydown_0"), 0xAA3333)
			else
				log_chat(L("tags.text.tags_keydown_17"), 0xAA3333)
			end
			return ""
		end

		local hold, err_kind, err = begin_emulated_key_hold("virtual", key, duration, thisbind_value)
		if not hold then
			if err_kind == "api_missing" then
				log_chat(L("tags.text.tags_keydown_18") .. tostring(err), 0xAA3333)
			else
				log_chat(L("tags.text.tags_keydown_19") .. tostring(err), 0xAA3333)
			end
			return ""
		end

		local ok_wait, wait_err = wait_for_emulated_key_hold(hold, thisbind_value)
		if not ok_wait and wait_err == "no_yield" then
			log_chat(L("tags.text.tags_keydown_20"), 0xAA3333)
		end
		return ""
	end,

	-- получить элемент текста открытого диалога по индексу (0-based)
	dialogtext = function(param)
		if not samp then
			log_chat(L("tags.text.tags_dialogtext_samp"), 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat(L("tags.text.tags_dialogtext"), 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local idx = tonumber(raw)
		if idx == nil then
			log_chat(L("tags.text.tags_dialogtext_21"), 0xAA3333)
			return ""
		end
		idx = math.floor(idx)
		if idx < 0 then
			log_chat(L("tags.text.tags_dialogtext_0"), 0xAA3333)
			return ""
		end

		local text, err, total = get_dialogtext_item_by_index(idx)
		if text ~= nil then
			return text
		end

		if err == "no_dialog" then
			return ""
		elseif err == "no_reader" then
			log_chat(L("tags.text.tags_dialogtext_sampgetdialogtext"), 0xAA3333)
		elseif err == "read_fail" then
			log_chat(L("tags.text.tags_dialogtext_22"), 0xAA3333)
		elseif err == "out_of_range" then
			log_chat(
				(L("tags.text.tags_dialogtext_number_number")):format(
					idx,
					tonumber(total) or 0
				),
				0xAA3333
			)
		end
		return ""
	end,

	-- parse words from chat lines (current or by index)
	chatwords = function(param)
		return resolve_chatwords(param)
	end,

	-- parse captures from chat-trigger text using Lua pattern
	chatwordsex = function(param, thisbind_value)
		return resolve_chatwordsex(param, thisbind_value)
	end,

	-- parse arguments from the command that launched current bind
	paramcmd = function(param, thisbind_value)
		return resolve_paramcmd(param, thisbind_value)
	end,

	SetPageSize = function(param)
		if not samp then
			log_chat(L("tags.text.tags_setpagesize_samp"), 0xAA3333)
			return ""
		end
		if not samp.SetPageSize then
			log_chat(L("tags.text.tags_setpagesize_setpagesize"), 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat(L("tags.text.tags_setpagesize"), 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local page_size = tonumber(raw)
		if page_size == nil then
			log_chat(L("tags.text.tags_setpagesize_23"), 0xAA3333)
			return ""
		end

		local ok_call, ok_result, reason, min_size, max_size = pcall(samp.SetPageSize, page_size)
		if not ok_call then
			log_chat(L("tags.text.tags_setpagesize_24") .. tostring(ok_result), 0xAA3333)
			return ""
		end
		if not ok_result then
			if reason == "invalid_value" then
				log_chat(L("tags.text.tags_setpagesize_23"), 0xAA3333)
			elseif reason == "range" then
				log_chat(
					(L("tags.text.tags_setpagesize_format_format")):format(
						tostring(min_size or 10),
						tostring(max_size or 20)
					),
					0xAA3333
				)
			elseif reason == "chat_unavailable" then
				log_chat(L("tags.text.tags_setpagesize_25"), 0xAA3333)
			end
			return ""
		end
		return ""
	end,

	GetIDByName = function(param)
		if not samp then
			log_chat(L("tags.text.tags_getidbyname_samp"), 0xAA3333)
			return ""
		end
		if not samp.GetIDByName then
			log_chat(L("tags.text.tags_getidbyname_getidbyname"), 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat(L("tags.text.tags_getidbyname"), 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local ok_call, id = pcall(samp.GetIDByName, raw)
		if not ok_call then
			log_chat(L("tags.text.tags_getidbyname_26") .. tostring(id), 0xAA3333)
			return ""
		end
		if id == nil then
			return ""
		end
		return tostring(id)
	end,

	-- открыть пункт активного диалога по имени/номеру
	dialogitem = function(param)
		if not samp then
			log_chat(L("tags.text.tags_dialogitem_samp"), 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat(L("tags.text.tags_dialogitem"), 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat(L("tags.text.tags_dialogitem_27"), 0xAA3333)
			return ""
		end
		local arg = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local idx = nil
		local as_num = tonumber(arg)
		if as_num then
			as_num = math.floor(as_num)
			idx = (as_num >= 1) and (as_num - 1) or as_num
		else
			local header_lines_to_skip = get_dialogitem_header_lines_to_skip()
			if samp.sampGetDialogText then
				local ok_text, dtext = pcall(samp.sampGetDialogText)
				if ok_text and type(dtext) == "string" and dtext ~= "" then
					idx = find_dialogitem_index_by_text(arg, dtext, header_lines_to_skip)
				end
			end
			if idx == nil and samp.getListItemNumberByText then
				local ok_find, found = pcall(samp.getListItemNumberByText, arg)
				if ok_find and found ~= false and found ~= nil then
					local found_idx = tonumber(found)
					if found_idx ~= nil then
						found_idx = found_idx - header_lines_to_skip
						if found_idx >= 0 then
							idx = found_idx
						end
					end
				end
			end
		end

		if idx == nil then
			log_chat(L("tags.text.tags_dialogitem_28") .. tostring(arg), 0xAA3333)
			return ""
		end

		local count = nil
		if samp.GetCurrentDialogListboxItemsCount then
			local ok_cnt, cnt = pcall(samp.GetCurrentDialogListboxItemsCount)
			if ok_cnt then
				count = tonumber(cnt)
			end
		end
		if idx < 0 or (count and idx >= count) then
			log_chat(
				(L("tags.text.tags_dialogitem_format")):format(tostring(idx + 1)),
				0xAA3333
			)
			return ""
		end

		local selected = false
		local dialog_style = samp.GetCurrentDialogStyle and samp.GetCurrentDialogStyle() or nil
		if samp.isDialogListStyle and not samp.isDialogListStyle(dialog_style) then
			log_chat(L("tags.text.tags_dialogitem_29"), 0xAA3333)
			return ""
		end
		if samp.SetCurrentDialogListItem then
			local ok_set = pcall(samp.SetCurrentDialogListItem, idx)
			selected = ok_set
		end
		if selected and samp.GetCurrentDialogListItem then
			local ok_cur, cur = pcall(samp.GetCurrentDialogListItem)
			if ok_cur and tonumber(cur) ~= nil then
				selected = tonumber(cur) == idx
			end
		end
		if not selected then
			log_chat(L("tags.text.tags_dialogitem_30"), 0xAA3333)
			return ""
		end

		local opened = false
		if samp.CDialog_Close_func then
			local ok_close = pcall(samp.CDialog_Close_func, 1)
			opened = ok_close
		end
		if not opened then
			log_chat(L("tags.text.tags_dialogitem_31"), 0xAA3333)
		end
		return ""
	end,

	-- закрыть активный диалог (1 = Enter/ОК, 0 = Esc/Cancel)
	dialogclose = function(param)
		if not samp then
			log_chat(L("tags.text.tags_dialogclose_samp"), 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat(L("tags.text.tags_dialogclose"), 0xAA3333)
			return ""
		end
		if not samp.CDialog_Close_func then
			log_chat(L("tags.text.tags_dialogclose_cdialog_close_func"), 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat(L("tags.text.tags_dialogclose_0_1"), 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local button = tonumber(raw)
		if button == nil then
			log_chat(L("tags.text.tags_dialogclose_0_1"), 0xAA3333)
			return ""
		end
		button = math.floor(button)
		if button ~= 0 and button ~= 1 then
			log_chat(L("tags.text.tags_dialogclose_0_1_32"), 0xAA3333)
			return ""
		end

		local ok_close, err = pcall(samp.CDialog_Close_func, button)
		if not ok_close then
			log_chat(L("tags.text.tags_dialogclose_33") .. tostring(err), 0xAA3333)
		end
		return ""
	end,

	-- установить текст в editbox активного диалога
	dialogsettext = function(param)
		if not samp then
			log_chat(L("tags.text.tags_dialogsettext_samp"), 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat(L("tags.text.tags_dialogsettext"), 0xAA3333)
			return ""
		end
		if not samp.sampSetDialogEditboxText then
			log_chat(L("tags.text.tags_dialogsettext_sampsetdialogeditboxtext"), 0xAA3333)
			return ""
		end

		local text = tostring(param or "")
		text = text:match('^"(.*)"$') or text:match("^'(.*)'$") or text

		local text_cp1251 = text
		if type(u8) == "table" and u8.decode then
			local ok_decode, decoded = pcall(u8.decode, u8, text)
			if ok_decode and type(decoded) == "string" then
				text_cp1251 = decoded
			end
		end

		local ok_set, err = pcall(samp.sampSetDialogEditboxText, text_cp1251)
		if not ok_set then
			log_chat(L("tags.text.tags_dialogsettext_34") .. tostring(err), 0xAA3333)
		end
		return ""
	end,

	-- bind API
	binddisable = function(param, thisbind_value)
		exec_bind_tag_action("disable", param, thisbind_value)
		return ""
	end,
	bindenable = function(param, thisbind_value)
		exec_bind_tag_action("enable", param, thisbind_value)
		return ""
	end,
	bindstart = function(param, thisbind_value)
		exec_bind_tag_action("start", param, thisbind_value)
		return ""
	end,
	bindstop = function(param, thisbind_value)
		exec_bind_tag_action("stop", param, thisbind_value)
		return ""
	end,
	bindpause = function(param, thisbind_value)
		exec_bind_tag_action("pause", param, thisbind_value)
		return ""
	end,
	bindunpause = function(param, thisbind_value)
		exec_bind_tag_action("unpause", param, thisbind_value)
		return ""
	end,
	bindfastmenu = function(param, thisbind_value)
		exec_bind_tag_action("fastmenu", param, thisbind_value)
		return ""
	end,
	bindunfastmenu = function(param, thisbind_value)
		exec_bind_tag_action("unfastmenu", param, thisbind_value)
		return ""
	end,
	bindrandom = function(param, thisbind_value)
		exec_bind_tag_action("random", param, thisbind_value)
		return ""
	end,
	bindended = function(param, thisbind_value)
		local res = exec_bind_tag_action("ended", param, thisbind_value, { default = "0", silent_fail = true })
		if res == "1" then
			return "1"
		end
		return "0"
	end,
	bindstopall = function(param, thisbind_value)
		exec_bind_tag_action("stopall", param, thisbind_value)
		return ""
	end,
	bindpopup = function(param, thisbind_value)
		exec_bind_tag_action("popup", param, thisbind_value)
		return ""
	end,
	-- листабельные проекции
	nickid = make_listable(function(id)
		return map_nick_raw(id)
	end),
	nickru = make_listable(function(id)
		return map_nick_ru(id)
	end),
	rpnick = make_listable(function(id)
		return map_rpnick(id)
	end),
	name = make_listable(function(id)
		return map_name(id)
	end),
	nameru = make_listable(function(id)
		return map_name_ru(id)
	end),
	surname = make_listable(function(id)
		return map_surname(id)
	end),
	surnameru = make_listable(function(id)
		return map_surname_ru(id)
	end),
	health = make_listable(function(id)
		return get_health_by_id(id)
	end),
	armour = make_listable(function(id)
		return get_armour_by_id(id)
	end),
	-- тип транспорта игрока по ID (листабельно)
	getvehtype = make_listable(function(param)
		local id = tonumber((tostring(param or "")):match("^%s*(.-)%s*$"))
		if not id then return "" end
		if type(sampGetCharHandleByPlayerId) ~= "function" then return "" end
		local ok_ped, is_valid, ped = pcall(sampGetCharHandleByPlayerId, id)
		if not ok_ped or not is_valid or not ped then return "" end
		if type(isCharInAnyCar) ~= "function" then return "" end
		local ok_in, in_car = pcall(isCharInAnyCar, ped)
		if not ok_in or not in_car then return "" end
		if type(storeCarCharIsInNoSave) ~= "function" or type(getCarModel) ~= "function" then return "" end
		local ok_veh, veh = pcall(storeCarCharIsInNoSave, ped)
		if not ok_veh or not veh then return "" end
		local ok_model, model = pcall(getCarModel, veh)
		if not ok_model or not model then return "" end
		return funcs.getVehicleType(model)
	end),

	-- текущее время + смещение мин:сек
	addtime = function(param)
		local min, sec = param:match("(%d+):(%d+)")
		min, sec = tonumber(min), tonumber(sec)
		if min and sec then
			return os.date("%H:%M:%S", os.time() + (min * 60) + sec)
		else
			return ""
		end
	end,

	-- скриншот
	screen = function(param)
		local args = {}
		for arg in tostring(param):gmatch('"(.-)"') do
			table.insert(args, arg)
		end
		if #args == 0 then
			for word in tostring(param):gmatch("([^,]+)") do
				table.insert(args, word:match("^%s*(.-)%s*$"))
			end
		end
		local name = args[1] and args[1] ~= "" and args[1] or nil
		local path = args[2] and args[2] ~= "" and args[2] or nil
		funcs.Take_Screenshot(path, name)
		return string.format(L("tags.text.format"), name or os.date("%d.%m.%Y %H.%M.%S"))
	end,
}


-- описания мульти-тегов (для справки)
local multi_tags_descriptions = {
	nickid = { desc = L("tags.text.id"), example = L("tags.example.nickid") },
	nickru = { desc = L("tags.text.id_35"), example = L("tags.example.nickru") },
	rpnick = { desc = L("tags.text.id_36"), example = L("tags.example.rpnick") },
	name = { desc = L("tags.text.id_37"), example = L("tags.example.name") },
	nameru = { desc = L("tags.text.id_38"), example = L("tags.example.nameru") },
	surname = { desc = L("tags.text.id_39"), example = L("tags.example.surname") },
	surnameru = { desc = L("tags.text.id_40"), example = L("tags.example.surnameru") },
	health = { desc = L("tags.text.id_41"), example = L("tags.example.health") },
	armour = { desc = L("tags.text.id_42"), example = L("tags.example.armour") },
	SetPageSize = { desc = L("tags.text.pagesize"), example = L("tags.example.setpagesize") },
	GetIDByName = { desc = L("tags.text.id_43"), example = L("tags.example.getidbyname") },
	getvehtype = { desc = L("tags.text.id_44"), example = L("tags.example.getvehtype") },
	strlow = { desc = L("tags.text.text_45"), example = L("tags.text.strlow") },
	math = { desc = L("tags.text.text_46"), example = L("tags.example.math") },
	ifandor = {
		desc = L("tags.text.condition_ya_true_value_false_value"),
		example = L("tags.text.ifandor_id_148ya_148_148"),
	},
	call = {
		desc = L("tags.text.lua"),
		example = L("tags.example.call"),
	},
	wait = {
		desc = L("tags.text.text_47"),
		example = L("tags.example.wait"),
	},
	waitif = {
		desc = L("tags.text.text_48"),
		example = L("tags.example.waitif"),
	},
	keyemulate = {
		desc = L("tags.text.text_49"),
		example = L("tags.example.keyemulate"),
	},
	charkeyemulate = {
		desc = L("tags.text.text_50"),
		example = L("tags.example.charkeyemulate_char"),
	},
	gamekeyemulate = {
		desc = L("tags.text.gamekeyemulate_32767"),
		example = L("tags.example.gamekeyemulate"),
	},
	keydown = {
		desc = L("tags.text.text_51"),
		example = L("tags.example.keydown"),
	},
	dialogitem = {
		desc = L("tags.text.text_52"),
		example = L("tags.text.dialogitem"),
	},
	dialogclose = {
		desc = L("tags.text.text_1_enter_0_esc"),
		example = L("tags.example.dialogclose"),
	},
	dialogtext = {
		desc = L("tags.text.text_0_based"),
		example = L("tags.example.dialogtext"),
	},
	dialogsettext = {
		desc = L("tags.text.editbox"),
		example = L("tags.text.dialogsettext"),
	},
	chatwords = {
		desc = L("tags.text.chatwords_desc"),
		example = L("tags.example.chatwords"),
	},
	chatwordsex = {
		desc = L("tags.text.chatwordsex_desc"),
		example = L("tags.example.chatwordsex"),
	},
	paramcmd = {
		desc = L("tags.text.n_n_n_n_m"),
		example = L("tags.example.paramcmd"),
	},
	binddisable = {
		desc = L("tags.text.text_53"),
		example = L("tags.text.binddisable"),
	},
	bindenable = {
		desc = L("tags.text.text_54"),
		example = L("tags.text.bindenable"),
	},
	bindstart = {
		desc = L("tags.text.text_55"),
		example = L("tags.example.bindstart"),
	},
	bindstop = {
		desc = L("tags.text.text_56"),
		example = L("tags.example.bindstop"),
	},
	bindpause = {
		desc = L("tags.text.text_57"),
		example = L("tags.text.bindpause"),
	},
	bindunpause = {
		desc = L("tags.text.text_58"),
		example = L("tags.text.bindunpause"),
	},
	bindfastmenu = {
		desc = L("tags.text.text_59"),
		example = L("tags.text.bindfastmenu"),
	},
	bindunfastmenu = {
		desc = L("tags.text.text_60"),
		example = L("tags.text.bindunfastmenu"),
	},
	bindrandom = {
		desc = L("tags.text.text_61"),
		example = L("tags.text.bindrandom"),
	},
	bindended = {
		desc = L("tags.text.text_1_0"),
		example = L("tags.example.bindended"),
	},
	bindstopall = {
		desc = L("tags.text.text_62"),
		example = L("tags.example.bindstopall"),
	},
	bindpopup = {
		desc = L("tags.text.text_63"),
		example = L("tags.example.bindpopup"),
	},
	addtime = { desc = L("tags.text.text_64"), example = L("tags.example.addtime") },
	screen = {
		desc = L("tags.text.text_65"),
		example = L("tags.text.screen"),
	},
}

-- ========== ВНЕШНИЕ ПЕРЕМЕННЫЕ И РЕГИСТРАТОРЫ ==========
_G.registerVariable = function(name, desc, fn)
	if type(name) ~= "string" or type(fn) ~= "function" then
		return
	end
	module.registerVariable(name, desc, fn)
end

_G.registerFunctionalVariable = function(name, desc, fn, opts)
	if type(name) ~= "string" or type(fn) ~= "function" then
		return
	end
	if opts and opts.no_cache ~= nil then
		if opts.no_cache then
			non_cache_multi_tags[name] = true
		else
			non_cache_multi_tags[name] = nil
		end
	end
	multi_tag_handlers[name] = function(param, thisbind_value)
		local ok, res = pcall(fn, tostring(param or ""), thisbind_value)
		if not ok then
			log_chat((L("tags.text.tags_format_format")):format(name, tostring(res)), 0xAA3333)
			return L("tags.text.text_66") .. name .. "]"
		end
		return res
	end
	multi_tags_descriptions[name] = {
		desc = desc or (L("tags.text.text_67") .. name .. "'"),
		example = (opts and opts.example) or ("[" .. name .. "(...)]"),
	}
end

local VARS_DIR = paths.dataPath("vars")

local function list_lua_files(dir)
	local out = {}
	dir = tostring(dir or ""):gsub("/", "\\"):gsub("\\+$", "")
	if dir == "" then
		return out
	end
	if type(doesDirectoryExist) == "function" and not doesDirectoryExist(dir) then
		return out
	end
	local h, f = findFirstFile(dir .. "\\*.lua")
	if h then
		while f do
			if f ~= "." and f ~= ".." and f:lower():match("%.lua$") then
				out[#out + 1] = (dir .. "\\" .. f):gsub("\\", "/")
			end
			f = findNextFile(h)
		end
		findClose(h)
	end
	table.sort(out, function(a, b)
		return a:lower() < b:lower()
	end)
	return out
end

local function load_external_vars()
	local files = list_lua_files(VARS_DIR)

	for _, path in ipairs(files) do
		local chunk, err = loadfile(path)
		if not chunk then
			log_chat((L("tags.text.tags_format_format_68")):format(path, tostring(err)), 0xAA3333)
		else
			local env = setmetatable({
				registerVariable = _G.registerVariable,
				registerFunctionalVariable = _G.registerFunctionalVariable,
				module = module,
				funcs = funcs,
				imgui = imgui,
				ffi = ffi,
			}, { __index = _G })
			setfenv(chunk, env)
			local ok, perr = pcall(chunk)
			if not ok then
				log_chat(
					(L("tags.text.tags_format_format_69")):format(path, tostring(perr)),
					0xAA3333
				)
			end
		end
	end

	clear_parse_cache()
end

-- внешние переменные (API)
local external_variables = {}
function module.registerVariable(name, desc, fn)
	external_variables[name] = { desc = desc, fn = fn }
end

-- список простых тегов (для справки)
local simple_tags = {
	{ name = L("tags.simple.id"), desc = L("tags.text.id_70") },
	{ name = L("tags.simple.nick"), desc = L("tags.text.text_71") },
	{ name = L("tags.simple.nickru"), desc = L("tags.text.text_72") },
	{ name = L("tags.simple.rpnick"), desc = L("tags.text.text_73") },
	{ name = L("tags.simple.name"), desc = L("tags.text.text_74") },
	{ name = L("tags.simple.nameru"), desc = L("tags.text.text_75") },
	{ name = L("tags.simple.surname"), desc = L("tags.text.text_76") },
	{ name = L("tags.simple.surnameru"), desc = L("tags.text.text_77") },
	{ name = L("tags.simple.myskin"), desc = L("tags.text.id_78") },
	{ name = L("tags.simple.city"), desc = L("tags.text.gta") },
	{ name = L("tags.simple.date"), desc = L("tags.text.text_79") },
	{ name = L("tags.simple.time"), desc = L("tags.text.text_80") },
	{ name = L("tags.simple.timenosec"), desc = L("tags.text.text_81") },
	{ name = L("tags.simple.myorg"), desc = L("tags.text.text_82") },
	{ name = L("tags.simple.myorgrang"), desc = L("tags.text.text_83") },
	{
		name = L("tags.simple.screen"),
		desc = L("tags.text.text_84"),
	},

	-- переменные по таргету (последний валидный ID)
	{ name = L("tags.simple.targetid"), desc = L("tags.text.id_85") },
	{ name = L("tags.simple.targetnick"), desc = L("tags.text.samp") },
	{ name = L("tags.simple.targetrpnick"), desc = L("tags.text.rp") },
	{ name = L("tags.simple.targetname"), desc = L("tags.text.text_86") },
	{ name = L("tags.simple.targetsurname"), desc = L("tags.text.text_87") },
	{ name = L("tags.simple.thisbind"), desc = L("tags.text.text_88") },
	{ name = L("tags.simple.dialogactive"), desc = L("tags.text.true_false") },
	{
		name = L("tags.simple.dialogwaitopen"),
		desc = L("tags.text.text_2_89"),
	},
	{ name = L("tags.simple.dialogcaption"), desc = L("tags.text.text_90") },
	{ name = L("tags.simple.getdialogid"), desc = L("tags.text.id_91") },
	{ name = L("tags.simple.dialoggetselecteditem"), desc = L("tags.text.listbox") },
	{ name = L("tags.simple.clipboard"), desc = L("tags.text.text_92") },
	{ name = L("tags.simple.mymoney"), desc = L("tags.text.text_93") },
	{ name = L("tags.simple.health"), desc = L("tags.text.text_94") },
	{ name = L("tags.simple.armour"), desc = L("tags.text.text_95") },
	{ name = L("tags.simple.samp_get_dialog_editbox_text"), desc = L("tags.text.editbox_96") },
	{ name = L("tags.simple.getvehtype"), desc = L("tags.text.text_97") },
}

-- таблица тегов {var}
local tags = setmetatable({}, {
	__index = function(_, key)
		if key == "{id}" then
			return function()
				local samp_module = samp
				return samp_module and samp_module.Local_ID and samp_module.Local_ID() or ""
			end
		elseif key == "{nick}" then
			return function()
				local samp_module = samp
				return (samp_module and samp_module.GetNameID and samp_module.Local_ID and samp_module.GetNameID(samp_module.Local_ID()))
					or ""
			end
		elseif key == "{nickru}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					return n and funcs.translite_name(strip_tag(n)) or ""
				end
				return ""
			end
		elseif key == "{screen}" then
			return function()
				funcs.Take_Screenshot()
				return L("tags.text.text_98")
			end
		elseif key == "{rpnick}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					return n and strip_tag(n):gsub("_", " ") or ""
				end
				return ""
			end
		elseif key == "{name}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					return n and strip_tag(n):match("([^_]+)") or ""
				end
				return ""
			end
		elseif key == "{nameru}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					local nm = n and strip_tag(n):match("([^_]+)")
					return nm and funcs.translite_name(nm) or ""
				end
				return ""
			end
		elseif key == "{surname}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					return n and strip_tag(n):match(".*_(.+)") or ""
				end
				return ""
			end
		elseif key == "{surnameru}" then
			return function()
				local samp_module = samp
				if samp_module and samp_module.GetNameID and samp_module.Local_ID then
					local n = samp_module.GetNameID(samp_module.Local_ID())
					local sn = n and strip_tag(n):match(".*_(.+)")
					return sn and funcs.translite_name(sn) or ""
				end
				return ""
			end
		elseif key == "{myskin}" then
			return function()
				return getCharModel and getCharModel(PLAYER_PED) or ""
			end
		elseif key == "{city}" then
			return function()
				local mapping = { [0] = "San-Andreas", [1] = "Los-Santos", [2] = "San-Fierro", [3] = "Las-Venturas" }
				local city = getCityPlayerIsIn and getCityPlayerIsIn(PLAYER_PED)
				return mapping[city or 0] or ""
			end
		elseif key == "{time}" then
			return function()
				return os.date("%H:%M:%S")
			end
		elseif key == "{timenosec}" then
			return function()
				return os.date("%H:%M")
			end
		elseif key == "{date}" then
			return function()
				return os.date("%d.%m.%Y")
			end

		-- TARGET-теги
		elseif key == "{targetid}" then
			return function()
				return target.last_id and tostring(target.last_id) or ""
			end
		elseif key == "{targetnick}" then
			return function()
				local id = target.last_id
				if not id then
					return ""
				end
				local n = get_nick_by_id(id)
				return n or ""
			end
		elseif key == "{targetrpnick}" then
			return function()
				local id = target.last_id
				if not id then
					return ""
				end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):gsub("_", " ") or ""
			end
		elseif key == "{targetname}" then
			return function()
				local id = target.last_id
				if not id then
					return ""
				end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):match("([^_]+)") or ""
			end
		elseif key == "{targetsurname}" then
			return function()
				local id = target.last_id
				if not id then
					return ""
				end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):match(".*_(.+)") or ""
			end
		elseif key == "{thisbind}" then
			return function(thisbind_value)
				local binder = resolve_binder()
				if binder and binder.getThisbindTagValue then
					local ok, value = pcall(binder.getThisbindTagValue, thisbind_value)
					if ok and type(value) == "string" then
						return value
					end
				end
				return ""
			end
		elseif key == "{dialogactive}" then
			return function()
				if samp and samp.isDialogActive then
					local ok, active = pcall(samp.isDialogActive)
					if ok and active then
						return "true"
					end
				end
				return "false"
			end
		elseif key == "{dialogwaitopen}" then
			return function(thisbind_value)
				local ok_wait, reason = wait_for_dialog_open(2000, thisbind_value)
				if ok_wait then
					return ""
				end
				if reason == "timeout" then
					log_chat(L("tags.text.tags_dialogwaitopen_2"), 0xAA8800)
					stop_thisbind(thisbind_value)
				elseif reason == "no_samp" then
					log_chat(L("tags.text.tags_dialogwaitopen_samp"), 0xAA3333)
				elseif reason == "no_yield" then
					log_chat(L("tags.text.tags_dialogwaitopen"), 0xAA3333)
				end
				return ""
			end
		elseif key == "{dialogcaption}" then
			return function()
				if samp and samp.get_dialog_caption then
					local ok, caption = pcall(samp.get_dialog_caption)
					if ok and type(caption) == "string" then
						return caption
					end
				end
				return ""
			end
		elseif key == "{getdialogid}" then
			return function()
				if samp and samp.SAMP_DIALOG_ID then
					local ok, dialog_id = pcall(samp.SAMP_DIALOG_ID)
					if ok and dialog_id ~= nil then
						return tostring(dialog_id)
					end
				end
				return ""
			end
		elseif key == "{dialoggetselecteditem}" then
			return function()
				if samp and samp.getDialogSelectedItemText then
					local ok, r1, r2 = pcall(samp.getDialogSelectedItemText)
					if ok and r1 and type(r2) == "string" then
						return to_utf8_safe(r2)
					end
				end
				return ""
			end
		elseif key == "{clipboard}" then
			return function()
				if type(getClipboardText) ~= "function" then
					return ""
				end
				local ok, text = pcall(getClipboardText)
				if ok and type(text) == "string" then
					return to_utf8_safe(text)
				end
				return ""
			end
		elseif key == "{mymoney}" then
			return function()
				if type(getPlayerMoney) ~= "function" then
					return ""
				end
				local handle = rawget(_G, "PLAYER_HANDLE")
				if handle == nil then
					return ""
				end
				local ok, money = pcall(getPlayerMoney, handle)
				if ok and money ~= nil then
					return tostring(math.floor(tonumber(money) or 0))
				end
				return ""
			end
		elseif key == "{health}" then
			return function()
				return get_health_by_id(nil)
			end
		elseif key == "{armour}" then
			return function()
				return get_armour_by_id(nil)
			end
		elseif key == "{sampGetDialogEditboxText}" then
			return function()
				if samp and samp.sampGetDialogEditboxText then
					local ok, text = pcall(samp.sampGetDialogEditboxText)
					if ok and type(text) == "string" then
						return to_utf8_safe(text)
					end
				end
				return ""
			end
		elseif key == "{getvehtype}" then
			return function()
				local ped = rawget(_G, "PLAYER_PED")
				if not ped then return "" end
				if type(isCharInAnyCar) ~= "function" then return "" end
				local ok_in, in_car = pcall(isCharInAnyCar, ped)
				if not ok_in or not in_car then return "" end
				if type(storeCarCharIsInNoSave) ~= "function" or type(getCarModel) ~= "function" then return "" end
				local ok_veh, veh = pcall(storeCarCharIsInNoSave, ped)
				if not ok_veh or not veh then return "" end
				local ok_model, model = pcall(getCarModel, veh)
				if not ok_model or not model then return "" end
				return funcs.getVehicleType(model)
			end
		end

		-- внешние переменные вида {var}
		local keystr = key:match("^{(.+)}$")
		if keystr and external_variables[keystr] then
			return function()
				return external_variables[keystr].fn()
			end
		end
		if keystr and custom_vars[keystr] ~= nil then
			return function()
				return tostring(custom_vars[keystr])
			end
		end
		return nil
	end,
})

local non_cache_simple_tags = {
	["{dialogwaitopen}"] = true,
}

local function parse_simple_tags(text, thisbind_value)
	local out, pos = "", 1
	text = tostring(text or "")

	while true do
		local start_s, start_e, key_name = text:find("{([%w_]+)}", pos)
		if not start_s then
			out = out .. text:sub(pos)
			break
		end

		out = out .. text:sub(pos, start_s - 1)
		local key = "{" .. tostring(key_name or "") .. "}"
		local fn = tags[key]
		if fn then
			local out_value
			local use_cache = not non_cache_simple_tags[key]
			if use_cache then
				local cached = cache_get(key)
				if cached ~= nil then
					out_value = cached
				end
			end
			if out_value == nil then
				local ok, res = pcall(fn, thisbind_value)
				out_value = (ok and res and tostring(res) ~= key) and tostring(res) or ""
				if use_cache then
					cache_set(key, out_value)
				end
			end
			out = out .. out_value
		else
			out = out .. key
		end
		pos = start_e + 1
	end

	if out:match("^%s*$") then
		out = ""
	end
	return out
end

-- ========== ПАРСЕР МУЛЬТИ-ТЕГОВ ==========
local RECURSION_LIMIT = 10

local function handle_multi_tag(tag, val, thisbind_value, depth)
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then
		return L("tags.text.text_99")
	end
	local no_cache = non_cache_multi_tags[tag] and true or false
	local cache_key = nil
	if not no_cache then
		cache_key = tag .. "(" .. tostring(val) .. ")" .. (thisbind_value and ("|" .. tostring(thisbind_value)) or "")
		local cached = cache_get(cache_key)
		if cached ~= nil then
			return cached
		end
	end

	local handler = multi_tag_handlers[tag]
	local ok, res
	if handler then
		ok, res = pcall(handler, val, thisbind_value, depth)
		if not ok then
			res = L("tags.text.text_100") .. tag .. "]"
		end
	else
		res = L("tags.text.text_101") .. tag .. "]"
	end
	if not no_cache and cache_key ~= nil then
		cache_set(cache_key, res)
	end
	return res
end

local function parse_multi_tags(text, thisbind_value, depth)
	local out, pos = "", 1
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then
		return L("tags.text.text_99")
	end
	while true do
		local start_s, start_e, tag = text:find("%[([%w_]+)%s*%(", pos)
		if not start_s then
			out = out .. text:sub(pos)
			break
		end
		out = out .. text:sub(pos, start_s - 1)
		local depth2, i = 1, start_e + 1
		while i <= #text do
			local c = text:sub(i, i)
			if c == "(" then
				depth2 = depth2 + 1
			elseif c == ")" then
				depth2 = depth2 - 1
				if depth2 == 0 then
					if text:sub(i + 1, i + 1) == "]" then
						break
					end
				end
			end
			i = i + 1
		end
		if depth2 == 0 and text:sub(i + 1, i + 1) == "]" then
			local full_tag_text = text:sub(start_s, i + 1)
			local handler = multi_tag_handlers[tag]
			if handler then
				local expr_raw = text:sub(start_e + 1, i - 1)
				local value
				if tag == "ifandor" or tag == "waitif" then
					value = expr_raw
				else
					value = module.change_tags(expr_raw, thisbind_value, depth)
				end
				local inner = handle_multi_tag(tag, value, thisbind_value, depth)
				out = out .. tostring(inner)
			else
				out = out .. full_tag_text
			end
			pos = i + 2
		else
			out = out .. text:sub(start_s)
			break
		end
	end
	return out
end

local function make_readonly_api(source, label)
	local proxy = {}
	return setmetatable(proxy, {
		__index = source,
		__newindex = function()
			error((label or "api") .. " is read-only", 2)
		end,
		__metatable = false,
	})
end

local safe_math_api = make_readonly_api(math, "math")
local safe_string_api = make_readonly_api(string, "string")
local safe_table_api = make_readonly_api(table, "table")

local function get_safe_module_api()
	return make_readonly_api({
		save_config = module.save_config,
		reload_config = module.reload_config,
		reload_external_vars = module.reload_external_vars,
		setTargetNoticeEnabled = module.setTargetNoticeEnabled,
		getTargetNoticeEnabled = module.getTargetNoticeEnabled,
		getLastTargetId = module.getLastTargetId,
		getCurrentTargetId = module.getCurrentTargetId,
		setTargetBlipEnabled = module.setTargetBlipEnabled,
		getTargetBlipEnabled = module.getTargetBlipEnabled,
	}, "module")
end

local function make_blocked_call_api(name)
	return function()
		error("binder." .. tostring(name) .. " is not available in [call(...)]", 2)
	end
end

local function get_safe_binder_api()
	local binder = binder_module
	if not binder then
		return nil
	end
	return make_readonly_api({
		findBind = binder.findBind,
		startBind = binder.startBind,
		stopBind = binder.stopBind,
		disableBind = binder.disableBind,
		enableBind = binder.enableBind,
		pauseBind = binder.pauseBind,
		unpauseBind = binder.unpauseBind,
		isBindEnded = binder.isBindEnded,
		setBindSelector = binder.setBindSelector,
		runBind = binder.runBind,
		runBindRandom = binder.runBindRandom,
		enqueueHotkey = binder.enqueueHotkey,
		stopHotkey = binder.stopHotkey,
		stopAllHotkeys = binder.stopAllHotkeys,
		onIncomingTextMessage = make_blocked_call_api("onIncomingTextMessage"),
		onOutgoingChatInput = make_blocked_call_api("onOutgoingChatInput"),
		onOutgoingCommandInput = make_blocked_call_api("onOutgoingCommandInput"),
		onServerMessage = make_blocked_call_api("onServerMessage"),
		onPlayerCommand = make_blocked_call_api("onPlayerCommand"),
		sendHotkeyCoroutine = make_blocked_call_api("sendHotkeyCoroutine"),
		getThisbindTagValue = make_blocked_call_api("getThisbindTagValue"),
		executeBindTagAction = make_blocked_call_api("executeBindTagAction"),
		runScheduler = make_blocked_call_api("runScheduler"),
		doSend = make_blocked_call_api("doSend"),
	}, "binder")
end

local function safe_require(name)
	name = tostring(name or "")
	if name == "HelperByOrc.binder" then
		local binder_api = get_safe_binder_api()
		if binder_api then
			return binder_api
		end
		error("module 'HelperByOrc.binder' is not available", 2)
	end
	if name == "HelperByOrc.tags" then
		return get_safe_module_api()
	end
	error("module '" .. name .. "' is not allowed in tags sandbox", 2)
end

local function make_safe_env(opts)
	opts = opts or {}
	local env = {
		tonumber = tonumber,
		tostring = tostring,
		type = type,
		pairs = pairs,
		ipairs = ipairs,
		next = next,
		select = select,
		unpack = unpack or table.unpack,
		math = safe_math_api,
		string = safe_string_api,
		table = safe_table_api,
		module = get_safe_module_api(),
		time = os.time,
		clock = os.clock,
		target_last_id = function()
			return target.last_id
		end,
	}
	if opts.allow_call_api then
		env.print = print
		env.require = safe_require
		local binder_api = get_safe_binder_api()
		if binder_api then
			env.binder = binder_api
		end
	end
	return env
end

safe_load_expr = function(expr, opts)
	opts = opts or {}
	local source = tostring(expr or "")
	local chunk_name = opts.chunk_name or "=HelperByOrc.tags"
	local chunk, err = load("return (" .. source .. ")", chunk_name)
	if not chunk and opts.allow_statements then
		chunk, err = load(source, chunk_name)
		if not chunk then
			return nil, err
		end
	elseif not chunk then
		return nil, err
	end
	setfenv(chunk, make_safe_env({ allow_call_api = opts.allow_call_api }))
	return chunk
end

-- ========== ОСНОВНАЯ ФУНКЦИЯ ПОДСТАНОВКИ ==========
function module.change_tags(text, thisbind_value, depth)
	flush_scheduled_config_save(false)
	begin_parse_scope()
	text = text or ""
	text = parse_multi_tags(text, thisbind_value, depth)
	return parse_simple_tags(text, thisbind_value)
end

-- ========== ПУБЛИЧНЫЕ API ПО TARGET / НАСТРОЙКИ ==========
function module.setTargetNoticeEnabled(flag)
	settings.show_target_notice = not not flag
	save_config()
end
function module.getTargetNoticeEnabled()
	return settings.show_target_notice and true or false
end
function module.getLastTargetId()
	return target.last_id
end
function module.getCurrentTargetId()
	return target.current_id
end
function module.setUnsafeAllowed(flag)
	settings.allow_unsafe = not not flag
	save_config()
end
function module.setTargetBlipEnabled(flag)
	settings.show_target_blip = not not flag
	if settings.show_target_blip then
		if target.current_ped then
			set_target_blip(target.current_ped)
		end
	else
		clear_target_blip()
	end
	save_config()
end
function module.getTargetBlipEnabled()
	return settings.show_target_blip and true or false
end

-- ========== СПРАВОЧНЫЕ СПИСКИ ДЛЯ UI ==========
local function get_custom_var_list()
	local out = {}
	for k, v in pairs(custom_vars) do
		out[#out + 1] = { key = k, value = v }
	end
	table.sort(out, function(a, b)
		return a.key < b.key
	end)
	return out
end

local function get_var_list()
	local out, exists = {}, {}
	for _, tag in ipairs(simple_tags) do
		table.insert(out, { name = tag.name, desc = tag.desc, custom = false })
		exists[tag.name] = true
	end
	for k, v in pairs(external_variables) do
		local tagname = "{" .. k .. "}"
		if not exists[tagname] then
			table.insert(out, { name = tagname, desc = v.desc or L("tags.text.text_102"), custom = false })
		end
	end
	table.sort(out, function(a, b)
		return a.name < b.name
	end)
	return out
end

local function get_func_list()
	local out = {}
	for tag, v in pairs(multi_tags_descriptions) do
		table.insert(
			out,
			{
				tag = tag,
				name = ("[%s(...)]"):format(tag),
				desc = v.desc,
				example = v.example or ("[%s(...)]"):format(tag),
			}
		)
	end
	table.sort(out, function(a, b)
		return a.name < b.name
	end)
	return out
end

-- ========== UI (mimgui) ==========
local showTagsWindow = imgui.new.bool(false)
module.showTagsWindow = showTagsWindow

-- состояние UI копирования
local ui_state = { copied_text = nil, copied_time = 0, flash_sec = 1.5 }
local function flash_copied(txt)
	ui_state.copied_text = txt
	ui_state.copied_time = os.clock()
end

-- ===== UI helpers =====
-- local function ICON(key) return (ok_fa and fa and fa[key]) or "" end
local function HelpTip(text)
	if imgui.IsItemHovered() then
		imgui.BeginTooltip()
		imgui.PushTextWrapPos(imgui.GetFontSize() * 35.0)
		imgui.TextUnformatted(text)
		imgui.PopTextWrapPos()
		imgui.EndTooltip()
	end
end

local function CopyFlash(str)
	imgui.SetClipboardText(str)
	flash_copied(L("tags.text.text_103") .. str)
end

local function Badge(txt)
	imgui.PushStyleVarVec2(imgui.StyleVar.FramePadding, imgui.ImVec2(6, 3))
	imgui.PushStyleVarFloat(imgui.StyleVar.FrameRounding, 6)
	imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.20, 0.60, 0.90, 0.25))
	imgui.PushStyleColor(imgui.Col.ButtonHovered, imgui.ImVec4(0.20, 0.60, 0.90, 0.35))
	imgui.PushStyleColor(imgui.Col.ButtonActive, imgui.ImVec4(0.20, 0.60, 0.90, 0.50))
	imgui.Button(txt)
	imgui.PopStyleColor(3)
	imgui.PopStyleVar(2)
end

local DIALOGTEXT_POPUP_SETTINGS = L("tags.text.dialogtext_picker_settings")
local DIALOGTEXT_POPUP_HELP = L("tags.text.dialogtext_picker_help")
local DIALOGTEXT_PICKER_DELAY_SEC = 0.05
local TAG_PICKER_DELAY_SEC = 0.05

local dialogtext_picker_state = {
	pending_popup = nil,
	pending_open_at = 0,
	rows = {},
	token_count = 0,
	caption = "",
	error = nil,
}

local function refresh_dialogtext_picker_data()
	dialogtext_picker_state.rows = {}
	dialogtext_picker_state.token_count = 0
	dialogtext_picker_state.caption = ""
	dialogtext_picker_state.error = nil

	local flat, rows, err = read_active_dialogtext_items(true)
	if not flat then
		if err == "no_samp" then
			dialogtext_picker_state.error = L("tags.text.samp_104")
		elseif err == "no_dialog" then
			dialogtext_picker_state.error = L("tags.text.text_105")
		elseif err == "no_reader" then
			dialogtext_picker_state.error = L("tags.text.sampgetdialogtext")
		else
			dialogtext_picker_state.error = L("tags.text.text_106")
		end
		return false
	end

	dialogtext_picker_state.rows = rows or {}
	dialogtext_picker_state.token_count = #flat
	if samp and samp.get_dialog_caption then
		local ok_caption, caption = pcall(samp.get_dialog_caption)
		if ok_caption and type(caption) == "string" then
			dialogtext_picker_state.caption = to_utf8_safe(caption)
		end
	end
	if dialogtext_picker_state.token_count == 0 then
		dialogtext_picker_state.error = L("tags.text.text_107")
		return false
	end
	return true
end

local function request_dialogtext_picker_open(popup_id)
	if not samp then
		log_chat(L("tags.text.tags_dialogtext_samp"), 0xAA3333)
		return
	end
	if not (samp.isDialogActive and samp.isDialogActive()) then
		log_chat(L("tags.text.tags_dialogtext_108"), 0xAA3333)
		return
	end
	dialogtext_picker_state.pending_popup = popup_id
	dialogtext_picker_state.pending_open_at = os.clock() + DIALOGTEXT_PICKER_DELAY_SEC
end

local function process_dialogtext_picker_open(popup_id)
	if dialogtext_picker_state.pending_popup ~= popup_id then
		return
	end
	if os.clock() < (dialogtext_picker_state.pending_open_at or 0) then
		return
	end
	refresh_dialogtext_picker_data()
	imgui.OpenPopup(popup_id)
	dialogtext_picker_state.pending_popup = nil
end

local function draw_dialogtext_picker_tokens(child_id)
	imgui.BeginChild(child_id, imgui.ImVec2(720, 480), true)
	imgui.PushStyleVarFloat(imgui.StyleVar.FrameRounding, 10)
	imgui.PushStyleVarVec2(imgui.StyleVar.FramePadding, imgui.ImVec2(8, 4))
	imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.18, 0.22, 0.28, 0.55))
	imgui.PushStyleColor(imgui.Col.ButtonHovered, imgui.ImVec4(0.28, 0.35, 0.45, 0.85))
	imgui.PushStyleColor(imgui.Col.ButtonActive, imgui.ImVec4(0.35, 0.45, 0.58, 1.00))

	for row_idx, row in ipairs(dialogtext_picker_state.rows) do
		local first_in_row = true
		for _, item in ipairs(row) do
			local label = item.text or ""
			if label == "" then
				label = " "
			end
			local style = imgui.GetStyle()
			local btn_w = imgui.CalcTextSize(label).x + style.FramePadding.x * 2 + 4
			if (not first_in_row) and (btn_w > imgui.GetContentRegionAvail().x) then
				first_in_row = true
			end
			if not first_in_row then
				imgui.SameLine()
			end
			first_in_row = false

			imgui.PushIDInt(item.index)
			if imgui.SmallButton(label) then
				CopyFlash(L("tags.example.dialogtext_picker_copy", { index = item.index }))
			end
			HelpTip((L("tags.text.number_dialogtext_number")):format(item.index, item.index))
			imgui.PopID()
		end
		if row_idx < #dialogtext_picker_state.rows then
			imgui.Spacing()
		end
	end

	imgui.PopStyleColor(3)
	imgui.PopStyleVar(2)
	imgui.EndChild()
end

local function draw_dialogtext_picker_popup(popup_id)
	process_dialogtext_picker_open(popup_id)

	imgui.SetNextWindowSize(imgui.ImVec2(760, 620), imgui.Cond.Appearing)
	if imgui.BeginPopupModal(popup_id, nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.TextUnformatted(L("tags.text.text_109"))
		if dialogtext_picker_state.caption ~= "" then
			imgui.TextUnformatted(L("tags.text.text_110") .. dialogtext_picker_state.caption)
		end
		if imgui.Button(L("tags.text.text_111")) then
			refresh_dialogtext_picker_data()
		end
		imgui.SameLine()
		if imgui.Button(L("tags.text.text_112")) then
			imgui.CloseCurrentPopup()
		end
		imgui.Separator()

		if dialogtext_picker_state.error then
			imgui.TextUnformatted(dialogtext_picker_state.error)
		else
			imgui.TextUnformatted((L("tags.text.number")):format(dialogtext_picker_state.token_count))
			draw_dialogtext_picker_tokens("dialogtext_picker_tokens##" .. popup_id)
		end

		imgui.EndPopup()
	end
end

local function tag_picker_popup_id(tag_name, context)
	return ("tags_%s_picker_%s"):format(tostring(tag_name or ""), tostring(context or "default"))
end

local virtual_key_picker_entries
local game_key_picker_entries
local virtual_key_filter = imgui.new.char[96]()
local game_key_filter = imgui.new.char[96]()
local dialogitem_filter = imgui.new.char[96]()
local keydown_duration_ms = imgui.new.int(1000)
local game_key_state_value = imgui.new.int(32767)
local tag_picker_state = {
	pending_popup = nil,
	pending_open_at = 0,
	pending_refresh = nil,
}
local char_key_examples = {
	{ copy = L("tags.example.charkeyemulate_char"), desc = L("tags.text.w_119") },
	{ copy = L("tags.example.charkeyemulate_digit"), desc = L("tags.text.text_1_49") },
	{ copy = L("tags.example.charkeyemulate_slash"), desc = L("tags.text.text_47_113") },
	{ copy = L("tags.example.charkeyemulate_space"), desc = L("tags.text.text_32") },
	{ copy = L("tags.example.charkeyemulate_code"), desc = L("tags.text.text_114") },
}
local dialogitem_picker_state = {
	items = {},
	header = "",
	caption = "",
	error = nil,
}

local function get_virtual_key_picker_entries()
	if virtual_key_picker_entries then
		return virtual_key_picker_entries
	end

	local out = {}
	local vk = get_vk_module()
	for code = 0, 255 do
		local name = ""
		if vk and type(vk.id_to_name) == "function" then
			local ok_name, raw_name = pcall(vk.id_to_name, code)
			if ok_name and raw_name ~= nil then
				name = tostring(raw_name)
			end
		end
		if name == "" or name == tostring(code) then
			name = ("VK_%03d"):format(code)
		end
		name = name:gsub("^VK_", ""):gsub("_", " ")
		out[#out + 1] = {
			code = code,
			name = name,
			search = (name .. " " .. tostring(code)):lower(),
		}
	end

	virtual_key_picker_entries = out
	return virtual_key_picker_entries
end

local function get_game_key_picker_entries()
	if game_key_picker_entries then
		return game_key_picker_entries
	end

	local out = {}
	local keys = get_game_keys_module()
	local function append(group_name, list)
		for name, code in pairs(list or {}) do
			if type(code) == "number" then
				local label = tostring(name):gsub("_", " ")
				out[#out + 1] = {
					group = group_name,
					name = tostring(name),
					label = label,
					code = math.floor(code),
					search = (group_name .. " " .. tostring(name) .. " " .. tostring(code)):lower(),
				}
			end
		end
	end

	if type(keys) == "table" then
		append("player", keys.player)
		append("vehicle", keys.vehicle)
	end

	table.sort(out, function(a, b)
		if a.code ~= b.code then
			return a.code < b.code
		end
		if a.group ~= b.group then
			return a.group < b.group
		end
		return a.name < b.name
	end)

	game_key_picker_entries = out
	return game_key_picker_entries
end

local function refresh_dialogitem_picker_data()
	dialogitem_picker_state.items = {}
	dialogitem_picker_state.header = ""
	dialogitem_picker_state.caption = ""
	dialogitem_picker_state.error = nil

	local items, header_text, err = read_active_dialog_list_items(true)
	if not items then
		if err == "no_samp" then
			dialogitem_picker_state.error = L("tags.text.samp_104")
		elseif err == "no_dialog" then
			dialogitem_picker_state.error = L("tags.text.text_105")
		elseif err == "not_list" then
			dialogitem_picker_state.error = L("tags.text.text_115")
		elseif err == "no_reader" then
			dialogitem_picker_state.error = L("tags.text.sampgetdialogtext")
		else
			dialogitem_picker_state.error = L("tags.text.text_116")
		end
		return false
	end

	dialogitem_picker_state.items = items or {}
	dialogitem_picker_state.header = header_text or ""
	if samp and samp.get_dialog_caption then
		local ok_caption, caption = pcall(samp.get_dialog_caption)
		if ok_caption and type(caption) == "string" then
			dialogitem_picker_state.caption = to_utf8_safe(caption)
		end
	end
	if #dialogitem_picker_state.items == 0 then
		dialogitem_picker_state.error = L("tags.text.text_117")
		return false
	end
	return true
end

local function request_tag_picker_open(popup_id, refresh_fn)
	if tag_picker_state.pending_popup == popup_id then
		return
	end
	tag_picker_state.pending_popup = popup_id
	tag_picker_state.pending_open_at = os.clock() + TAG_PICKER_DELAY_SEC
	tag_picker_state.pending_refresh = refresh_fn
end

local function process_tag_picker_open(popup_id)
	if tag_picker_state.pending_popup ~= popup_id then
		return
	end
	if os.clock() < (tag_picker_state.pending_open_at or 0) then
		return
	end
	if type(tag_picker_state.pending_refresh) == "function" then
		tag_picker_state.pending_refresh()
	end
	imgui.OpenPopup(popup_id)
	tag_picker_state.pending_popup = nil
	tag_picker_state.pending_open_at = 0
	tag_picker_state.pending_refresh = nil
end

local function draw_virtual_key_picker_popup(popup_id, mode)
	process_tag_picker_open(popup_id)
	imgui.SetNextWindowSize(imgui.ImVec2(560, 520), imgui.Cond.Appearing)
	if not imgui.BeginPopup(popup_id) then
		return
	end

	if mode == "keydown" then
		imgui.TextUnformatted(L("tags.text.keydown"))
		if imgui.InputInt(L("tags.text.text_118") .. popup_id, keydown_duration_ms, 100, 500) then
			if keydown_duration_ms[0] < 1 then
				keydown_duration_ms[0] = 1
			end
		end
	else
		imgui.TextUnformatted(L("tags.text.text_119"))
	end

	imgui.SetNextItemWidth(260)
	imgui.InputText(L("tags.text.text_120") .. popup_id, virtual_key_filter, ffi.sizeof(virtual_key_filter))
	imgui.Separator()

	local filter = ffi.string(virtual_key_filter):lower()
	imgui.BeginChild("virtual_key_picker_child##" .. popup_id, imgui.ImVec2(520, 360), true)
	for _, item in ipairs(get_virtual_key_picker_entries()) do
		if filter == "" or item.search:find(filter, 1, true) then
			local copy = (mode == "keydown")
				and L("tags.example.keydown_picker_copy", {
					code = item.code,
					duration = math.max(1, tonumber(keydown_duration_ms[0]) or 1000),
				})
				or L("tags.example.keyemulate_picker_copy", { code = item.code })
			local label = (L("tags.format.virtual_key_picker_label")):format(item.code, item.name)
			if imgui.Selectable(label, false) then
				CopyFlash(copy)
				imgui.CloseCurrentPopup()
			end
			HelpTip(L("tags.text.text_121") .. copy)
		end
	end
	imgui.EndChild()

	if imgui.Button(L("tags.text.text_122") .. popup_id) then
		imgui.CloseCurrentPopup()
	end
	imgui.EndPopup()
end

local function draw_char_key_examples_popup(popup_id)
	process_tag_picker_open(popup_id)
	imgui.SetNextWindowSize(imgui.ImVec2(460, 250), imgui.Cond.Appearing)
	if not imgui.BeginPopup(popup_id) then
		return
	end

	imgui.TextUnformatted(L("tags.text.charkeyemulate"))
	imgui.TextWrapped(L("tags.text.text_123"))
	imgui.Separator()
	for _, item in ipairs(char_key_examples) do
		if imgui.Selectable(item.copy, false) then
			CopyFlash(item.copy)
			imgui.CloseCurrentPopup()
		end
		HelpTip(item.desc)
	end
	if imgui.Button(L("tags.text.text_122") .. popup_id) then
		imgui.CloseCurrentPopup()
	end
	imgui.EndPopup()
end

local function draw_game_key_picker_popup(popup_id)
	process_tag_picker_open(popup_id)
	imgui.SetNextWindowSize(imgui.ImVec2(620, 520), imgui.Cond.Appearing)
	if not imgui.BeginPopup(popup_id) then
		return
	end

	imgui.TextUnformatted(L("tags.text.text_124"))
	if imgui.InputInt(L("tags.text.text_125") .. popup_id, game_key_state_value, 256, 1024) then
		if game_key_state_value[0] > 32767 then
			game_key_state_value[0] = 32767
		elseif game_key_state_value[0] < -32768 then
			game_key_state_value[0] = -32768
		end
	end
	HelpTip(L("tags.text.text_32767"))
	imgui.SetNextItemWidth(260)
	imgui.InputText(L("tags.text.text_120") .. popup_id, game_key_filter, ffi.sizeof(game_key_filter))
	imgui.Separator()

	local entries = get_game_key_picker_entries()
	if #entries == 0 then
		imgui.TextUnformatted(L("tags.text.game_keys"))
	else
		local filter = ffi.string(game_key_filter):lower()
		imgui.BeginChild("game_key_picker_child##" .. popup_id, imgui.ImVec2(580, 340), true)
		for _, item in ipairs(entries) do
			if filter == "" or item.search:find(filter, 1, true) then
				local state = math.max(-32768, math.min(32767, tonumber(game_key_state_value[0]) or 32767))
				local copy = L("tags.example.gamekeyemulate_picker_copy", { code = item.code, state = state })
				local label = (L("tags.format.game_key_picker_label")):format(item.code, item.group, item.label)
				if imgui.Selectable(label, false) then
					CopyFlash(copy)
					imgui.CloseCurrentPopup()
				end
				HelpTip(L("tags.text.text_121") .. copy)
			end
		end
		imgui.EndChild()
	end

	if imgui.Button(L("tags.text.text_122") .. popup_id) then
		imgui.CloseCurrentPopup()
	end
	imgui.EndPopup()
end

local function draw_dialogitem_picker_popup(popup_id)
	process_tag_picker_open(popup_id)
	imgui.SetNextWindowSize(imgui.ImVec2(640, 520), imgui.Cond.Appearing)
	if not imgui.BeginPopup(popup_id) then
		return
	end

	imgui.TextUnformatted(L("tags.text.text_126"))
	if dialogitem_picker_state.caption ~= "" then
		imgui.TextUnformatted(L("tags.text.text_110") .. dialogitem_picker_state.caption)
	end
	if dialogitem_picker_state.header ~= "" then
		imgui.TextWrapped(L("tags.text.text_127") .. dialogitem_picker_state.header)
	end
	if imgui.Button(L("tags.text.text_128") .. popup_id) then
		refresh_dialogitem_picker_data()
	end
	imgui.SameLine()
	if imgui.Button(L("tags.text.text_122") .. popup_id) then
		imgui.CloseCurrentPopup()
	end
	imgui.SetNextItemWidth(260)
	imgui.InputText(L("tags.text.text_120") .. popup_id, dialogitem_filter, ffi.sizeof(dialogitem_filter))
	imgui.Separator()

	if dialogitem_picker_state.error then
		imgui.TextUnformatted(dialogitem_picker_state.error)
	else
		local filter = ffi.string(dialogitem_filter):lower()
		imgui.BeginChild("dialogitem_picker_child##" .. popup_id, imgui.ImVec2(600, 340), true)
		for _, item in ipairs(dialogitem_picker_state.items) do
			local visible_text = item.text ~= "" and item.text or L("tags.text.text_129")
			local search = (visible_text .. " " .. tostring(item.index1)):lower()
			if filter == "" or search:find(filter, 1, true) then
				local copy = L("tags.example.dialogitem_picker_copy", { index = item.index1 })
				local label = (L("tags.format.dialogitem_picker_label")):format(item.index1, visible_text)
				if imgui.Selectable(label, false) then
					CopyFlash(copy)
					imgui.CloseCurrentPopup()
				end
				HelpTip(L("tags.text.text_121") .. copy)
			end
		end
		imgui.EndChild()
	end

	imgui.EndPopup()
end

local function draw_tag_picker_button(tag_name, context)
	local popup_id
	local tooltip
	local refresh_fn

	if tag_name == "keyemulate" then
		popup_id = tag_picker_popup_id(tag_name, context)
		tooltip = L("tags.text.text_130")
	elseif tag_name == "keydown" then
		popup_id = tag_picker_popup_id(tag_name, context)
		tooltip = L("tags.text.keydown_key_ms")
	elseif tag_name == "charkeyemulate" then
		popup_id = tag_picker_popup_id(tag_name, context)
		tooltip = L("tags.text.text_131")
	elseif tag_name == "gamekeyemulate" then
		popup_id = tag_picker_popup_id(tag_name, context)
		tooltip = L("tags.text.text_132")
	elseif tag_name == "dialogitem" then
		popup_id = tag_picker_popup_id(tag_name, context)
		tooltip = L("tags.text.text_133")
		refresh_fn = refresh_dialogitem_picker_data
	else
		return false
	end

	local opened = imgui.SmallButton("+##" .. popup_id)
	if opened then
		request_tag_picker_open(popup_id, refresh_fn)
	end
	HelpTip(tooltip)
	imgui.SameLine()
	return true
end

local function draw_tag_picker_popups(context)
	draw_virtual_key_picker_popup(tag_picker_popup_id("keyemulate", context), "keyemulate")
	draw_virtual_key_picker_popup(tag_picker_popup_id("keydown", context), "keydown")
	draw_char_key_examples_popup(tag_picker_popup_id("charkeyemulate", context))
	draw_game_key_picker_popup(tag_picker_popup_id("gamekeyemulate", context))
	draw_dialogitem_picker_popup(tag_picker_popup_id("dialogitem", context))
end

-- локальные буферы для поиска/ввода
local filter_vars = imgui.new.char[96]()
local filter_funcs = imgui.new.char[96]()
local new_var_name = imgui.new.char[64]()
local new_var_value = imgui.new.char[256]()
local edit_key = nil
local del_key = nil
local rename_var_name = imgui.new.char[64]()
local rename_popup_seeded = false

-- ===== ПЕРЕРАБОТАННАЯ СТРАНИЦА НАСТРОЕК =====
function module.DrawSettingsPage()
	imgui.TextColored(
		imgui.ImVec4(0.75, 1, 1, 1),
		L("tags.text.text_134")
	)
	imgui.Separator()

	if imgui.BeginTabBar("tags_tabbar") then
		-- === ВКЛАДКА: Основное ===
		if imgui.BeginTabItem(L("tags.text.text_135")) then
			imgui.Text(L("tags.text.text_136"))
			imgui.BeginChild("main_opts", imgui.ImVec2(0, 175), true)

			-- Показ уведомления о target
			do
				local v = ffi.new("bool[1]", settings.show_target_notice and true or false)
				if imgui.Checkbox(L("tags.text.targetid"), v) then
					settings.show_target_notice = v[0] and true or false
					save_config()
				end
				imgui.SameLine()
				Badge("{targetid}")
				HelpTip(
					L("tags.text.id_137")
				)
			end

			-- Разрешить [call]
			do
				local v = ffi.new("bool[1]", settings.allow_unsafe and true or false)
				if
					imgui.Checkbox(L("tags.text.call"), v)
				then
					settings.allow_unsafe = v[0] and true or false
					save_config()
				end
				HelpTip(
					L("tags.text.lua_138")
				)
			end

			-- Маркер над педом при выборе цели
			do
				local v = ffi.new("bool[1]", settings.show_target_blip and true or false)
				if imgui.Checkbox(L("tags.text.text_139"), v) then
					settings.show_target_blip = v[0] and true or false
					if settings.show_target_blip then
						if target.current_ped then
							set_target_blip(target.current_ped)
						end
					else
						clear_target_blip()
					end
					save_config()
				end
				HelpTip(
					L("tags.text.text_140")
				)
			end

			imgui.EndChild()

			-- Управление конфигом
			imgui.Text(L("tags.text.text_141"))
			imgui.BeginChild("cfg_ops", imgui.ImVec2(0, 70), true)
			if imgui.Button(L("tags.text.text_142")) then
				save_config()
				flash_copied(L("tags.text.text_143"))
			end
			imgui.SameLine()
			if imgui.Button(L("tags.text.text_144")) then
				module.reload_config()
				flash_copied(L("tags.text.text_145"))
			end
			imgui.SameLine()
			if imgui.Button(L("tags.text.text_146")) then
				custom_vars = {}
				for k, v in pairs(builtin_custom_vars) do
					custom_vars[k] = v
				end
				settings.show_target_notice = true
				settings.allow_unsafe = true
				settings.show_target_blip = false
				clear_target_blip()
				rebuild_cvar_buffers()
				save_config()
				clear_parse_cache()
				flash_copied(L("tags.text.text_147"))
			end
			imgui.EndChild()

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Переменные ===
		if imgui.BeginTabItem(L("tags.text.text_148")) then
			imgui.Text(L("tags.text.text_149"))
			imgui.BeginChild("vars_child", imgui.ImVec2(0, -140), true)

			-- Поиск
			imgui.SetNextItemWidth(240)
			imgui.InputText(L("tags.text.text_150"), filter_vars, ffi.sizeof(filter_vars))
			local fstr = ffi.string(filter_vars):lower()

			imgui.Separator()
			imgui.Columns(3, "vars_cols", false)
			imgui.Text(L("tags.text.text_151"))
			imgui.NextColumn()
			imgui.Text(L("tags.text.text_152"))
			imgui.NextColumn()
			imgui.Text(L("tags.text.text_153"))
			imgui.NextColumn()
			imgui.Separator()

			for _, tag in ipairs(get_custom_var_list()) do
				local name = tag.key or ""
				local val = tostring(tag.value or "")
				if fstr == "" or name:lower():find(fstr, 1, true) or val:lower():find(fstr, 1, true) then
					imgui.PushIDStr(name)
					-- Имя
					if imgui.Selectable("{" .. name .. "}", false) then
						CopyFlash("{" .. name .. "}")
					end
					imgui.NextColumn()
					-- Значение (редактируемое)
					local buf = cvar_bufs[name]
					if not buf then
						buf = imgui.new.char[256](val)
						cvar_bufs[name] = buf
					end
					if imgui.InputText("##val", buf, ffi.sizeof(buf)) then
						custom_vars[name] = ffi.string(buf)
						schedule_config_save()
						clear_parse_cache()
					end
					if imgui.IsItemDeactivatedAfterEdit and imgui.IsItemDeactivatedAfterEdit() then
						flush_scheduled_config_save(true)
					end
					imgui.NextColumn()
					-- Кнопки
					if imgui.SmallButton(L("tags.text.text_154")) then
						CopyFlash("{" .. name .. "}")
					end
					imgui.SameLine()
					if imgui.SmallButton(L("tags.text.text_155")) then
						edit_key = name
						rename_popup_seeded = false
					end
					imgui.SameLine()
					if imgui.SmallButton(L("tags.text.text_156")) then
						del_key = name
					end
					imgui.PopID()
					imgui.NextColumn()
				end
			end
			imgui.Columns(1)
			imgui.EndChild()

			-- Добавить новую
			imgui.Text(L("tags.text.text_157"))
			imgui.BeginChild("add_var", imgui.ImVec2(0, 70), true)
			imgui.SetNextItemWidth(180)
			imgui.InputText(L("tags.text.text_151"), new_var_name, ffi.sizeof(new_var_name))
			imgui.SameLine()
			imgui.SetNextItemWidth(340)
			imgui.InputText(L("tags.text.text_152"), new_var_value, ffi.sizeof(new_var_value))
			imgui.SameLine()
			if imgui.Button(L("tags.text.text_158")) then
				local k = (ffi.string(new_var_name) or ""):gsub("^%s*(.-)%s*$", "%1")
				local v = ffi.string(new_var_value) or ""
				if k ~= "" then
					custom_vars[k] = v
					cvar_bufs[k] = imgui.new.char[256](v)
					new_var_name = imgui.new.char[64]()
					new_var_value = imgui.new.char[256]()
					save_config()
					clear_parse_cache()
				end
			end
			imgui.EndChild()

			-- Попап: переименование
			if edit_key then
				if not rename_popup_seeded then
					imgui.StrCopy(rename_var_name, tostring(edit_key))
					rename_popup_seeded = true
				end
				imgui.OpenPopup(L("tags.text.text_159"))
			end
			if
				imgui.BeginPopupModal(
					L("tags.text.text_159"),
					nil,
					imgui.WindowFlags.AlwaysAutoResize
				)
			then
				imgui_text_safe(L("tags.text.text_160") .. tostring(edit_key) .. "}")
				imgui.InputText(L("tags.text.text_161"), rename_var_name, ffi.sizeof(rename_var_name))
				if imgui.Button(L("common.ok") .. "##rename") then
					local newname = ffi.string(rename_var_name)
					if newname ~= "" and newname ~= edit_key then
						custom_vars[newname] = custom_vars[edit_key]
						cvar_bufs[newname] = cvar_bufs[edit_key]
						custom_vars[edit_key] = nil
						cvar_bufs[edit_key] = nil
						save_config()
						clear_parse_cache()
					end
					edit_key = nil
					rename_popup_seeded = false
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button(L("tags.text.rename")) then
					edit_key = nil
					rename_popup_seeded = false
					imgui.CloseCurrentPopup()
				end
				imgui.EndPopup()
			end

			-- Попап: удаление
			if del_key then
				imgui.OpenPopup(L("tags.text.ya"))
			end
			if
				imgui.BeginPopupModal(L("tags.text.ya"), nil, imgui.WindowFlags.AlwaysAutoResize)
			then
				imgui_text_safe(L("tags.text.text_162") .. tostring(del_key) .. "}?")
				if imgui.Button(L("tags.text.del")) then
					custom_vars[del_key] = nil
					cvar_bufs[del_key] = nil
					save_config()
					clear_parse_cache()
					del_key = nil
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button(L("tags.text.del_163")) then
					del_key = nil
					imgui.CloseCurrentPopup()
				end
				imgui.EndPopup()
			end

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Теги и функции ===
		if imgui.BeginTabItem(L("tags.text.text_164")) then
			imgui.Columns(2, "tf_cols", true)

			-- Переменные (встроенные+внешние)
			imgui.Text(L("tags.text.text_148"))
			imgui.BeginChild("vars_list", imgui.ImVec2(0, -30), true)
			for i, tag in ipairs(get_var_list()) do
				imgui.PushIDStr("v" .. tostring(i))
				if imgui.Selectable(tag.name, false) then
					CopyFlash(tag.name)
				end
				HelpTip(tag.desc or "")
				imgui.PopID()
			end
			imgui.EndChild()
			imgui.NextColumn()

			-- Функции-теги
			imgui.Text(L("tags.text.text_165"))
			imgui.SetNextItemWidth(240)
			imgui.InputText(L("tags.text.text_166"), filter_funcs, ffi.sizeof(filter_funcs))
			local ff = ffi.string(filter_funcs):lower()

			imgui.BeginChild("funcs_list", imgui.ImVec2(0, -30), true)
			for i, tag in ipairs(get_func_list()) do
				local name = tag.name or ""
				local desc = tag.desc or ""
				local example = tag.example or name
				if ff == "" or name:lower():find(ff, 1, true) or desc:lower():find(ff, 1, true) then
					imgui.PushIDStr("f" .. tostring(i))
					if not draw_tag_picker_button(tag.tag, "settings") and tag.tag == "dialogtext" then
						if imgui.SmallButton("+") then
							request_dialogtext_picker_open(DIALOGTEXT_POPUP_SETTINGS)
						end
						HelpTip(L("tags.text.text_167"))
						imgui.SameLine()
					end
					if imgui.Selectable(name, false) then
						CopyFlash(example)
					end
					HelpTip((desc ~= "" and (desc .. L("tags.text.text_168") .. example)) or (L("tags.text.text_169") .. example))
					imgui.PopID()
				end
			end
			imgui.EndChild()
			draw_dialogtext_picker_popup(DIALOGTEXT_POPUP_SETTINGS)
			draw_tag_picker_popups("settings")

			imgui.Columns(1)

			imgui.Separator()
			if imgui.Button(L("tags.text.text_170")) then
				module.showTagsWindow[0] = true
			end
			imgui.SameLine()
			Badge(L("tags.text.text_171"))

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Импорт/экспорт ===
		if imgui.BeginTabItem(L("tags.text.text_172")) then
			imgui.TextWrapped(
				L("tags.text.text_173")
			)
			imgui.BeginChild("io_box", imgui.ImVec2(0, 100), true)
			if imgui.Button(L("tags.text.tags_json")) then
				save_config()
				flash_copied(L("tags.text.text_174") .. CONFIG_PATH_REL)
			end
			imgui.SameLine()
			if imgui.Button(L("tags.text.tags_json_175")) then
				module.reload_config()
				flash_copied(L("tags.text.text_176") .. CONFIG_PATH_REL)
			end
			imgui.EndChild()
			imgui.EndTabItem()
		end

		imgui.EndTabBar()
	end
	flush_scheduled_config_save(false)
end

-- ===== ОТДЕЛЬНОЕ ОКНО «Справка по тегам» =====
local _imguiSubs = {}
_imguiSubs[#_imguiSubs + 1] = imgui.OnFrame(function()
	return showTagsWindow[0]
end, function()
	imgui.SetNextWindowSize(imgui.ImVec2(820, 700), imgui.Cond.FirstUseEver)
	imgui.Begin(L("tags.text.helperbyorc"), showTagsWindow, imgui.WindowFlags.NoCollapse)

	imgui.TextColored(
		imgui.ImVec4(0.75, 1, 1, 1),
		L("tags.text.text_177")
	)
	imgui.Separator()

	imgui.Columns(2, "help_cols", true)

	-- Переменные
	imgui.Text(L("tags.text.text_148"))
	imgui.BeginChild("help_vars", imgui.ImVec2(0, -30), true)
	for i, tag in ipairs(get_var_list()) do
		imgui.PushIDStr("hv" .. tostring(i))
		if imgui.Selectable(tag.name, false) then
			CopyFlash(tag.name)
		end
		HelpTip(tag.desc or "")
		imgui.PopID()
	end
	imgui.EndChild()
	imgui.NextColumn()

	-- Функции-теги
	imgui.Text(L("tags.text.text_165"))
	imgui.BeginChild("help_funcs", imgui.ImVec2(0, -30), true)
	for i, tag in ipairs(get_func_list()) do
		imgui.PushIDStr("hf" .. tostring(i))
		local copy = tag.example or tag.name
		if not draw_tag_picker_button(tag.tag, "help") and tag.tag == "dialogtext" then
			if imgui.SmallButton("+") then
				request_dialogtext_picker_open(DIALOGTEXT_POPUP_HELP)
			end
			HelpTip(L("tags.text.text_167"))
			imgui.SameLine()
		end
		if imgui.Selectable(tag.name, false) then
			CopyFlash(copy)
		end
		HelpTip((tag.desc or "") .. (copy and (L("tags.text.text_168") .. copy) or ""))
		imgui.PopID()
	end
	imgui.EndChild()
	draw_dialogtext_picker_popup(DIALOGTEXT_POPUP_HELP)
	draw_tag_picker_popups("help")

	imgui.Columns(1)
	imgui.Separator()
	do
		local dt = os.clock() - (ui_state.copied_time or 0)
		if ui_state.copied_text and dt < (ui_state.flash_sec or 1.5) then
			imgui.Spacing()
			imgui_text_colored_safe(imgui.ImVec4(0.5, 1.0, 0.5, 1.0), ui_state.copied_text)
		end
	end

	imgui.Spacing()
	if imgui.Button(L("tags.text.text_112")) then
		showTagsWindow[0] = false
	end
	if mimgui_funcs and mimgui_funcs.clampCurrentWindowToScreen then
		mimgui_funcs.clampCurrentWindowToScreen(5)
	end
	imgui.End()
end)

-- ========== ПОДКЛЮЧЕНИЕ МОДУЛЕЙ ==========
function module.attachModules(mod)
	syncDependencies(mod)
	config_manager_ref = mod.config_manager
	event_bus_ref = mod.event_bus
	if event_bus_ref then
		event_bus_ref.offByOwner("tags")
	end
	if config_manager_ref then
		local data = config_manager_ref.register("tags", {
			path = CONFIG_PATH_REL,
			defaults = {},
			loader = function(path, defaults)
				load_custom_vars()
				return { vars = custom_vars, settings = settings }
			end,
			serialize = function(data)
				return { vars = custom_vars, settings = settings }
			end,
		})
		if type(data) == "table" then
			if type(data.vars) == "table" then
				custom_vars = data.vars
			end
			if type(data.settings) == "table" then
				settings = data.settings
			end
		end
	end
end

-- ========== СЕРВИСНЫЕ ФУНКЦИИ (СОХРАНИТЬ/ПЕРЕЧИТАТЬ) ==========
module.save_config = function()
	if pending_config_save_at then
		flush_scheduled_config_save(true)
	else
		save_config()
	end
end
module.reload_config = function()
	flush_scheduled_config_save(true)
	load_custom_vars()
	clear_parse_cache()
	pcall(load_external_vars)
end
module.reload_external_vars = function()
	pcall(load_external_vars)
end

-- автозагрузка внешних переменных из HelperByOrc/vars при старте
pcall(load_external_vars)

-- выгрузка: убрать маркер и сохранить конфиг
function module.onTerminate()
	for i = #_imguiSubs, 1, -1 do
		local sub = _imguiSubs[i]
		if sub and type(sub.Unsubscribe) == "function" then
			pcall(sub.Unsubscribe, sub)
		end
		_imguiSubs[i] = nil
	end
	if event_bus_ref then
		event_bus_ref.offByOwner("tags")
	end
	module._target_tracker_active = false
	if module._target_tracker_thread
		and type(module._target_tracker_thread.status) == "function"
		and type(module._target_tracker_thread.terminate) == "function"
	then
		local ok_status, status = pcall(module._target_tracker_thread.status, module._target_tracker_thread)
		if ok_status and status ~= "dead" then
			pcall(module._target_tracker_thread.terminate, module._target_tracker_thread)
		end
	end
	module._target_tracker_thread = nil
	module._target_tracker_started = false
	clear_target_blip()
	release_all_key_holds()
	flush_scheduled_config_save(true)
end

-- инициализация после загрузки SAMP
function module.onSampReady()
	-- фоновый поток слежения за таргетом
	local TARGET_TRACK_INTERVAL_MS = 25
	if not module._target_tracker_started then
		module._target_tracker_started = true
		module._target_tracker_active = true
		if lua_thread and lua_thread.create then
			local ok, thread_or_err = pcall(lua_thread.create, function()
				while module._target_tracker_active do
					pcall(read_target_once)
					pcall(update_target_blip)
					pcall(process_active_key_holds)
					wait(TARGET_TRACK_INTERVAL_MS)
				end
			end)
			if ok then
				module._target_tracker_thread = thread_or_err
			else
				module._target_tracker_started = false
				module._target_tracker_active = false
				log_chat(L("tags.text.tags") .. tostring(thread_or_err), 0xAA3333)
			end
		else
			log_chat(
				L("tags.text.tags_lua_thread_create"),
				0xAA8800
			)
		end
	end
end

return module
