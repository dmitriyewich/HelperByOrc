script_name("HelperByOrc Variables Host")
script_author("HelperByOrc")

local BRIDGE_EXPORT = "luaopen_helperbyorc_bridge"
local BRIDGE_PROTOCOL = 1
local POLL_MS = 250
local RETRY_MS = 1000
local MAX_PLAN_ENTRIES = 128

if type(package) ~= "table" or type(package.loadlib) ~= "function" then
    error("HelperByOrc requires package.loadlib from MoonLoader LuaJIT")
end
local loadlib_ok, open_bridge, load_error = pcall(package.loadlib, "HelperByOrc.asi", BRIDGE_EXPORT)
if not loadlib_ok then
    error("HelperByOrc bridge load raised an error: " .. tostring(open_bridge))
end
if not open_bridge then
    error("HelperByOrc bridge export is unavailable: " .. tostring(load_error))
end

local open_ok, bridge, bridge_error = pcall(open_bridge, BRIDGE_PROTOCOL)
if not open_ok then
    error("HelperByOrc bridge initialization raised an error: " .. tostring(bridge))
end
if type(bridge) ~= "table" then
    error("HelperByOrc bridge initialization failed: " .. tostring(bridge_error))
end
local required_bridge_methods = {
    "hello",
    "generation",
    "plan",
    "attach",
    "register_simple",
    "register_function",
    "run_provider_chunk",
    "ready",
    "detach",
    "fault",
    "read_source",
    "invalidate",
    "publish",
    "log",
}
for _, method_name in ipairs(required_bridge_methods) do
    if type(bridge[method_name]) ~= "function" then
        error("HelperByOrc bridge method is unavailable: " .. method_name)
    end
end

local hello_ok, protocol_ok, backend = pcall(bridge.hello, BRIDGE_PROTOCOL)
if not hello_ok then
    error("HelperByOrc bridge hello raised an error: " .. tostring(protocol_ok))
end
if not protocol_ok or backend ~= "moonloader" then
    error("HelperByOrc bridge protocol/backend mismatch: " .. tostring(backend))
end

if type(script) ~= "table" or type(script.load) ~= "function" then
    error("HelperByOrc requires script.load from MoonLoader")
end
if type(doesDirectoryExist) ~= "function"
    or type(createDirectory) ~= "function"
    or type(getWorkingDirectory) ~= "function"
    or type(wait) ~= "function"
    or type(thisScript) ~= "function"
then
    error("HelperByOrc requires the MoonLoader filesystem and script APIs")
end

local getenv_ok, temp_root = pcall(os.getenv, "TEMP")
if not getenv_ok or type(temp_root) ~= "string" or temp_root == "" then
    local working_ok, working_directory = pcall(getWorkingDirectory)
    if not working_ok or type(working_directory) ~= "string" or working_directory == "" then
        error("HelperByOrc temporary directory is unavailable: " .. tostring(working_directory))
    end
    temp_root = working_directory
end
local helper_temp_root = temp_root .. "\\HelperByOrc"
local session_suffix = tostring(os.time()) .. "_" .. tostring(math.floor(os.clock() * 1000000))
local generated_root = helper_temp_root .. "\\lua_vars\\" .. session_suffix
local loaded = {}
local active_generation = -1
local retry_elapsed_ms = 0
local last_host_error = nil

local function report_host_error(message)
    message = tostring(message)
    if message ~= last_host_error then
        last_host_error = message
        print("HelperByOrc: " .. message)
    end
end

local function clear_host_error()
    last_host_error = nil
end

local function call_bridge(method_name, ...)
    local method = bridge[method_name]
    if type(method) ~= "function" then
        return nil, "bridge method is unavailable: " .. tostring(method_name)
    end
    local ok, value, call_error = pcall(method, ...)
    if not ok then
        return nil, "bridge." .. method_name .. " raised an error: " .. tostring(value)
    end
    if value == nil then
        return nil, "bridge." .. method_name .. " failed: " .. tostring(call_error)
    end
    return value, call_error
end

local function ensure_directory(path)
    local exists_ok, exists = pcall(doesDirectoryExist, path)
    if exists_ok and exists then
        return true
    end
    local create_ok, created = pcall(createDirectory, path)
    return create_ok and created == true
end

local function write_file(path, source)
    local open_ok, file, err = pcall(io.open, path, "wb")
    if not open_ok then
        return nil, file
    end
    if not file then
        return nil, err
    end
    local write_ok, write_result, write_error = pcall(file.write, file, source)
    if not write_ok or not write_result then
        pcall(file.close, file)
        return nil, write_ok and write_error or write_result
    end
    local close_ok, close_result, close_error = pcall(file.close, file)
    if not close_ok or not close_result then
        return nil, close_ok and close_error or close_result
    end
    return true
