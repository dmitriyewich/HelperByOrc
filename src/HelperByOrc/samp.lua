local module = {}
local encoding = require("encoding")
local language = require("language")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ffi = require("ffi")
local memory = require("memory")
local tags

local function L(key, params)
	return language.getText(key, params)
end

function module.attachModules(mod)
	tags = mod.tags
end

module.currentVersion, module.sampModule = nil, getModuleHandle("samp.dll")
module._dialog_hook_bypass_depth = 0

local function has_sampfuncs_module()
	if type(getModuleHandle) ~= "function" then
		return false
	end
	local ok, handle = pcall(getModuleHandle, "SAMPFUNCS.asi")
	if not ok then
		return false
	end
	return (tonumber(handle) or 0) ~= 0
end

module.hasSampfuncs = has_sampfuncs_module
module.BACKEND_STANDARD = "standard"
module.BACKEND_SAMPFUNCS = "sampfuncs"
module.BACKEND_ARIZONA = "arizona"
module._function_backend_mode = module.BACKEND_STANDARD
module._function_backend_active = module.BACKEND_STANDARD
module.DIALOG_STYLE_MSGBOX = 0
module.DIALOG_STYLE_INPUT = 1
module.DIALOG_STYLE_LIST = 2
module.DIALOG_STYLE_PASSWORD = 3
module.DIALOG_STYLE_TABLIST = 4
module.DIALOG_STYLE_TABLIST_HEADERS = 5

local SAMP_GLOBAL_NAMES = {
	"sampAddChatMessage",
	"sampGetChatInputText",
	"sampGetChatString",
	"sampIsChatInputActive",
	"sampIsDialogActive",
	"sampProcessChatInput",
	"sampSendChat",
	"sampSendDialogResponse",
	"sampSetChatInputEnabled",
	"sampSetChatInputText",
}

local ARIZONA_NATIVE_PRIORITY_NAMES = {
	"sampGetChatInputText",
	"sampGetChatString",
	"sampSetChatInputText",
}

local ARIZONA_NATIVE_PRIORITY_GLOBALS = {
	sampGetChatInputText = true,
	sampGetChatString = true,
	sampSetChatInputText = true,
}

local SAMP_GLOBAL_STATE_KEY = "__helperbyorc_samp_globals"
local samp_global_state = rawget(_G, SAMP_GLOBAL_STATE_KEY)
if type(samp_global_state) ~= "table" then
	samp_global_state = { originals = {} }
	rawset(_G, SAMP_GLOBAL_STATE_KEY, samp_global_state)
end

local managed_samp_globals = {}
local active_samp_global_sources = {}

local function capture_original_samp_global(name)
	local originals = samp_global_state.originals
	if originals[name] ~= nil then
		return
	end
	local current = rawget(_G, name)
	if current == nil then
		originals[name] = false
		return
	end
	originals[name] = current
end

for i = 1, #SAMP_GLOBAL_NAMES do
	capture_original_samp_global(SAMP_GLOBAL_NAMES[i])
end

local function get_original_samp_global(name)
	local value = samp_global_state.originals[name]
	if value == false then
		return nil
	end
	return value
end

local function normalize_backend_mode(mode)
	mode = tostring(mode or ""):lower()
	if mode == module.BACKEND_SAMPFUNCS then
		return module.BACKEND_SAMPFUNCS
	end
	if mode == module.BACKEND_ARIZONA then
		return module.BACKEND_ARIZONA
	end
	return module.BACKEND_STANDARD
end

local function should_prefer_native_samp_global(mode, name)
	if mode == module.BACKEND_SAMPFUNCS then
		return true
	end
	if mode == module.BACKEND_ARIZONA then
		return ARIZONA_NATIVE_PRIORITY_GLOBALS[name] == true
	end
	return false
end

local entryPoint = {
	[0x2E2BB7] = { "E", false },
	[0x31DF13] = { "R1", true },
	[0x3195DD] = { "R2", false },
	[0xCC490] = { "R3", false },
	[0xCC4D0] = { "R3-1", true },
	[0xCBCD0] = { "R4", false },
	[0xCBCB0] = { "R4-2", false },
	[0xCBC90] = { "R5-2", true },
	[0xFDB60] = { "DL-R1", true },
}

