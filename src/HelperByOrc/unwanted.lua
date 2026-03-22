local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}

local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
local paths = require("HelperByOrc.paths")
local funcs
local mimgui_funcs
encoding.default = "CP1251"
local u8 = encoding.UTF8

local config_manager
local event_bus

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
end

syncDependencies()

local str = ffi.string
local sizeof = ffi.sizeof

-- Переиспользуемый bool-буфер для imgui Checkbox (вместо ffi.new каждый кадр)
local _btmp = ffi.new("bool[1]")
local function cbool(val)
	_btmp[0] = val and true or false
	return _btmp
end

-- ИКОНКИ (fallback на текст)
local okfa, fa = pcall(require, "fAwesome7")
local function I(glyph, text)
	return (okfa and glyph and (glyph .. " ") or "") .. (text or "")
end

-- === Конфиг ===
local CONFIG_PATH_REL = "unwanted.json"
local cfg = {
	settings = {
		enabled = true,
		normalizer = { strip_colors = true, collapse_ws = false, trim = true },
		max_pattern_len = 2048,
	},
	ignore = {
		-- { type="literal"|"pattern", text="...", enabled=true, nocase=false, whole_word=false }
	},
}

-- === Кэш (в CP1251) ===
local compiled = {
	literals = {}, -- { {s=cp1251, nocase, whole_word}, ... } только enabled
	literals_by_first = {}, -- [byte] = { indices into 'literals' }
	literals_first_char = {}, -- [byte] = string.char(byte), кэш для string.find
	patterns = {}, -- { cp1251_pattern, ... } только enabled
	flat_patterns_cp1251 = {}, -- совместимость: единый список
}
local error_by_idx = {} -- индекс правила -> текст ошибки (если паттерн невалиден)
local invalid_count = 0

-- === UI-состояние ===
M.showWindow = imgui.new.bool(false)
local new_buf = imgui.new.char[512]("")
local new_is_pat = imgui.new.bool(false)
local new_nocase = imgui.new.bool(false)
local new_whole = imgui.new.bool(false)
local test_buf = imgui.new.char[512]("")
local last_match = nil -- { idx, kind, s_utf8, a,b }
local selection = {} -- [idx] = true/false
local want_scroll_to_idx = -1

-- Инлайн-редактор
local editor = {} -- [idx] = { editing=true, buf=char[512], is_pattern=bool, nocase=bool, whole=bool, enabled=bool }

-- === Автопомощник (состояние) ===
local helper_buf = imgui.new.char[512]("")
local hp_anchor = imgui.new.bool(true)
local hp_money = imgui.new.bool(true)
local hp_numbers = imgui.new.bool(true)
local hp_time = imgui.new.bool(true)
local hp_colors = imgui.new.bool(true)
local hp_nick = imgui.new.bool(true)
local hp_bracket_tag = imgui.new.bool(true)
local helper_exact_out = "" -- UTF-8
local helper_general_out = "" -- UTF-8

-- === Безопасные текстовые хелперы ===
local function TextRaw(s)
	imgui.TextUnformatted(tostring(s or ""))
end
local function TextWrappedRaw(s)
	imgui.PushTextWrapPos(0)
	imgui.TextUnformatted(tostring(s or ""))
	imgui.PopTextWrapPos()
end
local function Tooltip(txt)
	if imgui.IsItemHovered() then
		imgui.BeginTooltip()
		imgui.PushTextWrapPos(420)
		TextRaw(txt)
		imgui.PopTextWrapPos()
		imgui.EndTooltip()
	end
end
local function SmallBtn(label, tip)
	local p = imgui.SmallButton(label)
	if tip then
		Tooltip(tip)
	end
	return p
end
local function HelpMark(txt)
	imgui.SameLine()
	imgui.TextUnformatted(okfa and fa.CIRCLE_INFO or "(i)")
	Tooltip(txt)
end

-- === Кодировки ===
local function to_cp1251(s)
	local ok, res = pcall(function()
		return u8:decode(s or "")
	end)
	if ok and type(res) == "string" then
		return res
	end
	return tostring(s or "")
end
local function to_utf8(s)
	local ok, res = pcall(function()
		return u8:encode(s or "")
	end)
	if ok and type(res) == "string" then
		return res
	end
	return tostring(s or "")
end

-- === Нормализация входного CP1251 текста (ускорение/точность) ===
local function normalize_cp1251(s)
	local t = s or ""
	local n = cfg.settings and cfg.settings.normalizer
	if n then
		if n.strip_colors then
			t = t:gsub("{%x%x%x%x%x%x}", "")
		end
		if n.collapse_ws then
			t = t:gsub("%s+", " ")
		end
		if n.trim then
			t = t:match("^%s*(.-)%s*$")
		end
	end
	return t
