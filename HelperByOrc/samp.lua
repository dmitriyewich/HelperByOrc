local module = {}
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ffi = require("ffi")
local memory = require("memory")
local tags

function module.attachModules(mod)
    tags = mod.tags
end

module.currentVersion, module.sampModule = nil, getModuleHandle("samp.dll")

local entryPoint = {
    [0x2E2BB7] = { "E", false },
    [0x31DF13] = { "R1", false },
    [0x3195DD] = { "R2", false },
    [0xCC490] = { "R3", false },
    [0xCC4D0] = { "R3-1", true },
    [0xCBCD0] = { "R4", false },
    [0xCBCB0] = { "R4-2", false },
    [0xCBC90] = { "R5", false },
    [0xFDB60] = { "DL-R1", false },
}

module.main_offsets = {
    ["SAMP_INFO_OFFSET"] = {
        ["R1"] = 0x21A0F8,
        ["R2"] = 0x21A100,
        ["R3-1"] = 0x26E8DC,
        ["R4"] = 0x26EA0C,
        ["R4-2"] = 0x26EA0C,
        ["R5"] = 0x26EB94,
        ["DL-R1"] = 0x2ACA24,
    },
    ["SAMP_DIALOG_INFO_OFFSET"] = {
        ["R1"] = 0x21A0B8,
        ["R2"] = 0x21A0C0,
        ["R3-1"] = 0x26E898,
        ["R4"] = 0x26E9C8,
        ["R4-2"] = 0x26E9C8,
        ["R5"] = 0x26EB50,
        ["DL-R1"] = 0x2AC9E0,
    },
    ["SAMP_DIALOG_ACTIVE_OFFSET"] = {
        ["R1"] = 0x28,
        ["R2"] = 0x28,
        ["R3"] = 0x28,
        ["R3-1"] = 0x28,
        ["R4"] = 0x28,
        ["R4-2"] = 0x28,
        ["R5"] = 0x28,
        ["DL-R1"] = 0x28,
    },
    ["SAMP_DIALOG_ID_OFFSET"] = {
        ["R1"] = 0x30,
        ["R2"] = 0x30,
        ["R3"] = 0x30,
        ["R3-1"] = 0x30,
        ["R4"] = 0x30,
        ["R4-2"] = 0x30,
        ["R5"] = 0x30,
        ["DL-R1"] = 0x30,
    },
    ["SAMP_DIALOG_TEXT_OFFSET"] = {
        ["R1"] = 0x34,
        ["R2"] = 0x34,
        ["R3"] = 0x34,
        ["R3-1"] = 0x34,
        ["R4"] = 0x34,
        ["R4-2"] = 0x34,
        ["R5"] = 0x34,
        ["DL-R1"] = 0x34,
    },
    ["CDialog_Show"] = {
        ["R1"] = 0x6B9C0,
        ["R2"] = 0x6BA70,
        ["R3"] = 0x6F8C0,
        ["R3-1"] = 0x6F8C0,
        ["R4"] = 0x6FFE0,
        ["R4-2"] = 0x70010,
        ["R5"] = 0x6FFB0,
        ["DL-R1"] = 0x6FA50,
    },
    ["CDialog_Close"] = {
        ["R1"] = 0x6C040,
        ["R2"] = 0x6C0F0,
        ["R3"] = 0x6FF40,
        ["R3-1"] = 0x6FF40,
        ["R4"] = 0x70660,
        ["R4-2"] = 0x70690,
        ["R5"] = 0x70630,
        ["DL-R1"] = 0x700D0,
    },
    ["SAMP_DIALOG_CAPTION_OFFSET"] = {
        ["R1"] = 0x40,
        ["R2"] = 0x40,
        ["R3"] = 0x40,
        ["R3-1"] = 0x40,
        ["R4"] = 0x40,
        ["R4-2"] = 0x40,
        ["R5"] = 0x40,
        ["DL-R1"] = 0x40,
    },
    ["CDXUTEditBox_GetText"] = {
        ["R1"] = 0x81030,
        ["R2"] = 0x810D0,
        ["R3"] = 0x84F40,
        ["R3-1"] = 0x84F40,
        ["R4"] = 0x85680,
        ["R4-2"] = 0x856B0,
        ["R5"] = 0x85650,
        ["DL-R1"] = 0x850D0,
    },
    ["CDXUTEditBox_SetText"] = {
        ["R1"] = 0x80F60,
        ["R2"] = 0x81000,
        ["R3"] = 0x84E70,
        ["R3-1"] = 0x84E70,
        ["R4"] = 0x855B0,
        ["R4-2"] = 0x855E0,
        ["R5"] = 0x85580,
        ["DL-R1"] = 0x85000,
    },
    ["pEditBox"] = { ["R1"] = 0x24, ["R3-1"] = 0x24, ["R5"] = 0x24, ["DL-R1"] = 0x24 },
    ["CDXUTDialog"] = { ["R1"] = 0x1C, ["R3-1"] = 0x1C, ["R5"] = 0x1C, ["DL-R1"] = 0x1C },
    ["SetCursorMode"] = { ["R1"] = 0x9BD30, ["R3-1"] = 0x9FFE0, ["R5"] = 0xA06F0, ["DL-R1"] = 0xA0530 },
    ["RefGame"] = { ["R1"] = 0x21A10C, ["R3-1"] = 0x26E8F4, ["R5"] = 0x26EBAC, ["DL-R1"] = 0x2ACA3C },
    ["AddEntry"] = {
        ["R1"] = 0x64010,
        ["R2"] = 0x640E0,
        ["R3-1"] = 0x67460,
        ["R4"] = 0x67BA0,
        ["R4-2"] = 0x67BE0,
        ["DL-R1"] = 0x067650,
    },
    ["RenderEntry"] = {
        ["R1"] = 0x638A0,
        ["R2"] = 0x640E0,
        ["R3-1"] = 0x66CF0,
        ["R4"] = 0x67BA0,
        ["R5"] = 0x68380,
        ["DL-R1"] = 0x66EE0,
    },
    ["pChat"] = {
        ["R1"] = 0x21A0E4,
        ["R2"] = 0x21A0EC,
        ["R3-1"] = 0x26E8C8,
        ["R4"] = 0x26E9F8,
        ["R4-2"] = 0x26E9F8,
        ["DL-R1"] = 0x2ACA10,
    },
    ["AddChatMessage"] = {
        ["R1"] = 0x645A0,
        ["R2"] = 0x64670,
        ["R3-1"] = 0x679F0,
        ["R4"] = 0x68130,
        ["R4-2"] = 0x68070,
        ["DL-R1"] = 0x67650,
    },

    ["CInput"] = { ["R1"] = 0x21A0E8, ["R3-1"] = 0x26E8CC, ["R5"] = 0x26EB84, ["DL-R1"] = 0x2ACA14 },
    ["CInput_Opened"] = { ["R1"] = 0x14E0, ["R3-1"] = 0x14E0, ["R5"] = 0x14E0, ["DL-R1"] = 0x14E0 },

    ["OnResetDevice"] = {
        ["R1"] = 0x64600,
        ["R2"] = 0x646D0,
        ["R3-1"] = 0x67A50,
        ["R4"] = 0x68190,
        ["R4-2"] = 0x681D0,
        ["DL-R1"] = 0x67C40,
    },

    ["CInput_Open"] = { ["R1"] = 0x657E0, ["R3-1"] = 0x68D10, ["R5"] = 0x69480, ["DL-R1"] = 0x68EC0 },
    ["CInput_Close"] = { ["R1"] = 0x658E0, ["R3-1"] = 0x68E10, ["R5"] = 0x69580, ["DL-R1"] = 0x68FC0 },
    ["CInput_Close_fix"] = { ["R1"] = 0x6B9FB, ["R3-1"] = 0x6F8FB, ["R5"] = 0x69580, ["DL-R1"] = 0x68FC0 },

    ["SetPageSize"] = { ["R1"] = 0x636D0, ["R3-1"] = 0x66B20, ["R5"] = 0x672A0, ["DL-R1"] = 0x66D10 },
    ["PageSize_MAX"] = { ["R1"] = 0x64A51, ["R3-1"] = 0x67EB1, ["R5"] = 0x672A0, ["DL-R1"] = 0x66D10 },
    ["PageSize_StringInfo"] = { ["R1"] = 0xD7AD5, ["R3-1"] = 0xE9DB5, ["R5"] = 0x672A0, ["DL-R1"] = 0x66D10 },

    ["CInput_Send"] = {
        ["R1"] = 0x65C60,
        ["R2"] = 0x65D30,
        ["R3-1"] = 0x69190,
        ["R4"] = 0x698C0,
        ["R4-2"] = 0x698C0,
        ["DL-R1"] = 0x69340,
    },
    ["CInput_SendSay"] = {
        ["R1"] = 0x57F0,
        ["R2"] = 0x57e0,
        ["R3-1"] = 0x5820,
        ["R4"] = 0x5A00,
        ["R4-2"] = 0x5A00,
        ["DL-R1"] = 0x5860,
    },
    ["GetName"] = { ["R1"] = 0x13CE0, ["R3-1"] = 0x16F00, ["R5"] = 0x175C0, ["DL-R1"] = 0x170D0 },
    ["SAMP_SLOCALPLAYERID_OFFSET"] = { ["R1"] = 0x4, ["R3-1"] = 0x2F1C, ["R5"] = 0x4, ["DL-R1"] = 0x0 },
    ["SAMP_INFO_OFFSET_Pools"] = { ["R1"] = 0x3CD, ["R3-1"] = 0x3DE, ["R5"] = 0x3DE, ["DL-R1"] = 0x3DE },
    ["SAMP_INFO_OFFSET_Pools_Player"] = { ["R1"] = 0x18, ["R3-1"] = 0x8, ["R5"] = 0x4, ["DL-R1"] = 0x8 },
    ["SAMP_INFO_OFFSET_Pools_Veh"] = { ["R1"] = 0x1C, ["R3-1"] = 0xC, ["R5"] = 0x0, ["DL-R1"] = 0xC },
    ["SAMP_COLOR_OFFSET"] = { ["R1"] = 0x216378, ["R3-1"] = 0x151578, ["R5"] = 0x151828, ["DL-R1"] = 0x18F6C0 },
    ["ID_Find"] = { ["R1"] = 0x10420, ["R3-1"] = 0x13570, ["R5"] = 0x138C0, ["DL-R1"] = 0x137C0 },
    ["CPlayerPool_IsConnected"] = { ["R1"] = 0x10B0, ["R3-1"] = 0x10B0, ["R5"] = 0x10B0, ["DL-R1"] = 0x10B0 },
    ["IDcar_Find"] = { ["R1"] = 0x1B0A0, ["R3-1"] = 0x1E440, ["R5"] = 0x1EB90, ["DL-R1"] = 0x1E650 },
    ["SAMP_PREMOTEPLAYER_OFFSET"] = { ["R1"] = 0x2E, ["R3-1"] = 0x4, ["R5"] = 0x69900, ["DL-R1"] = 0x26 },
    ["SAMP_REMOTEPLAYERDATA_OFFSET"] = { ["R1"] = 0x0, ["R3-1"] = 0x0, ["R5"] = 0x69900, ["DL-R1"] = 0x8 },
    ["pSAMP_Actor"] = { ["R1"] = 0x0, ["R3-1"] = 0x0, ["R5"] = 0x0, ["DL-R1"] = 0x0 },
    ["pGTA_Ped"] = { ["R1"] = 0x2a4, ["R3-1"] = 0x2a4, ["R5"] = 0x2a4, ["DL-R1"] = 0x2a4 },
    ["IsConnected"] = { ["R1"] = 0x10B0, ["R3-1"] = 0x10B0, ["R5"] = 0x10B0, ["DL-R1"] = 0x10B0 },

    ["SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET"] = {
        ["R1"] = 0x1BC,
        ["R2"] = 0x1BC,
        ["R3-1"] = 0x1B0,
        ["R4"] = 0x1B0,
        ["R4-2"] = 0x1B0,
        ["DL-R1"] = 0x1B0,
    },
    ["SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET"] = {
        ["R1"] = 0x1B8,
        ["R2"] = 0x1AC,
        ["R3-1"] = 0x1AC,
        ["R4"] = 0x1AC,
        ["R4-2"] = 0x1AC,
        ["DL-R1"] = 0x1AC,
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
            print("Samp version " .. module.currentVersion .. " is not supported")
            thisScript():unload()
        end
        if not module.currentVersion then
            assert(entryPoint[ep], ("Unknown version of SA-MP (Entry point: 0x%X)"):format(ep))
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

local ID_Find = ffi.cast(
    "int (__thiscall *)(intptr_t, intptr_t)",
    module.sampModule + module.main_offsets.ID_Find[module.currentVersion]
)

local CPlayerPool_IsConnected = ffi.cast(
    "bool (__thiscall *)(intptr_t, unsigned short)",
    module.sampModule + module.main_offsets.CPlayerPool_IsConnected[module.currentVersion]
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
    local id = tonumber(id)
    local nick = GetName(module.PedPool(), id)
    if nick ~= nil then
        return ffi.string(nick)
    else
        return "UNKNOWN"
    end
end

function module.IsConnected(id)
    id = tonumber(id)
    return id == module.Local_ID() or CPlayerPool_IsConnected(module.PedPool(), id)
end

-- print('313', module.IsConnected(313))
-- function module.IsConnected(id)
-- local on_off
-- local id = tonumber(id)
-- if id ~= module.Local_ID() then
-- on_off = CPlayerPool_IsConnected(module.PedPool(), id)
-- else
-- on_off = true
-- end
-- return on_off
-- end

-- function module.setScoreboardCursorPos(position) -- Указывать позицию нужно начиная с 0.
-- local pScoreboard = memory.getuint32(getModuleHandle("samp.dll") + 0x26E894, true)
-- local m_pListbox = memory.getuint32(pScoreboard + 0x38, true)
-- ffi.cast('void(__thiscall*)(void*, int)', getModuleHandle('samp.dll') + 0x8A2E0)(ffi.cast('void*', m_pListbox), position)
-- end

-- function setScoreboardCursorPos(position) -- Указывать позицию нужно начиная с 0.
-- local pScoreboard = memory.getuint32(getModuleHandle("samp.dll") + 0x26E894, true)
-- local m_pListbox = memory.getuint32(pScoreboard + 0x38, true)
-- ffi.cast('void(__thiscall*)(void*, int)', getModuleHandle('samp.dll') + 0x8A2E0)(ffi.cast('void*', m_pListbox), position)
-- end

-- Адреса для всех версий:
-- Lua:
-- Меняете 0x26E894 на:
-- R1: 0x21A188
-- R2: 0x21A190
-- R3: 0x26E894
-- R4: 0x26EAA0
-- R5: 0x26EC28
-- DL: 0x2ACAB8

-- Меняете 0x8A2E0 на:
-- R1: 0x863C0
-- R2: 0x86460
-- R3: 0x8A2E0
-- R4: 0x8AA20
-- R5: 0x8A9F0
-- DL: 0x8A470

-- Описание: Возвращает адрес RakClientInterface
-- Код:
-- Lua:
-- function getRakClientInterface()
-- local pCNetGame = ffi.cast("uintptr_t*", (samp + 0x21A0F8))
-- local pRakClient = ffi.cast("uintptr_t*", (pCNetGame[0] + 0x3C9))
-- return pRakClient[0]
-- end
-- Пример использования:
-- Lua:
-- local ffi = require("ffi")

-- function callVirtualMethod(vt, prototype, method, ...)
-- local virtualTable = ffi.cast("intptr_t**", vt)[0]
-- return ffi.cast(prototype, virtualTable[method])(...)
-- end

-- function sendRpc(id, bs)
-- local rakClient = getRakClientInterface()
-- local pRakClient = ffi.cast("void*", rakClient)
-- local pId = ffi.new("int[1]", id)
-- callVirtualMethod(
-- rakClient,
-- "bool(__thiscall*)(void*, int*, uintptr_t, char, char, char, bool)", 25,
-- pRakClient, pId, bs, 1, 9, 0, false
-- )
-- end

-- --[[
-- кароооче, функция спавна, которой не нужен сампфункс и прочие приблуды,
-- чистый мунлоадер, никаких анальных утех
-- (кроме тех, которые испытал я, когда писал этот пример)
-- ]]
-- function sendSpawn()
-- sendRpc(52, 0)
-- end

ffi.cdef([[
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

function module.CInput_func()
    return memory.read(module.sampModule + module.main_offsets.CInput[module.currentVersion], 4, true)
end

SetCursorMode = ffi.cast(
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
        callMethod(
            module.sampModule + module.main_offsets.CDialog_Close[module.currentVersion],
            module.pDialog_func(),
            1,
            0,
            int
        )
    end
end

function module.pEditBox_func()
    return memory.read(module.pDialog_func() + module.main_offsets.pEditBox[module.currentVersion], 4, true)
end

function module.pEditBox_active_func()
    return memory.read(module.pEditBox_func() + 0x4, 1, true) == 1 and true or false
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
        memory.setint8(module.pEditBox_func() + 0x11E, cursor, false)
        memory.setint8(module.pEditBox_func() + 0x119, cursor, false)
    end
end

function module.sampGetDialogInputCursor()
    if module.isDialogActive() and module.pEditBox_active_func() then
        pos1 = memory.getint8(module.pEditBox_func() + 0x11E, false) -- Начало выделения
        pos2 = memory.getint8(module.pEditBox_func() + 0x119, false) -- Конец выделенного текста.
        return pos1, pos2
    end
end

function module.pCInput_func()
    return memory.read(module.sampModule + module.main_offsets.CInput[module.currentVersion], 4, true)
end

function module.pCInput_func_test()
    return memory.read(module.pCInput_func() + 0x8, 4, true)
end

function module.pCInput_Open_Close(bool)
    local adress = bool and module.main_offsets.CInput_Open[module.currentVersion]
        or module.main_offsets.CInput_Close[module.currentVersion]
    callMethod(module.sampModule + adress, module.pCInput_func(), 0, 0)
end

function module.sampSetChatInputCursor(cursor)
    -- pCInput_Open_Close(true)
    -- if is_chat_opened()() then
    memory.setint8(module.pCInput_func_test() + 0x11E, cursor, false)
    memory.setint8(module.pCInput_func_test() + 0x119, cursor, false)
    -- end
end

-- end

-- function module.getSelectedText()
-- local input = sampGetChatInputText()
-- local ptr = sampGetInputInfoPtr()
-- local chat = getStructElement(ptr, 0x8, 4)
-- local pos1 = readMemory(chat + 0x11E, 4, false)
-- local pos2 = readMemory(chat + 0x119, 4, false)
-- local count = pos2 - pos1
-- return string.sub(input, count < 0 and pos2 + 1 or pos1 + 1, count < 0 and pos2 - count or pos2)
-- end

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
    if module.isDialogActive() and module.pEditBox_active_func() then
        return ffi.string(getEditboxText(module.pEditBox_func()))
    end
    return ""
end

function module.sampSetDialogEditboxText(text)
    if module.isDialogActive() and module.pEditBox_active_func() then
        return setEditboxText(module.pEditBox_func(), ffi.cast("char*", text), 0)
    end
end

function module.SetCurrentDialogListItem(int)
    if module.isDialogActive() then
        local SAMP_DIALOG_PTR2_OFFSET = memory.read(module.pDialog_func() + 0x20, 4, true)
        callMethod(module.sampModule + 0x8A2E0, SAMP_DIALOG_PTR2_OFFSET, 1, 0, int)
    end
end

function module.GetCurrentDialogListItem()
    if module.isDialogActive() then
        local SAMP_DIALOG_PTR2_OFFSET = memory.read(module.pDialog_func() + 0x20, 4, true)
        return callMethod(module.sampModule + 0x88760, SAMP_DIALOG_PTR2_OFFSET, 1, 0, -1)
    end
end

function module.GetCurrentDialogStyle()
    if module.isDialogActive() then
        return memory.getint32(module.pDialog_func() + 0x2C, false)
    end
end

function module.GetCurrentDialogListboxItemsCount()
    if module.isDialogActive() then
        local SAMP_DIALOG_PTR2_OFFSET = memory.read(module.pDialog_func() + 0x20, 4, true)
        local SAMP_DIALOG_LINECOUNT_OFFSET = memory.read(SAMP_DIALOG_PTR2_OFFSET + 0x150, 4, true)
        return SAMP_DIALOG_LINECOUNT_OFFSET
    end
end

function module.is_chat_opened()
    local opend_chat = memory.getint32(
        memory.getint32(module.sampModule + module.main_offsets.CInput[module.currentVersion], false) + 0x14E0,
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

function module.send_chat(text)
    if text == "" or text == nil or text:len() <= 0 then
        return
    end
    local text = u8:decode(text)
    local text = tags.change_tags(text)
    if text:find("^/") then
        local pInput = memory.getuint32(module.sampModule + module.main_offsets.CInput[module.currentVersion], true)
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
end

function module.memoryAddMessageSamp(szText, ulColor)
    if szText == "" or szText == nil or szText:len() <= 0 then
        szText = "nil"
    end
    local szText = tags.change_tags(szText)
    local pChat, szText, ulColor =
        memory.getint32(module.sampModule + module.main_offsets.pChat[module.currentVersion], true),
        tostring(szText),
        tonumber(ulColor)
    return ffi.cast(
        "void(__thiscall*)(uintptr_t, unsigned long, const char*)",
        module.sampModule + module.main_offsets.AddChatMessage[module.currentVersion]
    )(pChat, ulColor, szText)
end

function module.Set_ChatInputText(text, bool)
    local text = u8:decode(text)
    local text = tags.change_tags(text)
    sampSetChatInputText(text)
    if bool then
        sampSetChatInputEnabled(bool)
    end
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

-- Описание: Включает/отключает отрисовку хп и армора над игроком. Спасибо за помощь @AdCKuY_DpO4uLa
-- Код:
-- Lua:
-- local memory = require("memory")

-- function toggleHealthBar(toggle, sampVersion)
-- local offsets = {
-- ["DLR1"] = 0x73CB0,
-- ["R1"] = 0x6FC30,
-- ["R2"] = 0x6FCD0,
-- ["R3"] = 0x73B20,
-- ["R4"] = 0x74240,
-- ["R5"] = 0x74210,
-- }
-- local samp = getModuleHandle("samp.dll")
-- local address = (samp + offsets[sampVersion]
-- local byte = (toggle and 0x55 or 0xC3)
-- memory.write(address, byte, 1, true)
-- end

return module