module.main_offsets = {
	["SAMP_INFO_OFFSET"] = {
		["R1"] = 0x21A0F8,
		["R3-1"] = 0x26E8DC,
		["R5-2"] = 0x26EB94,
		["DL-R1"] = 0x2ACA24,
	},
	["rakclient_interface"] = {
		["R1"] = 0x3C9,
		["R3-1"] = 0x2C,
		["R5-2"] = 0x0,
		["DL-R1"] = 0x2C,
	},
	["SAMP_DIALOG_INFO_OFFSET"] = {
		["R1"] = 0x21A0B8,
		["R3-1"] = 0x26E898,
		["R5-2"] = 0x26EB50,
		["DL-R1"] = 0x2AC9E0,
	},
	["SAMP_DIALOG_ACTIVE_OFFSET"] = {
		["R1"] = 0x28,
		["R3-1"] = 0x28,
		["R5-2"] = 0x28,
		["DL-R1"] = 0x28,
	},
	["SAMP_DIALOG_ID_OFFSET"] = {
		["R1"] = 0x30,
		["R3-1"] = 0x30,
		["R5-2"] = 0x30,
		["DL-R1"] = 0x30,
	},
	["SAMP_DIALOG_TEXT_OFFSET"] = {
		["R1"] = 0x34,
		["R3-1"] = 0x34,
		["R5-2"] = 0x34,
		["DL-R1"] = 0x34,
	},
	["CDialog_Show"] = {
		["R1"] = 0x6B9C0,
		["R3-1"] = 0x6F8C0,
		["R5-2"] = 0x6FFB0,
		["DL-R1"] = 0x6FA50,
	},
	["CDialog_Close"] = {
		["R1"] = 0x6C040,
		["R3-1"] = 0x6FF40,
		["R5-2"] = 0x70630,
		["DL-R1"] = 0x700D0,
	},
	["SAMP_DIALOG_CAPTION_OFFSET"] = {
		["R1"] = 0x40,
		["R3-1"] = 0x40,
		["R5-2"] = 0x40,
		["DL-R1"] = 0x40,
	},
	["CDXUTEditBox_GetText"] = {
		["R1"] = 0x81030,
		["R3-1"] = 0x84F40,
		["R5-2"] = 0x85650,
		["DL-R1"] = 0x850D0,
	},
	["CDXUTEditBox_SetText"] = {
		["R1"] = 0x80F60,
		["R3-1"] = 0x84E70,
		["R5-2"] = 0x85580,
		["DL-R1"] = 0x85000,
	},
	["pDialogInput_pEditBox"] = { ["R1"] = 0x24, ["R3-1"] = 0x24, ["R5-2"] = 0x24, ["DL-R1"] = 0x24 },
	["pChatInput_pEditBox"] = { ["R1"] = 0x8, ["R3-1"] = 0x8, ["R5-2"] = 0x8, ["DL-R1"] = 0x8 },
	["CDXUTDialog"] = { ["R1"] = 0x1C, ["R3-1"] = 0x1C, ["R5-2"] = 0x1C, ["DL-R1"] = 0x1C },
	["SetCursorMode"] = { ["R1"] = 0x9BD30, ["R3-1"] = 0x9FFE0, ["R5-2"] = 0xA06F0, ["DL-R1"] = 0xA0530 },
	["RefGame"] = { ["R1"] = 0x21A10C, ["R3-1"] = 0x26E8F4, ["R5-2"] = 0x26EBAC, ["DL-R1"] = 0x2ACA3C },
	["AddEntry"] = {
		["R1"] = 0x64010,
		["R3-1"] = 0x67460,
		["R5-2"] = 0x67BE0,
		["DL-R1"] = 0x067650,
	},
	["RenderEntry"] = {
		["R1"] = 0x638A0,
		["R3-1"] = 0x66CF0,
		["R5-2"] = 0x68380,
		["DL-R1"] = 0x66EE0,
	},
	["pChat"] = {
		["R1"] = 0x21A0E4,
		["R3-1"] = 0x26E8C8,
		["R5-2"] = 0x26EB80,
		["DL-R1"] = 0x2ACA10,
	},
	["CHAT_TEXT_OFFSET"] = {
		["R1"] = 0x20,
		["R3-1"] = 0x20,
		["R5-2"] = 0x20,
		["DL-R1"] = 0x20,
	},
	["CHAT_PREFIX_TEXT_OFFSET"] = {
		["R1"] = 0x4,
		["R3-1"] = 0x4,
		["R5-2"] = 0x4,
		["DL-R1"] = 0x4,
	},
	["CHAT_COLOR_OFFSET"] = {
		["R1"] = 0xF4,
		["R3-1"] = 0xF4,
		["R5-2"] = 0xF4,
		["DL-R1"] = 0xF4,
	},
	["CHAT_PREFIX_COLOR_OFFSET"] = {
		["R1"] = 0xF8,
		["R3-1"] = 0xF8,
		["R5-2"] = 0xF8,
		["DL-R1"] = 0xF8,
	},
	["AddChatMessage"] = {
		["R1"] = 0x645A0,
		["R3-1"] = 0x679F0,
		["R5-2"] = 0x68170,
		["DL-R1"] = 0x67650,
	},

	["SAMP_CHAT_INPUT_INFO_OFFSET"] = { ["R1"] = 0x21A0E8, ["R3-1"] = 0x26E8CC, ["R5-2"] = 0x26EB84, ["DL-R1"] = 0x2ACA14 },
	["CInput_Opened"] = { ["R1"] = 0x14E0, ["R3-1"] = 0x14E0, ["R5-2"] = 0x14E0, ["DL-R1"] = 0x14E0 },

	["OnResetDevice"] = {
		["R1"] = 0x64600,
		["R3-1"] = 0x67A50,
		["R5-2"] = 0x681D0,
		["DL-R1"] = 0x67C40,
	},

	["CInput_Open"] = { ["R1"] = 0x657E0, ["R3-1"] = 0x68D10, ["R5-2"] = 0x69480, ["DL-R1"] = 0x68EC0 },
	["CInput_Close"] = { ["R1"] = 0x658E0, ["R3-1"] = 0x68E10, ["R5-2"] = 0x69580, ["DL-R1"] = 0x68FC0 },
	["CInput_Close_fix"] = { ["R1"] = 0x6B9FB, ["R3-1"] = 0x6F8FB, ["R5-2"] = 0x69580, ["DL-R1"] = 0x68FC0 },

	["SetPageSize"] = { ["R1"] = 0x636D0, ["R3-1"] = 0x66B20, ["R5-2"] = 0x672A0, ["DL-R1"] = 0x66D10 },
	["PageSize_MAX"] = { ["R1"] = 0x64A51, ["R3-1"] = 0x67EB1, ["R5-2"] = 0x68621, ["DL-R1"] = 0x68091 },
	["PageSize_StringInfo"] = { ["R1"] = 0xD7AD5, ["R3-1"] = 0xE9DB5, ["R5-2"] = 0xE9E0D, ["DL-R1"] = 0x11BE45 },

	["CInput_Send"] = {
		["R1"] = 0x65C60,
		["R3-1"] = 0x69190,
		["R5-2"] = 0x69900,
		["DL-R1"] = 0x69340,
	},
	["CInput_SendSay"] = {
		["R1"] = 0x57F0,
		["R3-1"] = 0x5820,
		["R5-2"] = 0x5A10,
		["DL-R1"] = 0x5860,
	},
	["GetName"] = { ["R1"] = 0x13CE0, ["R3-1"] = 0x16F00, ["R5-2"] = 0x175C0, ["DL-R1"] = 0x170D0 },
	["SAMP_SLOCALPLAYERID_OFFSET"] = { ["R1"] = 0x4, ["R3-1"] = 0x2F1C, ["R5-2"] = 0x4, ["DL-R1"] = 0x0 },
	["SAMP_INFO_OFFSET_Pools"] = { ["R1"] = 0x3CD, ["R3-1"] = 0x3DE, ["R5-2"] = 0x3DE, ["DL-R1"] = 0x3DE },
	["SAMP_INFO_OFFSET_Pools_Player"] = { ["R1"] = 0x18, ["R3-1"] = 0x8, ["R5-2"] = 0x4, ["DL-R1"] = 0x8 },
	["SAMP_INFO_OFFSET_Pools_Veh"] = { ["R1"] = 0x1C, ["R3-1"] = 0xC, ["R5-2"] = 0x0, ["DL-R1"] = 0xC },
	["SAMP_COLOR_OFFSET"] = { ["R1"] = 0x216378, ["R3-1"] = 0x151578, ["R5-2"] = 0x151828, ["DL-R1"] = 0x18F6C0 },
	["ID_Find"] = { ["R1"] = 0x10420, ["R3-1"] = 0x13570, ["R5-2"] = 0x138C0, ["DL-R1"] = 0x137C0 },
	["CPlayerPool_IsConnected"] = { ["R1"] = 0x10B0, ["R3-1"] = 0x10B0, ["R5-2"] = 0x10B0, ["DL-R1"] = 0x10B0 },
	["IDcar_Find"] = { ["R1"] = 0x1B0A0, ["R3-1"] = 0x1E440, ["R5-2"] = 0x1EB90, ["DL-R1"] = 0x1E650 },
	["SAMP_PREMOTEPLAYER_OFFSET"] = { ["R1"] = 0x2E, ["R3-1"] = 0x4, ["R5-2"] = 0x1F8A, ["DL-R1"] = 0x26 },
	["SAMP_REMOTEPLAYERDATA_OFFSET"] = { ["R1"] = 0x0, ["R3-1"] = 0x0, ["R5-2"] = 0x10, ["DL-R1"] = 0x8 },
	["pSAMP_Actor"] = { ["R1"] = 0x0, ["R3-1"] = 0x0, ["R5-2"] = 0x0, ["DL-R1"] = 0x0 },
	["pGTA_Ped"] = { ["R1"] = 0x2a4, ["R3-1"] = 0x2a4, ["R5-2"] = 0x2a4, ["DL-R1"] = 0x2a4 },
	["IsConnected"] = { ["R1"] = 0x10B0, ["R3-1"] = 0x10B0, ["R5-2"] = 0x10B0, ["DL-R1"] = 0x10B0 },

	["SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET"] = {
		["R1"] = 0x1BC,
		["R3-1"] = 0x1B0,
		["R5-2"] = 0x1B0,
		["DL-R1"] = 0x1B0,
	},
	["SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET"] = {
		["R1"] = 0x1B8,
		["R3-1"] = 0x1AC,
		["R5-2"] = 0x1B0,
		["DL-R1"] = 0x1AC,
	},
	["CDXUTListBox__GetSelectedIndex"] = {
		["R1"] = 0x84850,
		["R3-1"] = 0x88760,
		["R5-2"] = 0x88E70,
		["DL-R1"] = 0x888F0,
	},
	["CDXUTListBox__GetItem"] = {
		["R1"] = 0x86390,
		["R3-1"] = 0x8A2B0,
		["R5-2"] = 0x8A9C0,
		["DL-R1"] = 0x8A440,
	},
	["SAMP_SET_DIALOG_LIST_ITEM_OFFSET"] = {
		["R1"] = 0x863C0,
		["R3-1"] = 0x8A2E0,
		["R5-2"] = 0x8A9F0,
		["DL-R1"] = 0x8A470,
	},
}