end

-- === Загрузка/сохранение ===
local function normalizeUnwantedConfig(loaded)
	if type(loaded) ~= "table" then
		return cfg
	end
	local out = { settings = {}, ignore = {} }
	if type(loaded.settings) == "table" then
		local src = loaded.settings
		out.settings.enabled = src.enabled ~= false
		out.settings.max_pattern_len = tonumber(src.max_pattern_len) or cfg.settings.max_pattern_len
		out.settings.normalizer = {}
		if type(src.normalizer) == "table" then
			out.settings.normalizer.strip_colors = src.normalizer.strip_colors ~= false
			out.settings.normalizer.collapse_ws = src.normalizer.collapse_ws and true or false
			out.settings.normalizer.trim = src.normalizer.trim ~= false
		else
			out.settings.normalizer = cfg.settings.normalizer
		end
	else
		out.settings = cfg.settings
	end
	out.ignore = type(loaded.ignore) == "table" and loaded.ignore or {}
	return out
end

local function load_cfg()
	if config_manager then
		local data = config_manager.get("unwanted")
		if data then
			cfg = data
		end
		return
	end
	local CONFIG_PATH = paths.dataPath(CONFIG_PATH_REL)
	local s = funcs.readFile(CONFIG_PATH, "rb")
	if not s or s == "" then
		funcs.saveTableToJson(cfg, CONFIG_PATH)
		return
	end
	local t = funcs.decodeJsonSafe(s)
	if type(t) == "table" then
		local normalized = normalizeUnwantedConfig(t)
		cfg.settings = normalized.settings
		cfg.ignore = normalized.ignore
	end
end

local function save_cfg()
	if config_manager then
		config_manager.markDirty("unwanted")
	else
		local CONFIG_PATH = paths.dataPath(CONFIG_PATH_REL)
		funcs.backupFile(CONFIG_PATH)
		funcs.saveTableToJson(cfg, CONFIG_PATH)
	end
end

-- === Утилиты ===
local function escape_lua_magic(s)
	return (s:gsub("([%%%^%$%(%)%.%[%]%*%+%-%?])", "%%%1"))
end
local function rule_enabled(it)
	return it.enabled ~= false
end

-- Лёгкий линтер паттернов (эвристики против «тяжёлых»)
local function lint_pattern(src)
	if #src > (tonumber(cfg.settings.max_pattern_len) or 2048) then
		return false, L("unwanted.text.text")
	end
	if src:find("%.%*") and not (src:find("^%^") or src:find("%$%s*$")) then
		return false, L("unwanted.text.text_1")
	end
	if src:find("%[%^.-%]%*%.%*") then
		return false, L("unwanted.text.text_2")
	end
	return true
end

local function is_valid_pattern(pat_cp1251)
	local okL, msg = lint_pattern(pat_cp1251)
	if not okL then
		return false, msg
	end
	local ok1 = pcall(string.find, "", pat_cp1251)
	local ok2 = pcall(string.find, " ", pat_cp1251)
	if not ok1 and not ok2 then
		return false, L("unwanted.text.text_3")
	end
	return true
end

