-- neatjson.lua
-- NeatJSON pretty printer (Lua 5.1 / LuaJIT 2.1)
--
-- Основная идея сохранена: тот же API и те же опции форматирования.
-- Оптимизации и фиксы сделаны так, чтобы по умолчанию вывод оставался максимально близким к оригиналу.
--
-- Новые (необязательные) опции:
--   opts.strictArrays       : boolean (default false)
--     Строгая проверка массивов: допускаются только ключи 1..N без дырок.
--     Полезно, потому что оператор # для таблиц с "holes" формально не определён и может давать неожиданные границы.
--
--   opts.strictJsonStrings  : boolean (default false)
--     Делает корректное JSON-экранирование строк (control chars -> \u00XX и т.п.)
--     По умолчанию оставлено старое поведение через %q (Lua-литерал, не всегда валидный JSON).
--
--   opts.detectCircular     : boolean (default false)
--     Детект циклических ссылок в таблицах.
--     opts.circular: "error" (default) | "null" | "string"
--
-- Важные багфиксы:
--   - trimTrailingZeros больше не ломает indent (раньше мог съесть отступ)
--   - числовые ключи не ломают %q (ключи приводятся к строке в JSON, в Lua -> [1])
--
-- Главная оптимизация:
--   - ранний abort при попытке one-line: если длина уже превысила wrap, не строим оставшуюся часть one-line.

-- Локализация часто используемых функций (ускорение, особенно в обычной Lua; в LuaJIT профит зависит от кейса)
local type, next, pairs, ipairs = type, next, pairs, ipairs
local tostring, tonumber = tostring, tonumber
local modf, fmod, huge, floor = math.modf, math.fmod, math.huge, math.floor
local srep, sformat, smatch, sgsub, sfind = string.rep, string.format, string.match, string.gsub, string.find
local tconcat, tsort = table.concat, table.sort

-- Lua keywords (для opts.lua режима)
local keywords = {
	["and"]=1,["break"]=1,["do"]=1,["else"]=1,["elseif"]=1,["end"]=1,["false"]=1,["for"]=1,["function"]=1,
	["goto"]=1,["if"]=1,["in"]=1,["local"]=1,["nil"]=1,["not"]=1,["or"]=1,["repeat"]=1,["return"]=1,
	["then"]=1,["true"]=1,["until"]=1,["while"]=1
}