function module.isSAMPInitilizeLua()
	if module.sampModule <= 0 then
		return false
	end
	if
		memory.getint32(module.sampModule + module.main_offsets.SAMP_INFO_OFFSET[module.currentVersion], false) ~= 0
		and module.currentVersion ~= "UNKNOWN"
	then
		return true
	end
	return false
end

function module.isSampLoadedLua()
	if module.sampModule <= 0 then
		return false
	end
	if not module.currentVersion then
		local ep = memory.getuint32(module.sampModule + memory.getint32(module.sampModule + 0x3C, false) + 0x28, false)
		module.currentVersion = entryPoint[ep][1]
		if not entryPoint[ep][2] then
			print(L("samp.log.version_not_supported", {
				version = module.currentVersion,
			}))
			thisScript():unload()
		end
		if not module.currentVersion then
			assert(entryPoint[ep], (L("samp.error.unknown_version_entry_point")):format(ep))
		end
	end
	return true
end

module.isSampLoadedLua()
module.isSAMPInitilizeLua()

local GetName = ffi.cast(
	"const char*(__thiscall *)(uintptr_t, unsigned short)",
	module.sampModule + module.main_offsets.GetName[module.currentVersion]
)

local getEditboxText = ffi.cast(
	"char*(__thiscall *)(uintptr_t this)",
	module.sampModule + module.main_offsets.CDXUTEditBox_GetText[module.currentVersion]
)

local setEditboxText = ffi.cast(
	"void(__thiscall *)(uintptr_t this, char* text, int i)",
	module.sampModule + module.main_offsets.CDXUTEditBox_SetText[module.currentVersion]
)

local SetPageSizeFunc = ffi.cast(
	"void(__thiscall *)(uintptr_t, int)",
	module.sampModule + module.main_offsets.SetPageSize[module.currentVersion]
)

local ID_Find = ffi.cast(
	"int (__thiscall *)(intptr_t, intptr_t)",
	module.sampModule + module.main_offsets.ID_Find[module.currentVersion]
)

local CPlayerPool_IsConnected = ffi.cast(
	"bool (__thiscall *)(intptr_t, unsigned short)",
	module.sampModule + module.main_offsets.CPlayerPool_IsConnected[module.currentVersion]
)

local CDXUTListBox__GetSelectedIndex = ffi.cast(
	"int(__thiscall *)(uintptr_t, int)",
	module.sampModule + module.main_offsets.CDXUTListBox__GetSelectedIndex[module.currentVersion]
)

local CDXUTListBox__GetItem = ffi.cast(
	"uintptr_t(__thiscall *)(uintptr_t, int)",
	module.sampModule + module.main_offsets.CDXUTListBox__GetItem[module.currentVersion]
)

function module.PedPool()
	local OFFSET_SampInfo =
		memory.getint32(module.sampModule + module.main_offsets.SAMP_INFO_OFFSET[module.currentVersion], true)
	local OFFSET_SampInfo_pPools =
		memory.getint32(OFFSET_SampInfo + module.main_offsets.SAMP_INFO_OFFSET_Pools[module.currentVersion], true)
	local OFFSET_SampInfo_pPools_Player = memory.getint32(
		OFFSET_SampInfo_pPools + module.main_offsets.SAMP_INFO_OFFSET_Pools_Player[module.currentVersion],
		true
	)
	return OFFSET_SampInfo_pPools_Player
end

function module.GetNameID(id)
	id = tonumber(id)
	if not id then
		return "UNKNOWN"
	end
	id = math.floor(id)
	if id < 0 or id > 1003 then
		return "UNKNOWN"
	end
	if not module.IsConnected(id) then
		return "UNKNOWN"
	end

	local pool = module.PedPool()
	if not pool or pool == 0 then
		return "UNKNOWN"
	end

	local ok_call, nick_ptr = pcall(GetName, pool, id)
	if not ok_call or nick_ptr == nil then
		return "UNKNOWN"
	end

	local ok_str, nick = pcall(ffi.string, nick_ptr)
	if not ok_str or type(nick) ~= "string" or nick == "" then
		return "UNKNOWN"
	end

	return nick
end