-- === Компиляция в кэш (только enabled) ===
local function rebuild_cache()
	compiled.literals, compiled.literals_by_first, compiled.literals_first_char, compiled.patterns, compiled.flat_patterns_cp1251 = {}, {}, {}, {}, {}
	error_by_idx = {}
	invalid_count = 0

	for i, it in ipairs(cfg.ignore) do
		local kind = (it.type == "pattern") and "pattern" or "literal"
		local txt_u = tostring(it.text or "")
		local txt_c = to_cp1251(txt_u)
		local en = rule_enabled(it)

		if kind == "literal" then
			local rec = { s = txt_c, nocase = it.nocase and true or false, whole_word = it.whole_word and true or false }
			if en and txt_c ~= "" then
				local pos = #compiled.literals + 1
				compiled.literals[pos] = rec
				local b = txt_c:byte(1)
				if b then
					compiled.literals_by_first[b] = compiled.literals_by_first[b] or {}
					compiled.literals_first_char[b] = compiled.literals_first_char[b] or string.char(b)
					table.insert(compiled.literals_by_first[b], pos)
				end
				compiled.flat_patterns_cp1251[#compiled.flat_patterns_cp1251 + 1] = escape_lua_magic(txt_c)
			end
		else -- pattern
			if en then
				local ok, err = is_valid_pattern(txt_c)
				if ok then
					compiled.patterns[#compiled.patterns + 1] = txt_c
					compiled.flat_patterns_cp1251[#compiled.flat_patterns_cp1251 + 1] = txt_c
				else
					error_by_idx[i] = err or L("unwanted.text.lua")
					invalid_count = invalid_count + 1
				end
			end
		end
	end
end

-- === Сопоставление literal с флагами ===
local function match_literal(hay, needle, nocase, whole_word)
	local hs, ns = hay, needle
	if nocase then
		hs = hs:lower()
		ns = ns:lower()
	end
	if ns == "" then
		return nil
	end
	if whole_word then
		local pat = "%f[%w]" .. escape_lua_magic(ns) .. "%f[%W]"
		local ok, a, b = pcall(string.find, hs, pat)
		if ok and a then
			return a, b
		end
		return nil
	else
		local a, b = hs:find(ns, 1, true)
		if a then
			return a, b
		end
		return nil
	end
end

-- === Публичный API для хуков ===
function M.should_ignore(text_cp1251)
	if not (cfg.settings and cfg.settings.enabled) then
		return false
	end
	if type(text_cp1251) ~= "string" or text_cp1251 == "" then
		return false
	end
	local s = normalize_cp1251(text_cp1251)

	-- Быстрый фильтр по первому байту для literals
	for b, idxs in pairs(compiled.literals_by_first) do
		if s:find(compiled.literals_first_char[b], 1, true) then
			for _, pos in ipairs(idxs) do
				local rec = compiled.literals[pos]
				local a, _ = match_literal(s, rec.s, rec.nocase, rec.whole_word)
				if a then
					return true
				end
			end
		end
	end

	-- Паттерны
	for _, p in ipairs(compiled.patterns) do
		local ok, found = pcall(string.find, s, p)
		if ok and found then
			return true
		end
	end
	return false
end

function M.get_flat_patterns_cp1251()
	local out = {}
	for i = 1, #compiled.flat_patterns_cp1251 do
		out[i] = compiled.flat_patterns_cp1251[i]
	end
	return out
end

function M.get_config()
	return cfg
end

-- === Инлайн-редактор ===
local function start_edit(idx, it)
	editor[idx] = {
		editing = true,
		is_pattern = (it.type == "pattern"),
		buf = imgui.new.char[512](tostring(it.text or "")),
		nocase = it.nocase and true or false,
		whole = it.whole_word and true or false,
		enabled = rule_enabled(it),
	}
end
local function cancel_edit(idx)
	editor[idx] = nil
end
local function apply_edit(idx)
	local st = editor[idx]
	if not st then
		return
	end
	local txt = str(st.buf)
	local kind = st.is_pattern and "pattern" or "literal"
	if st.is_pattern then
		local ok, msg = is_valid_pattern(to_cp1251(txt))
		if not ok then
			return
		end
	end
	local it = {
		type = kind,
		text = txt,
		enabled = st.enabled and true or false,
		nocase = (not st.is_pattern) and (st.nocase and true or false) or nil,
		whole_word = (not st.is_pattern) and (st.whole and true or false) or nil,
	}
	cfg.ignore[idx] = it
	editor[idx] = nil
	rebuild_cache()
	save_cfg()
end
local function swap_editor(i, j)
	editor[i], editor[j] = editor[j], editor[i]
end
local function remove_editor(idx)
	local new_e = {}
	for i, st in pairs(editor) do
		if i < idx then
			new_e[i] = st
		elseif i > idx then
			new_e[i - 1] = st
		end
	end
	editor = new_e
end

-- === Дедупликация (type+text+flags) ===
local function remove_duplicates()
	local seen = {}
	local out = {}
	for _, it in ipairs(cfg.ignore) do
		local key = (it.type or "")
			.. "\n"
			.. (it.text or "")
			.. "\n"
			.. tostring(it.nocase and true or false)
			.. "\n"
			.. tostring(it.whole_word and true or false)
		if not seen[key] then
			seen[key] = true
			out[#out + 1] = it
		end
	end
	cfg.ignore = out
	rebuild_cache()
	save_cfg()
end

-- === Сортировка ===
local function sort_by_type()
	table.sort(cfg.ignore, function(a, b)
		local ka = (a.type == "literal") and 0 or 1
		local kb = (b.type == "literal") and 0 or 1
		if ka ~= kb then
			return ka < kb
		end
		return (tostring(a.text or ""):lower() < tostring(b.text or ""):lower())
	end)
	rebuild_cache()
	save_cfg()
end
local function sort_by_text()
	table.sort(cfg.ignore, function(a, b)
		local ta = tostring(a.text or ""):lower()
		local tb = tostring(b.text or ""):lower()
		if ta ~= tb then
			return ta < tb
		end
		local ka = (a.type == "literal") and 0 or 1
		local kb = (b.type == "literal") and 0 or 1
		return ka < kb
	end)
	rebuild_cache()
	save_cfg()
end

-- === Массовые операции ===
local function selected_indices()
	local idxs = {}
	for i, _ in pairs(selection) do
		if selection[i] then
			idxs[#idxs + 1] = i
		end
	end
	table.sort(idxs)
	return idxs
end
local function bulk_delete()
	local idxs = selected_indices()
	if #idxs == 0 then
		return
	end
	for k = #idxs, 1, -1 do
		local i = idxs[k]
		table.remove(cfg.ignore, i)
		remove_editor(i)
	end
	selection = {}
	rebuild_cache()
	save_cfg()
end
local function bulk_set_enabled(value)
	local idxs = selected_indices()
	if #idxs == 0 then
		return
	end
	for _, i in ipairs(idxs) do
		local it = cfg.ignore[i]
		if it then
			it.enabled = value and true or false
		end
	end
	rebuild_cache()
	save_cfg()
end
local function set_all_enabled(value)
	for _, it in ipairs(cfg.ignore) do
		it.enabled = value and true or false
	end
	rebuild_cache()
	save_cfg()
end

-- === Подсветка совпадения (тестер) ===
local function run_tester()
	local test_utf8 = str(test_buf)
	local s = normalize_cp1251(to_cp1251(test_utf8))
	last_match = nil
	if not (cfg.settings and cfg.settings.enabled) then
		return
	end

	-- literals
	for b, idxs in pairs(compiled.literals_by_first) do
		if s:find(compiled.literals_first_char[b], 1, true) then
			for _, pos in ipairs(idxs) do
				local rec = compiled.literals[pos]
				local a, bpos = match_literal(s, rec.s, rec.nocase, rec.whole_word)
				if a then
					local sub_cp = s:sub(a, bpos)
					local hit_idx = -1
					for i, it in ipairs(cfg.ignore) do
						if rule_enabled(it) and it.type == "literal" then
							if
								to_cp1251(it.text or "") == rec.s
								and (it.nocase and true or false) == (rec.nocase and true or false)
								and (it.whole_word and true or false) == (rec.whole and true or false)
							then
								hit_idx = i
								break
							end
						end
					end
					last_match = { kind = "literal", idx = hit_idx, a = a, b = bpos, s_utf8 = to_utf8(sub_cp) }
					if hit_idx > 0 then
						selection = {}
						selection[hit_idx] = true
						want_scroll_to_idx = hit_idx
					end
					return
				end
			end
		end
	end

	-- patterns
	for i, it in ipairs(cfg.ignore) do
		if rule_enabled(it) and it.type == "pattern" then
			local p = to_cp1251(it.text or "")
			local ok, a, bpos = pcall(string.find, s, p)
			if ok and a then
				local sub_cp = s:sub(a, bpos)
				last_match = { kind = "pattern", idx = i, a = a, b = bpos, s_utf8 = to_utf8(sub_cp) }
				selection = {}
				selection[i] = true
				want_scroll_to_idx = i
				return
			end
		end
	end
end

-- === Автопомощник: генерация подсказок ===
local function helper_generate(sample_utf8, opts)
	local cp = to_cp1251(sample_utf8 or "")
	if cp == "" then
		return { exact = "", generalized = "" }
	end

	-- базовый экранированный литерал
	local esc = escape_lua_magic(cp)
	local gen = esc

	-- Цвета {ffffff} -> {%x%x%x%x%x%x}
	if opts.colors then
		for token in cp:gmatch("{%x%x%x%x%x%x}") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "{%%x%%x%%x%%x%%x%%x}")
		end
	end

	-- Деньги $18.400 -> \%$%d[%d%.,]*
	if opts.money then
		for token in cp:gmatch("%$%d[%d%.,]*") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%$%%d[%d%.,]*")
		end
	end

	-- Время 12:34 -> %d%d:%d%d
	if opts.time then
		for token in cp:gmatch("%d%d:%d%d") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%d%%d:%%d%%d")
		end
	end

	-- Числа 123, 18.400 -> %d+ (после денег/времени)
	if opts.numbers then
		for token in cp:gmatch("%d[%d%.,]*") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%d+")
		end
	end

	-- Ник Имя_Фамилия -> %w+_%w+ (просто и понятно)
	if opts.nick then
		for token in cp:gmatch("([A-Za-z\192-\255\168\184]+_[A-Za-z\192-\255\168\184]+)") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%w+_%%w+")
		end
	end

	-- Тег в квадратных скобках в начале -> %b[]
	if opts.bracket_tag then
		local tag = cp:match("^%[(.-)%]")
		if tag then
			local token = "[" .. tag .. "]"
			local esc_token = escape_lua_magic(token)
			-- заменим первое вхождение на %b[]
			gen = gen:gsub(esc_token, "%%b[]", 1)
		end
	end

	-- Якоря
	if opts.anchor then
		esc = "^" .. esc .. "$"
		gen = "^" .. gen .. "$"
	end

	return { exact = to_utf8(esc), generalized = to_utf8(gen) }
end

-- === Рисование строки правила ===
local function draw_rule_row(idx, item)
	imgui.PushIDInt(idx)

	if want_scroll_to_idx == idx and imgui.SetScrollHereY then
		imgui.SetScrollHereY(0.20)
		want_scroll_to_idx = -1
	end

	TextRaw(("#%d"):format(idx))
	imgui.SameLine()

	if imgui.Checkbox("##sel", cbool(selection[idx])) then
		selection[idx] = _btmp[0] and true or false
	end
	Tooltip(L("unwanted.text.text_4"))
	imgui.SameLine()

	if imgui.Checkbox("##en", cbool(rule_enabled(item))) then
		item.enabled = _btmp[0] and true or false
		M.save()
	end
	Tooltip(L("unwanted.text.text_5"))
	imgui.SameLine()

	local typelabel = (item.type == "pattern") and (okfa and (fa.CODE .. L("unwanted.text.text_6")) or L("unwanted.text.pattern"))
		or (okfa and (fa.QUOTE_LEFT .. L("unwanted.text.text_7")) or L("unwanted.text.literal"))
	TextRaw(typelabel)
	HelpMark(
		(item.type == "pattern")
				and L("unwanted.text.lua_ad_ad_number_w_w")
			or L("unwanted.text.text_8")
	)
	imgui.SameLine()

	if SmallBtn(okfa and (fa.ANGLE_UP .. L("unwanted.text.text_9")) or L("unwanted.text.fallback_up"), L("unwanted.text.text_10")) and idx > 1 then
		cfg.ignore[idx], cfg.ignore[idx - 1] = cfg.ignore[idx - 1], cfg.ignore[idx]
		selection[idx], selection[idx - 1] = selection[idx - 1], selection[idx]
		swap_editor(idx, idx - 1)
		rebuild_cache()
	end
	imgui.SameLine()
	if
		SmallBtn(okfa and (fa.ANGLE_DOWN .. L("unwanted.text.text_11")) or L("unwanted.text.fallback_down"), L("unwanted.text.text_12"))
		and idx < #cfg.ignore
	then
		cfg.ignore[idx], cfg.ignore[idx + 1] = cfg.ignore[idx + 1], cfg.ignore[idx]
		selection[idx], selection[idx + 1] = selection[idx + 1], selection[idx]
		swap_editor(idx, idx + 1)
		rebuild_cache()
	end
	imgui.SameLine()
	if SmallBtn(okfa and (fa.PEN .. L("unwanted.text.text_13")) or L("unwanted.text.fallback_edit"), L("unwanted.text.text_14")) then
		start_edit(idx, item)
	end
	imgui.SameLine()
	if SmallBtn(okfa and (fa.TRASH_CAN .. L("unwanted.text.text_15")) or L("unwanted.text.fallback_delete"), L("unwanted.text.text_16")) then
		table.remove(cfg.ignore, idx)
		remove_editor(idx)
		selection[idx] = nil
		rebuild_cache()
		imgui.PopID()
		imgui.Separator()
		return
	end

	local st = editor[idx]
	if st and st.editing then
		imgui.InputText("##edit_text", st.buf, sizeof(st.buf))
		Tooltip(
			L("unwanted.text.lua_number_w_w")
		)

		imgui.SameLine()
		if imgui.Checkbox(okfa and (fa.CODE .. L("unwanted.text.lua_17")) or L("unwanted.text.lua_18"), cbool(st.is_pattern)) then
			st.is_pattern = _btmp[0] and true or false
		end
		Tooltip(
			L("unwanted.text.lua_19")
		)
		imgui.SameLine()
		if imgui.Checkbox(L("unwanted.text.text_20"), cbool(st.enabled)) then
			st.enabled = _btmp[0] and true or false
		end
		Tooltip(L("unwanted.text.text_21"))

		if not st.is_pattern then
			imgui.SameLine()
			if imgui.Checkbox(L("unwanted.text.text_22"), cbool(st.nocase)) then
				st.nocase = _btmp[0] and true or false
			end
			Tooltip(
				L("unwanted.text.vip_vip_vip_vip")
			)
			imgui.SameLine()
			if imgui.Checkbox(L("unwanted.text.text_23"), cbool(st.whole)) then
				st.whole = _btmp[0] and true or false
			end
			Tooltip(
				L("unwanted.text.vip_vip_vip")
			)
		end

		if SmallBtn(okfa and (fa.CHECK .. L("unwanted.text.text_24")) or L("unwanted.text.fallback_save"), L("unwanted.text.text_25")) then
			apply_edit(idx)
		end
		imgui.SameLine()
		if SmallBtn(okfa and (fa.XMARK .. L("unwanted.text.text_26")) or L("unwanted.text.fallback_cancel"), L("unwanted.text.text_27")) then
			cancel_edit(idx)
		end

		if st.is_pattern then
			local ok, msg = is_valid_pattern(to_cp1251(str(st.buf)))
			TextRaw(
				ok and (okfa and (fa.CHECK .. L("unwanted.text.text_28")) or L("unwanted.text.ok"))
					or (okfa and (fa.TRIANGLE_EXCLAMATION .. L("unwanted.text.text_29") .. (msg or "")) or L("unwanted.text.fallback_error") .. (msg or ""))
			)
		end
	else
		local tags = {}
		if item.type == "literal" then
			if item.nocase then
				tags[#tags + 1] = L("unwanted.text.tag_nocase")
			end
			if item.whole_word then
				tags[#tags + 1] = L("unwanted.text.tag_word")
			end
		end
		if #tags > 0 then
			TextRaw(table.concat(tags, " "))
		end

		local err = error_by_idx[idx]
		if err then
			TextWrappedRaw(
				(item.text or "")
					.. "   "
					.. (okfa and (fa.TRIANGLE_EXCLAMATION .. L("unwanted.text.text_29") .. tostring(err)) or (L("unwanted.text.fallback_error") .. tostring(err)))
			)
		else
			TextWrappedRaw(item.text or "")
		end
		if last_match and last_match.idx == idx then
			TextRaw(
				okfa and (fa.CHECK .. L("unwanted.text.text_30")) or L("unwanted.text.text_31")
			)
		end
	end

	imgui.Separator()
	imgui.PopID()
end

-- === ГЛАВНОЕ ОКНО ===
function M.DrawWindow(inline)
	if not inline then
		if not M.showWindow[0] then
			return
		end
		imgui.SetNextWindowSize(imgui.ImVec2(900, 760), imgui.Cond.FirstUseEver)
		imgui.Begin(
			I(fa.SHIELD, L("unwanted.text.text_32")) .. "##unwanted",
			M.showWindow,
			imgui.WindowFlags.NoCollapse
		)
	end

	-- Верхняя панель
	do
		imgui.AlignTextToFramePadding()
		if imgui.Checkbox(L("unwanted.text.text_33"), cbool(cfg.settings.enabled)) then
			cfg.settings.enabled = _btmp[0] and true or false
			save_cfg()
		end
		Tooltip(L("unwanted.text.text_34"))

		imgui.SameLine()
		if imgui.Button(I(fa.FLOPPY_DISK, L("unwanted.text.text_35"))) then
			M.save()
		end
		Tooltip(L("unwanted.text.text_36"))

		imgui.SameLine()
		if imgui.Button(I(fa.ROTATE_RIGHT, L("unwanted.text.text_37"))) then
			M.reload()
		end
		Tooltip(L("unwanted.text.text_38"))

		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_ON, L("unwanted.text.text_39"))) then
			set_all_enabled(true)
		end
		Tooltip(L("unwanted.text.text_40"))
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_OFF or fa.POWER_OFF, L("unwanted.text.text_41"))) then
			set_all_enabled(false)
		end
		Tooltip(L("unwanted.text.text_42"))
	end
	imgui.Spacing()
	TextRaw(I(fa.GEARS, L("unwanted.text.text_43")))
	imgui.SameLine()
	local n = cfg.settings.normalizer or {}
	if imgui.Checkbox(L("unwanted.text.text_44"), cbool(n.strip_colors)) then
		n.strip_colors = _btmp[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
		rebuild_cache()
	end
	Tooltip(
		L("unwanted.text.ffffff")
	)
	imgui.SameLine()
	if imgui.Checkbox(L("unwanted.text.text_45"), cbool(n.collapse_ws)) then
		n.collapse_ws = _btmp[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
	end
	Tooltip(
		L("unwanted.text.text_46")
	)
	imgui.SameLine()
	if imgui.Checkbox(L("unwanted.text.trim"), cbool(n.trim)) then
		n.trim = _btmp[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
	end
	Tooltip(
		L("unwanted.text.text_47")
	)

	imgui.SameLine()
	TextRaw(I(fa.LIST, (L("unwanted.text.number_number")):format(#cfg.ignore, invalid_count)))

	imgui.Separator()
	TextRaw(I(fa.LIST, L("unwanted.text.text_48")))

	-- Массовые операции
	do
		if imgui.Button(I(fa.SQUARE_CHECK or fa.CHECK, L("unwanted.text.text_49"))) then
			for i = 1, #cfg.ignore do
				selection[i] = true
			end
		end
		Tooltip(L("unwanted.text.text_50"))
		imgui.SameLine()
		if imgui.Button(I(fa.SQUARE_MINUS or fa.MINUS, L("unwanted.text.text_51"))) then
			selection = {}
		end
		Tooltip(L("unwanted.text.text_52"))
		imgui.SameLine()
		if imgui.Button(I(fa.TRASH_CAN, L("unwanted.text.text_53"))) then
			bulk_delete()
		end
		Tooltip(L("unwanted.text.text_54"))
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_ON, L("unwanted.text.text_55"))) then
			bulk_set_enabled(true)
		end
		Tooltip(L("unwanted.text.text_56"))
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_OFF or fa.POWER_OFF, L("unwanted.text.text_57"))) then
			bulk_set_enabled(false)
		end
		Tooltip(L("unwanted.text.text_58"))
		imgui.SameLine()
		if imgui.Button(I(fa.RECYCLE or fa.REPEAT, L("unwanted.text.text_59"))) then
			remove_duplicates()
		end
		Tooltip(L("unwanted.text.text_60"))
		imgui.SameLine()
		if imgui.Button(I(fa.LIST_OL or fa.SORT_UP, L("unwanted.text.text_61"))) then
			sort_by_type()
		end
		Tooltip(L("unwanted.text.text_62"))
		imgui.SameLine()
		if imgui.Button(I(fa.LIST_UL or fa.SORT_UP, L("unwanted.text.text_63"))) then
			sort_by_text()
		end
		Tooltip(L("unwanted.text.text_64"))
	end

	-- Список
	local list_h = 330
	if imgui.BeginChild("list_ignore", imgui.ImVec2(0, list_h), true) then
		if #cfg.ignore == 0 then
			TextRaw(L("unwanted.text.text_65"))
			imgui.Separator()
		else
			for i, it in ipairs(cfg.ignore) do
				draw_rule_row(i, it)
			end
		end
	end
	imgui.EndChild()

	-- Добавление
	imgui.Separator()
	TextRaw(I(fa.SQUARE_PLUS, L("unwanted.text.text_66")))
	imgui.InputText("##new_rule", new_buf, sizeof(new_buf))
	Tooltip(L("unwanted.text.lua_67"))
	imgui.SameLine()
	imgui.Checkbox(I(fa.CODE, L("unwanted.text.lua_18")), new_is_pat)
	Tooltip(
		L("unwanted.text.lua_ad_ad_number_w_w_68")
	)
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_22"), new_nocase)
	Tooltip(
		L("unwanted.text.vip_vip_vip_vip_69")
	)
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_23"), new_whole)
	Tooltip(
		L("unwanted.text.vip_vip_vip_70")
	)

	imgui.SameLine()
	if imgui.Button(I(fa.SQUARE_PLUS, L("unwanted.text.text_71"))) then
		local txt = str(new_buf)
		if txt ~= "" then
			local it = {
				type = new_is_pat[0] and "pattern" or "literal",
				text = txt,
				enabled = true,
			}
			if not new_is_pat[0] then
				it.nocase = new_nocase[0] and true or false
				it.whole_word = new_whole[0] and true or false
			end
			table.insert(cfg.ignore, it)
			ffi.fill(new_buf, 512)
			new_is_pat[0] = false
			new_nocase[0] = false
			new_whole[0] = false
			rebuild_cache()
			save_cfg()
		end
	end

	-- Тестер
	imgui.Separator()
	TextRaw(I(fa.FLASK_VIAL, L("unwanted.text.text_72")))
	imgui.InputText("##test_text", test_buf, sizeof(test_buf))
	Tooltip(
		L("unwanted.text.text_73")
	)
	imgui.SameLine()
	if imgui.Button(I(fa.MAGNIFYING_GLASS, L("unwanted.text.text_74"))) then
		run_tester()
	end
	imgui.SameLine()
	if last_match and last_match.idx and last_match.idx > 0 then
		TextRaw(I(fa.CHECK, (L("unwanted.text.number_format")):format(last_match.idx, last_match.kind)))
		TextWrappedRaw(L("unwanted.text.text_75") .. (last_match.s_utf8 or ""))
		TextRaw((L("unwanted.text.number_number_76")):format(last_match.a or 0, last_match.b or 0))
	else
		TextRaw(L("unwanted.text.text_77"))
	end

	-- Автопомощник
	imgui.Separator()
	TextRaw(I(fa.WAND_MAGIC, L("unwanted.text.text_78")))
	imgui.InputText("##helper_in", helper_buf, sizeof(helper_buf))
	Tooltip(
		L("unwanted.text.ffffff_18_400")
	)
	-- настройки помощника
	imgui.TextUnformatted(L("unwanted.text.text_79"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_80"), hp_anchor)
	Tooltip(
		L("unwanted.text.text_81")
	)
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_123"), hp_money)
	Tooltip(L("unwanted.text.text_18_400_number_number"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_82"), hp_numbers)
	Tooltip(L("unwanted.text.number"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_12_34"), hp_time)
	Tooltip(L("unwanted.text.text_12_34_number_number_number_number"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.hex"), hp_colors)
	Tooltip(L("unwanted.text.ffffff_x_x_x_x_x_x"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_83"), hp_nick)
	Tooltip(L("unwanted.text.w_w"))
	imgui.SameLine()
	imgui.Checkbox(L("unwanted.text.text_84"), hp_bracket_tag)
	Tooltip(L("unwanted.text.b"))

	if imgui.Button(I(fa.WAND_MAGIC or fa.MAGIC, L("unwanted.text.text_85"))) then
		local opts = {
			anchor = hp_anchor[0],
			money = hp_money[0],
			numbers = hp_numbers[0],
			time = hp_time[0],
			colors = hp_colors[0],
			nick = hp_nick[0],
			bracket_tag = hp_bracket_tag[0],
		}
		local out = helper_generate(str(helper_buf), opts)
		helper_exact_out = out.exact or ""
		helper_general_out = out.generalized or ""
	end

	if helper_exact_out ~= "" or helper_general_out ~= "" then
		imgui.Separator()
		TextRaw(L("unwanted.text.text_86"))
		if helper_exact_out ~= "" then
			TextRaw(L("unwanted.text.text_87"))
			TextWrappedRaw(helper_exact_out)
			imgui.SameLine()
			if imgui.Button(L("unwanted.text.add_exact")) then
				table.insert(cfg.ignore, { type = "pattern", text = helper_exact_out, enabled = true })
				rebuild_cache()
				save_cfg()
			end
			imgui.SameLine()
			if imgui.Button(L("unwanted.text.copy_exact")) and imgui.SetClipboardText then
				imgui.SetClipboardText(helper_exact_out)
			end
			HelpMark(
				L("unwanted.text.text_88")
			)
		end
		if helper_general_out ~= "" then
			TextRaw(L("unwanted.text.text_89"))
			TextWrappedRaw(helper_general_out)
			imgui.SameLine()
			if imgui.Button(L("unwanted.text.add_general")) then
				table.insert(cfg.ignore, { type = "pattern", text = helper_general_out, enabled = true })
				rebuild_cache()
				save_cfg()
			end
			imgui.SameLine()
			if imgui.Button(L("unwanted.text.copy_general")) and imgui.SetClipboardText then
				imgui.SetClipboardText(helper_general_out)
			end
			HelpMark(
				L("unwanted.text.number_number_number_number_number_number_number_x_x_x_x_x_x_w_w")
			)
		end
	end

	imgui.Separator()
	TextRaw(I(fa.BOOK, L("unwanted.text.text_90")))
	TextWrappedRaw(
		L("unwanted.text.text_91")
			.. L("unwanted.text.vip_vip")
			.. L("unwanted.text.text_92")
			.. L("unwanted.text.lua_93")
			.. L("unwanted.text.text_94")
			.. L("unwanted.text.number_95")
			.. L("unwanted.text.w_w_96")
			.. L("unwanted.text.x_x_x_x_x_x_ffffff")
			.. L("unwanted.text.text_97")
			.. L("unwanted.text.text_98")
	)

	if not inline then
		if mimgui_funcs and mimgui_funcs.clampCurrentWindowToScreen then
			mimgui_funcs.clampCurrentWindowToScreen(5)
		end
		imgui.End()
	end
end

function M.DrawWindowInline()
	M.DrawWindow(true)
end

function M.isEnabled()
	return cfg.settings and cfg.settings.enabled
end

-- === Инициализация ===
function M.save()
	save_cfg()
	rebuild_cache()
end
function M.reload()
	load_cfg()
	rebuild_cache()
end

function M.attachModules(mod)
	syncDependencies(mod)
	config_manager = mod.config_manager
	event_bus = mod.event_bus
	if event_bus then
		event_bus.offByOwner("unwanted")
	end
	if config_manager then
		cfg = config_manager.register("unwanted", {
			path = CONFIG_PATH_REL,
			defaults = cfg,
			normalize = normalizeUnwantedConfig,
			onBeforeSave = function(data, path)
				funcs.backupFile(path)
			end,
		})
		rebuild_cache()
	end
end

function M.onTerminate()
	if event_bus then
		event_bus.offByOwner("unwanted")
	end
end

M.reload()
return M
