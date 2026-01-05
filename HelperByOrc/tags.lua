-- HelperByOrc/tags.lua
-- Полный модуль: теги/переменные + улучшенный UI для MoonLoader (mimgui)

-- ========== МОДУЛЬ / ИМПОРТЫ ==========
local module = {}
local imgui = require("mimgui")
local ffi = require("ffi")

local funcs
local ok_fa, fa = pcall(require, "HelperByOrc.fAwesome6_solid") -- необязательно, UI работает и без иконок
local samp

function module.attachModules(mod)
	funcs = mod.funcs
	samp = mod.samp
end

-- ========== КОНФИГ / ХРАНИЛИЩА / НАСТРОЙКИ ==========
local config_path = "moonloader/HelperByOrc/tags.json"

-- пользовательские переменные + кэш парсинга
local custom_vars, parse_cache = {}, {}

-- FIFO-кэш с лимитом
local PARSE_CACHE_MAX = 200
local parse_cache_order = {}

-- буферы ввода для UI и $call-хендлеров
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
	allow_unsafe = true, -- разрешать $call(...)
	wait_timeout_sec = 30, -- таймаут для $wait(...)
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
}

-- SAMP ссылка
local function S_get()
	return samp
end

-- ========== УТИЛИТЫ ==========
local function strip_tag(nick)
	return nick and nick:gsub("^%b[]", "") or nick
end

local function log_chat(msg, color)
	if sampAddChatMessage then
		sampAddChatMessage(tostring(msg), color or 0xFFD700)
	end
end

-- гарантируем наличие папки перед записью файла
local function ensure_parent_dir(file_path)
	local p = tostring(file_path):gsub("\\", "/")
	local dir = p:match("^(.*)/[^/]+$") or ""
	if dir == "" then
		return
	end
	local ok_lfs, lfs = pcall(require, "lfs")
	if ok_lfs and lfs then
		local function ensure(d)
			if d == "" then
				return true
			end
			local acc = ""
			for part in d:gmatch("[^/]+") do
				acc = acc == "" and part or (acc .. "/" .. part)
				if lfs.attributes(acc, "mode") ~= "directory" then
					lfs.mkdir(acc)
				end
			end
			return true
		end
		ensure(dir)
	else
		os.execute(('mkdir "%s" 2>nul'):format(dir:gsub("/", "\\")))
	end
end

local function save_config()
	local data = { vars = custom_vars, settings = settings }
	ensure_parent_dir(config_path)
	local f_mod = funcs
	if f_mod and f_mod.saveTableToJson then
		local ok, saved = pcall(f_mod.saveTableToJson, data, config_path)
		if ok and saved then
			return
		end
	end
	if type(encodeJson) ~= "function" then
		return
	end
	local ok, encoded = pcall(encodeJson, data)
	if not (ok and type(encoded) == "string") then
		return
	end
	local f = io.open(config_path, "w+")
	if f then
		f:write(encoded)
		f:close()
	end
end

local function load_custom_vars()
	local tbl
	local f_mod = funcs
	local has_file
	if type(doesFileExist) == "function" then
		has_file = doesFileExist(config_path)
	else
		local f = io.open(config_path, "r")
		if f then
			has_file = true
			f:close()
		else
			has_file = false
		end
	end
	if f_mod and f_mod.loadTableFromJson then
		local defaults = {}
		local ok, loaded = pcall(f_mod.loadTableFromJson, config_path, defaults)
		if ok and type(loaded) == "table" then
			if loaded ~= defaults then
				tbl = loaded
			elseif not has_file then
				tbl = defaults
			end
		end
	end
	if not tbl and has_file then
		local f = io.open(config_path, "r")
		if f then
			local content = f:read("*a")
			f:close()
			if type(decodeJson) == "function" then
				local ok, parsed = pcall(decodeJson, content)
				if ok and type(parsed) == "table" then
					tbl = parsed
				end
			end
		end
	end
	if type(tbl) == "table" then
		if tbl.vars or tbl.settings then
			custom_vars = type(tbl.vars) == "table" and tbl.vars or {}
			settings = type(tbl.settings) == "table" and tbl.settings or settings
		else
			custom_vars = tbl
		end
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
	if not tonumber(settings.wait_timeout_sec) then
		settings.wait_timeout_sec = 30
	end
	rebuild_cvar_buffers()