end

local function unload_all()
    for _, item in pairs(loaded) do
        if item.script then
            local unload_ok, unload_error = pcall(function()
                if not item.script.dead then
                    item.script:unload()
                end
            end)
            if not unload_ok then
                report_host_error("failed to unload provider wrapper: " .. tostring(unload_error))
            end
        end
        if item.path then
            pcall(os.remove, item.path)
        end
    end
    loaded = {}
end

local function wrapper_needs_load(item)
    if not item.script then
        return true
    end
    local state_ok, dead = pcall(function()
        return item.script.dead
    end)
    if not state_ok then
        report_host_error("failed to query provider wrapper state: " .. tostring(dead))
        item.script = nil
        return true
    end
    return dead == true
end

local function read_generation()
    local generation, generation_error = call_bridge("generation")
    if type(generation) ~= "number"
        or generation < 1
        or generation > 9007199254740991
        or generation ~= math.floor(generation)
    then
        return nil, generation_error or "bridge.generation returned an invalid value"
    end
    return generation
end

local function read_plan(generation)
    local plan, plan_error = call_bridge("plan")
    if type(plan) ~= "table" then
        return nil, plan_error or "bridge.plan returned a non-table value"
    end

    local highest_index = 0
    local entry_count = 0
    for key in pairs(plan) do
        if type(key) ~= "number"
            or key < 1
            or key ~= math.floor(key)
        then
            return nil, "bridge.plan contains an invalid array key: " .. tostring(key)
        end
        highest_index = math.max(highest_index, key)
        entry_count = entry_count + 1
    end
    if highest_index ~= entry_count then
        return nil, "bridge.plan must be a dense array"
    end
    if entry_count > MAX_PLAN_ENTRIES then
        return nil, "bridge.plan exceeds the provider limit"
    end

    local normalized = {}
    local ids = {}
    for index = 1, entry_count do
        local entry = plan[index]
        if type(entry) ~= "table" then
            return nil, "bridge.plan entry " .. tostring(index) .. " is not a table"
        end
        if type(entry.id) ~= "string" or entry.id == "" then
            return nil, "bridge.plan entry " .. tostring(index) .. " has an invalid id"
        end
        if ids[entry.id] then
            return nil, "bridge.plan contains a duplicate provider id: " .. entry.id
        end
        if type(entry.path) ~= "string" or entry.path == "" then
            return nil, "bridge.plan entry " .. tostring(index) .. " has an invalid path"
        end
        if type(entry.enabled) ~= "boolean" then
            return nil, "bridge.plan entry " .. tostring(index) .. " has an invalid enabled flag"
        end
        if type(entry.generation) ~= "number"
            or entry.generation ~= generation
            or entry.generation ~= math.floor(entry.generation)
        then
            return nil, "bridge.plan entry " .. tostring(index) .. " has an invalid generation"
        end
        if type(entry.state) ~= "string" or entry.state == "" then
            return nil, "bridge.plan entry " .. tostring(index) .. " has an invalid state"
        end
        ids[entry.id] = true
        normalized[index] = {
            id = entry.id,
            path = entry.path,
            enabled = entry.enabled,
            generation = entry.generation,
            state = entry.state,
        }
    end
    return normalized
end

