local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

-- SMIHelp/settings_ui.lua — Панель настроек СМИ Хелпера
local M = {}

local ffi = require("ffi")
local imgui = require("mimgui")
local new = imgui.new
local str = ffi.string
local sizeof = ffi.sizeof
local floor = math.floor
local min = math.min
local max = math.max

local ImVec2 = imgui.ImVec2
local ImVec4 = imgui.ImVec4
local InputTextFlags = imgui.InputTextFlags
local StyleVar = imgui.StyleVar
local Col = imgui.Col

local ok_fa, fa = pcall(require, "fAwesome7")
if not ok_fa or type(fa) ~= "table" then
	fa = setmetatable({}, {
		__index = function()
			return ""
		end,
	})
end

local ctx -- устанавливается через M.init
local constructor -- ссылка на конструктор

-- ========= СЕРИАЛИЗАЦИЯ НАСТРОЕК =========
local function autocorrect_to_string(list)
	local lines = {}
	if type(list) == "table" then
		for _, pair in ipairs(list) do
			local find = pair[1] or ""
			local repl = pair[2] or ""
			table.insert(lines, find .. "=" .. repl)
		end
	end
	return table.concat(lines, "\n")
end

local function parse_autocorrect(text)
	local res = {}
	text = tostring(text or "")
	for line in text:gmatch("[^\n]+") do
		local find, repl = line:match("^(.-)=(.*)$")
		if find and find ~= "" then
			table.insert(res, { find, repl or "" })
		end
	end
	return res
end

local function price_type_map_to_string(map, types)
	local lines = {}
	if type(map) ~= "table" then
		map = {}
	end
	for _, type_name in ipairs(types or {}) do
		local mode = map[type_name] or "both"
		table.insert(lines, type_name .. "=" .. mode)
	end
	return table.concat(lines, "\n")
end

local function parse_price_type_map(text)
	local res = {}
	text = tostring(text or "")
	for line in text:gmatch("[^\n]+") do
		local key, val = line:match("^(.-)=(.*)$")
		key = ctx.trim(key)
		val = ctx.trim(val)
		if key ~= "" then
			if val ~= "buy" and val ~= "sell" and val ~= "both" then
				val = "both"
			end
			res[key] = val
		end
	end
	return res
end

-- helpers for editable text buffers
local function buf_ensure(tbl, key, size)
	size = size or 256
	local entry = tbl[key]
	if not entry then
		entry = { buf = new.char[size](), size = size }
		tbl[key] = entry
	elseif entry.size < size then
		local content = str(entry.buf)
		local new_buf = new.char[size]()
		imgui.StrCopy(new_buf, content)
		entry.buf = new_buf
		entry.size = size
	end
	return entry
end