-- Проверка: массив ли таблица.
-- strict=false: совместимость со старым поведением (через #t).
-- strict=true : только 1..N без дырок.
local function isarray(t, emptyIsObject, strict)
	if type(t) ~= 'table' then return false end
	if not next(t) then return not emptyIsObject end

	if not strict then
		local len = #t
		for k,_ in pairs(t) do
			if type(k) ~= 'number' then return false end
			local _, frac = modf(k)
			if frac ~= 0 or k < 1 or k > len then return false end
		end
		return true
	end

	local maxk, count = 0, 0
	for k,_ in pairs(t) do
		if type(k) ~= 'number' then return false end
		local _, frac = modf(k)
		if frac ~= 0 or k < 1 then return false end
		if k > maxk then maxk = k end
		count = count + 1
	end
	return count == maxk
end

-- Lua-литерал строки (поведение “как было”)
local function quoteLuaString(s)
	return sformat('%q', s):gsub('\\\n', '\\n')
end

-- Корректное JSON-экранирование (включается opts.strictJsonStrings)
local function escapeJsonString(s)
	return (s:gsub('[%z\1-\31\\"]', function(c)
		if c == '"'  then return '\\"' end
		if c == '\\' then return '\\\\' end
		if c == '\b' then return '\\b' end
		if c == '\f' then return '\\f' end
		if c == '\n' then return '\\n' end
		if c == '\r' then return '\\r' end
		if c == '\t' then return '\\t' end
		return sformat('\\u%04x', c:byte())
	end))
end

-- Ключ в Lua:
--  - число -> [1]
--  - валидный идентификатор -> name
--  - иначе -> ["строка"] (через %q, как в оригинале)
local function luaKey(k)
	local tk = type(k)
	if tk == 'number' then
		return '[' .. tostring(k) .. ']'
	end
	if tk == 'string' then
		if (not keywords[k]) and smatch(k, '^[%a_][%w_]*$') then
			return k
		end
		return '[' .. quoteLuaString(k) .. ']'
	end
	return '[' .. quoteLuaString(tostring(k)) .. ']'
end

local function neatJSON(value, opts)
	opts = opts or {}

	-- дефолты как в оригинале
	if opts.wrap == nil  then opts.wrap = 80 end
	if opts.wrap == true then opts.wrap = -1 end
	opts.indent         = opts.indent         or "  "
	opts.arrayPadding   = opts.arrayPadding   or opts.padding      or 0
	opts.objectPadding  = opts.objectPadding  or opts.padding      or 0
	opts.afterComma     = opts.afterComma     or opts.aroundComma  or 0
	opts.beforeComma    = opts.beforeComma    or opts.aroundComma  or 0
	opts.beforeColon    = opts.beforeColon    or opts.aroundColon  or 0
	opts.afterColon     = opts.afterColon     or opts.aroundColon  or 0
	opts.beforeColon1   = opts.beforeColon1   or opts.aroundColon1 or opts.beforeColon or 0
	opts.afterColon1    = opts.afterColon1    or opts.aroundColon1 or opts.afterColon  or 0
	opts.beforeColonN   = opts.beforeColonN   or opts.aroundColonN or opts.beforeColon or 0
	opts.afterColonN    = opts.afterColonN    or opts.aroundColonN or opts.afterColon  or 0

	-- новые опции (по умолчанию выключены)
	local strictArrays      = not not opts.strictArrays
	local strictJsonStrings = not not opts.strictJsonStrings
	local detectCircular    = not not opts.detectCircular
	local circularMode      = opts.circular or "error" -- "error" | "null" | "string"

	-- lookup: какие ключи принудительно печатать как float
	local floatsForcedForKey = {}
	do
		local list = opts.forceFloatsIn or {}
		for i = 1, #list do
			floatsForcedForKey[list[i]] = true
		end
	end

	local colon  = opts.lua and '=' or ':'
	local arrayB = opts.lua and {'{','}'} or {'[',']'}
	local apad   = srep(' ', opts.arrayPadding)
	local opad   = srep(' ', opts.objectPadding)
	local comma  = srep(' ', opts.beforeComma) .. ',' .. srep(' ', opts.afterComma)
	local colon1 = srep(' ', opts.beforeColon1) .. colon .. srep(' ', opts.afterColon1)
	local colonN = srep(' ', opts.beforeColonN) .. colon .. srep(' ', opts.afterColonN)

	-- Кэш для indent .. opts.indent (уменьшаем число аллокаций строк на глубине)
	local indentNextCache = {}
	local function nextIndent(indent)
		local v = indentNextCache[indent]
		if not v then
			v = indent .. opts.indent
			indentNextCache[indent] = v
		end
		return v
	end

	-- Ключ в JSON: по спецификации это строка.
	-- По умолчанию оставляем “как было” через %q, но приводим ключ к tostring(), чтобы не падать на числах.
	local function jsonKey(k)
		local s = tostring(k)
		if strictJsonStrings then
			return '"' .. escapeJsonString(s) .. '"'
		end
		return quoteLuaString(s)
	end

	-- Строка значения
	local function quoteValueString(s)
		if (not opts.lua) and strictJsonStrings then
			return '"' .. escapeJsonString(s) .. '"'
		end
		return quoteLuaString(s)
	end

	local build -- будет присвоено ниже

	-- Для детекта циклов (опционально)
	local inProgress = detectCircular and {} or nil

	-- Попытка собрать массив в одну строку с ранним abort по wrap.
	-- Возвращает oneLine или nil (если нужно переносить).
	local function tryArrayOneLine(o, indent, floatsForced)
		if opts.wrap == false then
			-- wrap выключен: всегда oneLine
			local pieces = {}
			for i = 1, #o do pieces[i] = build(o[i], '', floatsForced) end
			return indent .. arrayB[1] .. apad .. tconcat(pieces, comma) .. apad .. arrayB[2]
		end
		if opts.wrap < 0 then
			-- wrap=true -> -1: всегда переносы
			return nil
		end

		local baseLen = #indent + #arrayB[1] + #apad + #apad + #arrayB[2]
		local sumLen = 0
		local pieces = {}

		for i = 1, #o do
			local s = build(o[i], '', floatsForced)
			pieces[i] = s
			if i == 1 then
				sumLen = sumLen + #s
			else
				sumLen = sumLen + #comma + #s
			end
			if baseLen + sumLen > opts.wrap then
				return nil
			end
		end

		return indent .. arrayB[1] .. apad .. tconcat(pieces, comma) .. apad .. arrayB[2]
	end

	-- Попытка собрать объект в одну строку с ранним abort по wrap.
	-- Возвращает oneLine или nil.
	local function tryObjectOneLine(sortedKV, indent)
		if opts.wrap ~= false and opts.wrap < 0 then
			return nil
		end

		local pieces = {}
		local baseLen = #indent + 2 + (#opad * 2) -- indent + "{" + "}" + pads
		local sumLen = 0

		for i = 1, #sortedKV do
			local k = sortedKV[i][1]
			local v = sortedKV[i][2]
			local ff = opts.forceFloats or floatsForcedForKey[k]

			local kstr = opts.lua and luaKey(k) or jsonKey(k)
			local part = kstr .. colon1 .. build(v, '', ff)
			pieces[i] = part

			if i == 1 then
				sumLen = sumLen + #part
			else
				sumLen = sumLen + #comma + #part
			end

			if opts.wrap ~= false and (baseLen + sumLen > opts.wrap) then
				return nil
			end
		end

		return indent .. '{' .. opad .. tconcat(pieces, comma) .. opad .. '}'
	end

	-- Низкоуровневая сборка (без мемоизации)
	local function rawBuild(o, indent, floatsForced)
		if o == nil then
			return indent .. (opts.lua and 'nil' or 'null')
		end

		local kind = type(o)

		if kind == 'number' then
			local treatAsFloat = floatsForced or (fmod(o, 1) ~= 0)

			local num
			if treatAsFloat and opts.decimals then
				num = sformat('%.' .. opts.decimals .. 'f', o)
			else
				num = sformat('%.10g', o)
			end

			-- BUGFIX: trimTrailingZeros раньше мог уничтожать indent, потому что tonumber применяли к indent..num
			if opts.trimTrailingZeros then
				local n = tonumber(num)
				if n then
					if fmod(n, 1) == 0 then
						num = tostring(floor(n))
					else
						num = tostring(n)
					end
				end
			end

			if floatsForced and not sfind(num, '%.') then
				num = num .. '.0'
			end

			return indent .. num

		elseif kind == 'boolean' then
			return indent .. tostring(o)

		elseif kind == 'string' then
			return indent .. quoteValueString(o)

		elseif kind == 'table' then
			-- циклы (опционально)
			if detectCircular then
				if inProgress[o] then
					if circularMode == "null" then
						return indent .. (opts.lua and 'nil' or 'null')
					elseif circularMode == "string" then
						return indent .. (opts.lua and quoteLuaString("[Circular]") or '"[Circular]"')
					end
					error("neatJSON: circular reference detected", 2)
				end
				inProgress[o] = true
			end

			-- удобный helper, чтобы гарантированно чистить inProgress перед return
			local function done(ret)
				if detectCircular then inProgress[o] = nil end
				return ret
			end

			-- массив?
			if isarray(o, opts.emptyTablesAreObjects, strictArrays) then
				if #o == 0 then
					return done(indent .. arrayB[1] .. arrayB[2])
				end

				-- oneLine attempt (с ранним abort)
				local oneLine = tryArrayOneLine(o, indent, floatsForced)
				if oneLine then
					return done(oneLine)
				end

				-- переносы
				if opts.short then
					local indent2 = indent .. ' ' .. apad
					local pieces = {}
					for i = 1, #o do
						pieces[i] = build(o[i], indent2, floatsForced)
					end
					pieces[1] = pieces[1]:gsub(indent2, indent .. arrayB[1] .. apad, 1)
					pieces[#pieces] = pieces[#pieces] .. apad .. arrayB[2]
					return done(tconcat(pieces, ',\n'))
				else
					local indent2 = nextIndent(indent)
					local lines = {}
					for i = 1, #o do
						lines[i] = build(o[i], indent2, floatsForced)
					end
					return done(
						indent .. arrayB[1] .. '\n'
						.. tconcat(lines, ',\n') .. '\n'
						.. (opts.indentLast and indent2 or indent) .. arrayB[2]
					)
				end
			end

			-- объект
			if not next(o) then
				return done(indent .. '{}')
			end

			-- собрать пары и отсортировать при необходимости
			local sortedKV = {}
			local sort = opts.sort or opts.sorted
			for k, v in pairs(o) do
				local kt = type(k)
				if kt == 'string' or kt == 'number' then
					local item = {k, v}
					if sort == true then
						item[3] = tostring(k)
					elseif type(sort) == 'function' then
						item[3] = sort(k, v, o)
					end
					sortedKV[#sortedKV + 1] = item
				end
			end
			if sort then
				tsort(sortedKV, function(a, b) return a[3] < b[3] end)
			end

			-- oneLine attempt (с ранним abort)
			local oneLine = tryObjectOneLine(sortedKV, indent)
			if oneLine then
				return done(oneLine)
			end

			-- переносы
			if opts.short then
				-- short: ключи по одному на строку, значения стараемся держать рядом
				local kv = {} -- {keyPrefix, value} либо уже готовая строка
				for i = 1, #sortedKV do
					local k = sortedKV[i][1]
					local v = sortedKV[i][2]
					local kstr = opts.lua and luaKey(k) or jsonKey(k)
					kv[i] = { (i == 1) and (indent .. '{' .. opad .. kstr) or (indent .. ' ' .. opad .. kstr), v, k }
				end

				-- aligned: выравнивание по длине префикса ключа
				if opts.aligned then
					local longest = 0
					for i = 1, #kv do
						local l = #kv[i][1]
						if l > longest then longest = l end
					end
					local padrt = '%-' .. longest .. 's'
					for i = 1, #kv do
						kv[i][1] = sformat(padrt, kv[i][1])
					end
				end

				for i = 1, #kv do
					local kprefix = kv[i][1]
					local v = kv[i][2]
					local origKey = kv[i][3]
					local ff = opts.forceFloats or floatsForcedForKey[origKey]

					local alignIndent = srep(' ', #(kprefix .. colonN))
					local lineOne = kprefix .. colonN .. build(v, '', ff)

					if opts.wrap == false or #lineOne <= opts.wrap or (not v) or type(v) ~= 'table' then
						kv[i] = lineOne
					else
						kv[i] = kprefix .. colonN .. build(v, alignIndent, ff):gsub('^%s+', '', 1)
					end
				end

				return done(tconcat(kv, ',\n') .. opad .. '}')
			else
				-- обычный многострочный режим
				local indent2 = nextIndent(indent)
				local kvp = {} -- {keyPrefix, value, origKey}

				for i = 1, #sortedKV do
					local k = sortedKV[i][1]
					local v = sortedKV[i][2]
					local kstr = opts.lua and luaKey(k) or jsonKey(k)
					kvp[i] = { indent2 .. kstr, v, k }
				end

				if opts.aligned then
					local longest = 0
					for i = 1, #kvp do
						local l = #kvp[i][1]
						if l > longest then longest = l end
					end
					local padrt = '%-' .. longest .. 's'
					for i = 1, #kvp do
						kvp[i][1] = sformat(padrt, kvp[i][1])
					end
				end

				local lines = {}
				for i = 1, #kvp do
					local kprefix = kvp[i][1]
					local v = kvp[i][2]
					local origKey = kvp[i][3]
					local ff = opts.forceFloats or floatsForcedForKey[origKey]

					local lineOne = kprefix .. colonN .. build(v, '', ff)
					if opts.wrap == false or #lineOne <= opts.wrap or (not v) or type(v) ~= 'table' then
						lines[i] = lineOne
					else
						lines[i] = kprefix .. colonN .. build(v, indent2, ff):gsub('^%s+', '', 1)
					end
				end

				return done(
					indent .. '{\n'
					.. tconcat(lines, ',\n') .. '\n'
					.. (opts.indentLast and indent2 or indent) .. '}'
				)
			end
		end

		-- неизвестный тип: как в духе JSON -> null (или nil в lua-режиме)
		return indent .. (opts.lua and 'nil' or 'null')
	end

	-- Мемоизация: object -> indent -> floatsForced
	-- Слабые ключи, чтобы не держать большие структуры в памяти.
	local function memoize()
		local memo = setmetatable({}, { _mode = 'k' })

		return function(o, indent, floatsForced)
			-- спец числа (NaN/inf)
			if o == nil then
				return indent .. (opts.lua and 'nil' or 'null')
			elseif o ~= o then
				return indent .. (opts.lua and '0/0' or '"NaN"')
			elseif o == huge then
				return indent .. (opts.lua and '1/0' or '9e9999')
			elseif o == -huge then
				return indent .. (opts.lua and '-1/0' or '-9e9999')
			end

			local byIndent = memo[o]
			if not byIndent then
				byIndent = setmetatable({}, { _mode = 'k' })
				memo[o] = byIndent
			end

			local byFloat = byIndent[indent]
			if not byFloat then
				byFloat = setmetatable({}, { _mode = 'k' })
				byIndent[indent] = byFloat
			end

			floatsForced = not not floatsForced
			local cached = byFloat[floatsForced]
			if cached == nil then
				cached = rawBuild(o, indent, floatsForced)
				byFloat[floatsForced] = cached
			end
			return cached
		end
	end

	build = memoize()
	return build(value, '', opts.forceFloats)
end

return neatJSON
