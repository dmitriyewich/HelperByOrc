--[[
    AUTHOR: RTD/RutreD(https://www.blast.hk/members/126461/)
    Refactor/safety: (твоя версия с улучшениями)

    API НЕ ЛОМАЛ:
      return { vmt = vmt_hook, jmp = jmp_hook, call = call_hook }
      vmt.new(vt)
      jmp.new(cast, callback, hook_addr, size, trampoline, org_bytes_tramp)
      call.new(cast, callback, hook_addr)

    Добавил:
      jmp_hook объект: destroy()
      call_hook объект: destroy()
]]

local ffi = require "ffi"

ffi.cdef[[
    typedef unsigned long DWORD;
    typedef unsigned long SIZE_T;
    typedef int BOOL;
    typedef void* LPVOID;

    BOOL  VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, DWORD* lpflOldProtect);
    LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
    BOOL  VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);
]]

-- WinAPI constants (как у тебя, только вынес)
local PAGE_READWRITE           = 0x04  -- :contentReference[oaicite:2]{index=2}
local PAGE_EXECUTE_READWRITE   = 0x40  -- :contentReference[oaicite:3]{index=3}
local MEM_COMMIT               = 0x1000
local MEM_RELEASE              = 0x8000 -- :contentReference[oaicite:4]{index=4}

local PTR_SIZE = ffi.sizeof("intptr_t")
local IS_32BIT = (ffi.sizeof("void*") == 4)

-- В GTA SA / MoonLoader это 32-bit. Тут явная защита от “случайно запустили не там”.
if not IS_32BIT then
    error("[hooks] This hooks library is intended for 32-bit process (GTA SA).", 2)
end

-- ------------------------------------------------------------
-- utils
-- ------------------------------------------------------------

local function copy(dst, src, len)
    return ffi.copy(ffi.cast("void*", dst), ffi.cast("const void*", src), len)
end

local function to_uintptr(addr)
    -- Принимаем number или cdata pointer
    if type(addr) == "number" then
        return addr
    end
    return tonumber(ffi.cast("intptr_t", addr))
end

local function safe_jit_off(fn)
    if jit and jit.off and type(fn) == "function" then
        -- Отключаем JIT для функции и подфункций (как у тебя). :contentReference[oaicite:5]{index=5}
        jit.off(fn, true)
    end
end

-- Список аллокаций для освобождения на terminate
local buff = { free = {} }

local function VirtualProtect(addr, size, newProtect, oldProtectArr)
    return ffi.C.VirtualProtect(ffi.cast("void*", addr), size, newProtect, oldProtectArr)
end

local function VirtualAlloc(size, protect, trackForFree)
    local p = ffi.C.VirtualAlloc(nil, size, MEM_COMMIT, protect)
    if p == nil then
        return nil
    end
    if trackForFree then
        table.insert(buff.free, p)
    end
    return p
end

local function VirtualFree(ptr)
    if ptr ~= nil then
        -- MEM_RELEASE требует dwSize = 0. :contentReference[oaicite:6]{index=6}
        ffi.C.VirtualFree(ptr, 0, MEM_RELEASE)
    end
end

-- Удобный “поменял защиту -> записал -> восстановил”
local function with_protect_write(addr, size, newProtect, fn)
    local old_prot = ffi.new("DWORD[1]")
    local ok = VirtualProtect(addr, size, newProtect, old_prot) ~= 0
    if not ok then
        return false, "VirtualProtect failed (set)"
    end

    local ok2, err = pcall(fn)

    -- восстановление защиты в любом случае
    VirtualProtect(addr, size, old_prot[0], old_prot)

    if not ok2 then
        return false, err
    end
    return true
end

local function warn(msg)
    -- print оставил, как в оригинале
    print("[WARNING] " .. msg)
end

-- ------------------------------------------------------------
-- VMT HOOKS
-- ------------------------------------------------------------

local vmt_hook = { hooks = {} }