local function wrapper_source(entry)
    return ([[
script_name(%q)
script_author("HelperByOrc")

local provider_id = %q
local provider_generation = %d
if type(package) ~= "table" or type(package.loadlib) ~= "function" then
    error("HelperByOrc requires package.loadlib from MoonLoader LuaJIT")
end
local loadlib_ok, open_bridge, load_error = pcall(
    package.loadlib,
    "HelperByOrc.asi",
    "luaopen_helperbyorc_bridge")
if not loadlib_ok then
    error("HelperByOrc bridge load raised an error: " .. tostring(open_bridge))
end
if not open_bridge then
    error("HelperByOrc bridge export is unavailable: " .. tostring(load_error))
end
local open_ok, bridge, bridge_error = pcall(open_bridge, %d)
if not open_ok then
    error("HelperByOrc bridge initialization raised an error: " .. tostring(bridge))
end
if type(bridge) ~= "table" then
    error("HelperByOrc bridge initialization failed: " .. tostring(bridge_error))
end
local hello_ok, protocol_ok, backend = pcall(bridge.hello, %d)
if not hello_ok then
    error("HelperByOrc bridge hello raised an error: " .. tostring(protocol_ok))
end
if not protocol_ok or backend ~= "moonloader" then
    error("HelperByOrc bridge protocol/backend mismatch: " .. tostring(backend))
end

local function call_bridge(method_name, ...)
    local method = bridge[method_name]
    if type(method) ~= "function" then
        return nil, "bridge method is unavailable: " .. tostring(method_name)
    end
    local ok, value, call_error = pcall(method, ...)
    if not ok then
        return nil, "bridge." .. method_name .. " raised an error: " .. tostring(value)
    end
    if value == nil then
        return nil, "bridge." .. method_name .. " failed: " .. tostring(call_error)
    end
    return value, call_error
end

local jit_control = jit
if type(jit_control) ~= "table" or type(jit_control.off) ~= "function" then
    error("HelperByOrc requires the MoonLoader LuaJIT runtime")
end

local attached, attach_error = call_bridge("attach", provider_id, provider_generation)
if not attached then
    error("HelperByOrc provider attach failed: " .. tostring(attach_error))
end

local detached = false
local function detach(detail)
    if not detached then
        detached = true
        call_bridge("detach", provider_id, provider_generation, detail or "script terminated")
    end
end

local current_thisbind_value = nil
local environment = {}
setmetatable(environment, {
    __index = function(_, key)
        if key == "thisbind_value" then
            return current_thisbind_value
        end
        return _G[key]
    end,
    __newindex = function(_, key, value)
        if key == "thisbind_value" then
            error("thisbind_value is read-only", 2)
        end
        rawset(_G, key, value)
    end,
})

local function with_thisbind(thisbind_value, callback, ...)
    current_thisbind_value = thisbind_value
    local ok, value = pcall(callback, ...)
    current_thisbind_value = nil
    if not ok then
        error(value, 0)
    end
    return value
end

local function disable_callback_jit(callback)
    pcall(jit_control.off, callback, true)
end

environment.registerVariable = function(name, description, callback, options)
    if type(callback) ~= "function" then
        error("registerVariable callback must be a function", 2)
    end
    disable_callback_jit(callback)
    local wrapped = function(thisbind_value)
        return with_thisbind(thisbind_value, callback, thisbind_value)
    end
    disable_callback_jit(wrapped)
    return call_bridge(
        "register_simple",
        provider_id,
        provider_generation,
        name,
        description or "",
        wrapped,
        options)
end

environment.registerFunctionalVariable = function(name, description, callback, options)
    if type(callback) ~= "function" then
        error("registerFunctionalVariable callback must be a function", 2)
    end
    disable_callback_jit(callback)
    local wrapped = function(parameter, thisbind_value)
        return with_thisbind(thisbind_value, callback, parameter, thisbind_value)
    end
    disable_callback_jit(wrapped)
    return call_bridge(
        "register_function",
        provider_id,
        provider_generation,
        name,
        description or "",
        wrapped,
        options)
end

environment.invalidateVariable = function(name)
    return call_bridge("invalidate", provider_id, name)
end

environment.publishVariable = function(name, value)
    return call_bridge("publish", provider_id, name, value)
end

environment.logVariableInfo = function(message)
    return call_bridge("log", provider_id, "info", tostring(message))
end

environment.logVariableError = function(message)
    return call_bridge("log", provider_id, "error", tostring(message))
end

local source, source_path = call_bridge("read_source", provider_id, provider_generation)
if not source then
    call_bridge("fault", provider_id, provider_generation, tostring(source_path))
    error("HelperByOrc failed to read provider source: " .. tostring(source_path))
end

local chunk, syntax_error = loadstring(source, "@" .. source_path)
if not chunk then
    call_bridge("fault", provider_id, provider_generation, tostring(syntax_error))
    error("HelperByOrc provider syntax error: " .. tostring(syntax_error))
end
setfenv(chunk, environment)

local loaded_ok, runtime_error = call_bridge(
    "run_provider_chunk",
    provider_id,
    provider_generation,
    chunk)
if not loaded_ok then
    call_bridge("fault", provider_id, provider_generation, tostring(runtime_error))
    error("HelperByOrc provider load error: " .. tostring(runtime_error))
end

local ready, ready_error = call_bridge("ready", provider_id, provider_generation, function()
    return tostring(getMoonloaderVersion()) .. "|" .. tostring(jit_control.version)
end)
if not ready then
    call_bridge("fault", provider_id, provider_generation, tostring(ready_error))
    error("HelperByOrc MoonLoader self-test failed: " .. tostring(ready_error))
end

local provider_main = rawget(_G, "main")
local provider_on_terminate = rawget(_G, "onScriptTerminate")

function main()
    if type(provider_main) == "function" then
        if type(lua_thread) ~= "table" or type(lua_thread.create) ~= "function" then
            call_bridge("fault", provider_id, provider_generation, "lua_thread.create is unavailable")
            error("HelperByOrc requires lua_thread.create for provider main")
        end
        lua_thread.create(provider_main)
    end
    while true do
        wait(250)
        local current_generation, generation_error = call_bridge("generation")
        if type(current_generation) ~= "number" then
            call_bridge("fault", provider_id, provider_generation, tostring(generation_error))
            detach("bridge generation failed")
            thisScript():unload()
            return
        end
        if current_generation ~= provider_generation then
            detach("profile or extension reload")
            thisScript():unload()
            return
        end
    end
end

function onScriptTerminate(script, quit)
    if script == thisScript() then
        if type(provider_on_terminate) == "function" then
            pcall(provider_on_terminate, script, quit)
        end
        detach(quit and "game shutdown" or "script terminated")
    end
end
]]):format(
        "HelperByOrc variable: " .. entry.id,
        entry.id,
        entry.generation,
        BRIDGE_PROTOCOL,
        BRIDGE_PROTOCOL)
