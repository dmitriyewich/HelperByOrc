-- МОДУЛЬ / ИМПОРТЫ

local module = {}
local imgui  = require 'mimgui'
local ffi	= require 'ffi'

local funcs
local ok2, fa      = pcall(require, 'HelperByOrc.fAwesome6_solid')
local samp

function module.attachModules(mod)
        funcs = mod.funcs
        samp = mod.samp
end

-- КОНФИГ / ХРАНИЛИЩА / НАСТРОЙКИ

local config_path = "moonloader/HelperByOrc/tags.json"

-- пользовательские переменные + кэш парсинга
local custom_vars, parse_cache = {}, {}
-- LRU-параметры для кэша (небольшой ограничитель)
local PARSE_CACHE_MAX = 200
local parse_cache_order = {}

-- настройки модуля
local settings = {
	show_target_notice = true,
	allow_unsafe = true,		-- разрешать $call(...) и кастомные выражения
	wait_timeout_sec = 30	   -- таймаут для $wait(...)
}

-- базовые пользовательские переменные по умолчанию
local builtin_custom_vars = {
	myorg = "СМИ ЛС",
	myorgrang = "Ведущий"
}

-- состояние таргета
local target = {
	current_ped = nil,   -- Ped, в которого сейчас целимся (или nil)
	current_id  = nil,   -- ID, соответствующий current_ped (или nil)
	last_id	 = nil,   -- последний валидный ID цели
	_notice_id  = nil,   -- защита от повторных уведомлений
}


-- SAMP ссылка
local function S_get()
        return samp
end



-- УТИЛИТЫ

local function strip_tag(nick)
	return nick and nick:gsub("^%b[]", "") or nick
end

local function log_chat(msg, color)
	if sampAddChatMessage then
		sampAddChatMessage(tostring(msg), color or 0xFFD700)
	end
end

