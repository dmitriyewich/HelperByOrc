local modules = {}
local language = require("language")

local function L(key, params)
	return language.getText(key, params)
end

do
	local ok_funcs, funcs = pcall(require, "HelperByOrc.funcs")
	if ok_funcs and type(funcs) == "table" and type(funcs.loadTableFromJson) == "function" then
		local loaded = funcs.loadTableFromJson("HelperByOrc.json", {
			language = language.getDefaultCode(),
		})
		if type(loaded) == "table" then
			language.setLanguage(loaded.language)
		end
	end
end

-- Diagnostic mode: enable modules one by one here.
local MODULE_FLAGS = {
	event_bus = true,
	config_manager = true,
	funcs = true,
	mimgui_funcs = true,
	toasts = true,
	correct = true,
	tags = true,
	binder = true,
	unwanted = true,
	VIPandADchat = true,
	notepad = true,
	SMIHelp = true,
	SMILive = true,
	weapon_rp = true,
	samp = true,
	my_hooks = true,
}

local MODULE_PATHS = {
	event_bus = "HelperByOrc.event_bus",
	config_manager = "HelperByOrc.config_manager",
	funcs = "HelperByOrc.funcs",
	mimgui_funcs = "HelperByOrc.mimgui_funcs",
	toasts = "HelperByOrc.toasts",
	correct = "HelperByOrc.correct",
	tags = "HelperByOrc.tags",
	binder = "HelperByOrc.binder",
	unwanted = "HelperByOrc.unwanted",
	VIPandADchat = "HelperByOrc.VIPandADchat",
	notepad = "HelperByOrc.notepad",
	SMIHelp = "HelperByOrc.SMIHelp",
	SMILive = "HelperByOrc.SMILive",
	weapon_rp = "HelperByOrc.weapon_rp",
}

local HEAVY_MODULE_PATHS = {
	samp = "HelperByOrc.samp",
	my_hooks = "HelperByOrc.my_hooks",
}

local ATTACH_ORDER = {
	"event_bus",
	"config_manager",
	"funcs",
	"mimgui_funcs",
	"toasts",
	"tags",
	"binder",
	"correct",
	"VIPandADchat",
	"unwanted",
	"notepad",
	"SMIHelp",
	"SMILive",
	"weapon_rp",
	"samp",
	"my_hooks",
}

local TERMINATE_METHODS = {
	"onTerminate",
	"stop",
	"deinit",
}

local function loadModulesFromMap(path_map)
	for name, path in pairs(path_map) do
		if MODULE_FLAGS[name] then
			modules[name] = require(path)
		end
	end
end

local function listHasValue(list, value)
	for i = 1, #list do
		if list[i] == value then
			return true
		end
	end
	return false
end

local function buildTerminateOrder()
	local order = {}
	for i = #ATTACH_ORDER, 1, -1 do
		order[#order + 1] = ATTACH_ORDER[i]
	end
	for name, module_ref in pairs(modules) do
		if type(module_ref) == "table" and not listHasValue(order, name) then
			order[#order + 1] = name
		end
	end
	return order
end

local function attachAll()
	for i = 1, #ATTACH_ORDER do
		local name = ATTACH_ORDER[i]
		local module_ref = modules[name]
		if type(module_ref) == "table" and type(module_ref.attachModules) == "function" then
			module_ref.attachModules(modules)
		end
	end
end

loadModulesFromMap(MODULE_PATHS)
attachAll()

local config_tick_thread
local function ensureConfigTick()
	if config_tick_thread or not MODULE_FLAGS.config_manager then
		return
	end
	config_tick_thread = lua_thread.create(function()
		while true do
			wait(500)
			local cm = modules.config_manager
			if cm and type(cm.tick) == "function" then
				cm.tick()
			end
		end
	end)
end

ensureConfigTick()

function modules.loadHeavyModules()
	loadModulesFromMap(HEAVY_MODULE_PATHS)
	attachAll()
end

function modules.terminateAll(opts)
	opts = opts or {}
	local reason = tostring(opts.reason or "unknown")
	local errors = 0
	local visited = {}
	local order = buildTerminateOrder()

	for i = 1, #order do
		local name = order[i]
		local module_ref = modules[name]
		if type(module_ref) == "table" and not visited[module_ref] then
			visited[module_ref] = true

			if not module_ref._terminated then
				module_ref._terminated = true

				for j = 1, #TERMINATE_METHODS do
					local method_name = TERMINATE_METHODS[j]
					local method = module_ref[method_name]
						if type(method) == "function" then
							local ok, err = pcall(method, reason)
							if not ok then
								errors = errors + 1
								print(L("modules.log.terminate_failed", {
									module = name,
									method = method_name,
									error = tostring(err),
								}))
							end
						end
				end
			end
		end
	end

	return errors == 0, errors
end

return modules
