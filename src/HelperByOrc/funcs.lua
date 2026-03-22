local module = {}
local toasts_module = {
	push = function() end,
}
local event_bus_ref
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ffi = require("ffi")
local memory = require("memory")
local paths = require("HelperByOrc.paths")
local language = require("language")

local function L(key, params)
	return language.getText(key, params)
end

function module.CGame__EnableHUD()
	return memory.getint8(0xBA6769) == 1 and true or false
end

function module.attachModules(mod)
	toasts_module = mod.toasts or toasts_module
	event_bus_ref = mod.event_bus
end

local function pushToast(msg, kind, dur)
	if event_bus_ref then
		event_bus_ref.emit("toast", msg, kind, dur)
	elseif toasts_module and type(toasts_module.push) == "function" then
		toasts_module.push(msg, kind, dur)
	end
end

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

pcall(ffi.cdef, [[
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

	void* OpenProcess(uint32_t dwDesiredAccess, int bInheritHandle, uint32_t dwProcessId);
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

local neatJSON = require("neatjson")

local resolve_profile_json_path

function module.convertTableToJsonString(config)
	return (
		neatJSON(
			config,
			{ wrap = 40, short = true, sort = true, aligned = true, arrayPadding = 1, afterComma = 1, beforeColon1 = 1 }
		)
	)
end

local PROFILE_DEFAULT_NAME = "Standard"
local helper_root_path = paths.dataRoot()
local profiles_root_path = helper_root_path .. "\\Profiles"
local active_profile_file_path = profiles_root_path .. "\\active_profile.txt"
local profile_state = { initialized = false, active = PROFILE_DEFAULT_NAME }

local function normalize_path_slashes(path)
	path = tostring(path or "")
	return path:gsub("/", "\\")
end

local function is_json_path(path)
	return type(path) == "string" and path:lower():match("%.json$") ~= nil
end

local function get_parent_dir(path)
	path = normalize_path_slashes(path)
	return path:match("^(.*)\\[^\\]+$")
end

local function sanitize_profile_name(name)
	name = tostring(name or "")
	name = name:gsub("^%s+", ""):gsub("%s+$", "")
	name = name:gsub("[%c]", "")
	name = name:gsub('[\\/:*?"<>|]', "_")
	return name
end

local function does_file_exist(path)
	if type(doesFileExist) == "function" then
		return doesFileExist(path)
	end
	local f = io.open(path, "rb")
	if f then
		f:close()
		return true
	end
	return false
end

local function ensure_directory(path)
	path = normalize_path_slashes(path)
	if path == "" then
		return false
	end
	if type(doesDirectoryExist) == "function" and doesDirectoryExist(path) then
		return true
	end

	local prefix = ""
	local rest = path

	local drive, tail = path:match("^([A-Za-z]:)\\(.*)$")
	if drive then
		prefix = drive .. "\\"
		rest = tail
	elseif path:sub(1, 2) == "\\\\" then
		local server, share, unc_tail = path:match("^\\\\([^\\]+)\\([^\\]+)\\?(.*)$")
		if server and share then
			prefix = "\\\\" .. server .. "\\" .. share
			rest = unc_tail or ""
		else
			prefix = "\\\\"
			rest = path:sub(3)
		end
	elseif path:sub(1, 1) == "\\" then
		prefix = "\\"
		rest = path:sub(2)
	end

	for part in rest:gmatch("[^\\]+") do
		if prefix == "" or prefix == "\\" then
			prefix = prefix .. part
		elseif prefix:sub(-1) == "\\" then
			prefix = prefix .. part
		else
			prefix = prefix .. "\\" .. part
		end
		if type(doesDirectoryExist) ~= "function" or not doesDirectoryExist(prefix) then
			if type(createDirectory) == "function" then
				createDirectory(prefix)
			end
		end
	end

	if type(doesDirectoryExist) == "function" then
		return doesDirectoryExist(path)
	end
	return true
end

local function read_text_file(path)
	local f = io.open(path, "rb")
	if not f then
		return nil
	end
	local data = f:read("*a")
	f:close()
	return data
end

local function write_text_file(path, text)
	local parent = get_parent_dir(path)
	if parent and parent ~= "" then
		ensure_directory(parent)
	end
	local f = io.open(path, "wb")
	if not f then
		return false
	end
	f:write(text or "")
	f:close()
	return true
end

local function copy_file_raw(src, dst)
	local fsrc = io.open(src, "rb")
	if not fsrc then
		return false
	end
	local data = fsrc:read("*a")
	fsrc:close()

	local parent = get_parent_dir(dst)
	if parent and parent ~= "" then
		ensure_directory(parent)
	end

	local fdst = io.open(dst, "wb")
	if not fdst then
		return false
	end
	fdst:write(data or "")
	fdst:close()
	return true
end

local function ensure_profile_storage()
	if profile_state.initialized then
		return
	end

	ensure_directory(helper_root_path)
	ensure_directory(profiles_root_path)
	ensure_directory(profiles_root_path .. "\\" .. PROFILE_DEFAULT_NAME)

	local active = sanitize_profile_name(read_text_file(active_profile_file_path))
	if active == "" then
		active = PROFILE_DEFAULT_NAME
	end

	profile_state.active = active
	ensure_directory(profiles_root_path .. "\\" .. profile_state.active)
	write_text_file(active_profile_file_path, profile_state.active)
	profile_state.initialized = true
end

local function profile_dir_path(profile_name)
	local safe_name = sanitize_profile_name(profile_name)
	if safe_name == "" then
		safe_name = PROFILE_DEFAULT_NAME
	end
	return profiles_root_path .. "\\" .. safe_name, safe_name
end

local function copy_profile_json_files(from_profile, to_profile)
	local from_dir = profile_dir_path(from_profile)
	local to_dir = profile_dir_path(to_profile)
	if from_dir:lower() == to_dir:lower() then
		return
	end
	if type(doesDirectoryExist) == "function" and not doesDirectoryExist(from_dir) then
		return
	end

	local handle, name = findFirstFile(from_dir .. "\\*")
	if not handle then
		return
	end

	while name do
		if name ~= "." and name ~= ".." then
			local src = from_dir .. "\\" .. name
			local dst = to_dir .. "\\" .. name
			local is_dir = type(doesDirectoryExist) == "function" and doesDirectoryExist(src)
			if (not is_dir) and name:lower():match("%.json$") then
				if not does_file_exist(dst) then
					copy_file_raw(src, dst)
				end
			end
		end
		name = findNextFile(handle)
	end

	findClose(handle)
end

local function remove_path_recursive(path)
	path = normalize_path_slashes(path)
	if path == "" then
		return false
	end

	if type(doesDirectoryExist) == "function" and doesDirectoryExist(path) then
		local handle, name = findFirstFile(path .. "\\*")
		if handle then
			while name do
				if name ~= "." and name ~= ".." then
					local child = path .. "\\" .. name
					if type(doesDirectoryExist) == "function" and doesDirectoryExist(child) then
						local ok = remove_path_recursive(child)
						if not ok then
							findClose(handle)
							return false
						end
					else
						os.remove(child)
						if does_file_exist(child) then
							findClose(handle)
							return false
						end
					end
				end
				name = findNextFile(handle)
			end
			findClose(handle)
		end

		os.remove(path)
		if type(doesDirectoryExist) == "function" and doesDirectoryExist(path) then
			return false
		end
		return true
	end

	if does_file_exist(path) then
		os.remove(path)
		return not does_file_exist(path)
	end
	return true
end

resolve_profile_json_path = function(path)
	path = normalize_path_slashes(path)
	path = paths.remapLegacyDataPath(path)
	if path == "" or not is_json_path(path) then
		return path
	end

	local lower = path:lower()
	if lower:find("helperbyorc\\profiles\\", 1, true) then
		return path
	end

	local marker = "helperbyorc\\"
	local idx = lower:find(marker, 1, true)

	-- Support relative paths (e.g. "binder.json") — expand via paths.dataPath
	if not idx then
		if not lower:find(":\\") and not lower:find("^\\\\") then
			-- Relative path without drive letter — expand to full path
			path = paths.dataPath(path)
			lower = path:lower()
			idx = lower:find(marker, 1, true)
		end
		if not idx then
			return path
		end
	end

	local relative = path:sub(idx + #marker)
	if relative == "" or relative:lower():find("^profiles\\") then
		return path
	end

	ensure_profile_storage()
	local target = profiles_root_path .. "\\" .. profile_state.active .. "\\" .. relative
	local parent = get_parent_dir(target)
	if parent and parent ~= "" then
		ensure_directory(parent)
	end

	return target
end

function module.getDefaultProfileName()
	return PROFILE_DEFAULT_NAME
end

function module.getProfilesRootPath()
	ensure_profile_storage()
	return profiles_root_path
end

function module.getProfilePath(profile_name)
	ensure_profile_storage()
	local path = profile_dir_path(profile_name)
	return path
end

function module.listProfiles()
	ensure_profile_storage()
	local profiles = {}
	local handle, name = findFirstFile(profiles_root_path .. "\\*")
	if handle then
		while name do
			if name ~= "." and name ~= ".." then
				local full = profiles_root_path .. "\\" .. name
				if type(doesDirectoryExist) ~= "function" or doesDirectoryExist(full) then
					profiles[#profiles + 1] = name
				end
			end
			name = findNextFile(handle)
		end
		findClose(handle)
	end
	if #profiles == 0 then
		profiles[1] = PROFILE_DEFAULT_NAME
		ensure_directory(profiles_root_path .. "\\" .. PROFILE_DEFAULT_NAME)
	end
	table.sort(profiles, function(a, b)
		return tostring(a):lower() < tostring(b):lower()
	end)
	return profiles
end

function module.profileExists(profile_name)
	ensure_profile_storage()
	local path = profile_dir_path(profile_name)
	if type(doesDirectoryExist) == "function" then
		return doesDirectoryExist(path)
	end
	return false
end

function module.createProfile(profile_name, opts)
	ensure_profile_storage()
	local safe_name = sanitize_profile_name(profile_name)
	if safe_name == "" then
		return false, "invalid_name"
	end
	local path = profiles_root_path .. "\\" .. safe_name

	local existed = type(doesDirectoryExist) == "function" and doesDirectoryExist(path)
	if not existed then
		ensure_directory(path)
	end
	if type(doesDirectoryExist) == "function" and not doesDirectoryExist(path) then
		return false, "create_failed"
	end

	local source_profile = nil
	local copy_from = false
	if type(opts) == "table" then
		source_profile = opts.from
		copy_from = opts.copy_from ~= false
	elseif type(opts) == "string" then
		source_profile = opts
		copy_from = true
	end

	if copy_from then
		source_profile = sanitize_profile_name(source_profile)
		if source_profile == "" then
			source_profile = profile_state.active
		end
		copy_profile_json_files(source_profile, safe_name)
	end

	return true, safe_name, not existed
end

function module.getActiveProfileName()
	ensure_profile_storage()
	return profile_state.active
end

function module.setActiveProfileName(profile_name)
	ensure_profile_storage()
	local safe_name = sanitize_profile_name(profile_name)
	if safe_name == "" then
		return false, "invalid_name"
	end
	local path = profiles_root_path .. "\\" .. safe_name
	ensure_directory(path)
	if type(doesDirectoryExist) == "function" and not doesDirectoryExist(path) then
		return false, "create_failed"
	end
	profile_state.active = safe_name
	write_text_file(active_profile_file_path, safe_name)
	return true, safe_name
end

function module.deleteProfile(profile_name, opts)
	ensure_profile_storage()
	local safe_name = sanitize_profile_name(profile_name)
	if safe_name == "" then
		return false, "invalid_name"
	end

	local keep_default = not (type(opts) == "table" and opts.keep_default == false)
	local forbid_active = not (type(opts) == "table" and opts.forbid_active == false)

	if keep_default and safe_name == PROFILE_DEFAULT_NAME then
		return false, "default_profile"
	end
	if forbid_active and safe_name == profile_state.active then
		return false, "active_profile"
	end

	local path = profiles_root_path .. "\\" .. safe_name
	if type(doesDirectoryExist) == "function" and not doesDirectoryExist(path) then
		return false, "not_found"
	end

	if not remove_path_recursive(path) then
		return false, "remove_failed"
	end

	if safe_name == profile_state.active then
		profile_state.active = PROFILE_DEFAULT_NAME
		ensure_directory(profiles_root_path .. "\\" .. PROFILE_DEFAULT_NAME)
		write_text_file(active_profile_file_path, profile_state.active)
	end

	return true, safe_name
end

function module.resolveJsonPath(path)
	return resolve_profile_json_path(path)
end

function module.ensureProfileStorage()
	ensure_profile_storage()
	return true
end

ensure_profile_storage()

function module.readFile(path, mode)
	path = tostring(path or "")
	if path == "" then
		return nil
	end
	path = resolve_profile_json_path(path)
	local f = io.open(path, mode or "rb")
	if not f then
		return nil
	end
	local data = f:read("*a")
	f:close()
	return data
end

function module.writeFile(path, data, mode)
	path = tostring(path or "")
	if path == "" then
		return false
	end
	path = resolve_profile_json_path(path)
	local parent = get_parent_dir(path)
	if parent and parent ~= "" then
		ensure_directory(parent)
	end
	local f = io.open(path, mode or "wb")
	if not f then
		return false
	end
	f:write(data or "")
	f:close()
	return true
end

function module.decodeJsonSafe(raw)
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

function module.loadJsonTable(path)
	if type(path) ~= "string" or path == "" then
		return nil
	end
	path = resolve_profile_json_path(path)
	if type(doesFileExist) == "function" and not doesFileExist(path) then
		return nil
	end
	local raw = module.readFile(path, "rb")
	return module.decodeJsonSafe(raw)
end

function module.encodeJsonSafe(tbl, opts)
	opts = opts or {}
	if type(tbl) ~= "table" then
		tbl = {}
	end

	if opts.prefer_neat ~= false then
		local ok, encoded = pcall(module.convertTableToJsonString, tbl)
		if ok and type(encoded) == "string" and encoded ~= "" then
			return encoded
		end
	end

	local encode = rawget(_G, "encodeJson")
	if type(encode) == "function" then
		local ok, encoded = pcall(encode, tbl)
		if ok and type(encoded) == "string" and encoded ~= "" then
			return encoded
		end
	end

	return "{}"
end

function module.backupFile(path, suffix)
	path = tostring(path or "")
	if path == "" then
		return false
	end
	path = resolve_profile_json_path(path)
	local data = module.readFile(path, "rb")
	if type(data) ~= "string" then
		return false
	end
	return module.writeFile(path .. (suffix or ".bak"), data, "wb")
end

function module.applyKnownKeys(dst, src, deepcopy_tables)
	if type(dst) ~= "table" or type(src) ~= "table" then
		return dst
	end
	local need_deepcopy = deepcopy_tables ~= false
	for k, v in pairs(dst) do
		local incoming = src[k]
		if incoming ~= nil then
			if need_deepcopy and type(v) == "table" and type(incoming) == "table" then
				dst[k] = module.deepcopy(incoming)
			else
				dst[k] = incoming
			end
		end
	end
	return dst
end

function module.saveTableToJson(tbl, path)
	path = resolve_profile_json_path(path)
	local ok, saved = pcall(function()
		local content = module.encodeJsonSafe(tbl, { prefer_neat = true })
		return module.writeFile(path, content, "w+b")
	end)
	return ok and saved or false
end

function module.loadTableFromJson(path, defaults)
	local tbl = module.loadJsonTable(path)
	if type(tbl) == "table" then
		return tbl
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
	local main_folder = paths.join(paths.projectRoot(), "screens")
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
	str = tostring(str or "")
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
	s = tostring(s or "")
	return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

function module.escape_imgui_text(text)
	text = tostring(text or "")
	if text:find("%%") then
		text = text:gsub("%%", "%%%%")
	end
	return text
end

function module.flags_or(...)
	local sum = 0
	local bor = bit and bit.bor
	for i = 1, select("#", ...) do
		local value = select(i, ...)
		if value then
			if bor then
				sum = bor(sum, value)
			else
				sum = sum + value
			end
		end
	end
	return sum
end

function module.utf8_len(s)
	s = tostring(s or "")
	local len = #s
	local i, n = 1, 0
	while i <= len do
		n = n + 1
		local c = s:byte(i)
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
	return n
end

function module.normalizeKey(k, vk)
	if vk then
		if k == vk.VK_LSHIFT or k == vk.VK_RSHIFT then
			return vk.VK_SHIFT
		end
		if k == vk.VK_LCONTROL or k == vk.VK_RCONTROL then
			return vk.VK_CONTROL
		end
		if k == vk.VK_LMENU or k == vk.VK_RMENU then
			return vk.VK_MENU
		end
	end
	return k
end

function module.isKeyboardKey(k, vk)
	if type(k) ~= "number" then
		return false
	end
	if vk and k >= vk.VK_LBUTTON and k <= vk.VK_XBUTTON2 then
		return false
	end
	return k >= 0 and k <= 255
end

function module.isMouseKey(k, vk)
	if type(k) ~= "number" then
		return false
	end
	if not vk then
		return false
	end
	return k >= vk.VK_LBUTTON and k <= vk.VK_XBUTTON2
end

function module.isHotkeyKey(k, vk)
	if type(k) ~= "number" then
		return false
	end
	if module.isMouseKey(k, vk) then
		return true
	end
	return module.isKeyboardKey(k, vk)
end

function module.keysMatchCombo(current, combo, normalize_fn)
	if type(combo) ~= "table" or #combo == 0 then
		return false
	end
	current = current or {}
	normalize_fn = normalize_fn or function(v)
		return v
	end
	for i = 1, #combo do
		local target = combo[i]
		local found = false
		for j = 1, #current do
			if normalize_fn(current[j]) == normalize_fn(target) then
				found = true
				break
			end
		end
		if not found then
			return false
		end
	end
	return true
end

function module.rebuildPressedList(pressed_set)
	local list = {}
	for k, v in pairs(pressed_set or {}) do
		if v then
			list[#list + 1] = k
		end
	end
	return list
end

function module.hotkeyToString(keys, vk, empty_text)
	local out = {}
	for _, k in ipairs(keys or {}) do
		out[#out + 1] = vk and vk.id_to_name and vk.id_to_name(k) or tostring(k)
	end
	return #out > 0 and table.concat(out, " + ") or (empty_text or L("hotkey_manager.capture.placeholder"))
end

function module.getHotkeyHelpers(vk, empty_text)
	local function normalize_key(k)
		return module.normalizeKey(k, vk)
	end
	return {
		normalizeKey = normalize_key,
		isKeyboardKey = function(k)
			return module.isKeyboardKey(k, vk)
		end,
		isMouseKey = function(k)
			return module.isMouseKey(k, vk)
		end,
		isHotkeyKey = function(k)
			return module.isHotkeyKey(k, vk)
		end,
		keysMatchCombo = function(current, combo)
			return module.keysMatchCombo(current, combo, normalize_key)
		end,
		rebuildPressedList = module.rebuildPressedList,
		hotkeyToString = function(keys)
			return module.hotkeyToString(keys, vk, empty_text)
		end,
		normalizeHotkeyTable = function(tbl)
			return module.normalizeHotkeyTable(tbl, vk)
		end,
	}
end

function module.normalizeHotkeyTable(tbl, vk)
	local combo = {}
	if type(tbl) ~= "table" then
		return nil
	end
	for _, k in ipairs(tbl) do
		if module.isHotkeyKey(k, vk) then
			local nk = module.normalizeKey(k, vk)
			local dup = false
			for _, cur in ipairs(combo) do
				if cur == nk then
					dup = true
					break
				end
			end
			if not dup then
				combo[#combo + 1] = nk
			end
		end
	end
	if #combo == 0 then
		return nil
	end
	return combo
end

function module.passFilter(text, filter_raw, opts)
	opts = opts or {}
	local trim_fn = opts.trim_fn or module.trim
	local lower_fn = opts.lower_fn or module.string_lower

	local target = opts.target_prepared and tostring(text or "") or lower_fn(text)
	local filter = opts.filter_prepared and tostring(filter_raw or "") or lower_fn(filter_raw)

	if filter == "" then
		return true
	end

	local has_include = false
	local include_matched = false

	for word in filter:gmatch("[^,]+") do
		word = trim_fn(word)
		if word ~= "" then
			local is_exclude = word:sub(1, 1) == "-"
			local value = is_exclude and trim_fn(word:sub(2)) or word
			if value ~= "" then
				local found = target:find(value, 1, true) ~= nil
				if is_exclude then
					if found then
						return false
					end
				else
					has_include = true
					if found then
						include_matched = true
						if opts.return_on_first_include then
							return true
						end
					end
				end
			end
		end
	end

	if opts.require_include then
		return has_include and include_matched
	end
	if has_include then
		return include_matched
	end
	return true
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
	local deltaR = module.rounding(((toR - fromR) / maxhp), 2)
	local deltaG = module.rounding(((toG - fromG) / maxhp), 2)
	local deltaB = module.rounding(((toB - fromB) / maxhp), 2)
	local t = { (fromR + curhp * deltaR), (fromG + curhp * deltaG), (fromB + curhp * deltaB) }
	return t
end

function module.set_triangle_color(r, g, b) -- by etereon
	local bytes = "90909090909090909090909090C744240E00000000909090909090909090909090909090C744240F0000000090B300"
	memory.hex2bin(bytes, 0x60BB41, bytes:len() / 2)
	memory.setint8(0x60BB52, r, false)
	memory.setint8(0x60BB69, g, false)
	memory.setint8(0x60BB6F, b, false)
end

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

function module.translite_name(name)

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

local getBonePosition = ffi.cast("int (__thiscall*)(void*, float*, int, bool)", 0x5E4280)
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
	local cmdLabel = {
		[1] = L("funcs.obs.command.start_recording"),
		[2] = L("funcs.obs.command.stop_recording"),
		[3] = L("funcs.obs.command.save_replay"),
	}

	if tbl[cmd] == nil then
		pushToast(L("funcs.obs.toast.unknown_command"), "warn")
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
		pushToast(
			L("funcs.obs.toast.connect_error", {
				error = err or L("funcs.common.unknown"),
			}),
			"err"
		)
		return false
	end
	client:send(tbl[cmd])

	client:close()
	pushToast(L("funcs.obs.toast.command_sent", {
		command = cmdLabel[cmd] or L("funcs.obs.command.sent"),
	}), "ok")
end

function module.findSignatureInModule(signature, moduleName, data)
	local moduleAddress = ffi.C.GetModuleHandleA(moduleName)
	if moduleAddress == nil then
		return nil, L("funcs.signature.module_not_found")
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
	return nil, L("funcs.signature.signature_not_found")
end

local correct_module

local function getCorrectModule()
	if type(correct_module) == "table" then
		return correct_module
	end

	local ok, loaded = pcall(require, "HelperByOrc.correct")
	if ok and type(loaded) == "table" then
		correct_module = loaded
		return correct_module
	end

	return nil
end

function module.handleCorrection(message, setText)
	local correct = getCorrectModule()
	if correct and type(correct.handleCorrection) == "function" then
		return correct.handleCorrection(message, setText)
	end
end

function module.handleLanguageTool(message, setText)
	local correct = getCorrectModule()
	if correct and type(correct.handleLanguageTool) == "function" then
		return correct.handleLanguageTool(message, setText)
	end
end

function module.handleAutoCorrection(message, setText, provider)
	local correct = getCorrectModule()
	if correct and type(correct.handleAuto) == "function" then
		return correct.handleAuto(message, setText, provider)
	end
end

-- ========== ТИП ТРАНСПОРТА ==========
local _veh_type_defs = {
	{ "IsBoatModel",         0x4C5A70, "Boat"         },
	{ "IsCarModel",          0x4C5AA0, "Car"          },
	{ "IsTrainModel",        0x4C5AD0, "Train"        },
	{ "IsHeliModel",         0x4C5B00, "Heli"         },
	{ "IsPlaneModel",        0x4C5B30, "Plane"        },
	{ "IsBikeModel",         0x4C5B60, "Bike"         },
	{ "IsFakePlaneModel",    0x4C5B90, "FakePlane"    },
	{ "IsMonsterTruckModel", 0x4C5BC0, "MonsterTruck" },
	{ "IsQuadBikeModel",     0x4C5BF0, "QuadBike"     },
	{ "IsBmxModel",          0x4C5C20, "Bicycle"      },
	{ "IsTrailerModel",      0x4C5C50, "Trailer"      },
}
local _veh_type_checkers = {}
for _, v in ipairs(_veh_type_defs) do
	_veh_type_checkers[v[1]] = ffi.cast("bool (__cdecl *)(int)", v[2])
end

function module.getVehicleType(modelId)
	modelId = tonumber(modelId) or 0
	for _, v in ipairs(_veh_type_defs) do
		local ok, res = pcall(_veh_type_checkers[v[1]], modelId)
		if ok and res then return v[3] end
	end
	return "unknown"
end

module.deepCopyTable = module.deepcopy

function module.tablesShallowEqual(a, b)
	if type(a) ~= "table" or type(b) ~= "table" then
		return a == b
	end
	for k, v in pairs(a) do
		local bv = b[k]
		if type(v) == "table" and type(bv) == "table" then
			if not module.tablesShallowEqual(v, bv) then
				return false
			end
		elseif v ~= bv then
			return false
		end
	end
	for k in pairs(b) do
		if a[k] == nil then
			return false
		end
	end
	return true
end

return module
