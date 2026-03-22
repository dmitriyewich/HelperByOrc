local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local function fail(message)
	error(L("init.error.prefix", {
		message = tostring(message),
	}), 0)
end

local function ensureMoonlyRuntime()
	if type(getWorkingDirectory) ~= "function" then
		fail(L("init.error.moonly_runtime_unavailable"))
	end
end

local function loadApplication()
	local ok_modules, modules = pcall(require, "HelperByOrc.modules")
	if not ok_modules then
		fail(L("init.error.load_modules_failed", {
			error = tostring(modules),
		}))
	end

	local ok_app, app = pcall(require, "HelperByOrc")
	if not ok_app then
		fail(L("init.error.load_app_failed", {
			error = tostring(app),
		}))
	end

	if type(modules) ~= "table" then
		fail(L("init.error.module_loader_non_table"))
	end

	if type(modules.loadHeavyModules) == "function" then
		local ok_heavy, err = pcall(modules.loadHeavyModules)
		if not ok_heavy then
			fail(L("init.error.load_heavy_modules_failed", {
				error = tostring(err),
			}))
		end
	end

	if type(app) == "table" and type(app.attachModules) == "function" then
		local ok_attach, err = pcall(app.attachModules, modules)
		if not ok_attach then
			fail(L("init.error.attach_modules_failed", {
				error = tostring(err),
			}))
		end
	end

	return app, modules
end

ensureMoonlyRuntime()
local app, modules = loadApplication()
local samp_module = type(modules) == "table" and modules.samp or nil
local shutdown_in_progress = false

local function runShutdown(reason)
	if shutdown_in_progress then
		return
	end
	shutdown_in_progress = true
	reason = tostring(reason or "unknown")

	if type(app) == "table" and type(app.onTerminate) == "function" then
		pcall(app.onTerminate, reason)
	end

	if type(modules) == "table" and type(modules.terminateAll) == "function" then
		pcall(modules.terminateAll, {
			reason = reason,
		})
	end
end

function main()
	if type(samp_module) ~= "table" or type(samp_module.isSAMPInitilizeLua) ~= "function" then
		fail(L("init.error.samp_module_unavailable"))
	end

	while not samp_module.isSAMPInitilizeLua() do
		wait(1000)
	end

	if type(app) == "table" and type(app.onSampReady) == "function" then
		local ok, err = pcall(app.onSampReady)
		if not ok then
			sampAddChatMessage(L("init.text.helperbyorc") .. tostring(err), 0xFF0000)
		end
	end

	wait(-1)
end

if type(addEventHandler) == "function" and type(thisScript) == "function" then
	addEventHandler("onScriptTerminate", function(scr, quitGame)
		if scr == thisScript() then
			local reason = quitGame and "quit_game" or "reload_or_stop"
			runShutdown(reason)
		end
	end)
end