-- JSON fallback (если нет funcs.convertTableToJsonString)
local function json_encode_fallback(tbl)
	local ok, dk = pcall(require, 'dkjson')
	if ok and dk and dk.encode then return dk.encode(tbl, { indent = true }) end
	local function esc(s) return tostring(s):gsub("\\","\\\\"):gsub('"','\\"') end
	local function dump(v)
		local t = type(v)
		if t == "table" then
			local isArr, idx = true, 1
			for k,_ in pairs(v) do if k ~= idx then isArr = false break end idx = idx + 1 end
			if isArr then
				local parts = {}
				for i=1,#v do parts[#parts+1] = dump(v[i]) end
				return "["..table.concat(parts, ",").."]"
			else
				local parts = {}
				for k,val in pairs(v) do parts[#parts+1] = '"'..esc(k)..'":'..dump(val) end
				return "{"..table.concat(parts, ",").."}"
			end
		elseif t == "string" then return '"'..esc(v)..'"'
		elseif t == "number" or t == "boolean" then return tostring(v)
		else return "null" end
	end
	return dump(tbl)
end

local function save_config()
	local data = { vars = custom_vars, settings = settings }
	local json
    if funcs and funcs.convertTableToJsonString then
            json = funcs.convertTableToJsonString(data)
	else
		json = json_encode_fallback(data)
	end
	local f = io.open(config_path, "w+")
	if f then f:write(json or ""); f:close() end
end

local function load_custom_vars()
	if doesFileExist and doesFileExist(config_path) then
		local f = io.open(config_path, "r")
		if f then
			local content = f:read("*a"); f:close()
			local ok, tbl = pcall(decodeJson, content)
			if ok and type(tbl) == "table" then
				if tbl.vars or tbl.settings then
					custom_vars = type(tbl.vars) == "table" and tbl.vars or {}
					settings = type(tbl.settings) == "table" and tbl.settings or settings
				else
					custom_vars = tbl
				end
			end
		end
	end
	for k, v in pairs(builtin_custom_vars) do
		if custom_vars[k] == nil then custom_vars[k] = v end
	end
	if settings.show_target_notice == nil then settings.show_target_notice = true end
	if settings.allow_unsafe == nil then settings.allow_unsafe = true end
	if not tonumber(settings.wait_timeout_sec) then settings.wait_timeout_sec = 30 end
end

local function save_custom_vars()
	save_config()
end

load_custom_vars()

-- Кэш парсинга (LRU)
local function cache_set(key, val)
	parse_cache[key] = val
	parse_cache_order[#parse_cache_order+1] = key
	if #parse_cache_order > PARSE_CACHE_MAX then
		local old = table.remove(parse_cache_order, 1)
		parse_cache[old] = nil
	end
end
local function cache_get(key) return parse_cache[key] end
local function clear_parse_cache()
	for k in pairs(parse_cache) do parse_cache[k] = nil end
	parse_cache_order = {}
end



-- TARGET: ЧТЕНИЕ И СОСТОЯНИЕ

local function read_target_once()
	if not getCharPlayerIsTargeting then
		target.current_ped, target.current_id = nil, nil
		return
	end

	local res, ped = false, nil
	if rawget(_G, "PLAYER_HANDLE") ~= nil then
		local ok, r, p = pcall(getCharPlayerIsTargeting, PLAYER_HANDLE)
		if ok then res, ped = r, p end
	end
	if not res then
		local ok, r, p = pcall(getCharPlayerIsTargeting, 0)
		if ok then res, ped = r, p end
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
	lua_thread.create(function()
		while true do
			pcall(read_target_once)
			wait(0)
		end
	end)
end

-- получить ник по ID через SAMP-обёртку
local function get_nick_by_id(id)
	if not id then return nil end
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


-- ЛИСТАБЕЛЬНЫЕ ПАРСЕРЫ ПАРАМЕТРОВ
-- Разделяем строку параметров на часть значений и кастомный разделитель
-- Синтаксис: [tag(1 2 3 | ", ")]  -- всё после | — разделитель (кавычки опциональны)
local function split_param_list_with_delim(raw)
	local s = tostring(raw or "")
	local in_q = false
	local i_bar = nil
	for i = 1, #s do
		local c = s:sub(i, i)
		if c == '"' then in_q = not in_q end
		if c == '|' and not in_q then i_bar = i; break end
	end
	local items_str, delim_str
	if i_bar then
		items_str = s:sub(1, i_bar - 1)
		delim_str = s:sub(i_bar + 1)
	else
		items_str = s
	end

	-- Вытащим кавычечные токены из items_str
	local quoted = {}
	for q in items_str:gmatch('"(.-)"') do quoted[#quoted+1] = q end
	local s2 = items_str:gsub('"(.-)"', ' ')
	local list = {}
	for _,q in ipairs(quoted) do if q ~= "" then list[#list+1] = q end end
	for part in s2:gmatch("[^,%s]+") do list[#list+1] = part end

	-- Разделитель
	local delim = ", "
	if delim_str and delim_str:match("%S") then
		local dq = delim_str:match('"(.-)"')
		if dq then
			delim = dq
		else
			delim = delim_str:match("^%s*(.-)%s*$")
			if delim == "" then delim = ", " end
		end
	end
	return list, delim
end

-- Универсальная обёртка: делает хендлер «листабельным»
local function make_listable(handler)
	return function(param, thisbind_value)
		local items, delim = split_param_list_with_delim(param)
		if #items <= 1 then
			return handler(param, thisbind_value)
		end
		local results = {}
		for _, it in ipairs(items) do
			local r = handler(it, thisbind_value)
			if r and r ~= "" then results[#results+1] = r end
		end
		return table.concat(results, delim or ", ")
	end
end


-- ПРОЕКЦИИ НИКОВ / МАППЕРЫ
local function map_nick_raw(id)
	id = tonumber(id)
	if not id then return "" end
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
	return n and strip_tag(n):match('([^_]+)') or ""
end
local function map_name_ru(id)
	local nm = map_name(id)
	return (nm and funcs and funcs.translite_name) and funcs.translite_name(nm) or ""
end
local function map_surname(id)
	local n = map_nick_raw(id)
	return n and strip_tag(n):match('.*_(.+)') or ""
end
local function map_surname_ru(id)
	local sn = map_surname(id)
	return (sn and funcs and funcs.translite_name) and funcs.translite_name(sn) or ""
end


-- MULTI-TAG HANDLERS

local multi_tag_handlers = {
	-- строка в нижний регистр
	strlow = function(str)
		return funcs and funcs.string_lower and funcs.string_lower(str) or str or ""
	end,

	-- ----- ЛИСТАБЕЛЬНЫЕ ПРОЕКЦИИ ПО ID -----
	nickid   = make_listable(function(id) return map_nick_raw(id) end),
	nickru   = make_listable(function(id) return map_nick_ru(id) end),
	rpnick   = make_listable(function(id) return map_rpnick(id) end),
	name	 = make_listable(function(id) return map_name(id) end),
	nameru   = make_listable(function(id) return map_name_ru(id) end),
	surname  = make_listable(function(id) return map_surname(id) end),
	surnameru= make_listable(function(id) return map_surname_ru(id) end),

	-- текущее время + смещение мин:сек
	addtime = function(param)
		local min, sec = param:match('(%d+):(%d+)')
		min, sec = tonumber(min), tonumber(sec)
		if min and sec then
			return os.date("%H:%M:%S", os.time() + (min*60) + sec)
		else
			return ""
		end
	end,

	-- Скриншот
	screen = function(param)
		local args = {}
		for arg in tostring(param):gmatch('"(.-)"') do table.insert(args, arg) end
		if #args == 0 then
			for word in tostring(param):gmatch("([^,]+)") do table.insert(args, word:match("^%s*(.-)%s*$")) end
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

-- Описания мульти-тегов (для справки)
local multi_tags_descriptions = {
	nickid = {desc="Ник игрока по ID (листабельно)", example="[nickid(1 2 3)]"},
	nickru = {desc="Русский ник по ID (листабельно)", example="[nickru(1,2,3 | \", \")]"},
	rpnick = {desc="РП-ник по ID (листабельно)", example="[rpnick(4 5 6)]"},
	name = {desc="Имя по ID (листабельно)", example="[name(1 2 3)]"},
	nameru = {desc="Имя (рус) по ID (листабельно)", example="[nameru(1 2 3 | \" / \")]"},
	surname = {desc="Фамилия по ID (листабельно)", example="[surname(1, 2, 3)]"},
	surnameru = {desc="Фамилия (рус) по ID (листабельно)", example="[surnameru(1 2 3)]"},
	strlow = {desc="Строка в нижнем регистре", example="[strlow(ТЕКСТ)]"},
        addtime = {desc="Текущее время + мин:сек", example="[addtime(\"10:10\")]"},
        screen = {desc="Сделать скриншот. Аргументы опциональны.", example='[screen("имя_файла", "папка")]'},
}

-- =======================
-- БЛОК #vars-loader: внешние переменные и функциональные переменные из HelperByOrc/vars
-- =======================

-- Глобальные регистраторы (совместимость со старыми файлами вида obs.lua)
		_G.registerVariable = function(name, desc, fn)
	if type(name) ~= "string" or type(fn) ~= "function" then return end
	-- будет делегировать на module.registerVariable (определена ниже), загрузка файлов произойдёт ПОСЛЕ её определения
	module.registerVariable(name, desc, fn)
end

		_G.registerFunctionalVariable = function(name, desc, fn, opts)
	if type(name) ~= "string" or type(fn) ~= "function" then return end
	-- регистрируем хендлер в мульти-тегах
	multi_tag_handlers[name] = function(param, thisbind_value)
		local ok, res = pcall(fn, tostring(param or ""), thisbind_value)
		if not ok then
			log_chat(("[Tags]1 Ошибка в [%s(...)]: %s"):format(name, tostring(res)), 0xAA3333)
			return "[Ошибка "..name.."]"
		end
		return res
	end
	-- описание для справки
	multi_tags_descriptions[name] = {
		desc = desc or ("Внешняя функциональная переменная '"..name.."'"),
		example = (opts and opts.example) or ("["..name.."(...)]")
	}
end

local VARS_DIR = "moonloader/HelperByOrc/vars"

local function list_lua_files(dir)
	local out = {}
	dir = tostring(dir or ""):gsub("\\","/"):gsub("/+$","")
	local ok_lfs, lfs = pcall(require, "lfs")
	if ok_lfs and lfs and lfs.attributes(dir, "mode") == "directory" then
		for f in lfs.dir(dir) do
			if f ~= "." and f ~= ".." and f:match("%.lua$") then
				out[#out+1] = dir.."/"..f
			end
		end
		return out
	end
	if io.popen then
		local cmd = ('dir /b "%s\\*.lua"'):format(dir:gsub("/","\\"))
		local p = io.popen(cmd)
		if p then
			for line in p:lines() do
				if line and line:match("%.lua$") then
					out[#out+1] = dir.."/"..line
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
			log_chat(("[Tags]2 Не удалось загрузить '%s': %s"):format(path, tostring(err)), 0xAA3333)
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
				log_chat(("[Tags]3 Ошибка при выполнении '%s': %s"):format(path, tostring(perr)), 0xAA3333)
			else
				loaded = loaded + 1
				_loaded_var_files[#_loaded_var_files+1] = path
			end
		end
	end

	clear_parse_cache()

	-- if loaded > 0 or errors > 0 then
		-- log_chat(("[Tags]4 Внешние переменные: загружено %d файл(ов), ошибок %d."):format(loaded, errors), 0xFFD700)
	-- end
end


-- Командные «строчная форма»
local command_tags = {
        { name = "$wait(expr)", desc = "Ждать до выполнения условия (строка полностью: $wait(...))", example = "$wait(time() % 2 == 0)" },
        { name = "$call(expr)", desc = "Выполнить Lua-выражение/код без вставки текста (строка полностью: $call(...))", example = "$call(module.save_config())" },
}


-- ВНЕШНИЕ ПЕРЕМЕННЫЕ (API)
local external_variables = {}
function module.registerVariable(name, desc, fn)
	external_variables[name] = {desc = desc, fn = fn}
end


-- СПИСОК ПРОСТЫХ ТЕГОВ (для справки)
local simple_tags = {
	{ name = "{id}",			desc = "Ваш ID на сервере" },
	{ name = "{nick}",		  desc = "Ваш ник (с тегом)" },
	{ name = "{nickru}",		desc = "Ваш ник (русскими буквами, без тега)" },
	{ name = "{rpnick}",		desc = "Ник для РП-формата" },
	{ name = "{name}",		  desc = "Имя до подчёркивания" },
	{ name = "{nameru}",		desc = "Имя (русскими буквами)" },
	{ name = "{surname}",	   desc = "Фамилия (после подчёркивания)" },
	{ name = "{surnameru}",	 desc = "Фамилия (русскими буквами)" },
	{ name = "{myskin}",		desc = "Ваш ID скина" },
	{ name = "{city}",		  desc = "Ваш город (по зоне GTA)" },
	{ name = "{date}",		  desc = "Текущая дата (ДД.ММ.ГГГГ)" },
	{ name = "{time}",		  desc = "Текущее время (ЧЧ:ММ:СС)" },
	{ name = "{timenosec}",	 desc = "Время (без секунд)" },
	{ name = "{myorg}",		 desc = "Ваша организация (можно изменить)" },
	{ name = "{myorgrang}",	 desc = "Ваш ранг в организации (можно изменить)" },
	{ name = "{screen}",		desc = "Сделать скриншот. По умолчанию — в стандартную папку с текущей датой" },

	-- переменные по таргету (используют ПОСЛЕДНИЙ валидный ID)
	{ name = "{targetid}",	  desc = "ID игрока, в которого вы целились последним (последний валидный)" },
	{ name = "{targetnick}",	desc = "Ник игрока последней цели (как в SAMP)" },
	{ name = "{targetrpnick}",  desc = "Ник последней цели в RP-формате (без тега, пробел вместо подчёркивания)" },
	{ name = "{targetname}",	desc = "Имя последней цели (до подчёркивания, без тега)" },
	{ name = "{targetsurname}", desc = "Фамилия последней цели (после подчёркивания, без тега)" },
}


-- ТАБЛИЦА ТЕГОВ {var}
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
					return n and strip_tag(n):match('([^_]+)') or ""
				end
				return ""
			end
		elseif key == "{nameru}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID and funcs and funcs.translite_name then
					local n = Ss.GetNameID(Ss.Local_ID())
					local nm = n and strip_tag(n):match('([^_]+)')
					return nm and funcs.translite_name(nm) or ""
				end
				return ""
			end
		elseif key == "{surname}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID then
					local n = Ss.GetNameID(Ss.Local_ID())
					return n and strip_tag(n):match('.*_(.+)') or ""
				end
				return ""
			end
		elseif key == "{surnameru}" then
			return function()
				local Ss = S_get()
				if Ss and Ss.GetNameID and Ss.Local_ID and funcs and funcs.translite_name then
					local n = Ss.GetNameID(Ss.Local_ID())
					local sn = n and strip_tag(n):match('.*_(.+)')
					return sn and funcs.translite_name(sn) or ""
				end
				return ""
			end
		elseif key == "{myskin}" then
			return function() return getCharModel and getCharModel(PLAYER_PED) or "" end
		elseif key == "{city}" then
			return function()
				local mapping = {[0]="San-Andreas",[1]="Los-Santos",[2]="San-Fierro",[3]="Las-Venturas"}
				local city = getCityPlayerIsIn and getCityPlayerIsIn(PLAYER_PED)
				return mapping[city or 0] or ""
			end
		elseif key == "{time}" then
			return function() return os.date("%H:%M:%S") end
		elseif key == "{timenosec}" then
			return function() return os.date("%H:%M") end
		elseif key == "{date}" then
			return function() return os.date("%d.%m.%Y") end

		-- TARGET-теги: используют ПОСЛЕДНИЙ ВАЛИДНЫЙ ID
		elseif key == "{targetid}" then
			return function()
				return target.last_id and tostring(target.last_id) or ""
			end
		elseif key == "{targetnick}" then
			return function()
				local id = target.last_id
				if not id then return "" end
				local n = get_nick_by_id(id)
				return n or ""
			end
		elseif key == "{targetrpnick}" then
			return function()
				local id = target.last_id
				if not id then return "" end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):gsub("_", " ") or ""
			end
		elseif key == "{targetname}" then
			return function()
				local id = target.last_id
				if not id then return "" end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):match('([^_]+)') or ""
			end
		elseif key == "{targetsurname}" then
			return function()
				local id = target.last_id
				if not id then return "" end
				local n = get_nick_by_id(id)
				return n and strip_tag(n):match('.*_(.+)') or ""
			end
		end

		-- внешние переменные вида {var}
		local keystr = key:match("^{(.+)}$")
		if keystr and external_variables[keystr] then
			return function() return external_variables[keystr].fn() end
		end
		if keystr and custom_vars[keystr] ~= nil then
			return function() return tostring(custom_vars[keystr]) end
		end
		return function() return "" end
	end
})


-- ПАРСЕР МУЛЬТИ-ТЕГОВ
local RECURSION_LIMIT = 10

local function handle_multi_tag(tag, val, thisbind_value, depth)
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then return "[Ошибка: слишком глубокая вложенность]" end
	local cache_key = tag .. "(" .. tostring(val) .. ")" .. (thisbind_value and ("|"..tostring(thisbind_value)) or "")
	local cached = cache_get(cache_key)
	if cached ~= nil then return cached end

	local handler = multi_tag_handlers[tag]
	local ok, res
	if handler then
		ok, res = pcall(handler, val, thisbind_value)
		if not ok then res = "[Ошибка парсинга тега: "..tag.."]" end
	else
		res = "[Неизвестный тег: "..tag.."]"
	end
	cache_set(cache_key, res)
	return res
end

local function parse_multi_tags(text, thisbind_value, depth)
	local out, pos = "", 1
	depth = (depth or 0) + 1
	if depth > RECURSION_LIMIT then return "[Ошибка: слишком глубокая вложенность]" end
	while true do
		local start_s, start_e, tag = text:find("%[([%w_]+)%s*%(", pos)
		if not start_s then
			out = out .. text:sub(pos)
			break
		end
		out = out .. text:sub(pos, start_s - 1)
		local depth2, i = 1, start_e + 1
		while i <= #text do
			local c = text:sub(i,i)
			if c == "(" then depth2 = depth2 + 1
			elseif c == ")" then
				depth2 = depth2 - 1
				if depth2 == 0 then
					if text:sub(i+1,i+1) == "]" then break end
				end
			end
			i = i + 1
		end
		if depth2 == 0 and text:sub(i+1,i+1) == "]" then
			local expr = text:sub(start_e+1, i-1)
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


-- $wait / $call — БЕЗОПАСНАЯ СРЕДА
local function make_safe_env()
	local env = {
		tonumber = tonumber, tostring = tostring, type = type,
		pairs = pairs, ipairs = ipairs, select = select, unpack = unpack or table.unpack,
                math = math, string = string, table = table,
                module = module,
                time = os.time, clock = os.clock,
                target_last_id = function() return target.last_id end,
        }
	return setmetatable(env, { __index = _G })
end

local function safe_load_expr(expr)
	-- попытка как "return (expr)"
	local chunk, err = load("return ("..expr..")")
	if not chunk then
		-- вторая попытка — как есть (для $call со стейтментами)
		chunk, err = load(expr)
		if not chunk then return nil, err end
	end
	setfenv(chunk, make_safe_env())
	return chunk
end

local function execute_special_commands(text)
	local lines = {}
	for line in text:gmatch("[^\r\n]+") do table.insert(lines, line) end

	local out = {}
	for _, line in ipairs(lines) do
		local expr = line:match("^%$wait%((.+)%)$") -- строка целиком
		if expr then
			local timeout = tonumber(settings.wait_timeout_sec) or 30
			local finished, timed_out = false, false
			local chunk, err = safe_load_expr(expr)
			if not chunk then
				log_chat("[Tags]5 Ошибка в $wait: "..tostring(err), 0xAA3333)
			else
				lua_thread.create(function()
					local t0 = os.clock()
					while true do
						local ok, res = pcall(chunk)
						if ok and res then finished = true break end
						if (os.clock() - t0) > timeout then
							timed_out = true
							break
						end
						wait(50)
					end
				end)
				local tstart = os.clock()
				while not finished and not timed_out do wait(25) end
				if timed_out then
					log_chat("[Tags]6 $wait: истёк таймаут "..timeout.." сек", 0xAA3333)
				end
			end
		else
			local expr2 = line:match("^%$call%((.+)%)$") -- строка целиком
			if expr2 then
				if not settings.allow_unsafe then
					log_chat("[Tags]7 $call отклонён: небезопасный режим выключен", 0xAA3333)
				else
					local chunk, err = safe_load_expr(expr2)
					if not chunk then
						log_chat("[Tags]8 Ошибка в $call: "..tostring(err), 0xAA3333)
					else
						lua_thread.create(function() pcall(chunk) end)
					end
				end
			else
				table.insert(out, line)
			end
		end
	end
	return table.concat(out, "\n")
end


-- ОСНОВНАЯ ФУНКЦИЯ ПОДСТАНОВКИ
function module.change_tags(text, thisbind_value, depth)
	clear_parse_cache()
	text = execute_special_commands(text)
	text = parse_multi_tags(text, thisbind_value, depth)
	text = text:gsub("{[%w_]+}", function(key)
		local fn = tags[key]
		if fn then
			local cache_key = key
			local c = cache_get(cache_key)
			if c ~= nil then return c end
			local ok, res = pcall(fn)
			local out = (ok and res and tostring(res) ~= key) and tostring(res) or ""
			cache_set(cache_key, out)
			return out
		end
		return ""
	end)
	text = text:gsub("[%]%)]*$", "")
	if text:match("^%s*$") then text = "" end
	return text
end


-- ПУБЛИЧНЫЕ API ПО TARGET / НАСТРОЙКИ
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
	if n and n > 0 then settings.wait_timeout_sec = n; save_config() end
end


-- СПРАВОЧНЫЕ СПИСКИ ДЛЯ UI
local function get_custom_var_list()
	local out = {}
	for k, v in pairs(custom_vars) do
		out[#out+1] = { key = k, value = v }
	end
	table.sort(out, function(a,b) return a.key < b.key end)
	return out
end

local function get_var_list()
	local out, exists = {}, {}
	for _, tag in ipairs(simple_tags) do
		table.insert(out, { name = tag.name, desc = tag.desc, custom = false })
		exists[tag.name] = true
	end
	for k, v in pairs(external_variables) do
		local tagname = "{"..k.."}"
		if not exists[tagname] then
			table.insert(out, { name = tagname, desc = v.desc or "(доп. переменная)", custom = false })
		end
	end
	table.sort(out, function(a,b) return a.name < b.name end)
	return out
end

local function get_func_list()
	local out = {}
	for tag, v in pairs(multi_tags_descriptions) do
		table.insert(out, {
			name = ("[%s(...)]"):format(tag),
			desc = v.desc,
			example = v.example or ("[%s(...)]"):format(tag)
		})
	end
	table.sort(out, function(a,b) return a.name < b.name end)
	return out
end


-- UI (mimgui): СПРАВКА / КОПИРОВАНИЕ ПО КЛИКУ
local showTagsWindow = imgui.new.bool(false)
module.showTagsWindow = showTagsWindow
local cvar_bufs = {}
local new_var_name = imgui.new.char[64]()
local new_var_value = imgui.new.char[256]()

-- состояние UI (флэш «скопировано»)
local ui_state = { copied_text = nil, copied_time = 0, flash_sec = 1.5 }

local function flash_copied(txt)
        ui_state.copied_text = txt
        ui_state.copied_time = os.clock()
end

-- Рисует страницу настроек (вкладка "Прочее")
function module.DrawSettingsPage()
    imgui.TextColored(imgui.ImVec4(0.7,1,1,1), "Переменные для сообщений, биндеров и шаблонов")
    imgui.Separator()

    imgui.Text("Настройки:")
    do
        local chk1 = ffi.new("bool[1]", settings.show_target_notice and true or false)
        if imgui.Checkbox("Показывать уведомление о {targetid}", chk1) then
            settings.show_target_notice = chk1[0] and true or false
            save_config()
        end

        imgui.SameLine()
        local chk2 = ffi.new("bool[1]", settings.allow_unsafe and true or false)
        if imgui.Checkbox("Разрешить $call (небезопасно)", chk2) then
            settings.allow_unsafe = chk2[0] and true or false
            save_config()
        end

        local wt = ffi.new("int[1]", settings.wait_timeout_sec or 30)
        if imgui.InputInt("Таймаут $wait, сек", wt) then
            if wt[0] < 1 then wt[0] = 1 end
            settings.wait_timeout_sec = wt[0]
            save_config()
        end
    end

    imgui.Separator()
    imgui.Text("Кастомные переменные:")
    for _, tag in ipairs(get_custom_var_list()) do
        local buf = cvar_bufs[tag.key]
        if not buf then
            buf = imgui.new.char[256](tostring(tag.value or ""))
            cvar_bufs[tag.key] = buf
        end
        imgui.PushID(tag.key)
        imgui.Text(tag.key)
        imgui.SameLine()
        if imgui.InputText("##val", buf, ffi.sizeof(buf)) then
            custom_vars[tag.key] = ffi.string(buf)
            save_config()
            clear_parse_cache()
        end
        imgui.PopID()
    end

    imgui.Separator()
    imgui.Text("Добавить переменную:")
    imgui.InputText("Имя##newvar", new_var_name, ffi.sizeof(new_var_name))
    imgui.SameLine()
    imgui.InputText("Значение##newvar", new_var_value, ffi.sizeof(new_var_value))
    imgui.SameLine()
    if imgui.Button("Добавить##newvar") then
        local name = ffi.string(new_var_name)
        if name ~= "" then
            local value = ffi.string(new_var_value)
            custom_vars[name] = value
            cvar_bufs[name] = imgui.new.char[256](value)
            new_var_name = imgui.new.char[64]()
            new_var_value = imgui.new.char[256]()
            save_config()
            clear_parse_cache()
        end
    end

    imgui.Separator()
    if imgui.Button("Открыть список переменных") then
        showTagsWindow[0] = true
    end
end

imgui.OnFrame(
        function() return showTagsWindow[0] end,
        function()
                imgui.SetNextWindowSize(imgui.ImVec2(780, 680), imgui.Cond.FirstUseEver)
                imgui.Begin("Справка по тегам / HelperByOrc", showTagsWindow, imgui.WindowFlags.NoCollapse)

		imgui.TextColored(imgui.ImVec4(0.7,1,1,1), "Переменные для сообщений, биндеров и шаблонов")
		imgui.Separator()

		-- Настройки
		imgui.Text("Настройки:")
		do
			local chk1 = ffi.new("bool[1]", settings.show_target_notice and true or false)
			if imgui.Checkbox("Показывать уведомление о {targetid}", chk1) then
				settings.show_target_notice = chk1[0] and true or false
				save_config()
			end

			imgui.SameLine()
			local chk2 = ffi.new("bool[1]", settings.allow_unsafe and true or false)
			if imgui.Checkbox("Разрешить $call (небезопасно)", chk2) then
				settings.allow_unsafe = chk2[0] and true or false
				save_config()
			end

			local wt = ffi.new("int[1]", settings.wait_timeout_sec or 30)
			if imgui.InputInt("Таймаут $wait, сек", wt) then
				if wt[0] < 1 then wt[0] = 1 end
				settings.wait_timeout_sec = wt[0]
				save_config()
			end
		end

	imgui.Separator()
	imgui.Text("Кастомные переменные (клик по имени — копировать):")
	imgui.Columns(2, "cvars", false)
	for i, tag in ipairs(get_custom_var_list()) do
		if imgui.Selectable("{"..tag.key.."}##cvar"..tostring(i), false) then
		imgui.SetClipboardText("{"..tag.key.."}")
		flash_copied("Скопировано: {"..tag.key.."}")
		end
		imgui.NextColumn()
		imgui.TextWrapped(tostring(tag.value))
		imgui.NextColumn()
		end
	imgui.Columns(1)
	imgui.Separator()
	imgui.Text("Доступные переменные (клик по имени — копировать):")
	imgui.Columns(2, "vars", false)
	for i, tag in ipairs(get_var_list()) do
			if imgui.Selectable((tag.name).."##var"..tostring(i), false) then
				imgui.SetClipboardText(tag.name)
				flash_copied("Скопировано: "..tag.name)
			end
			if imgui.IsItemHovered() then
				imgui.BeginTooltip()
				imgui.Text("Кликните, чтобы скопировать")
				imgui.EndTooltip()
			end
			imgui.NextColumn()
			imgui.TextWrapped(tag.desc)
			imgui.NextColumn()
		end
		imgui.Columns(1)

		imgui.Separator()
		imgui.Text("Функции-теги (клик по имени — копировать пример):")
		imgui.Columns(2, "funcs", false)
		for i, tag in ipairs(get_func_list()) do
			local to_copy = tag.example or tag.name
			if imgui.Selectable((tag.name).."##fn"..tostring(i), false) then
				imgui.SetClipboardText(to_copy)
				flash_copied("Скопировано: "..to_copy)
			end
			if imgui.IsItemHovered() then
				imgui.BeginTooltip()
				imgui.Text("Копировать пример")
				imgui.EndTooltip()
			end
			imgui.NextColumn()
			imgui.TextWrapped(tag.desc..(tag.example and ("  Пример: "..tag.example) or ""))
			imgui.NextColumn()
		end
		imgui.Columns(1)

		imgui.Separator()
		imgui.Text("Командные (строка целиком):")
		for _, t in ipairs(command_tags) do
			imgui.Text(t.name.." — "..t.desc.."  Пример: "..t.example)
		end

		-- Флэш «Скопировано»
		do
			local dt = os.clock() - (ui_state.copied_time or 0)
			if ui_state.copied_text and dt < (ui_state.flash_sec or 1.5) then
				imgui.Spacing()
				imgui.TextColored(imgui.ImVec4(0.5, 1.0, 0.5, 1.0), ui_state.copied_text)
			end
		end

		imgui.Spacing()
		if imgui.Button("Закрыть") then showTagsWindow[0] = false end

		imgui.End()
	end
)


-- ВНЕШНИЕ СЕРВИСНЫЕ ФУНКЦИИ (СОХРАНИТЬ/ПЕРЕЧИТАТЬ)
module.save_config = save_custom_vars
module.reload_config = function()
	load_custom_vars()
	clear_parse_cache()
	pcall(load_external_vars) -- подтянуть обновлённые obs.lua и прочие файлы
end
module.reload_external_vars = function()
	pcall(load_external_vars)
end

-- автозагрузка внешних переменных из HelperByOrc/vars при старте (после определения API registerVariable)
pcall(load_external_vars)

return module