function module.GetIDByName(name)
	local target = tostring(name or ""):gsub("^%s*(.-)%s*$", "%1")
	if target == "" then
		return nil
	end

	for id = 0, 1003 do
		if module.IsConnected(id) then
			local nick = module.GetNameID(id)
			if type(nick) == "string" and nick ~= "" and nick ~= "UNKNOWN" and nick == target then
				return id
			end
		end
	end

	return nil
end

function module.IsConnected(id)
	id = tonumber(id)
	if not id then
		return false
	end
	id = math.floor(id)
	if id < 0 or id > 1003 then
		return false
	end

	local local_id = module.Local_ID()
	if local_id ~= nil and id == local_id then
		return true
	end

	local pool = module.PedPool()
	if not pool or pool == 0 then
		return false
	end

	local ok, connected = pcall(CPlayerPool_IsConnected, pool, id)
	if not ok then
		return false
	end
	return connected and true or false
end

pcall(ffi.cdef, [[
	typedef struct CUniBuffer {
		unsigned __int16 *m_pwszBuffer;
	} CUniBuffer;

	typedef struct CDXUTEditBox {
		void* baseclass_0;
		struct CUniBuffer* m_buffer;
	} CDXUTEditBox;

	typedef struct stDLG {
		void* m_pDevice;
		long unsigned int m_position[2];
		long unsigned int m_size[2];
		long unsigned int m_buttonOffset[2];
		void* m_pDialog;
		void* m_pListbox;
		struct CDXUTEditBox* m_pEditbox;
	} stDLG;
]])

function module.pDialog_func()
	return memory.read(module.sampModule + module.main_offsets.SAMP_DIALOG_INFO_OFFSET[module.currentVersion], 4, true)
end

function module.sampSetCurrentDialogEditboxTextFix(newstring)
	local st1 = ffi.cast("stDLG*", module.pDialog_func())
	ffi.cast(
		"void(__thiscall *)(struct CDXUTEditBox*, const char *, char)",
		module.sampModule + module.main_offsets.CDXUTEditBox_SetText[module.currentVersion]
	)(st1.m_pEditbox, ffi.cast("const char *", newstring), 0)
end

function module.isDialogActive()
	local isActive = memory.read(
		module.pDialog_func() + module.main_offsets.SAMP_DIALOG_ACTIVE_OFFSET[module.currentVersion],
		4,
		true
	)
	return isActive == 1 and true or false
end

function module.getCurrentDialogPosition()
	if module.isDialogActive() then
		local CDialog = module.pDialog_func()
		local CDXUTDialog = memory.getuint32(CDialog + module.main_offsets.CDXUTDialog[module.currentVersion], false)
		local x = memory.read(CDialog + 0x04, 4, true) + memory.read(CDXUTDialog + 0x116, 4, true)
		local y = memory.read(CDialog + 0x08, 4, true) + memory.read(CDXUTDialog + 0x11A, 4, true)
		local SizeX = memory.read(CDialog + 0xC, 4, true)
		local SizeY = memory.read(CDialog + 0x10, 4, true)
		return x, y, SizeX, SizeY
	end
	return 0, 0, 0, 0
end



local SetCursorMode = ffi.cast(
	"char*(__thiscall *)(uintptr_t, int, bool)",
	module.sampModule + module.main_offsets.SetCursorMode[module.currentVersion]
)

function module.Set_CursorMode(int, bool)
	local Cgame = memory.getuint32(module.sampModule + module.main_offsets.RefGame[module.currentVersion], true)
	SetCursorMode(Cgame, int, bool)
end

function module.hideDialog()
	-- memory.setint64(pDialog_func() + 40, bool and 1 or 0, true)
	local Cgame = memory.getuint32(module.sampModule + module.main_offsets.RefGame[module.currentVersion], true)
	memory.setint64(module.pDialog_func() + 40, 0, true)
	SetCursorMode(Cgame, 0, false)
	-- sampToggleCursor(bool)
end

function module.CDialog_Close_func(int)
	if module.isDialogActive() then
		module._dialog_hook_bypass_depth = (tonumber(module._dialog_hook_bypass_depth) or 0) + 1
		local result = { pcall(
			callMethod,
			module.sampModule + module.main_offsets.CDialog_Close[module.currentVersion],
			module.pDialog_func(),
			1,
			0,
			int
		) }
		module._dialog_hook_bypass_depth = math.max((tonumber(module._dialog_hook_bypass_depth) or 1) - 1, 0)
		if not result[1] then
			error(result[2], 0)
		end
		return unpack(result, 2)
	end
end

function module.isDialogHookBypassActive()
	return (tonumber(module._dialog_hook_bypass_depth) or 0) > 0
end

function module.pDialogInput_pEditBox_func()
	return memory.read(module.pDialog_func() + module.main_offsets.pDialogInput_pEditBox[module.currentVersion], 4, true)
end

function module.pDialogInput_pEditBox_active_func()
	return memory.read(module.pDialogInput_pEditBox_func() + 0x4, 1, true) == 1 and true or false
end

function module.SAMP_DIALOG_ID()
	if module.isDialogActive() then
		return memory.read(
			module.pDialog_func() + module.main_offsets.SAMP_DIALOG_ID_OFFSET[module.currentVersion],
			4,
			true
		)
	end
end

function module.sampSetDialogInputCursor(cursor)
	if module.isDialogActive() then
		memory.setint8(module.pDialogInput_pEditBox_func() + 0x11E, cursor, false)
		memory.setint8(module.pDialogInput_pEditBox_func() + 0x119, cursor, false)
	end
end

function module.sampGetDialogInputCursor()
	if module.isDialogActive() and module.pDialogInput_pEditBox_active_func() then
		local pos1 = memory.getint8(module.pDialogInput_pEditBox_func() + 0x11E, false) -- Начало выделения
		local pos2 = memory.getint8(module.pDialogInput_pEditBox_func() + 0x119, false) -- Конец выделенного текста.
		return pos1, pos2
	end
end

function module.SAMP_CHAT_INPUT_INFO_OFFSET_func()
	return memory.read(module.sampModule + module.main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET[module.currentVersion], 4, true)
end

function module.SAMP_CHAT_INPUT_INFO_OFFSET_func_test()
	return memory.read(module.SAMP_CHAT_INPUT_INFO_OFFSET_func() + 0x8, 4, true)
end

function module.sampGetChatEditboxText()
	return ffi.string(getEditboxText(module.SAMP_CHAT_INPUT_INFO_OFFSET_func_test()))
end

function module.pCInput_Open_Close(bool)
	local adress = bool and module.main_offsets.CInput_Open[module.currentVersion]
		or module.main_offsets.CInput_Close[module.currentVersion]
	callMethod(module.sampModule + adress, module.SAMP_CHAT_INPUT_INFO_OFFSET_func(), 0, 0)
