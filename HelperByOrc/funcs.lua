local module = {}
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ffi = require("ffi")
local memory = require("memory")
-- recursive table copy
function module.deepcopy(obj, seen)
	seen = seen or {}
	if type(obj) ~= "table" then
		return obj
	end
	if seen[obj] then
		return seen[obj]
	end
	local res = {}
	seen[obj] = res
	for k, v in pairs(obj) do
		res[module.deepcopy(k, seen)] = module.deepcopy(v, seen)
	end
	return res
end

ffi.cdef([[
	intptr_t LoadKeyboardLayoutA(const char* pwszKLID, unsigned int Flags);
	int PostMessageA(intptr_t hWnd, unsigned int Msg, unsigned int wParam, long lParam);
	intptr_t GetActiveWindow();
	
	typedef void* HANDLE;
	typedef void* LPSECURITY_ATTRIBUTES;
	typedef unsigned long DWORD;
	typedef int BOOL;
	typedef const char *LPCSTR;
	typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
	} FILETIME, *PFILETIME, *LPFILETIME;

	BOOL __stdcall GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime);
	HANDLE __stdcall CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
	BOOL __stdcall CloseHandle(HANDLE hObject);
	
	bool SetCursorPos(int X, int Y);
	
	typedef uint32_t DWORD;
	typedef int BOOL;
	void* OpenProcess(uint32_t dwDesiredAccess, int bInheritHandle, uint32_t dwProcessId);
	int CloseHandle(void* hObject);
	void* GetModuleHandleA(const char* lpModuleName);
	int VirtualQueryEx(void* hProcess, const void* lpAddress, void* lpBuffer, size_t dwLength);
	int ReadProcessMemory(void* hProcess, const void* lpBaseAddress, void* lpBuffer, size_t nSize, size_t* lpNumberOfBytesRead);
	uint32_t GetCurrentProcessId(void);
	typedef struct {
		void* BaseAddress;
		void* AllocationBase;
		uint32_t AllocationProtect;
		size_t RegionSize;
		uint32_t State;
		uint32_t Protect;
		uint32_t Type;
	} MEMORY_BASIC_INFORMATION;
	
	
]])

-- NeatJSON
function isarray(t, emptyIsObject)
	if type(t) ~= "table" then
		return false
	end
	if not next(t) then
		return not emptyIsObject
	end
	local len = #t
	for k, _ in pairs(t) do
		if type(k) ~= "number" then
			return false
		else
			local _, frac = math.modf(k)
			if frac ~= 0 or k < 1 or k > len then
				return false
			end
		end
	end
	return true
end

function map(t, f)
	local r = {}
	for i, v in ipairs(t) do
		r[i] = f(v)
	end
	return r
end

local keywords = {
	["and"] = 1,
	["break"] = 1,
	["do"] = 1,
	["else"] = 1,
	["elseif"] = 1,
	["end"] = 1,
	["false"] = 1,
	["for"] = 1,
	["function"] = 1,
	["goto"] = 1,
	["if"] = 1,
	["in"] = 1,
	["local"] = 1,
	["nil"] = 1,
	["not"] = 1,
	["or"] = 1,
	["repeat"] = 1,
	["return"] = 1,
	["then"] = 1,
	["true"] = 1,
	["until"] = 1,
	["while"] = 1,
}

