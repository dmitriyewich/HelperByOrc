local module = {}

local funcs
local paths = require("HelperByOrc.paths")

-- registrations[name] = {
--   path         = "toasts.json",         -- relative path as registered
--   fullpath     = "<dataRoot>/toasts.json", -- resolved via paths.dataPath()
--   defaults     = {...},
--   normalize    = fn or nil,       -- transform after load
--   serialize    = fn or nil,       -- transform before save (returns table to write)
--   onBeforeSave = fn or nil,       -- side-effect before save (e.g. backup)
--   loader       = fn or nil,       -- custom loader (replaces loadTableFromJson)
--   data         = {...},           -- live table
--   dirty        = false,
--   dirty_at     = 0,
-- }
local registrations = {}

local FLUSH_INTERVAL = 2.0
local DEBOUNCE_SEC = 0.5
local last_flush_check = 0

--- Expand a relative path like "toasts.json" to full path with HelperByOrc marker
--- so that resolve_profile_json_path can redirect it into the active profile.
local function expandPath(rel)
	if type(rel) ~= "string" or rel == "" then
		return rel
	end
	-- If already absolute (contains drive letter or HelperByOrc marker), return as-is
	if rel:find(":\\") or rel:lower():find("helperbyorc\\") then
		return rel
	end
	return paths.dataPath(rel)
end

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
end

syncDependencies()

function module.attachModules(mod)
	syncDependencies(mod)
end

function module.register(name, opts)
	if type(name) ~= "string" or type(opts) ~= "table" then
		return {}
	end
	if not opts.path or not opts.defaults then
		print(("[HelperByOrc][config_manager] register '%s': missing path or defaults"):format(name))
		return opts.defaults or {}
	end

	local fullpath = expandPath(opts.path)

	local loaded
	if type(opts.loader) == "function" then
		loaded = opts.loader(fullpath, opts.defaults)
	else
		loaded = funcs.loadTableFromJson(fullpath, opts.defaults)
	end
	local data = loaded
	if type(opts.normalize) == "function" then
		data = opts.normalize(loaded)
	end

	registrations[name] = {
		path = opts.path,
		fullpath = fullpath,
		defaults = opts.defaults,
		normalize = opts.normalize,
		serialize = opts.serialize,
		onBeforeSave = opts.onBeforeSave,
		loader = opts.loader,
		data = data,
		dirty = false,
		dirty_at = 0,
	}

	return registrations[name].data
end

function module.get(name)
	local reg = registrations[name]
	return reg and reg.data or nil
end

function module.set(name, newData)
	local reg = registrations[name]
	if not reg then
		return
	end
	if type(reg.normalize) == "function" then
		newData = reg.normalize(newData)
	end
	reg.data = newData
	reg.dirty = true
	reg.dirty_at = os.clock()
end

function module.markDirty(name)
	local reg = registrations[name]
	if reg then
		reg.dirty = true
		reg.dirty_at = os.clock()
	end
end

function module.flush(name, force)
	local reg = registrations[name]
	if not reg or not reg.dirty then
		return false
	end
	if not force and (os.clock() - reg.dirty_at) < DEBOUNCE_SEC then
		return false
	end
	local savePath = reg.fullpath or expandPath(reg.path)
	if type(reg.onBeforeSave) == "function" then
		reg.onBeforeSave(reg.data, savePath)
	end
	local toSave = reg.data
	if type(reg.serialize) == "function" then
		toSave = reg.serialize(reg.data)
	end
	funcs.saveTableToJson(toSave, savePath)
	reg.dirty = false
	return true
end

function module.flushAll(force)
	for name in pairs(registrations) do
		module.flush(name, force)
	end
end

function module.tick()
	local now = os.clock()
	if (now - last_flush_check) < FLUSH_INTERVAL then
		return
	end
	last_flush_check = now
	module.flushAll(false)
end

function module.unregister(name)
	local reg = registrations[name]
	if reg and reg.dirty then
		module.flush(name, true)
	end
	registrations[name] = nil
end

function module.listRegistered()
	local names = {}
	for name in pairs(registrations) do
		names[#names + 1] = name
	end
	return names
end

function module.onTerminate()
	module.flushAll(true)
end

return module