end

function module.sampSetChatInputCursor(cursor)
	memory.setint8(module.SAMP_CHAT_INPUT_INFO_OFFSET_func_test() + 0x11E, cursor, false)
	memory.setint8(module.SAMP_CHAT_INPUT_INFO_OFFSET_func_test() + 0x119, cursor, false)
end

function module.sampGetDialogText()
	if module.isDialogActive() then
		local text = memory.read(
			module.pDialog_func() + module.main_offsets.SAMP_DIALOG_TEXT_OFFSET[module.currentVersion],
			4,
			true
		)
		return memory.tostring(text)
	end
	return ""
end

function module.getListItemNumberByText(text)
	local dtext = module.sampGetDialogText()
	local arr = {}
	for str in string.gmatch(dtext, "([^\n]+)") do
		table.insert(arr, str)
	end

	for i = 1, #arr do
		if arr[i]:find(text) then
			return i - 1
		end
	end
	return false
end

function module.get_dialog_caption()
	if module.isDialogActive() then
		return memory.tostring(
			module.pDialog_func() + module.main_offsets.SAMP_DIALOG_CAPTION_OFFSET[module.currentVersion]
		)
	end
	return ""
end

function module.sampGetDialogEditboxText()
	local style = module.GetCurrentDialogStyle()
	if module.isDialogActive() and module.isDialogInputStyle(style) and module.pDialogInput_pEditBox_active_func() then
		return ffi.string(getEditboxText(module.pDialogInput_pEditBox_func()))
	end
	return ""
end

function module.sampSetDialogEditboxText(text)
	local style = module.GetCurrentDialogStyle()
	if module.isDialogActive() and module.isDialogInputStyle(style) and module.pDialogInput_pEditBox_active_func() then
		return setEditboxText(module.pDialogInput_pEditBox_func(), ffi.cast("char*", text), 0)
	end
end

function module.SetCurrentDialogListItem(int)
	local style = module.GetCurrentDialogStyle()
	if module.isDialogActive() and module.isDialogListStyle(style) then
		local SAMP_DIALOG_PTR2_OFFSET = memory.read(module.pDialog_func() + 0x20, 4, true)
		callMethod(module.sampModule + module.main_offsets.SAMP_SET_DIALOG_LIST_ITEM_OFFSET[module.currentVersion], SAMP_DIALOG_PTR2_OFFSET, 1, 0, int)
	end
end

local function getCurrentDialogListBoxPointer()
	if not module.isDialogActive() then
		return nil
	end
	local style = module.GetCurrentDialogStyle()
	if not module.isDialogListStyle(style) then
		return nil
	end
	local pListBox = memory.read(module.pDialog_func() + 0x20, 4, true)
	if not pListBox or pListBox == 0 then
		return nil
	end
	return pListBox
end

local function getCurrentDialogSelectedIndex()
	local pListBox = getCurrentDialogListBoxPointer()
	if not pListBox then
		return nil
	end
	return CDXUTListBox__GetSelectedIndex(pListBox, -1), pListBox
end

function module.GetCurrentDialogListItem()
	local selectedIndex = getCurrentDialogSelectedIndex()
	return selectedIndex
end

function module.getDialogSelectedItemText()
	local selectedIndex, pListBox = getCurrentDialogSelectedIndex()
	if selectedIndex == nil or pListBox == nil then return false, "" end
	local pItem = CDXUTListBox__GetItem(pListBox, selectedIndex)
	return true, ffi.string(ffi.cast("const char*", pItem))
end

function module.GetCurrentDialogStyle()
	if module.isDialogActive() then
		return memory.getint32(module.pDialog_func() + 0x2C, false)
	end
end

function module.isDialogInputStyle(style)
	style = tonumber(style)
	return style == module.DIALOG_STYLE_INPUT or style == module.DIALOG_STYLE_PASSWORD
end

function module.isDialogListStyle(style)
	style = tonumber(style)
	return style == module.DIALOG_STYLE_LIST
		or style == module.DIALOG_STYLE_TABLIST
		or style == module.DIALOG_STYLE_TABLIST_HEADERS
end

function module.submitCurrentDialog(button, listItem, inputText)
	if not module.isDialogActive() then
		return false, "no_dialog"
	end

	button = tonumber(button)
	if button == nil then
		return false, "invalid_button"
	end
	button = math.floor(button)
	if button ~= 0 and button ~= 1 then
		return false, "invalid_button"
	end

	local style = tonumber(module.GetCurrentDialogStyle())
	if style == nil then
		return false, "no_style"
	end

	if button == 1 then
		if module.isDialogInputStyle(style) then
			if inputText ~= nil then
				local ok_set, err = pcall(module.sampSetDialogEditboxText, tostring(inputText))
				if not ok_set then
					return false, "set_input_failed", err
				end
			end
		elseif module.isDialogListStyle(style) then
			if listItem ~= nil then
				listItem = tonumber(listItem)
				if listItem == nil then
					return false, "invalid_listitem"
				end
				listItem = math.floor(listItem)
				local ok_set, err = pcall(module.SetCurrentDialogListItem, listItem)
				if not ok_set then
					return false, "set_listitem_failed", err
				end
			end
		end
	end

	local ok_close, err = pcall(module.CDialog_Close_func, button)
	if not ok_close then
		return false, "close_failed", err
	end
	return true, style
end

function module.GetCurrentDialogListboxItemsCount()
	local style = module.GetCurrentDialogStyle()
	if module.isDialogActive() and module.isDialogListStyle(style) then
		local SAMP_DIALOG_PTR2_OFFSET = memory.read(module.pDialog_func() + 0x20, 4, true)
		local SAMP_DIALOG_LINECOUNT_OFFSET = memory.read(SAMP_DIALOG_PTR2_OFFSET + 0x150, 4, true)
		return SAMP_DIALOG_LINECOUNT_OFFSET
	end
end

local bit = require("bit")

pcall(ffi.cdef, [[
	typedef unsigned long DWORD;
	typedef struct {
		void* BaseAddress;
		void* AllocationBase;
		DWORD AllocationProtect;
		size_t RegionSize;
		DWORD State;
		DWORD Protect;
		DWORD Type;
	} MEMORY_BASIC_INFORMATION;
	size_t __stdcall VirtualQuery(const void* lpAddress, MEMORY_BASIC_INFORMATION* lpBuffer, size_t dwLength);
]])