end

local function save_custom_vars()
	save_config()
end

load_custom_vars()

-- ========== КЭШ ПАРСИНГА ==========
local function cache_set(key, val)
	if parse_cache[key] ~= nil then
		-- удалить старую позицию
		for i = 1, #parse_cache_order do
			if parse_cache_order[i] == key then
				table.remove(parse_cache_order, i)
				break
			end
		end
	end
	parse_cache[key] = val
	parse_cache_order[#parse_cache_order + 1] = key
	if #parse_cache_order > PARSE_CACHE_MAX then
		local old = table.remove(parse_cache_order, 1)
		parse_cache[old] = nil
	end
end
local function cache_get(key)
	return parse_cache[key]
end
local function clear_parse_cache()
	for k in pairs(parse_cache) do
		parse_cache[k] = nil
	end
	parse_cache_order = {}
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
					if settings.show_target_notice and sampAddChatMessage and target._notice_id ~= id then
						sampAddChatMessage(("[Tags] Выбран target id: %d"):format(id), 0xFFD700)
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

-- фоновый поток слежения за таргетом
if not module._target_tracker_started then
	module._target_tracker_started = true
	if lua_thread and lua_thread.create then
		lua_thread.create(function()
			while true do
				pcall(read_target_once)
				wait(0)
			end
		end)
	else
		log_chat(
			"[Tags] Предупреждение: lua_thread.create недоступен, трекинг цели отключён",
			0xAA8800
		)
	end
end

-- получить ник по ID через SAMP-обёртку
local function get_nick_by_id(id)
	if not id then
		return nil
	end
	if sampGetPlayerNickname then
		local ok, name = pcall(sampGetPlayerNickname, id)
		if ok and type(name) == "string" and name ~= "" then
			return name
		end
	end
	local Ss = S_get()
	if Ss and Ss.GetNameID then
		local ok2, name2 = pcall(Ss.GetNameID, id)
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
	return (n and funcs and funcs.translite_name) and funcs.translite_name(strip_tag(n)) or ""
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
	return (nm and funcs and funcs.translite_name) and funcs.translite_name(nm) or ""
end
local function map_surname(id)
	local n = map_nick_raw(id)
	return n and strip_tag(n):match(".*_(.+)") or ""
end
local function map_surname_ru(id)
	local sn = map_surname(id)
	return (sn and funcs and funcs.translite_name) and funcs.translite_name(sn) or ""
end

-- ========== MULTI-TAG HANDLERS ==========
local multi_tag_handlers = {
	-- строка в нижний регистр
	strlow = function(str)
		return funcs and funcs.string_lower and funcs.string_lower(str) or str or ""
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
		if funcs and funcs.Take_Screenshot then
			funcs.Take_Screenshot(path, name)
			return string.format("[Скриншот: %s]", name or os.date("%d.%m.%Y %H.%M.%S"))
		end
		return "[Скрин не выполнен]"
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
	strlow = { desc = "Строка в нижнем регистре", example = "[strlow(ТЕКСТ)]" },
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

local VARS_DIR = "moonloader/HelperByOrc/vars"

local function list_lua_files(dir)
	local out = {}
	dir = tostring(dir or ""):gsub("\\", "/"):gsub("/+$", "")
	local ok_lfs, lfs = pcall(require, "lfs")
	if ok_lfs and lfs and lfs.attributes(dir, "mode") == "directory" then
		for f in lfs.dir(dir) do
			if f ~= "." and f ~= ".." and f:match("%.lua$") then
				out[#out + 1] = dir .. "/" .. f
			end
		end
		return out
	end
	if io.popen then
		local cmd = ('dir /b "%s\\*.lua"'):format(dir:gsub("/", "\\"))
		local p = io.popen(cmd)
		if p then
			for line in p:lines() do
				if line and line:match("%.lua$") then
					out[#out + 1] = dir .. "/" .. line
				end
			end
			p:close()
			return out
		end
	end
	return out
end

local _loaded_var_files = {}

local function load_external_vars()
	_loaded_var_files = {}

	local files = list_lua_files(VARS_DIR)
	local loaded, errors = 0, 0

	for _, path in ipairs(files) do
		local chunk, err = loadfile(path)
		if not chunk then
			errors = errors + 1
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
				errors = errors + 1
				log_chat(
					("[Tags] Ошибка при выполнении '%s': %s"):format(path, tostring(perr)),
					0xAA3333
				)
			else
				loaded = loaded + 1
				_loaded_var_files[#_loaded_var_files + 1] = path
			end
		end
	end

	clear_parse_cache()
end

-- командные теги для справки
local command_tags = {
	{
		name = "$wait(expr)",
		desc = "Ждать до выполнения условия (строка полностью: $wait(...))",
		example = "$wait(time() % 2 == 0)",
	},
	{
		name = "$call(expr)",
		desc = "Выполнить Lua-выражение/код без вставки текста (строка полностью: $call(...))",
		example = "$call(module.save_config())",
	},
}

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
}

-- таблица тегов {var}
local tags = setmetatable({}, {
	__index = function(_, key)
		if key == "{id}" then
			return function()
				local Ss = S_get()
				return Ss and Ss.Local_ID and Ss.Local_ID() or ""
			end
		elseif key == "{nick}" then
			return function()
				local Ss = S_get()
				return Ss and Ss.GetNameID and Ss.Local_ID and Ss.GetNameID(Ss.Local_ID()) or ""
			end
		elseif key == "{nickru}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID and funcs and funcs.translite_name then
					local n = Ss.GetNameID(Ss.Local_ID())
					return n and funcs.translite_name(strip_tag(n)) or ""
				end
				return ""
			end
		elseif key == "{screen}" then
			return function()
				if funcs and funcs.Take_Screenshot then
					funcs.Take_Screenshot()
					return "[Скриншот сделан]"
				end
				return "[Скрин не выполнен]"
			end
		elseif key == "{rpnick}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID then
					local n = Ss.GetNameID(Ss.Local_ID())
					return n and strip_tag(n):gsub("_", " ") or ""
				end
				return ""
			end
		elseif key == "{name}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID then
					local n = Ss.GetNameID(Ss.Local_ID())
					return n and strip_tag(n):match("([^_]+)") or ""
				end
				return ""
			end
		elseif key == "{nameru}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID and funcs and funcs.translite_name then
					local n = Ss.GetNameID(Ss.Local_ID())
					local nm = n and strip_tag(n):match("([^_]+)")
					return nm and funcs.translite_name(nm) or ""
				end
				return ""
			end
		elseif key == "{surname}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID then
					local n = Ss.GetNameID(Ss.Local_ID())
					return n and strip_tag(n):match(".*_(.+)") or ""
				end
				return ""
			end
		elseif key == "{surnameru}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID and funcs and funcs.translite_name then
					local n = Ss.GetNameID(Ss.Local_ID())
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
		return function()
			return ""
		end
	end,
})

-- ========== ПАРСЕР МУЛЬТИ-ТЕГОВ ==========
local RECURSION_LIMIT = 10

local function handle_multi_tag(tag, val, thisbind_value, depth)
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then
		return "[Ошибка: слишком глубокая вложенность]"
	end
	local cache_key = tag .. "(" .. tostring(val) .. ")" .. (thisbind_value and ("|" .. tostring(thisbind_value)) or "")
	local cached = cache_get(cache_key)
	if cached ~= nil then
		return cached
	end

	local handler = multi_tag_handlers[tag]
	local ok, res
	if handler then
		ok, res = pcall(handler, val, thisbind_value)
		if not ok then
			res = "[Ошибка парсинга тега: " .. tag .. "]"
		end
	else
		res = "[Неизвестный тег: " .. tag .. "]"
	end
	cache_set(cache_key, res)
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
			local expr = text:sub(start_e + 1, i - 1)
			local value = module.change_tags(expr, thisbind_value, depth)
			local inner = handle_multi_tag(tag, value, thisbind_value, depth)
			out = out .. tostring(inner)
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

local function safe_load_expr(expr)
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

local function execute_special_commands(text)
	local lines = {}
	for line in text:gmatch("[^\r\n]+") do
		table.insert(lines, line)
	end

	local out = {}
	for _, line in ipairs(lines) do
		local expr = line:match("^%$wait%((.+)%)$")
		if expr then
			local timeout = tonumber(settings.wait_timeout_sec) or 30
			local finished, timed_out = false, false
			local chunk, err = safe_load_expr(expr)
			if not chunk then
				log_chat("[Tags] Ошибка в $wait: " .. tostring(err), 0xAA3333)
			else
				lua_thread.create(function()
					local t0 = os.clock()
					while true do
						local ok, res = pcall(chunk)
						if ok and res then
							finished = true
							break
						end
						if (os.clock() - t0) > timeout then
							timed_out = true
							break
						end
						wait(50)
					end
				end)
				while not finished and not timed_out do
					wait(25)
				end
				if timed_out then
					log_chat("[Tags] $wait: истёк таймаут " .. timeout .. " сек", 0xAA3333)
				end
			end
		else
			local expr2 = line:match("^%$call%((.+)%)$")
			if expr2 then
				if not settings.allow_unsafe then
					log_chat(
						"[Tags] $call отклонён: небезопасный режим выключен",
						0xAA3333
					)
				else
					local chunk, err = safe_load_expr(expr2)
					if not chunk then
						log_chat("[Tags] Ошибка в $call: " .. tostring(err), 0xAA3333)
					else
						lua_thread.create(function()
							pcall(chunk)
						end)
					end
				end
			else
				table.insert(out, line)
			end
		end
	end
	return table.concat(out, "\n")
end

-- ========== ОСНОВНАЯ ФУНКЦИЯ ПОДСТАНОВКИ ==========
function module.change_tags(text, thisbind_value, depth)
	clear_parse_cache()
	text = execute_special_commands(text or "")
	text = parse_multi_tags(text, thisbind_value, depth)
	text = text:gsub("{[%w_]+}", function(key)
		local fn = tags[key]
		if fn then
			local cache_key = key
			local c = cache_get(cache_key)
			if c ~= nil then
				return c
			end
			local ok, res = pcall(fn)
			local out = (ok and res and tostring(res) ~= key) and tostring(res) or ""
			cache_set(cache_key, out)
			return out
		end
		return ""
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
function module.setWaitTimeout(sec)
	local n = tonumber(sec)
	if n and n > 0 then
		settings.wait_timeout_sec = n
		save_config()
	end
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
			{ name = ("[%s(...)]"):format(tag), desc = v.desc, example = v.example or ("[%s(...)]"):format(tag) }
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

-- локальные буферы для поиска/ввода
local filter_vars = imgui.new.char[96]()
local filter_funcs = imgui.new.char[96]()
local new_var_name = imgui.new.char[64]()
local new_var_value = imgui.new.char[256]()
local edit_key = nil
local del_key = nil

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
			imgui.BeginChild("main_opts", imgui.ImVec2(0, 140), true)

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

			-- Разрешить $call
			do
				local v = ffi.new("bool[1]", settings.allow_unsafe and true or false)
				if
					imgui.Checkbox("Разрешить $call (только если понимаешь риски)", v)
				then
					settings.allow_unsafe = v[0] and true or false
					save_config()
				end
				HelpTip(
					"Выполнение Lua-выражений из строки. Включай только для своих шаблонов. На чужие не ставь."
				)
			end

			-- Таймаут $wait
			do
				local wt = ffi.new("int[1]", settings.wait_timeout_sec or 30)
				imgui.SetNextItemWidth(120)
				if imgui.InputInt("Таймаут $wait, сек", wt) then
					if wt[0] < 1 then
						wt[0] = 1
					end
					settings.wait_timeout_sec = wt[0]
					save_config()
				end
				HelpTip(
					"Максимальное ожидание условия в $wait(...). По умолчанию 30 сек."
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
				settings.wait_timeout_sec = 30
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
						save_config()
						clear_parse_cache()
					end
					imgui.NextColumn()
					-- Кнопки
					if imgui.SmallButton(" Коп.") then
						CopyFlash("{" .. name .. "}")
					end
					imgui.SameLine()
					if imgui.SmallButton(" Переим.") then
						edit_key = name
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
				imgui.OpenPopup("Переименование переменной")
			end
			if
				imgui.BeginPopupModal(
					"Переименование переменной",
					nil,
					imgui.WindowFlags.AlwaysAutoResize
				)
			then
				imgui.Text("Старое имя: {" .. tostring(edit_key) .. "}")
				local tmp = imgui.new.char[64](tostring(edit_key))
				imgui.InputText("Новое имя", tmp, ffi.sizeof(tmp))
				if imgui.Button("OK##rename") then
					local newname = ffi.string(tmp)
					if newname ~= "" and newname ~= edit_key then
						custom_vars[newname] = custom_vars[edit_key]
						cvar_bufs[newname] = cvar_bufs[edit_key]
						custom_vars[edit_key] = nil
						cvar_bufs[edit_key] = nil
						save_config()
						clear_parse_cache()
					end
					edit_key = nil
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button("Отмена##rename") then
					edit_key = nil
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
				imgui.Text("Точно удалить {" .. tostring(del_key) .. "}?")
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
					if imgui.Selectable(name, false) then
						CopyFlash(example)
					end
					HelpTip((desc ~= "" and (desc .. "\nПример: " .. example)) or ("Пример: " .. example))
					imgui.PopID()
				end
			end
			imgui.EndChild()

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
				flash_copied("Экспортировано в " .. config_path)
			end
			imgui.SameLine()
			if imgui.Button(" Импорт из tags.json") then
				module.reload_config()
				flash_copied("Импортировано из " .. config_path)
			end
			imgui.EndChild()
			imgui.EndTabItem()
		end

		imgui.EndTabBar()
	end
end

-- ===== ОТДЕЛЬНОЕ ОКНО «Справка по тегам» =====
imgui.OnFrame(function()
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
		if imgui.Selectable(tag.name, false) then
			CopyFlash(copy)
		end
		HelpTip((tag.desc or "") .. (copy and ("\nПример: " .. copy) or ""))
		imgui.PopID()
	end
	imgui.EndChild()

	imgui.Columns(1)
	imgui.Separator()

	imgui.Text("Команды строкой")
	for _, t in ipairs(command_tags) do
		imgui.BulletText(t.name .. " — " .. t.desc .. "  Пример: " .. t.example)
	end
	do
		local dt = os.clock() - (ui_state.copied_time or 0)
		if ui_state.copied_text and dt < (ui_state.flash_sec or 1.5) then
			imgui.Spacing()
			imgui.TextColored(imgui.ImVec4(0.5, 1.0, 0.5, 1.0), ui_state.copied_text)
		end
	end

	imgui.Spacing()
	if imgui.Button("Закрыть") then
		showTagsWindow[0] = false
	end
	imgui.End()
end)

-- ========== СЕРВИСНЫЕ ФУНКЦИИ (СОХРАНИТЬ/ПЕРЕЧИТАТЬ) ==========
module.save_config = save_custom_vars
module.reload_config = function()
	load_custom_vars()
	clear_parse_cache()
	pcall(load_external_vars)
end
module.reload_external_vars = function()
	pcall(load_external_vars)
end

-- автозагрузка внешних переменных из HelperByOrc/vars при старте
pcall(load_external_vars)

return module