function vmt_hook.new(vt)
    local new_hook = {}
    local org_func = {}

    -- vt ожидается как “this” (C++ object*), где [0] лежит vtable
    local virtual_table = ffi.cast("intptr_t**", vt)[0]
    new_hook.this = virtual_table

    -- hookMethod(cast, func, methodIndex) -> original_function_casted
    new_hook.hookMethod = function(cast, func, method)
        if type(method) ~= "number" then
            error("[vmt] method index must be a number", 2)
        end

        safe_jit_off(func)

        if org_func[method] ~= nil then
            -- Уже хукали этот метод. Поведение “молча перезаписать” опасное,
            -- поэтому возвращаем текущий original, но не ставим еще раз.
            return ffi.cast(cast, org_func[method])
        end

        org_func[method] = virtual_table[method]

        local slot_addr = to_uintptr(virtual_table + method)
        local function write()
            virtual_table[method] = ffi.cast("intptr_t", ffi.cast(cast, func))
        end

        local ok, err = with_protect_write(slot_addr, PTR_SIZE, PAGE_READWRITE, write)
        if not ok then
            org_func[method] = nil
            error("[vmt] hookMethod failed: " .. tostring(err), 2)
        end

        return ffi.cast(cast, org_func[method])
    end

    -- unHookMethod(methodIndex)
    -- ВАЖНО: я сохранил твою логику с trampoline в vtable (не прямой restore).
    new_hook.unHookMethod = function(method)
        if type(method) ~= "number" then
            return false
        end
        local original_ptr = org_func[method]
        if original_ptr == nil then
            return false
        end

        -- Делаем маленький trampoline: JMP original
        local tramp_mem = VirtualAlloc(5, PAGE_EXECUTE_READWRITE, true)
        if tramp_mem == nil then
            error("[vmt] VirtualAlloc failed for trampoline", 2)
        end

        local alloc_addr = to_uintptr(tramp_mem)
        local trampoline_bytes = ffi.new("uint8_t[5]", 0x90)
        trampoline_bytes[0] = 0xE9
        -- rel32 = target - (src + 5)
        ffi.cast("int32_t*", trampoline_bytes + 1)[0] = original_ptr - alloc_addr - 5

        copy(alloc_addr, trampoline_bytes, 5)

        local slot_addr = to_uintptr(virtual_table + method)
        local function write()
            virtual_table[method] = ffi.cast("intptr_t", alloc_addr)
        end

        local ok, err = with_protect_write(slot_addr, PTR_SIZE, PAGE_READWRITE, write)
        if not ok then
            error("[vmt] unHookMethod failed: " .. tostring(err), 2)
        end

        org_func[method] = nil
        return true
    end

    -- unHookAll()
    new_hook.unHookAll = function()
        -- НЕЛЬЗЯ мутировать таблицу org_func во время pairs.
        local methods = {}
        for method, _ in pairs(org_func) do
            methods[#methods + 1] = method
        end
        for i = 1, #methods do
            new_hook.unHookMethod(methods[i])
        end
    end

    table.insert(vmt_hook.hooks, new_hook.unHookAll)
    return new_hook
end

-- ------------------------------------------------------------
-- JMP HOOKS
-- ------------------------------------------------------------

local jmp_hook = { hooks = {} }

function jmp_hook.new(cast, callback, hook_addr, size, trampoline, org_bytes_tramp)
    safe_jit_off(callback)

    local new_hook, mt = {}, {}

    local hook_addr_n = to_uintptr(hook_addr)
    local size_n = (type(size) == "number" and size) or 5
    if size_n < 5 then
        error("[jmp] size must be >= 5 for JMP rel32", 2)
    end

    local trampoline_bool = (trampoline == true)

    local detour_addr = to_uintptr(ffi.cast("intptr_t", ffi.cast(cast, callback)))

    local org_bytes = ffi.new("uint8_t[?]", size_n)
    copy(org_bytes, hook_addr_n, size_n)

    -- Если делаем trampoline, то call(...) вызывает trampoline (оригинальные байты + прыжок обратно)
    if trampoline_bool then
        local tramp_mem = VirtualAlloc(size_n + 5, PAGE_EXECUTE_READWRITE, true)
        if tramp_mem == nil then
            error("[jmp] VirtualAlloc failed for trampoline", 2)
        end

        local alloc_addr = to_uintptr(tramp_mem)
        local trampoline_bytes = ffi.new("uint8_t[?]", size_n + 5, 0x90)

        if type(org_bytes_tramp) == "string" then
            local i = 0
            for byte in org_bytes_tramp:gmatch("(%x%x)") do
                if i >= size_n then break end
                trampoline_bytes[i] = tonumber(byte, 16)
                i = i + 1
            end
            if i < size_n then
                -- если строка короткая/кривая, добиваем исходными байтами
                copy(trampoline_bytes + i, org_bytes + i, size_n - i)
            end
        else
            copy(trampoline_bytes, org_bytes, size_n)
        end

        trampoline_bytes[size_n] = 0xE9
        -- rel32 = (hook_addr + size) - (alloc_addr + size + 5) = hook_addr - alloc_addr - 5
        ffi.cast("int32_t*", trampoline_bytes + size_n + 1)[0] = hook_addr_n - alloc_addr - 5

        copy(alloc_addr, trampoline_bytes, size_n + 5)

        new_hook._trampoline_ptr = tramp_mem
        new_hook.call = ffi.cast(cast, alloc_addr)

        mt = {
            __call = function(self, ...)
                return self.call(...)
            end
        }
    else
        new_hook.call = ffi.cast(cast, hook_addr_n)

        mt = {
            __call = function(self, ...)
                self.stop()
                local res = self.call(...)
                self.start()
                return res
            end
        }
    end

    -- Готовим байты JMP detour
    local hook_bytes = ffi.new("uint8_t[?]", size_n, 0x90)
    hook_bytes[0] = 0xE9
    ffi.cast("int32_t*", hook_bytes + 1)[0] = detour_addr - hook_addr_n - 5

    new_hook.status = false

    local function set_status(bool)
        if new_hook.status == bool then
            return
        end
        new_hook.status = bool

        local function write()
            copy(hook_addr_n, bool and hook_bytes or org_bytes, size_n)
        end

        local ok, err = with_protect_write(hook_addr_n, size_n, PAGE_EXECUTE_READWRITE, write)
        if not ok then
            error("[jmp] set_status failed: " .. tostring(err), 2)
        end
    end

    new_hook.stop = function() set_status(false) end
    new_hook.start = function() set_status(true) end

    new_hook.destroy = function()
        -- Снимаем хук и, если был trampoline, освобождаем его сразу
        if new_hook.status then
            new_hook.stop()
        end
        if new_hook._trampoline_ptr ~= nil then
            VirtualFree(new_hook._trampoline_ptr)
            new_hook._trampoline_ptr = nil
        end
        return true
    end

    new_hook.start()

    if org_bytes[0] == 0xE9 or org_bytes[0] == 0xE8 then
        warn("rewrote another hook" .. (trampoline_bool and " (old hook was bypassed via trampoline)" or ""))
    end

    table.insert(jmp_hook.hooks, new_hook)
    return setmetatable(new_hook, mt)
end

-- ------------------------------------------------------------
-- CALL HOOKS
-- ------------------------------------------------------------

local call_hook = { hooks = {} }

function call_hook.new(cast, callback, hook_addr)
    local hook_addr_n = to_uintptr(hook_addr)

    -- Проверяем, что это CALL rel32 (0xE8)
    if ffi.cast("uint8_t*", hook_addr_n)[0] ~= 0xE8 then
        return nil
    end

    safe_jit_off(callback)

    local new_hook = {}

    local detour_addr = to_uintptr(ffi.cast("intptr_t", ffi.cast(cast, callback)))

    local org_bytes = ffi.new("uint8_t[5]")
    copy(org_bytes, hook_addr_n, 5)

    local hook_bytes = ffi.new("uint8_t[5]", 0x90)
    hook_bytes[0] = 0xE8
    ffi.cast("uint32_t*", hook_bytes + 1)[0] = detour_addr - hook_addr_n - 5

    -- Исходная цель CALL: next + rel32
    local rel = ffi.cast("int32_t*", hook_addr_n + 1)[0]
    local target = hook_addr_n + 5 + rel
    new_hook.call = ffi.cast(cast, target)

    new_hook.status = false

    local function set_status(bool)
        if new_hook.status == bool then
            return
        end
        new_hook.status = bool

        local function write()
            copy(hook_addr_n, bool and hook_bytes or org_bytes, 5)
        end

        local ok, err = with_protect_write(hook_addr_n, 5, PAGE_EXECUTE_READWRITE, write)
        if not ok then
            error("[call] set_status failed: " .. tostring(err), 2)
        end
    end

    new_hook.stop = function() set_status(false) end
    new_hook.start = function() set_status(true) end

    new_hook.destroy = function()
        if new_hook.status then
            new_hook.stop()
        end
        return true
    end

    new_hook.start()

    table.insert(call_hook.hooks, new_hook)

    return setmetatable(new_hook, {
        __call = function(self, ...)
            return self.call(...)
        end
    })
end

-- ------------------------------------------------------------
-- CLEANUP (MoonLoader)
-- ------------------------------------------------------------

local function cleanup_all()
    -- Снимаем JMP хуки
    for i = 1, #jmp_hook.hooks do
        local hook = jmp_hook.hooks[i]
        if hook and hook.status then
            pcall(hook.stop)
        end
        -- если кто-то руками destroy() не вызвал, trampoline освободится ниже общим списком
    end

    -- Снимаем CALL хуки
    for i = 1, #call_hook.hooks do
        local hook = call_hook.hooks[i]
        if hook and hook.status then
            pcall(hook.stop)
        end
    end

    -- Освобождаем все VirtualAlloc, которые помечены trackForFree=true
    for i = 1, #buff.free do
        local addr = buff.free[i]
        pcall(VirtualFree, addr)
    end
    buff.free = {}

    -- Снимаем VMT хуки
    for i = 1, #vmt_hook.hooks do
        local unHookFunc = vmt_hook.hooks[i]
        if type(unHookFunc) == "function" then
            pcall(unHookFunc)
        end
    end
    vmt_hook.hooks = {}
end

-- Под MoonLoader есть addEventHandler + script.this.
-- Если вдруг библиотеку дернут не там, просто не вешаемся.
if type(addEventHandler) == "function" and script and script.this then
    addEventHandler("onScriptTerminate", function(scr)
        if scr == script.this then
            cleanup_all()
        end
    end)
end

return { vmt = vmt_hook, jmp = jmp_hook, call = call_hook, _cleanup = cleanup_all }
