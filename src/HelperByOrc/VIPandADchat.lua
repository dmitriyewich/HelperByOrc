local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local module = {}

-- ===================== ЗАВИСИМОСТИ =====================
local imgui = require("mimgui")
local ffi = require("ffi")
local paths = require("HelperByOrc.paths")
local mimgui_funcs

local bit = require("bit")

local ok_fa, fa = pcall(require, "fAwesome7")
if not ok_fa or type(fa) ~= "table" then
	fa = setmetatable({}, { __index = function() return "" end })
end

local function bor(...)
	local v = 0
	for _, a in ipairs({ ... }) do
		v = bit.bor(v, a)
	end
	return v
end

local funcs
local start_save_worker
local clone_table
local utf8_len

local samp
local config_manager_ref
local event_bus_ref

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	samp = mod.samp or samp
	clone_table = funcs.deepcopy
	utf8_len = funcs.utf8_len
end

syncDependencies()

local function merge_defaults(dst, defaults)
	if type(dst) ~= "table" or type(defaults) ~= "table" then
		return
	end
	for k, v in pairs(defaults) do
		if dst[k] == nil then
			dst[k] = clone_table(v)
		elseif type(v) == "table" and type(dst[k]) == "table" then
			merge_defaults(dst[k], v)
		end
	end
end

local function sanitize_json_string(s)
	s = tostring(s or "")
	if s == "" then
		return s
	end
	s = s:gsub("\r\n", "\n")
	s = s:gsub("\r", "\n")
	s = s:gsub("\\9", "    ")
	s = s:gsub("\t", "    ")
	s = s:gsub("[%z\1-\8\11\12\14-\31]", "")
	return s
end

local function sanitize_table_strings_in_place(tbl, seen)
	if type(tbl) ~= "table" then
		return
	end
	seen = seen or {}
	if seen[tbl] then
		return
	end
	seen[tbl] = true
	for k, v in pairs(tbl) do
		if type(v) == "string" then
			tbl[k] = sanitize_json_string(v)
		elseif type(v) == "table" then
			sanitize_table_strings_in_place(v, seen)
		end
	end
end