local MEM_COMMIT = 0x1000
local PAGE_NOACCESS = 0x01
local PAGE_GUARD = 0x100
local CHAT_ENTRY_BASE_OFFSET = 0x132
local CHAT_ENTRY_SIZE = 0xFC
local CHAT_ENTRY_COUNT = 100
local READABLE_PAGE_PROTECT = {
	[0x02] = true, -- PAGE_READONLY
	[0x04] = true, -- PAGE_READWRITE
	[0x08] = true, -- PAGE_WRITECOPY
	[0x20] = true, -- PAGE_EXECUTE_READ
	[0x40] = true, -- PAGE_EXECUTE_READWRITE
	[0x80] = true, -- PAGE_EXECUTE_WRITECOPY
}

local function safe_memory_call(fn, ...)
	local ok, value = pcall(fn, ...)
	if not ok then
		return nil
	end
	return value
end

local function is_readable_memory(address, size)
	address = tonumber(address)
	size = tonumber(size)
	if not address or address <= 0 then
		return false
	end

	size = math.floor(size or 1)
	if size <= 0 then
		return false
	end

	local mbi = ffi.new("MEMORY_BASIC_INFORMATION[1]")
	local current = address
	local finish = address + size - 1

	while current <= finish do
		local queried = tonumber(ffi.C.VirtualQuery(ffi.cast("const void*", current), mbi, ffi.sizeof(mbi[0]))) or 0
		if queried == 0 then
			return false
		end

		local protect = tonumber(mbi[0].Protect) or 0
		if (tonumber(mbi[0].State) or 0) ~= MEM_COMMIT then
			return false
		end
		if bit.band(protect, PAGE_GUARD) ~= 0 or bit.band(protect, PAGE_NOACCESS) ~= 0 then
			return false
		end
		if not READABLE_PAGE_PROTECT[bit.band(protect, 0xFF)] then
			return false
		end

		local regionBase = tonumber(ffi.cast("uintptr_t", mbi[0].BaseAddress)) or 0
		local regionSize = tonumber(mbi[0].RegionSize) or 0
		if regionBase <= 0 or regionSize <= 0 then
			return false
		end

		current = regionBase + regionSize
	end

	return true
end

local function safe_memory_tostring(address, size)
	address = tonumber(address)
	size = tonumber(size)
	if not address or address <= 0 then
		return ""
	end

	size = math.floor(size or 0)
	if size <= 0 or not is_readable_memory(address, size) then
		return ""
	end

	local text = safe_memory_call(memory.tostring, address, size, false)
	if type(text) ~= "string" then
		return ""
	end

	local zeroPos = text:find("\0", 1, true)
	if zeroPos then
		text = text:sub(1, zeroPos - 1)
	end

	return text
end

local function safe_memory_uint8(address, unprotect)
	address = tonumber(address)
	if not address or address <= 0 or not is_readable_memory(address, 1) then
		return nil
	end
	return safe_memory_call(memory.getuint8, address, unprotect and true or false)
end

local function safe_memory_uint32(address, unprotect)
	address = tonumber(address)
	if not address or address <= 0 or not is_readable_memory(address, 4) then
		return nil
	end
	return safe_memory_call(memory.getuint32, address, unprotect and true or false)
end

local function safe_memory_int32(address, unprotect)
	address = tonumber(address)
	if not address or address <= 0 or not is_readable_memory(address, 4) then
		return nil
	end
	return safe_memory_call(memory.getint32, address, unprotect and true or false)
end

ffi.cdef[[
typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;
typedef signed short   int16_t;

typedef struct RakNetBitStreamCompat {
    uint32_t numberOfBitsUsed;
    uint32_t numberOfBitsAllocated;
    uint32_t readOffset;
    uint8_t* data;
    bool copyData;
} RakNetBitStreamCompat;
]]

function module.getRakClientInterface()
    local pCNetGame = ffi.cast("uintptr_t*", module.sampModule + module.main_offsets.SAMP_INFO_OFFSET[module.currentVersion])
    if pCNetGame == nil or pCNetGame[0] == 0 then
        return nil
    end

    local pRakClient = ffi.cast("uintptr_t*", pCNetGame[0] + module.main_offsets.rakclient_interface[module.currentVersion])
    if pRakClient == nil or pRakClient[0] == 0 then
        return nil
    end

    return pRakClient[0]
end

function module.callVirtualMethod(vt, prototype, method, ...)
    local virtualTable = ffi.cast("intptr_t**", vt)[0]
    return ffi.cast(prototype, virtualTable[method])(...)
end

function module.sendRpc(id, bsPtr)
    local rakClient = module.getRakClientInterface()
    if not rakClient then
        return false, L("samp.error.rakclient_not_found")
    end

    local pRakClient = ffi.cast("void*", rakClient)
    local pId = ffi.new("int[1]", id)

    local ok = module.callVirtualMethod(
        rakClient,
        "bool(__thiscall*)(void*, int*, void*, char, char, char, bool)",
        25,
        pRakClient,
        pId,
        bsPtr,
        1,   -- HIGH_PRIORITY
        9,   -- RELIABLE_ORDERED
        0,
        false
    )

    return ok ~= false
end

function writeUInt8(buf, pos, value)
    buf[pos] = bit.band(value, 0xFF)
    return pos + 1
end

function writeInt16LE(buf, pos, value)
    if value < 0 then
        value = 0x10000 + value
    end
    buf[pos] = bit.band(value, 0xFF)
    buf[pos + 1] = bit.band(bit.rshift(value, 8), 0xFF)
    return pos + 2
end

function module.sendDialogResponse(dialogId, button, listItem, inputText)
    inputText = tostring(inputText or "")
    listItem = listItem ~= nil and listItem or -1
    button = button == 1 and 1 or 0

    local textLen = #inputText
    if textLen > 255 then
        error(L("samp.error.input_text_too_long"))
    end

    -- RPC 62: int16 id, uint8 button, int16 listitem, uint8 len, bytes text
    local payloadSize = 2 + 1 + 2 + 1 + textLen
    local payload = ffi.new("uint8_t[?]", payloadSize)

    local pos = 0
    pos = writeInt16LE(payload, pos, dialogId)
    pos = writeUInt8(payload, pos, button)
    pos = writeInt16LE(payload, pos, listItem)
    pos = writeUInt8(payload, pos, textLen)

    if textLen > 0 then
        ffi.copy(payload + pos, inputText, textLen)
    end

    -- ВАЖНО: создаем МАССИВ ИЗ 1 ЭЛЕМЕНТА, чтобы получить нормальный указатель
    local bs = ffi.new("RakNetBitStreamCompat[1]")
    bs[0].numberOfBitsUsed = payloadSize * 8
    bs[0].numberOfBitsAllocated = payloadSize * 8
    bs[0].readOffset = 0
    bs[0].data = payload
    bs[0].copyData = false

    -- bs уже является pointer-like объектом, его можно передать как void*
    local ok, err = module.sendRpc(62, ffi.cast("void*", bs))
    if not ok then
        return false, err
    end

    return true
