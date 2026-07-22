script_name("HelperByOrc Variables Host")
script_author("HelperByOrc")

local BRIDGE_EXPORT = "luaopen_helperbyorc_bridge"
local BRIDGE_PROTOCOL = 2
local POLL_MS = 250
local RETRY_MS = 1000
local BRIDGE_RETRY_TIMEOUT_MS = 15000
local MAX_PLAN_ENTRIES = 128
local MAX_EXACT_INTEGER = 9007199254740991

local required_bridge_methods = {
    "hello",
    "activate",
    "claim_role",
    "begin_provider_load",
    "provider_load_status",
    "finish_provider_load",
    "cancel_provider_load",
    "release_role",
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

local bridge = nil
local backend_epoch = nil
local role = nil
local provider_id = nil
local provider_generation = nil
local provider_claim_token = nil
local provider_main = nil
local provider_on_terminate = nil
local provider_detached = false

local loaded = {}
local active_generation = -1
local last_host_error = nil
local bridge_terminal_error = nil
local provider_loading_disabled = false

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

local function is_exact_positive_integer(value)
    return type(value) == "number"
        and value >= 1
        and value <= MAX_EXACT_INTEGER
        and value == math.floor(value)
end

local function validate_bridge(candidate)
    if type(candidate) ~= "table" then
        return nil, "bridge initialization returned a non-table value"
    end
    for _, method_name in ipairs(required_bridge_methods) do
        if type(candidate[method_name]) ~= "function" then
            return nil, "bridge method is unavailable: " .. method_name
        end
    end
    return true
end

local function open_and_claim_bridge()
    if type(package) ~= "table" or type(package.loadlib) ~= "function" then
        return nil, "package.loadlib is unavailable"
    end

    local loadlib_ok, open_bridge, load_error = pcall(
        package.loadlib,
        "HelperByOrc.asi",
        BRIDGE_EXPORT)
    if not loadlib_ok then
        return nil, "bridge load raised an error: " .. tostring(open_bridge)
    end
    if type(open_bridge) ~= "function" then
        return nil, "bridge export is unavailable: " .. tostring(load_error)
    end

    local open_ok, candidate, bridge_error = pcall(open_bridge, BRIDGE_PROTOCOL)
    if not open_ok then
        return nil, "bridge initialization raised an error: " .. tostring(candidate)
    end
    if type(candidate) ~= "table" then
        return nil, "bridge is not ready: " .. tostring(bridge_error)
    end
    local valid, validation_error = validate_bridge(candidate)
    if not valid then
        return nil, validation_error
    end

    local hello_ok, protocol_ok, backend = pcall(candidate.hello, BRIDGE_PROTOCOL)
    if not hello_ok then
        return nil, "bridge hello raised an error: " .. tostring(protocol_ok)
    end
    if not protocol_ok or backend ~= "moonloader" then
        return nil, "bridge protocol/backend mismatch: " .. tostring(backend)
    end

    local activate_ok, activated, generation, epoch = pcall(candidate.activate)
    if not activate_ok then
        return nil, "bridge activation raised an error: " .. tostring(activated)
    end
    if not activated then
        return nil, "bridge activation failed: " .. tostring(generation)
    end
    if not is_exact_positive_integer(generation)
        or not is_exact_positive_integer(epoch)
    then
        return nil, "bridge activation returned invalid generation/epoch"
    end

    local claim_ok, claimed_role, claimed_id, claimed_generation, claim_token = pcall(
        candidate.claim_role,
        epoch)
    if not claim_ok then
        return nil, "bridge role claim raised an error: " .. tostring(claimed_role), true
    end
    if claimed_role ~= "controller" and claimed_role ~= "provider" then
        return nil, "bridge role claim failed: " .. tostring(claimed_id), true
    end
    if claimed_role == "provider"
        and (type(claimed_id) ~= "string"
            or claimed_id == ""
            or claimed_generation ~= generation
            or not is_exact_positive_integer(claim_token))
    then
        return nil, "bridge returned an invalid provider role claim", true
    end

    return {
        bridge = candidate,
        epoch = epoch,
        role = claimed_role,
        provider_id = claimed_id,
        provider_generation = claimed_generation,
        provider_claim_token = claim_token,
    }
end

local function try_initialize_bridge()
    local result, initialize_error, terminal = open_and_claim_bridge()
    if not result then
        return nil, initialize_error, terminal
    end
    bridge = result.bridge
    backend_epoch = result.epoch
    role = result.role
    provider_id = result.provider_id
    provider_generation = result.provider_generation
    provider_claim_token = result.provider_claim_token
    return true
end

local function call_bridge(method_name, ...)
    if type(bridge) ~= "table" then
        return nil, "bridge is unavailable"
    end
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

local function read_generation()
    local generation, generation_error = call_bridge("generation")
    if not is_exact_positive_integer(generation) then
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
        if entry.generation ~= generation or not is_exact_positive_integer(entry.generation) then
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

local function detach_provider(detail)
    if role == "provider" and not provider_detached then
        provider_detached = true
        call_bridge(
            "detach",
            provider_id,
            provider_generation,
            detail or "script terminated")
    end
end

local function initialize_provider()
    if type(jit) ~= "table" or type(jit.off) ~= "function" then
        error("HelperByOrc requires the MoonLoader LuaJIT runtime", 0)
    end
    pcall(script_name, "HelperByOrc variable: " .. provider_id)

    local attached, attach_error = call_bridge(
        "attach",
        provider_id,
        provider_generation,
        provider_claim_token)
    if not attached then
        error("HelperByOrc provider attach failed: " .. tostring(attach_error), 0)
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
        pcall(jit.off, callback, true)
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
        error("HelperByOrc failed to read provider source: " .. tostring(source_path), 0)
    end
    local chunk, syntax_error = loadstring(source, "@" .. source_path)
    if not chunk then
        error("HelperByOrc provider syntax error: " .. tostring(syntax_error), 0)
    end
    setfenv(chunk, environment)

    local loaded_ok, runtime_error = call_bridge(
        "run_provider_chunk",
        provider_id,
        provider_generation,
        chunk)
    if not loaded_ok then
        error("HelperByOrc provider load error: " .. tostring(runtime_error), 0)
    end

    local ready, ready_error = call_bridge("ready", provider_id, provider_generation, function()
        return tostring(getMoonloaderVersion()) .. "|" .. tostring(jit.version)
    end)
    if not ready then
        error("HelperByOrc MoonLoader self-test failed: " .. tostring(ready_error), 0)
    end

    provider_main = rawget(_G, "main")
    provider_on_terminate = rawget(_G, "onScriptTerminate")
end

local function script_is_dead(script_object)
    if not script_object then
        return true
    end
    local state_ok, dead = pcall(function()
        return script_object.dead
    end)
    return not state_ok or dead == true
end

local function unload_script(script_object)
    if not script_object then
        return true
    end
    local unload_ok, unload_error = pcall(function()
        if not script_object.dead then
            script_object:unload()
        end
    end)
    if not unload_ok then
        report_host_error("failed to unload provider Host state: " .. tostring(unload_error))
    end
    return unload_ok
end

local function unload_all()
    for _, item in pairs(loaded) do
        unload_script(item.script)
    end
    loaded = {}
end

local function host_script_path()
    local path_ok, path = pcall(function()
        return thisScript().path
    end)
    if not path_ok or type(path) ~= "string" or path == "" then
        return nil, "current Host path is unavailable: " .. tostring(path)
    end
    return path
end

local function start_provider(item)
    local token, begin_error = call_bridge(
        "begin_provider_load",
        item.id,
        item.generation,
        backend_epoch)
    if not is_exact_positive_integer(token) then
        return nil, begin_error or "bridge returned an invalid provider load token"
    end

    local path, path_error = host_script_path()
    if not path then
        call_bridge("cancel_provider_load", token, backend_epoch)
        provider_loading_disabled = true
        return nil, path_error
    end
    local load_ok, script_object = pcall(script.load, path)
    if not load_ok
        or (type(script_object) ~= "table" and type(script_object) ~= "userdata")
    then
        call_bridge("cancel_provider_load", token, backend_epoch)
        provider_loading_disabled = true
        return nil, load_ok
            and ("script.load returned an invalid object for " .. item.id)
            or ("script.load raised an error for " .. item.id .. ": " .. tostring(script_object))
    end
    local current_script_ok, current_script = pcall(thisScript)
    if current_script_ok and script_object == current_script then
        call_bridge("cancel_provider_load", token, backend_epoch)
        provider_loading_disabled = true
        return nil, "script.load reused the controller instead of creating a provider state"
    end
    for _, loaded_item in pairs(loaded) do
        if loaded_item.script and loaded_item.script == script_object then
            call_bridge("cancel_provider_load", token, backend_epoch)
            provider_loading_disabled = true
            return nil, "script.load reused an existing provider state for " .. item.id
        end
    end

    local status, status_error = call_bridge(
        "provider_load_status",
        token,
        backend_epoch)
    if status ~= "claimed" then
        call_bridge("cancel_provider_load", token, backend_epoch)
        unload_script(script_object)
        provider_loading_disabled = true
        return nil, status_error
            or "script.load did not synchronously claim a provider state for " .. item.id
    end
    local finished, finish_error = call_bridge(
        "finish_provider_load",
        token,
        backend_epoch)
    if not finished then
        call_bridge("cancel_provider_load", token, backend_epoch)
        unload_script(script_object)
        provider_loading_disabled = true
        return nil, finish_error
    end

    item.script = script_object
    item.retry_elapsed_ms = 0
    clear_host_error()
    return true
end

local function synchronize_plan(plan, generation)
    local plan_by_id = {}
    for _, entry in ipairs(plan) do
        plan_by_id[entry.id] = entry
    end

    for id, item in pairs(loaded) do
        local entry = plan_by_id[id]
        if not entry or not entry.enabled or entry.generation ~= generation then
            unload_script(item.script)
            loaded[id] = nil
        else
            item.state = entry.state
            item.generation = entry.generation
            if item.script and script_is_dead(item.script) then
                item.script = nil
                item.retry_elapsed_ms = 0
            end
        end
    end

    for _, entry in ipairs(plan) do
        if entry.enabled and not loaded[entry.id] then
            loaded[entry.id] = {
                id = entry.id,
                generation = entry.generation,
                state = entry.state,
                script = nil,
                retry_elapsed_ms = RETRY_MS,
            }
        end
    end
end

local function start_next_provider(plan)
    if provider_loading_disabled then
        return true
    end
    for _, entry in ipairs(plan) do
        local item = loaded[entry.id]
        if item
            and entry.enabled
            and entry.state == "waiting_moonloader"
            and not item.script
        then
            item.retry_elapsed_ms = item.retry_elapsed_ms + POLL_MS
            if item.retry_elapsed_ms >= RETRY_MS then
                local started, start_error = start_provider(item)
                if not started then
                    item.retry_elapsed_ms = 0
                    return nil, start_error
                end
            end
            return true
        end
    end
    return true
end

local function controller_tick()
    local generation, generation_error = read_generation()
    if not generation then
        return nil, generation_error
    end
    if generation ~= active_generation then
        unload_all()
        active_generation = generation
    end

    local plan, plan_error = read_plan(generation)
    if not plan then
        return nil, plan_error
    end
    synchronize_plan(plan, generation)
    return start_next_provider(plan)
end

local function controller_main()
    local bridge_wait_ms = 0
    local bridge_error = nil
    if bridge_terminal_error then
        report_host_error(bridge_terminal_error)
        thisScript():unload()
        return
    end
    while not role and bridge_wait_ms < BRIDGE_RETRY_TIMEOUT_MS do
        local initialized, initialize_error, terminal = try_initialize_bridge()
        if initialized then
            break
        end
        if terminal then
            report_host_error(initialize_error)
            thisScript():unload()
            return
        end
        bridge_error = initialize_error
        wait(POLL_MS)
        bridge_wait_ms = bridge_wait_ms + POLL_MS
    end
    if not role then
        report_host_error(
            "bridge did not become ready within 15 seconds: " .. tostring(bridge_error))
        thisScript():unload()
        return
    end
    if role ~= "controller" then
        call_bridge(
            "fault",
            provider_id,
            provider_generation,
            "provider role was claimed after the script main state")
        report_host_error("provider role was claimed too late for a safe attach")
        thisScript():unload()
        return
    end

    while true do
        local tick_ok, tick_error = controller_tick()
        if not tick_ok then
            report_host_error(tick_error)
        end
        wait(POLL_MS)
    end
end

local function provider_host_main()
    if type(provider_main) == "function" then
        if type(lua_thread) ~= "table" or type(lua_thread.create) ~= "function" then
            call_bridge("fault", provider_id, provider_generation, "lua_thread.create is unavailable")
            error("HelperByOrc requires lua_thread.create for provider main", 0)
        end
        lua_thread.create(provider_main)
    end
    while true do
        wait(POLL_MS)
        local current_generation, generation_error = read_generation()
        if not current_generation then
            call_bridge("fault", provider_id, provider_generation, tostring(generation_error))
            detach_provider("bridge generation failed")
            thisScript():unload()
            return
        end
        if current_generation ~= provider_generation then
            detach_provider("profile or extension reload")
            thisScript():unload()
            return
        end
    end
end

if type(script) ~= "table"
    or type(script.load) ~= "function"
    or type(wait) ~= "function"
    or type(thisScript) ~= "function"
then
    error("HelperByOrc requires MoonLoader script.load/wait/thisScript APIs", 0)
end

do
    local initialized, initialize_error, terminal = try_initialize_bridge()
    if not initialized and terminal then
        bridge_terminal_error = initialize_error
    end
end
if role == "provider" then
    local initialized, initialize_error = pcall(initialize_provider)
    if not initialized then
        call_bridge("fault", provider_id, provider_generation, tostring(initialize_error))
        detach_provider("provider initialization failed")
        error(tostring(initialize_error), 0)
    end
end

function main()
    if role == "provider" then
        provider_host_main()
    else
        controller_main()
    end
end

function onScriptTerminate(script_object, quit)
    if script_object ~= thisScript() then
        return
    end
    if role == "provider" then
        if type(provider_on_terminate) == "function" then
            pcall(provider_on_terminate, script_object, quit)
        end
        detach_provider(quit and "game shutdown" or "script terminated")
    elseif role == "controller" then
        unload_all()
        call_bridge("release_role")
    end
end