function neatJSON(value, opts) -- https://github.com/Phrogz/NeatJSON
	opts = opts or {}
	if opts.wrap == nil then
		opts.wrap = 80
	end
	if opts.wrap == true then
		opts.wrap = -1
	end
	opts.indent = opts.indent or "   "
	opts.arrayPadding = opts.arrayPadding or opts.padding or 0
	opts.objectPadding = opts.objectPadding or opts.padding or 0
	opts.afterComma = opts.afterComma or opts.aroundComma or 0
	opts.beforeComma = opts.beforeComma or opts.aroundComma or 0
	opts.beforeColon = opts.beforeColon or opts.aroundColon or 0
	opts.afterColon = opts.afterColon or opts.aroundColon or 0
	opts.beforeColon1 = opts.beforeColon1 or opts.aroundColon1 or opts.beforeColon or 0
	opts.afterColon1 = opts.afterColon1 or opts.aroundColon1 or opts.afterColon or 0
	opts.beforeColonN = opts.beforeColonN or opts.aroundColonN or opts.beforeColon or 0
	opts.afterColonN = opts.afterColonN or opts.aroundColonN or opts.afterColon or 0

	-- Convert array to a lookup table for convenience
	local floatsForcedForKey = {}
	for _, key in ipairs(opts.forceFloatsIn or {}) do
		floatsForcedForKey[key] = true
	end

	local colon = opts.lua and "=" or ":"
	local array = opts.lua and { "{", "}" } or { "[", "]" }
	local apad = string.rep(" ", opts.arrayPadding)
	local opad = string.rep(" ", opts.objectPadding)
	local comma = string.rep(" ", opts.beforeComma) .. "," .. string.rep(" ", opts.afterComma)
	local colon1 = string.rep(" ", opts.beforeColon1) .. colon .. string.rep(" ", opts.afterColon1)
	local colonN = string.rep(" ", opts.beforeColonN) .. colon .. string.rep(" ", opts.afterColonN)

	local build -- set lower
	function rawBuild(o, indent, floatsForced)
		if o == nil then
			return indent .. "null"
		else
			local kind = type(o)
			if kind == "number" then
				local treatAsFloat = floatsForced or (math.fmod(o, 1) ~= 0)
				local result = indent
					.. string.format(treatAsFloat and opts.decimals and ("%." .. opts.decimals .. "f") or "%.10g", o)
				if opts.trimTrailingZeros then
					result = tonumber(result)
					result = tostring((math.fmod(result, 1) == 0) and math.floor(result) or result)
				end
				if floatsForced and not result:find("%.") then
					result = result .. ".0"
				end
				return result
			elseif kind == "boolean" or kind == "nil" then
				return indent .. tostring(o)
			elseif kind == "string" then
				return indent .. string.format("%q", o):gsub("\\\n", "\\n")
			elseif isarray(o, opts.emptyTablesAreObjects) then
				if #o == 0 then
					return indent .. array[1] .. array[2]
				end
				local pieces = map(o, function(v)
					return build(v, "", floatsForced)
				end)
				local oneLine = indent .. array[1] .. apad .. table.concat(pieces, comma) .. apad .. array[2]
				if opts.wrap == false or #oneLine <= opts.wrap then
					return oneLine
				end
				if opts.short then
					local indent2 = indent .. " " .. apad
					pieces = map(o, function(v)
						return build(v, indent2, floatsForced)
					end)
					pieces[1] = pieces[1]:gsub(indent2, indent .. array[1] .. apad, 1)
					pieces[#pieces] = pieces[#pieces] .. apad .. array[2]
					return table.concat(pieces, ",\n")
				else
					local indent2 = indent .. opts.indent
					return indent
						.. array[1]
						.. "\n"
						.. table.concat(
							map(o, function(v)
								return build(v, indent2, floatsForced)
							end),
							",\n"
						)
						.. "\n"
						.. (opts.indentLast and indent2 or indent)
						.. array[2]
				end
			elseif kind == "table" then
				if not next(o) then
					return indent .. "{}"
				end

				local sortedKV = {}
				local sort = opts.sort or opts.sorted
				for k, v in pairs(o) do
					local kind = type(k)
					if kind == "string" or kind == "number" then
						sortedKV[#sortedKV + 1] = { k, v }
						if sort == true then
							sortedKV[#sortedKV][3] = tostring(k)
						elseif type(sort) == "function" then
							sortedKV[#sortedKV][3] = sort(k, v, o)
						end
					end
				end
				if sort then
					table.sort(sortedKV, function(a, b)
						return a[3] < b[3]
					end)
				end
				local keyvals
				if opts.lua then
					keyvals = map(sortedKV, function(kv)
						local isFloatKey = opts.forceFloats or floatsForcedForKey[kv[1]]
						if type(kv[1]) == "string" and not keywords[kv[1]] and string.match(kv[1], "^[%a_][%w_]*$") then
							return string.format("%s%s%s", kv[1], colon1, build(kv[2], "", isFloatKey))
						else
							return string.format("[%q]%s%s", kv[1], colon1, build(kv[2], "", isFloatKey))
						end
					end)
				else
					keyvals = map(sortedKV, function(kv)
						return string.format(
							"%q%s%s",
							kv[1],
							colon1,
							build(kv[2], "", opts.forceFloats or floatsForcedForKey[kv[1]])
						)
					end)
				end
				keyvals = table.concat(keyvals, comma)
				local oneLine = indent .. "{" .. opad .. keyvals .. opad .. "}"
				if opts.wrap == false or #oneLine < opts.wrap then
					return oneLine
				end
				if opts.short then
					keyvals = map(sortedKV, function(kv)
						return { indent .. " " .. opad .. string.format("%q", kv[1]), kv[2] }
					end)
					keyvals[1][1] = keyvals[1][1]:gsub(indent .. " ", indent .. "{", 1)
					if opts.aligned then
						local longest = math.max(table.unpack(map(keyvals, function(kv)
							return #kv[1]
						end)))
						local padrt = "%-" .. longest .. "s"
						for _, kv in ipairs(keyvals) do
							kv[1] = padrt:format(kv[1])
						end
					end
					for i, kv in ipairs(keyvals) do
						local k, v = kv[1], kv[2]
						local indent2 = string.rep(" ", #(k .. colonN))
						floatsForced = opts.forceFloats or floatsForcedForKey[sortedKV[i][1]]
						local oneLine = k .. colonN .. build(v, "", floatsForced)
						if opts.wrap == false or #oneLine <= opts.wrap or not v or type(v) ~= "table" then
							keyvals[i] = oneLine
						else
							keyvals[i] = k .. colonN .. build(v, indent2, floatsForced):gsub("^%s+", "", 1)
						end
					end
					return table.concat(keyvals, ",\n") .. opad .. "}"
				else
					local keyvals
					if opts.lua then
						keyvals = map(sortedKV, function(kv)
							if
								type(kv[1]) == "string"
								and not keywords[kv[1]]
								and string.match(kv[1], "^[%a_][%w_]*$")
							then
								return { table.concat({ indent, opts.indent, kv[1] }), kv[2] }
							else
								return { string.format("%s%s[%q]", indent, opts.indent, kv[1]), kv[2] }
							end
						end)
					else
						keyvals = {}
						for i, kv in ipairs(sortedKV) do
							keyvals[i] = { indent .. opts.indent .. string.format("%q", kv[1]), kv[2] }
						end
					end
					if opts.aligned then
						local longest = math.max(table.unpack(map(keyvals, function(kv)
							return #kv[1]
						end)))
						local padrt = "%-" .. longest .. "s"
						for _, kv in ipairs(keyvals) do
							kv[1] = padrt:format(kv[1])
						end
					end
					local indent2 = indent .. opts.indent
					for i, kv in ipairs(keyvals) do
						floatsForced = opts.forceFloats or floatsForcedForKey[sortedKV[i][1]]
						local k, v = kv[1], kv[2]
						local oneLine = k .. colonN .. build(v, "", floatsForced)
						if opts.wrap == false or #oneLine <= opts.wrap or not v or type(v) ~= "table" then
							keyvals[i] = oneLine
						else
							keyvals[i] = k .. colonN .. build(v, indent2, floatsForced):gsub("^%s+", "", 1)
						end
					end
					return indent
						.. "{\n"
						.. table.concat(keyvals, ",\n")
						.. "\n"
						.. (opts.indentLast and indent2 or indent)
						.. "}"
				end
			end
		end
	end

	-- indexed by object, then by indent level, then by floatsForced
	function memoize()
		local memo = setmetatable({}, { _mode = "k" })
		return function(o, indent, floatsForced)
			if o == nil then
				return indent .. (opts.lua and "nil" or "null")
			elseif o ~= o then --test for NaN
				return indent .. (opts.lua and "0/0" or '"NaN"')
			elseif o == math.huge then
				return indent .. (opts.lua and "1/0" or "9e9999")
			elseif o == -math.huge then
				return indent .. (opts.lua and "-1/0" or "-9e9999")
			end
			local byIndent = memo[o]
			if not byIndent then
				byIndent = setmetatable({}, { _mode = "k" })
				memo[o] = byIndent
			end
			local byFloatForce = byIndent[indent]
			if not byFloatForce then
				byFloatForce = setmetatable({}, { _mode = "k" })
				byIndent[indent] = byFloatForce
			end
			floatsForced = not not floatsForced -- convert nil to false
			if not byFloatForce[floatsForced] then
				byFloatForce[floatsForced] = rawBuild(o, indent, floatsForced)
			end
			return byFloatForce[floatsForced]
		end
	end

	build = memoize()
	return build(value, "", opts.forceFloats)
end
-- NeatJSON

function module.savejson(table, path)
	local f = io.open(path, "w")
	if f ~= nil then
		f:write(table)
		f:close()
	end
end

function module.convertTableToJsonString(config)
	return (
		neatJSON(
			config,
			{ wrap = 40, short = true, sort = true, aligned = true, arrayPadding = 1, afterComma = 1, beforeColon1 = 1 }
		)
	)
end

function module.saveTableToJson(tbl, path)
	local ok, err = pcall(function()
		local f = io.open(path, "w+b")
		if f then
			f:write(module.convertTableToJsonString(tbl))
			f:close()
		end
	end)
	return ok
end

function module.loadTableFromJson(path, defaults)
	if doesFileExist(path) then
		local ok, tbl = pcall(function()
			local f = io.open(path, "rb")
			if f then
				local content = f:read("*a")
				f:close()
				local ok, data = pcall(decodeJson, content)
				if ok and type(data) == "table" then
					return data
				end
			end
		end)
		if type(tbl) == "table" then
			return tbl
		end
	end
	return defaults or {}
end

function module.Set_CursorPos(x, y)
	lua_thread.create(function()
		ffi.C.SetCursorPos(x, y)
	end)
end

function module.RusToGame(text)
	local convtbl = {
		[230] = 155,
		[231] = 159,
		[247] = 164,
		[234] = 107,
		[250] = 144,
		[251] = 168,
		[254] = 171,
		[253] = 170,
		[255] = 172,
		[224] = 97,
		[240] = 112,
		[241] = 99,
		[226] = 162,
		[228] = 154,
		[225] = 151,
		[227] = 153,
		[248] = 165,
		[243] = 121,
		[184] = 101,
		[235] = 158,
		[238] = 111,
		[245] = 120,
		[233] = 157,
		[242] = 166,
		[239] = 163,
		[244] = 63,
		[237] = 174,
		[229] = 101,
		[246] = 160,
		[236] = 175,
		[232] = 156,
		[249] = 161,
		[252] = 169,
		[215] = 141,
		[202] = 75,
		[204] = 77,
		[220] = 146,
		[221] = 147,
		[222] = 148,
		[192] = 65,
		[193] = 128,
		[209] = 67,
		[194] = 139,
		[195] = 130,
		[197] = 69,
		[206] = 79,
		[213] = 88,
		[168] = 69,
		[223] = 149,
		[207] = 140,
		[203] = 135,
		[201] = 133,
		[199] = 136,
		[196] = 131,
		[208] = 80,
		[200] = 133,
		[198] = 132,
		[210] = 143,
		[211] = 89,
		[216] = 142,
		[212] = 129,
		[214] = 137,
		[205] = 72,
		[217] = 138,
		[218] = 167,
		[219] = 145,
	}
	local result = {}
	for i = 1, #text do
		local c = text:byte(i)
		result[i] = string.char(convtbl[c] or c)
	end
	return table.concat(result)
end

function module.pressKey_func(key, duration)
	setVirtualKeyDown(key, true) -- нажать клавишу
	wait(duration)
	setVirtualKeyDown(key, false) -- отпустить клавишу
end

function module.delay_func(bool, delay)
	local char_count = #getAllChars() > 10 and 1 or 0
	local delay_map = {
		[1] = { 1374, 1074 }, -- обычный
		[2] = { 3374, 3074 }, -- /s
		[3] = { 2574, 2074 }, -- /b
	}
	-- Если delay не 1 и не 2, добавляем его в таблицу как новое значение
	if not delay_map[delay] then
		delay_map[delay] = { delay, delay }
	end
	-- Выполнение действия в зависимости от bool
	if bool then
		wait(delay_map[delay][char_count + 1])
	else
		return delay_map[delay][char_count + 1]
	end
end

function module.insert_dashes(number)
	local str_number = tostring(number) -- Преобразуем число в строку
	if #str_number < 2 then
		return str_number -- Если число одноцифровое, возвращаем его как есть
	end
	return str_number:sub(1, 1) .. str_number:sub(2):gsub("(%d)", "-%1") -- Вставляем дефисы
end

local TakeScreenshot = ffi.cast("void(__cdecl*)(uintptr_t, const char*)", 0x5D0820)

function module.Take_Screenshot(path, name)
	local date_time = os.date("%d.%m.%Y %H.%M.%S")
	local main_folder = getWorkingDirectory() .. "\\screens"
	name = name and u8:decode(name) or date_time
	path = path and main_folder .. "\\" .. u8:decode(path) or main_folder

	if not doesDirectoryExist(path) then
		createDirectory(path)
	end

	local full_path = path .. "\\" .. name .. ".png"
	TakeScreenshot(0, full_path)
end

function module.getFiles(folder)
	local files = {}
	local handleFile, nameFile = findFirstFile(folder)
	while nameFile do
		if handleFile then
			if not nameFile then
				findClose(handleFile)
			else
				files[#files + 1] = nameFile
				nameFile = findNextFile(handleFile)
			end
		end
	end
	return files
end

function module.sensa(float)
	if float then
		local value = float / 1000
		memory.setfloat(0xB6EC1C, value, false)
		memory.setfloat(0xB6EC18, value, false)
	end
end

function module.getAllWeapons()
	local tWeapons = {}
	for i = 1, 13 do
		local weapon, ammo, id = getCharWeaponInSlot(PLAYER_PED, i)
		if weapon >= 1 and weapon <= 46 then
			tWeapons[weapon] = ammo
		end
	end
	return tWeapons
end

function module.ARGBtoRGB(color)
	return bit32 or bit.band(color, 0xFFFFFF)
end

function module.getRealCameraCoordinates()
	local CCamera = ffi.cast("float*", 0xB6F028)
	return CCamera[0x20F], CCamera[0x210], CCamera[0x211]
end

function module.join_argb(a, r, g, b)
	local argb = b -- b
	argb = bit.bor(argb, bit.lshift(g, 8)) -- g
	argb = bit.bor(argb, bit.lshift(r, 16)) -- r
	argb = bit.bor(argb, bit.lshift(a, 24)) -- a
	return argb
end

function module.explode_argb(argb)
	local a = bit.band(bit.rshift(argb, 24), 0xFF)
	local r = bit.band(bit.rshift(argb, 16), 0xFF)
	local g = bit.band(bit.rshift(argb, 8), 0xFF)
	local b = bit.band(argb, 0xFF)
	return a, r, g, b
end

function module.editRadarMapColor(R, G, B, A)
	memory.setuint8(0x5864CC, R, true)
	memory.setuint8(0x5865BD, R, true)
	memory.setuint8(0x5865DB, R, true)
	memory.setuint8(0x5865F9, R, true)
	memory.setuint8(0x586617, R, true)

	memory.setuint8(0x5864C7, G, true)
	memory.setuint8(0x5865B8, G, true)
	memory.setuint8(0x5865D6, G, true)
	memory.setuint8(0x5865F4, G, true)
	memory.setuint8(0x586612, G, true)

	memory.setuint8(0x5864C2, B, true)
	memory.setuint8(0x5865B3, B, true)
	memory.setuint8(0x5865D1, B, true)
	memory.setuint8(0x5865EF, B, true)
	memory.setuint8(0x58660D, B, true)

	memory.setuint8(0x5864BD, A, true)
	memory.setuint8(0x5865AE, A, true)
	memory.setuint8(0x5865CC, A, true)
	memory.setuint8(0x5865EA, A, true)
	memory.setuint8(0x586608, A, true)
end

function module.setOutboundWaterColor(R, G, B)
	memory.setuint8(0x586442, R, true)
	memory.setuint8(0x575491, R, true)
	memory.setuint8(0x5758FF, R, true)
	memory.setuint32(0x58643D, G, true)
	memory.setuint32(0x57548C, G, true)
	memory.setuint32(0x5758FA, G, true)
	memory.setuint32(0x586438, B, true)
	memory.setuint32(0x575487, B, true)
	memory.setuint32(0x5758F1, B, true)
end

function module.changeRadarColor(rgba)
	local r = bit.band(bit.rshift(rgba, 24), 0xFF)
	local g = bit.band(bit.rshift(rgba, 16), 0xFF)
	local b = bit.band(bit.rshift(rgba, 8), 0xFF)
	local a = bit.band(rgba, 0xFF)
	memory.write(0x58A798, r, 1, true)
	memory.write(0x58A89A, r, 1, true)
	memory.write(0x58A8EE, r, 1, true)
	memory.write(0x58A9A2, r, 1, true)

	memory.write(0x58A790, g, 1, true)
	memory.write(0x58A896, g, 1, true)
	memory.write(0x58A8E6, g, 1, true)
	memory.write(0x58A99A, g, 1, true)

	memory.write(0x58A78E, b, 1, true)
	memory.write(0x58A894, b, 1, true)
	memory.write(0x58A8DE, b, 1, true)
	memory.write(0x58A996, b, 1, true)

	memory.write(0x58A789, a, 1, true)
	memory.write(0x58A88F, a, 1, true)
	memory.write(0x58A8D9, a, 1, true)
	memory.write(0x58A98F, a, 1, true)
end

function module.drawClickableText(font, text, posX, posY, color, colorA, copy) -- by hnnssy
	renderFontDrawText(font, text, posX, posY, color)
	local textLenght = renderGetFontDrawTextLength(font, text)
	local textHeight = renderGetFontDrawHeight(font)
	local curX, curY = getCursorPos()

	if curX >= posX and curX <= posX + textLenght and curY >= posY and curY <= posY + textHeight then
		renderFontDrawText(font, text, posX, posY, colorA)
		if wasKeyPressed(1) then
			if copy then
				setClipboardText(text)
			end
			return true
		end
	end
end

local utf8_lower_map = {
	["А"] = "а",
	["Б"] = "б",
	["В"] = "в",
	["Г"] = "г",
	["Д"] = "д",
	["Е"] = "е",
	["Ё"] = "ё",
	["Ж"] = "ж",
	["З"] = "з",
	["И"] = "и",
	["Й"] = "й",
	["К"] = "к",
	["Л"] = "л",
	["М"] = "м",
	["Н"] = "н",
	["О"] = "о",
	["П"] = "п",
	["Р"] = "р",
	["С"] = "с",
	["Т"] = "т",
	["У"] = "у",
	["Ф"] = "ф",
	["Х"] = "х",
	["Ц"] = "ц",
	["Ч"] = "ч",
	["Ш"] = "ш",
	["Щ"] = "щ",
	["Ъ"] = "ъ",
	["Ы"] = "ы",
	["Ь"] = "ь",
	["Э"] = "э",
	["Ю"] = "ю",
	["Я"] = "я",
}

function module.string_lower(str)
	return (
		str:gsub("[%z\1-\127\194-\244][\128-\191]*", function(c)
			return utf8_lower_map[c] or string.lower(c)
		end)
	)
end

function module.memory_setfloat(adr, value, prot)
	return writeMemory(adr, 4, representFloatAsInt(value), prot)
end

math.randomseed(os.time() + math.floor(os.clock() * 1000000))

function module.randomed(min, max)
	return math.random(min, max)
end

function module.trim(s)
	return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

function module.parseLines(s, keep_spaces)
	s = tostring(s or "")
	local t = {}
	for line in s:gmatch("[^\n]+") do
		local p = keep_spaces and line or module.trim(line)
		if p ~= "" then
			table.insert(t, p)
		end
	end
	return t
end

function module.parseList(s)
	s = tostring(s or "")
	local t = {}
	for part in s:gmatch("[^,\n]+") do
		local p = module.trim(part)
		if p ~= "" then
			table.insert(t, p)
		end
	end
	return t
end

function module.string_rupper(s)
	s = s:upper()
	local strlen = s:len()
	if strlen == 0 then
		return s
	end
	local output = ""
	for i = 1, strlen do
		local ch = s:byte(i)
		if ch >= 224 and ch <= 255 then
			output = output .. string.char(ch - 32)
		elseif ch == 184 then
			output = output .. string.char(168)
		else
			output = output .. string.char(ch)
		end
	end
	return output
end

function module.rounding(num, idp)
	local mult = 10 ^ (idp or 0)
	return math.floor(num * mult + 0.5) / mult
end

function module.fromRGBtoRGB(maxhp, curhp, fromR, fromG, fromB, toR, toG, toB)
	deltaR = module.rounding(((toR - fromR) / maxhp), 2)
	deltaG = module.rounding(((toG - fromG) / maxhp), 2)
	deltaB = module.rounding(((toB - fromB) / maxhp), 2)
	t = { (fromR + curhp * deltaR), (fromG + curhp * deltaG), (fromB + curhp * deltaB) }
	return t
end

function module.set_triangle_color(r, g, b) -- by etereon
	local bytes = "90909090909090909090909090C744240E00000000909090909090909090909090909090C744240F0000000090B300"
	memory.hex2bin(bytes, 0x60BB41, bytes:len() / 2)
	memory.setint8(0x60BB52, r, false)
	memory.setint8(0x60BB69, g, false)
	memory.setint8(0x60BB6F, b, false)
end

function module.translite_name(name)
	-- Таблица для буквосочетаний
	-- Имеется LUA таблица,
	local multi_char_dict = {
		["ph"] = "ф",
		["Ph"] = "Ф",
		["Mc"] = "Мак-",
		["Ch"] = "Ч",
		["ch"] = "ч",
		["Th"] = "Т",
		["th"] = "т",
		["Sh"] = "Ш",
		["sh"] = "ш",
		["ea"] = "и",
		["Ae"] = "Э",
		["ae"] = "э",
		["size"] = "сайз",
		["Jj"] = "Джейджей",
		["Whi"] = "Вай",
		["whi"] = "вай",
		["Ck"] = "К",
		["ck"] = "к",
		["Kh"] = "Х",
		["kh"] = "х",
		["hn"] = "н",
		["Hen"] = "Ген",
		["Zh"] = "Ж",
		["zh"] = "ж",
		["Yu"] = "Ю",
		["yu"] = "ю",
		["Yo"] = "Ё",
		["yo"] = "ё",
		["Cz"] = "Ц",
		["cz"] = "ц",
		["ia"] = "ия",
		["Ya"] = "Я",
		["ya"] = "я",
		["ove"] = "ав",
		["ay"] = "эй",
		["rise"] = "райз",
		["oo"] = "у",
		["Oo"] = "У",
		["ps"] = "пс",
		["ks"] = "кс",
		["ts"] = "ц",
		["ck"] = "к",
		["Zd"] = "Жд",
		["zd"] = "жд",
		["ou"] = "о",
		["ai"] = "ай",
		["Ai"] = "Ай",
		["oi"] = "ой",
		["Oi"] = "Ой",
		["ui"] = "уй",
		["Ui"] = "Уй",
		["bi"] = "би",
		["Bi"] = "Би",
		["di"] = "ди",
		["Di"] = "Ди",
		["gi"] = "ги",
		["Gi"] = "Ги",
		["hi"] = "хи",
		["Hi"] = "Хи",
		["ji"] = "джи",
		["Ji"] = "Джи",
		["ki"] = "ки",
		["Ki"] = "Ки",
		["li"] = "ли",
		["Li"] = "Ли",
		["mi"] = "ми",
		["Mi"] = "Ми",
		["ni"] = "ни",
		["Ni"] = "Ни",
		["pi"] = "пи",
		["Pi"] = "Пи",
		["qi"] = "ци",
		["Qi"] = "Ци",
		["ri"] = "ри",
		["Ri"] = "Ри",
		["si"] = "си",
		["Si"] = "Си",
		["ti"] = "ти",
		["Ti"] = "Ти",
		["vi"] = "ви",
		["Vi"] = "Ви",
		["wi"] = "ви",
		["Wi"] = "Ви",
		["xi"] = "кси",
		["Xi"] = "Кси",
		["yi"] = "йи",
		["Yi"] = "Йи",
		["zi"] = "зи",
		["Zi"] = "Зи",
		["Ey"] = "Ей",
		["ey"] = "ей",
	}
	-- Таблица для одиночных символов
	local single_char_dict = {
		["B"] = "Б",
		["Z"] = "З",
		["T"] = "Т",
		["Y"] = "Й",
		["P"] = "П",
		["J"] = "Дж",
		["X"] = "Кс",
		["G"] = "Г",
		["V"] = "В",
		["H"] = "Х",
		["N"] = "Н",
		["E"] = "Е",
		["I"] = "И",
		["D"] = "Д",
		["O"] = "О",
		["K"] = "К",
		["F"] = "Ф",
		["y`"] = "ы",
		["e`"] = "э",
		["A"] = "А",
		["C"] = "К",
		["L"] = "Л",
		["M"] = "М",
		["W"] = "В",
		["Q"] = "К",
		["U"] = "А",
		["R"] = "Р",
		["S"] = "С",
		["zm"] = "зьм",
		["h"] = "х",
		["q"] = "к",
		["y"] = "и",
		["a"] = "а",
		["w"] = "в",
		["b"] = "б",
		["v"] = "в",
		["g"] = "г",
		["d"] = "д",
		["e"] = "е",
		["z"] = "з",
		["i"] = "и",
		["j"] = "ж",
		["k"] = "к",
		["l"] = "л",
		["m"] = "м",
		["n"] = "н",
		["o"] = "о",
		["p"] = "п",
		["r"] = "р",
		["s"] = "с",
		["t"] = "т",
		["u"] = "у",
		["f"] = "ф",
		["x"] = "кс",
		["c"] = "к",
		["``"] = "ъ",
		["`"] = "ь",
		["_"] = " ",
	}

	-- Таблица для популярных имен
	local name_dict = {
		-- A
		["Artemiy"] = "Артемий", -- Основные и редкие исторические
		["Afanasy"] = "Афанасий",
		["Agata"] = "Агата",
		["Agafya"] = "Агафья",
		["Aglaya"] = "Аглая",
		["Aksinya"] = "Аксинья",
		["Akulina"] = "Акулина",
		["Alena"] = "Алёна",
		["Anastasia"] = "Анастасия",
		["Anfisa"] = "Анфиса",
		["Anisim"] = "Анисим",
		["Antonina"] = "Антонина",
		["Apolinariya"] = "Аполлинария",
		["Ariadna"] = "Ариадна",
		["Arkadiy"] = "Аркадий",
		["Arina"] = "Арина",
		["Arseniy"] = "Арсений",
		["Avelina"] = "Авелина",
		["Avdotya"] = "Авдотья",
		["Avraam"] = "Авраам",
		["Avgust"] = "Август",
		["Avgusta"] = "Августа",
		["Avreliy"] = "Аврелий",
		["Avreliya"] = "Аврелия",
		["Bogdan"] = "Богдан",
		["Bogdana"] = "Богдана",
		["Bronislav"] = "Бронислав",
		["Bronislava"] = "Бронислава",
		["Varvara"] = "Варвара",
		["Vasiliy"] = "Василий",
		["Vasilisa"] = "Василиса",
		["Venjamin"] = "Вениамин",
		["Veniamin"] = "Вениамин",
		["Vera"] = "Вера",
		["Veronika"] = "Вероника",
		["Vikentiy"] = "Викентий",
		["Vikentia"] = "Викентия",
		["Vikenty"] = "Викентий",
		["Vissarion"] = "Виссарион",
		["Vlada"] = "Влада",
		["Vladislav"] = "Владислав",
		["Vladlen"] = "Владлен",
		["Vladimir"] = "Владимир",
		["Vlas"] = "Влас",
		["Vsevolod"] = "Всеволод",
		["Vyacheslav"] = "Вячеслав",
		["Galina"] = "Галина",
		["Gavriil"] = "Гавриил",
		["Gennadiy"] = "Геннадий",
		["Georgiy"] = "Георгий",
		["Gerasim"] = "Герасим",
		["Gleb"] = "Глеб",
		["Gordey"] = "Гордей",
		["Grigoriy"] = "Григорий",
		["Daria"] = "Дарья",
		["Darya"] = "Дарья",
		["Demid"] = "Демид",
		["Denis"] = "Денис",
		["Dmitriy"] = "Дмитрий",
		["Dobrynya"] = "Добрыня",
		["Evgeniy"] = "Евгений",
		["Evgeniya"] = "Евгения",
		["Egor"] = "Егор",
		["Evdokiya"] = "Евдокия",
		["Evfrosiniya"] = "Евфросиния",
		["Evstafiy"] = "Евстафий",
		["Evstigney"] = "Евстигней",
		["Elena"] = "Елена",
		["Elizaveta"] = "Елизавета",
		["Emelyan"] = "Емельян",
		["Ermolai"] = "Ермолай",
		["Erast"] = "Эраст",
		["Efim"] = "Ефим",
		["Efrosinya"] = "Ефросинья",
		["Faina"] = "Фаина",
		["Fedor"] = "Фёдор",
		["Fedot"] = "Федот",
		["Fekla"] = "Фёкла",
		["Filipp"] = "Филипп",
		["Foma"] = "Фома",
		["Gennadiya"] = "Геннадия",
		["Grisha"] = "Гриша",
		["Ignat"] = "Игнат",
		["Ignatiy"] = "Игнатий",
		["Igor"] = "Игорь",
		["Inna"] = "Инна",
		["Irina"] = "Ирина",
		["Iosif"] = "Иосиф",
		["Ipat"] = "Ипат",
		["Isidor"] = "Исидор",
		["Iya"] = "Ия",
		["Kapitolina"] = "Капитолина",
		["Katerina"] = "Катерина",
		["Khariton"] = "Харитон",
		["Kirill"] = "Кирилл",
		["Klavdiya"] = "Клавдия",
		["Klementina"] = "Клементина",
		["Klim"] = "Клим",
		["Kliment"] = "Климент",
		["Kondrat"] = "Кондрат",
		["Konstantin"] = "Константин",
		["Kornei"] = "Корней",
		["Kostya"] = "Костя",
		["Kuzma"] = "Кузьма",
		["Lada"] = "Лада",
		["Larisa"] = "Лариса",
		["Lazar"] = "Лазарь",
		["Lev"] = "Лев",
		["Leonid"] = "Леонид",
		["Lidiya"] = "Лидия",
		["Lilia"] = "Лилия",
		["Lina"] = "Лина",
		["Lubov"] = "Любовь",
		["Lyudmila"] = "Людмила",
		["Makar"] = "Макар",
		["Maksim"] = "Максим",
		["Maksimilian"] = "Максимилиан",
		["Margarita"] = "Маргарита",
		["Maria"] = "Мария",
		["Marina"] = "Марина",
		["Mark"] = "Марк",
		["Marta"] = "Марта",
		["Matvey"] = "Матвей",
		["Mefodiy"] = "Мефодий",
		["Mikhail"] = "Михаил",
		["Miloslav"] = "Мирослав",
		["Miroslava"] = "Мирослава",
		["Miroslav"] = "Мирослав",
		["Modest"] = "Модест",
		["Nadezhda"] = "Надежда",
		["Nazar"] = "Назар",
		["Nikita"] = "Никита",
		["Nikolai"] = "Николай",
		["Nina"] = "Нина",
		["Oksana"] = "Оксана",
		["Oleg"] = "Олег",
		["Olga"] = "Ольга",
		["Pavel"] = "Павел",
		["Pelageya"] = "Пелагея",
		["Petr"] = "Пётр",
		["Platon"] = "Платон",
		["Polina"] = "Полина",
		["Praskovya"] = "Прасковья",
		["Prokhor"] = "Прохор",
		["Prokofiy"] = "Прокофий",
		["Pyotr"] = "Пётр",
		["Raisa"] = "Раиса",
		["Rostislav"] = "Ростислав",
		["Rodion"] = "Родион",
		["Roman"] = "Роман",
		["Ruslan"] = "Руслан",
		["Saveliy"] = "Савелий",
		["Semen"] = "Семён",
		["Serafim"] = "Серафим",
		["Sergey"] = "Сергей",
		["Sofia"] = "София",
		["Sofiya"] = "София",
		["Solomon"] = "Соломон",
		["Stanislav"] = "Станислав",
		["Stepan"] = "Степан",
		["Stesha"] = "Стеша",
		["Svetlana"] = "Светлана",
		["Taisiya"] = "Таисия",
		["Tamara"] = "Тамара",
		["Tatyana"] = "Татьяна",
		["Terentiy"] = "Терентий",
		["Timofey"] = "Тимофей",
		["Tikhon"] = "Тихон",
		["Ulyana"] = "Ульяна",
		["Ustinya"] = "Устинья",
		["Faddey"] = "Фаддей",
		["Fekla"] = "Фёкла",
		["Fyodor"] = "Фёдор",
		["Filipp"] = "Филипп",
		["Yakim"] = "Яким",
		["Yakov"] = "Яков",
		["Yan"] = "Ян",
		["Yana"] = "Яна",
		["Yaromir"] = "Яромир",
		["Yaropolk"] = "Ярополк",
		["Yaroslav"] = "Ярослав",
		["Yelizaveta"] = "Елизавета",
		["Yevdokiya"] = "Евдокия",
		["Yevgeniy"] = "Евгений",
		["Yuliya"] = "Юлия",
		["Yuliana"] = "Юлиана",
		["Yuriy"] = "Юрий",
		["Zinaida"] = "Зинаида",
		["Zlata"] = "Злата",
		["Zoya"] = "Зоя",
		-- Современные, региональные, "пацанские", народные, и уменьшительно-разговорные (могут быть как основной формой для ников)
		["Alyona"] = "Алёна",
		["Alla"] = "Алла",
		["Anya"] = "Аня",
		["Vanya"] = "Ваня",
		["Vanka"] = "Ванька",
		["Dimon"] = "Димон",
		["Dima"] = "Дима",
		["Vitya"] = "Витя",
		["Vovan"] = "Вован",
		["Vova"] = "Вова",
		["Sanya"] = "Саня",
		["Sanka"] = "Санька",
		["Sasha"] = "Саша",
		["Sashka"] = "Сашка",
		["Shurik"] = "Шурик",
		["Pashka"] = "Пашка",
		["Pasha"] = "Паша",
		["Kolyan"] = "Коля",
		["Kolya"] = "Коля",
		["Tolik"] = "Толик",
		["Tolyan"] = "Толян",
		["Mishka"] = "Мишка",
		["Misha"] = "Миша",
		["Yurka"] = "Юрка",
		["Yura"] = "Юра",
		["Grisha"] = "Гриша",
		["Andryukha"] = "Андрюха",
		["Andrey"] = "Андрей",
		["Serёga"] = "Серёга",
		["Seryoga"] = "Серёга",
		["Seryozha"] = "Серёжа",
		["Stas"] = "Стас",
		["Stasya"] = "Стася",
		["Kostya"] = "Костя",
		["Lesha"] = "Лёша",
		["Lyosha"] = "Лёша",
		["Vlad"] = "Влад",
		["Vladik"] = "Владик",
		["Slava"] = "Слава",
		["Zhenya"] = "Женя",
		["Vitya"] = "Витя",
		["Zhanna"] = "Жанна",
		["Olya"] = "Оля",
		["Olka"] = "Олька",
		["Katya"] = "Катя",
		["Katyusha"] = "Катюша",
		["Rita"] = "Рита",
		["Ritka"] = "Ритка",
		["Nastya"] = "Настя",
		["Nastyusha"] = "Настюша",
		["Natasha"] = "Наташа",
		["Natashka"] = "Наташка",
		["Tanya"] = "Таня",
		["Tanka"] = "Танька",
		["Tanyusha"] = "Танюша",
		["Svetka"] = "Светка",
		["Svetlana"] = "Светлана",
		["Galya"] = "Галя",
		["Lena"] = "Лена",
		["Lenka"] = "Ленка",
		["Lenochka"] = "Леночка",
		["Lyuda"] = "Люда",
		["Lyudmila"] = "Людмила",
		["Zina"] = "Зина",
		["Nina"] = "Нина",
		["Alla"] = "Алла",
		["Nelya"] = "Неля",
		["Liza"] = "Лиза",
		["Lizka"] = "Лизка",
		["Lida"] = "Лида",
		["Sima"] = "Сима",
		["Frosya"] = "Фрося",
		["Marinka"] = "Маринка",
		["Marisha"] = "Мариша",
		["Marusya"] = "Маруся",
		["Raya"] = "Рая",
		["Raya"] = "Рая",
		["Zoya"] = "Зоя",
		["Zoyka"] = "Зойка",
		["Yulya"] = "Юля",
		["Yulka"] = "Юлька",
		["Yulechka"] = "Юлечка",
		["Yanochka"] = "Яночка",
		["Yana"] = "Яна",
		["Yashka"] = "Яшка",
		["Yasha"] = "Яша",
		["Gosha"] = "Гоша",
		["Edik"] = "Эдик",
		["Emil"] = "Эмиль",
		["Yan"] = "Ян",
		["Vlad"] = "Влад",
		["Vladlen"] = "Владлен",
		["Danil"] = "Данил",
		["Den"] = "Ден",
		["Valera"] = "Валера",
		["Valerka"] = "Валерка",
		["Zhorik"] = "Жорик",
		["Zhora"] = "Жора",
		["Stepka"] = "Стёпка",
		["Stepan"] = "Стёпа",
		["Petya"] = "Петя",
		["Petka"] = "Петька",
		["Senya"] = "Сеня",
		["Senka"] = "Сенька",
		["Alyosha"] = "Алёша",
		["Alekha"] = "Алеха",
		["Alina"] = "Алина",
		["Snezhana"] = "Снежана",
		["Rusya"] = "Русья",
		["Toma"] = "Тома",
		["Toma"] = "Тома",
		-- Дворовые и региональные прозвища
		["Ryzhiy"] = "Рыжий",
		["Chernysh"] = "Черныш",
		["Sharik"] = "Шарик",
		["Kesha"] = "Кеша",
		["Kostik"] = "Костик",
		["Dimka"] = "Димка",
		["Dimka"] = "Димка",
		["Vasyok"] = "Васьок",
		["Vasyan"] = "Васьян",
		["Senka"] = "Сенька",
		["Leshka"] = "Лёшка",
		["Mishka"] = "Мишка",
		["Igrunya"] = "Игруня",
		["Borisych"] = "Борисыч",
		["Ivanych"] = "Иваныч",
		["Mikhalych"] = "Михалыч",
		["Petrovich"] = "Петрович",
		["Sergeich"] = "Сергеич",
		["Palych"] = "Палыч",
		["Andryusha"] = "Андрюша",
		["Filka"] = "Филька",
		["Yashka"] = "Яшка",
		["Timosha"] = "Тимоша",
		["Grysha"] = "Грыша",
		["Fedya"] = "Федя",
		["Fedka"] = "Федька",
		["Zhorik"] = "Жорик",
		["Stesha"] = "Стеша",
		["Zina"] = "Зина",
		["Frosya"] = "Фрося",
		["Aaron"] = "Аарон",
		["Abel"] = "Абель",
		["Abigail"] = "Абигейл",
		["Adams"] = "Адамс",
		["Addison"] = "Аддисон",
		["Adrian"] = "Адриан",
		["Aiden"] = "Айден",
		["Alex"] = "Алекс",
		["Alexander"] = "Александр",
		["Alicia"] = "Алисия",
		["Alice"] = "Алиса",
		["Alina"] = "Алина",
		["Alisa"] = "Алиса",
		["Allen"] = "Аллен",
		["Alfred"] = "Альфред",
		["Alyssa"] = "Алисса",
		["Amelia"] = "Амелия",
		["Amanda"] = "Аманда",
		["Amber"] = "Амбер",
		["Amy"] = "Эми",
		["Angela"] = "Анжела",
		["Anderson"] = "Андерсон",
		["Andrew"] = "Андрей",
		["Angelina"] = "Анджелина",
		["Anjali"] = "Анджали",
		["Anthony"] = "Энтони",
		["Antonio"] = "Антонио",
		["Aqua"] = "Аква",
		["Aria"] = "Ария",
		["Archibald"] = "Арчибальд",
		["Arthur"] = "Артур",
		["Asuka"] = "Асука",
		["Ashley"] = "Эшли",
		["Astrid"] = "Астрид",
		["Audrey"] = "Одри",
		["Aurora"] = "Аврора",
		["Austin"] = "Остин",
		["Ava"] = "Эйва",
		["Avery"] = "Эйвери",
		["Ayumi"] = "Аюми",
		-- B
		["Bailey"] = "Бейли",
		["Baker"] = "Бейкер",
		["Barbara"] = "Барбара",
		["Barack"] = "Барак",
		["Beatrice"] = "Беатрис",
		["Bella"] = "Белла",
		["Benjamin"] = "Бенжамин",
		["Bernard"] = "Бернард",
		["Betty"] = "Бетти",
		["Bianca"] = "Бьянка",
		["Bill"] = "Билл",
		["Blade"] = "Блэйд",
		["Blake"] = "Блэйк",
		["Bo"] = "Бо",
		["Bonnie"] = "Бонни",
		["Bradley"] = "Брэдли",
		["Brandon"] = "Брэндон",
		["Brenda"] = "Бренда",
		["Brianna"] = "Брианна",
		["Brittany"] = "Бриттани",
		["Brooke"] = "Брук",
		["Brown"] = "Браун",
		["Bruce"] = "Брюс",
		-- C
		["Caleb"] = "Калеб",
		["Camila"] = "Камила",
		["Candice"] = "Кэндис",
		["Carlos"] = "Карлос",
		["Carla"] = "Карла",
		["Carmen"] = "Кармен",
		["Carol"] = "Кэрол",
		["Caroline"] = "Каролин",
		["Carter"] = "Картер",
		["Catherine"] = "Кэтрин",
		["Cecilia"] = "Сесилия",
		["Celia"] = "Селия",
		["Cesar"] = "Сезар",
		["Chad"] = "Чад",
		["Chandler"] = "Чендлер",
		["Charles"] = "Чарльз",
		["Charlotte"] = "Шарлотта",
		["Chelsea"] = "Челси",
		["Chen"] = "Чен",
		["Chloe"] = "Хлоя",
		["Chris"] = "Крис",
		["Christian"] = "Кристиан",
		["Christopher"] = "Кристофер",
		["Chu"] = "Чу",
		["Clark"] = "Кларк",
		["Claude"] = "Клод",
		["Clara"] = "Клара",
		["Clarence"] = "Кларенс",
		["Claudia"] = "Клаудия",
		["Colin"] = "Колин",
		["Connor"] = "Коннор",
		["Conor"] = "Конор",
		["Cook"] = "Кук",
		["Cooper"] = "Купер",
		["Craig"] = "Крейг",
		["Crystal"] = "Кристал",
		["Curtis"] = "Кёртис",
		-- D
		["Daisy"] = "Дейзи",
		["Dakota"] = "Дакота",
		["Damian"] = "Дэмиан",
		["Dana"] = "Дана",
		["Daphne"] = "Дафна",
		["Darcy"] = "Дарси",
		["Darlene"] = "Дарлин",
		["David"] = "Дэвид",
		["Dean"] = "Дин",
		["Deborah"] = "Дебора",
		["Denis"] = "Денис",
		["Dennis"] = "Деннис",
		["Derek"] = "Дерек",
		["Desmond"] = "Десмонд",
		["Diana"] = "Диана",
		["Diego"] = "Диего",
		["Dominic"] = "Доминик",
		["Donna"] = "Донна",
		["Dorothy"] = "Дороти",
		["Douglas"] = "Дуглас",
		["Dwayne"] = "Дуэйн",
		["Dylan"] = "Дилан",
		-- E
		["Edward"] = "Эдвард",
		["Edwin"] = "Эдвин",
		["Elaine"] = "Элейн",
		["Eli"] = "Элай",
		["Elijah"] = "Элайджа",
		["Elizabeth"] = "Элизабет",
		["Elise"] = "Элис",
		["Ella"] = "Элла",
		["Eleanor"] = "Элеонора",
		["Elon"] = "Илон",
		["Elsa"] = "Эльза",
		["Emily"] = "Эмили",
		["Emma"] = "Эмма",
		["Emilia"] = "Эмилия",
		["Ethan"] = "Итан",
		["Erica"] = "Эрика",
		["Erik"] = "Эрик",
		["Erika"] = "Эрика",
		["Erin"] = "Эрин",
		["Esther"] = "Эстер",
		["Eugene"] = "Юджин",
		["Eva"] = "Ева",
		["Evan"] = "Эван",
		["Evans"] = "Эванс",
		["Evelyn"] = "Эвелин",
		-- F
		["Faith"] = "Фэйт",
		["Felicity"] = "Фелисити",
		["Felix"] = "Феликс",
		["Finn"] = "Финн",
		["Francis"] = "Фрэнсис",
		["Francesco"] = "Франческо",
		["Frank"] = "Фрэнк",
		-- G
		["Gabriel"] = "Габриэль",
		["Gabriella"] = "Габриэлла",
		["Gavin"] = "Гэвин",
		["Gene"] = "Джин",
		["George"] = "Джордж",
		["Georgia"] = "Джорджия",
		["Gerald"] = "Джеральд",
		["Giovanni"] = "Джованни",
		["Giulia"] = "Джулия",
		["Grace"] = "Грейс",
		["Green"] = "Грин",
		["Gregory"] = "Грегори",
		["Guo"] = "Го",
		["Gwen"] = "Гвен",
		-- H
		["Hailey"] = "Хейли",
		["Hannah"] = "Ханна",
		["Harley"] = "Харли",
		["Harold"] = "Гарольд",
		["Harper"] = "Харпер",
		["Harris"] = "Харрис",
		["Hazel"] = "Хейзел",
		["Heather"] = "Хезер",
		["Helen"] = "Хелен",
		["Henry"] = "Генри",
		["Hernandez"] = "Эрнандес",
		["Hill"] = "Хилл",
		["Hiroshi"] = "Хироши",
		["Holly"] = "Холли",
		["Hope"] = "Хоуп",
		["Hua"] = "Хуа",
		["Hung"] = "Хунг",
		["Hyun"] = "Хён",
		-- I
		["Ian"] = "Иан",
		["Irene"] = "Айрин",
		["Iris"] = "Ирис",
		["Isabella"] = "Изабелла",
		["Ivan"] = "Айвен",
		-- J
		["Jack"] = "Джек",
		["Jackie"] = "Джеки",
		["Jackson"] = "Джексон",
		["Jacob"] = "Джейкоб",
		["Jade"] = "Джейд",
		["Jake"] = "Джейк",
		["James"] = "Джеймс",
		["Jane"] = "Джейн",
		["Jasmine"] = "Жасмин",
		["Javier"] = "Хавьер",
		["Jean"] = "Жан",
		["Jenna"] = "Дженна",
		["Jennifer"] = "Дженнифер",
		["Jerry"] = "Джерри",
		["Jia"] = "Цзя",
		["Jiang"] = "Цзян",
		["Jill"] = "Джилл",
		["Jin"] = "Джин",
		["Joan"] = "Джоан",
		["Joanna"] = "Джоанна",
		["Joey"] = "Джоуи",
		["Joel"] = "Джоэл",
		["Johanna"] = "Йоханна",
		["John"] = "Джон",
		["Johnson"] = "Джонсон",
		["Jonas"] = "Йонас",
		["Jonathan"] = "Джонатан",
		["Jordan"] = "Джордан",
		["Jose"] = "Хосе",
		["Joseph"] = "Джозеф",
		["Josephine"] = "Жозефина",
		["Joshua"] = "Джошуа",
		["Juan"] = "Хуан",
		["Judy"] = "Джуди",
		["Julian"] = "Джулиан",
		["Julia"] = "Джулия",
		["Juliet"] = "Джульетта",
		["June"] = "Джун",
		["Jun"] = "Дзюн",
		["Justin"] = "Джастин",
		-- K
		["Kaito"] = "Кайто",
		["Kara"] = "Кара",
		["Karen"] = "Карен",
		["Katherine"] = "Кэтрин",
		["Kathleen"] = "Кэтлин",
		["Kathryn"] = "Кэтрин",
		["Katie"] = "Кэти",
		["Kayla"] = "Кайла",
		["Keith"] = "Кит",
		["Kelly"] = "Келли",
		["Kenneth"] = "Кеннет",
		["Kevin"] = "Кевин",
		["Kim"] = "Ким",
		["Kimberly"] = "Кимберли",
		["King"] = "Кинг",
		["Kristen"] = "Кристен",
		["Kristin"] = "Кристин",
		["Kun"] = "Кун",
		["Kyle"] = "Кайл",
		-- L
		["Lana"] = "Лана",
		["Laura"] = "Лаура",
		["Lauren"] = "Лорен",
		["Layla"] = "Лейла",
		["Lea"] = "Леа",
		["Leah"] = "Лия",
		["Lee"] = "Ли",
		["Leo"] = "Лео",
		["Leon"] = "Леон",
		["Leonard"] = "Леонард",
		["Leslie"] = "Лесли",
		["Li"] = "Ли",
		["Lila"] = "Лайла",
		["Lily"] = "Лили",
		["Lin"] = "Лин",
		["Lindsay"] = "Линдси",
		["Ling"] = "Лин",
		["Lisa"] = "Лиза",
		["Liu"] = "Лю",
		["Logan"] = "Логан",
		["Lola"] = "Лола",
		["Lopez"] = "Лопес",
		["Lorraine"] = "Лоррейн",
		["Louis"] = "Луи",
		["Louise"] = "Луиза",
		["Lucas"] = "Лукас",
		["Lucia"] = "Лусия",
		["Lucille"] = "Люсиль",
		["Luis"] = "Луис",
		["Luna"] = "Луна",
		["Lydia"] = "Лидия",
		["Lucy"] = "Люси",
		-- M
		["Madeline"] = "Маделин",
		["Madison"] = "Мэдисон",
		["Maggie"] = "Мэгги",
		["Mai"] = "Май",
		["Makoto"] = "Макото",
		["Manuel"] = "Мануэль",
		["Marcus"] = "Маркус",
		["Margaret"] = "Маргарет",
		["Maria"] = "Мария",
		["Marie"] = "Мари",
		["Marina"] = "Марина",
		["Mario"] = "Марио",
		["Marissa"] = "Марисса",
		["Martha"] = "Марта",
		["Martin"] = "Мартин",
		["Martinez"] = "Мартинес",
		["Marvin"] = "Марвин",
		["Masaru"] = "Масару",
		["Mason"] = "Мэйсон",
		["Matteo"] = "Матео",
		["Matthew"] = "Мэтью",
		["Max"] = "Макс",
		["Maximilian"] = "Максимилиан",
		["Mei"] = "Мэй",
		["Megan"] = "Меган",
		["Melanie"] = "Мелани",
		["Melissa"] = "Мелисса",
		["Melvin"] = "Мелвин",
		["Mia"] = "Миа",
		["Michelle"] = "Мишель",
		["Mickey"] = "Микки",
		["Miguel"] = "Мигель",
		["Mila"] = "Мила",
		["Miles"] = "Майлз",
		["Miller"] = "Миллер",
		["Min"] = "Мин",
		["Miranda"] = "Миранда",
		["Mitchell"] = "Митчелл",
		["Miyu"] = "Мию",
		["Molly"] = "Молли",
		["Monica"] = "Моника",
		["Moore"] = "Мур",
		["Morgan"] = "Морган",
		["Myrka"] = "Мурка",
		["Murphy"] = "Мёрфи",
		-- N
		["Nancy"] = "Нэнси",
		["Naomi"] = "Наоми",
		["Natalia"] = "Наталия",
		["Natalie"] = "Натали",
		["Nathan"] = "Нэйтан",
		["Nathaniel"] = "Натаниэль",
		["Neil"] = "Нил",
		["Nia"] = "Ниа",
		["Nicole"] = "Николь",
		["Nina"] = "Нина",
		["Noah"] = "Ноа",
		["Noel"] = "Ноэль",
		["Nora"] = "Нора",
		["Norman"] = "Норман",
		["Novella"] = "Навелла",
		["Nguyen"] = "Нгуен",
		-- O
		["Oliver"] = "Оливер",
		["Olivia"] = "Оливия",
		["Oscar"] = "Оскар",
		["Owen"] = "Оуэн",
		-- P
		["Pamela"] = "Памела",
		["Pablo"] = "Пабло",
		["Patricia"] = "Патриция",
		["Patrick"] = "Патрик",
		["Paula"] = "Паула",
		["Paul"] = "Пауль",
		["Pauline"] = "Полин",
		["Paxton"] = "Пакстон",
		["Peggy"] = "Пегги",
		["Penelope"] = "Пенелопа",
		["Perez"] = "Перес",
		["Peter"] = "Питер",
		["Phoebe"] = "Фиби",
		["Phillips"] = "Филлипс",
		["Pierre"] = "Пьер",
		["Ping"] = "Пин",
		["Pyo"] = "Пё",
		-- Q
		["Qiang"] = "Цян",
		["Quentin"] = "Квентин",
		["Quinn"] = "Куинн",
		-- R
		["Rachel"] = "Рэйчел",
		["Rafael"] = "Рафаэль",
		["Ralph"] = "Ральф",
		["Randall"] = "Рэндалл",
		["Rebecca"] = "Ребекка",
		["Regina"] = "Реджина",
		["Reed"] = "Рид",
		["Renee"] = "Рене",
		["Ren"] = "Рэн",
		["Rex"] = "Рекс",
		["Rhonda"] = "Ронда",
		["Ricardo"] = "Рикардо",
		["Riley"] = "Райли",
		["Rita"] = "Рита",
		["Rivera"] = "Ривера",
		["Robin"] = "Робин",
		["Robert"] = "Роберт",
		["Roberts"] = "Робертс",
		["Rodrigo"] = "Родриго",
		["Roger"] = "Роджер",
		["Rogers"] = "Роджерс",
		["Roland"] = "Роланд",
		["Roman"] = "Роман",
		["Ronald"] = "Рональд",
		["Ross"] = "Росс",
		["Rose"] = "Роуз",
		["Ruby"] = "Руби",
		["Russell"] = "Рассел",
		["Ruth"] = "Рут",
		["Ryan"] = "Райан",
		-- S
		["Sabrina"] = "Сабрина",
		["Sakura"] = "Сакура",
		["Sam"] = "Сэм",
		["Samantha"] = "Саманта",
		["Sandra"] = "Сандра",
		["Sanchez"] = "Санчез",
		["Sara"] = "Сара",
		["Sarah"] = "Сара",
		["Scarlett"] = "Скарлетт",
		["Scott"] = "Скотт",
		["Sebastian"] = "Себастьян",
		["Selena"] = "Селена",
		["Serena"] = "Серена",
		["Sergey"] = "Сергей",
		["Shane"] = "Шейн",
		["Shannon"] = "Шеннон",
		["Sharon"] = "Шэрон",
		["Sheila"] = "Шейла",
		["Shelby"] = "Шелби",
		["Shirley"] = "Ширли",
		["Shona"] = "Шона",
		["Sienna"] = "Сиенна",
		["Simon"] = "Саймон",
		["Skylar"] = "Скайлар",
		["Smith"] = "Смит",
		["Sofia"] = "София",
		["Sophie"] = "Софи",
		["Stanley"] = "Стэнли",
		["Stacy"] = "Стэйси",
		["Stella"] = "Стелла",
		["Stephen"] = "Стивен",
		["Stephanie"] = "Стефани",
		["Steve"] = "Стив",
		["Sue"] = "Сью",
		["Summer"] = "Саммер",
		["Susan"] = "Сьюзан",
		["Suzanne"] = "Сюзанна",
		["Sydney"] = "Сидни",
		["Sylvia"] = "Сильвия",
		-- T
		["Tamara"] = "Тамара",
		["Tanya"] = "Таня",
		["Tara"] = "Тара",
		["Taylor"] = "Тейлор",
		["Teresa"] = "Тереза",
		["Terry"] = "Терри",
		["Theodore"] = "Теодор",
		["Thomas"] = "Томас",
		["Thompson"] = "Томпсон",
		["Tiffany"] = "Тиффани",
		["Timothy"] = "Тимоти",
		["Tina"] = "Тина",
		["Todd"] = "Тодд",
		["Tom"] = "Том",
		["Tori"] = "Тори",
		["Tracy"] = "Трейси",
		["Travis"] = "Трэвис",
		["Trevor"] = "Тревор",
		["Tristan"] = "Тристан",
		["Troy"] = "Трой",
		["Tyler"] = "Тайлер",
		-- V
		["Valentina"] = "Валентина",
		["Valerie"] = "Валери",
		["Vanessa"] = "Ванесса",
		["Vera"] = "Вера",
		["Veronica"] = "Вероника",
		["Vicki"] = "Викки",
		["Vicky"] = "Вики",
		["Victor"] = "Виктор",
		["Victoria"] = "Виктория",
		["Vince"] = "Винс",
		["Vincent"] = "Винсент",
		["Violet"] = "Виолетта",
		["Vivian"] = "Вивиан",
		-- W
		["Walter"] = "Вальтер",
		["Wanda"] = "Ванда",
		["Warren"] = "Уоррен",
		["Wayne"] = "Вейн",
		["Wei"] = "Вэй",
		["Wendy"] = "Уэнди",
		["White"] = "Уайт",
		["Will"] = "Уилл",
		["William"] = "Уильям",
		["Williams"] = "Уильямс",
		["Willow"] = "Уиллоу",
		["Wilson"] = "Уилсон",
		["Winona"] = "Вайнона",
		["Wright"] = "Райт",
		-- X
		["Xavier"] = "Ксавьер",
		["Xiao"] = "Сяо",
		["Xiang"] = "Сян",
		["Xin"] = "Синь",
		-- Y
		["Yasmin"] = "Ясмин",
		["Yeong"] = "Ён",
		["Yoshi"] = "Йоши",
		["Yuki"] = "Юки",
		["Yulia"] = "Юлия",
		["Young"] = "Янг",
		["Yvonne"] = "Ивонн",
		["Yvette"] = "Иветт",
		-- Z
		["Zach"] = "Зак",
		["Zachary"] = "Захари",
		["Zara"] = "Зара",
		["Zhang"] = "Чжан",
		["Zhao"] = "Чжао",
		["Zhen"] = "Чжэнь",
		["Zhi"] = "Чжи",
		["Zoe"] = "Зои",
		["Zoey"] = "Зои",
	}
	-- Сначала заменяем популярные имена
	for pattern, replacement in pairs(name_dict) do
		name = name:gsub(pattern, replacement)
	end

	-- Затем заменяем буквосочетания
	for pattern, replacement in pairs(multi_char_dict) do
		name = name:gsub(pattern, replacement)
	end

	-- Затем заменяем одиночные символы
	for pattern, replacement in pairs(single_char_dict) do
		name = name:gsub(pattern, replacement)
	end

	return name
end

function module.fixed_camera_to_skin() -- проверка на приклепление камеры к скину
	local res, i = pcall(memory.getint8, getModuleHandle("gta_sa.exe") + 0x76F053)
	return res and i >= 1
end

getBonePosition = ffi.cast("int (__thiscall*)(void*, float*, int, bool)", 0x5E4280)
function module.getBodyPartCoordinates(id, handle)
	local pedptr = getCharPointer(handle)
	local vec = ffi.new("float[3]")
	getBonePosition(ffi.cast("void*", pedptr), vec, id, true)
	return vec[0], vec[1], vec[2]
end

do
	local buffer = {}
	function module.setKeyboardLanguage(lang) -- Выбирает раскладку. При первом вызове профризит
		if buffer[lang] == nil then
			buffer[lang] = ffi.C.LoadKeyboardLayoutA(lang, 1)
		end
		ffi.C.PostMessageA(ffi.C.GetActiveWindow(), 0x50, 1, buffer[lang])
	end
	-- setKeyboardLanguage("00000409") --en
	-- setKeyboardLanguage("00000419") --ru
end

-- initialization table
local lu_rus, ul_rus = {}, {}
for i = 192, 223 do
	local A, a = string.char(i), string.char(i + 32)
	ul_rus[A] = a
	lu_rus[a] = A
end
local E, e = string.char(168), string.char(184)
ul_rus[E] = e
lu_rus[e] = E

function module.string_nlower(s)
	s = string.lower(s)
	local len, res = #s, {}
	for i = 1, len do
		local ch = string.sub(s, i, i)
		res[i] = ul_rus[ch] or ch
	end
	return table.concat(res)
end

function module.string_nupper(s)
	s = string.upper(s)
	local len, res = #s, {}
	for i = 1, len do
		local ch = string.sub(s, i, i)
		res[i] = lu_rus[ch] or ch
	end
	return table.concat(res)
end

function module.split(str, delim, plain)
	local lines, pos, plain = {}, 1, not (plain == false) --[[ delimiter is plain text by default ]]
	repeat
		local npos, epos = string.find(str, delim, pos, plain)
		table.insert(lines, string.sub(str, pos, npos and npos - 1))
		pos = epos and epos + 1
	until not pos
	return lines
end

module.HIWORD = function(param)
	return bit.rshift(bit.band(param, 0xffff0000), 16)
end

module.splitsigned = function(n) -- СПАСИБО WINAPI.lua и GITHUB и Chat mimgui
	n = tonumber(n)
	local x, y = bit.band(n, 0xffff), bit.rshift(n, 16)
	if x >= 0x8000 then
		x = x - 0xffff
	end
	if y >= 0x8000 then
		y = y - 0xffff
	end
	return x, y
end

function module.get_file_modify_time(path)
	local GENERIC_READ = 0x80000000
	local FILE_SHARE_READ = 0x00000001
	local FILE_SHARE_WRITE = 0x00000002
	local OPEN_EXISTING = 3
	local FILE_ATTRIBUTE_NORMAL = 0x00000080

	-- Открываем файл
	local handle = ffi.C.CreateFileA(
		path,
		GENERIC_READ,
		FILE_SHARE_READ + FILE_SHARE_WRITE,
		nil,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nil
	)

	if handle == -1 then
		return nil -- Не удалось открыть файл
	end

	-- Чтение времени изменения файла
	local filetime = ffi.new("FILETIME[3]")
	local result = ffi.C.GetFileTime(handle, filetime, filetime + 1, filetime + 2)
	ffi.C.CloseHandle(handle)

	if result ~= 0 then
		local modify_time = filetime[2]
		return { tonumber(modify_time.dwLowDateTime), tonumber(modify_time.dwHighDateTime) }
	end

	return nil -- Если чтение времени завершилось неудачей
end

-- Загрузка файлов и текста
function module.loadFiles(bool_text, path_1, path_2)
	local files = module.getFiles(path_1)
	local text = {}
	if bool_text then
		for _, file in ipairs(files) do
			text[file] = {}
			for line in io.lines(("%s\\%s"):format(path_2, file)) do
				table.insert(text[file], line)
			end
		end
	end

	return files, text
end

-- рекурсивный сбор .txt файлов
-- Собирает, строит дерево и читает .txt сразу
function module.collectFileTree(path)
	local function scan(dir, rel)
		local node = {}
		local h, file = findFirstFile(dir .. "\\*")
		while file do
			if file ~= "." and file ~= ".." then
				local full = dir .. "\\" .. file
				local sub, subfile = findFirstFile(full .. "\\*")
				if subfile then
					findClose(sub)
					node[file] = scan(full, rel .. file .. "\\")
				elseif file:lower():match("%.txt$") then
					local lines, f = {}, io.open(full, "r")
					if f then
						for line in f:lines() do
							table.insert(lines, line)
						end
						f:close()
					end
					node[file] = { __lines = lines }
				end
			end
			file = findNextFile(h)
		end
		findClose(h)
		return node
	end
	-- Автоматически создаём корневую папку если её нет
	if not doesDirectoryExist(path) then
		createDirectory(path)
	end
	local tree = scan(path, "")
	return tree
end

local eCamMode = {
	[0] = "MODE_NONE",
	[1] = "MODE_TOPDOWN",
	[2] = "MODE_GTACLASSIC",
	[3] = "MODE_BEHINDCAR",
	[4] = "MODE_FOLLOWPED",
	[5] = "MODE_AIMING",
	[6] = "MODE_DEBUG",
	[7] = "MODE_SNIPER",
	[8] = "MODE_ROCKETLAUNCHER",
	[9] = "MODE_MODELVIEW",
	[10] = "MODE_BILL",
	[11] = "MODE_SYPHON",
	[12] = "MODE_CIRCLE",
	[13] = "MODE_CHEESYZOOM",
	[14] = "MODE_WHEELCAM",
	[15] = "MODE_FIXED",
	[16] = "MODE_1STPERSON",
	[17] = "MODE_FLYBY",
	[18] = "MODE_CAM_ON_A_STRING",
	[19] = "MODE_REACTION",
	[20] = "MODE_FOLLOW_PED_WITH_BIND",
	[21] = "MODE_CHRIS",
	[22] = "MODE_BEHINDBOAT",
	[23] = "MODE_PLAYER_FALLEN_WATER",
	[24] = "MODE_CAM_ON_TRAIN_ROOF",
	[25] = "MODE_CAM_RUNNING_SIDE_TRAIN",
	[26] = "MODE_BLOOD_ON_THE_TRACKS",
	[27] = "MODE_IM_THE_PASSENGER_WOOWOO",
	[28] = "MODE_SYPHON_CRIM_IN_FRONT",
	[29] = "MODE_PED_DEAD_BABY",
	[30] = "MODE_PILLOWS_PAPS",
	[31] = "MODE_LOOK_AT_CARS",
	[32] = "MODE_ARRESTCAM_ONE",
	[33] = "MODE_ARRESTCAM_TWO",
	[34] = "MODE_M16_1STPERSON",
	[35] = "MODE_SPECIAL_FIXED_FOR_SYPHON",
	[36] = "MODE_FIGHT_CAM",
	[37] = "MODE_TOP_DOWN_PED",
	[38] = "MODE_LIGHTHOUSE",
	[39] = "MODE_SNIPER_RUNABOUT",
	[40] = "MODE_ROCKETLAUNCHER_RUNABOUT",
	[41] = "MODE_1STPERSON_RUNABOUT",
	[42] = "MODE_M16_1STPERSON_RUNABOUT",
	[43] = "MODE_FIGHT_CAM_RUNABOUT",
	[44] = "MODE_EDITOR",
	[45] = "MODE_HELICANNON_1STPERSON",
	[46] = "MODE_CAMERA",
	[47] = "MODE_ATTACHCAM",
	[48] = "MODE_TWOPLAYER",
	[49] = "MODE_TWOPLAYER_IN_CAR_AND_SHOOTING",
	[50] = "MODE_TWOPLAYER_SEPARATE_CARS",
	[51] = "MODE_ROCKETLAUNCHER_HS",
	[52] = "MODE_ROCKETLAUNCHER_RUNABOUT_HS",
	[53] = "MODE_AIMWEAPON",
	[54] = "MODE_TWOPLAYER_SEPARATE_CARS_TOPDOWN",
	[55] = "MODE_AIMWEAPON_FROMCAR",
	[56] = "MODE_DW_HELI_CHASE",
	[57] = "MODE_DW_CAM_MAN",
	[58] = "MODE_DW_BIRDY",
	[59] = "MODE_DW_PLANE_SPOTTER",
	[60] = "MODE_DW_DOG_FIGHT",
	[61] = "MODE_DW_FISH",
	[62] = "MODE_DW_PLANECAM1",
	[63] = "MODE_DW_PLANECAM2",
	[64] = "MODE_DW_PLANECAM3",
	[65] = "MODE_AIMWEAPON_ATTACHED",
}

function module.getCameraMode()
	local modeValue = memory.getint16(0xB6F1A8, false) -- Читаем значение из памяти
	local modeName = eCamMode[modeValue] or "UNKNOWN" -- Получаем имя по значению
	return modeName, modeValue -- Возвращаем имя и значение
end

function module.sendOBS(cmd)
	local tbl = {
		[1] = [[ { "request-type": "StartRecording", "message-id": "1" } ]],
		[2] = [[ { "request-type": "StopRecording", "message-id": "1" } ]],
		[3] = [[ { "request-type": "SaveReplayBuffer", "message-id": "1" } ]],
	}

	if tbl[cmd] == nil then
		return
	end

	local websocket = require("websocket")

	local host = "localhost"
	local port = 4444
	local url = "ws://" .. host .. ":" .. port

	local client = websocket.client.sync()

	local success, err = pcall(function()
		client:connect(url)
	end)

	if not success then
		print("Ошибка подключения: " .. err)
		return false
	end
	client:send(tbl[cmd])

	-- Ожидаем ответа от OBS (необязательно, но полезно для проверки)
	-- local response = client:receive()
	-- print("Ответ от OBS: " .. response)

	client:close()
end

function module.findSignatureInModule(signature, moduleName, data)
	local moduleAddress = ffi.C.GetModuleHandleA(moduleName)
	if moduleAddress == nil then
		return nil, "Module not found"
	end

	local processId = ffi.C.GetCurrentProcessId()
	local hProcess = ffi.C.OpenProcess(0x1F0FFF, 0, processId)

	local moduleInfo = ffi.new("MEMORY_BASIC_INFORMATION")
	local address = moduleAddress

	local signatureBytes = {}
	for byte in signature:gmatch("%S+") do
		if byte == "??" then
			table.insert(signatureBytes, false)
		else
			table.insert(signatureBytes, tonumber(byte, 16))
		end
	end

	while ffi.C.VirtualQueryEx(hProcess, address, moduleInfo, ffi.sizeof(moduleInfo)) ~= 0 do
		if moduleInfo.State == 0x1000 and moduleInfo.Protect == (data and 0x2 or 0x20) then
			local size = tonumber(moduleInfo.RegionSize)
			local buffer = ffi.new("uint8_t[?]", size)
			local bytesRead = ffi.new("size_t[1]")

			ffi.C.ReadProcessMemory(hProcess, address, buffer, size, bytesRead)

			for i = 0, size - #signatureBytes do
				local found = true
				for j = 1, #signatureBytes do
					local byte = buffer[i + j - 1]
					if signatureBytes[j] ~= false and byte ~= signatureBytes[j] then
						found = false
						break
					end
				end

				if found then
					ffi.C.CloseHandle(hProcess)
					return ffi.cast("intptr_t", address) + i
				end
			end
		end

		address = ffi.cast("void*", ffi.cast("intptr_t", address) + moduleInfo.RegionSize)
	end

	ffi.C.CloseHandle(hProcess)
	return nil, "Signature not found"
end

local dlstatus = require('moonloader').download_status

-- кеши отдельно для яндекса и LanguageTool
local cacheSpeller = {}
local cacheLT = {}

local function trim(s)
    return s:match('^%s*(.-)%s*$')
end

-- старый urlencode для Yandex Speller, оставляю как есть
local function urlencode(text)
    text = tostring(text)
    text = text:gsub('{......}', '') -- убираем цветовые коды
        :gsub(' ', '+')
        :gsub('\n', '%%0A')
        :gsub('&gt;', '>')
        :gsub('&lt;', '<')
        :gsub('&quot;', '"')
    return text
end

-- общий HTTP GET -> строка через downloadUrlToFile
local function asyncRequest(url, resolve, reject)
    reject = reject or function() end

    local tmpPath = getWorkingDirectory()
        .. '\\cw_req_' .. tostring(os.clock()):gsub('%.', '') .. '_' .. tostring(math.random(1000, 9999)) .. '.tmp'

    downloadUrlToFile(url, tmpPath, function(id, status, p1, p2)
        if status == dlstatus.STATUS_ENDDOWNLOADDATA then
            local f = io.open(tmpPath, 'rb')
            if not f then
                os.remove(tmpPath)
                reject('cannot open temp file')
                return
            end
            local data = f:read('*a') or ''
            f:close()
            os.remove(tmpPath)

            if #data > 0 then
                resolve(data)
            else
                reject('empty response')
            end
        elseif status == dlstatus.STATUS_ERROR then
            os.remove(tmpPath)
            reject('download error')
        end
    end)
end

-------------------------------------------------
--            YANDEX SPELLER (Alt+X)           --
-------------------------------------------------

local function module.handleCorrection(message, setText)
    if message == '' then return end
    message = trim(message)

    if cacheSpeller[message] then
        setText(cacheSpeller[message])
        return
    end

    -- http, чтобы не тащить ssl.https
    local url = "http://speller.yandex.net/services/spellservice.json/checkText?text=" .. u8(urlencode(message))

    asyncRequest(url, function(response)
        local words = decodeJson(response)
        if words and type(words) == 'table' and #words > 0 then
            local used = {}
            local corrected = message

            for _, word_data in ipairs(words) do
                local incorrect = u8:decode(word_data.word)
                local correct = word_data.s and word_data.s[1] and u8:decode(word_data.s[1]) or incorrect
                if not used[incorrect] and correct then
                    corrected = corrected:gsub(incorrect, correct)
                    used[incorrect] = true
                end
            end

            corrected = corrected:gsub('//', '/')
            cacheSpeller[message] = corrected
            setText(corrected)
        end
    end)
end

-------------------------------------------------
--          LanguageTool (API, Alt+D)          --
-------------------------------------------------

-- лимиты LanguageTool
local ltLimits = {
    windowStart = 0,
    reqCount = 0,
    bytesThisMin = 0,
    MAX_REQ_PER_MIN = 20,
    MAX_BYTES_PER_MIN = 75 * 1024, -- 75KB
    MAX_BYTES_PER_REQ = 20 * 1024  -- 20KB
}

local function ltReserve(bytes)
    local now = os.clock()
    if ltLimits.windowStart == 0 or (now - ltLimits.windowStart) >= 60.0 then
        ltLimits.windowStart = now
        ltLimits.reqCount = 0
        ltLimits.bytesThisMin = 0
    end

    if bytes > ltLimits.MAX_BYTES_PER_REQ then
        return false, 'text_too_long'
    end
    if ltLimits.reqCount + 1 > ltLimits.MAX_REQ_PER_MIN then
        return false, 'too_many_requests'
    end
    if ltLimits.bytesThisMin + bytes > ltLimits.MAX_BYTES_PER_MIN then
        return false, 'too_much_text'
    end

    ltLimits.reqCount = ltLimits.reqCount + 1
    ltLimits.bytesThisMin = ltLimits.bytesThisMin + bytes
    return true
end

-- убираем цветовые коды для LT, чтобы они не ломали оффсеты
local function ltSanitizeMessage(msg)
    msg = trim(msg or '')
    if msg == '' then return '' end
    msg = msg:gsub('{......}', '')
    return msg
end

-- нормальный urlencode для UTF-8 (для LanguageTool)
local function urlencode_utf8(str)
    return (str:gsub("([^%w%-_%.%~ ])", function(c)
        return string.format("%%%02X", string.byte(c))
    end):gsub(" ", "+"))
end

-- строим карту: номер символа (UTF-8) -> байтовый индекс
local function buildUtf8Index(str)
    local index = {}
    local len = 0
    local i = 1
    local strLen = #str
    while i <= strLen do
        len = len + 1
        index[len] = i
        local c = str:byte(i)
        if c < 0x80 then
            i = i + 1
        elseif c < 0xE0 then
            i = i + 2
        elseif c < 0xF0 then
            i = i + 3
        else
            i = i + 4
        end
    end
    index[len + 1] = strLen + 1
    return index, len
end

-- применяем правки LanguageTool к UTF-8 строке
local function applyLanguageToolMatches(originalUtf8, matches)
    if not matches or #matches == 0 then
        return originalUtf8
    end

    local index, charCount = buildUtf8Index(originalUtf8)
    local strLenBytes = #originalUtf8

    -- сортируем по offset по возрастанию
    table.sort(matches, function(a, b)
        return (a.offset or 0) < (b.offset or 0)
    end)

    local parts = {}
    local currBytePos = 1
    local used = 0

    for _, m in ipairs(matches) do
        if used >= 30 then break end -- максимум 30 правок

        local repls = m.replacements
        local offset = m.offset
        local length = m.length

        if type(offset) == 'number' and type(length) == 'number'
            and repls and repls[1] and repls[1].value then

            local repl = repls[1].value -- UTF-8
            local charStart = offset + 1
            local charEnd = offset + length

            local byteStart = index[charStart]
            local byteEndPlus1 = index[charEnd + 1] or (strLenBytes + 1)

            if byteStart and byteEndPlus1 and byteStart >= currBytePos then
                table.insert(parts, originalUtf8:sub(currBytePos, byteStart - 1))
                table.insert(parts, repl)
                currBytePos = byteEndPlus1
                used = used + 1
            end
        end
    end

    table.insert(parts, originalUtf8:sub(currBytePos))
    return table.concat(parts)
end

local function module.handleLanguageTool(message, setText)
    message = ltSanitizeMessage(message)
    if message == '' then return end

    if cacheLT[message] then
        setText(cacheLT[message])
        return
    end

    -- исходный текст в UTF-8
    local textUtf8 = u8(message)
    local textBytes = #textUtf8

    local allowed, reason = ltReserve(textBytes)
    if not allowed then
        if sampAddChatMessage then
            local msg
            if reason == 'text_too_long' then
                msg = 'LanguageTool: текст длиннее 20KB, запрос не отправлен.'
            elseif reason == 'too_many_requests' then
                msg = 'LanguageTool: превышен лимит 20 запросов в минуту.'
            elseif reason == 'too_much_text' then
                msg = 'LanguageTool: превышен лимит 75KB текста в минуту.'
            else
                msg = 'LanguageTool: лимит, запрос отклонён.'
            end
            sampAddChatMessage(u8:decode(msg), 0xFFFF0000)
        end
        return
    end

    -- кодируем именно UTF-8 строку
    local encodedText = urlencode_utf8(textUtf8)
    local url = 'https://api.languagetool.org/v2/check?language=ru-RU&text=' .. encodedText

    asyncRequest(url, function(response)
        local data = decodeJson(response)
        if not data or type(data) ~= 'table' or type(data.matches) ~= 'table' or #data.matches == 0 then
            return
        end

        local fixedUtf8 = applyLanguageToolMatches(textUtf8, data.matches)
        local resultCp = u8:decode(fixedUtf8)

        cacheLT[message] = resultCp
        setText(resultCp)
    end, function(err)
        if sampAddChatMessage then
            sampAddChatMessage(u8:decode'LanguageTool: ошибка запроса.', 0xFFFF0000)
        end
    end)
end

return module