local function normalize_string_list(value, fallback)
	if type(value) ~= "table" then
		return clone_table(fallback or {})
	end
	local out = {}
	for i = 1, #value do
		local item = sanitize_json_string(value[i] or "")
		if item ~= "" then
			out[#out + 1] = item
		end
	end
	return out
end

local function serialize_string_list(list)
	if type(list) ~= "table" then
		return ""
	end
	local out = {}
	for i = 1, #list do
		local item = tostring(list[i] or "")
		if item ~= "" then
			out[#out + 1] = item
		end
	end
	return table.concat(out, "\n")
end

local function parse_string_list(raw)
	raw = tostring(raw or "")
	local out = {}
	for line in raw:gmatch("[^\r\n]+") do
		local trimmed = line:match("^%s*(.-)%s*$")
		if trimmed ~= "" then
			out[#out + 1] = trimmed
		end
	end
	return out
end

local function deep_copy_sanitized(value, seen)
	local tv = type(value)
	if tv == "string" then
		return sanitize_json_string(value)
	end
	if tv ~= "table" then
		return value
	end
	seen = seen or {}
	if seen[value] then
		return seen[value]
	end
	-- CircBuf: сериализовать как плоский массив (duck-type: наличие to_array)
	if type(rawget(value, '_d')) == "table" and type(rawget(value, '_n')) == "number" then
		local arr = value:to_array()
		local out = {}
		seen[value] = out
		for i = 1, #arr do
			out[i] = deep_copy_sanitized(arr[i], seen)
		end
		return out
	end
	local out = {}
	seen[value] = out
	for k, v in pairs(value) do
		out[k] = deep_copy_sanitized(v, seen)
	end
	return out
end

local function repair_invalid_json_escapes(raw)
	if type(raw) ~= "string" or raw == "" then
		return raw
	end
	local repaired = raw
	repaired = repaired:gsub("\\9", "\\t")
	repaired = repaired:gsub('\\([^"\\/bfnrtu])', '\\\\%1')
	return repaired
end

local function try_decode_json_table(raw)
	if type(raw) ~= "string" or raw == "" then
		return nil
	end
	local decode = rawget(_G, "decodeJson")
	if type(decode) == "function" then
		local ok, parsed = pcall(decode, raw)
		if ok and type(parsed) == "table" then
			return parsed
		end
	end
	return nil
end

local function load_table_with_json_repair(path)
	if type(path) ~= "string" or path == "" then
		return nil, false
	end
	path = funcs.resolveJsonPath(path)
	if not doesFileExist(path) then
		return nil, false
	end
	local f = io.open(path, "rb")
	if not f then
		return nil, false
	end
	local raw = f:read("*a")
	f:close()
	if type(raw) ~= "string" or raw == "" then
		return nil, false
	end
	local parsed = try_decode_json_table(raw)
	if parsed then
		return parsed, false
	end
	local repaired = repair_invalid_json_escapes(raw)
	if repaired == raw then
		return nil, false
	end
	local parsed_repaired = try_decode_json_table(repaired)
	if parsed_repaired then
		return parsed_repaired, true
	end
	return nil, false
end

-- ===================== КОЛЬЦЕВОЙ БУФЕР =====================
local CircBuf = {}
do
	local CB = {}

	function CircBuf.new(limit)
		local self = {}
		rawset(self, '_d', {})
		rawset(self, '_h', 0)
		rawset(self, '_n', 0)
		rawset(self, '_lim', limit or 200)
		return setmetatable(self, CB)
	end

	CB.__index = function(self, k)
		if type(k) == "number" then
			local n = rawget(self, '_n')
			if k < 1 or k > n then return nil end
			return rawget(self, '_d')[rawget(self, '_h') + k]
		end
		return CB[k]
	end

	CB.__newindex = function(self, k, v)
		if type(k) == "number" then
			local n = rawget(self, '_n')
			if k >= 1 and k <= n then
				rawget(self, '_d')[rawget(self, '_h') + k] = v
			end
			return
		end
		rawset(self, k, v)
	end

	function CB:len()
		return rawget(self, '_n')
	end

	function CB:push(item)
		local d = rawget(self, '_d')
		local h = rawget(self, '_h')
		local n = rawget(self, '_n') + 1
		local lim = rawget(self, '_lim')
		d[h + n] = item
		if n > lim then
			d[h + 1] = nil
			h = h + 1
			n = n - 1
		end
		if h > lim * 2 then
			local nd = {}
			for i = 1, n do nd[i] = d[h + i] end
			d = nd
			h = 0
			rawset(self, '_d', d)
		end
		rawset(self, '_h', h)
		rawset(self, '_n', n)
	end

	function CB:clear()
		rawset(self, '_d', {})
		rawset(self, '_h', 0)
		rawset(self, '_n', 0)
	end

	function CB:set_limit(lim)
		rawset(self, '_lim', lim or rawget(self, '_lim'))
		local d = rawget(self, '_d')
		local h = rawget(self, '_h')
		local n = rawget(self, '_n')
		while n > rawget(self, '_lim') do
			d[h + 1] = nil
			h = h + 1
			n = n - 1
		end
		rawset(self, '_h', h)
		rawset(self, '_n', n)
	end

	function CB:to_array()
		local d = rawget(self, '_d')
		local h = rawget(self, '_h')
		local n = rawget(self, '_n')
		local a = {}
		for i = 1, n do a[i] = d[h + i] end
		return a
	end

	function CB:from_array(arr)
		local lim = rawget(self, '_lim')
		rawset(self, '_d', {})
		rawset(self, '_h', 0)
		rawset(self, '_n', 0)
		if type(arr) == "table" then
			local start = math.max(1, #arr - lim + 1)
			local d = rawget(self, '_d')
			local n = 0
			for i = start, #arr do
				n = n + 1
				d[n] = arr[i]
			end
			rawset(self, '_n', n)
		end
	end

	CircBuf._mt = CB
end

local JSON_PATH_REL = "VIPandADchat.json"
local SAVE_DEBOUNCE_SEC = 0.35
local SAVE_FLUSH_POLL_MS = 250
local moduleInitialized = false

-- ===================== КОНФИГ =====================
local config = {}

local default_config = {
	enabled = true,
	vip_limit = 100,
	ad_limit = 100,
	all_limit = 200,
	highlightWords = { "Walcher_Flett", "Admin_John", "VIP_News" },
	timestamp = {
		enabled = true,
		scale = 0.5,
		offset_y = 0.0,
		padding = 0.0,
	},
	vip = {
		"%[VIP ADV%]",
		"%[VIP%]",
		"%[PREMIUM%]",
		"%[FOREVER%]",
		"%[SERVER%]",
		"%[ADMIN%]",
		L("vip_and_ad_chat.text.text"),
		L("vip_and_ad_chat.text.text_1"),
		"%[Family Car%]",
		L("vip_and_ad_chat.text.text_2"),
		L("vip_and_ad_chat.text.text_3"),
	},
	ad = {
		main = {
			"^Объявление:",
			"^{079C1C}Объявление:",
			"^%[VIP%] Объявление:.",
			"^{FCAA4D}%[VIP%] Объявление:.",
			"^{FCAA4D}%[Реклама Бизнеса%] Объявление:",
			"^%[Реклама Бизнеса%] Объявление:",
		},
		pre_edit = {
			"^Сообщение до редакции:",
		},
		edited = {
			"Отредактировал сотрудник СМИ %[",
		},
	},
	table_config = { vip_text = {}, ad_text = {}, all = {} },

	text_alpha_chat = 1.00,
	text_alpha_idle = 0.50,

	popup = {
		min_w = 320,
		max_w = 900,
		min_lines = 3,
		max_lines = 14,
		chars_per_line = 70,
	},
	chatbox = {
		enabled = true,
		pos_x = 30,
		pos_y = 600,
		width = 520,
		height = 210,
		bg_alpha = 0.35,
		rounding = 8,
	},
}

-- Кольцевые буферы для сообщений (заменяют config.table_config.vip_text/ad_text/all)
local vip_buf = CircBuf.new(default_config.vip_limit)
local ad_buf = CircBuf.new(default_config.ad_limit)
local all_buf = CircBuf.new(default_config.all_limit)

local function persist_main_enabled_state(state, force_flush)
	if not config_manager_ref then
		return false
	end
	local main_config = config_manager_ref.get("main")
	if type(main_config) ~= "table" then
		return false
	end
	if type(main_config.moduleStates) ~= "table" then
		main_config.moduleStates = {}
	end
	local normalized = not not state
	local changed = main_config.moduleStates.VIPandADchat ~= normalized
	main_config.moduleStates.VIPandADchat = normalized
	if changed then
		config_manager_ref.markDirty("main")
	end
	if force_flush and type(config_manager_ref.flush) == "function" then
		config_manager_ref.flush("main", true)
	end
	return true
end

function module.isEnabled()
	return config.enabled ~= false
end

function module.setEnabled(state, opts)
	opts = opts or {}
	config.enabled = not not state
	if opts.save_project ~= false then
		persist_main_enabled_state(config.enabled, opts.flush_project)
	end
	return config.enabled
end

-- ===================== УТИЛИТЫ =====================
local function strip_color_tags(str)
	return (tostring(str or ""):gsub("{[%xX]+}", ""))
end

local function clamp(v, lo, hi)
	if v < lo then
		return lo
	end
	if v > hi then
		return hi
	end
	return v
end

local function hex2rgba_vec4(hex, force_alpha)
	local r = tonumber(hex:sub(1, 2), 16) or 255
	local g = tonumber(hex:sub(3, 4), 16) or 255
	local b = tonumber(hex:sub(5, 6), 16) or 255
	local a = (#hex >= 8) and (tonumber(hex:sub(7, 8), 16) or 255) or 255
	if force_alpha ~= nil then
		a = clamp(math.floor(force_alpha * 255 + 0.5), 0, 255)
	end
	return imgui.ImVec4(r / 255, g / 255, b / 255, a / 255)
end

local function mul_alpha(col, alpha)
	return imgui.ImVec4(col.x, col.y, col.z, clamp(col.w * alpha, 0.0, 1.0))
end

local function text_size(s, font, fsize)
	return font:CalcTextSizeA(fsize, 10000, -1, s).x
end

local function measure_stripped(s, font, fsize)
	local clean = tostring(s or ""):gsub("{[%x]+}", "")
	return text_size(clean, font, fsize)
end

local function is_color_tag(tag)
	return type(tag) == "string" and (tag:match("^%{%x%x%x%x%x%x%}$") or tag:match("^%{%x%x%x%x%x%x%x%x%}$"))
end

local function line_height()
	return imgui.GetTextLineHeightWithSpacing()
end

local function strip_leading_timestamp(text)
	local s = tostring(text or "")
	if s == "" then
		return s
	end

	local i = 1
	local prefix_tags = ""
	while true do
		local tag = s:match("^%b{}", i)
		if tag and is_color_tag(tag) then
			prefix_tags = prefix_tags .. tag
			i = i + #tag
		else
			break
		end
	end

	local timestamp = s:match("^%[%d%d:%d%d:%d%d%]", i)
	if not timestamp then
		return s
	end

	i = i + #timestamp
	while i <= #s and s:sub(i, i):match("%s") do
		i = i + 1
	end

	return prefix_tags .. s:sub(i)
end

local function wrap_to_lines_keep_tags(text_with_tags, max_px)
	local lines = {}
	local cleaned = tostring(text_with_tags or "")
	local ts_cfg = config.timestamp or default_config.timestamp or {}
	local ts_enabled = ts_cfg.enabled ~= false
	if not ts_enabled then
		cleaned = strip_leading_timestamp(cleaned)
	end
	if cleaned == "" then
		lines[1] = ""
		return lines
	end

	local words = {}
	local current_tag = ""
	local i = 1
	while i <= #cleaned do
		while i <= #cleaned and cleaned:sub(i, i):match("%s") do
			i = i + 1
		end
		if i > #cleaned then
			break
		end

		local raw = ""
		local visible = ""
		local last_tag = nil
		while i <= #cleaned and not cleaned:sub(i, i):match("%s") do
			if cleaned:sub(i, i) == "{" then
				local tag = cleaned:match("^%b{}", i)
				if is_color_tag(tag) then
					raw = raw .. tag
					last_tag = tag
					i = i + #tag
				else
					local ch = cleaned:sub(i, i)
					raw = raw .. ch
					visible = visible .. ch
					i = i + 1
				end
			else
				local ch = cleaned:sub(i, i)
				raw = raw .. ch
				visible = visible .. ch
				i = i + 1
			end
		end
		words[#words + 1] = { raw = raw, visible = visible, last_tag = last_tag }
	end

	if #words == 0 then
		lines[1] = ""
		return lines
	end

	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local ts_scale = tonumber(ts_cfg.scale) or 0.5
	local ts_padding = tonumber(ts_cfg.padding) or 0
	local function split_long_word(word_raw, line_max_px)
		local parts = {}
		local chunk_raw = ""
		local chunk_visible = ""
		local active_tag = ""
		local i = 1
		while i <= #word_raw do
			local ch = word_raw:sub(i, i)
			if ch == "{" then
				local tag = word_raw:match("^%b{}", i)
				if is_color_tag(tag) then
					active_tag = tag
					chunk_raw = chunk_raw .. tag
					i = i + #tag
				else
					local next_visible = chunk_visible .. ch
					if measure_stripped(next_visible, font, fsize) > line_max_px and chunk_visible ~= "" then
						parts[#parts + 1] = {
							raw = chunk_raw,
							visible = chunk_visible,
							last_tag = active_tag ~= "" and active_tag or nil,
						}
						chunk_raw = active_tag ~= "" and active_tag or ""
						chunk_visible = ""
					end
					chunk_raw = chunk_raw .. ch
					chunk_visible = chunk_visible .. ch
					i = i + 1
				end
			else
				local next_visible = chunk_visible .. ch
				if measure_stripped(next_visible, font, fsize) > line_max_px and chunk_visible ~= "" then
					parts[#parts + 1] = {
						raw = chunk_raw,
						visible = chunk_visible,
						last_tag = active_tag ~= "" and active_tag or nil,
					}
					chunk_raw = active_tag ~= "" and active_tag or ""
					chunk_visible = ""
				end
				chunk_raw = chunk_raw .. ch
				chunk_visible = chunk_visible .. ch
				i = i + 1
			end
		end
		if chunk_raw ~= "" then
			parts[#parts + 1] = {
				raw = chunk_raw,
				visible = chunk_visible,
				last_tag = active_tag ~= "" and active_tag or nil,
			}
		end
		return parts
	end

	local expanded_words = {}
	for idx = 1, #words do
		local word = words[idx]
		local word_visible = word.visible or ""
		local word_raw = word.raw or ""
		if measure_stripped(word_visible, font, fsize) > max_px and not word_visible:match("%s") then
			local parts = split_long_word(word_raw, max_px)
			for j = 1, #parts do
				expanded_words[#expanded_words + 1] = parts[j]
			end
		else
			expanded_words[#expanded_words + 1] = word
		end
	end
	words = expanded_words
	local first_word_is_ts = false
	if words[1] and words[1].visible then
		first_word_is_ts = words[1].visible:match("^%[%d%d:%d%d:%d%d%]$") ~= nil
	end
	local first_line_max_px = max_px
	if ts_enabled and first_word_is_ts then
		first_line_max_px = max_px - (measure_stripped(words[1].visible, font, fsize * ts_scale) + ts_padding)
		if first_line_max_px < 0 then
			first_line_max_px = 0
		end
	end
	local current_visible = ""
	local current_raw = ""
	current_tag = ""
	local is_first_line = true
	for idx = 1, #words do
		local word = words[idx]
		local word_visible = word.visible or ""
		local word_raw = word.raw or ""
		local next_visible = current_visible == "" and word_visible or (current_visible .. " " .. word_visible)

		local measure_visible = next_visible
		local line_max_px = is_first_line and first_line_max_px or max_px
		if is_first_line and first_word_is_ts then
			measure_visible = measure_visible:gsub("^%[%d%d:%d%d:%d%d%]%s*", "")
		end
		if measure_stripped(measure_visible, font, fsize) <= line_max_px or current_visible == "" then
			if current_raw == "" then
				current_raw = (current_tag ~= "" and current_tag or "") .. word_raw
			else
				current_raw = current_raw .. " " .. word_raw
			end
			current_visible = next_visible
		else
			lines[#lines + 1] = current_raw
			current_raw = (current_tag ~= "" and current_tag or "") .. word_raw
			current_visible = word_visible
			is_first_line = false
		end

		if word.last_tag and word.last_tag ~= "" then
			current_tag = word.last_tag
		end
	end

	if current_raw ~= "" then
		lines[#lines + 1] = current_raw
	end
	if #lines == 0 then
		lines[1] = ""
	end

	local active_tag = ""
	for idx = 1, #lines do
		local line = lines[idx] or ""
		local last_tag = nil
		for tag in line:gmatch("%b{}") do
			if is_color_tag(tag) then
				last_tag = tag
			end
		end
		if active_tag ~= "" and not is_color_tag(line:match("^(%b{})")) then
			line = active_tag .. line
			lines[idx] = line
		end
		if last_tag and last_tag ~= "" then
			active_tag = last_tag
		end
	end
	return lines
end

local function get_is_chat_open()
	if samp and samp.is_chat_opened then
		local ok, v = pcall(samp.is_chat_opened)
		if ok then
			return v and true or false
		end
	end
	return false
end

local function item_right_clicked()
	if imgui.IsItemClicked then
		return imgui.IsItemClicked(1)
	end
	if imgui.IsMouseClicked then
		return imgui.IsMouseClicked(1)
	end
	return false
end

local function tooltip_text(s)
	s = tostring(s or "")
	if s == "" then
		return
	end
	mimgui_funcs.imgui_text_safe(s)
end

local function begin_tooltip_wrap(px)
	if imgui.PushTextWrapPos then
		imgui.PushTextWrapPos(px or 520)
	end
end

local function end_tooltip_wrap()
	if imgui.PopTextWrapPos then
		imgui.PopTextWrapPos()
	end
end

local function ensure_ad_entry_table_at(ad_list, index)
	if type(ad_list) ~= "table" then
		return nil, false
	end
	local entry = ad_list[index]
	if type(entry) == "table" then
		-- Миграция старого формата: строковые ключи "1","2","3","4" -> целочисленные
		-- Возникал когда neatjson сериализовал таблицу с дублирующимися ключами
		local migrated = false
		for _, sk in ipairs({"1", "2", "3", "4"}) do
			if entry[sk] ~= nil then
				local ik = tonumber(sk)
				if entry[ik] == nil or entry[ik] == "" then
					entry[ik] = entry[sk]
				end
				entry[sk] = nil
				migrated = true
			end
		end
		entry[1] = sanitize_json_string(entry[1] or "")
		entry[2] = sanitize_json_string(entry[2] or "")
		entry[3] = sanitize_json_string(entry[3] or "")
		local id = tonumber(entry.id or entry[4])
		if id and id > 0 then
			id = math.floor(id)
			entry.id = id
			entry[4] = id
		end
		return entry, migrated
	end
	entry = { sanitize_json_string(entry or ""), "", "" }
	ad_list[index] = entry
	return entry, false
end

local function normalize_ad_id(id)
	local n = tonumber(id)
	if not n then
		return nil
	end
	n = math.floor(n)
	if n <= 0 then
		return nil
	end
	return n
end

local function find_ad_entry_by_id(ad_id)
	ad_id = normalize_ad_id(ad_id)
	if not ad_id then
		return nil, nil
	end
	local ad_list = ad_buf
	for i = ad_list:len(), 1, -1 do
		local entry = ensure_ad_entry_table_at(ad_list, i)
		if entry and normalize_ad_id(entry.id or entry[4]) == ad_id then
			return entry, i
		end
	end
	return nil, nil
end

local function get_last_ad_id()
	local ad_list = ad_buf
	for i = ad_list:len(), 1, -1 do
		local entry = ensure_ad_entry_table_at(ad_list, i)
		local id = entry and normalize_ad_id(entry.id or entry[4]) or nil
		if id then
			return id
		end
	end
	return nil
end

local function get_ad_tooltip_data(kind, src_index, src_cp, all_index, ad_id)
	if kind ~= "ad" then
		return nil, nil, nil
	end

	local main_cp = tostring(src_cp or "")
	local edited_cp = ""
	local pre_cp = ""
	ad_id = normalize_ad_id(ad_id)

	local ad_entry
	if ad_id then
		ad_entry = select(1, find_ad_entry_by_id(ad_id))
	end

	if not ad_entry and type(src_index) == "number" and src_index >= 1 then
		ad_entry = ad_buf[src_index]
	end

	if ad_entry ~= nil then
		if type(ad_entry) == "table" then
			if main_cp == "" then
				main_cp = tostring(ad_entry[1] or "")
			end
			edited_cp = tostring(ad_entry[2] or "")
			pre_cp = tostring(ad_entry[3] or "")
		elseif main_cp == "" then
			main_cp = tostring(ad_entry or "")
		end
	end

	if (edited_cp == "" and pre_cp == "") and type(all_index) == "number" and all_index >= 1 then
		local all_entry = all_buf[all_index]
		if type(all_entry) == "table" and all_entry.kind == "ad" then
			if main_cp == "" then
				main_cp = tostring(all_entry.text or "")
			end
			edited_cp = tostring(all_entry.edited or "")
			pre_cp = tostring(all_entry.toredact or "")
			if not ad_id then
				ad_id = normalize_ad_id(all_entry.ad_id)
			end
		end
	end

	if (edited_cp == "" and pre_cp == "") and ad_id then
		for i = all_buf:len(), 1, -1 do
			local entry = all_buf[i]
			if type(entry) == "table" and entry.kind == "ad" and normalize_ad_id(entry.ad_id) == ad_id then
				if main_cp == "" then
					main_cp = tostring(entry.text or "")
				end
				edited_cp = tostring(entry.edited or "")
				pre_cp = tostring(entry.toredact or "")
				break
			end
		end
	end

	return main_cp, edited_cp, pre_cp
end

local function draw_ad_tooltip(main_cp, edited_cp, pre_cp)
	if not imgui.BeginTooltip then
		return
	end

	local main_u8 = strip_color_tags(main_cp or "")
	local edited_u8 = strip_color_tags(edited_cp or "")
	local pre_u8 = strip_color_tags(pre_cp or "")
	if main_u8 == "" and edited_u8 == "" and pre_u8 == "" then
		return
	end

	imgui.BeginTooltip()
	begin_tooltip_wrap(560)

	if main_u8 ~= "" then
		imgui.Text(L("vip_and_ad_chat.popup.message"))
		tooltip_text(main_u8)
	end

	if edited_u8 ~= "" then
		if main_u8 ~= "" then
			imgui.Separator()
		end
		imgui.Text(L("vip_and_ad_chat.popup.edited_by"))
		tooltip_text(edited_u8)
	end

	if pre_u8 ~= "" then
		if main_u8 ~= "" or edited_u8 ~= "" then
			imgui.Separator()
		end
		imgui.Text(L("vip_and_ad_chat.popup.before_edit"))
		tooltip_text(pre_u8)
	end

	end_tooltip_wrap()
	imgui.EndTooltip()
end

local function show_ad_tooltip_from_line(line)
	if type(line) ~= "table" then
		return
	end
	local src_index = tonumber(line.src_index)
	local all_index = tonumber(line.all_index)
	local ad_id = normalize_ad_id(line.ad_id)
	local main_cp, edited_cp, pre_cp = get_ad_tooltip_data(line.kind, src_index, line.src_cp, all_index, ad_id)
	if main_cp ~= nil then
		draw_ad_tooltip(main_cp, edited_cp, pre_cp)
	end
end

local function update_all_ad_field(ad_id, field, value)
	value = sanitize_json_string(value or "")
	ad_id = normalize_ad_id(ad_id)
	for i = all_buf:len(), 1, -1 do
		local entry = all_buf[i]
		if type(entry) == "table" and entry.kind == "ad" and (not ad_id or normalize_ad_id(entry.ad_id) == ad_id) then
			entry[field] = value
			return
		end
	end
end

local function next_ad_id()
	config.table_config = config.table_config or {}
	local seq = normalize_ad_id(config.table_config.ad_seq) or 0
	seq = seq + 1
	config.table_config.ad_seq = seq
	return seq
end

local last_ad_id_hint = nil

local data_rev = { all = 0, vip = 0, ad = 0 }
local save_dirty = false
local save_due_at = 0.0
local save_worker_started = false

local function flush_save_if_due(force)
	if not save_dirty then
		return
	end
	if force or os.clock() >= save_due_at then
		if config_manager_ref then
			config_manager_ref.markDirty("VIPandADchat")
		else
			funcs.saveTableToJson(deep_copy_sanitized(config), paths.dataPath(JSON_PATH_REL))
		end
		save_dirty = false
	end
end

start_save_worker = function()
	if save_worker_started or not lua_thread or not lua_thread.create or type(wait) ~= "function" then
		return
	end
	local ok = pcall(function()
		lua_thread.create(function()
			while true do
				wait(SAVE_FLUSH_POLL_MS)
				flush_save_if_due(false)
			end
		end)
	end)
	if ok then
		save_worker_started = true
	end
end

local function fill_buf_utf8(buf, s)
	local text = tostring(s or "")
	local max_len = ffi.sizeof(buf) - 1
	ffi.fill(buf, ffi.sizeof(buf))
	if max_len <= 0 then
		return
	end
	local copy_len = math.min(#text, max_len)
	ffi.copy(buf, text, copy_len)
end

-- ===================== ДОБАВЛЕНИЕ ТЕКСТА С РАЗМЕРОМ =====================
local add_text_with_font
do
	local function try_get(name)
		local ok, fn = pcall(function()
			return imgui.lib[name]
		end)
		if ok and fn then
			return fn
		end
	end

	local add_text_fontptr = try_get("ImDrawList_AddText_FontPtr")
	if add_text_fontptr then
		add_text_with_font = function(draw, font, font_size, pos, col, text)
			add_text_fontptr(draw, font, font_size, pos, col, text, nil, 0.0, nil)
		end
	else
		local add_text_vec2 = try_get("ImDrawList_AddText")
		if add_text_vec2 then
			add_text_with_font = function(draw, font, font_size, pos, col, text)
				if not font then
					draw:AddText(pos, col, text)
					return
				end
				local current_size = imgui.GetFontSize()
				if current_size <= 0 then
					draw:AddText(pos, col, text)
					return
				end

				local ratio = font_size / current_size
				if ratio <= 0 or math.abs(ratio - 1.0) < 0.001 then
					draw:AddText(pos, col, text)
					return
				end

				if not imgui.SetWindowFontScale then
					draw:AddText(pos, col, text)
					return
				end

				local io = imgui.GetIO()
				local window_scale = 1.0

				local ok_fontsize, base_font_size = pcall(function()
					return font.FontSize
				end)
				if not ok_fontsize or not base_font_size or base_font_size == 0 then
					base_font_size = current_size
				end

				local ok_fontscale, font_scale = pcall(function()
					return font.Scale
				end)
				if not ok_fontscale or not font_scale or font_scale == 0 then
					font_scale = 1.0
				end

				local denom = base_font_size * font_scale * (io.FontGlobalScale ~= 0 and io.FontGlobalScale or 1)
				if denom ~= 0 then
					window_scale = current_size / denom
				end
				if window_scale <= 0 then
					window_scale = 1.0
				end

				local new_window_scale = window_scale * ratio

				imgui.PushFont(font)
				imgui.SetWindowFontScale(new_window_scale)
				local ok, err = pcall(add_text_vec2, draw, pos, col, text, nil)
				imgui.SetWindowFontScale(window_scale)
				imgui.PopFont()
				if not ok then
					error(err, 0)
				end
			end
		else
			add_text_with_font = function(draw, _, _, pos, col, text)
				draw:AddText(pos, col, text)
			end
		end
	end
end

-- ===================== ПОДСВЕТКА =====================
-- Переиспользуемые ImVec2 для draw_text_with_highlight_at (избегаем аллокации на каждый AddText)
local _draw_pos = imgui.ImVec2(0, 0)
local _draw_pos2 = imgui.ImVec2(0, 0)

local function draw_text_with_highlight_at(draw, start_pos, text, highlightWordsLower, rect_color, text_alpha)
	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local lh = line_height()

	local timestamp_cfg = config.timestamp or default_config.timestamp or {}
	local ts_enabled = not (timestamp_cfg.enabled == false)
	local ts_scale = clamp(tonumber(timestamp_cfg.scale) or 0.5, 0.2, 1.0)
	local ts_padding = math.max(0.0, tonumber(timestamp_cfg.padding) or 0.0)
	local ts_offset_y = tonumber(timestamp_cfg.offset_y) or 0.0
	local ts_font_size = fsize * ts_scale
	local ts_baseline_shift = fsize - ts_font_size

	text = tostring(text or "")
	if not ts_enabled then
		text = strip_leading_timestamp(text)
	end

	local x, y = start_pos.x, start_pos.y
	local ta = text_alpha or 1.0

	local base_text = imgui.GetStyle().Colors[ffi.C.ImGuiCol_Text]
	local default_col = mul_alpha(imgui.ImVec4(base_text.x, base_text.y, base_text.z, base_text.w), ta)
	local cur_col = default_col
	local cur_col_u32 = imgui.ColorConvertFloat4ToU32(cur_col)

	-- Предвычисленные константы для подсветки
	local rect_col_u32 = imgui.GetColorU32Vec4(rect_color)
	local hl_rounding = imgui.GetStyle().FrameRounding
	local white_u32 = imgui.ColorConvertFloat4ToU32(mul_alpha(imgui.ImVec4(1, 1, 1, 1), ta))
	local has_highlights = #highlightWordsLower > 0

	local lower = has_highlights and text:lower() or nil
	local i, n = 1, #text

	while i <= n do
		local tag_s, tag_e, tag = text:find("{([%xX]+)}", i)
		if tag_s == i and (tag and (#tag == 6 or #tag == 8)) then
			cur_col = mul_alpha(hex2rgba_vec4(tag), ta)
			cur_col_u32 = imgui.ColorConvertFloat4ToU32(cur_col)
			i = tag_e + 1
		elseif tag_s == i then
			-- Невалидный тег — батчим символ с последующим текстом
			local batch_end = i
			-- Ищем конец текстового сегмента (до следующего валидного тега или конца)
			local next_valid = text:find("{[%xX]+}", i + 1)
			if next_valid then
				-- Проверяем, что найденный тег валиден, иначе берём до него
				batch_end = next_valid - 1
			else
				batch_end = n
			end
			local part = text:sub(i, batch_end)
			_draw_pos.x = x; _draw_pos.y = y
			draw:AddText(_draw_pos, cur_col_u32, part)
			x = x + text_size(part, font, fsize)
			i = batch_end + 1
		else
			local timestamp = ts_enabled and text:match("^(%[%d%d:%d%d:%d%d%])", i)
			if timestamp and ts_font_size > 0 then
				_draw_pos.x = x; _draw_pos.y = y + ts_baseline_shift + ts_offset_y
				add_text_with_font(draw, font, ts_font_size, _draw_pos, cur_col_u32, timestamp)
				x = x + text_size(timestamp, font, ts_font_size) + ts_padding
				i = i + #timestamp
				while i <= n and text:byte(i) <= 32 do
					i = i + 1
				end
				goto continue
			end

			local next_tag_s = text:find("{[%xX]+}", i)

			local hit_s, hit_e
			if has_highlights then
				for _, w in ipairs(highlightWordsLower) do
					local s, e = lower:find(w, i, true)
					if s and (not hit_s or s < hit_s) then
						hit_s, hit_e = s, e
					end
				end
			end

			if hit_s and (not next_tag_s or hit_s < next_tag_s) then
				if hit_s > i then
					local part = text:sub(i, hit_s - 1)
					_draw_pos.x = x; _draw_pos.y = y
					draw:AddText(_draw_pos, cur_col_u32, part)
					x = x + text_size(part, font, fsize)
				end

				local wtxt = text:sub(hit_s, hit_e)
				local ww = text_size(wtxt, font, fsize)

				_draw_pos.x = x; _draw_pos.y = y
				_draw_pos2.x = x + ww; _draw_pos2.y = y + lh
				draw:AddRectFilled(_draw_pos, _draw_pos2, rect_col_u32, hl_rounding)
				draw:AddText(_draw_pos, white_u32, wtxt)

				x = x + ww
				i = hit_e + 1
			else
				local next_pos = next_tag_s or (n + 1)
				local part = text:sub(i, next_pos - 1)
				_draw_pos.x = x; _draw_pos.y = y
				draw:AddText(_draw_pos, cur_col_u32, part)
				x = x + text_size(part, font, fsize)
				i = next_pos
			end
		end
		::continue::
	end

	return lh
end

-- ===================== POPUP STATE =====================
local popup_buffers = {
	message = imgui.new.char[8192](),
	edited = imgui.new.char[4096](),
	before_edit = imgui.new.char[4096](),
}

local function fill_char_buffer_from_string(buf, s_cp1251)
	local us = tostring(s_cp1251 or "")
	local maxlen = ffi.sizeof(buf)
	local n = math.min(#us, maxlen - 1)
	if n > 0 then
		ffi.copy(buf, us, n)
	end
	buf[n] = 0
end

local function calc_popup_input_size(text_utf8, max_w)
	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local lh = line_height()

	local cfg = config.popup or default_config.popup or {}
	local min_w = tonumber(cfg.min_w) or 320
	local max_w2 = tonumber(cfg.max_w) or 900
	local min_lines = tonumber(cfg.min_lines) or 3
	local max_lines = tonumber(cfg.max_lines) or 14
	local cpl = tonumber(cfg.chars_per_line) or 70

	local max_allowed_w = math.min(max_w2, max_w or max_w2)

	local longest_w = 0
	local lines = 0
	local s = tostring(text_utf8 or "")
	for line in (s .. "\n"):gmatch("(.-)\n") do
		lines = lines + 1
		local w = text_size(line, font, fsize)
		if w > longest_w then
			longest_w = w
		end
	end
	if lines <= 0 then
		lines = 1
	end

	local len = utf8_len(s)
	local est_lines = math.max(lines, math.ceil(len / math.max(1, cpl)))

	local w = clamp(longest_w + 40, min_w, max_allowed_w)
	local h = clamp(est_lines * lh + 12, min_lines * lh + 12, max_lines * lh + 12)

	return imgui.ImVec2(w, h)
end

local popup_target = {
	key = nil,
	kind = nil,
	index = nil,
	src_cp = "",
	ad_id = nil,
	main_cp = "",
	edited_cp = "",
	pre_cp = "",
	initialized = false,

	pending_open = false,
	pos = imgui.ImVec2(0, 0),

	size = imgui.ImVec2(0, 0),
	size_dirty = false,

	open_try_frames = 0,

	was_open_last = false,
}

local function fill_popup_target_fields(target)
	if not target or target.key == nil then
		return
	end

	if target.kind == "ad" then
		local main_cp, edited_cp, pre_cp =
			get_ad_tooltip_data("ad", target.index, target.src_cp, nil, normalize_ad_id(target.ad_id))
		target.main_cp = tostring(main_cp or "")
		target.edited_cp = tostring(edited_cp or "")
		target.pre_cp = tostring(pre_cp or "")
	else
		target.main_cp = tostring(target.src_cp or "")
		target.edited_cp = ""
		target.pre_cp = ""
	end

	fill_char_buffer_from_string(popup_buffers.message, strip_color_tags(target.main_cp))
	fill_char_buffer_from_string(popup_buffers.edited, strip_color_tags(target.edited_cp))
	fill_char_buffer_from_string(popup_buffers.before_edit, strip_color_tags(target.pre_cp))
end

local function calc_line_popup_size(target, max_w)
	local allowed_w = max_w or 520
	local message_sz = calc_popup_input_size(ffi.string(popup_buffers.message), allowed_w)
	local popup_w = message_sz.x + 28
	local popup_h = message_sz.y + 86

	if target and target.kind == "ad" then
		local edited_sz = calc_popup_input_size(ffi.string(popup_buffers.edited), allowed_w)
		local before_sz = calc_popup_input_size(ffi.string(popup_buffers.before_edit), allowed_w)
		popup_w = math.max(popup_w, edited_sz.x + 28, before_sz.x + 28)
		popup_h = message_sz.y + edited_sz.y + before_sz.y + 156
	end

	return imgui.ImVec2(popup_w, popup_h)
end

-- Новая функция: копирование в буфер обмена
local function copy_to_clipboard(text)
	text = tostring(text or "")
	if text == "" then
		return false
	end
	if imgui.SetClipboardText then
		imgui.SetClipboardText(text)
		return true
	end
	return false
end

local function draw_readonly_popup_field(title, id, buf, max_w, flags)
	imgui.Text(title)
	imgui.SameLine()
	imgui.PushIDStr(id .. "_copy")
	if imgui.SmallButton(L("common.copy")) then
		copy_to_clipboard(ffi.string(buf))
	end
	imgui.PopID()
	local input_sz = calc_popup_input_size(ffi.string(buf), max_w)
	imgui.InputTextMultiline(id, buf, ffi.sizeof(buf), input_sz, flags)
end

local function open_line_popup(kind, index, src_cp, key_hint, ad_id)
	local popup_key = kind .. tostring(index)
	if key_hint ~= nil and key_hint ~= "" then
		popup_key = kind .. ":" .. tostring(key_hint)
	end
	popup_target.key = popup_key
	popup_target.kind = kind
	popup_target.index = index
	popup_target.src_cp = src_cp or ""
	popup_target.ad_id = normalize_ad_id(ad_id) or normalize_ad_id(key_hint)
	popup_target.main_cp = ""
	popup_target.edited_cp = ""
	popup_target.pre_cp = ""
	popup_target.initialized = false

	local mp = imgui.GetIO().MousePos
	popup_target.pos = imgui.ImVec2(mp.x + 8, mp.y + 8)

	popup_target.pending_open = true
	popup_target.size_dirty = true
	popup_target.open_try_frames = 0
end

local function draw_line_popup(anchor_max_w)
	if popup_target.key == nil then
		return false
	end

	local popup_open_this_frame = false
	if not popup_target.initialized then
		fill_popup_target_fields(popup_target)
		popup_target.initialized = true
		popup_target.size_dirty = true
	end

	local max_w = anchor_max_w or 520
	if popup_target.size_dirty then
		popup_target.size = calc_line_popup_size(popup_target, max_w)
	end

	if popup_target.pending_open then
		imgui.OpenPopup("##VIPAD_LINE_POPUP")
		imgui.SetNextWindowPos(popup_target.pos, imgui.Cond.Appearing)
		imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Appearing)
	elseif popup_target.size_dirty then
		imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Always)
	end

	if imgui.BeginPopup("##VIPAD_LINE_POPUP") then
		popup_open_this_frame = true
		popup_target.pending_open = false
		popup_target.size_dirty = false
		popup_target.open_try_frames = 0

		imgui.Text(L("vip_and_ad_chat.text.ctrl_c"))

		-- Отображение ad_id для AD
		if popup_target.kind == "ad" and popup_target.ad_id then
			imgui.SameLine()
			imgui.TextDisabled(string.format(L("vip_and_ad_chat.popup.ad_id"), popup_target.ad_id))
		end

		imgui.Spacing()

		local itf = 0
		local ITF = imgui.InputTextFlags or {}
		if ITF.ReadOnly ~= nil then
			itf = bor(itf, ITF.ReadOnly)
		end

		local field_max_w = math.max(220, max_w - 24)
		draw_readonly_popup_field(L("vip_and_ad_chat.popup.message"), "##popup_message", popup_buffers.message, field_max_w, itf)
		if popup_target.kind == "ad" then
			imgui.Spacing()
			draw_readonly_popup_field(L("vip_and_ad_chat.popup.edited_by"), "##popup_edited", popup_buffers.edited, field_max_w, itf)
			imgui.Spacing()
			draw_readonly_popup_field(L("vip_and_ad_chat.popup.before_edit"), "##popup_before_edit", popup_buffers.before_edit, field_max_w, itf)
		end

		imgui.Spacing()

		-- Кнопка "Copy All"
		if imgui.Button(L("vip_and_ad_chat.text.text_4"), imgui.ImVec2(120, 0)) then
			local all_text = ffi.string(popup_buffers.message)
			if popup_target.kind == "ad" then
				local edited = ffi.string(popup_buffers.edited)
				local before = ffi.string(popup_buffers.before_edit)
				if edited ~= "" then
					all_text = all_text .. "\n\n" .. L("vip_and_ad_chat.popup.edited_by") .. "\n" .. edited
				end
				if before ~= "" then
					all_text = all_text .. "\n\n" .. L("vip_and_ad_chat.popup.before_edit") .. "\n" .. before
				end
			end
			copy_to_clipboard(all_text)
		end

		imgui.SameLine()
		if imgui.Button(L("vip_and_ad_chat.text.text_5"), imgui.ImVec2(120, 0)) then
			imgui.CloseCurrentPopup()
		end

		imgui.EndPopup()
	else
		if popup_target.pending_open then
			popup_target.open_try_frames = popup_target.open_try_frames + 1
			if popup_target.open_try_frames > 2 then
				popup_target.key = nil
				popup_target.pending_open = false
				popup_target.size_dirty = false
				popup_target.open_try_frames = 0
			end
		else
			popup_target.key = nil
		end
	end

	return popup_open_this_frame
end

-- ===================== ЗАГРУЗКА/СОХРАНЕНИЕ =====================
function module.load()
	config = clone_table(default_config)
	local loaded
	local repaired_json = false
	local ok, data = pcall(funcs.loadTableFromJson, JSON_PATH_REL)
	if ok and type(data) == "table" and next(data) then
		loaded = data
	end
	if type(loaded) ~= "table" or not next(loaded) then
		local repaired_loaded, was_repaired = load_table_with_json_repair(JSON_PATH_REL)
		if type(repaired_loaded) == "table" and next(repaired_loaded) then
			loaded = repaired_loaded
			repaired_json = was_repaired
		end
	end
	if type(loaded) == "table" and next(loaded) then
		for k, v in pairs(loaded) do
			config[k] = v
		end
	end
	local loaded_vip = type(loaded) == "table" and deep_copy_sanitized(loaded.vip) or nil
	local loaded_highlight_words = type(loaded) == "table" and deep_copy_sanitized(loaded.highlightWords) or nil
	local loaded_ad = type(loaded) == "table" and type(loaded.ad) == "table" and deep_copy_sanitized(loaded.ad) or nil

	merge_defaults(config, default_config)
	sanitize_table_strings_in_place(config)
	config.vip = normalize_string_list(loaded_vip, default_config.vip)
	config.highlightWords = normalize_string_list(loaded_highlight_words, default_config.highlightWords)
	config.ad = {
		main = normalize_string_list(loaded_ad and loaded_ad.main or nil, default_config.ad.main),
		pre_edit = normalize_string_list(loaded_ad and loaded_ad.pre_edit or nil, default_config.ad.pre_edit),
		edited = normalize_string_list(loaded_ad and loaded_ad.edited or nil, default_config.ad.edited),
	}
	config.table_config = config.table_config or { vip_text = {}, ad_text = {}, all = {} }
	config.table_config.vip_text = config.table_config.vip_text or {}
	config.table_config.ad_text = config.table_config.ad_text or {}
	config.table_config.all = config.table_config.all or {}
	config.timestamp = config.timestamp or clone_table(default_config.timestamp)
	merge_defaults(config.timestamp, default_config.timestamp)
	config.popup = config.popup or clone_table(default_config.popup)
	merge_defaults(config.popup, default_config.popup)
	config.chatbox = config.chatbox or clone_table(default_config.chatbox)
	merge_defaults(config.chatbox, default_config.chatbox)

	local seq = normalize_ad_id(config.table_config.ad_seq) or 0
	local migrated_old_format = false
	for i = 1, #config.table_config.ad_text do
		local entry, migrated = ensure_ad_entry_table_at(config.table_config.ad_text, i)
		if migrated then migrated_old_format = true end
		local id = normalize_ad_id(entry and (entry.id or entry[4]) or nil)
		if not id then
			seq = seq + 1
			id = seq
		elseif id > seq then
			seq = id
		end
		entry.id = id
		entry[4] = id
	end
	for i = 1, #config.table_config.all do
		local entry = config.table_config.all[i]
		if type(entry) == "table" and entry.kind == "ad" then
			entry.text = tostring(entry.text or "")
			entry.edited = tostring(entry.edited or "")
			entry.toredact = tostring(entry.toredact or "")
			local id = normalize_ad_id(entry.ad_id or entry.id)
			if not id then
				local src_index = tonumber(entry.src_index)
				if src_index and src_index >= 1 then
					local src_entry = config.table_config.ad_text[src_index]
					id = normalize_ad_id(src_entry and (src_entry.id or src_entry[4]) or nil)
				end
			end
			if not id then
				seq = seq + 1
				id = seq
			elseif id > seq then
				seq = id
			end
			entry.ad_id = id
		end
	end
	config.table_config.ad_seq = seq

	-- Заполняем кольцевые буферы из загруженных массивов
	vip_buf:set_limit(tonumber(config.vip_limit) or default_config.vip_limit)
	vip_buf:from_array(config.table_config.vip_text)
	ad_buf:set_limit(tonumber(config.ad_limit) or default_config.ad_limit)
	ad_buf:from_array(config.table_config.ad_text)
	all_buf:set_limit(tonumber(config.all_limit) or default_config.all_limit)
	all_buf:from_array(config.table_config.all)

	-- Заменяем массивы в config на буферы (для единой точки доступа)
	config.table_config.vip_text = vip_buf
	config.table_config.ad_text = ad_buf
	config.table_config.all = all_buf

	last_ad_id_hint = get_last_ad_id()

	data_rev.vip = data_rev.vip + 1
	data_rev.ad = data_rev.ad + 1
	data_rev.all = data_rev.all + 1
	save_dirty = false

	if repaired_json or migrated_old_format then
		module.save(true)
	end
end

function module.save(force)
	save_dirty = true
	save_due_at = os.clock() + SAVE_DEBOUNCE_SEC
	if force == true then
		flush_save_if_due(true)
	elseif not save_worker_started then
		flush_save_if_due(true)
	end
end

-- ===================== ПУБЛИЧНОЕ API =====================
function module.AddVIPMessage(text)
	if not config.enabled then
		return
	end
	text = sanitize_json_string(text or "")
	vip_buf:push(text)
	all_buf:push({ kind = "vip", text = text, src_index = vip_buf:len() })
	data_rev.vip = data_rev.vip + 1
	data_rev.all = data_rev.all + 1
	module.save()
end

function module.AddADMessage(main, edited, toredact)
	if not config.enabled then
		return
	end
	main = sanitize_json_string(main or "")
	edited = sanitize_json_string(edited or "")
	toredact = sanitize_json_string(toredact or "")
	local ad_id = next_ad_id()
	last_ad_id_hint = ad_id
	ad_buf:push({ main, edited, toredact, ad_id, id = ad_id })
	all_buf:push({
		kind = "ad",
		text = main,
		edited = edited,
		toredact = toredact,
		src_index = ad_buf:len(),
		ad_id = ad_id,
	})
	data_rev.ad = data_rev.ad + 1
	data_rev.all = data_rev.all + 1
	module.save()
end

function module.SetLastADEdited(text)
	if not config.enabled then
		return
	end
	text = sanitize_json_string(text or "")
	local ad = ad_buf
	if ad:len() > 0 then
		local target_id = normalize_ad_id(last_ad_id_hint) or get_last_ad_id()
		local entry
		if target_id then
			entry = select(1, find_ad_entry_by_id(target_id))
		end
		if not entry then
			entry = ensure_ad_entry_table_at(ad, ad:len())
			target_id = normalize_ad_id(entry and (entry.id or entry[4]))
		end
		if entry then
			entry[2] = text
			last_ad_id_hint = normalize_ad_id(target_id)
			update_all_ad_field(target_id, "edited", text)
		end
		module.save()
	end
end

function module.SetLastADPreEdit(text)
	if not config.enabled then
		return
	end
	text = sanitize_json_string(text or "")
	local ad = ad_buf
	if ad:len() > 0 then
		local target_id = normalize_ad_id(last_ad_id_hint) or get_last_ad_id()
		local entry
		if target_id then
			entry = select(1, find_ad_entry_by_id(target_id))
		end
		if not entry then
			entry = ensure_ad_entry_table_at(ad, ad:len())
			target_id = normalize_ad_id(entry and (entry.id or entry[4]))
		end
		if entry then
			entry[3] = text
			last_ad_id_hint = normalize_ad_id(target_id)
			update_all_ad_field(target_id, "toredact", text)
		end
		module.save()
	end
end

function module.ClearVIP()
	vip_buf:clear()
	data_rev.vip = data_rev.vip + 1
	module.save()
end

function module.ClearAD()
	ad_buf:clear()
	last_ad_id_hint = nil
	data_rev.ad = data_rev.ad + 1
	module.save()
end

function module.VIP()
	if not config.enabled then
		return {}
	end
	return config.vip
end

function module.AD()
	if not config.enabled then
		return { main = {}, pre_edit = {}, edited = {} }
	end
	return config.ad or { main = {}, pre_edit = {}, edited = {} }
end

-- ===================== HUD CHATBOX + ПРОКРУТКА + POPUP =====================
module.showFeedWindow = imgui.new.bool(false)
local vip_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local ad_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local all_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local highlight_words_cache = { serialized = nil, lower = {} }

local function get_highlight_words_lower()
	local src = (config and config.highlightWords) or {}
	local serialized = table.concat(src, "\1")
	if highlight_words_cache.serialized ~= serialized then
		local out = {}
		for i = 1, #src do
			out[i] = tostring(src[i] or ""):lower()
		end
		highlight_words_cache.serialized = serialized
		highlight_words_cache.lower = out
	end
	return highlight_words_cache.lower
end

-- Состояние автоскролла и фильтров
local all_autoscroll = true
local all_last_line = 0
local all_filter_buf = imgui.new.char[256]()
local all_filter_text = ""

local vip_autoscroll = true
local vip_last_line = 0
local vip_filter_buf = imgui.new.char[256]()
local vip_filter_text = ""

local ad_autoscroll = true
local ad_last_line = 0
local ad_filter_buf = imgui.new.char[256]()
local ad_filter_text = ""

local chat_open_was = false
local chat_open_scroll_frames = 0

-- Режим перетаскивания
local chatbox_drag_mode = false
local chatbox_drag_offset = imgui.ImVec2(0, 0)

local function handle_autoscroll(lines_count, last_line, autoscroll, is_chat_open, force_scroll)
	if force_scroll or ((not is_chat_open) and autoscroll) then
		imgui.SetScrollY(imgui.GetScrollMaxY())
	end
	last_line = lines_count
	local maxY = imgui.GetScrollMaxY()
	local y = imgui.GetScrollY()
	local threshold = line_height() * 0.5
	local at_bottom = (maxY <= 0) or (y >= maxY - threshold)
	if is_chat_open then
		if force_scroll then
			autoscroll = true
		else
			autoscroll = at_bottom
		end
	end
	return autoscroll, last_line
end

local draw_settings_content

local function draw_chatbox_window()
	if not config then
		return false, false, false
	end

	local is_chat_open = get_is_chat_open()
	if is_chat_open and not chat_open_was then
		chat_open_scroll_frames = 2
	elseif not is_chat_open then
		chat_open_scroll_frames = 0
	end
	chat_open_was = is_chat_open
	local cfg = config.chatbox or default_config.chatbox
	if not cfg or cfg.enabled == false then
		return false, false, false
	end

	local pos = imgui.ImVec2(tonumber(cfg.pos_x) or 30, tonumber(cfg.pos_y) or 600)
	local size = imgui.ImVec2(tonumber(cfg.width) or 520, tonumber(cfg.height) or 210)

	-- Обработка режима перетаскивания (ЛКМ фиксирует позицию)
	if chatbox_drag_mode then
		local io = imgui.GetIO()
		local mouse_pos = io.MousePos
		pos = imgui.ImVec2(mouse_pos.x - chatbox_drag_offset.x, mouse_pos.y - chatbox_drag_offset.y)
		if imgui.IsMouseClicked(0) then
			chatbox_drag_mode = false
			cfg.pos_x = pos.x
			cfg.pos_y = pos.y
			module.save()
		end
	end

	if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
		pos, size = mimgui_funcs.clampWindowToScreen(pos, size, 5)
	end

	imgui.SetNextWindowPos(pos, imgui.Cond.Always)
	imgui.SetNextWindowSize(size, imgui.Cond.Always)

	local flags = bor(imgui.WindowFlags.NoTitleBar, imgui.WindowFlags.NoResize, imgui.WindowFlags.NoSavedSettings)

	local highlightLower = get_highlight_words_lower()
	local rect_highlight = imgui.ImVec4(1, 1, 0, 0.38)
	local text_alpha = is_chat_open and (config.text_alpha_chat or 1.0) or (config.text_alpha_idle or 1.0)
	local child_flags = 0
	if not is_chat_open then
		child_flags =
			bor(imgui.WindowFlags.NoScrollWithMouse, imgui.WindowFlags.NoScrollbar, imgui.WindowFlags.NoBackground)
	end

	local hovered = false
	local popup_open = false
	local bg_alpha = clamp(tonumber(cfg.bg_alpha) or 0.35, 0.0, 1.0)
	local rounding = clamp(tonumber(cfg.rounding) or 8, 0, 20)
	local pushed_colors = 0
	local pushed_vars = 0
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, rounding)
	pushed_vars = pushed_vars + 1
	if is_chat_open then
		imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0, 0, 0, bg_alpha))
		imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0, 0, 0, bg_alpha))
		pushed_colors = 2
	else
		imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0, 0, 0, 0))
		imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0, 0, 0, 0))
		pushed_colors = 2
	end
	if chatbox_drag_mode then
		imgui.PushStyleColor(imgui.Col.Border, imgui.ImVec4(1.0, 0.6, 0.0, 0.8))
		imgui.PushStyleVarFloat(imgui.StyleVar.WindowBorderSize, 2.0)
		pushed_colors = pushed_colors + 1
		pushed_vars = pushed_vars + 1
	end
	if imgui.Begin("##VIPAD_CHATBOX", nil, flags) then
		local function render_line(draw, line_pos, line, alpha)
			draw_text_with_highlight_at(draw, line_pos, line.text or "", highlightLower, rect_highlight, alpha)
		end

		local function build_wrap_cache(cache, list, max_px, data_rev_key, cfg_key, build_lines)
			if cache.rev ~= data_rev_key or cache.cfg_key ~= cfg_key then
				cache.width = max_px
				cache.src_count = type(list.len) == "function" and list:len() or #list
				cache.rev = data_rev_key
				cache.cfg_key = cfg_key
				cache.lines = build_lines()
			end
		end

		local function draw_hitboxes(lines, start_y, row_h, on_hover)
			local row_w = imgui.GetContentRegionAvail().x
			local start = imgui.GetCursorScreenPos()
			if is_chat_open then
				imgui.PushIDStr(tostring(start_y))
				imgui.InvisibleButton("##chat_line", imgui.ImVec2(row_w, row_h))
				local hovered_item = imgui.IsItemHovered()
				if hovered_item and on_hover then
					on_hover(lines)
				end
				imgui.PopID()
			else
				imgui.Dummy(imgui.ImVec2(row_w, row_h))
			end
			imgui.SetCursorScreenPos(start)
			return start
		end

		-- Функция отрисовки контекстного меню для строки
		local function draw_line_context_menu(line, on_open_popup)
			if imgui.BeginPopupContextItem("##line_ctx") then
				if imgui.MenuItemBool(L("vip_and_ad_chat.text.text_6")) then
					local text_no_tags = strip_color_tags(line.src_cp or line.text or "")
					copy_to_clipboard(text_no_tags)
				end
				if imgui.MenuItemBool(L("vip_and_ad_chat.text.text_7")) then
					local text_no_tags = strip_color_tags(line.src_cp or line.text or "")
					local text_no_time = strip_leading_timestamp(text_no_tags)
					copy_to_clipboard(text_no_time)
				end
				imgui.Separator()
				if imgui.MenuItemBool(L("vip_and_ad_chat.text.text_8")) then
					if on_open_popup then
						on_open_popup(line)
					end
				end
				imgui.EndPopup()
			end
		end

		-- Функция для отрисовки кнопок управления вкладкой
		local function draw_tab_controls(tab_name, autoscroll, filter_buf, filter_text, clear_func, get_visible_text_func)
			imgui.PushIDStr(tab_name .. "_controls")

			-- Кнопка "Вниз"
			if imgui.SmallButton(L("vip_and_ad_chat.text.text_9")) then
				imgui.SetScrollY(imgui.GetScrollMaxY())
			end
			if imgui.IsItemHovered() then
				imgui.SetTooltip(L("vip_and_ad_chat.text.text_10"))
			end

			imgui.SameLine()

			-- Кнопка паузы автоскролла
			local pause_label = autoscroll and (fa.PAUSE .. L("vip_and_ad_chat.text.text_11")) or (fa.PLAY .. L("vip_and_ad_chat.text.text_12"))
			if imgui.SmallButton(pause_label) then
				autoscroll = not autoscroll
			end
			if imgui.IsItemHovered() then
				imgui.SetTooltip(autoscroll and L("vip_and_ad_chat.text.text_13") or L("vip_and_ad_chat.text.text_14"))
			end

			imgui.SameLine()

			-- Кнопка "Очистить вкладку"
			if imgui.SmallButton(L("vip_and_ad_chat.text.text_15")) then
				if clear_func then
					clear_func()
				end
			end
			if imgui.IsItemHovered() then
				imgui.SetTooltip(L("vip_and_ad_chat.text.text_16"))
			end

			imgui.SameLine()

			-- Кнопка "Копировать видимое"
			if imgui.SmallButton(L("vip_and_ad_chat.text.text_17")) then
				if get_visible_text_func then
					local visible_text = get_visible_text_func()
					copy_to_clipboard(visible_text)
				end
			end
			if imgui.IsItemHovered() then
				imgui.SetTooltip(L("vip_and_ad_chat.text.text_18"))
			end

			imgui.SameLine()
			imgui.Text(L("vip_and_ad_chat.text.text_19"))
			imgui.SameLine()

			-- Фильтр
			imgui.PushItemWidth(150)
			if imgui.InputText("##filter", filter_buf, ffi.sizeof(filter_buf)) then
				filter_text = ffi.string(filter_buf):lower()
			end
			imgui.PopItemWidth()

			if filter_text ~= "" then
				imgui.SameLine()
				if imgui.SmallButton(L("common.clear_compact")) then
					ffi.fill(filter_buf, ffi.sizeof(filter_buf))
					filter_text = ""
				end
			end

			imgui.PopID()
			imgui.Separator()
			return autoscroll, filter_text
		end

		local function render_all_tab()
			local all = all_buf

			if imgui.BeginChild("##all_scroll", imgui.ImVec2(0, 0), false, child_flags) then
			if is_chat_open then
				all_autoscroll, all_filter_text = draw_tab_controls(
					"all",
					all_autoscroll,
					all_filter_buf,
					all_filter_text,
					function()
						all_buf:clear()
						data_rev.all = data_rev.all + 1
						module.save()
					end,
					function()
						local lines = {}
						for i = 1, #all_wrap_cache.lines do
							local line = all_wrap_cache.lines[i] or {}
							local text = strip_color_tags(line.text or "")
							if all_filter_text == "" or text:lower():find(all_filter_text, 1, true) then
								lines[#lines + 1] = text
							end
						end
						return table.concat(lines, "\n")
					end
				)
			end

				local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
				local lh = line_height()
				local ts_cfg = config.timestamp or default_config.timestamp or {}
				local cfg_key = string.format(
					"%.1f|%s|%.3f|%.3f",
					max_px,
					tostring(ts_cfg.enabled ~= false),
					tonumber(ts_cfg.scale) or 0.5,
					tonumber(ts_cfg.padding) or 0
				)
				if all_wrap_cache.width ~= max_px then
					all_autoscroll = true
				end
				build_wrap_cache(all_wrap_cache, all, max_px, data_rev.all, cfg_key, function()
					local lines = {}
					for i = 1, all:len() do
						local entry = all[i] or {}
						local text_cp = entry.text or ""
						local text = tostring(text_cp or "")
						local wrapped = wrap_to_lines_keep_tags(text, max_px)
						for j = 1, #wrapped do
							lines[#lines + 1] = {
								text = wrapped[j],
								kind = entry.kind,
								src_index = entry.src_index or i,
								src_cp = text_cp,
								all_index = i,
								ad_id = normalize_ad_id(entry.ad_id),
							}
						end
					end
					return lines
				end)

				-- Применяем фильтр
				local filtered_lines = {}
				for i = 1, #all_wrap_cache.lines do
					local line = all_wrap_cache.lines[i]
					if all_filter_text == "" then
						filtered_lines[#filtered_lines + 1] = {line = line, index = i}
					else
						local text_check = strip_color_tags(line.text or ""):lower()
						if text_check:find(all_filter_text, 1, true) then
							filtered_lines[#filtered_lines + 1] = {line = line, index = i}
						end
					end
				end

				local draw = imgui.GetWindowDrawList()
				local clipper = imgui.ImGuiListClipper(#filtered_lines)
				while clipper:Step() do
					for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
						local item = filtered_lines[i]
						local line = item.line
						local original_idx = item.index
						imgui.PushIDInt(original_idx)
					local start = draw_hitboxes(line, original_idx, lh, function(entry)
						show_ad_tooltip_from_line(entry)
					end)
					draw_line_context_menu(line, function(entry)
						open_line_popup(
							entry.kind or "vip",
							entry.src_index or original_idx,
							entry.src_cp or "",
							entry.ad_id or entry.all_index,
							entry.ad_id
						)
					end)
						imgui.PopID()
						render_line(draw, imgui.ImVec2(start.x, start.y), line, text_alpha)
						imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
					end
				end

				local force_scroll = chat_open_scroll_frames > 0
				all_autoscroll, all_last_line =
					handle_autoscroll(#all_wrap_cache.lines, all_last_line, all_autoscroll, is_chat_open, force_scroll)
				if chat_open_scroll_frames > 0 then
					chat_open_scroll_frames = chat_open_scroll_frames - 1
				end
			end
			imgui.EndChild()
		end

		if is_chat_open then
			if imgui.BeginTabBar("##VIPAD_CHATBOX_TABS") then
				if imgui.BeginTabItem(L("vip_and_ad_chat.tab.all")) then
					render_all_tab()
					imgui.EndTabItem()
				end

				if imgui.BeginTabItem(L("vip_and_ad_chat.tab.vip")) then
					local vip = vip_buf

					if imgui.BeginChild("##vip_scroll", imgui.ImVec2(0, 0), false, child_flags) then
					if is_chat_open then
						vip_autoscroll, vip_filter_text = draw_tab_controls(
							"vip",
							vip_autoscroll,
							vip_filter_buf,
							vip_filter_text,
							function()
								module.ClearVIP()
							end,
							function()
								local lines = {}
								for i = 1, #vip_wrap_cache.lines do
									local line = vip_wrap_cache.lines[i] or {}
									local text = strip_color_tags(line.text or "")
									if vip_filter_text == "" or text:lower():find(vip_filter_text, 1, true) then
										lines[#lines + 1] = text
									end
								end
								return table.concat(lines, "\n")
							end
						)
					end

						local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
						local lh = line_height()
						local ts_cfg = config.timestamp or default_config.timestamp or {}
						local cfg_key = string.format(
							"%.1f|%s|%.3f|%.3f",
							max_px,
							tostring(ts_cfg.enabled ~= false),
							tonumber(ts_cfg.scale) or 0.5,
							tonumber(ts_cfg.padding) or 0
						)
						build_wrap_cache(vip_wrap_cache, vip, max_px, data_rev.vip, cfg_key, function()
							local lines = {}
							for i = 1, vip:len() do
								local text_cp = vip[i] or ""
								local text = tostring(text_cp or "")
								local wrapped = wrap_to_lines_keep_tags(text, max_px)
								for j = 1, #wrapped do
									lines[#lines + 1] = {
										text = wrapped[j],
										kind = "vip",
										src_index = i,
										src_cp = text_cp,
									}
								end
							end
							return lines
						end)

						-- Применяем фильтр
						local filtered_lines = {}
						for i = 1, #vip_wrap_cache.lines do
							local line = vip_wrap_cache.lines[i]
							if vip_filter_text == "" then
								filtered_lines[#filtered_lines + 1] = {line = line, index = i}
							else
								local text_check = strip_color_tags(line.text or ""):lower()
								if text_check:find(vip_filter_text, 1, true) then
									filtered_lines[#filtered_lines + 1] = {line = line, index = i}
								end
							end
						end

						local clipper = imgui.ImGuiListClipper(#filtered_lines)
						local draw = imgui.GetWindowDrawList()
						while clipper:Step() do
							for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
								local item = filtered_lines[i]
								local line = item.line
								local original_idx = item.index
								imgui.PushIDInt(original_idx)
							local start = draw_hitboxes(line, original_idx, lh, nil)
							draw_line_context_menu(line, function(entry)
								open_line_popup(entry.kind or "vip", entry.src_index or original_idx, entry.src_cp or "", original_idx)
							end)
								imgui.PopID()
								render_line(draw, imgui.ImVec2(start.x, start.y), line, text_alpha)
								imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
							end
						end

						local force_scroll = chat_open_scroll_frames > 0
						vip_autoscroll, vip_last_line = handle_autoscroll(
							#vip_wrap_cache.lines,
							vip_last_line,
							vip_autoscroll,
							is_chat_open,
							force_scroll
						)
						if chat_open_scroll_frames > 0 then
							chat_open_scroll_frames = chat_open_scroll_frames - 1
						end
					end
					imgui.EndChild()
					imgui.EndTabItem()
				end

				if imgui.BeginTabItem(L("vip_and_ad_chat.tab.ad")) then
					local ad = ad_buf

					if imgui.BeginChild("##ad_scroll", imgui.ImVec2(0, 0), false, child_flags) then
					if is_chat_open then
						ad_autoscroll, ad_filter_text = draw_tab_controls(
							"ad",
							ad_autoscroll,
							ad_filter_buf,
							ad_filter_text,
							function()
								module.ClearAD()
							end,
							function()
								local lines = {}
								for i = 1, #ad_wrap_cache.lines do
									local line = ad_wrap_cache.lines[i] or {}
									local text = strip_color_tags(line.text or "")
									if ad_filter_text == "" or text:lower():find(ad_filter_text, 1, true) then
										lines[#lines + 1] = text
									end
								end
								return table.concat(lines, "\n")
							end
						)
					end

						local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
						local lh = line_height()
						local ts_cfg = config.timestamp or default_config.timestamp or {}
						local cfg_key = string.format(
							"%.1f|%s|%.3f|%.3f",
							max_px,
							tostring(ts_cfg.enabled ~= false),
							tonumber(ts_cfg.scale) or 0.5,
							tonumber(ts_cfg.padding) or 0
						)
						build_wrap_cache(ad_wrap_cache, ad, max_px, data_rev.ad, cfg_key, function()
							local lines = {}
							for i = 1, ad:len() do
								local entry = ad[i] or {}
								local text_cp = entry[1] or ""
								local text = tostring(text_cp or "")
								local wrapped = wrap_to_lines_keep_tags(text, max_px)
								for j = 1, #wrapped do
									lines[#lines + 1] = {
										text = wrapped[j],
										kind = "ad",
										src_index = i,
										src_cp = text_cp,
										ad_id = normalize_ad_id(entry.id),
									}
								end
							end
							return lines
						end)

						-- Применяем фильтр
						local filtered_lines = {}
						for i = 1, #ad_wrap_cache.lines do
							local line = ad_wrap_cache.lines[i]
							if ad_filter_text == "" then
								filtered_lines[#filtered_lines + 1] = {line = line, index = i}
							else
								local text_check = strip_color_tags(line.text or ""):lower()
								if text_check:find(ad_filter_text, 1, true) then
									filtered_lines[#filtered_lines + 1] = {line = line, index = i}
								end
							end
						end

						local clipper = imgui.ImGuiListClipper(#filtered_lines)
						local draw = imgui.GetWindowDrawList()
						while clipper:Step() do
							for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
								local item = filtered_lines[i]
								local line = item.line
								local original_idx = item.index
								imgui.PushIDInt(original_idx)
							local start = draw_hitboxes(line, original_idx, lh, function(entry)
								show_ad_tooltip_from_line(entry)
							end)
							draw_line_context_menu(line, function(entry)
								open_line_popup(
									entry.kind or "ad",
									entry.src_index or original_idx,
									entry.src_cp or "",
									entry.ad_id or original_idx,
									entry.ad_id
								)
							end)
								imgui.PopID()
								render_line(draw, imgui.ImVec2(start.x, start.y), line, text_alpha)
								imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
							end
						end

						local force_scroll = chat_open_scroll_frames > 0
						ad_autoscroll, ad_last_line = handle_autoscroll(
							#ad_wrap_cache.lines,
							ad_last_line,
							ad_autoscroll,
							is_chat_open,
							force_scroll
						)
						if chat_open_scroll_frames > 0 then
							chat_open_scroll_frames = chat_open_scroll_frames - 1
						end
					end
					imgui.EndChild()
					imgui.EndTabItem()
				end

				imgui.EndTabBar()
			end
		else
			render_all_tab()
		end

		if is_chat_open then
			popup_open = draw_line_popup(cfg.width or 520)
		end

		local wpos = imgui.GetWindowPos()
		local wsize = imgui.GetWindowSize()
		cfg.pos_x = wpos.x
		cfg.pos_y = wpos.y
		cfg.width = wsize.x
		cfg.height = wsize.y

		hovered = imgui.IsWindowHovered()
	end
	imgui.End()
	if pushed_colors > 0 then
		imgui.PopStyleColor(pushed_colors)
	end
	if pushed_vars > 0 then
		imgui.PopStyleVar(pushed_vars)
	end

	local allow_process = is_chat_open or popup_target.pending_open or popup_open or popup_target.was_open_last
	local want_cursor = popup_target.pending_open or popup_open or hovered

	return allow_process, want_cursor, popup_open
end

-- ===================== ONFRAME =====================
local hudFrame = imgui.OnFrame(function()
	return module.showFeedWindow[0]
		and config
		and config.enabled
		and funcs
		and funcs.CGame__EnableHUD
		and funcs.CGame__EnableHUD()
		and not isPauseMenuActive()
end, function(frame)
	local chat_open = get_is_chat_open()

	imgui.Process = chat_open or popup_target.pending_open or popup_target.was_open_last

	local allow_process, want_cursor, popup_open = draw_chatbox_window()
	imgui.Process = allow_process

	popup_target.was_open_last = popup_open

	if frame then
		frame.HideCursor = not want_cursor
		frame.LockPlayer = false
	end

	flush_save_if_due(false)
end)

-- ===================== ОКНО НАСТРОЕК (опционально) =====================
local settings_open = imgui.new.bool(false)
module.showSettingsWindow = settings_open

local highlight_words_buf = imgui.new.char[2048]("")
local highlight_words_last_serialized = ""
local vip_patterns_buf = imgui.new.char[4096]("")
local vip_patterns_last_serialized = ""
local ad_main_patterns_buf = imgui.new.char[4096]("")
local ad_main_patterns_last_serialized = ""
local ad_pre_edit_patterns_buf = imgui.new.char[4096]("")
local ad_pre_edit_patterns_last_serialized = ""
local ad_edited_patterns_buf = imgui.new.char[4096]("")
local ad_edited_patterns_last_serialized = ""
local settings_ui_state = { bools = {}, ints = {}, floats = {} }
local settings_window_nav = { page = 0 }
local settings_inline_nav = { page = 0 }

local SETTINGS_SECTION_CHAT_VIEW = 1
local SETTINGS_SECTION_LIMITS = 2
local SETTINGS_SECTION_FILTERS = 3
local SETTINGS_SECTION_POPUP = 4

local function sync_list_buffer(list, buf, last_serialized)
	local serialized = serialize_string_list(list)
	if serialized ~= last_serialized then
		fill_buf_utf8(buf, serialized)
		return serialized
	end
	return last_serialized
end

local function apply_pattern_list_from_buffer(buf)
	return parse_string_list(ffi.string(buf))
end

local function draw_pattern_list_editor(opts)
	imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L(opts.title_key))
	imgui.TextWrapped(L(opts.desc_key))
	imgui.Spacing()

	local last_serialized = sync_list_buffer(opts.list, opts.buf, opts.last_serialized)

	imgui.PushItemWidth(-1)
	imgui.InputTextMultiline(
		opts.input_id,
		opts.buf,
		ffi.sizeof(opts.buf),
		imgui.ImVec2(0, opts.height or 110)
	)
	imgui.PopItemWidth()

	local changed = false
	if imgui.Button(L("vip_and_ad_chat.text.ad_patterns_apply") .. opts.apply_id, imgui.ImVec2(120, 0)) then
		opts.list = apply_pattern_list_from_buffer(opts.buf)
		last_serialized = serialize_string_list(opts.list)
		changed = true
	end
	imgui.SameLine()
	if imgui.Button(L("vip_and_ad_chat.text.ad_patterns_reset") .. opts.reset_id, imgui.ImVec2(120, 0)) then
		opts.list = clone_table(opts.defaults or {})
		last_serialized = serialize_string_list(opts.list)
		fill_buf_utf8(opts.buf, last_serialized)
		changed = true
	end

	return opts.list, last_serialized, changed
end

local function ui_bool(id, value)
	local b = settings_ui_state.bools[id]
	if not b then
		b = imgui.new.bool(false)
		settings_ui_state.bools[id] = b
	end
	b[0] = value and true or false
	return b
end

local function ui_int(id, value)
	local b = settings_ui_state.ints[id]
	if not b then
		b = imgui.new.int(0)
		settings_ui_state.ints[id] = b
	end
	b[0] = math.floor(tonumber(value) or 0)
	return b
end

local function ui_float(id, value)
	local b = settings_ui_state.floats[id]
	if not b then
		b = imgui.new.float(0)
		settings_ui_state.floats[id] = b
	end
	b[0] = tonumber(value) or 0
	return b
end

-- Функция для отрисовки подсказки (?)
local function help_marker(desc)
	imgui.TextDisabled(L("vip_and_ad_chat.text.help_short"))
	if imgui.IsItemHovered() then
		imgui.BeginTooltip()
		imgui.PushTextWrapPos(imgui.GetFontSize() * 35.0)
		imgui.Text(desc)
		imgui.PopTextWrapPos()
		imgui.EndTooltip()
	end
end

-- Функция для выравнивания лейблов
local function aligned_text(label, width)
	width = width or 200
	imgui.Text(label)
	imgui.SameLine(width)
end

-- Функция для рисования интерактивного превью
local function draw_preview_panel(force_visible)
	if not force_visible and not imgui.CollapsingHeader(L("vip_and_ad_chat.text.text_20")) then
		return
	end

	local chatbox = config.chatbox or default_config.chatbox
	local bg_alpha = clamp(tonumber(chatbox.bg_alpha) or 0.35, 0.0, 1.0)
	local rounding = clamp(tonumber(chatbox.rounding) or 8, 0, 20)
	local text_alpha_chat = config.text_alpha_chat or 1.0
	local ts_cfg = config.timestamp or default_config.timestamp or {}
	local ts_scale = clamp(tonumber(ts_cfg.scale) or 0.5, 0.2, 1.0)
	local ts_padding = math.max(0.0, tonumber(ts_cfg.padding) or 0.0)
	local ts_offset_y = tonumber(ts_cfg.offset_y) or 0.0
	local ts_enabled = ts_cfg.enabled ~= false

	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, rounding)
	imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0, 0, 0, bg_alpha))

	if imgui.BeginChild("##preview_child", imgui.ImVec2(0, 120), true) then
		local draw = imgui.GetWindowDrawList()
		local start_pos = imgui.GetCursorScreenPos()
		local font = imgui.GetFont()
		local fsize = imgui.GetFontSize()

		-- Фейковые сообщения для превью
		local fake_messages = {
			L("vip_and_ad_chat.text.ffff00_12_34_56_ffffff_vip_00ff00_player_name_ffffff_vip"),
			L("vip_and_ad_chat.text.ffff00_12_35_12_ff00ff_admin_ff0000_admin_john_ffffff"),
			L("vip_and_ad_chat.text.ffff00_12_36_48_00ffff_server_ffffff"),
		}

		local y_offset = 0
		for _, msg in ipairs(fake_messages) do
			local line_y = start_pos.y + y_offset
			local line_pos = imgui.ImVec2(start_pos.x, line_y)

			-- Рисуем текст с учетом настроек timestamp
			local text_to_draw = msg
			if not ts_enabled then
				text_to_draw = strip_leading_timestamp(msg)
			end

			draw_text_with_highlight_at(draw, line_pos, text_to_draw, {}, imgui.ImVec4(1, 1, 0, 0.38), text_alpha_chat)
			y_offset = y_offset + line_height()
		end

		imgui.SetCursorScreenPos(imgui.ImVec2(start_pos.x, start_pos.y + y_offset))
	end
	imgui.EndChild()

	imgui.PopStyleColor()
	imgui.PopStyleVar()
end

local settings_sections = {
	{ id = SETTINGS_SECTION_CHAT_VIEW, title = L("vip_and_ad_chat.text.text_21") },
	{ id = SETTINGS_SECTION_LIMITS, title = L("vip_and_ad_chat.text.text_22") },
	{ id = SETTINGS_SECTION_FILTERS, title = L("vip_and_ad_chat.text.text_23") },
	{ id = SETTINGS_SECTION_POPUP, title = L("vip_and_ad_chat.text.text_24") },
}

local function get_settings_section(page)
	for i = 1, #settings_sections do
		local section = settings_sections[i]
		if section.id == page then
			return section
		end
	end
end

local function draw_settings_cards(nav)
	imgui.TextColored(imgui.ImVec4(0.8, 0.8, 1, 1), L("vip_and_ad_chat.text.text_25"))
	imgui.Spacing()

	local avail = imgui.GetContentRegionAvail().x
	local cardW, cardH = 220, 60
	local spacing = 16
	local cols = math.max(1, math.floor((avail + spacing) / (cardW + spacing)))
	local start_pos = imgui.GetCursorScreenPos()

	for i = 1, #settings_sections do
		local section = settings_sections[i]
		local x = start_pos.x + ((i - 1) % cols) * (cardW + spacing)
		local y = start_pos.y + math.floor((i - 1) / cols) * (cardH + spacing)
		imgui.SetCursorScreenPos(imgui.ImVec2(x, y))
		imgui.InvisibleButton("##vipad_settings_card_" .. section.id, imgui.ImVec2(cardW, cardH))

		local hovered = imgui.IsItemHovered()
		if imgui.IsItemClicked(0) then
			nav.page = section.id
		end

		local pmin = imgui.GetItemRectMin()
		local pmax = imgui.GetItemRectMax()
		local style = imgui.GetStyle()
		local dl = imgui.GetWindowDrawList()
		local bg = hovered and style.Colors[imgui.Col.FrameBgHovered] or style.Colors[imgui.Col.FrameBg]
		local text_y = pmin.y + math.max(0, (cardH - imgui.GetTextLineHeight()) * 0.5)

		dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(bg), 8)
		dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(style.Colors[imgui.Col.Border]), 8, 2)
		dl:AddText(
			imgui.ImVec2(pmin.x + 12, text_y),
			imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]),
			section.title
		)
	end

	local rows = math.ceil(#settings_sections / cols)
	if rows > 0 then
		local grid_height = rows * cardH + math.max(0, rows - 1) * spacing
		imgui.SetCursorScreenPos(imgui.ImVec2(start_pos.x, start_pos.y + grid_height))
		imgui.Dummy(imgui.ImVec2(0, 1))
	end
end

local function draw_settings_detail_header(nav)
	local section = get_settings_section(nav.page)
	if not section then
		nav.page = 0
		return false
	end

	if imgui.Button(fa.ARROW_LEFT .. L("vip_and_ad_chat.text.text_26")) then
		nav.page = 0
		return false
	end
	imgui.SameLine()
	imgui.Text(section.title)
	imgui.Separator()
	return true
end

draw_settings_content = function(nav)
	nav = nav or settings_inline_nav

	local en = ui_bool("enabled", config.enabled and true or false)
	if imgui.Checkbox(L("vip_and_ad_chat.text.text_27"), en) then
		module.setEnabled(en[0], { flush_project = true })
	end

	imgui.Separator()

	if nav.page == 0 then
		draw_settings_cards(nav)
		return
	end

	if not draw_settings_detail_header(nav) then
		if nav.page == 0 then
			draw_settings_cards(nav)
		end
		return
	end

	if nav.page == SETTINGS_SECTION_CHAT_VIEW then
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_28"))
		draw_preview_panel(true)
		imgui.Separator()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_29"))
		config.chatbox = config.chatbox or clone_table(default_config.chatbox)
		local chatbox = config.chatbox

		local chatbox_enabled = ui_bool("chatbox_enabled", chatbox.enabled ~= false)
		if imgui.Checkbox(L("vip_and_ad_chat.text.chatbox"), chatbox_enabled) then
			chatbox.enabled = chatbox_enabled[0] and true or false
			module.save()
		end

		imgui.Spacing()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_30"))

		aligned_text(L("vip_and_ad_chat.text.x"), 150)
		imgui.PushItemWidth(150)
		local chatbox_pos_x = ui_int("chatbox_pos_x", chatbox.pos_x)
		if imgui.DragInt("##pos_x_chatbox", chatbox_pos_x) then
			chatbox.pos_x = chatbox_pos_x[0]
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		aligned_text(L("vip_and_ad_chat.text.y"), 150)
		imgui.PushItemWidth(150)
		local chatbox_pos_y = ui_int("chatbox_pos_y", chatbox.pos_y)
		if imgui.DragInt("##pos_y_chatbox", chatbox_pos_y) then
			chatbox.pos_y = chatbox_pos_y[0]
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		aligned_text(L("vip_and_ad_chat.text.text_32"), 150)
		imgui.PushItemWidth(150)
		local chatbox_width = ui_int("chatbox_width", chatbox.width or 520)
		if imgui.DragInt("##width_chatbox", chatbox_width, 1, 200, 1200) then
			chatbox.width = clamp(chatbox_width[0], 200, 1200)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		aligned_text(L("vip_and_ad_chat.text.text_33"), 150)
		imgui.PushItemWidth(150)
		local chatbox_height = ui_int("chatbox_height", chatbox.height or 210)
		if imgui.DragInt("##height_chatbox", chatbox_height, 1, 120, 700) then
			chatbox.height = clamp(chatbox_height[0], 120, 700)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		imgui.Spacing()

		-- Кнопки управления позицией
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_34"))
		local io = imgui.GetIO()
		local screen_w, screen_h = io.DisplaySize.x, io.DisplaySize.y

		if imgui.Button(fa.UP_LEFT .. L("vip_and_ad_chat.text.text_35")) then
			chatbox.pos_x = 10
			chatbox.pos_y = 10
			module.save()
		end
		imgui.SameLine()
		if imgui.Button(fa.UP_RIGHT .. L("vip_and_ad_chat.text.text_36")) then
			chatbox.pos_x = screen_w - chatbox.width - 10
			chatbox.pos_y = 10
			module.save()
		end
		imgui.SameLine()
		if imgui.Button(fa.DOWN_LEFT .. L("vip_and_ad_chat.text.text_37")) then
			chatbox.pos_x = 10
			chatbox.pos_y = screen_h - chatbox.height - 10
			module.save()
		end
		imgui.SameLine()
		if imgui.Button(fa.DOWN_RIGHT .. L("vip_and_ad_chat.text.text_38")) then
			chatbox.pos_x = screen_w - chatbox.width - 10
			chatbox.pos_y = screen_h - chatbox.height - 10
			module.save()
		end

		imgui.Spacing()

		if imgui.Button(L("vip_and_ad_chat.text.text_39"), imgui.ImVec2(200, 0)) then
			chatbox.pos_x = default_config.chatbox.pos_x
			chatbox.pos_y = default_config.chatbox.pos_y
			chatbox.width = default_config.chatbox.width
			chatbox.height = default_config.chatbox.height
			module.save()
		end
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_40"))

		imgui.Spacing()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_41"))

		if chatbox_drag_mode then
			imgui.TextColored(imgui.ImVec4(1.0, 0.6, 0.0, 1.0), fa.HAND .. L("vip_and_ad_chat.text.text_42"))
			if imgui.Button(fa.STOP .. L("vip_and_ad_chat.text.text_43"), imgui.ImVec2(200, 0)) then
				chatbox_drag_mode = false
			end
		else
			if imgui.Button(fa.HAND .. L("vip_and_ad_chat.text.text_44"), imgui.ImVec2(200, 0)) then
				chatbox_drag_mode = true
				local w = tonumber(chatbox.width) or 520
				local h = tonumber(chatbox.height) or 210
				chatbox_drag_offset = imgui.ImVec2(w * 0.5, h * 0.5)
			end
		end
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_45"))

		imgui.Spacing()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_46"))

		aligned_text(L("vip_and_ad_chat.text.text_47"), 180)
		imgui.PushItemWidth(200)
		local chatbox_bg_alpha = ui_float("chatbox_bg_alpha", chatbox.bg_alpha or 0)
		if imgui.SliderFloat("##chatbox_bg_alpha", chatbox_bg_alpha, 0, 1, "%.2f") then
			chatbox.bg_alpha = clamp(chatbox_bg_alpha[0], 0, 1)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_0_1"))

		aligned_text(L("vip_and_ad_chat.text.text_48"), 180)
		imgui.PushItemWidth(200)
		local chatbox_rounding = ui_int("chatbox_rounding", chatbox.rounding or 0)
		if imgui.SliderInt("##chatbox_rounding", chatbox_rounding, 0, 20, L("vip_and_ad_chat.text.number")) then
			chatbox.rounding = clamp(chatbox_rounding[0], 0, 20)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_49"))
		imgui.Separator()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_50"))
		aligned_text(L("vip_and_ad_chat.text.text_51"), 220)
		imgui.PushItemWidth(200)
		local text_alpha_chat = ui_float(
			"chatbox_text_alpha_chat",
			config.text_alpha_chat or default_config.text_alpha_chat or 1.0
		)
		if imgui.SliderFloat("##text_alpha_chat", text_alpha_chat, 0, 1, "%.2f") then
			config.text_alpha_chat = clamp(text_alpha_chat[0], 0, 1)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_52"))

		aligned_text(L("vip_and_ad_chat.text.text_53"), 220)
		imgui.PushItemWidth(200)
		local text_alpha_idle = ui_float(
			"chatbox_text_alpha_idle",
			config.text_alpha_idle or default_config.text_alpha_idle or 0.5
		)
		if imgui.SliderFloat("##text_alpha_idle", text_alpha_idle, 0, 1, "%.2f") then
			config.text_alpha_idle = clamp(text_alpha_idle[0], 0, 1)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_54"))
		imgui.Separator()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_55"))
		config.timestamp = config.timestamp or clone_table(default_config.timestamp)
		local timestamp = config.timestamp

		local timestamp_enabled = ui_bool("timestamp_enabled", timestamp.enabled ~= false)
		if imgui.Checkbox(L("vip_and_ad_chat.text.timestamp_enabled"), timestamp_enabled) then
			timestamp.enabled = timestamp_enabled[0] and true or false
			module.save()
		end

		aligned_text(L("vip_and_ad_chat.text.text_56"), 180)
		imgui.PushItemWidth(200)
		local timestamp_scale = ui_float("timestamp_scale", timestamp.scale or 0.5)
		if imgui.SliderFloat("##timestamp_scale", timestamp_scale, 0.2, 1.0, "%.2f") then
			timestamp.scale = clamp(timestamp_scale[0], 0.2, 1.0)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_57"))

		aligned_text(L("vip_and_ad_chat.text.text_58"), 180)
		imgui.PushItemWidth(200)
		local timestamp_padding = ui_float("timestamp_padding", timestamp.padding or 0)
		if imgui.SliderFloat("##timestamp_padding", timestamp_padding, 0, 10, L("vip_and_ad_chat.text.text_1f")) then
			timestamp.padding = clamp(timestamp_padding[0], 0, 10)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_59"))

		aligned_text(L("vip_and_ad_chat.text.text_60"), 180)
		imgui.PushItemWidth(200)
		local timestamp_offset_y = ui_float("timestamp_offset_y", timestamp.offset_y or 0)
		if imgui.SliderFloat("##timestamp_offset_y", timestamp_offset_y, -10, 10, L("vip_and_ad_chat.text.text_1f")) then
			timestamp.offset_y = clamp(timestamp_offset_y[0], -10, 10)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_61"))
	elseif nav.page == SETTINGS_SECTION_LIMITS then
		aligned_text(L("vip_and_ad_chat.text.vip"), 150)
		imgui.PushItemWidth(150)
		local vip_limit = ui_int("vip_limit", tonumber(config.vip_limit) or default_config.vip_limit or 100)
		if imgui.InputInt("##vip_limit", vip_limit) then
			config.vip_limit = clamp(vip_limit[0], 10, 2000)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_62"))
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.vip_63"))

		aligned_text(L("vip_and_ad_chat.text.ad"), 150)
		imgui.PushItemWidth(150)
		local ad_limit = ui_int("ad_limit", tonumber(config.ad_limit) or default_config.ad_limit or 100)
		if imgui.InputInt("##ad_limit", ad_limit) then
			config.ad_limit = clamp(ad_limit[0], 10, 2000)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_62"))
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.ad_64"))

		aligned_text(L("vip_and_ad_chat.text.all"), 150)
		imgui.PushItemWidth(150)
		local all_limit = ui_int("all_limit", tonumber(config.all_limit) or default_config.all_limit or 200)
		if imgui.InputInt("##all_limit", all_limit) then
			config.all_limit = clamp(all_limit[0], 10, 2000)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_62"))
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.vip_ad"))
	elseif nav.page == SETTINGS_SECTION_FILTERS then
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.vip_65"))
		imgui.TextWrapped(L("vip_and_ad_chat.text.vip_66"))
		imgui.Spacing()
		imgui.TextWrapped(L("vip_and_ad_chat.text.lua"))
		imgui.Spacing()

		vip_patterns_last_serialized = sync_list_buffer(config.vip or {}, vip_patterns_buf, vip_patterns_last_serialized)

		imgui.PushItemWidth(-1)
		imgui.InputTextMultiline(
			"##vipPatterns",
			vip_patterns_buf,
			ffi.sizeof(vip_patterns_buf),
			imgui.ImVec2(0, 180)
		)
		imgui.PopItemWidth()

		if imgui.Button(L("vip_and_ad_chat.text.vip_patterns"), imgui.ImVec2(120, 0)) then
			local next_patterns = apply_pattern_list_from_buffer(vip_patterns_buf)
			config.vip = next_patterns
			vip_patterns_last_serialized = serialize_string_list(next_patterns)
			module.save()
		end
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.vip_67"))

		imgui.SameLine()
		if imgui.Button(L("vip_and_ad_chat.text.vip_patterns_reset"), imgui.ImVec2(120, 0)) then
			config.vip = clone_table(default_config.vip or {})
			local reset_serialized = serialize_string_list(config.vip)
			fill_buf_utf8(vip_patterns_buf, reset_serialized)
			vip_patterns_last_serialized = reset_serialized
			module.save()
		end
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_68"))
		imgui.Separator()
		config.ad = config.ad or clone_table(default_config.ad)
		config.ad.main = config.ad.main or clone_table(default_config.ad.main)
		config.ad.pre_edit = config.ad.pre_edit or clone_table(default_config.ad.pre_edit)
		config.ad.edited = config.ad.edited or clone_table(default_config.ad.edited)

		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.ad_templates"))
		imgui.TextWrapped(L("vip_and_ad_chat.text.ad_templates_desc"))
		imgui.Spacing()
		imgui.TextWrapped(L("vip_and_ad_chat.text.lua"))
		imgui.Spacing()

		local changed
		config.ad.main, ad_main_patterns_last_serialized, changed = draw_pattern_list_editor({
			title_key = "vip_and_ad_chat.text.ad_templates_main",
			desc_key = "vip_and_ad_chat.text.ad_templates_main_desc",
			list = config.ad.main,
			defaults = default_config.ad.main,
			buf = ad_main_patterns_buf,
			last_serialized = ad_main_patterns_last_serialized,
			input_id = "##adMainPatterns",
			apply_id = "##adMainPatternsApply",
			reset_id = "##adMainPatternsReset",
			height = 120,
		})
		if changed then
			module.save()
		end

		imgui.Spacing()
		config.ad.pre_edit, ad_pre_edit_patterns_last_serialized, changed = draw_pattern_list_editor({
			title_key = "vip_and_ad_chat.text.ad_templates_pre_edit",
			desc_key = "vip_and_ad_chat.text.ad_templates_pre_edit_desc",
			list = config.ad.pre_edit,
			defaults = default_config.ad.pre_edit,
			buf = ad_pre_edit_patterns_buf,
			last_serialized = ad_pre_edit_patterns_last_serialized,
			input_id = "##adPreEditPatterns",
			apply_id = "##adPreEditPatternsApply",
			reset_id = "##adPreEditPatternsReset",
			height = 90,
		})
		if changed then
			module.save()
		end

		imgui.Spacing()
		config.ad.edited, ad_edited_patterns_last_serialized, changed = draw_pattern_list_editor({
			title_key = "vip_and_ad_chat.text.ad_templates_edited",
			desc_key = "vip_and_ad_chat.text.ad_templates_edited_desc",
			list = config.ad.edited,
			defaults = default_config.ad.edited,
			buf = ad_edited_patterns_buf,
			last_serialized = ad_edited_patterns_last_serialized,
			input_id = "##adEditedPatterns",
			apply_id = "##adEditedPatternsApply",
			reset_id = "##adEditedPatternsReset",
			height = 90,
		})
		if changed then
			module.save()
		end

		imgui.Separator()
		imgui.TextColored(imgui.ImVec4(0.5, 0.5, 0.5, 1), L("vip_and_ad_chat.text.text_69"))
		imgui.TextWrapped(L("vip_and_ad_chat.text.text_70"))
		imgui.Spacing()

		highlight_words_last_serialized = sync_list_buffer(
			config.highlightWords or {},
			highlight_words_buf,
			highlight_words_last_serialized
		)
		imgui.PushItemWidth(-1)
		imgui.InputTextMultiline(
			"##highlightWords",
			highlight_words_buf,
			ffi.sizeof(highlight_words_buf),
			imgui.ImVec2(0, 120)
		)
		imgui.PopItemWidth()

		if imgui.Button(L("vip_and_ad_chat.text.highlight_words"), imgui.ImVec2(120, 0)) then
			local raw = ffi.string(highlight_words_buf)
			local next_words = {}
			local seen = {}
			for line in raw:gmatch("[^\r\n]+") do
				local trimmed = line:match("^%s*(.-)%s*$")
				if trimmed ~= "" and not seen[trimmed] then
					seen[trimmed] = true
					next_words[#next_words + 1] = trimmed
				end
			end
			config.highlightWords = next_words
			highlight_words_last_serialized = table.concat(next_words, "\n")
			module.save()
		end
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_71"))
	elseif nav.page == SETTINGS_SECTION_POPUP then
		config.popup = config.popup or clone_table(default_config.popup)
		local popup_cfg = config.popup

		imgui.TextWrapped(L("vip_and_ad_chat.text.text_72"))
		imgui.Spacing()

		aligned_text(L("vip_and_ad_chat.text.text_73"), 180)
		imgui.PushItemWidth(150)
		local popup_min_w = ui_int("popup_min_w", popup_cfg.min_w or 320)
		if imgui.DragInt("##popup_min_w", popup_min_w, 1, 200, 1000) then
			popup_cfg.min_w = clamp(popup_min_w[0], 200, 1000)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		aligned_text(L("vip_and_ad_chat.text.text_74"), 180)
		imgui.PushItemWidth(150)
		local popup_max_w = ui_int("popup_max_w", popup_cfg.max_w or 900)
		if imgui.DragInt("##popup_max_w", popup_max_w, 1, 400, 1920) then
			popup_cfg.max_w = clamp(popup_max_w[0], 400, 1920)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.Text(L("vip_and_ad_chat.text.text_31"))

		aligned_text(L("vip_and_ad_chat.text.text_75"), 180)
		imgui.PushItemWidth(150)
		local popup_min_lines = ui_int("popup_min_lines", popup_cfg.min_lines or 3)
		if imgui.DragInt("##popup_min_lines", popup_min_lines, 1, 1, 20) then
			popup_cfg.min_lines = clamp(popup_min_lines[0], 1, 20)
			module.save()
		end
		imgui.PopItemWidth()

		aligned_text(L("vip_and_ad_chat.text.text_76"), 180)
		imgui.PushItemWidth(150)
		local popup_max_lines = ui_int("popup_max_lines", popup_cfg.max_lines or 14)
		if imgui.DragInt("##popup_max_lines", popup_max_lines, 1, 3, 50) then
			popup_cfg.max_lines = clamp(popup_max_lines[0], 3, 50)
			module.save()
		end
		imgui.PopItemWidth()

		aligned_text(L("vip_and_ad_chat.text.text_77"), 180)
		imgui.PushItemWidth(150)
		local popup_chars_per_line = ui_int("popup_chars_per_line", popup_cfg.chars_per_line or 70)
		if imgui.DragInt("##popup_chars_per_line", popup_chars_per_line, 1, 30, 150) then
			popup_cfg.chars_per_line = clamp(popup_chars_per_line[0], 30, 150)
			module.save()
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		help_marker(L("vip_and_ad_chat.text.text_78"))
	end
end

function module.DrawSettingsWindow()
	if not settings_open[0] then
		settings_window_nav.page = 0
		return
	end
	imgui.SetNextWindowSize(imgui.ImVec2(650, 600), imgui.Cond.FirstUseEver)
	if imgui.Begin(L("vip_and_ad_chat.text.vip_ad_79"), settings_open) then
		draw_settings_content(settings_window_nav)
		if mimgui_funcs and mimgui_funcs.clampCurrentWindowToScreen then
			mimgui_funcs.clampCurrentWindowToScreen(5)
		end
	end
	imgui.End()
	if not settings_open[0] then
		settings_window_nav.page = 0
	end
	flush_save_if_due(false)
end

function module.DrawSettingsInline()
	draw_settings_content(settings_inline_nav)
	flush_save_if_due(false)
end

function module.attachModules(mod)
	syncDependencies(mod)
	config_manager_ref = mod.config_manager
	event_bus_ref = mod.event_bus
	if event_bus_ref then
		event_bus_ref.offByOwner("VIPandADchat")
	end
	if not moduleInitialized then
		if config_manager_ref then
			config_manager_ref.register("VIPandADchat", {
				path = JSON_PATH_REL,
				defaults = {},
				loader = function(path, defaults)
					module.load()
					return config
				end,
				serialize = function(data)
					local serialized = deep_copy_sanitized(data)
					serialized.enabled = nil
					return serialized
				end,
			})
		else
			start_save_worker()
			module.load()
		end
		moduleInitialized = true
	end
end

function module.onTerminate()
	if hudFrame and type(hudFrame.Unsubscribe) == "function" then
		pcall(hudFrame.Unsubscribe, hudFrame)
	end
	if event_bus_ref then
		event_bus_ref.offByOwner("VIPandADchat")
	end
	flush_save_if_due(true)
end

return module
