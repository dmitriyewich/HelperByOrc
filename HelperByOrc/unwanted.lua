-- HelperByOrc.unwanted — редактор/движок игнор-списка + Автопомощник шаблонов
-- UI: mimgui (иконки, массовые операции, подсказки; без Columns и форматных Text*)
-- Хранилище: moonloader/HelperByOrc/unwanted.json (UTF-8)
-- Runtime: CP1251 (матчинг в CP1251, конфиг — UTF-8)

local M = {}

local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local str = ffi.string
local sizeof = ffi.sizeof

-- ИКОНКИ (fallback на текст)
local okfa, fa = pcall(require, "HelperByOrc.fAwesome6_solid")
local function I(glyph, text)
	return (okfa and glyph and (glyph .. " ") or "") .. (text or "")
end

-- === Конфиг ===
local CONFIG_PATH = "moonloader/HelperByOrc/unwanted.json"
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

-- === IO helpers ===
local function read_file(p)
	local f = io.open(p, "rb")
	if not f then
		return nil
	end
	local d = f:read("*a")
	f:close()
	return d
end
local function write_file(p, data)
	local f = io.open(p, "wb")
	if not f then
		return false
	end
	f:write(data or "")
	f:close()
	return true
end

local function json_encode(tbl)
	local ok, dk = pcall(require, "dkjson")
	if ok and dk and dk.encode then
		return dk.encode(tbl, { indent = true })
	end
	-- tiny fallback
	local function esc(s)
		return tostring(s):gsub("\\", "\\\\"):gsub('"', '\\"')
	end
	local function dump(v)
		local t = type(v)
		if t == "table" then
			local isArr, n = true, 0
			for k, _ in pairs(v) do
				n = n + 1
				if k ~= n then
					isArr = false
				end
			end
			local parts = {}
			if isArr then
				for i = 1, #v do
					parts[#parts + 1] = dump(v[i])
				end
			else
				for k, val in pairs(v) do
					parts[#parts + 1] = '"' .. esc(k) .. '":' .. dump(val)
				end
			end
			return (isArr and "[" .. table.concat(parts, ",") .. "]" or "{" .. table.concat(parts, ",") .. "}")
		elseif t == "string" then
			return '"' .. esc(v) .. '"'
		elseif t == "number" or t == "boolean" then
			return tostring(v)
		else
			return "null"
		end
	end
	return dump(tbl)
end

local function json_decode(s)
	local ok1, res1 = pcall(function()
		return decodeJson and decodeJson(s) or nil
	end)
	if ok1 and res1 ~= nil then
		return res1
	end
	local ok2, dk = pcall(require, "dkjson")
	if ok2 and dk and dk.decode then
		return dk.decode(s)
	end
	return nil
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

-- === Резервная копия (.bak) ===
local function backup_file(path)
        local bak = path .. ".bak"
        local data = read_file(path)
        if data then
                write_file(bak, data)
        end
end

-- === Загрузка/сохранение ===
local function load_cfg()
	local s = read_file(CONFIG_PATH)
	if not s or s == "" then
		write_file(CONFIG_PATH, json_encode(cfg))
		return
	end
	local t = json_decode(s)
	if type(t) == "table" then
		if type(t.settings) == "table" then
			local src = t.settings
			local dst = cfg.settings
			dst.enabled = src.enabled ~= false
			dst.max_pattern_len = tonumber(src.max_pattern_len) or dst.max_pattern_len
			if type(src.normalizer) == "table" then
				local dn, sn = dst.normalizer, src.normalizer
				dn.strip_colors = sn.strip_colors ~= false
				dn.collapse_ws = sn.collapse_ws and true or false
				dn.trim = sn.trim ~= false
			end
		end
		cfg.ignore = type(t.ignore) == "table" and t.ignore or {}
	end
end

local function save_cfg()
        backup_file(CONFIG_PATH)
        write_file(CONFIG_PATH, json_encode(cfg))
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
		return false, "Слишком длинный шаблон"
	end
	if src:find("%.%*") and not (src:find("^%^") or src:find("%$%s*$")) then
		return false, 'Жадный ".*" без якорей ^ / $ (может тормозить)'
	end
	if src:find("%[%^.-%]%*%.%*") then
		return false, "Подозрительный класс + жадность (может тормозить)"
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
		return false, "Ошибка синтаксиса"
	end
	return true
end

-- === Компиляция в кэш (только enabled) ===
local function rebuild_cache()
	compiled.literals, compiled.literals_by_first, compiled.patterns, compiled.flat_patterns_cp1251 = {}, {}, {}, {}
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
					error_by_idx[i] = err or "Некорректный Lua-шаблон"
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
	local present = {}
	for i = 1, #s do
		present[s:byte(i)] = true
	end
	for b, idxs in pairs(compiled.literals_by_first) do
		if present[b] then
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
	local present = {}
	for i = 1, #s do
		present[s:byte(i)] = true
	end
	for b, idxs in pairs(compiled.literals_by_first) do
		if present[b] then
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

	-- Цвета {ffffff} → {%x%x%x%x%x%x}
	if opts.colors then
		for token in cp:gmatch("{%x%x%x%x%x%x}") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "{%%x%%x%%x%%x%%x%%x}")
		end
	end

	-- Деньги $18.400 → \%$%d[%d%.,]*
	if opts.money then
		for token in cp:gmatch("%$%d[%d%.,]*") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%$%%d[%d%.,]*")
		end
	end

	-- Время 12:34 → %d%d:%d%d
	if opts.time then
		for token in cp:gmatch("%d%d:%d%d") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%d%%d:%%d%%d")
		end
	end

	-- Числа 123, 18.400 → %d+ (после денег/времени)
	if opts.numbers then
		for token in cp:gmatch("%d[%d%.,]*") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%d+")
		end
	end

	-- Ник Имя_Фамилия → %w+_%w+ (просто и понятно)
	if opts.nick then
		for token in cp:gmatch("([A-Za-z\192-\255\168\184]+_[A-Za-z\192-\255\168\184]+)") do
			local esc_token = escape_lua_magic(token)
			gen = gen:gsub(esc_token, "%%w+_%%w+")
		end
	end

	-- Тег в квадратных скобках в начале → %b[]
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

	local sel = ffi.new("bool[1]", selection[idx] and true or false)
	if imgui.Checkbox("##sel", sel) then
		selection[idx] = sel[0] and true or false
	end
	Tooltip("Выделить правило для массовых операций")
	imgui.SameLine()

	local en = ffi.new("bool[1]", rule_enabled(item))
	if imgui.Checkbox("##en", en) then
		item.enabled = en[0] and true or false
		M.save()
	end
	Tooltip("Включить/выключить именно это правило")
	imgui.SameLine()

	local typelabel = (item.type == "pattern") and (okfa and (fa.CODE .. " Шаблон") or "[PATTERN] Шаблон")
		or (okfa and (fa.QUOTE_LEFT .. " Точная") or "[LITERAL] Точная")
	TextRaw(typelabel)
	HelpMark(
		(item.type == "pattern")
				and "Шаблон — продвинутое правило по образцу (Lua-паттерн).\nПримеры:\n• ^%[AD%] — строки, начинающиеся с [AD]\n• %d+ — одна или больше цифр\n• %w+_%w+ — ник Имя_Фамилия\n• ^Текст$ — точное совпадение всей строки\nСовет: избегайте «.*» без якорей ^ и $ — это может тормозить."
			or "Точная — простая подстрока без спецсимволов.\nСоветы:\n• «Без регистра» — игнорирует регистр.\n• «Целое слово» — только отдельное слово (не часть)."
	)
	imgui.SameLine()

	if SmallBtn(okfa and (fa.ANGLE_UP .. " Вверх") or "[UP]", "Переместить выше") and idx > 1 then
		cfg.ignore[idx], cfg.ignore[idx - 1] = cfg.ignore[idx - 1], cfg.ignore[idx]
		selection[idx], selection[idx - 1] = selection[idx - 1], selection[idx]
		swap_editor(idx, idx - 1)
		rebuild_cache()
	end
	imgui.SameLine()
	if
		SmallBtn(okfa and (fa.ANGLE_DOWN .. " Вниз") or "[DOWN]", "Переместить ниже")
		and idx < #cfg.ignore
	then
		cfg.ignore[idx], cfg.ignore[idx + 1] = cfg.ignore[idx + 1], cfg.ignore[idx]
		selection[idx], selection[idx + 1] = selection[idx + 1], selection[idx]
		swap_editor(idx, idx + 1)
		rebuild_cache()
	end
	imgui.SameLine()
	if SmallBtn(okfa and (fa.PEN .. " Редактировать") or "[EDIT]", "Редактировать") then
		start_edit(idx, item)
	end
	imgui.SameLine()
	if SmallBtn(okfa and (fa.TRASH_CAN .. " Удалить") or "[DEL]", "Удалить правило") then
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
			"Текст правила. Для шаблонов — Lua-паттерн.\nПримеры:\n• ^%[Подсказка%]\n• %d+ (числа)\n• %w+_%w+ (ник)\n• ^Текст$ (строка целиком)"
		)

		imgui.SameLine()
		local isp = ffi.new("bool[1]", st.is_pattern and true or false)
		if imgui.Checkbox(okfa and (fa.CODE .. " Lua-шаблон") or "Lua-шаблон", isp) then
			st.is_pattern = isp[0] and true or false
		end
		Tooltip(
			"Включите, если хотите использовать шаблон (Lua-паттерн) вместо точной подстроки."
		)
		imgui.SameLine()
		local en2 = ffi.new("bool[1]", st.enabled and true or false)
		if imgui.Checkbox("Вкл", en2) then
			st.enabled = en2[0] and true or false
		end
		Tooltip("Включает/выключает это правило.")

		if not st.is_pattern then
			imgui.SameLine()
			local nc = ffi.new("bool[1]", st.nocase and true or false)
			if imgui.Checkbox("Без регистра", nc) then
				st.nocase = nc[0] and true or false
			end
			Tooltip(
				"Искать без учёта регистра.\nПример: «vip» найдёт «VIP», «Vip», «vIp»."
			)
			imgui.SameLine()
			local wh = ffi.new("bool[1]", st.whole and true or false)
			if imgui.Checkbox("Целое слово", wh) then
				st.whole = wh[0] and true or false
			end
			Tooltip(
				"Совпадение только как отдельного слова.\nПример: «vip» не сработает на «vipка», но сработает на «... vip ...»."
			)
		end

		if SmallBtn(okfa and (fa.CHECK .. " Сохранить") or "[SAVE]", "Сохранить изменения") then
			apply_edit(idx)
		end
		imgui.SameLine()
		if SmallBtn(okfa and (fa.XMARK .. " Отмена") or "[CANCEL]", "Отменить изменения") then
			cancel_edit(idx)
		end

		if st.is_pattern then
			local ok, msg = is_valid_pattern(to_cp1251(str(st.buf)))
			TextRaw(
				ok and (okfa and (fa.CHECK .. " Валидно") or "[OK] Валидно")
					or (okfa and (fa.TRIANGLE_EXCLAMATION .. " Ошибка: " .. (msg or "")) or "[ERROR] " .. (msg or ""))
			)
		end
	else
		local tags = {}
		if item.type == "literal" then
			if item.nocase then
				tags[#tags + 1] = "[nocase]"
			end
			if item.whole_word then
				tags[#tags + 1] = "[word]"
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
					.. (okfa and (fa.TRIANGLE_EXCLAMATION .. " Ошибка: " .. tostring(err)) or ("[ERROR] " .. tostring(err)))
			)
		else
			TextWrappedRaw(item.text or "")
		end
		if last_match and last_match.idx == idx then
			TextRaw(
				okfa and (fa.CHECK .. " ← совпадение (тестер)") or "<- совпадение (тестер)"
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
			I(fa.SHIELD, "Игнорируемые сообщения") .. "##unwanted",
			M.showWindow,
			imgui.WindowFlags.NoCollapse
		)
	end

	-- Верхняя панель
	do
		local en = ffi.new("bool[1]", cfg.settings.enabled and true or false)
		imgui.AlignTextToFramePadding()
		if imgui.Checkbox(" Фильтр включён", en) then
			cfg.settings.enabled = en[0] and true or false
			save_cfg()
		end
		Tooltip("Главный переключатель: включить/выключить весь фильтр.")

		imgui.SameLine()
		if imgui.Button(I(fa.FLOPPY_DISK, "Сохранить")) then
			M.save()
		end
		Tooltip("Сохранить конфиг на диск.")

		imgui.SameLine()
		if imgui.Button(I(fa.ROTATE_RIGHT, "Перечитать")) then
			M.reload()
		end
		Tooltip("Перечитать конфиг с диска.")

		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_ON, "Включить все")) then
			set_all_enabled(true)
		end
		Tooltip("Включить ВСЕ правила.")
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_OFF or fa.POWER_OFF, "Выключить все")) then
			set_all_enabled(false)
		end
		Tooltip("Выключить ВСЕ правила.")
	end
	imgui.Spacing()
	TextRaw(I(fa.GEARS, "Нормализация:"))
	imgui.SameLine()
	local n = cfg.settings.normalizer or {}
	local sc = ffi.new("bool[1]", n.strip_colors and true or false)
	if imgui.Checkbox("Без {цветов}", sc) then
		n.strip_colors = sc[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
		rebuild_cache()
	end
	Tooltip(
		"Убирает цветовые коды вида {FFFFFF} из входящего текста перед проверкой.\nРекомендуется оставить включённым."
	)
	imgui.SameLine()
	local cw = ffi.new("bool[1]", n.collapse_ws and true or false)
	if imgui.Checkbox("Схлоп. пробелы", cw) then
		n.collapse_ws = cw[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
	end
	Tooltip(
		"Заменяет подряд идущие пробелы на один. Помогает, если в чат летят «лохматые» пробелы."
	)
	imgui.SameLine()
	local tr = ffi.new("bool[1]", n.trim and true or false)
	if imgui.Checkbox("Trim", tr) then
		n.trim = tr[0] and true or false
		cfg.settings.normalizer = n
		save_cfg()
	end
	Tooltip(
		"Удаляет пробелы в начале и конце строки.\nПолезно при копировании текста из чата."
	)

	imgui.SameLine()
	TextRaw(I(fa.LIST, (" Правил: %d | Ошибок: %d"):format(#cfg.ignore, invalid_count)))

	imgui.Separator()
	TextRaw(I(fa.LIST, "Список правил:"))

	-- Массовые операции
	do
		if imgui.Button(I(fa.SQUARE_CHECK or fa.CHECK, "Выделить все")) then
			for i = 1, #cfg.ignore do
				selection[i] = true
			end
		end
		Tooltip("Выделить весь список правил.")
		imgui.SameLine()
		if imgui.Button(I(fa.SQUARE_MINUS or fa.MINUS, "Снять выделение")) then
			selection = {}
		end
		Tooltip("Снять выделение со всех правил.")
		imgui.SameLine()
		if imgui.Button(I(fa.TRASH_CAN, "Удалить выделенные")) then
			bulk_delete()
		end
		Tooltip("Удалить все выделенные правила.")
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_ON, "Включить выделенные")) then
			bulk_set_enabled(true)
		end
		Tooltip("Включить все выделенные правила.")
		imgui.SameLine()
		if imgui.Button(I(fa.TOGGLE_OFF or fa.POWER_OFF, "Выключить выделенные")) then
			bulk_set_enabled(false)
		end
		Tooltip("Выключить все выделенные правила.")
		imgui.SameLine()
		if imgui.Button(I(fa.RECYCLE or fa.REPEAT, "Удалить дубли")) then
			remove_duplicates()
		end
		Tooltip("Удаляет дубликаты (совпадают тип, текст и флаги).")
		imgui.SameLine()
		if imgui.Button(I(fa.LIST_OL or fa.SORT_UP, "Сортировать по типу")) then
			sort_by_type()
		end
		Tooltip("Сначала точные, затем шаблоны. Внутри — по тексту.")
		imgui.SameLine()
		if imgui.Button(I(fa.LIST_UL or fa.SORT_UP, "Сортировать по тексту")) then
			sort_by_text()
		end
		Tooltip("Чистая алфавитная сортировка по тексту.")
	end

	-- Список
	local list_h = 330
	if imgui.BeginChild("list_ignore", imgui.ImVec2(0, list_h), true) then
		if #cfg.ignore == 0 then
			TextRaw("Пусто. Добавьте правило ниже.")
			imgui.Separator()
		else
			for i, it in ipairs(cfg.ignore) do
				draw_rule_row(i, it)
			end
		end
		imgui.EndChild()
	end

	-- Добавление
	imgui.Separator()
	TextRaw(I(fa.SQUARE_PLUS, "Добавить правило:"))
	imgui.InputText("##new_rule", new_buf, sizeof(new_buf))
	Tooltip("Текст правила.\nДля «Lua-шаблон» используйте примеры ниже.")
	imgui.SameLine()
	imgui.Checkbox(I(fa.CODE, "Lua-шаблон"), new_is_pat)
	Tooltip(
		"Включите для шаблонов (Lua-паттерны):\n• ^%[AD%] — строки, начинающиеся с [AD]\n• %d+ — цифры\n• %w+_%w+ — ник Имя_Фамилия\n• ^Текст$ — строка целиком\nИзбегайте «.*» без ^ и $ — может тормозить."
	)
	imgui.SameLine()
	imgui.Checkbox("Без регистра", new_nocase)
	Tooltip(
		"Для Точных правил: игнорировать регистр.\nПример: «vip» найдёт «VIP», «Vip», «vIp»."
	)
	imgui.SameLine()
	imgui.Checkbox("Целое слово", new_whole)
	Tooltip(
		"Для Точных правил: совпадение только как отдельного слова.\nПример: «vip» не совпадёт с «vipка», но совпадёт в «... vip ...»."
	)

	imgui.SameLine()
	if imgui.Button(I(fa.SQUARE_PLUS, "Добавить")) then
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
	TextRaw(I(fa.FLASK_VIAL, "Тестер:"))
	imgui.InputText("##test_text", test_buf, sizeof(test_buf))
	Tooltip(
		"Вставьте сюда текст из чата и нажмите «Проверить».\nПокажем номер сработавшего правила и совпавшую подстроку."
	)
	imgui.SameLine()
	if imgui.Button(I(fa.MAGNIFYING_GLASS, "Проверить")) then
		run_tester()
	end
	imgui.SameLine()
	if last_match and last_match.idx and last_match.idx > 0 then
		TextRaw(I(fa.CHECK, (" Совпало с правилом #%d (%s)"):format(last_match.idx, last_match.kind)))
		TextWrappedRaw("Подстрока: " .. (last_match.s_utf8 or ""))
		TextRaw(("Позиция: %d..%d"):format(last_match.a or 0, last_match.b or 0))
	else
		TextRaw("Совпадений нет")
	end

	-- Автопомощник
	imgui.Separator()
	TextRaw(I(fa.WAND_MAGIC, "Автопомощник шаблонов (введите пример сообщения)"))
	imgui.InputText("##helper_in", helper_buf, sizeof(helper_buf))
	Tooltip(
		"Пример: [Информация] {ffffff}Вы получили $18.400 за отредактированое вами объявление."
	)
	-- настройки помощника
	imgui.TextUnformatted("Обобщать:")
	imgui.SameLine()
	imgui.Checkbox("Якоря ^ $", hp_anchor)
	Tooltip(
		"Добавляет ^ в начало и $ в конец — совпадение по всей строке целиком."
	)
	imgui.SameLine()
	imgui.Checkbox("Деньги $123", hp_money)
	Tooltip("Превращает «$18.400» в шаблон «\\%$%d[%d%.,]*».")
	imgui.SameLine()
	imgui.Checkbox("Числа", hp_numbers)
	Tooltip("Числа и числа с разделителями заменяются на «%d+».")
	imgui.SameLine()
	imgui.Checkbox("Время 12:34", hp_time)
	Tooltip("Фрагменты вида 12:34 заменяются на «%d%d:%d%d».")
	imgui.SameLine()
	imgui.Checkbox("Цвета {HEX}", hp_colors)
	Tooltip("Фрагменты вида {ffffff} заменяются на «{%x%x%x%x%x%x}».")
	imgui.SameLine()
	imgui.Checkbox("Ник Имя_Фамилия", hp_nick)
	Tooltip("Фрагменты вида Имя_Фамилия заменяются на «%w+_%w+».")
	imgui.SameLine()
	imgui.Checkbox("Тег [..] в начале", hp_bracket_tag)
	Tooltip("Если строка начинается с [Текст], заменит его на «%b[]».")

	if imgui.Button(I(fa.WAND_MAGIC or fa.MAGIC, "Сгенерировать")) then
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
		TextRaw("Подсказки:")
		if helper_exact_out ~= "" then
			TextRaw("• Точный (экранированный):")
			TextWrappedRaw(helper_exact_out)
			imgui.SameLine()
			if imgui.Button("[ADD exact]") then
				table.insert(cfg.ignore, { type = "pattern", text = helper_exact_out, enabled = true })
				rebuild_cache()
				save_cfg()
			end
			imgui.SameLine()
			if imgui.Button("[COPY exact]") and imgui.SetClipboardText then
				imgui.SetClipboardText(helper_exact_out)
			end
			HelpMark(
				"Просто экранированная версия — совпадение именно с такой строкой (удобно с ^ и $)."
			)
		end
		if helper_general_out ~= "" then
			TextRaw("• Обобщённый:")
			TextWrappedRaw(helper_general_out)
			imgui.SameLine()
			if imgui.Button("[ADD general]") then
				table.insert(cfg.ignore, { type = "pattern", text = helper_general_out, enabled = true })
				rebuild_cache()
				save_cfg()
			end
			imgui.SameLine()
			if imgui.Button("[COPY general]") and imgui.SetClipboardText then
				imgui.SetClipboardText(helper_general_out)
			end
			HelpMark(
				"Включает замены:\n— деньги → \\%$%d[%d%.,]*\n— числа → %d+\n— время → %d%d:%d%d\n— цвет → {%x%x%x%x%x%x}\n— ник → %w+_%w+"
			)
		end
	end

	imgui.Separator()
	TextRaw(I(fa.BOOK, " Краткие примеры:"))
	TextWrappedRaw(
		"Точная подстрока:\n"
			.. "  • «VIP» — уберёт любые сообщения, где встречается VIP.\n"
			.. "  • Флаги: «Без регистра», «Целое слово».\n\n"
			.. "Lua-шаблон (продвинутый):\n"
			.. "  • ^%[Подсказка%] — сообщение начинается с [Подсказка]\n"
			.. "  • %d+ — любая последовательность цифр\n"
			.. "  • %w+_%w+ — ник Имя_Фамилия\n"
			.. "  • { %x%x%x%x%x%x } — цветовой код, например {ffffff}\n"
			.. "  • ^Текст$ — строка должна совпасть полностью\n"
			.. "  • Избегайте «.*» без якорей ^ и $, это может тормозить."
	)

	if not inline then
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
M.reload()
return M