end
-- примеры:
-- sendDialogResponse(1430, 1, -1, "#Orc") -- INPUT / PASSWORD / MSGBOX
-- sendDialogResponse(1430, 0, -1, "")         -- Cancel / ESC
-- sendDialogResponse(500, 1, 2, "")           -- LIST, выбран 3-й пункт

function module.is_chat_opened()
	local opend_chat = memory.getint32(
		memory.getint32(module.sampModule + module.main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET[module.currentVersion], false) + 0x14E0,
		false
	)
	return opend_chat == 1 and true or false
end

function module.Local_ID()
	return memory.getint16(
		module.PedPool() + module.main_offsets.SAMP_SLOCALPLAYERID_OFFSET[module.currentVersion],
		false
	)
end

function module.getPedID(handle)
	if handle == PLAYER_PED then
		return true, module.Local_ID()
	end
	local id = ID_Find(module.PedPool(), getCharPointer(handle))
	if id ~= 65535 then
		return true, id
	end
	return false, -1
end

function module.getChatMode()
	local pChat = memory.getint32(module.sampModule + module.main_offsets.pChat[module.currentVersion], true)
	return memory.getint8(pChat + 0x8, true)
end

function module.SetPageSize(pageSize)
	pageSize = tonumber(pageSize)
	if not pageSize then
		return false, "invalid_value"
	end

	pageSize = math.floor(pageSize)
	local minSize = 10
	local maxSize = 20
	local maxOffset = module.main_offsets.PageSize_MAX[module.currentVersion]
	if maxOffset then
		local readMax = memory.getint8(module.sampModule + maxOffset, false)
		if readMax and readMax > 0 then
			maxSize = readMax
		end
	end

	if pageSize < minSize or pageSize > maxSize then
		return false, "range", minSize, maxSize
	end

	local pChatOffset = module.main_offsets.pChat[module.currentVersion]
	if not pChatOffset then
		return false, "chat_unavailable"
	end

	local pChat = memory.getint32(module.sampModule + pChatOffset, true)
	if not pChat or pChat == 0 then
		return false, "chat_unavailable"
	end

	SetPageSizeFunc(pChat, pageSize)
	return true, pageSize
end

function module.GetHealthAndArmour(id)
	local fHP, fARM = 100, 100
	if id ~= module.Local_ID() then
		local dwRemoteplayer = memory.getint32(
			module.PedPool() + module.main_offsets.SAMP_PREMOTEPLAYER_OFFSET[module.currentVersion] + id * 4,
			true
		)
		if dwRemoteplayer ~= nil then
			local dw_remoteplayer_data = memory.getuint32(
				dwRemoteplayer + module.main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET[module.currentVersion],
				true
			)
			if dw_remoteplayer_data ~= nil then
				fHP = memory.getfloat(
					dw_remoteplayer_data
						+ module.main_offsets.SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET[module.currentVersion],
					true
				)
				fARM = memory.getfloat(
					dw_remoteplayer_data + module.main_offsets.SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET[module.currentVersion],
					true
				)
			end
		end
	else
		fHP = memory.getfloat(memory.getuint32(0xB6F5F0) + 0x540, true)
		fARM = memory.getfloat(memory.getuint32(0xB6F5F0) + 0x548, true)
	end
	return fHP, fARM
end

local function send_chat_internal(text, already_decoded)
	text = tostring(text or "")
	if text == "" or text:len() <= 0 then
		return false
	end
	if not already_decoded then
		text = u8:decode(text)
	end
	if tags and tags.change_tags then
		text = tags.change_tags(text)
	end
	if text:find("^/") then
		local pInput = memory.getuint32(module.sampModule + module.main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET[module.currentVersion], true)
		ffi.cast(
			"void (__thiscall*)(uintptr_t, const char*)",
			module.sampModule + module.main_offsets.CInput_Send[module.currentVersion]
		)(pInput, text)
	else
		ffi.cast(
			"void (__thiscall*)(uintptr_t, const char*)",
			module.sampModule + module.main_offsets.CInput_SendSay[module.currentVersion]
		)(getCharPointer(PLAYER_PED), text)
	end
	return true
end

function module.send_chat(text, already_decoded)
	return send_chat_internal(text, already_decoded)
end

function module.memoryAddMessageSamp(szText, ulColor)
	if szText == "" or szText == nil or szText:len() <= 0 then
		szText = "nil"
	end
	if tags and tags.change_tags then
		szText = tags.change_tags(szText)
	end
	local pChat, szText, ulColor =
		memory.getint32(module.sampModule + module.main_offsets.pChat[module.currentVersion], true),
		tostring(szText),
		tonumber(ulColor)
	return ffi.cast(
		"void(__thiscall*)(uintptr_t, unsigned long, const char*)",
		module.sampModule + module.main_offsets.AddChatMessage[module.currentVersion]
	)(pChat, ulColor, szText)
end

local function set_chat_input_text_internal(text, bool, already_decoded)
	text = tostring(text or "")
	if not already_decoded then
		text = u8:decode(text)
	end
	if tags and tags.change_tags then
		text = tags.change_tags(text)
	end
	local pEditBox = module.SAMP_CHAT_INPUT_INFO_OFFSET_func_test()
	if not pEditBox or pEditBox == 0 then
		return false
	end
	setEditboxText(pEditBox, ffi.cast("char*", text), 0)
	if bool then
		module.pCInput_Open_Close(bool)
	end
	return true
end