local function buf_set(tbl, key, text)
	local entry = buf_ensure(tbl, key, #text + 1)
	imgui.StrCopy(entry.buf, text)
	return entry
end

local function buf_maybe_grow(entry)
	local content = str(entry.buf)
	if #content + 1 >= entry.size then
		local new_size = entry.size * 2
		local new_buf = new.char[new_size]()
		imgui.StrCopy(new_buf, content)
		entry.buf = new_buf
		entry.size = new_size
	end
	return content
end

-- ========= ОБЪЕКТЫ: сериализация пар {short, full} =========
local function objects_to_cache(objs)
	local out = {}
	for _, p in ipairs(objs or {}) do
		if type(p) == "table" then
			local s = ctx.trim(tostring(p[1] or ""))
			local f = ctx.trim(tostring(p[2] or s))
			if s ~= "" then
				out[#out + 1] = (s == f) and s or (s .. "=" .. f)
			end
		elseif type(p) == "string" then
			local ps = ctx.trim(p)
			if ps ~= "" then
				out[#out + 1] = ps
			end
		end
	end
	return out
end

local function cache_to_objects(cache)
	local out = {}
	for _, item in ipairs(cache or {}) do
		item = ctx.trim(tostring(item or ""))
		if item ~= "" then
			local s, f = item:match("^(.-)=(.+)$")
			if s and f then
				s, f = ctx.trim(s), ctx.trim(f)
				if s ~= "" then
					out[#out + 1] = { s, f }
				end
			else
				out[#out + 1] = { item, item }
			end
		end
	end
	return out
end

local function DrawSettingsUI_Refactored()
	local SMIHelp = ctx.SMIHelp
	local Config = ctx.Config
	local SMILive = ctx.SMILive

	local function clone_templates(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for _, tpl in ipairs(src) do
			if type(tpl) == "table" then
				local copy = { category = tpl.category, texts = {} }
				if type(tpl.texts) == "table" then
					for _, g in ipairs(tpl.texts) do
						if type(g) == "table" then
							local group = {}
							for _, s in ipairs(g) do
								if type(s) == "string" then
									group[#group + 1] = s
								end
							end
							if #group > 0 then
								copy.texts[#copy.texts + 1] = group
							end
						elseif type(g) == "string" then
							copy.texts[#copy.texts + 1] = { g }
						end
					end
				elseif type(tpl.text) == "string" then
					copy.texts[#copy.texts + 1] = { tpl.text }
				end
				out[#out + 1] = copy
			end
		end
		return out
	end

	local function clone_string_array(src)
		local out = {}
		for i, v in ipairs(src or {}) do
			out[i] = v
		end
		return out
	end

	local function restore_string_array(dst, src)
		for i = #dst, 1, -1 do
			dst[i] = nil
		end
		for i, v in ipairs(src or {}) do
			dst[i] = v
		end
	end

	if not SMIHelp._settings then
		SMIHelp._settings = {
			list_strs = {
				type_buttons = "",
				objects = "",
				prices_buy = "",
				prices_sell = "",
				currencies = "",
				addons = "",
			},
			autocorrect = new.char[1024](),
			price_type_map = new.char[1024](),
			history_limit = new.int(Config.data.history_limit or 100),
			nick_memory_limit = new.int(Config.data.nick_memory_limit or 100),
			vip_timer_enabled = new.bool(SMIHelp.timer_send_enabled),
			vip_timer_delay = new.int(SMIHelp.timer_send_delay or 10),
			btn_timer_enabled = new.bool(SMIHelp.btn_timer_enabled),
			btn_timer_delay = new.int(SMIHelp.btn_timer_delay or 3),
			timer_news_delay = new.int(SMIHelp.timer_news_delay or 4),
			objects_use_full = new.bool(Config.data.objects_use_full ~= false),
			templates_list = clone_templates(Config.data.templates or {}),
			tpl_input = {},
			tpl_multiline = {},
			tpl_edit_mode = {},
			tpl_edit_buf = {},
			tpl_add_mode = {},
			dirty = false,
			tab_dirty = {},
			active_list_cat = 2,
			list_cache = {},
			list_cache_loaded = {},
			list_state = {},
			list_export_buf = {},
			autocorrect_rules = {},
			ac_find_buf = {},
			ac_repl_buf = {},
			ac_new_find = { buf = new.char[128](), size = 128 },
			ac_new_repl = { buf = new.char[128](), size = 128 },
			ac_need_rebuild = true,
			ac_error = "",
			import_export_buf = {},
			import_export_error = "",
			tpl_active_category = 1,
			tpl_category_filter = new.char[96](),
			tpl_template_filter = new.char[96](),
			tpl_category_new = { buf = new.char[128](), size = 128 },
			tpl_category_rename = { buf = new.char[128](), size = 128 },
			tpl_delete_target = { kind = "", cat_idx = 0, tpl_idx = 0 },
			tpl_action_error = "",
		}
		local _ls = SMIHelp._settings.list_strs
		_ls.type_buttons = table.concat(Config.data.type_buttons or {}, ",")
		_ls.objects      = table.concat(objects_to_cache(Config.data.objects), ",")
		_ls.prices_buy   = table.concat(Config.data.prices_buy or {}, ",")
		_ls.prices_sell  = table.concat(Config.data.prices_sell or {}, ",")
		_ls.currencies   = table.concat(Config.data.currencies or {}, ",")
		_ls.addons       = table.concat(Config.data.addons or {}, ",")
		imgui.StrCopy(SMIHelp._settings.autocorrect, autocorrect_to_string(Config.data.autocorrect))
		imgui.StrCopy(
			SMIHelp._settings.price_type_map,
			price_type_map_to_string(Config.data.price_type_map, Config.data.type_buttons)
		)
		SMIHelp._settings.autocorrect_rules = parse_autocorrect(str(SMIHelp._settings.autocorrect))
	end

	local S = SMIHelp._settings
	-- совместимость: если _settings существует, но list_strs ещё нет (старый формат)
	if not S.list_strs then
		S.list_strs = {
			type_buttons = table.concat(Config.data.type_buttons or {}, ","),
			objects      = table.concat(objects_to_cache(Config.data.objects), ","),
			prices_buy   = table.concat(Config.data.prices_buy or {}, ","),
			prices_sell  = table.concat(Config.data.prices_sell or {}, ","),
			currencies   = table.concat(Config.data.currencies or {}, ","),
			addons       = table.concat(Config.data.addons or {}, ","),
		}
		S.list_cache_loaded = {}
	end

	if not S.objects_use_full then
		S.objects_use_full = new.bool(Config.data.objects_use_full ~= false)
	end

	S.dirty = S.dirty or false
	S.tab_dirty = S.tab_dirty or {}
	S.active_list_cat = S.active_list_cat or 2
	S.list_cache = S.list_cache or {}
	S.list_cache_loaded = S.list_cache_loaded or {}
	S.list_state = S.list_state or {}
	S.list_export_buf = S.list_export_buf or {}
	S.autocorrect_rules = S.autocorrect_rules or parse_autocorrect(str(S.autocorrect))
	S.ac_find_buf = S.ac_find_buf or {}
	S.ac_repl_buf = S.ac_repl_buf or {}
	if not S.ac_new_find or not S.ac_new_find.buf then
		S.ac_new_find = { buf = new.char[128](), size = 128 }
	end
	if not S.ac_new_repl or not S.ac_new_repl.buf then
		S.ac_new_repl = { buf = new.char[128](), size = 128 }
	end
	if S.ac_need_rebuild == nil then
		S.ac_need_rebuild = true
	end
	S.ac_error = S.ac_error or ""
	S.import_export_buf = S.import_export_buf or {}
	S.import_export_error = S.import_export_error or ""
	S.tpl_add_mode = S.tpl_add_mode or {}
	S.tpl_active_category = S.tpl_active_category or 1
	if not S.tpl_category_filter then
		S.tpl_category_filter = new.char[96]()
	end
	if not S.tpl_template_filter then
		S.tpl_template_filter = new.char[96]()
	end
	if not S.tpl_category_new or not S.tpl_category_new.buf then
		S.tpl_category_new = { buf = new.char[128](), size = 128 }
	end
	if not S.tpl_category_rename or not S.tpl_category_rename.buf then
		S.tpl_category_rename = { buf = new.char[128](), size = 128 }
	end
	S.tpl_delete_target = S.tpl_delete_target or { kind = "", cat_idx = 0, tpl_idx = 0 }
	S.tpl_action_error = S.tpl_action_error or ""

	local LISTS = {
		{ id = 1, key = "type_buttons", name = L("smi_help.settings_ui.text.text"),      allow_duplicates = false },
		{ id = 2, key = "objects",      name = L("smi_help.settings_ui.text.text_1"),   allow_duplicates = false },
		{ id = 3, key = "prices_buy",   name = L("smi_help.settings_ui.text.buy"),  allow_duplicates = false },
		{ id = 4, key = "prices_sell",  name = L("smi_help.settings_ui.text.sell"), allow_duplicates = false },
		{ id = 5, key = "currencies",   name = L("smi_help.settings_ui.text.text_2"),    allow_duplicates = false },
		{ id = 6, key = "addons",       name = L("smi_help.settings_ui.text.text_3"),allow_duplicates = false },
	}
	local LIST_BY_KEY = {}
	for _, cat in ipairs(LISTS) do
		LIST_BY_KEY[cat.key] = cat
	end
	if S.active_list_cat < 1 or S.active_list_cat > #LISTS then
		S.active_list_cat = 1
	end

	local function set_dirty()
		S.dirty = true
	end

	local TAB_KEYS = {
		"general",
		"timers",
		"lists",
		"templates",
		"autocorrect",
		"import_export",
	}
	for _, key in ipairs(TAB_KEYS) do
		if S.tab_dirty[key] == nil then
			S.tab_dirty[key] = false
		end
	end

	local function set_tab_dirty(tab_key)
		if tab_key and S.tab_dirty[tab_key] ~= nil then
			S.tab_dirty[tab_key] = true
		end
		set_dirty()
	end

	local function set_many_tabs_dirty(tab_keys)
		for _, key in ipairs(tab_keys or {}) do
			if S.tab_dirty[key] ~= nil then
				S.tab_dirty[key] = true
			end
		end
		set_dirty()
	end

	local function reset_tab_dirty()
		for _, key in ipairs(TAB_KEYS) do
			S.tab_dirty[key] = false
		end
	end

	local function tab_caption(base_text, tab_key)
		if S.tab_dirty[tab_key] then
			return base_text .. "*"
		end
		return base_text
	end

	local function fa_icon_safe(name, fallback)
		local glyph = fa and fa[name] or ""
		if type(glyph) ~= "string" or glyph == "" then
			return fallback or ""
		end
		return glyph
	end

	local ICON_UP = fa_icon_safe("ARROW_UP", "^")
	local ICON_DOWN = fa_icon_safe("ARROW_DOWN", "v")
	local ICON_DELETE = fa_icon_safe("TRASH_CAN", "X")
	local ICON_EDIT = fa_icon_safe("PEN_TO_SQUARE", "E")
	local ICON_DUPLICATE = fa_icon_safe("COPY", "C")

	local function clamp_int(ptr, mn, mx)
		if ptr[0] < mn then
			ptr[0] = mn
		end
		if ptr[0] > mx then
			ptr[0] = mx
		end
	end

	local function set_cstring(buf, text)
		text = tostring(text or "")
		if #text >= sizeof(buf) then
			return false
		end
		imgui.StrCopy(buf, text)
		return true
	end

	local function parse_buf_to_list(buf)
		return ctx.funcs.parseList(str(buf))
	end

	local function ensure_list_state(cat_id)
		local state = S.list_state[cat_id]
		if not state then
			state = {
				filter = new.char[96](),
				new_value = new.char[256](),
				import_buf = new.char[4096](),
				error = "",
				focus_new = false,
			}
			S.list_state[cat_id] = state
		end
		if type(S.list_cache[cat_id]) ~= "table" then
			S.list_cache[cat_id] = {}
		end
		return state
	end

	local function sync_list_cache(cat)
		ensure_list_state(cat.id)
		if not S.list_cache_loaded[cat.id] then
			S.list_cache[cat.id] = ctx.funcs.parseList(S.list_strs[cat.key] or "")
			S.list_cache_loaded[cat.id] = true
		end
	end

	local function apply_list_cache(cat)
		local state = ensure_list_state(cat.id)
		S.list_strs[cat.key] = table.concat(S.list_cache[cat.id] or {}, ",")
		state.error = ""
		return true
	end

	local function list_contains(list, value)
		for _, v in ipairs(list or {}) do
			if v == value then
				return true
			end
		end
		return false
	end

	local function rebuild_ac_buffers()
		S.ac_find_buf = {}
		S.ac_repl_buf = {}
		for i, pair in ipairs(S.autocorrect_rules or {}) do
			local find_v = tostring(pair[1] or "")
			local repl_v = tostring(pair[2] or "")
			local find_entry = buf_ensure(S.ac_find_buf, i, max(128, #find_v + 16))
			local repl_entry = buf_ensure(S.ac_repl_buf, i, max(128, #repl_v + 16))
			imgui.StrCopy(find_entry.buf, find_v)
			imgui.StrCopy(repl_entry.buf, repl_v)
		end
	end

	local function refresh_ac_from_buffer()
		S.autocorrect_rules = parse_autocorrect(str(S.autocorrect))
		S.ac_need_rebuild = true
		S.ac_error = ""
	end

	local function sync_ac_to_buffer()
		local lines = {}
		for _, pair in ipairs(S.autocorrect_rules or {}) do
			local find = ctx.trim(tostring(pair[1] or ""))
			local repl = tostring(pair[2] or "")
			if find ~= "" then
				lines[#lines + 1] = find .. "=" .. repl
			end
		end
		local payload = table.concat(lines, "\n")
		if not set_cstring(S.autocorrect, payload) then
			S.ac_error = L("smi_help.settings_ui.text.text_4")
			return false
		end
		S.ac_error = ""
		return true
	end

	local function save_settings()
		clamp_int(S.history_limit, 0, 9999)
		clamp_int(S.nick_memory_limit, 0, 9999)
		clamp_int(S.vip_timer_delay, 0, 600)
		clamp_int(S.btn_timer_delay, 0, 600)
		clamp_int(S.timer_news_delay, 0, 600)

		local ls = S.list_strs
		local type_buttons = ctx.funcs.parseList(ls.type_buttons)
		local prices_buy = ctx.funcs.parseList(ls.prices_buy)
		local prices_sell = ctx.funcs.parseList(ls.prices_sell)
		local vip_enabled = S.vip_timer_enabled[0]
		local vip_delay = S.vip_timer_delay[0]
		local btn_enabled = S.btn_timer_enabled[0]
		local btn_delay = S.btn_timer_delay[0]

		Config.data.type_buttons = type_buttons
		Config.data.objects = cache_to_objects(ctx.funcs.parseList(ls.objects))
		Config.data.prices_buy = prices_buy
		Config.data.prices_sell = prices_sell
		Config.data.prices = constructor.merge_price_lists(prices_buy, prices_sell)
		Config.data.currencies = ctx.funcs.parseList(ls.currencies)
		Config.data.addons = ctx.funcs.parseList(ls.addons)
		Config.data.price_type_map = parse_price_type_map(str(S.price_type_map))
		Config.data.price_type_map = ctx.ensure_price_type_map(Config.data.price_type_map, type_buttons)
		Config.data.autocorrect = parse_autocorrect(str(S.autocorrect))
		Config.data.templates = S.templates_list
		Config.data.history_limit = S.history_limit[0]
		Config.data.nick_memory_limit = S.nick_memory_limit[0]
		Config.data.vip_timer_enabled = vip_enabled
		Config.data.vip_timer_delay = vip_delay
		Config.data.btn_timer_enabled = btn_enabled
		Config.data.btn_timer_delay = btn_delay
		Config.data.objects_use_full = S.objects_use_full[0]

		SMIHelp.timer_send_enabled = vip_enabled
		SMIHelp.timer_send_delay = vip_delay
		SMIHelp.btn_timer_enabled = btn_enabled
		SMIHelp.btn_timer_delay = btn_delay
		SMIHelp.timer_news_delay = S.timer_news_delay[0]

		Config:save()
		S.dirty = false
		reset_tab_dirty()
	end

	local function normalize_string_list(value)
		if type(value) ~= "table" then
			return nil
		end
		local out = {}
		for _, item in ipairs(value) do
			if type(item) == "string" then
				item = ctx.trim(item)
				if item ~= "" then
					out[#out + 1] = item
				end
			end
		end
		return out
	end

	local function normalize_objects_list(value)
		if type(value) ~= "table" then
			return nil
		end
		local out = {}
		for _, item in ipairs(value) do
			if type(item) == "table" then
				local s = ctx.trim(tostring(item[1] or item.short or ""))
				local f = ctx.trim(tostring(item[2] or item.full or s))
				if s ~= "" then
					out[#out + 1] = (s == f) and s or (s .. "=" .. f)
				end
			elseif type(item) == "string" then
				local v = ctx.trim(item)
				if v ~= "" then
					out[#out + 1] = v
				end
			end
		end
		return #out > 0 and out or nil
	end

	local function collect_export_payload()
		local ls = S.list_strs
		local type_buttons = ctx.funcs.parseList(ls.type_buttons)
		return {
			version = 1,
			type_buttons = type_buttons,
			objects = cache_to_objects(ctx.funcs.parseList(ls.objects)),
			prices_buy = ctx.funcs.parseList(ls.prices_buy),
			prices_sell = ctx.funcs.parseList(ls.prices_sell),
			currencies = ctx.funcs.parseList(ls.currencies),
			addons = ctx.funcs.parseList(ls.addons),
			price_type_map = ctx.ensure_price_type_map(parse_price_type_map(str(S.price_type_map)), type_buttons),
			autocorrect = parse_autocorrect(str(S.autocorrect)),
			templates = clone_templates(S.templates_list),
			history_limit = S.history_limit[0],
			nick_memory_limit = S.nick_memory_limit[0],
			vip_timer_enabled = S.vip_timer_enabled[0],
			vip_timer_delay = S.vip_timer_delay[0],
			btn_timer_enabled = S.btn_timer_enabled[0],
			btn_timer_delay = S.btn_timer_delay[0],
			timer_news_delay = S.timer_news_delay[0],
			objects_use_full = S.objects_use_full[0],
		}
	end

	local function apply_import_payload(payload)
		if type(payload) ~= "table" then
			return false, L("smi_help.settings_ui.text.text_5")
		end

		local pending = {}
		local imported_lists = {}
		for key, cat in pairs(LIST_BY_KEY) do
			local list = (key == "objects") and normalize_objects_list(payload[key]) or normalize_string_list(payload[key])
			if list then
				local serialized = table.concat(list, ",")
				imported_lists[key] = list
				pending[#pending + 1] = { cat = cat, text = serialized }
			end
		end

		local ac_text = nil
		if type(payload.autocorrect) == "table" then
			ac_text = autocorrect_to_string(payload.autocorrect)
		elseif type(payload.autocorrect) == "string" then
			ac_text = payload.autocorrect
		end
		if ac_text and #ac_text >= sizeof(S.autocorrect) then
			return false, L("smi_help.settings_ui.text.text_6")
		end

		local map_text = nil
		local map_types = imported_lists.type_buttons or ctx.funcs.parseList(S.list_strs.type_buttons)
		if type(payload.price_type_map) == "table" then
			map_text = price_type_map_to_string(ctx.ensure_price_type_map(payload.price_type_map, map_types), map_types)
		elseif type(payload.price_type_map) == "string" then
			map_text = payload.price_type_map
		end
		if map_text and #map_text >= sizeof(S.price_type_map) then
			return false, L("smi_help.settings_ui.text.text_7")
		end

		for _, item in ipairs(pending) do
			S.list_strs[item.cat.key] = item.text
			S.list_cache_loaded[item.cat.id] = false
		end
		if ac_text then
			imgui.StrCopy(S.autocorrect, ac_text)
			refresh_ac_from_buffer()
		end
		if map_text then
			imgui.StrCopy(S.price_type_map, map_text)
		end

		if type(payload.templates) == "table" then
			S.templates_list = clone_templates(payload.templates)
			S.tpl_input = {}
			S.tpl_multiline = {}
			S.tpl_edit_mode = {}
			S.tpl_edit_buf = {}
		end

		if type(payload.history_limit) == "number" then
			S.history_limit[0] = floor(payload.history_limit)
		end
		if type(payload.nick_memory_limit) == "number" then
			S.nick_memory_limit[0] = floor(payload.nick_memory_limit)
		end
		if type(payload.vip_timer_enabled) == "boolean" then
			S.vip_timer_enabled[0] = payload.vip_timer_enabled
		end
		if type(payload.vip_timer_delay) == "number" then
			S.vip_timer_delay[0] = floor(payload.vip_timer_delay)
		end
		if type(payload.btn_timer_enabled) == "boolean" then
			S.btn_timer_enabled[0] = payload.btn_timer_enabled
		end
		if type(payload.btn_timer_delay) == "number" then
			S.btn_timer_delay[0] = floor(payload.btn_timer_delay)
		end
		if type(payload.timer_news_delay) == "number" then
			S.timer_news_delay[0] = floor(payload.timer_news_delay)
		end
		if type(payload.objects_use_full) == "boolean" then
			S.objects_use_full[0] = payload.objects_use_full
		end

		clamp_int(S.history_limit, 0, 9999)
		clamp_int(S.nick_memory_limit, 0, 9999)
		clamp_int(S.vip_timer_delay, 0, 600)
		clamp_int(S.btn_timer_delay, 0, 600)
		clamp_int(S.timer_news_delay, 0, 600)

		for _, cat in ipairs(LISTS) do
			sync_list_cache(cat)
		end

		set_many_tabs_dirty(TAB_KEYS)
		return true
	end

	local function draw_toolbar()
		local style = imgui.GetStyle()
		local spacing = style.ItemSpacing.x
		local editor_w = imgui.CalcTextSize(L("smi_help.settings_ui.text.text_8")).x + style.FramePadding.x * 2
		local live_w = imgui.CalcTextSize(L("smi_help.settings_ui.text.text_9")).x + style.FramePadding.x * 2
		local save_w = imgui.CalcTextSize(L("smi_help.settings_ui.text.text_10")).x + style.FramePadding.x * 2
		local dirty_w = S.dirty and (imgui.CalcTextSize("*").x + spacing) or 0
		local total_w = editor_w + live_w + save_w + spacing * 2 + dirty_w

		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_11"))
		local x = imgui.GetWindowWidth() - style.WindowPadding.x - total_w
		if x < imgui.GetCursorPosX() then
			x = imgui.GetCursorPosX()
		end
		imgui.SameLine(x)

		if imgui.Button(L("smi_help.settings_ui.text.smi_toolbar_editor")) then
			constructor.OpenEditPreview()
		end
		imgui.SameLine()

		local can_live = SMILive and SMILive.OpenWindow
		if imgui.Button(L("smi_help.settings_ui.text.smi_toolbar_live")) then
			if SMILive and SMILive.OpenWindow then
				SMILive.OpenWindow()
			end
		end
		if not can_live and imgui.IsItemHovered() then
			if ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.smilive"))
			end
		end

		imgui.SameLine()
		if S.dirty then
			imgui.PushStyleColor(Col.Button, ImVec4(0.25, 0.49, 0.86, 1))
			imgui.PushStyleColor(Col.ButtonHovered, ImVec4(0.31, 0.58, 0.95, 1))
			imgui.PushStyleColor(Col.ButtonActive, ImVec4(0.22, 0.43, 0.76, 1))
			if imgui.Button(L("smi_help.settings_ui.text.smi_toolbar_save")) then
				save_settings()
			end
			imgui.PopStyleColor(3)
			imgui.SameLine()
			imgui.TextColored(ImVec4(1, 0.78, 0.32, 1), "*")
		else
			imgui.PushStyleVarFloat(StyleVar.Alpha, style.Alpha * 0.55)
			imgui.Button(L("smi_help.settings_ui.text.smi_toolbar_save"))
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_12"))
			end
			imgui.PopStyleVar()
		end
	end

	local function ListEditor(id, title, items_table, opts)
		opts = opts or {}
		local state = opts.state
		local allow_duplicates = opts.allow_duplicates == true
		local on_changed = opts.on_changed
		local on_dirty = opts.on_dirty
		if not state then
			return
		end

		local function commit(backup)
			if on_changed and on_changed() then
				state.error = ""
				if on_dirty then
					on_dirty()
				else
					set_dirty()
				end
				return true
			end
			if backup then
				restore_string_array(items_table, backup)
			end
			return false
		end

		imgui.TextUnformatted(title)
		imgui.Separator()

		imgui.PushItemWidth(-190)
		imgui.InputTextWithHint("##list_filter_" .. id, L("smi_help.settings_ui.text.text_13"), state.filter, sizeof(state.filter))
		imgui.PopItemWidth()
		imgui.SameLine()
		if imgui.Button(L("common.add_compact") .. "##list_focus_" .. id) then
			state.focus_new = true
		end
		imgui.SameLine()
		if imgui.Button(L("smi_help.settings_ui.text.list_import_btn") .. id) then
			imgui.OpenPopup("list_import_popup##" .. id)
		end
		imgui.SameLine()
		if imgui.Button(L("smi_help.settings_ui.text.list_export_btn") .. id) then
			imgui.OpenPopup("list_export_popup##" .. id)
		end

		if imgui.BeginPopup("list_import_popup##" .. id) then
			imgui.TextWrapped(L("smi_help.settings_ui.text.text_14"))
			imgui.InputTextMultiline("##list_import_text_" .. id, state.import_buf, sizeof(state.import_buf), ImVec2(0, 170))
			if imgui.Button(L("smi_help.settings_ui.text.list_import_apply") .. id) then
				local backup = clone_string_array(items_table)
				local changed = false
				for line in str(state.import_buf):gmatch("[^\r\n]+") do
					local v = ctx.trim(line)
					if v ~= "" then
						if allow_duplicates or not list_contains(items_table, v) then
							items_table[#items_table + 1] = v
							changed = true
						end
					end
				end
				if changed then
					if not commit(backup) then
						state.error = state.error ~= "" and state.error or L("smi_help.settings_ui.text.text_15")
					end
				else
					state.error = L("smi_help.settings_ui.text.text_16")
				end
				imgui.StrCopy(state.import_buf, "")
				imgui.CloseCurrentPopup()
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.list_import_close") .. id) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		if imgui.BeginPopup("list_export_popup##" .. id) then
			local out = table.concat(items_table or {}, "\n")
			local entry = buf_set(S.list_export_buf, id, out)
			imgui.InputTextMultiline(
				"##list_export_text_" .. id,
				entry.buf,
				entry.size,
				ImVec2(0, 170),
				InputTextFlags.ReadOnly
			)
			if imgui.SetClipboardText then
				if imgui.Button(L("smi_help.settings_ui.text.list_export_copy") .. id) then
					imgui.SetClipboardText(str(entry.buf))
				end
			else
				imgui.TextUnformatted(L("smi_help.settings_ui.text.text_17"))
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.list_export_close") .. id) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		if state.error and state.error ~= "" then
			imgui.TextColored(ImVec4(1, 0.48, 0.38, 1), state.error)
		end

		local frame_h = imgui.GetFrameHeightWithSpacing and imgui.GetFrameHeightWithSpacing() or 28
		imgui.BeginChild("list_items##" .. id, ImVec2(0, -(frame_h * 2.3)), true)
		imgui.Columns(2, "list_cols##" .. id, false)
		local col_w = imgui.GetWindowWidth() - 130
		if col_w < 80 then
			col_w = 80
		end
		imgui.SetColumnWidth(0, col_w)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_18"))
		imgui.NextColumn()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_19"))
		imgui.NextColumn()
		imgui.Separator()

		local filter = ctx.trim(str(state.filter))
		local filter_l = (ctx.tolower_utf8 and ctx.tolower_utf8(filter)) or filter:lower()
		local visible_indices = {}
		for i = 1, #items_table do
			local v = tostring(items_table[i] or "")
			local visible = true
			if filter_l ~= "" then
				local v_l = (ctx.tolower_utf8 and ctx.tolower_utf8(v)) or v:lower()
				visible = v_l:find(filter_l, 1, true) ~= nil
			end
			if visible then
				visible_indices[#visible_indices + 1] = i
			end
		end

		local visible_count = #visible_indices
		local modified = false
		local clipper = imgui.ImGuiListClipper(visible_count)
		while clipper:Step() do
			for vi = clipper.DisplayStart + 1, clipper.DisplayEnd do
				local i = visible_indices[vi]
				local v = tostring(items_table[i] or "")
				imgui.TextUnformatted(v)
				imgui.NextColumn()

				local action = nil
				if imgui.SmallButton(ICON_UP .. "##list_up_" .. id .. "_" .. i) and i > 1 then
					action = "up"
				end
				imgui.SameLine()
				if imgui.SmallButton(ICON_DOWN .. "##list_dn_" .. id .. "_" .. i) and i < #items_table then
					action = action or "down"
				end
				imgui.SameLine()
				if imgui.SmallButton(ICON_DELETE .. "##list_del_" .. id .. "_" .. i) then
					action = "del"
				end

				if action then
					local backup = clone_string_array(items_table)
					if action == "up" then
						items_table[i], items_table[i - 1] = items_table[i - 1], items_table[i]
					elseif action == "down" then
						items_table[i], items_table[i + 1] = items_table[i + 1], items_table[i]
					elseif action == "del" then
						table.remove(items_table, i)
					end
					if not commit(backup) then
						state.error = state.error ~= "" and state.error or L("smi_help.settings_ui.text.text_20")
					end
					modified = true
				end

				imgui.NextColumn()
				if modified then
					break
				end
			end
			if modified then
				break
			end
		end

		imgui.Columns(1)
		if visible_count == 0 then
			imgui.TextUnformatted(L("smi_help.settings_ui.text.text_21"))
		end
		imgui.EndChild()

		if state.focus_new then
			imgui.SetKeyboardFocusHere()
			state.focus_new = false
		end
		imgui.PushItemWidth(-120)
		local submit = imgui.InputTextWithHint(
			"##list_new_value_" .. id,
			L("smi_help.settings_ui.text.text_22"),
			state.new_value,
			sizeof(state.new_value),
			InputTextFlags.EnterReturnsTrue
		)
		imgui.PopItemWidth()
		imgui.SameLine()
		local clicked = imgui.Button(L("smi_help.settings_ui.text.list_add") .. id)
		if submit or clicked then
			local value = ctx.trim(str(state.new_value))
			if value == "" then
				state.error = L("smi_help.settings_ui.text.text_23")
			elseif (not allow_duplicates) and list_contains(items_table, value) then
				state.error = L("smi_help.settings_ui.text.text_24")
			else
				local backup = clone_string_array(items_table)
				items_table[#items_table + 1] = value
				if commit(backup) then
					state.error = ""
					imgui.StrCopy(state.new_value, "")
				else
					state.error = state.error ~= "" and state.error or L("smi_help.settings_ui.text.text_25")
				end
			end
		end
	end

	local function draw_general_tab()
		imgui.BeginChild("smi_settings_general", ImVec2(0, 0), false)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_26"))
		imgui.Separator()
		imgui.PushItemWidth(220)
		if imgui.InputInt(L("smi_help.settings_ui.text.text_27"), S.history_limit, 1, 1000) then
			set_tab_dirty("general")
		end
		clamp_int(S.history_limit, 0, 9999)
		if imgui.InputInt(L("smi_help.settings_ui.text.text_28"), S.nick_memory_limit, 1, 1000) then
			set_tab_dirty("general")
		end
		clamp_int(S.nick_memory_limit, 0, 9999)
		imgui.PopItemWidth()
		imgui.EndChild()
	end

	local function draw_timers_tab()
		imgui.BeginChild("smi_settings_timers", ImVec2(0, 0), false)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_29"))
		imgui.Separator()

		if imgui.Checkbox(L("smi_help.settings_ui.text.vip_vip_enabled"), S.vip_timer_enabled) then
			set_tab_dirty("timers")
		end
		imgui.SameLine()
		imgui.PushItemWidth(170)
		if imgui.InputInt(L("smi_help.settings_ui.text.vip_delay"), S.vip_timer_delay, 1, 60) then
			set_tab_dirty("timers")
		end
		imgui.PopItemWidth()
		clamp_int(S.vip_timer_delay, 0, 600)

		if imgui.Checkbox(L("smi_help.settings_ui.text.btn_enabled"), S.btn_timer_enabled) then
			set_tab_dirty("timers")
		end
		imgui.SameLine()
		imgui.PushItemWidth(170)
		if imgui.InputInt(L("smi_help.settings_ui.text.btn_delay"), S.btn_timer_delay, 1, 60) then
			set_tab_dirty("timers")
		end
		imgui.PopItemWidth()
		clamp_int(S.btn_timer_delay, 0, 600)

		imgui.Spacing()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_30"))
		imgui.PushItemWidth(170)
		if imgui.InputInt(L("smi_help.settings_ui.text.text_31"), S.timer_news_delay, 1, 60) then
			set_tab_dirty("timers")
		end
		imgui.PopItemWidth()
		clamp_int(S.timer_news_delay, 0, 600)

		imgui.EndChild()
	end

	local function draw_lists_tab()
		for _, cat in ipairs(LISTS) do
			sync_list_cache(cat)
		end

		imgui.BeginChild("smi_settings_lists", ImVec2(0, 0), false)
		imgui.BeginChild("smi_lists_left", ImVec2(210, 0), true)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_32"))
		imgui.Separator()
		for _, cat in ipairs(LISTS) do
			local selected = (S.active_list_cat == cat.id)
			if imgui.Selectable(cat.name .. "##list_cat_" .. cat.id, selected) then
				S.active_list_cat = cat.id
			end
		end
		imgui.EndChild()

		imgui.SameLine()

		imgui.BeginChild("smi_lists_right", ImVec2(0, 0), true)
		local cat = LISTS[S.active_list_cat] or LISTS[1]
		local state = ensure_list_state(cat.id)
		if cat.key == "objects" then
			imgui.TextColored(ImVec4(0.72, 0.72, 0.72, 1), L("smi_help.settings_ui.text.text_33"))
			imgui.SameLine()
			if imgui.SmallButton(L("smi_help.settings_ui.text.objects_reset_defaults")) then
				local items = S.list_cache[cat.id]
				local defaults = objects_to_cache(ctx.OBJECTS_DEFAULT)
				for i = #items, 1, -1 do
					items[i] = nil
				end
				for i, v in ipairs(defaults) do
					items[i] = v
				end
				apply_list_cache(cat)
				set_tab_dirty("lists")
			end
			if imgui.IsItemHovered() then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_34"))
			end
			imgui.Spacing()
			if imgui.Checkbox(L("smi_help.settings_ui.text.text_80_objects_use_full"), S.objects_use_full) then
				set_tab_dirty("lists")
			end
			if imgui.IsItemHovered() then
				ctx.imgui_set_tooltip_safe(
					S.objects_use_full[0]
					and L("smi_help.settings_ui.text.text_80")
					or  L("smi_help.settings_ui.text.text_35")
				)
			end
		end
		ListEditor(cat.id, cat.name, S.list_cache[cat.id], {
			state = state,
			allow_duplicates = cat.allow_duplicates,
			on_changed = function()
				return apply_list_cache(cat)
			end,
			on_dirty = function()
				set_tab_dirty("lists")
			end,
		})
		imgui.EndChild()
		imgui.EndChild()
	end

	local function draw_templates_tab()
		local function normalize_template_group(group)
			local out = {}
			if type(group) == "table" then
				for _, line in ipairs(group) do
					if type(line) == "string" then
						out[#out + 1] = line
					end
				end
			elseif type(group) == "string" then
				out[#out + 1] = group
			end
			return out
		end

		local function clone_template_group(group)
			local out = {}
			for i, line in ipairs(normalize_template_group(group)) do
				out[i] = line
			end
			return out
		end

		local function category_name(idx)
			local tpl = S.templates_list[idx]
			local name = ctx.trim(tostring(tpl and tpl.category or ""))
			if name == "" then
				name = L("smi_help.settings_ui.text.text_36") .. tostring(idx)
			end
			return name
		end

		local function has_category_name(name, skip_idx)
			name = ctx.trim(tostring(name or ""))
			if name == "" then
				return false
			end
			for i, tpl in ipairs(S.templates_list) do
				if i ~= skip_idx and ctx.trim(tostring(tpl.category or "")) == name then
					return true
				end
			end
			return false
		end

		local function build_unique_category_name(base, skip_idx)
			base = ctx.trim(tostring(base or ""))
			if base == "" then
				base = L("smi_help.settings_ui.text.text_37")
			end
			if not has_category_name(base, skip_idx) then
				return base
			end
			local suffix = 2
			local candidate = base .. " " .. tostring(suffix)
			while has_category_name(candidate, skip_idx) do
				suffix = suffix + 1
				candidate = base .. " " .. tostring(suffix)
			end
			return candidate
		end

		local function set_delete_target(kind, cat_idx, tpl_idx)
			S.tpl_delete_target.kind = kind or ""
			S.tpl_delete_target.cat_idx = cat_idx or 0
			S.tpl_delete_target.tpl_idx = tpl_idx or 0
		end

		if S.tpl_active_category < 1 then
			S.tpl_active_category = (#S.templates_list > 0) and 1 or 0
		end
		if S.tpl_active_category > #S.templates_list then
			S.tpl_active_category = #S.templates_list
		end

		imgui.BeginChild("smi_settings_templates", ImVec2(0, 0), false)
		imgui.BeginChild("tpl_left_categories", ImVec2(240, 0), true)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_32"))
		imgui.Separator()
		imgui.InputTextWithHint(
			"##tpl_category_filter",
			L("smi_help.settings_ui.text.text_38"),
			S.tpl_category_filter,
			sizeof(S.tpl_category_filter)
		)
		if imgui.Button(L("common.add_compact") .. "##tpl_cat_add") then
			imgui.StrCopy(S.tpl_category_new.buf, "")
			imgui.OpenPopup("tpl_cat_add_popup")
		end
		if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
			ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_39"))
		end
		imgui.SameLine()
		local can_rename = S.tpl_active_category > 0 and S.templates_list[S.tpl_active_category] ~= nil
		if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_rename")) and can_rename then
			imgui.StrCopy(S.tpl_category_rename.buf, category_name(S.tpl_active_category))
			imgui.OpenPopup("tpl_cat_rename_popup")
		end
		if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
			ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_40"))
		end
		imgui.SameLine()
		if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_dup")) and can_rename then
			local src = S.templates_list[S.tpl_active_category]
			local copy = clone_templates({ src })[1] or { category = category_name(S.tpl_active_category), texts = {} }
			copy.category = build_unique_category_name(category_name(S.tpl_active_category) .. L("smi_help.settings_ui.text.text_41"))
			table.insert(S.templates_list, S.tpl_active_category + 1, copy)
			S.tpl_active_category = S.tpl_active_category + 1
			S.tpl_action_error = ""
			set_tab_dirty("templates")
		end
		if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
			ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_42"))
		end
		if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_del")) and can_rename then
			set_delete_target("category", S.tpl_active_category, 0)
			imgui.OpenPopup("tpl_delete_confirm_popup")
		end
		if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
			ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_43"))
		end

		if imgui.BeginPopup("tpl_cat_add_popup") then
			imgui.InputText(L("smi_help.settings_ui.text.tpl_cat_add_name"), S.tpl_category_new.buf, S.tpl_category_new.size)
			if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_add_ok")) then
				local name = ctx.trim(str(S.tpl_category_new.buf))
				if name == "" then
					S.tpl_action_error = L("smi_help.settings_ui.text.text_44")
				elseif has_category_name(name) then
					S.tpl_action_error = L("smi_help.settings_ui.text.text_45")
				else
					table.insert(S.templates_list, { category = name, texts = {} })
					S.tpl_active_category = #S.templates_list
					S.tpl_action_error = ""
					set_tab_dirty("templates")
					imgui.CloseCurrentPopup()
				end
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_add_cancel")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		if imgui.BeginPopup("tpl_cat_rename_popup") then
			imgui.InputText(L("smi_help.settings_ui.text.tpl_cat_rename_name"), S.tpl_category_rename.buf, S.tpl_category_rename.size)
			if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_rename_ok")) then
				local idx = S.tpl_active_category
				local name = ctx.trim(str(S.tpl_category_rename.buf))
				if name == "" then
					S.tpl_action_error = L("smi_help.settings_ui.text.text_44")
				elseif has_category_name(name, idx) then
					S.tpl_action_error = L("smi_help.settings_ui.text.text_45")
				elseif S.templates_list[idx] then
					S.templates_list[idx].category = name
					S.tpl_action_error = ""
					set_tab_dirty("templates")
					imgui.CloseCurrentPopup()
				end
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.tpl_cat_rename_cancel")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		local category_filter = ctx.trim(str(S.tpl_category_filter))
		local category_filter_l = (ctx.tolower_utf8 and ctx.tolower_utf8(category_filter)) or category_filter:lower()
		local visible_categories = {}
		for idx = 1, #S.templates_list do
			local name = category_name(idx)
			local test_name = (ctx.tolower_utf8 and ctx.tolower_utf8(name)) or name:lower()
			if category_filter_l == "" or test_name:find(category_filter_l, 1, true) ~= nil then
				visible_categories[#visible_categories + 1] = idx
			end
		end

		imgui.Separator()
		imgui.BeginChild("tpl_category_list", ImVec2(0, 0), false)
		if #visible_categories == 0 then
			imgui.TextUnformatted(L("smi_help.settings_ui.text.text_46"))
		else
			local clipper = imgui.ImGuiListClipper(#visible_categories)
			while clipper:Step() do
				for vi = clipper.DisplayStart + 1, clipper.DisplayEnd do
					local idx = visible_categories[vi]
					local tpl = S.templates_list[idx]
					if tpl then
						local name = category_name(idx)
						local count = #(tpl.texts or {})
						local selected = (idx == S.tpl_active_category)
						if imgui.Selectable(name .. " (" .. tostring(count) .. ")##tpl_cat_select_" .. idx, selected) then
							S.tpl_active_category = idx
						end
					end
				end
			end
		end
		imgui.EndChild()
		imgui.EndChild()

		imgui.SameLine()
		imgui.BeginChild("tpl_right_content", ImVec2(0, 0), true)
		if S.tpl_action_error ~= "" then
			imgui.TextColored(ImVec4(1, 0.48, 0.38, 1), S.tpl_action_error)
		end

		local active_idx = S.tpl_active_category
		local active_tpl = S.templates_list[active_idx]
		if not active_tpl then
			imgui.TextUnformatted(L("smi_help.settings_ui.text.text_47"))
			imgui.EndChild()
			imgui.EndChild()
			return
		end

		active_tpl.texts = active_tpl.texts or {}
		S.tpl_edit_mode[active_idx] = S.tpl_edit_mode[active_idx] or {}
		S.tpl_edit_buf[active_idx] = S.tpl_edit_buf[active_idx] or {}
		S.tpl_input[active_idx] = buf_ensure(S.tpl_input, active_idx, 256)
		S.tpl_multiline[active_idx] = S.tpl_multiline[active_idx] or new.bool(false)
		buf_maybe_grow(S.tpl_input[active_idx])
		if S.tpl_add_mode[active_idx] == nil then
			S.tpl_add_mode[active_idx] = S.tpl_multiline[active_idx][0] and 2 or 1
		end

		imgui.TextUnformatted(category_name(active_idx))
		imgui.Separator()
		imgui.InputTextWithHint(
			"##tpl_template_filter",
			L("smi_help.settings_ui.text.text_48"),
			S.tpl_template_filter,
			sizeof(S.tpl_template_filter)
		)
		local template_filter = ctx.trim(str(S.tpl_template_filter))
		local template_filter_l = (ctx.tolower_utf8 and ctx.tolower_utf8(template_filter)) or template_filter:lower()

		local visible_templates = {}
		local edit_mode_active = false
		for j, group in ipairs(active_tpl.texts) do
			group = normalize_template_group(group)
			active_tpl.texts[j] = group
			local match = template_filter_l == ""
			if not match then
				for _, line in ipairs(group) do
					local line_l = (ctx.tolower_utf8 and ctx.tolower_utf8(line)) or line:lower()
					if line_l:find(template_filter_l, 1, true) ~= nil then
						match = true
						break
					end
				end
			end
			if match then
				visible_templates[#visible_templates + 1] = j
			end
			S.tpl_edit_mode[active_idx][j] = S.tpl_edit_mode[active_idx][j] or new.bool(false)
			if S.tpl_edit_mode[active_idx][j][0] then
				edit_mode_active = true
			end
		end

		imgui.BeginChild("tpl_items_list", ImVec2(0, -165), true)
		imgui.Columns(2, "tpl_items_cols", false)
		local col_w = imgui.GetWindowWidth() - 220
		if col_w < 120 then
			col_w = 120
		end
		imgui.SetColumnWidth(0, col_w)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_49"))
		imgui.NextColumn()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_19"))
		imgui.NextColumn()
		imgui.Separator()

		local function draw_template_row(j)
			local group = active_tpl.texts[j]
			if type(group) ~= "table" then
				group = normalize_template_group(group)
				active_tpl.texts[j] = group
			end
			local edit_mode = S.tpl_edit_mode[active_idx][j]
			if edit_mode[0] then
				local buf = buf_ensure(S.tpl_edit_buf[active_idx], j, 256)
				buf_maybe_grow(buf)
				imgui.InputTextMultiline("##tpl_edit_" .. active_idx .. "_" .. j, buf.buf, buf.size, ImVec2(0, 76))
				imgui.NextColumn()
				if imgui.SmallButton(L("smi_help.settings_ui.text.tpl_row_save") .. active_idx .. "_" .. j) then
					local edited = {}
					for line in str(buf.buf):gmatch("[^\r\n]+") do
						edited[#edited + 1] = line
					end
					active_tpl.texts[j] = edited
					edit_mode[0] = false
					set_tab_dirty("templates")
				end
				if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
					ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_50"))
				end
				imgui.SameLine()
				if imgui.SmallButton(L("smi_help.settings_ui.text.tpl_row_cancel") .. active_idx .. "_" .. j) then
					edit_mode[0] = false
				end
				if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
					ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_51"))
				end
				imgui.NextColumn()
				return false
			end

			local first_line = tostring(group[1] or "")
			local line_count = #group
			local display = first_line
			if line_count > 1 then
				display = display .. " (" .. tostring(line_count) .. L("smi_help.settings_ui.text.text_52")
			end
			ctx.imgui_text_safe(display)
			if line_count > 1 and imgui.IsItemHovered() then
				imgui.BeginTooltip()
				imgui.TextUnformatted(table.concat(group, "\n"))
				imgui.EndTooltip()
			end
			imgui.NextColumn()

			if imgui.SmallButton(ICON_UP .. "##tpl_up_" .. active_idx .. "_" .. j) and j > 1 then
				active_tpl.texts[j], active_tpl.texts[j - 1] = active_tpl.texts[j - 1], active_tpl.texts[j]
				set_tab_dirty("templates")
				imgui.NextColumn()
				return true
			end
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_53"))
			end
			imgui.SameLine()
			if imgui.SmallButton(ICON_DOWN .. "##tpl_down_" .. active_idx .. "_" .. j) and j < #active_tpl.texts then
				active_tpl.texts[j], active_tpl.texts[j + 1] = active_tpl.texts[j + 1], active_tpl.texts[j]
				set_tab_dirty("templates")
				imgui.NextColumn()
				return true
			end
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_54"))
			end
			imgui.SameLine()
			if imgui.SmallButton(ICON_EDIT .. "##tpl_edit_btn_" .. active_idx .. "_" .. j) then
				buf_set(S.tpl_edit_buf[active_idx], j, table.concat(group, "\n"))
				S.tpl_edit_mode[active_idx][j][0] = true
			end
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_55"))
			end
			imgui.SameLine()
			if imgui.SmallButton(ICON_DUPLICATE .. "##tpl_dup_btn_" .. active_idx .. "_" .. j) then
				table.insert(active_tpl.texts, j + 1, clone_template_group(group))
				set_tab_dirty("templates")
				imgui.NextColumn()
				return true
			end
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_56"))
			end
			imgui.SameLine()
			if imgui.SmallButton(ICON_DELETE .. "##tpl_del_btn_" .. active_idx .. "_" .. j) then
				set_delete_target("template", active_idx, j)
				imgui.OpenPopup("tpl_delete_confirm_popup")
			end
			if imgui.IsItemHovered() and ctx.imgui_set_tooltip_safe then
				ctx.imgui_set_tooltip_safe(L("smi_help.settings_ui.text.text_57"))
			end
			imgui.NextColumn()
			return false
		end

		local modified = false
		if edit_mode_active then
			for _, j in ipairs(visible_templates) do
				if draw_template_row(j) then
					modified = true
					break
				end
			end
		else
			local clipper = imgui.ImGuiListClipper(#visible_templates)
			while clipper:Step() do
				for vi = clipper.DisplayStart + 1, clipper.DisplayEnd do
					local j = visible_templates[vi]
					if j and draw_template_row(j) then
						modified = true
						break
					end
				end
				if modified then
					break
				end
			end
		end

		imgui.Columns(1)
		imgui.EndChild()

		if imgui.BeginPopup("tpl_delete_confirm_popup") then
			local target = S.tpl_delete_target
			if target.kind == "template" then
				imgui.TextUnformatted(L("smi_help.settings_ui.text.ya"))
			elseif target.kind == "category" then
				imgui.TextUnformatted(L("smi_help.settings_ui.text.ya_58"))
			else
				imgui.TextUnformatted(L("smi_help.settings_ui.text.text_59"))
			end
			if imgui.Button(L("smi_help.settings_ui.text.tpl_delete_yes")) then
				if target.kind == "template" then
					local cat = S.templates_list[target.cat_idx]
					if cat and type(cat.texts) == "table" and cat.texts[target.tpl_idx] then
						table.remove(cat.texts, target.tpl_idx)
						set_tab_dirty("templates")
					end
				elseif target.kind == "category" then
					if S.templates_list[target.cat_idx] then
						table.remove(S.templates_list, target.cat_idx)
						table.remove(S.tpl_input, target.cat_idx)
						table.remove(S.tpl_multiline, target.cat_idx)
						table.remove(S.tpl_edit_mode, target.cat_idx)
						table.remove(S.tpl_edit_buf, target.cat_idx)
						table.remove(S.tpl_add_mode, target.cat_idx)
						if #S.templates_list == 0 then
							S.tpl_active_category = 0
						else
							S.tpl_active_category = min(target.cat_idx, #S.templates_list)
						end
						set_tab_dirty("templates")
					end
				end
				set_delete_target("", 0, 0)
				imgui.CloseCurrentPopup()
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.tpl_delete_no")) then
				set_delete_target("", 0, 0)
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		imgui.Separator()
		local mode = S.tpl_add_mode[active_idx] or 1
		if mode ~= 1 and mode ~= 2 and mode ~= 3 then
			mode = 1
		end
		if imgui.RadioButtonBool(L("smi_help.settings_ui.text.tpl_mode_1") .. active_idx, mode == 1) then
			mode = 1
		end
		imgui.SameLine()
		if imgui.RadioButtonBool(L("smi_help.settings_ui.text.tpl_mode_2") .. active_idx, mode == 2) then
			mode = 2
		end
		imgui.SameLine()
		if imgui.RadioButtonBool(L("smi_help.settings_ui.text.tpl_mode_3") .. active_idx, mode == 3) then
			mode = 3
		end
		S.tpl_add_mode[active_idx] = mode
		S.tpl_multiline[active_idx][0] = (mode ~= 1)

		if mode == 1 then
			imgui.InputText("##tpl_input_" .. active_idx, S.tpl_input[active_idx].buf, S.tpl_input[active_idx].size)
			imgui.TextUnformatted(L("smi_help.settings_ui.text.text_60"))
		else
			imgui.InputTextMultiline(
				"##tpl_input_" .. active_idx,
				S.tpl_input[active_idx].buf,
				S.tpl_input[active_idx].size,
				ImVec2(0, 72)
			)
			if mode == 2 then
				imgui.TextUnformatted(L("smi_help.settings_ui.text.text_61"))
			else
				imgui.TextUnformatted(L("smi_help.settings_ui.text.text_62"))
			end
		end

		if imgui.Button(L("smi_help.settings_ui.text.tpl_add") .. active_idx) then
			local raw = str(S.tpl_input[active_idx].buf)
			if raw ~= "" then
				if mode == 1 then
					table.insert(active_tpl.texts, { raw })
				elseif mode == 2 then
					for line in raw:gmatch("[^\r\n]+") do
						line = ctx.trim(line)
						if line ~= "" then
							table.insert(active_tpl.texts, { line })
						end
					end
				else
					local group = {}
					for line in raw:gmatch("[^\r\n]+") do
						line = ctx.trim(line)
						if line ~= "" then
							group[#group + 1] = line
						end
					end
					if #group > 0 then
						table.insert(active_tpl.texts, group)
					end
				end
				imgui.StrCopy(S.tpl_input[active_idx].buf, "")
				set_tab_dirty("templates")
			end
		end

		imgui.EndChild()
		imgui.EndChild()
	end

	local function draw_autocorrect_tab()
		if S.ac_need_rebuild then
			rebuild_ac_buffers()
			S.ac_need_rebuild = false
		end

		imgui.BeginChild("smi_settings_autocorrect", ImVec2(0, 0), false)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_63"))
		imgui.Separator()

		buf_maybe_grow(S.ac_new_find)
		buf_maybe_grow(S.ac_new_repl)
		imgui.PushItemWidth(180)
		local submit_find = imgui.InputTextWithHint(
			L("smi_help.settings_ui.text.ac_new_find"),
			L("smi_help.settings_ui.text.text_64"),
			S.ac_new_find.buf,
			S.ac_new_find.size,
			InputTextFlags.EnterReturnsTrue
		)
		imgui.PopItemWidth()
		imgui.SameLine()
		imgui.PushItemWidth(220)
		local submit_repl = imgui.InputTextWithHint(
			L("smi_help.settings_ui.text.ac_new_repl"),
			L("smi_help.settings_ui.text.text_65"),
			S.ac_new_repl.buf,
			S.ac_new_repl.size,
			InputTextFlags.EnterReturnsTrue
		)
		imgui.PopItemWidth()
		imgui.SameLine()
		if imgui.Button(L("smi_help.settings_ui.text.ac_add")) or submit_find or submit_repl then
			local find_v = ctx.trim(str(S.ac_new_find.buf))
			local repl_v = str(S.ac_new_repl.buf)
			if find_v ~= "" then
				table.insert(S.autocorrect_rules, { find_v, repl_v })
				S.ac_need_rebuild = true
				if sync_ac_to_buffer() then
					imgui.StrCopy(S.ac_new_find.buf, "")
					imgui.StrCopy(S.ac_new_repl.buf, "")
					set_tab_dirty("autocorrect")
				end
			end
		end

		if S.ac_error ~= "" then
			imgui.TextColored(ImVec4(1, 0.48, 0.38, 1), S.ac_error)
		end

		local frame_h = imgui.GetFrameHeightWithSpacing and imgui.GetFrameHeightWithSpacing() or 28
		imgui.BeginChild("smi_ac_table", ImVec2(0, -(frame_h * 5.4)), true)
		imgui.Columns(3, "smi_ac_cols", false)
		local action_w = 88
		local col_w = (imgui.GetWindowWidth() - action_w) * 0.5
		if col_w < 120 then
			col_w = 120
		end
		imgui.SetColumnWidth(0, col_w)
		imgui.SetColumnWidth(1, col_w)
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_64"))
		imgui.NextColumn()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_65"))
		imgui.NextColumn()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_19"))
		imgui.NextColumn()
		imgui.Separator()

		local i = 1
		while i <= #S.autocorrect_rules do
			local rule = S.autocorrect_rules[i]
			local find_entry = buf_ensure(S.ac_find_buf, i, max(128, #(rule[1] or "") + 16))
			local repl_entry = buf_ensure(S.ac_repl_buf, i, max(128, #(rule[2] or "") + 16))
			buf_maybe_grow(find_entry)
			buf_maybe_grow(repl_entry)

			if imgui.InputText("##ac_find_" .. i, find_entry.buf, find_entry.size) then
				rule[1] = str(find_entry.buf)
				if sync_ac_to_buffer() then
					set_tab_dirty("autocorrect")
				end
			end
			imgui.NextColumn()

			if imgui.InputText("##ac_repl_" .. i, repl_entry.buf, repl_entry.size) then
				rule[2] = str(repl_entry.buf)
				if sync_ac_to_buffer() then
					set_tab_dirty("autocorrect")
				end
			end
			imgui.NextColumn()

			if imgui.SmallButton(L("smi_help.settings_ui.text.ac_del") .. i) then
				table.remove(S.autocorrect_rules, i)
				S.ac_need_rebuild = true
				sync_ac_to_buffer()
				set_tab_dirty("autocorrect")
			else
				i = i + 1
			end
			imgui.NextColumn()
		end

		imgui.Columns(1)
		imgui.EndChild()

		if imgui.CollapsingHeader(L("smi_help.settings_ui.text.text_66")) then
			if imgui.InputTextMultiline("##ac_raw", S.autocorrect, sizeof(S.autocorrect), ImVec2(0, 140)) then
				refresh_ac_from_buffer()
				set_tab_dirty("autocorrect")
			end
		end
		imgui.EndChild()
	end

	local function draw_import_export_tab()
		imgui.BeginChild("smi_settings_import_export", ImVec2(0, 0), false)
		imgui.TextWrapped(L("smi_help.settings_ui.text.text_67"))
		if imgui.Button(L("smi_help.settings_ui.text.smi_export_all")) then
			local encoded = ctx.funcs.encodeJsonSafe(collect_export_payload(), { indent = true })
			if type(encoded) ~= "string" then
				encoded = ""
			end
			buf_set(S.import_export_buf, "all_export", encoded)
			S.import_export_error = ""
			imgui.OpenPopup("smi_export_all_popup")
		end
		imgui.SameLine()
		if imgui.Button(L("smi_help.settings_ui.text.smi_import_all")) then
			S.import_export_error = ""
			imgui.OpenPopup("smi_import_all_popup")
		end

		if S.import_export_error ~= "" then
			imgui.TextColored(ImVec4(1, 0.48, 0.38, 1), S.import_export_error)
		end

		if imgui.BeginPopup("smi_export_all_popup") then
			local entry = S.import_export_buf.all_export or buf_set(S.import_export_buf, "all_export", "")
			imgui.InputTextMultiline(
				"##smi_export_all_text",
				entry.buf,
				entry.size,
				ImVec2(0, 220),
				InputTextFlags.ReadOnly
			)
			if imgui.SetClipboardText then
				if imgui.Button(L("smi_help.settings_ui.text.smi_export_copy")) then
					imgui.SetClipboardText(str(entry.buf))
				end
			else
				imgui.TextUnformatted(L("smi_help.settings_ui.text.text_17"))
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.smi_export_close")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		if imgui.BeginPopup("smi_import_all_popup") then
			local entry = buf_ensure(S.import_export_buf, "all_import", 16384)
			buf_maybe_grow(entry)
			imgui.InputTextMultiline("##smi_import_all_text", entry.buf, entry.size, ImVec2(0, 220))
			if imgui.Button(L("smi_help.settings_ui.text.smi_import_apply")) then
				local parsed = ctx.funcs.decodeJsonSafe(str(entry.buf))
				if type(parsed) ~= "table" then
					S.import_export_error = L("smi_help.settings_ui.text.json")
				else
					local ok, err = apply_import_payload(parsed)
					if ok then
						S.import_export_error = ""
						imgui.CloseCurrentPopup()
					else
						S.import_export_error = err or L("smi_help.settings_ui.text.text_15")
					end
				end
			end
			imgui.SameLine()
			if imgui.Button(L("smi_help.settings_ui.text.smi_import_close")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end

		imgui.Spacing()
		imgui.TextUnformatted(L("smi_help.settings_ui.text.text_68"))
		imgui.Separator()
		imgui.TextWrapped(L("smi_help.settings_ui.text.buy_sell_both"))
		if imgui.InputTextMultiline("##price_type_map", S.price_type_map, sizeof(S.price_type_map), ImVec2(0, 140)) then
			set_tab_dirty("import_export")
		end
		imgui.EndChild()
	end

	imgui.BeginChild("smi_settings_root", ImVec2(0, 0), true)
	draw_toolbar()
	imgui.Separator()

	if imgui.BeginTabBar("smi_settings_tabs") then
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_69"), "general")) then
			draw_general_tab()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_70"), "timers")) then
			draw_timers_tab()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_71"), "lists")) then
			draw_lists_tab()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_72"), "templates")) then
			draw_templates_tab()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_73"), "autocorrect")) then
			draw_autocorrect_tab()
			imgui.EndTabItem()
		end
		if imgui.BeginTabItem(tab_caption(L("smi_help.settings_ui.text.text_74"), "import_export")) then
			draw_import_export_tab()
			imgui.EndTabItem()
		end
		imgui.EndTabBar()
	end

	imgui.EndChild()
end

function M.DrawSettingsUI()
	DrawSettingsUI_Refactored()
end

function M.init(parent_ctx)
	ctx = parent_ctx
	constructor = ctx.constructor
end

return M
