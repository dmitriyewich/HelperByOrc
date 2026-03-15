local function fail(message)
	error("[HelperByOrc:init] " .. tostring(message), 0)
end

local function ensureMoonlyRuntime()
	if type(getWorkingDirectory) ~= "function" then
		fail("getWorkingDirectory unavailable; expected moonly runtime")
	end
end

local function loadApplication()
	local ok_modules, modules = pcall(require, "HelperByOrc.modules")
	if not ok_modules then
		fail("failed to load src/HelperByOrc/modules.lua: " .. tostring(modules))
	end

	local ok_app, app = pcall(require, "HelperByOrc")
	if not ok_app then
		fail("failed to load src/HelperByOrc.lua: " .. tostring(app))
	end

	if type(modules) ~= "table" then
		fail("module loader returned non-table value")
	end

	if type(modules.loadHeavyModules) == "function" then
		local ok_heavy, err = pcall(modules.loadHeavyModules)
		if not ok_heavy then
			fail("failed to load heavy modules: " .. tostring(err))
		end
	end

	if type(app) == "table" and type(app.attachModules) == "function" then
		local ok_attach, err = pcall(app.attachModules, modules)
		if not ok_attach then
			fail("failed to attach modules: " .. tostring(err))
		end
	end

	return app, modules
end

ensureMoonlyRuntime()
local app, modules = loadApplication()
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
	while not isSampAvailable() do
		wait(1000)
	end

	if type(app) == "table" and type(app.onSampReady) == "function" then
		local ok, err = pcall(app.onSampReady)
		if not ok then
			sampAddChatMessage("[HelperByOrc] Ошибка при запуске: " .. tostring(err), 0xFF0000)
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