function module.pGetChatString(index)
	index = tonumber(index)
	if not index then
		return "", "", 0, 0
	end

	index = math.floor(index)
	if index < 0 or index >= CHAT_ENTRY_COUNT then
		return "", "", 0, 0
	end

	local version = module.currentVersion
	if not version or version == "UNKNOWN" then
		return "", "", 0, 0
	end

	local pChatOffset = module.main_offsets.pChat[version]
	local textOffset = module.main_offsets.CHAT_TEXT_OFFSET[version]
	local prefixOffset = module.main_offsets.CHAT_PREFIX_TEXT_OFFSET[version]
	local colorOffset = module.main_offsets.CHAT_COLOR_OFFSET[version]
	local prefixColorOffset = module.main_offsets.CHAT_PREFIX_COLOR_OFFSET[version]
	if not pChatOffset or not textOffset or not prefixOffset or not colorOffset or not prefixColorOffset then
		return "", "", 0, 0
	end

	local prefixSize = textOffset - prefixOffset
	local textSize = colorOffset - textOffset
	if prefixSize <= 0 or textSize <= 0 then
		return "", "", 0, 0
	end

	if not module.sampModule or module.sampModule <= 0 then
		return "", "", 0, 0
	end

	local pChat = safe_memory_int32(module.sampModule + pChatOffset, true)
	if not pChat or pChat == 0 then
		return "", "", 0, 0
	end

	local entry = pChat + CHAT_ENTRY_BASE_OFFSET + (index * CHAT_ENTRY_SIZE)
	if not is_readable_memory(entry, CHAT_ENTRY_SIZE) then
		return "", "", 0, 0
	end

	local text = safe_memory_tostring(entry + textOffset, textSize)
	local prefix = safe_memory_tostring(entry + prefixOffset, prefixSize)
	local color = safe_memory_uint32(entry + colorOffset, false) or 0

	local prefixColorAddr = entry + prefixColorOffset
	local prefixColorFlag = safe_memory_uint8(prefixColorAddr, false) or 0

	local prefixColor = 0
	if prefixColorFlag > 0 then
		prefixColor = safe_memory_uint32(prefixColorAddr, false) or 0
	end

	return text, prefix, color, prefixColor
end

function module.Set_ChatInputText(text, bool, already_decoded)
	return set_chat_input_text_internal(text, bool, already_decoded)
end

local function build_custom_samp_globals()
	return {
		sampAddChatMessage = function(text, color)
			return module.memoryAddMessageSamp(text, color)
		end,
		sampGetChatInputText = function()
			local ok, result = pcall(module.sampGetChatEditboxText)
			if ok and type(result) == "string" then
				return result
			end
			return ""
		end,
		sampGetChatString = function(index)
			return module.pGetChatString(index)
		end,
		sampIsChatInputActive = function()
			return module.is_chat_opened()
		end,
		sampIsDialogActive = function()
			return module.isDialogActive()
		end,
		sampProcessChatInput = function(text)
			return module.send_chat(text, true)
		end,
		sampSendChat = function(text)
			return module.send_chat(text, true)
		end,
		sampSendDialogResponse = function(dialogId, button, listItem, inputText)
			return module.sendDialogResponse(dialogId, button, listItem, inputText)
		end,
		sampSetChatInputEnabled = function(bool)
			return module.pCInput_Open_Close(bool and true or false)
		end,
		sampSetChatInputText = function(text)
			return module.Set_ChatInputText(text, nil, true)
		end,
	}
end

function module.restoreOriginalFunctionGlobals()
	for i = 1, #SAMP_GLOBAL_NAMES do
		local name = SAMP_GLOBAL_NAMES[i]
		local original = get_original_samp_global(name)
		active_samp_global_sources[name] = original and "native" or "missing"
		if original ~= nil then
			_G[name] = original
		else
			_G[name] = nil
		end
		managed_samp_globals[name] = nil
	end
	module._function_backend_active = module.BACKEND_STANDARD
	return true
end

function module.applyFunctionBackend(mode)
	mode = normalize_backend_mode(mode or module._function_backend_mode)
	module._function_backend_mode = mode

	local custom_globals = build_custom_samp_globals()
	local native_selected = false
	local arizona_native_ready = true

	for i = 1, #SAMP_GLOBAL_NAMES do
		local name = SAMP_GLOBAL_NAMES[i]
		local original = get_original_samp_global(name)
		local custom = custom_globals[name]
		local selected = nil
		local source = "missing"
		local prefer_native = should_prefer_native_samp_global(mode, name)

		if prefer_native and type(original) == "function" then
			selected = original
			source = "native"
			native_selected = true
		elseif type(custom) == "function" then
			selected = custom
			source = "custom"
		elseif type(original) == "function" then
			selected = original
			source = "native"
			native_selected = true
		end

		if mode == module.BACKEND_ARIZONA and ARIZONA_NATIVE_PRIORITY_GLOBALS[name] and source ~= "native" then
			arizona_native_ready = false
		end

		active_samp_global_sources[name] = source
		managed_samp_globals[name] = selected
		if selected ~= nil then
			_G[name] = selected
		else
			_G[name] = nil
		end
	end

	if mode == module.BACKEND_SAMPFUNCS and native_selected then
		module._function_backend_active = module.BACKEND_SAMPFUNCS
	elseif mode == module.BACKEND_ARIZONA and arizona_native_ready then
		module._function_backend_active = module.BACKEND_ARIZONA
	else
		module._function_backend_active = module.BACKEND_STANDARD
	end

	return true, module._function_backend_active
end

function module.setFunctionBackendMode(mode)
	return module.applyFunctionBackend(mode)
end

function module.getFunctionBackendMode()
	return module._function_backend_mode
end

function module.getFunctionBackendStatus()
	local status = {
		desired = module._function_backend_mode,
		active = module._function_backend_active,
		hasSampfuncs = module.hasSampfuncs(),
		globals = {},
		requiredNativeGlobals = {},
		missingRequiredNativeGlobals = {},
	}
	for i = 1, #SAMP_GLOBAL_NAMES do
		local name = SAMP_GLOBAL_NAMES[i]
		status.globals[name] = active_samp_global_sources[name] or "missing"
	end
	if module._function_backend_mode == module.BACKEND_ARIZONA then
		for i = 1, #ARIZONA_NATIVE_PRIORITY_NAMES do
			local name = ARIZONA_NATIVE_PRIORITY_NAMES[i]
			status.requiredNativeGlobals[#status.requiredNativeGlobals + 1] = name
			if status.globals[name] ~= "native" then
				status.missingRequiredNativeGlobals[#status.missingRequiredNativeGlobals + 1] = name
			end
		end
	end
	return status
end

module.setBackendMode = module.setFunctionBackendMode
module.getBackendMode = module.getFunctionBackendMode
module.getBackendStatus = module.getFunctionBackendStatus

function module.installSampfuncsCompat(mode)
	return module.applyFunctionBackend(mode)
end

function module.onTerminate()
	module.restoreOriginalFunctionGlobals()
end

--Требуется сампфункс, работа с cef и телефоном аризоны
function module.arizonaOpenPhoneApp(appId)
	local action = ("launchedApp|%s"):format(appId)
	local bs = raknetNewBitStream()
	raknetBitStreamWriteInt8(bs, 220)
	raknetBitStreamWriteInt8(bs, 18)
	raknetBitStreamWriteInt32(bs, #action)
	raknetBitStreamWriteString(bs, action)
	raknetBitStreamWriteInt32(bs, 0)
	raknetSendBitStreamEx(bs, 1, 7, 1)
	raknetDeleteBitStream(bs)
end

module.applyFunctionBackend(module._function_backend_mode)

return module
