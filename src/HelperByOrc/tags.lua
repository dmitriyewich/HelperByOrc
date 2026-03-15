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
	myorg = "СМИ ЛС",
	myorgrang = "Ведущий",
}

-- состояние таргета
local target = {
	current_ped = nil,
	current_id = nil,
	last_id = nil,
	_notice_id = nil,
	_blip_ped = nil, -- ped, для которого сейчас стоит маркер
}

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
							event_bus_ref.emit("toast", ("[Tags] Выбран target id: %d"):format(id), "ok", 2.5)
						else
							toasts_module.push(("[Tags] Выбран target id: %d"):format(id), "ok", 2.5)
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
		log_chat("[Tags] [bind...] недоступен: модуль binder не подключён", 0xAA3333)
		return opts.default or ""
	end
	if type(binder.executeBindTagAction) ~= "function" then
		log_chat("[Tags] [bind...] недоступен: executeBindTagAction не найден", 0xAA3333)
		return opts.default or ""
	end

	local ok_call, ok_action, result, err =
		pcall(binder.executeBindTagAction, action, tostring(param or ""), thisbind_value)
	if not ok_call then
		log_chat("[Tags] Ошибка в [bind...]: " .. tostring(ok_action), 0xAA3333)
		return opts.default or ""
	end
	if not ok_action then
		if not opts.silent_fail then
			log_chat(("[Tags] [%s(...)]: %s"):format(tostring(action), tostring(err or "ошибка")), 0xAA3333)
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
			log_chat("[Tags] [chatwords(...)] недоступен: sampGetChatString не найден", 0xAA3333)
		elseif err == "read_fail" then
			log_chat("[Tags] [chatwords(...)] не удалось прочитать строку чата", 0xAA3333)
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
		log_chat("[Tags] Ошибка в [chatwordsex(...)]: " .. tostring(captures), 0xAA3333)
		return ""
	end
	if type(captures) ~= "table" or #captures == 0 then
		return ""
	end
	return tostring(captures[idx] or "")
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
			log_chat("[Tags] [math(...)] пустой параметр", 0xAA3333)
			return ""
		end

		local chunk, err = safe_load_expr(expr)
		if not chunk then
			log_chat("[Tags] Ошибка в [math(...)]: " .. tostring(err), 0xAA3333)
			return ""
		end

		local ok, res = pcall(chunk)
		if not ok then
			log_chat("[Tags] Ошибка в [math(...)]: " .. tostring(res), 0xAA3333)
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
			log_chat("[Tags] Ошибка в [ifandor(...)]: ожидается формат condition?true:false", 0xAA3333)
			return ""
		end

		local cond_expr = module.change_tags(cond_raw, thisbind_value, depth)
		local chunk, err = safe_load_expr(cond_expr)
		local cond_ok = false
		if not chunk then
			log_chat("[Tags] Ошибка в [ifandor(...)]: " .. tostring(err), 0xAA3333)
		else
			local ok, res = pcall(chunk)
			if not ok then
				log_chat("[Tags] Ошибка в [ifandor(...)]: " .. tostring(res), 0xAA3333)
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
			log_chat("[Tags] [call(...)] отклонён: небезопасный режим выключен", 0xAA3333)
			return ""
		end
		local expr = tostring(param or "")
		local chunk, err = safe_load_expr(expr)
		if not chunk then
			log_chat("[Tags] Ошибка в [call(...)]: " .. tostring(err), 0xAA3333)
			return ""
		end
		if lua_thread and lua_thread.create then
			lua_thread.create(function()
				local ok_exec, exec_err = pcall(chunk)
				if not ok_exec then
					log_chat("[Tags] Ошибка в [call(...)]: " .. tostring(exec_err), 0xAA3333)
				end
			end)
		else
			local ok_exec, exec_err = pcall(chunk)
			if not ok_exec then
				log_chat("[Tags] Ошибка в [call(...)]: " .. tostring(exec_err), 0xAA3333)
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
			local chunk, err = safe_load_expr(expr)
			if not chunk then
				log_chat("[Tags] Ошибка в [wait(...)]: " .. tostring(err), 0xAA3333)
				return ""
			end
			local ok, res = pcall(chunk)
			if not ok then
				log_chat("[Tags] Ошибка в [wait(...)]: " .. tostring(res), 0xAA3333)
				return ""
			end
			ms = tonumber(res)
		end
		if not ms then
			log_chat("[Tags] [wait(...)] ожидает число миллисекунд", 0xAA3333)
			return ""
		end
		if not yield_or_wait(ms) then
			log_chat("[Tags] [wait(...)] не может быть выполнен в этом контексте", 0xAA3333)
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
			local chunk, err = safe_load_expr(expr)
			if not chunk then
				if not had_compile_error then
					log_chat("[Tags] Ошибка в [waitif(...)]: " .. tostring(err), 0xAA3333)
					had_compile_error = true
				end
				if not yield_or_wait(50) then
					log_chat("[Tags] [waitif(...)] не может быть выполнен в этом контексте", 0xAA3333)
					break
				end
			else
				had_compile_error = false
				local ok, res = pcall(chunk)
				if ok and res then
					break
				end
				if not ok then
					log_chat("[Tags] Ошибка в [waitif(...)]: " .. tostring(res), 0xAA3333)
					break
				end
				if not yield_or_wait(50) then
					log_chat("[Tags] [waitif(...)] не может быть выполнен в этом контексте", 0xAA3333)
					break
				end
			end
		end
		if os.clock() >= deadline then
			log_chat("[Tags] [waitif(...)] прервано по таймауту (10 сек)", 0xAA3333)
		end
		return ""
	end,

	-- получить элемент текста открытого диалога по индексу (0-based)
	dialogtext = function(param)
		if not samp then
			log_chat("[Tags] [dialogtext(...)] недоступен: модуль samp не подключён", 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat("[Tags] [dialogtext(...)] пустой параметр", 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local idx = tonumber(raw)
		if idx == nil then
			log_chat("[Tags] [dialogtext(...)] ожидается номер текста", 0xAA3333)
			return ""
		end
		idx = math.floor(idx)
		if idx < 0 then
			log_chat("[Tags] [dialogtext(...)] индекс должен быть >= 0", 0xAA3333)
			return ""
		end

		local text, err, total = get_dialogtext_item_by_index(idx)
		if text ~= nil then
			return text
		end

		if err == "no_dialog" then
			return ""
		elseif err == "no_reader" then
			log_chat("[Tags] [dialogtext(...)] недоступен: sampGetDialogText не найден", 0xAA3333)
		elseif err == "read_fail" then
			log_chat("[Tags] [dialogtext(...)] не удалось прочитать текст диалога", 0xAA3333)
		elseif err == "out_of_range" then
			log_chat(
				("[Tags] [dialogtext(...)] индекс вне диапазона: %d (элементов: %d)"):format(
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

	-- открыть пункт активного диалога по имени/номеру
	dialogitem = function(param)
		if not samp then
			log_chat("[Tags] [dialogitem(...)] недоступен: модуль samp не подключён", 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat("[Tags] [dialogitem(...)] нет активного диалога", 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat("[Tags] [dialogitem(...)] пустой параметр", 0xAA3333)
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
			log_chat("[Tags] [dialogitem(...)] пункт не найден: " .. tostring(arg), 0xAA3333)
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
				("[Tags] [dialogitem(...)] неверный номер пункта: %s"):format(tostring(idx + 1)),
				0xAA3333
			)
			return ""
		end

		local selected = false
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
			log_chat("[Tags] [dialogitem(...)] не удалось выбрать пункт", 0xAA3333)
			return ""
		end

		local opened = false
		if samp.CDialog_Close_func then
			local ok_close = pcall(samp.CDialog_Close_func, 1)
			opened = ok_close
		end
		if not opened and type(sampSendDialogResponse) == "function" then
			local did = samp.SAMP_DIALOG_ID and samp.SAMP_DIALOG_ID() or nil
			if did then
				local ok_resp = pcall(sampSendDialogResponse, did, 1, idx, "")
				opened = ok_resp
			end
		end
		if not opened then
			log_chat("[Tags] [dialogitem(...)] не удалось открыть выбранный пункт", 0xAA3333)
		end
		return ""
	end,

	-- закрыть активный диалог (1 = Enter/ОК, 0 = Esc/Cancel)
	dialogclose = function(param)
		if not samp then
			log_chat("[Tags] [dialogclose(...)] недоступен: модуль samp не подключён", 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat("[Tags] [dialogclose(...)] нет активного диалога", 0xAA3333)
			return ""
		end
		if not samp.CDialog_Close_func then
			log_chat("[Tags] [dialogclose(...)] недоступен: CDialog_Close_func не найден", 0xAA3333)
			return ""
		end

		local raw = tostring(param or ""):match("^%s*(.-)%s*$")
		if raw == "" then
			log_chat("[Tags] [dialogclose(...)] ожидается 0 или 1", 0xAA3333)
			return ""
		end
		raw = raw:match('^"(.*)"$') or raw:match("^'(.*)'$") or raw

		local button = tonumber(raw)
		if button == nil then
			log_chat("[Tags] [dialogclose(...)] ожидается 0 или 1", 0xAA3333)
			return ""
		end
		button = math.floor(button)
		if button ~= 0 and button ~= 1 then
			log_chat("[Tags] [dialogclose(...)] допустимы только 0 или 1", 0xAA3333)
			return ""
		end

		local ok_close, err = pcall(samp.CDialog_Close_func, button)
		if not ok_close then
			log_chat("[Tags] Ошибка в [dialogclose(...)]: " .. tostring(err), 0xAA3333)
		end
		return ""
	end,

	-- установить текст в editbox активного диалога
	dialogsettext = function(param)
		if not samp then
			log_chat("[Tags] [dialogsettext(...)] недоступен: модуль samp не подключён", 0xAA3333)
			return ""
		end
		if not (samp.isDialogActive and samp.isDialogActive()) then
			log_chat("[Tags] [dialogsettext(...)] нет активного диалога", 0xAA3333)
			return ""
		end
		if not samp.sampSetDialogEditboxText then
			log_chat("[Tags] [dialogsettext(...)] недоступен: sampSetDialogEditboxText не найден", 0xAA3333)
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
			log_chat("[Tags] Ошибка в [dialogsettext(...)]: " .. tostring(err), 0xAA3333)
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
		return string.format("[Скриншот: %s]", name or os.date("%d.%m.%Y %H.%M.%S"))
	end,
}


-- описания мульти-тегов (для справки)
local multi_tags_descriptions = {
	nickid = { desc = "Ник игрока по ID (листабельно)", example = "[nickid(1 2 3)]" },
	nickru = { desc = "Русский ник по ID (листабельно)", example = '[nickru(1,2,3 | ", ")]' },
	rpnick = { desc = "РП-ник по ID (листабельно)", example = "[rpnick(4 5 6)]" },
	name = { desc = "Имя по ID (листабельно)", example = "[name(1 2 3)]" },
	nameru = { desc = "Имя (рус) по ID (листабельно)", example = '[nameru(1 2 3 | " / ")]' },
	surname = { desc = "Фамилия по ID (листабельно)", example = "[surname(1, 2, 3)]" },
	surnameru = { desc = "Фамилия (рус) по ID (листабельно)", example = "[surnameru(1 2 3)]" },
	getvehtype = { desc = "Тип транспорта игрока по ID (листабельно)", example = "[getvehtype(5)]" },
	strlow = { desc = "Строка в нижнем регистре", example = "[strlow(ТЕКСТ)]" },
	math = { desc = "Вычислить математическое выражение", example = "[math(2+2)]" },
	ifandor = {
		desc = "Вернуть один из двух вариантов по условию: condition ? true_value : false_value",
		example = '[ifandor({id}==148?Мой ид 148:Мой ид не 148)]',
	},
	call = {
		desc = "Выполнить Lua-выражение/код без вставки текста",
		example = "[call(module.save_config())]",
	},
	wait = {
		desc = "Пауза выполнения бинда на указанное число миллисекунд",
		example = "[wait(500)]",
	},
	waitif = {
		desc = "Пауза выполнения бинда, пока условие не станет истинным",
		example = "[waitif(isKeyDown(0x0D))]",
	},
	dialogitem = {
		desc = "Открыть пункт активного диалога по названию или номеру",
		example = "[dialogitem(Помощь)]",
	},
	dialogclose = {
		desc = "Закрыть активный диалог: 1 = положительный ответ (Enter), 0 = отрицательный (Esc)",
		example = "[dialogclose(1)]",
	},
	dialogtext = {
		desc = "Вернуть элемент текста открытого диалога по индексу (0-based). Для выбора индекса нажми + рядом с тегом.",
		example = "[dialogtext(69)]",
	},
	dialogsettext = {
		desc = "Установить текст в editbox активного диалога",
		example = '[dialogsettext("Текст")]',
	},
	chatwords = {
		desc = "Get text from chat line by selector: N, N+, N-, A-B, line;selector",
		example = "[chatwords(98;4+)]",
	},
	chatwordsex = {
		desc = "Get capture from Lua pattern of current chat trigger message",
		example = "[chatwordsex(1)]",
	},
	binddisable = {
		desc = "Выключить (деактивировать) бинд",
		example = '[binddisable("Флудер")]',
	},
	bindenable = {
		desc = "Включить деактивированный бинд",
		example = '[bindenable("Флудер")]',
	},
	bindstart = {
		desc = "Запустить бинд",
		example = "[bindstart({thisbind})]",
	},
	bindstop = {
		desc = "Остановить запущенный бинд",
		example = "[bindstop({thisbind})]",
	},
	bindpause = {
		desc = "Поставить бинд на паузу",
		example = '[bindpause("Лекция" "Обучение")]',
	},
	bindunpause = {
		desc = "Снять бинд с паузы",
		example = '[bindunpause("Лекция" "Обучение")]',
	},
	bindfastmenu = {
		desc = "Показывать бинд в быстром меню",
		example = '[bindfastmenu("Лекция" "Обучение")]',
	},
	bindunfastmenu = {
		desc = "Не показывать бинд в быстром меню",
		example = '[bindunfastmenu("Лекция" "Обучение")]',
	},
	bindrandom = {
		desc = "Запустить случайный бинд из папки",
		example = '[bindrandom("Обучение")]',
	},
	bindended = {
		desc = "Проверка завершения бинда: вернёт 1 или 0",
		example = "[bindended({thisbind})]",
	},
	bindstopall = {
		desc = "Остановить все запущенные бинды",
		example = "[bindstopall()]",
	},
	bindpopup = {
		desc = "Открыть попап со списком строк бинда для быстрой отправки",
		example = "[bindpopup({thisbind})]",
	},
	addtime = { desc = "Текущее время + мин:сек", example = '[addtime("10:10")]' },
	screen = {
		desc = "Сделать скриншот. Аргументы опциональны.",
		example = '[screen("имя_файла", "папка")]',
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
			log_chat(("[Tags] Ошибка в [%s(...)]: %s"):format(name, tostring(res)), 0xAA3333)
			return "[Ошибка " .. name .. "]"
		end
		return res
	end
	multi_tags_descriptions[name] = {
		desc = desc or ("Внешняя функциональная переменная '" .. name .. "'"),
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
			log_chat(("[Tags] Не удалось загрузить '%s': %s"):format(path, tostring(err)), 0xAA3333)
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
					("[Tags] Ошибка при выполнении '%s': %s"):format(path, tostring(perr)),
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
	{ name = "{id}", desc = "Ваш ID на сервере" },
	{ name = "{nick}", desc = "Ваш ник (с тегом)" },
	{ name = "{nickru}", desc = "Ваш ник (русскими буквами, без тега)" },
	{ name = "{rpnick}", desc = "Ник для РП-формата" },
	{ name = "{name}", desc = "Имя до подчёркивания" },
	{ name = "{nameru}", desc = "Имя (русскими буквами)" },
	{ name = "{surname}", desc = "Фамилия (после подчёркивания)" },
	{ name = "{surnameru}", desc = "Фамилия (русскими буквами)" },
	{ name = "{myskin}", desc = "Ваш ID скина" },
	{ name = "{city}", desc = "Ваш город (по зоне GTA)" },
	{ name = "{date}", desc = "Текущая дата (ДД.ММ.ГГГГ)" },
	{ name = "{time}", desc = "Текущее время (ЧЧ:ММ:СС)" },
	{ name = "{timenosec}", desc = "Время (без секунд)" },
	{ name = "{myorg}", desc = "Ваша организация (можно изменить)" },
	{ name = "{myorgrang}", desc = "Ваш ранг в организации (можно изменить)" },
	{
		name = "{screen}",
		desc = "Сделать скриншот. По умолчанию в стандартную папку с текущей датой",
	},

	-- переменные по таргету (последний валидный ID)
	{ name = "{targetid}", desc = "ID игрока, в которого вы целились последним" },
	{ name = "{targetnick}", desc = "Ник игрока последней цели (как в SAMP)" },
	{ name = "{targetrpnick}", desc = "Ник последней цели в RP-формате" },
	{ name = "{targetname}", desc = "Имя последней цели" },
	{ name = "{targetsurname}", desc = "Фамилия последней цели" },
	{ name = "{thisbind}", desc = 'Имя и папка текущего выполняемого бинда в формате: "имя" "папка"' },
	{ name = "{dialogactive}", desc = "Есть ли активный диалог (true/false)" },
	{ name = "{dialogcaption}", desc = "Заголовок активного диалога" },
	{ name = "{dialoggetselecteditem}", desc = "Текст выбранного пункта активного диалога (listbox)" },
	{ name = "{clipboard}", desc = "Текст из буфера обмена" },
	{ name = "{mymoney}", desc = "Количество ваших денег" },
	{ name = "{getvehtype}", desc = "Тип транспорта, в котором вы находитесь" },
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
				return "[Скриншот сделан]"
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

-- ========== ПАРСЕР МУЛЬТИ-ТЕГОВ ==========
local RECURSION_LIMIT = 10

local function handle_multi_tag(tag, val, thisbind_value, depth)
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then
		return "[Ошибка: слишком глубокая вложенность]"
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
			res = "[Ошибка парсинга тега: " .. tag .. "]"
		end
	else
		res = "[Неизвестный тег: " .. tag .. "]"
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
		return "[Ошибка: слишком глубокая вложенность]"
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

local function make_safe_env()
	local env = {
		tonumber = tonumber,
		tostring = tostring,
		type = type,
		pairs = pairs,
		ipairs = ipairs,
		select = select,
		unpack = unpack or table.unpack,
		math = math,
		string = string,
		table = table,
		module = module,
		time = os.time,
		clock = os.clock,
		target_last_id = function()
			return target.last_id
		end,
	}
	return setmetatable(env, { __index = _G })
end

safe_load_expr = function(expr)
	local chunk, err = load("return (" .. expr .. ")")
	if not chunk then
		chunk, err = load(expr)
		if not chunk then
			return nil, err
		end
	end
	setfenv(chunk, make_safe_env())
	return chunk
end

-- ========== ОСНОВНАЯ ФУНКЦИЯ ПОДСТАНОВКИ ==========
function module.change_tags(text, thisbind_value, depth)
	flush_scheduled_config_save(false)
	begin_parse_scope()
	text = text or ""
	text = parse_multi_tags(text, thisbind_value, depth)
	text = text:gsub("{[%w_]+}", function(key)
		local fn = tags[key]
		if fn then
			local cache_key = key
			local c = cache_get(cache_key)
			if c ~= nil then
				return c
			end
			local ok, res = pcall(fn, thisbind_value)
			local out = (ok and res and tostring(res) ~= key) and tostring(res) or ""
			cache_set(cache_key, out)
			return out
		end
		return key
	end)
	if text:match("^%s*$") then
		text = ""
	end
	return text
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
			table.insert(out, { name = tagname, desc = v.desc or "(доп. переменная)", custom = false })
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
	flash_copied("Скопировано: " .. str)
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

local DIALOGTEXT_POPUP_SETTINGS = "Выбери нужное слово##dialogtext_picker_settings"
local DIALOGTEXT_POPUP_HELP = "Выбери нужное слово##dialogtext_picker_help"
local DIALOGTEXT_PICKER_DELAY_SEC = 0.05

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
			dialogtext_picker_state.error = "Модуль samp не подключён."
		elseif err == "no_dialog" then
			dialogtext_picker_state.error = "Нет активного диалога."
		elseif err == "no_reader" then
			dialogtext_picker_state.error = "Функция sampGetDialogText недоступна."
		else
			dialogtext_picker_state.error = "Не удалось прочитать текст диалога."
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
		dialogtext_picker_state.error = "В тексте диалога не найдено элементов."
		return false
	end
	return true
end

local function request_dialogtext_picker_open(popup_id)
	if not samp then
		log_chat("[Tags] [dialogtext(...)] недоступен: модуль samp не подключён", 0xAA3333)
		return
	end
	if not (samp.isDialogActive and samp.isDialogActive()) then
		log_chat("[Tags] [dialogtext(...)] нет активного диалога", 0xAA3333)
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
				CopyFlash(("[dialogtext(%d)]"):format(item.index))
			end
			HelpTip(("Элемент #%d\nКлик: скопировать [dialogtext(%d)]"):format(item.index, item.index))
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
		imgui.TextUnformatted("Выбери нужное слово")
		if dialogtext_picker_state.caption ~= "" then
			imgui.TextUnformatted("Диалог: " .. dialogtext_picker_state.caption)
		end
		if imgui.Button("Обновить") then
			refresh_dialogtext_picker_data()
		end
		imgui.SameLine()
		if imgui.Button("Закрыть") then
			imgui.CloseCurrentPopup()
		end
		imgui.Separator()

		if dialogtext_picker_state.error then
			imgui.TextUnformatted(dialogtext_picker_state.error)
		else
			imgui.TextUnformatted(("Элементов: %d"):format(dialogtext_picker_state.token_count))
			draw_dialogtext_picker_tokens("dialogtext_picker_tokens##" .. popup_id)
		end

		imgui.EndPopup()
	end
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
		"Переменные и теги — удобно и без лишнего шума"
	)
	imgui.Separator()

	if imgui.BeginTabBar("tags_tabbar") then
		-- === ВКЛАДКА: Основное ===
		if imgui.BeginTabItem("Основное") then
			imgui.Text("Быстрые настройки")
			imgui.BeginChild("main_opts", imgui.ImVec2(0, 175), true)

			-- Показ уведомления о target
			do
				local v = ffi.new("bool[1]", settings.show_target_notice and true or false)
				if imgui.Checkbox("Показывать уведомление о {targetid}", v) then
					settings.show_target_notice = v[0] and true or false
					save_config()
				end
				imgui.SameLine()
				Badge("{targetid}")
				HelpTip(
					"При смене цели выводится всплывашка с ID. Удобно, если часто используешь теги по цели."
				)
			end

			-- Разрешить [call]
			do
				local v = ffi.new("bool[1]", settings.allow_unsafe and true or false)
				if
					imgui.Checkbox("Разрешить [call] (только если понимаешь риски)", v)
				then
					settings.allow_unsafe = v[0] and true or false
					save_config()
				end
				HelpTip(
					"Выполнение Lua-выражений из строки. Включай только для своих шаблонов. На чужие не ставь."
				)
			end

			-- Маркер над педом при выборе цели
			do
				local v = ffi.new("bool[1]", settings.show_target_blip and true or false)
				if imgui.Checkbox("Показывать маркер над педом при выборе цели", v) then
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
					"Когда включено — над выбранным игроком в прицеле появляется зелёный маркер. Маркер снимается при потере цели или выключении опции."
				)
			end

			imgui.EndChild()

			-- Управление конфигом
			imgui.Text("Конфиг")
			imgui.BeginChild("cfg_ops", imgui.ImVec2(0, 70), true)
			if imgui.Button(" Сохранить сейчас") then
				save_config()
				flash_copied("Конфиг сохранён")
			end
			imgui.SameLine()
			if imgui.Button(" Перечитать из файла") then
				module.reload_config()
				flash_copied("Конфиг перечитан")
			end
			imgui.SameLine()
			if imgui.Button(" Сброс к дефолту") then
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
				flash_copied("Сброшено к дефолту")
			end
			imgui.EndChild()

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Переменные ===
		if imgui.BeginTabItem("Переменные") then
			imgui.Text("Кастомные переменные")
			imgui.BeginChild("vars_child", imgui.ImVec2(0, -140), true)

			-- Поиск
			imgui.SetNextItemWidth(240)
			imgui.InputText(" Поиск по имени/значению", filter_vars, ffi.sizeof(filter_vars))
			local fstr = ffi.string(filter_vars):lower()

			imgui.Separator()
			imgui.Columns(3, "vars_cols", false)
			imgui.Text("Имя")
			imgui.NextColumn()
			imgui.Text("Значение")
			imgui.NextColumn()
			imgui.Text("Действия")
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
					if imgui.SmallButton(" Коп.") then
						CopyFlash("{" .. name .. "}")
					end
					imgui.SameLine()
					if imgui.SmallButton(" Переим.") then
						edit_key = name
						rename_popup_seeded = false
					end
					imgui.SameLine()
					if imgui.SmallButton(" Удал.") then
						del_key = name
					end
					imgui.PopID()
					imgui.NextColumn()
				end
			end
			imgui.Columns(1)
			imgui.EndChild()

			-- Добавить новую
			imgui.Text("Добавить переменную")
			imgui.BeginChild("add_var", imgui.ImVec2(0, 70), true)
			imgui.SetNextItemWidth(180)
			imgui.InputText("Имя", new_var_name, ffi.sizeof(new_var_name))
			imgui.SameLine()
			imgui.SetNextItemWidth(340)
			imgui.InputText("Значение", new_var_value, ffi.sizeof(new_var_value))
			imgui.SameLine()
			if imgui.Button(" Добавить") then
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
				imgui.OpenPopup("Переименование переменной")
			end
			if
				imgui.BeginPopupModal(
					"Переименование переменной",
					nil,
					imgui.WindowFlags.AlwaysAutoResize
				)
			then
				imgui_text_safe("Старое имя: {" .. tostring(edit_key) .. "}")
				imgui.InputText("Новое имя", rename_var_name, ffi.sizeof(rename_var_name))
				if imgui.Button("OK##rename") then
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
				if imgui.Button("Отмена##rename") then
					edit_key = nil
					rename_popup_seeded = false
					imgui.CloseCurrentPopup()
				end
				imgui.EndPopup()
			end

			-- Попап: удаление
			if del_key then
				imgui.OpenPopup("Удалить переменную?")
			end
			if
				imgui.BeginPopupModal("Удалить переменную?", nil, imgui.WindowFlags.AlwaysAutoResize)
			then
				imgui_text_safe("Точно удалить {" .. tostring(del_key) .. "}?")
				if imgui.Button("Да##del") then
					custom_vars[del_key] = nil
					cvar_bufs[del_key] = nil
					save_config()
					clear_parse_cache()
					del_key = nil
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button("Нет##del") then
					del_key = nil
					imgui.CloseCurrentPopup()
				end
				imgui.EndPopup()
			end

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Теги и функции ===
		if imgui.BeginTabItem("Теги и функции") then
			imgui.Columns(2, "tf_cols", true)

			-- Переменные (встроенные+внешние)
			imgui.Text("Переменные")
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
			imgui.Text("Функции-теги")
			imgui.SetNextItemWidth(240)
			imgui.InputText(" Поиск по функциям", filter_funcs, ffi.sizeof(filter_funcs))
			local ff = ffi.string(filter_funcs):lower()

			imgui.BeginChild("funcs_list", imgui.ImVec2(0, -30), true)
			for i, tag in ipairs(get_func_list()) do
				local name = tag.name or ""
				local desc = tag.desc or ""
				local example = tag.example or name
				if ff == "" or name:lower():find(ff, 1, true) or desc:lower():find(ff, 1, true) then
					imgui.PushIDStr("f" .. tostring(i))
					if tag.tag == "dialogtext" then
						if imgui.SmallButton("+") then
							request_dialogtext_picker_open(DIALOGTEXT_POPUP_SETTINGS)
						end
						HelpTip("Открыть модальное окно выбора элемента из текста открытого диалога.")
						imgui.SameLine()
					end
					if imgui.Selectable(name, false) then
						CopyFlash(example)
					end
					HelpTip((desc ~= "" and (desc .. "\nПример: " .. example)) or ("Пример: " .. example))
					imgui.PopID()
				end
			end
			imgui.EndChild()
			draw_dialogtext_picker_popup(DIALOGTEXT_POPUP_SETTINGS)

			imgui.Columns(1)

			imgui.Separator()
			if imgui.Button("Открыть отдельное окно справки") then
				module.showTagsWindow[0] = true
			end
			imgui.SameLine()
			Badge("Клик по пункту — копирование")

			imgui.EndTabItem()
		end

		-- === ВКЛАДКА: Импорт/экспорт ===
		if imgui.BeginTabItem("Импорт/экспорт") then
			imgui.TextWrapped(
				"Экспортирует/импортирует только пользовательские переменные и настройки."
			)
			imgui.BeginChild("io_box", imgui.ImVec2(0, 100), true)
			if imgui.Button(" Экспорт в tags.json") then
				save_config()
				flash_copied("Экспортировано в " .. CONFIG_PATH_REL)
			end
			imgui.SameLine()
			if imgui.Button(" Импорт из tags.json") then
				module.reload_config()
				flash_copied("Импортировано из " .. CONFIG_PATH_REL)
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
	imgui.Begin("Справка по тегам / HelperByOrc", showTagsWindow, imgui.WindowFlags.NoCollapse)

	imgui.TextColored(
		imgui.ImVec4(0.75, 1, 1, 1),
		"Подставляй теги — получай готовый текст"
	)
	imgui.Separator()

	imgui.Columns(2, "help_cols", true)

	-- Переменные
	imgui.Text("Переменные")
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
	imgui.Text("Функции-теги")
	imgui.BeginChild("help_funcs", imgui.ImVec2(0, -30), true)
	for i, tag in ipairs(get_func_list()) do
		imgui.PushIDStr("hf" .. tostring(i))
		local copy = tag.example or tag.name
		if tag.tag == "dialogtext" then
			if imgui.SmallButton("+") then
				request_dialogtext_picker_open(DIALOGTEXT_POPUP_HELP)
			end
			HelpTip("Открыть модальное окно выбора элемента из текста открытого диалога.")
			imgui.SameLine()
		end
		if imgui.Selectable(tag.name, false) then
			CopyFlash(copy)
		end
		HelpTip((tag.desc or "") .. (copy and ("\nПример: " .. copy) or ""))
		imgui.PopID()
	end
	imgui.EndChild()
	draw_dialogtext_picker_popup(DIALOGTEXT_POPUP_HELP)

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
	if imgui.Button("Закрыть") then
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
					wait(TARGET_TRACK_INTERVAL_MS)
				end
			end)
			if ok then
				module._target_tracker_thread = thread_or_err
			else
				module._target_tracker_started = false
				module._target_tracker_active = false
				log_chat("[Tags] Ошибка запуска трекинга цели: " .. tostring(thread_or_err), 0xAA3333)
			end
		else
			log_chat(
				"[Tags] Предупреждение: lua_thread.create недоступен, трекинг цели отключён",
				0xAA8800
			)
		end
	end
end

return module