end

local function load_wrapper(item)
    if not item.written then
        local written, write_error = write_file(item.path, item.source)
        if not written then
            return nil, "failed to write provider wrapper: " .. tostring(write_error)
        end
        item.written = true
    end

    local load_ok, script_object = pcall(script.load, item.path)
    if not load_ok then
        return nil, "script.load raised an error for " .. item.id .. ": " .. tostring(script_object)
    end
    if not script_object then
        return nil, "script.load returned nil for " .. item.id
    end
    if type(script_object) ~= "table" and type(script_object) ~= "userdata" then
        return nil, "script.load returned an invalid script object for " .. item.id
    end
    item.script = script_object
    return true
end

local function load_plan()
    local generation, generation_error = read_generation()
    if not generation then
        report_host_error(generation_error)
        return false
    end
    if generation == active_generation then
        return true
    end

    local plan, plan_error = read_plan(generation)
    if not plan then
        report_host_error(plan_error)
        return false
    end
    if not ensure_directory(helper_temp_root) then
        report_host_error("failed to create temporary root: " .. helper_temp_root)
        return false
    end
    local lua_temp_root = helper_temp_root .. "\\lua_vars"
    if not ensure_directory(lua_temp_root) or not ensure_directory(generated_root) then
        report_host_error("failed to create Lua host directory: " .. generated_root)
        return false
    end
    local generation_root = generated_root .. "\\" .. tostring(generation)
    if not ensure_directory(generation_root) then
        report_host_error("failed to create generation directory: " .. generation_root)
        return false
    end

    unload_all()
    active_generation = generation
    local all_loaded = true
    for index, entry in ipairs(plan) do
        if entry.enabled then
            local item = {
                id = entry.id,
                path = generation_root .. "\\provider_" .. tostring(index) .. ".lua",
                source = wrapper_source(entry),
                written = false,
                script = nil,
            }
            loaded[entry.id] = item
            local started, start_error = load_wrapper(item)
            if not started then
                all_loaded = false
                report_host_error(start_error)
            end
        end
    end
    if all_loaded then
        clear_host_error()
    end
    return true
end

local function current_plan_by_id()
    local generation, generation_error = read_generation()
    if not generation then
        return nil, generation_error
    end
    if generation ~= active_generation then
        return nil, "bridge generation changed while reading the plan"
    end
    local plan, plan_error = read_plan(generation)
    if not plan then
        return nil, plan_error
    end
    local result = {}
    for _, entry in ipairs(plan) do
        result[entry.id] = entry
    end
    return result
end

function main()
    while true do
        wait(POLL_MS)
        load_plan()

        retry_elapsed_ms = retry_elapsed_ms + POLL_MS
        if retry_elapsed_ms >= RETRY_MS then
            retry_elapsed_ms = 0
            local plan_by_id, plan_error = current_plan_by_id()
            if not plan_by_id then
                report_host_error(plan_error)
            else
                local retry_failed = false
                for id, item in pairs(loaded) do
                    local entry = plan_by_id[id]
                    if entry
                        and entry.enabled
                        and entry.state == "waiting_moonloader"
                        and wrapper_needs_load(item)
                    then
                        local started, start_error = load_wrapper(item)
                        if not started then
                            retry_failed = true
                            report_host_error(start_error)
                        end
                    end
                end
                if not retry_failed then
                    clear_host_error()
                end
            end
        end
    end
end

function onScriptTerminate(script_object, quit)
    if script_object == thisScript() then
        unload_all()
    end
end
