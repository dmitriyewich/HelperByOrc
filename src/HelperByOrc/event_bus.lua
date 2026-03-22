local module = {}
local language = require("language")

local function L(key, params)
	return language.getText(key, params)
end

-- listeners[event] = { {callback=fn, owner=string|nil}, ... }
local listeners = {}

function module.on(event, callback, owner)
	if type(event) ~= "string" or type(callback) ~= "function" then
		return
	end
	if not listeners[event] then
		listeners[event] = {}
	end
	local list = listeners[event]
	list[#list + 1] = { callback = callback, owner = owner }
end

function module.off(event, callback)
	local list = listeners[event]
	if not list then
		return
	end
	for i = #list, 1, -1 do
		if list[i].callback == callback then
			table.remove(list, i)
		end
	end
	if #list == 0 then
		listeners[event] = nil
	end
end

function module.offByOwner(owner)
	if not owner then
		return
	end
	for event, list in pairs(listeners) do
		for i = #list, 1, -1 do
			if list[i].owner == owner then
				table.remove(list, i)
			end
		end
		if #list == 0 then
			listeners[event] = nil
		end
	end
end

function module.emit(event, ...)
	local list = listeners[event]
	if not list then
		return
	end
	for i = 1, #list do
		local entry = list[i]
		if entry then
			local ok, err = pcall(entry.callback, ...)
			if not ok then
				print(L("event_bus.log.listener_error", {
					event = event,
					error = tostring(err),
				}))
			end
		end
	end
end

function module.clear()
	listeners = {}
end

function module.listenerCount(event)
	if event then
		local list = listeners[event]
		return list and #list or 0
	end
	local total = 0
	for _, list in pairs(listeners) do
		total = total + #list
	end
	return total
end

function module.onTerminate()
	module.clear()
end

return module
