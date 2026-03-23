local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}

local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local HotkeyManager = require("HelperByOrc.hotkey_manager")
local vk = require("vkeys")
local wm = require("windows.message")
local bit = require("bit")

local ctx

-- === Internal state ===
local active_coroutines = {} -- { hk, co, state, wake }
local binderKeyTracker = HotkeyManager.newKeyTracker()
local pressedKeysList = {}
local quickMenuHotkeyCapture = HotkeyManager.new({
	on_save = function(keys)
		if ctx and ctx.module and type(ctx.module.setQuickMenuHotkey) == "function" then
			ctx.module.setQuickMenuHotkey(keys)
		end
	end,
})
local TEXT_EVENT_SOURCE = {
	INCOMING_SERVER = "incoming_server",
	OUTGOING_CHAT = "outgoing_chat",
	OUTGOING_COMMAND = "outgoing_command",
}
local OUTGOING_TRIGGER_GUARD_TTL_MS = 2000
local OUTGOING_TRIGGER_GUARD_MAX = 64
local outgoingTriggerGuard = {
	chat = {},
	command = {},
}
local incomingEchoTriggerGuard = {}

-- === Scheduler coroutine ===
local function log_error(err)
	ctx.pushToast(err, "err", 5.0)
end

local scheduler = coroutine.create(function()
	while true do
		local now = os.clock() * 1000
		for i = #active_coroutines, 1, -1 do
			local item = active_coroutines[i]
			local state = item.state
			if state.stopped then
				item.hk.is_running = false
				item.hk._co_state = nil
				item.hk._awaiting_input = false
				table.remove(active_coroutines, i)
			elseif now >= item.wake then
				if state.paused then
					item.wake = now + 50
				else
					local ok, wait_ms = coroutine.resume(item.co)
					if not ok then
						log_error(wait_ms)
						item.hk.is_running = false
						item.hk._co_state = nil
						item.hk._awaiting_input = false
						table.remove(active_coroutines, i)
					elseif coroutine.status(item.co) == "dead" then
						item.hk.is_running = false
						item.hk._co_state = nil
						item.hk._awaiting_input = false
						table.remove(active_coroutines, i)
					else
						item.wake = now + (wait_ms or 0)
					end
				end
			end
		end
		coroutine.yield()
	end
end)

local function runScheduler()
	if coroutine.status(scheduler) ~= "dead" then
		local ok, err = coroutine.resume(scheduler)
		if not ok then
			log_error(err)
		end
	end
	local flushHotkeysDirty = ctx.flushHotkeysDirty
	if flushHotkeysDirty then
		flushHotkeysDirty(false)
	end
end

M.runScheduler = runScheduler

-- === ConditionSystem ===
local cond_labels, quick_cond_labels, cond_count, quick_cond_count
local condition_language_generation = -1
local BUILTIN_CONDITION_TEXT_KEYS = {
	in_water = {
		message = "binder.execution.text.text",
		label = "binder.execution.text.text_1",
		quick_label = "binder.execution.text.text_2",
	},
	dead = {
		message = "binder.execution.text.text_3",
		label = "binder.execution.text.text_4",
		quick_label = "binder.execution.text.text_5",
	},
	in_air = {
		message = "binder.execution.text.text_6",
		label = "binder.execution.text.text_7",
		quick_label = "binder.execution.text.text_8",
	},
	in_any_car = {
		message = "binder.execution.text.text_9",
		label = "binder.execution.text.text_10",
		quick_label = "binder.execution.text.text_11",
	},
	without_weapon = {
		message = "binder.execution.text.text_12",
		label = "binder.execution.text.text_13",
		quick_label = "binder.execution.text.text_14",
	},
	with_weapon = {
		message = "binder.execution.text.text_15",
		label = "binder.execution.text.text_16",
		quick_label = "binder.execution.text.text_17",
	},
	on_foot = {
		message = "binder.execution.text.text_18",
		label = "binder.execution.text.text_19",
		quick_label = "binder.execution.text.text_20",
	},
	chat_opened = {
		message = "binder.execution.text.text_21",
		label = "binder.execution.text.text_22",
		quick_label = "binder.execution.text.text_23",
	},
	dialog_opened = {
		message = "binder.execution.text.text_24",
		label = "binder.execution.text.text_25",
		quick_label = "binder.execution.text.text_26",
	},
}

local ConditionSystem = {
	order = {
		"in_water",
		"dead",
		"in_air",
		"in_any_car",
		"without_weapon",
		"with_weapon",
		"on_foot",
		"chat_opened",
		"dialog_opened",
	},

	conditions = {
		in_water = {
			check = function()
				return isCharInWater(PLAYER_PED)
			end,
			priority = 1,
			message = L("binder.execution.text.text"),
			label = L("binder.execution.text.text_1"),
			quick_label = L("binder.execution.text.text_2"),
		},
		dead = {
			check = function()
				return isCharDead(PLAYER_PED)
			end,
			priority = 2,
			message = L("binder.execution.text.text_3"),
			label = L("binder.execution.text.text_4"),
			quick_label = L("binder.execution.text.text_5"),
		},
		in_air = {
			check = function()
				return isCharInAir(PLAYER_PED)
			end,
			priority = 3,
			message = L("binder.execution.text.text_6"),
			label = L("binder.execution.text.text_7"),
			quick_label = L("binder.execution.text.text_8"),
		},
		in_any_car = {
			check = function()
				return isCharInAnyCar(PLAYER_PED)
			end,
			priority = 4,
			message = L("binder.execution.text.text_9"),
			label = L("binder.execution.text.text_10"),
			quick_label = L("binder.execution.text.text_11"),
		},
		without_weapon = {
			check = function()
				local weapon = getCurrentCharWeapon(PLAYER_PED) or 0
				return weapon == 0
			end,
			priority = 5,
			message = L("binder.execution.text.text_12"),
			label = L("binder.execution.text.text_13"),
			quick_label = L("binder.execution.text.text_14"),
		},
		with_weapon = {
			check = function()
				local weapon = getCurrentCharWeapon(PLAYER_PED) or 0
				return weapon ~= 0
			end,
			priority = 6,
			message = L("binder.execution.text.text_15"),
			label = L("binder.execution.text.text_16"),
			quick_label = L("binder.execution.text.text_17"),
		},
		on_foot = {
			check = function()
				return not isCharInAnyCar(PLAYER_PED)
			end,
			priority = 7,
			message = L("binder.execution.text.text_18"),
			label = L("binder.execution.text.text_19"),
			quick_label = L("binder.execution.text.text_20"),
		},

		chat_opened = {
			check = function()
				local samp_api = ctx and ctx.module and ctx.module.samp
				if type(samp_api) ~= "table" then
					return false
				end

				local backend = "standard"
				if type(samp_api.getBackendStatus) == "function" then
					local status = samp_api.getBackendStatus()
					if type(status) == "table" and status.active == "sampfuncs" then
						backend = "sampfuncs"
					end
				elseif type(samp_api.getBackendMode) == "function" then
					backend = tostring(samp_api.getBackendMode() or ""):lower()
				end

				if backend == "sampfuncs" and type(sampIsChatInputActive) == "function" then
					local ok, result = pcall(sampIsChatInputActive)
					if ok then
						return result and true or false
					end
				end

				if type(samp_api.is_chat_opened) == "function" then
					local ok, result = pcall(samp_api.is_chat_opened)
					if ok then
						return result and true or false
					end
				end

				return false
			end,
			priority = 8,
			message = L("binder.execution.text.text_21"),
			label = L("binder.execution.text.text_22"),
			quick_label = L("binder.execution.text.text_23"),
		},
		dialog_opened = {
			check = function()
				local samp_api = ctx and ctx.module and ctx.module.samp
				if type(samp_api) ~= "table" then
					return false
				end

				local backend = "standard"
				if type(samp_api.getBackendStatus) == "function" then
					local status = samp_api.getBackendStatus()
					if type(status) == "table" and status.active == "sampfuncs" then
						backend = "sampfuncs"
					end
				elseif type(samp_api.getBackendMode) == "function" then
					backend = tostring(samp_api.getBackendMode() or ""):lower()
				end

				if backend == "sampfuncs" and type(sampIsDialogActive) == "function" then
					local ok, result = pcall(sampIsDialogActive)
					if ok then
						return result and true or false
					end
				end

				if type(samp_api.isDialogActive) == "function" then
					local ok, result = pcall(samp_api.isDialogActive)
					if ok then
						return result and true or false
					end
				end

				return false
			end,
			priority = 9,
			message = L("binder.execution.text.text_24"),
			label = L("binder.execution.text.text_25"),
			quick_label = L("binder.execution.text.text_26"),
		},
		custom = {},
	},

	register_condition = function(self, name, check_fn, priority, message, label, quick_label)
		self.conditions.custom[name] = {
			check = check_fn,
			priority = priority or 10,
			message = message or L("binder.execution.text.text_27"),
			label = label,
			quick_label = quick_label,
		}
		table.insert(self.order, name)
		self:refresh_labels()
	end,

	refresh_labels = function(self)
		self.labels = {}
		self.quick_labels = {}
		for _, cond_name in ipairs(self.order) do
			local cond = self.conditions[cond_name] or self.conditions.custom[cond_name]
			if cond then
				table.insert(self.labels, cond.label or cond.message or cond_name)
				table.insert(self.quick_labels, cond.quick_label or cond.label or cond.message or cond_name)
			end
		end
		cond_labels = self.labels
		quick_cond_labels = self.quick_labels
		cond_count = #self.labels
		quick_cond_count = #self.quick_labels
	end,

	flags_to_names = function(self, flags)
		local names = {}
		if not flags then
			return names
		end
		for idx, cond_name in ipairs(self.order) do
			local value = flags[idx]
			local enabled = value
			if type(value) == "table" then
				enabled = value[0]
			end
			if enabled then
				table.insert(names, cond_name)
			end
		end
		return names
	end,

	check_all = function(self, condition_list, context)
		local results = {}
		for _, cond_name in ipairs(condition_list or {}) do
			local cond = self.conditions[cond_name] or self.conditions.custom[cond_name]
			if cond then
				local ok, result = pcall(cond.check, context)
				if ok and result then
					table.insert(results, {
						name = cond_name,
						failed = true,
						message = cond.message,
						priority = cond.priority or 0,
					})
				end
			end
		end
		table.sort(results, function(a, b)
			return (a.priority or 0) < (b.priority or 0)
		end)
		return results
	end,
}

local function ensure_condition_language_cache()
	if condition_language_generation == language.getGeneration() then
		return
	end

	for name, keys in pairs(BUILTIN_CONDITION_TEXT_KEYS) do
		local cond = ConditionSystem.conditions[name]
		if cond then
			cond.message = L(keys.message)
			cond.label = L(keys.label)
			cond.quick_label = L(keys.quick_label)
		end
	end

	ConditionSystem:refresh_labels()
	condition_language_generation = language.getGeneration()
end

ensure_condition_language_cache()

M.ConditionSystem = ConditionSystem

local function conditions_ok(conds, opts)
	local cond_names = ConditionSystem:flags_to_names(conds)
	local results = ConditionSystem:check_all(cond_names)
	if results and #results > 0 then
		if not (opts and opts.silent) then
			local top = results[1]
			if top and top.message then
				ctx.pushToast(top.message, "warn", 3.0)
			end
		end
		return false
	end
	return true
end

M.conditions_ok = conditions_ok

local function check_quick_visibility(conds)
	return conditions_ok(conds, { silent = true })
end

M.check_quick_visibility = check_quick_visibility

local function isFolderChainVisible(folder)
	local node = folder
	while node do
		if node.quick_menu == false then
			return false
		end
		if not check_quick_visibility(node.quick_conditions or {}) then
			return false
		end
		node = node.parent
	end
	return true
end

M.isFolderChainVisible = isFolderChainVisible

function M.getCondLabels()
	ensure_condition_language_cache()
	return cond_labels, cond_count
end

function M.getQuickCondLabels()
	ensure_condition_language_cache()
	return quick_cond_labels, quick_cond_count
end

-- === Отправка сообщений ===
local function change_tags_ignore_colors(text, thisbind_value)
	local tags = ctx.tags
	if not (tags and tags.change_tags) then
		return text
	end
	local colors = {}
	local idx = 0
	text = text:gsub("{%x%x%x%x%x%x}", function(code)
		idx = idx + 1
		local token = "__COLOR" .. idx .. "__"
		colors[token] = code
		return token
	end)
	text = tags.change_tags(text, thisbind_value)
	for token, code in pairs(colors) do
		text = text:gsub(token, code)
	end
	return text
end

local function apply_input_values(text, values)
	if not text or text == "" then
		return text
	end
	if not values then
		return text
	end
	return text:gsub("{{([%w_]+)}}", function(key)
		local replacement = values[key] or values[key:lower()] or values[key:upper()]
		if replacement == nil then
			return ""
		end
		return replacement
	end)
end

M.apply_input_values = apply_input_values

local function parse_leading_chat_color(text)
	local color_hex, rest = text:match("^%{([%x][%x][%x][%x][%x][%x][%x][%x])%}(.*)$")
	if not color_hex then
		color_hex, rest = text:match("^%{([%x][%x][%x][%x][%x][%x])%}(.*)$")
	end
	if color_hex then
		local parsed_color = tonumber(color_hex, 16)
		if parsed_color then
			return rest, parsed_color
		end
	end
	return text, -1
end

local function normalize_text_event_value(text)
	local s = tostring(text or "")
	s = s:gsub("\r\n", "\n"):gsub("\r", "\n")
	local trim = ctx and ctx.trim
	if trim then
		s = trim(s)
	else
		s = s:gsub("^%s+", ""):gsub("%s+$", "")
	end
	return s
end

local function prune_outgoing_trigger_guard_bucket(bucket, now_ms)
	for i = #bucket, 1, -1 do
		local item = bucket[i]
		if not item or now_ms >= (tonumber(item.expire_at) or 0) then
			table.remove(bucket, i)
		end
	end
end

local function register_binder_outgoing_text_guard(text, thisbind_value)
	local normalized = normalize_text_event_value(text)
	if normalized == "" then
		return
	end

	local kind = normalized:sub(1, 1) == "/" and "command" or "chat"
	local now_ms = os.clock() * 1000
	local bucket = outgoingTriggerGuard[kind]
	local chain_depth = (tonumber(thisbind_value and thisbind_value._active_trigger_chain_depth) or 0) + 1
	prune_outgoing_trigger_guard_bucket(bucket, now_ms)
	bucket[#bucket + 1] = {
		text = normalized,
		chain_depth = chain_depth,
		source_hk = thisbind_value,
		expire_at = now_ms + OUTGOING_TRIGGER_GUARD_TTL_MS,
	}
	while #bucket > OUTGOING_TRIGGER_GUARD_MAX do
		table.remove(bucket, 1)
	end
end

local function register_incoming_echo_trigger_guard(text)
	local normalized = normalize_text_event_value(text)
	if normalized == "" then
		return
	end

	local now_ms = os.clock() * 1000
	prune_outgoing_trigger_guard_bucket(incomingEchoTriggerGuard, now_ms)
	incomingEchoTriggerGuard[#incomingEchoTriggerGuard + 1] = {
		text = normalized,
		expire_at = now_ms + OUTGOING_TRIGGER_GUARD_TTL_MS,
	}
	while #incomingEchoTriggerGuard > OUTGOING_TRIGGER_GUARD_MAX do
		table.remove(incomingEchoTriggerGuard, 1)
	end
end

local function consume_binder_outgoing_text_guard(text, source_kind)
	local kind = nil
	if source_kind == TEXT_EVENT_SOURCE.OUTGOING_CHAT then
		kind = "chat"
	elseif source_kind == TEXT_EVENT_SOURCE.OUTGOING_COMMAND then
		kind = "command"
	end
	if not kind then
		return false
	end

	local normalized = normalize_text_event_value(text)
	if normalized == "" then
		return false
	end

	local now_ms = os.clock() * 1000
	local bucket = outgoingTriggerGuard[kind]
	prune_outgoing_trigger_guard_bucket(bucket, now_ms)
	for i = 1, #bucket do
		local item = bucket[i]
		if item and item.text == normalized then
			table.remove(bucket, i)
			return item
		end
	end
	return nil
end

local function consume_incoming_echo_trigger_guard(text)
	local normalized = normalize_text_event_value(text)
	if normalized == "" then
		return false
	end

	local now_ms = os.clock() * 1000
	prune_outgoing_trigger_guard_bucket(incomingEchoTriggerGuard, now_ms)
	for i = 1, #incomingEchoTriggerGuard do
		local item = incomingEchoTriggerGuard[i]
		if item and item.text == normalized then
			table.remove(incomingEchoTriggerGuard, i)
			return true
		end
	end
	return false
end

local function doSend(msg, method, thisbind_value, state)
	local s_utf8 = tostring(msg or "")
	method = tonumber(method) or 0
	local tags = ctx.tags

	if tags and tags.change_tags then
		if method == 0 then
			s_utf8 = change_tags_ignore_colors(s_utf8, thisbind_value)
		else
			s_utf8 = tags.change_tags(s_utf8, thisbind_value)
		end
	end

	if s_utf8 == nil then
		s_utf8 = ""
	end

	local s = u8:decode(s_utf8)
	local pushToast = ctx.pushToast
	local module = ctx.module

	if method == 0 then
		local text, color = parse_leading_chat_color(s)
		sampAddChatMessage(text, color)
	elseif method == 1 then
		register_binder_outgoing_text_guard(s_utf8, thisbind_value)
		register_incoming_echo_trigger_guard(s_utf8)
		sampProcessChatInput(s)
	elseif method == 2 then
		register_binder_outgoing_text_guard(s_utf8, thisbind_value)
		register_incoming_echo_trigger_guard(s_utf8)
		sampSendChat(s)
	elseif method == 4 then
		local ok = false
		if type(sampSetChatInputEnabled) == "function" and type(sampSetChatInputText) == "function" then
			ok = pcall(function()
				sampSetChatInputEnabled(true)
				sampSetChatInputText(s)
			end)
			pcall(sampSetChatInputEnabled, false)
		end
		if not ok then
			pushToast(L("binder.execution.text.text_28"), "warn", 3.0)
		end
	elseif method == 5 then
		local ok = false
		if type(sampSetChatInputEnabled) == "function" and type(sampSetChatInputText) == "function" then
			ok = pcall(function()
				sampSetChatInputEnabled(true)
				sampSetChatInputText(s)
			end)
		end
		if not ok then
			pushToast(L("binder.execution.text.text_29"), "warn", 3.0)
		end
	elseif method == 6 then
		local samp_api = module.samp
		local reason
		local ok = false

		if not samp_api then
			reason = "samp_missing"
		elseif type(samp_api.sampSetDialogEditboxText) ~= "function" or type(samp_api.isDialogActive) ~= "function" then
			reason = "api_missing"
		else
			local ready = samp_api.isDialogActive()
			if not ready then
				local can_yield = coroutine.running() ~= nil
				local started_at = os.clock() * 1000
				while (os.clock() * 1000 - started_at) < 3000 do
					if state and state.stopped then
						reason = "stopped"
						break
					end
					if samp_api.isDialogActive() then
						ready = true
						break
					end
					if not can_yield then
						break
					end
					coroutine.yield(50)
				end
			end

			if not reason then
				if ready then
					local ok_set, err = pcall(samp_api.sampSetDialogEditboxText, s)
					ok = ok_set
					if not ok then
						reason = tostring(err)
					end
				else
					reason = "timeout"
				end
			end
		end

		if not ok then
			if reason == "timeout" then
				pushToast(L("binder.execution.text.text_3_30"), "warn", 3.0)
			elseif reason == "stopped" then
				return false
			elseif reason == "samp_missing" then
				pushToast(L("binder.execution.text.samp"), "warn", 3.0)
			elseif reason == "api_missing" then
				pushToast(L("binder.execution.text.api"), "warn", 3.0)
			else
				pushToast(L("binder.execution.text.text_31") .. tostring(reason), "warn", 3.0)
			end
		end
	elseif method == 7 then
		local ok = false
		if type(setClipboardTextUTF8) == "function" then
			local ok_call, result = pcall(setClipboardTextUTF8, s_utf8)
			if ok_call then
				ok = result ~= false
			end
		end
		if not ok and type(setClipboardText) == "function" then
			local ok_call, result = pcall(setClipboardText, s)
			if ok_call then
				ok = result ~= false
			end
		end
		if not ok then
			pushToast(L("binder.execution.text.text_32"), "warn", 3.0)
		end
	elseif method == 8 then
		pcall(print, s)
	elseif method == 9 then
		pushToast(s_utf8, "ok")
	elseif method == 3 then
		-- Без отправки: строка уже обработана тегами.
	end

	return true
end

M.doSend = doSend

local function get_text_confirmation(hk)
	local clone_text_confirmation = ctx and ctx.clone_text_confirmation
	if type(clone_text_confirmation) == "function" then
		return clone_text_confirmation(hk and hk.text_confirmation)
	end
	local C = ctx and ctx.C or {}
	local cfg = type(hk and hk.text_confirmation) == "table" and hk.text_confirmation or {}
	return {
		enabled = cfg.enabled == true,
		key = tonumber(cfg.key) or C.DEFAULT_TEXT_CONFIRM_KEY or 0x31,
		cancel_key = tonumber(cfg.cancel_key) or C.DEFAULT_TEXT_CANCEL_KEY or 0x32,
		wait_for_resolution = cfg.wait_for_resolution ~= false,
	}
end

local function clear_text_confirmation_wait(hk, clear_pending_trigger)
	if not hk then
		return
	end
	hk._awaiting_text_confirmation = nil
	if clear_pending_trigger then
		hk._pending_chat_trigger = nil
	end
end

local function get_text_confirmation_prompt(hk)
	local text_confirmation_key_label = ctx and ctx.text_confirmation_key_label
	local confirm = get_text_confirmation(hk)
	local confirm_label = type(text_confirmation_key_label) == "function"
		and text_confirmation_key_label(confirm.key)
		or tostring(confirm.key or "")
	local cancel_label = type(text_confirmation_key_label) == "function"
		and text_confirmation_key_label(confirm.cancel_key)
		or tostring(confirm.cancel_key or "")
	local prompt = string.format(
		L("binder.execution.text.format_format"),
		confirm_label,
		cancel_label
	)
	if not confirm.wait_for_resolution then
		prompt = prompt .. string.format(L("binder.execution.text.text_1f"), (ctx.C.TEXT_CONFIRM_TIMEOUT_MS or 2000) / 1000)
	end
	return prompt
end

-- === Корутины отправки ===
function M.sendHotkeyCoroutine(hk, state)
	local messages = hk.messages
	for idx, msg in ipairs(messages) do
		if state.stopped then
			return
		end
		local text = msg.text or ""
		local final_str = apply_input_values(text, state and state.inputs)
		if final_str and final_str:match("%S") then
			doSend(final_str, msg.method or 0, hk, state)
		end
		if idx < #messages then
			local interval = tonumber(msg.interval) or 0
			if interval < 50 then
				interval = 50
			end
			while state.paused do
				if state.stopped then
					return
				end
				coroutine.yield(50)
			end
			coroutine.yield(interval)
		end
	end
end

local function startHotkeyCoroutine(hk, delay_ms, input_values)
	local C = ctx.C
	local module = ctx.module
	if not (hk.messages and #hk.messages > 0) then
		return false
	end
	if #active_coroutines >= C.MAX_ACTIVE_HOTKEYS then
		ctx.pushToast(L("binder.execution.text.text_33"), "warn", 3.0)
		return false
	end
	local state = { paused = false, idx = 1, stopped = false, inputs = input_values or {} }
	clear_text_confirmation_wait(hk)
	local pending_trigger = hk._pending_chat_trigger
	local active_trigger_chain_depth = 0
	if type(pending_trigger) == "table" then
		hk._active_chat_trigger_text = tostring(pending_trigger.text or "")
		hk._active_chat_trigger_pattern = tostring(pending_trigger.pattern or "")
		hk._active_chat_trigger_at = tonumber(pending_trigger.at) or nil
		active_trigger_chain_depth = tonumber(pending_trigger.depth) or active_trigger_chain_depth
		local pending_source = tostring(pending_trigger.source or "")
		hk._active_chat_trigger_source = pending_source ~= "" and pending_source or nil
	else
		hk._active_chat_trigger_text = nil
		hk._active_chat_trigger_pattern = nil
		hk._active_chat_trigger_at = nil
		hk._active_chat_trigger_source = nil
	end
	local pending_command = hk._pending_command_trigger
	if type(pending_command) == "table" then
		hk._active_command_trigger_text = tostring(pending_command.text or "")
		hk._active_command_trigger_command = tostring(pending_command.command or hk.command or "")
		active_trigger_chain_depth = tonumber(pending_command.depth) or active_trigger_chain_depth
	else
		hk._active_command_trigger_text = nil
		hk._active_command_trigger_command = nil
	end
	hk._active_trigger_chain_depth = active_trigger_chain_depth > 0 and active_trigger_chain_depth or nil
	hk._pending_chat_trigger = nil
	hk._pending_command_trigger = nil
	hk._co_state = state
	hk.is_running = true
	hk._awaiting_input = false
	local co = coroutine.create(function()
		if delay_ms and delay_ms > 0 then
			coroutine.yield(delay_ms)
		end
		M.sendHotkeyCoroutine(hk, state)
	end)
	table.insert(active_coroutines, { hk = hk, co = co, state = state, wake = 0 })
	return true
end

M.startHotkeyCoroutine = startHotkeyCoroutine

function M.enqueueHotkey(hk, delay_ms)
	if hk.is_running or hk._awaiting_input or not hk.enabled then
		clear_text_confirmation_wait(hk)
		hk._pending_chat_trigger = nil
		hk._pending_command_trigger = nil
		return false
	end
	if not conditions_ok(hk.conditions) then
		clear_text_confirmation_wait(hk)
		hk._pending_chat_trigger = nil
		hk._pending_command_trigger = nil
		return false
	end
	clear_text_confirmation_wait(hk)
	if hk.inputs and #hk.inputs > 0 then
		local input_dialog = ctx.input_dialog
		local activeDialog = input_dialog.getActiveInputDialog()
		if activeDialog and activeDialog.hk ~= hk then
			ctx.pushToast(
				L("binder.execution.text.text_34"),
				"warn",
				3.0
			)
			clear_text_confirmation_wait(hk)
			hk._pending_chat_trigger = nil
			hk._pending_command_trigger = nil
			return false
		end
		if input_dialog.openInputDialog(hk, delay_ms) then
			return true
		end
	end
	if hk.messages and #hk.messages > 0 then
		return startHotkeyCoroutine(hk, delay_ms, nil)
	end
	hk._pending_chat_trigger = nil
	hk._pending_command_trigger = nil
	return false
end

-- === Текстовые триггеры ===
local function normalize_text_trigger_value(text)
	return normalize_text_event_value(text)
end

local function normalize_hex_tag_case(text)
	return tostring(text or ""):gsub("{(%x%x%x%x%x%x)}", function(hex)
		return "{" .. tostring(hex):lower() .. "}"
	end)
end

local function strip_hex_tags(text)
	return tostring(text or ""):gsub("{%x%x%x%x%x%x}", "")
end

local function equals_text_trigger(a, b)
	a = normalize_text_trigger_value(a)
	b = normalize_text_trigger_value(b)
	if a == b then
		return true
	end
	local ac = normalize_hex_tag_case(a)
	local bc = normalize_hex_tag_case(b)
	if ac == bc then
		return true
	end
	return normalize_text_trigger_value(strip_hex_tags(ac)) == normalize_text_trigger_value(strip_hex_tags(bc))
end

local function match_text_trigger_message(message_text, trig, hk, now_ms)
	local source = normalize_text_trigger_value(message_text)
	local target = normalize_text_trigger_value(trig and trig.text or "")
	if target == "" then
		return false, false, source
	end

	if trig and trig.pattern then
		local ok, matched = pcall(string.match, source, target)
		if ok and matched ~= nil then
			return true, true, source
		end
		if equals_text_trigger(source, target) then
			return true, false, source
		end
		if not ok then
			local prev_err = hk and hk._last_trigger_pattern_error or nil
			local prev_at = hk and hk._last_trigger_pattern_error_at or 0
			if hk and (prev_err ~= tostring(matched) or (now_ms - prev_at) > 1000) then
				hk._last_trigger_pattern_error = tostring(matched)
				hk._last_trigger_pattern_error_at = now_ms
				log_error(L("binder.execution.text.lua") .. tostring(hk.label or "") .. "': " .. tostring(matched))
			end
		end
		return false, false, source
	end

	return equals_text_trigger(source, target), false, source
end

local function activate_text_trigger_hotkey(hk, trig, source_text, matched_by_pattern, source_kind, now_ms)
	local C = ctx.C
	local pending_chat_trigger = {
		text = source_text,
		pattern = matched_by_pattern and trig.text or nil,
		at = now_ms,
		source = source_kind,
	}
	local text_confirmation = get_text_confirmation(hk)
	if text_confirmation.enabled then
		if
			hk.enabled
			and not hk.is_running
			and not hk._awaiting_input
			and not hk._awaiting_text_confirmation
			and conditions_ok(hk.conditions)
		then
			hk._pending_chat_trigger = pending_chat_trigger
			hk._awaiting_text_confirmation = {
				key = text_confirmation.key,
				cancel_key = text_confirmation.cancel_key,
				at = now_ms,
				timeout_at = text_confirmation.wait_for_resolution and nil
					or (now_ms + (C.TEXT_CONFIRM_TIMEOUT_MS or 2000)),
			}
			ctx.pushToast(
				get_text_confirmation_prompt(hk),
				"warn",
				text_confirmation.wait_for_resolution and 4.0 or math.max(2.0, (C.TEXT_CONFIRM_TIMEOUT_MS or 2000) / 1000)
			)
		else
			clear_text_confirmation_wait(hk, true)
		end
	else
		hk._pending_chat_trigger = pending_chat_trigger
		M.enqueueHotkey(hk)
	end
end

local function handle_text_trigger_event(text, source_kind)
	if source_kind == TEXT_EVENT_SOURCE.INCOMING_SERVER and consume_incoming_echo_trigger_guard(text) then
		return false, false
	end

	local hotkeys = ctx.hotkeys
	local C = ctx.C
	local nowMs = os.clock() * 1000
	local handled = false
	for _, hk in ipairs(hotkeys) do
		local trig = hk.text_trigger
		if trig and trig.enabled and trig.text and trig.text ~= "" then
			local matched, matched_by_pattern, source_text = match_text_trigger_message(text, trig, hk, nowMs)
			if matched then
				if not hk._debounce_until or nowMs >= hk._debounce_until then
					activate_text_trigger_hotkey(hk, trig, source_text, matched_by_pattern, source_kind, nowMs)
					hk._debounce_until = nowMs + C.DEBOUNCE_MS
					handled = true
				end
			end
		end
	end
	return handled, false
end

function M.onIncomingTextMessage(text)
	handle_text_trigger_event(text, TEXT_EVENT_SOURCE.INCOMING_SERVER)
end

function M.onOutgoingChatInput(text)
	if consume_binder_outgoing_text_guard(text, TEXT_EVENT_SOURCE.OUTGOING_CHAT) then
		return false
	end
	return handle_text_trigger_event(text, TEXT_EVENT_SOURCE.OUTGOING_CHAT)
end

function M.onServerMessage(text)
	M.onIncomingTextMessage(text)
end

local function expire_pending_text_confirmations(now_ms)
	local handled = false
	for _, hk in ipairs(ctx.hotkeys) do
		local pending = hk._awaiting_text_confirmation
		if pending and pending.timeout_at and now_ms >= pending.timeout_at then
			clear_text_confirmation_wait(hk, true)
			ctx.pushToast(L("binder.execution.text.text_35") .. tostring(hk.label or ""), "warn", 3.0)
			handled = true
		end
	end
	return handled
end

local function activate_pending_text_confirmations(key_code)
	local handled = false
	for _, hk in ipairs(ctx.hotkeys) do
		local pending = hk._awaiting_text_confirmation
		if pending and hk.enabled then
			if tonumber(pending.key) == tonumber(key_code) then
				clear_text_confirmation_wait(hk)
				M.enqueueHotkey(hk)
				handled = true
			elseif tonumber(pending.cancel_key) == tonumber(key_code) then
				clear_text_confirmation_wait(hk, true)
				ctx.pushToast(L("binder.execution.text.text_36") .. tostring(hk.label or ""), "warn", 3.0)
				handled = true
			end
		elseif pending then
			clear_text_confirmation_wait(hk, true)
		end
	end
	return handled
end

local function normalizeActivationText(text)
	if type(text) ~= "string" then
		return ""
	end
	local trim = ctx.trim
	if trim then
		return trim(text)
	end
	return text:gsub("^%s+", ""):gsub("%s+$", "")
end

local function matchesActivationCommand(inputText, commandText)
	local input = normalizeActivationText(inputText)
	local command = normalizeActivationText(commandText)
	if input == "" or command == "" then
		return false
	end
	local len = #command
	return input:sub(1, len) == command and (input:len() == len or input:sub(len + 1, len + 1) == " ")
end

local function handle_command_activation_input(cmd, opts)
	opts = opts or {}
	local hotkeys = ctx.hotkeys
	local C = ctx.C
	local nowMs = os.clock() * 1000
	local chain_depth = tonumber(opts.chain_depth) or 0
	local handled = false
	if chain_depth > C.MAX_BIND_DEPTH then
		ctx.pushToast(L("binder.execution.text.runbind") .. C.MAX_BIND_DEPTH .. ")", "warn", 3.0)
		return false
	end
	for _, hk in ipairs(hotkeys) do
		if hk.command_enabled and not hk.is_running and matchesActivationCommand(cmd, hk.command) then
			if not hk._debounce_until or nowMs >= hk._debounce_until then
				hk._pending_command_trigger = {
					text = tostring(cmd or ""),
					command = tostring(hk.command or ""),
					depth = chain_depth,
				}
				M.enqueueHotkey(hk)
				hk._debounce_until = nowMs + C.DEBOUNCE_MS
				handled = true
			end
		end
	end
	return handled
end

function M.onOutgoingCommandInput(cmd)
	local guard_item = consume_binder_outgoing_text_guard(cmd, TEXT_EVENT_SOURCE.OUTGOING_COMMAND)
	local text_handled = false
	if not guard_item then
		text_handled = handle_text_trigger_event(cmd, TEXT_EVENT_SOURCE.OUTGOING_COMMAND)
	end
	local command_handled = handle_command_activation_input(cmd, {
		chain_depth = guard_item and guard_item.chain_depth or 0,
		source_hk = guard_item and guard_item.source_hk or nil,
	})
	return text_handled or command_handled
end

function M.onPlayerCommand(cmd)
	return handle_command_activation_input(cmd)
end

-- === Stop ===
function M.stopHotkey(hk)
	local input_dialog = ctx.input_dialog
	local activeDialog = input_dialog.getActiveInputDialog()
	if activeDialog and activeDialog.hk == hk then
		input_dialog.cancelInputDialog()
	end
	clear_text_confirmation_wait(hk, true)
	local state = hk._co_state
	if state then
		state.stopped = true
		hk.is_running = false
		hk._co_state = nil
	end
end

function M.stopAllHotkeys()
	ctx.input_dialog.cancelInputDialog()
	for _, hk in ipairs(ctx.hotkeys) do
		clear_text_confirmation_wait(hk, true)
	end
	for i = 1, #active_coroutines do
		local info = active_coroutines[i]
		info.state.stopped = true
		info.hk.is_running = false
		info.hk._co_state = nil
	end
end

-- === Quick menu frame state builder ===
function M.buildQuickMenuFrameState()
	local hotkeys = ctx.hotkeys
	local pathKey = ctx.pathKey
	local folderFullPath = ctx.folderFullPath

	local quickByFolderKey = {}
	for i, hk in ipairs(hotkeys) do
		if hk.quick_menu and hk.folderPath and #hk.folderPath > 0 then
			local key = pathKey(hk.folderPath)
			if key ~= "" then
				local bucket = quickByFolderKey[key]
				if not bucket then
					bucket = {}
					quickByFolderKey[key] = bucket
				end
				bucket[#bucket + 1] = { hk = hk, idx = i }
			end
		end
	end

	local folderKeyMemo = setmetatable({}, { __mode = "k" })
	local folderVisibleMemo = setmetatable({}, { __mode = "k" })
	local folderHasVisibleMemo = setmetatable({}, { __mode = "k" })
	local quickVisibleEntriesMemo = setmetatable({}, { __mode = "k" })

	local function folderKey(node)
		local key = folderKeyMemo[node]
		if key ~= nil then
			return key
		end
		key = pathKey(folderFullPath(node))
		folderKeyMemo[node] = key
		return key
	end

	local function isFolderVisible(node)
		local cached = folderVisibleMemo[node]
		if cached ~= nil then
			return cached
		end
		local visible = true
		if node.quick_menu == false then
			visible = false
		elseif not check_quick_visibility(node.quick_conditions or {}) then
			visible = false
		elseif node.parent and not isFolderVisible(node.parent) then
			visible = false
		end
		folderVisibleMemo[node] = visible
		return visible
	end

	local function quickEntriesFor(node)
		local entries = quickVisibleEntriesMemo[node]
		if entries then
			return entries
		end
		entries = {}
		local bucket = quickByFolderKey[folderKey(node)]
		if bucket then
			for _, entry in ipairs(bucket) do
				if check_quick_visibility(entry.hk.quick_conditions or {}) then
					entries[#entries + 1] = entry
				end
			end
		end
		quickVisibleEntriesMemo[node] = entries
		return entries
	end

	local function folderHasQuickBindsVisible(node)
		local cached = folderHasVisibleMemo[node]
		if cached ~= nil then
			return cached
		end
		if not isFolderVisible(node) then
			folderHasVisibleMemo[node] = false
			return false
		end
		if #quickEntriesFor(node) > 0 then
			folderHasVisibleMemo[node] = true
			return true
		end
		for _, child in ipairs(node.children or {}) do
			if folderHasQuickBindsVisible(child) then
				folderHasVisibleMemo[node] = true
				return true
			end
		end
		folderHasVisibleMemo[node] = false
		return false
	end

	return {
		folderKey = folderKey,
		isFolderVisible = isFolderVisible,
		quickEntriesFor = quickEntriesFor,
		folderHasQuickBindsVisible = folderHasQuickBindsVisible,
	}
end

local function hasVisibleQuickMenuEntries()
	local folders = ctx.folders
	local quickFrameState = M.buildQuickMenuFrameState()
	for _, folder in ipairs(folders) do
		if quickFrameState.folderHasQuickBindsVisible(folder) then
			return true
		end
	end
	return false
end

-- === Bind-tag system ===
local function bind_tag_trim(s)
	local trim = ctx.trim
	if trim then
		return trim(s or "")
	end
	return tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", "")
end

local function bind_tag_quote_token(s)
	s = tostring(s or "")
	s = s:gsub("\\", "\\\\"):gsub('"', '\\"')
	return '"' .. s .. '"'
end

local function bind_tag_tokenize_args(raw)
	local s = tostring(raw or "")
	local out = {}
	local i = 1
	local n = #s

	while i <= n do
		while i <= n do
			local ch = s:sub(i, i)
			if ch == "," or ch:match("%s") then
				i = i + 1
			else
				break
			end
		end
		if i > n then
			break
		end

		local ch = s:sub(i, i)
		if ch == '"' or ch == "'" then
			local q = ch
			local buf = {}
			local esc = false
			i = i + 1
			while i <= n do
				local c = s:sub(i, i)
				if esc then
					buf[#buf + 1] = c
					esc = false
				elseif c == "\\" then
					esc = true
				elseif c == q then
					i = i + 1
					break
				else
					buf[#buf + 1] = c
				end
				i = i + 1
			end
			out[#out + 1] = { value = table.concat(buf), quoted = true }
		else
			local start_i = i
			while i <= n do
				local c = s:sub(i, i)
				if c == "," or c:match("%s") then
					break
				end
				i = i + 1
			end
			local token = s:sub(start_i, i - 1)
			if token ~= "" then
				out[#out + 1] = { value = token, quoted = false }
			end
		end
	end

	return out
end

local function bind_tag_hotkey_name(hk)
	local name = bind_tag_trim(hk and hk.label or "")
	if name ~= "" then
		return name
	end
	ctx.refreshHotkeyNumbers()
	local num = hk and hk._number
	if num then
		return tostring(num)
	end
	return ""
end

local function bind_tag_hotkey_folder(hk)
	if not hk then
		return ""
	end
	return ctx.hotkeyFolderString(hk)
end

local function bind_tag_context_desc(thisbind_value)
	if type(thisbind_value) ~= "table" then
		return nil
	end
	if type(thisbind_value.folderPath) == "table" then
		return {
			hk = thisbind_value,
			name = bind_tag_hotkey_name(thisbind_value),
			folder = bind_tag_hotkey_folder(thisbind_value),
		}
	end
	local name = bind_tag_trim(thisbind_value.name or thisbind_value.label or "")
	local folder = bind_tag_trim(thisbind_value.folder or thisbind_value.folder_path or "")
	if name ~= "" then
		return { hk = nil, name = name, folder = folder }
	end
	return nil
end

function M.getThisbindTagValue(thisbind_value)
	local desc = bind_tag_context_desc(thisbind_value)
	if not desc or desc.name == "" then
		return ""
	end
	local out = bind_tag_quote_token(desc.name)
	if desc.folder and desc.folder ~= "" then
		out = out .. " " .. bind_tag_quote_token(desc.folder)
	end
	return out
end

local function bind_tag_collect_folders_in_order()
	local folders = ctx.folders
	local out = {}
	local function walk(node)
		out[#out + 1] = node
		for _, child in ipairs(node.children or {}) do
			walk(child)
		end
	end
	for _, root in ipairs(folders or {}) do
		walk(root)
	end
	return out
end

local function bind_tag_resolve_scope_folder_path(folder_query, folder_exact)
	local q = bind_tag_trim(folder_query or "")
	if q == "" then
		return nil, nil
	end
	q = q:lower()

	local folders = ctx.folders
	local folderFullPath = ctx.folderFullPath
	if not (folders and folders[1]) then
		return nil, "no_folders"
	end

	for _, node in ipairs(bind_tag_collect_folders_in_order()) do
		local path_tbl = folderFullPath(node)
		local full = table.concat(path_tbl, "/"):lower()
		local fname = tostring(node.name or ""):lower()
		local matched = false
		if folder_exact then
			matched = (fname == q) or (full == q)
		else
			matched = (fname:find(q, 1, true) ~= nil) or (full:find(q, 1, true) ~= nil)
		end
		if matched then
			return path_tbl, nil
		end
	end

	return nil, "folder_not_found"
end

local function bind_tag_collect_binds_in_exact_folder(scope_path)
	local hotkeys = ctx.hotkeys
	local pathEquals = ctx.pathEquals
	local out = {}
	if type(scope_path) ~= "table" or #scope_path == 0 then
		for _, hk in ipairs(hotkeys) do
			out[#out + 1] = hk
		end
		return out
	end
	for _, hk in ipairs(hotkeys) do
		if hk.folderPath and pathEquals(hk.folderPath, scope_path) then
			out[#out + 1] = hk
		end
	end
	return out
end

local function bind_tag_parse_selector(raw_param, thisbind_value, action)
	local tokens = bind_tag_tokenize_args(raw_param)
	local bt_ctx = bind_tag_context_desc(thisbind_value)

	local selector = {
		has_tokens = #tokens > 0,
		context_hk = bt_ctx and bt_ctx.hk or nil,
		folder = nil,
		folder_exact = false,
		all = false,
		by_index = false,
		index = nil,
		name = nil,
		name_exact = false,
	}

	if #tokens == 0 then
		if bt_ctx and bt_ctx.folder ~= "" then
			selector.folder = bt_ctx.folder
			selector.folder_exact = true
		end
		if action == "random" then
			selector.all = true
		end
		return selector
	end

	local first = tokens[1]
	local second = tokens[2]
	local first_value = tostring(first.value or "")

	if first_value == "*" then
		selector.all = true
	else
		local numeric = tonumber(first_value)
		if numeric and not first.quoted then
			selector.by_index = true
			selector.index = math.floor(numeric)
		else
			selector.name = first_value
			selector.name_exact = first.quoted and true or false
		end
	end

	if second and tostring(second.value or "") ~= "" then
		selector.folder = tostring(second.value)
		selector.folder_exact = second.quoted and true or false
	end

	if action == "random" and not selector.all and not selector.by_index and selector.name and not selector.folder then
		selector.folder = selector.name
		selector.folder_exact = selector.name_exact
		selector.name = nil
		selector.name_exact = false
		selector.all = true
	end

	return selector
end

local function bind_tag_resolve_targets(selector)
	local scope_path, folder_err = bind_tag_resolve_scope_folder_path(selector.folder, selector.folder_exact)
	if folder_err then
		return nil, folder_err
	end

	local candidates = bind_tag_collect_binds_in_exact_folder(scope_path)
	if selector.all then
		return candidates, nil
	end

	if selector.by_index then
		if not selector.index or selector.index < 1 then
			return {}, nil
		end
		ctx.refreshHotkeyNumbers()
		for _, hk in ipairs(candidates) do
			if hk._number == selector.index then
				return { hk }, nil
			end
		end
		return {}, nil
	end

	if selector.name and selector.name ~= "" then
		local q = tostring(selector.name):lower()
		local partial = nil
		for _, hk in ipairs(candidates) do
			local lbl = bind_tag_hotkey_name(hk):lower()
			if selector.name_exact then
				if lbl == q then
					return { hk }, nil
				end
			else
				if lbl == q then
					return { hk }, nil
				end
				if not partial and lbl:find(q, 1, true) then
					partial = hk
				end
			end
		end
		if partial then
			return { partial }, nil
		end
		return {}, nil
	end

	if selector.context_hk then
		for _, hk in ipairs(candidates) do
			if hk == selector.context_hk then
				return { hk }, nil
			end
		end
	end

	if #candidates > 0 then
		return { candidates[1] }, nil
	end

	return {}, nil
end

local function bind_tag_is_ended(hk)
	return not (hk and (hk.is_running or hk._awaiting_input))
end

function M.executeBindTagAction(action, raw_param, thisbind_value)
	action = tostring(action or ""):lower()
	local markHotkeysDirty = ctx.markHotkeysDirty

	if action == "stopall" then
		M.stopAllHotkeys()
		return true, 1, nil
	end

	local selector = bind_tag_parse_selector(raw_param, thisbind_value, action)
	if (not selector.has_tokens) and (action ~= "random") and (not selector.context_hk) then
		return false, nil, "param_required"
	end
	local targets, err = bind_tag_resolve_targets(selector)
	if not targets then
		return false, nil, err or "bind_not_found"
	end
	if #targets == 0 then
		return false, nil, "bind_not_found"
	end

	if action == "ended" then
		local ended = true
		for _, hk in ipairs(targets) do
			if not bind_tag_is_ended(hk) then
				ended = false
				break
			end
		end
		return true, ended and "1" or "0", nil
	end

	if action == "random" then
		local pool = {}
		for _, hk in ipairs(targets) do
			if hk.enabled then
				pool[#pool + 1] = hk
			end
		end
		if #pool == 0 then
			return false, nil, "bind_not_found"
		end
		local pick = pool[math.random(1, #pool)]
		local was_active = pick.is_running or pick._awaiting_input
		M.enqueueHotkey(pick)
		local now_active = pick.is_running or pick._awaiting_input
		if not was_active and now_active then
			return true, 1, nil
		end
		return false, nil, "bind_not_started"
	end

	if action == "popup" then
		local opened = 0
		local last_err = "bind_popup_unavailable"
		local requestBindLinesPopup = ctx.requestBindLinesPopup
		for _, hk in ipairs(targets) do
			if requestBindLinesPopup then
				local ok_open, open_err = requestBindLinesPopup(hk)
				if ok_open then
					opened = opened + 1
					break
				end
				if open_err and open_err ~= "" then
					last_err = open_err
				end
			end
		end
		return opened > 0, opened, opened > 0 and nil or last_err
	end

	if action == "start" then
		local started = 0
		for _, hk in ipairs(targets) do
			local was_active = hk.is_running or hk._awaiting_input
			if not was_active and hk.enabled then
				M.enqueueHotkey(hk)
				if hk.is_running or hk._awaiting_input then
					started = started + 1
				end
			end
		end
		return started > 0, started, started > 0 and nil or "bind_not_started"
	end

	if action == "stop" then
		local stopped = 0
		for _, hk in ipairs(targets) do
			local was_active = hk.is_running or hk._awaiting_input
			if was_active then
				M.stopHotkey(hk)
				stopped = stopped + 1
			end
		end
		return stopped > 0, stopped, stopped > 0 and nil or "bind_not_running"
	end

	if action == "pause" then
		local changed = 0
		for _, hk in ipairs(targets) do
			if hk.is_running and hk._co_state and not hk._co_state.paused then
				hk._co_state.paused = true
				changed = changed + 1
			end
		end
		return changed > 0, changed, changed > 0 and nil or "bind_not_running"
	end

	if action == "unpause" then
		local changed = 0
		for _, hk in ipairs(targets) do
			if hk.is_running and hk._co_state and hk._co_state.paused then
				hk._co_state.paused = false
				changed = changed + 1
			end
		end
		return changed > 0, changed, changed > 0 and nil or "bind_not_paused"
	end

	if action == "disable" then
		local changed = 0
		for _, hk in ipairs(targets) do
			if hk.enabled ~= false then
				hk.enabled = false
				changed = changed + 1
			end
		end
		if changed > 0 then
			markHotkeysDirty()
		end
		return changed > 0, changed, changed > 0 and nil or "bind_no_changes"
	end

	if action == "enable" then
		local changed = 0
		for _, hk in ipairs(targets) do
			if hk.enabled ~= true then
				hk.enabled = true
				changed = changed + 1
			end
		end
		if changed > 0 then
			markHotkeysDirty()
		end
		return changed > 0, changed, changed > 0 and nil or "bind_no_changes"
	end

	if action == "fastmenu" or action == "unfastmenu" then
		local desired = action == "fastmenu"
		local changed = 0
		for _, hk in ipairs(targets) do
			local current = hk.quick_menu ~= false
			if current ~= desired then
				hk.quick_menu = desired
				changed = changed + 1
			end
		end
		if changed > 0 then
			markHotkeysDirty()
		end
		return changed > 0, changed, changed > 0 and nil or "bind_no_changes"
	end

	return false, nil, "unknown_action"
end

-- === Public API ===
local function pathFromString(s)
	if not s or s == "" then
		return nil
	end
	local t = {}
	for part in tostring(s):gmatch("[^/]+") do
		t[#t + 1] = part
	end
	return #t > 0 and t or nil
end

local function collectBindsInFolder(pathTbl, recursive)
	local hotkeys = ctx.hotkeys
	local pathEquals = ctx.pathEquals
	local res = {}
	local function matchPath(hk)
		if not pathTbl or #pathTbl == 0 then
			return true
		end
		if not hk.folderPath then
			return false
		end
		if recursive then
			if #hk.folderPath < #pathTbl then
				return false
			end
			for i = 1, #pathTbl do
				if hk.folderPath[i] ~= pathTbl[i] then
					return false
				end
			end
			return true
		else
			return pathEquals(hk.folderPath, pathTbl)
		end
	end
	for _, hk in ipairs(hotkeys) do
		if hk.enabled and matchPath(hk) then
			table.insert(res, hk)
		end
	end
	return res
end

local function resolveBindForExecution(name, folder)
	if name == nil then
		return nil
	end
	local numericName = tonumber(name)
	if numericName and (folder == nil or folder == "") then
		return ctx.findHotkeyByNumberInScope(numericName, nil)
	end
	return M.findBind(name, folder)
end

function M.findBind(name, folder)
	if not name then
		return nil
	end
	local hotkeys = ctx.hotkeys
	local hotkeyFolderString = ctx.hotkeyFolderString
	local findHotkeyByNumberInScope = ctx.findHotkeyByNumberInScope
	local folderLower = folder and tostring(folder):lower() or nil
	local numericName = tonumber(name)
	if numericName then
		local hkByNumber = findHotkeyByNumberInScope(numericName, folderLower)
		if hkByNumber then
			return hkByNumber
		end
	end
	local query = tostring(name):lower()
	local partial
	for _, hk in ipairs(hotkeys) do
		local inFolder = true
		if folderLower and folderLower ~= "" then
			local fstr = hotkeyFolderString(hk):lower()
			inFolder = fstr:find(folderLower, 1, true) and true or false
		end
		if inFolder and hk.label then
			local lbl = hk.label:lower()
			if lbl == query then
				return hk
			elseif not partial and lbl:find(query, 1, true) then
				partial = hk
			end
		end
	end
	return partial
end

function M.startBind(name, folder)
	local hk = resolveBindForExecution(name, folder)
	if hk and not hk.is_running and hk.enabled then
		M.enqueueHotkey(hk)
		return true
	end
	return false
end

function M.stopBind(name, folder)
	local hk = M.findBind(name, folder)
	if hk and hk.is_running then
		M.stopHotkey(hk)
		return true
	end
	return false
end

function M.disableBind(name, folder)
	local hk = M.findBind(name, folder)
	if hk then
		hk.enabled = false
		clear_text_confirmation_wait(hk, true)
		ctx.markHotkeysDirty()
		return true
	end
	return false
end

function M.enableBind(name, folder)
	local hk = M.findBind(name, folder)
	if hk then
		hk.enabled = true
		ctx.markHotkeysDirty()
		return true
	end
	return false
end

function M.pauseBind(name, folder)
	local hk = M.findBind(name, folder)
	if hk and hk.is_running and hk._co_state then
		hk._co_state.paused = true
		return true
	end
	return false
end

function M.unpauseBind(name, folder)
	local hk = M.findBind(name, folder)
	if hk and hk.is_running and hk._co_state then
		hk._co_state.paused = false
		return true
	end
	return false
end

function M.isBindEnded(name, folder)
	local hk = M.findBind(name, folder)
	return not (hk and hk.is_running)
end

function M.setBindSelector(name, folder, state)
	local hk = M.findBind(name, folder)
	if hk then
		hk.quick_menu = not not state
		ctx.markHotkeysDirty()
		return true
	end
	return false
end

function M.runBind(name, folder, opts)
	opts = opts or {}
	local C = ctx.C
	local pushToast = ctx.pushToast
	local depth = tonumber(opts._depth or 0) or 0
	if depth > C.MAX_BIND_DEPTH then
		pushToast(L("binder.execution.text.runbind") .. C.MAX_BIND_DEPTH .. ")", "warn", 3.0)
		return false
	end
	local delay = tonumber(opts.delay_ms or 0) or 0
	local hk = resolveBindForExecution(name, folder)
	if not hk then
		pushToast((L("binder.execution.text.format_format_37")):format(tostring(name), tostring(folder or "")), "warn", 3.0)
		return false
	end
	if not hk.enabled then
		pushToast((L("binder.execution.text.format")):format(hk.label or "?"), "warn", 3.0)
		return false
	end
	M.enqueueHotkey(hk, delay)
	return true
end

function M.runBindRandom(folderPathString, opts)
	opts = opts or {}
	local C = ctx.C
	local pushToast = ctx.pushToast
	local depth = tonumber(opts._depth or 0) or 0
	if depth > C.MAX_BIND_DEPTH then
		pushToast(L("binder.execution.text.runbindrandom") .. C.MAX_BIND_DEPTH .. ")", "warn", 3.0)
		return false
	end
	local recursive = not not opts.recursive
	local delay = tonumber(opts.delay_ms or 0) or 0
	local p = pathFromString(folderPathString)
	local pool = collectBindsInFolder(p, recursive)
	if #pool == 0 then
		pushToast((L("binder.execution.text.format_38")):format(folderPathString or L("binder.execution.text.text_39")), "warn", 3.0)
		return false
	end
	local target = pool[math.random(1, #pool)]
	M.enqueueHotkey(target, delay)
	return true
end

-- === Key tracking and input ===
local function resetTrackedHotkeys(perf_state)
	for i = #perf_state.active_combo_hotkeys, 1, -1 do
		local hk = perf_state.active_combo_hotkeys[i]
		if hk then
			hk._comboActive = false
			hk._lastRepeatPressed = nil
			perf_state.active_combo_hotkeys_set[hk] = nil
		end
		perf_state.active_combo_hotkeys[i] = nil
	end
end

local function isQuickMenuHotkeyHeldWithMouseExtras(quickMenuHotkey)
	if type(quickMenuHotkey) ~= "table" or #quickMenuHotkey == 0 then
		return false
	end

	local mode = HotkeyManager.MODE_MODIFIER_TRIGGER
	local comboNormalized = HotkeyManager.normalizeComboForMode(quickMenuHotkey, mode)
	local pressedNormalized = HotkeyManager.normalizeComboForMode(pressedKeysList, mode)
	if #pressedNormalized < #comboNormalized then
		return false
	end

	local pressedSet = {}
	for i = 1, #pressedNormalized do
		pressedSet[pressedNormalized[i]] = true
	end

	local comboSet = {}
	for i = 1, #comboNormalized do
		local key = comboNormalized[i]
		if not pressedSet[key] then
			return false
		end
		comboSet[key] = true
	end

	for i = 1, #pressedNormalized do
		local key = pressedNormalized[i]
		if not comboSet[key] and not HotkeyManager.isMouseKey(key) then
			return false
		end
	end

	return true
end

local function getQuickMenuActivationMode()
	local getMode = ctx and ctx.getQuickMenuActivationMode
	local module = ctx and ctx.module
	local mode = type(getMode) == "function" and getMode() or nil
	if module and mode == module.QUICK_MENU_ACTIVATION_MODE_TOGGLE then
		return module.QUICK_MENU_ACTIVATION_MODE_TOGGLE
	end
	return module and module.QUICK_MENU_ACTIVATION_MODE_HOLD or "hold"
end

local function isQuickMenuHotkeyExactPressed()
	local getQuickMenuHotkey = ctx and ctx.getQuickMenuHotkey
	local quickMenuHotkey = type(getQuickMenuHotkey) == "function" and getQuickMenuHotkey() or nil
	if type(quickMenuHotkey) == "table" and #quickMenuHotkey > 0 then
		return HotkeyManager.comboMatch(pressedKeysList, quickMenuHotkey, HotkeyManager.MODE_MODIFIER_TRIGGER)
	end
	return isKeyDown(vk.VK_XBUTTON1) and true or false
end

local function isQuickMenuHotkeyPressed()
	local getQuickMenuHotkey = ctx and ctx.getQuickMenuHotkey
	local quickMenuHotkey = type(getQuickMenuHotkey) == "function" and getQuickMenuHotkey() or nil
	if type(quickMenuHotkey) == "table" and #quickMenuHotkey > 0 then
		if HotkeyManager.comboMatch(pressedKeysList, quickMenuHotkey, HotkeyManager.MODE_MODIFIER_TRIGGER) then
			return true
		end

		local State = ctx and ctx.State
		if State and State.quickMenuOpen then
			return isQuickMenuHotkeyHeldWithMouseExtras(quickMenuHotkey)
		end
		return false
	end
	return isKeyDown(vk.VK_XBUTTON1) and true or false
end

local function setQuickMenuOpenState(isOpen, opts)
	local State = ctx.State
	local module = ctx.module
	local quick_menu = ctx.quick_menu

	opts = opts or {}
	State.quickMenuOpen = isOpen and true or false
	module.quickMenuOpen = State.quickMenuOpen

	if not State.quickMenuOpen then
		State.quickMenuScrollQueued = 0
		State.quickMenuSelectRequest = nil
		if opts.block_reopen then
			State.quickMenuReopenBlocked = true
		end
		if quick_menu then
			quick_menu.resetState()
		end
	end
end

function M.resetInputState(reason)
	local State = ctx.State
	local perf_state = ctx.perf_state
	local combo_capture = ctx.combo_capture
	local text_confirm_capture = ctx.text_confirm_capture

	binderKeyTracker:reset()
	pressedKeysList = {}
	State.quickMenuScrollQueued = 0
	State.quickMenuSelectRequest = nil
	State.quickMenuReopenBlocked = false
	State.quickMenuToggleLatch = false
	State.quickMenuOpen = false

	local quick_menu = ctx.quick_menu
	if quick_menu then
		quick_menu.resetState()
	end

	if combo_capture:isActive() then
		combo_capture:stop()
	end
	if text_confirm_capture and text_confirm_capture:isActive() then
		text_confirm_capture:stop()
	end
	if quickMenuHotkeyCapture:isActive() then
		quickMenuHotkeyCapture:stop()
	end

	resetTrackedHotkeys(perf_state)

	ctx.module._inputSuppressUntil = os.clock() + 0.35
end

function M.onWindowMessage(msg, wparam, lparam)
	local now = os.clock()
	local module = ctx.module
	local _d = ctx._d
	local State = ctx.State
	local perf_state = ctx.perf_state
	local combo_capture = ctx.combo_capture
	local text_confirm_capture = ctx.text_confirm_capture

	local activateState = (tonumber(wparam) or 0) % 0x10000
	local lostFocus = msg == (wm.WM_KILLFOCUS or 0x0008)
		or (msg == (wm.WM_ACTIVATEAPP or 0x001C) and tonumber(wparam) == 0)
		or (msg == (wm.WM_ACTIVATE or 0x0006) and activateState == 0)
	local gainedFocus = msg == (wm.WM_SETFOCUS or 0x0007)
		or (msg == (wm.WM_ACTIVATEAPP or 0x001C) and tonumber(wparam) ~= 0)
		or (msg == (wm.WM_ACTIVATE or 0x0006) and (activateState == 1 or activateState == 2))

	if lostFocus then
		M.resetInputState("focus_lost")
		return false
	end
	if gainedFocus then
		M.resetInputState("focus_gain")
		return false
	end

	local hotkeyMessage = HotkeyManager.getMessageKeyInfo(msg, wparam)
	if hotkeyMessage and now < (module._inputSuppressUntil or 0) then
		return false
	end

	if (msg == (wm.WM_SYSKEYDOWN or 0x0104) or msg == (wm.WM_SYSKEYUP or 0x0105)) and _d.normalizeKey(wparam) == vk.VK_TAB then
		M.resetInputState("alttab")
		return false
	end

	local cwm = type(consumeWindowMessage) == "function" and consumeWindowMessage or nil
	if quickMenuHotkeyCapture:isActive() then
		if quickMenuHotkeyCapture:onWindowMessage(msg, wparam, cwm) then
			return true
		end
	end
	if combo_capture:isActive() then
		if combo_capture:onWindowMessage(msg, wparam, cwm) then
			return true
		end
	end
	if text_confirm_capture and text_confirm_capture:isActive() then
		if text_confirm_capture:onWindowMessage(msg, wparam, cwm) then
			return true
		end
	end
	if hotkeyMessage and hotkeyMessage.isDown and activate_pending_text_confirmations(hotkeyMessage.keyCode) then
		if cwm then
			cwm(true, true)
		end
		return true
	end

	if binderKeyTracker:onWindowMessage(msg, wparam) then
		pressedKeysList = binderKeyTracker:getOrdered()
	elseif msg == wm.WM_MOUSEWHEEL then
		if State.quickMenuOpen then
			local delta = bit.rshift(bit.band(wparam, 0xFFFF0000), 16)
			if delta >= 0x8000 then
				delta = delta - 0x10000
			end
			if delta ~= 0 then
				local steps = math.max(1, math.floor(math.abs(delta) / 120 + 0.5))
				if delta > 0 then
					State.quickMenuScrollQueued = State.quickMenuScrollQueued - steps
				else
					State.quickMenuScrollQueued = State.quickMenuScrollQueued + steps
				end
				if type(consumeWindowMessage) == "function" then
					consumeWindowMessage(true, true)
				end
				return true
			end
		end
	end
	return false
end

-- === processHotkeys ===
local function setComboTracked(perf_state, hk, active)
	if active then
		if not perf_state.active_combo_hotkeys_set[hk] then
			perf_state.active_combo_hotkeys_set[hk] = true
			perf_state.active_combo_hotkeys[#perf_state.active_combo_hotkeys + 1] = hk
		end
		return
	end
	if perf_state.active_combo_hotkeys_set[hk] then
		perf_state.active_combo_hotkeys_set[hk] = nil
		for i = #perf_state.active_combo_hotkeys, 1, -1 do
			if perf_state.active_combo_hotkeys[i] == hk then
				table.remove(perf_state.active_combo_hotkeys, i)
				break
			end
		end
	end
end

local function rebuildHotkeyRuntimeCache(perf_state)
	local hotkeys = ctx.hotkeys
	local by_len = {}
	local indexed = {}
	for _, hk in ipairs(hotkeys) do
		local keys = hk.keys
		local len = keys and #keys or 0
		if hk.enabled and len > 0 then
			indexed[#indexed + 1] = hk
			local bucket = by_len[len]
			if not bucket then
				bucket = {}
				by_len[len] = bucket
			end
			bucket[#bucket + 1] = hk
		end
	end
	perf_state.hotkey_runtime_cache.by_len = by_len
	perf_state.hotkey_runtime_cache.indexed = indexed
	perf_state.hotkey_runtime_cache.revision = perf_state.hotkeys_revision
end

local function getHotkeyRuntimeCache(perf_state)
	if perf_state.hotkey_runtime_cache.revision ~= perf_state.hotkeys_revision then
		rebuildHotkeyRuntimeCache(perf_state)
	end
	return perf_state.hotkey_runtime_cache
end

function M.processHotkeys()
	local C = ctx.C
	local perf_state = ctx.perf_state
	local State = ctx.State
	local now = os.clock()
	local nowMs = now * 1000
	local pressedCount = #pressedKeysList
	getHotkeyRuntimeCache(perf_state)

	if pressedCount == 0 then
		resetTrackedHotkeys(perf_state)
		return
	end

	if isQuickMenuHotkeyPressed() then
		resetTrackedHotkeys(perf_state)
		return
	end

	if State.quickMenuOpen and getQuickMenuActivationMode() == (ctx.module and ctx.module.QUICK_MENU_ACTIVATION_MODE_TOGGLE) then
		resetTrackedHotkeys(perf_state)
		return
	end

	for i = #perf_state.active_combo_hotkeys, 1, -1 do
		local hk = perf_state.active_combo_hotkeys[i]
		if
			not hk
			or not hk.enabled
			or not hk.keys
			or #hk.keys ~= pressedCount
			or not HotkeyManager.comboMatch(pressedKeysList, hk.keys, hk.hotkey_mode)
		then
			if hk then
				hk._comboActive = false
				hk._lastRepeatPressed = nil
				perf_state.active_combo_hotkeys_set[hk] = nil
			end
			table.remove(perf_state.active_combo_hotkeys, i)
		end
	end

	local candidates = perf_state.hotkey_runtime_cache.by_len[pressedCount]
	if not candidates then
		return
	end

	for _, hk in ipairs(candidates) do
		local comboNow = HotkeyManager.comboMatch(pressedKeysList, hk.keys, hk.hotkey_mode)
		if hk.repeat_mode then
			if comboNow then
				local lastInterval = hk.repeat_interval_ms
				if not lastInterval and hk.messages and #hk.messages > 0 then
					lastInterval = math.max(hk.messages[#hk.messages].interval or 500, 50)
				end
				lastInterval = lastInterval or 500
				local sec = lastInterval / 1000
				if not hk._lastRepeatPressed or not HotkeyManager.comboMatch(hk._lastRepeatPressed, hk.keys, hk.hotkey_mode) then
					M.enqueueHotkey(hk)
					hk.lastActivated = now
					hk._lastRepeatPressed = { table.unpack(pressedKeysList) }
				elseif now - (hk.lastActivated or 0) >= sec then
					M.enqueueHotkey(hk)
					hk.lastActivated = now
					hk._lastRepeatPressed = { table.unpack(pressedKeysList) }
				end
				setComboTracked(perf_state, hk, true)
			else
				hk._lastRepeatPressed = nil
				hk._comboActive = false
				setComboTracked(perf_state, hk, false)
			end
		else
			if comboNow and not hk._comboActive then
				if not hk._debounce_until or nowMs >= hk._debounce_until then
					M.enqueueHotkey(hk)
					hk._debounce_until = nowMs + C.DEBOUNCE_MS
				end
				hk._comboActive = true
				setComboTracked(perf_state, hk, true)
			elseif not comboNow and hk._comboActive then
				hk._comboActive = false
				setComboTracked(perf_state, hk, false)
			elseif comboNow then
				setComboTracked(perf_state, hk, true)
			end
		end
	end
end

-- === CheckQuickMenuKey / OnTick ===
function M.getQuickMenuHotkeyCapture()
	return quickMenuHotkeyCapture
end

function M.startQuickMenuHotkeyCapture(initial_keys)
	M.resetInputState("quick_menu_hotkey_capture")
	quickMenuHotkeyCapture:start(initial_keys)
	return quickMenuHotkeyCapture
end

function M.CheckQuickMenuKey()
	local State = ctx.State
	local module = ctx.module
	if os.clock() < (module._inputSuppressUntil or 0) then
		State.quickMenuScrollQueued = 0
		State.quickMenuSelectRequest = nil
		State.quickMenuOpen = false
		return
	end

	if quickMenuHotkeyCapture:isActive() then
		State.quickMenuScrollQueued = 0
		State.quickMenuSelectRequest = nil
		State.quickMenuOpen = false
		return
	end

	local activationMode = getQuickMenuActivationMode()
	local hotkeyHeld = isQuickMenuHotkeyPressed()
	local hotkeyExact = isQuickMenuHotkeyExactPressed()
	if State.quickMenuReopenBlocked then
		if hotkeyHeld then
			State.quickMenuScrollQueued = 0
			State.quickMenuSelectRequest = nil
			State.quickMenuOpen = false
			return
		end
		State.quickMenuReopenBlocked = false
		State.quickMenuToggleLatch = false
	end

	if activationMode == (module and module.QUICK_MENU_ACTIVATION_MODE_TOGGLE) then
		if hotkeyHeld then
			if hotkeyExact and not State.quickMenuToggleLatch then
				local shouldOpen = not State.quickMenuOpen
				if shouldOpen and hasVisibleQuickMenuEntries() then
					State.quickMenuOpen = true
					module.quickMenuOpen = true
					State.quickMenuSelectRequest = State.quickMenuTabIndex
				else
					setQuickMenuOpenState(false)
				end
				State.quickMenuToggleLatch = true
			end
		else
			State.quickMenuToggleLatch = false
		end
		if not State.quickMenuOpen then
			State.quickMenuScrollQueued = 0
			State.quickMenuSelectRequest = nil
		end
		return
	end

	local wantsOpen = hotkeyHeld
	local isOpen = wantsOpen and (State.quickMenuOpen or hasVisibleQuickMenuEntries()) or false
	if not isOpen then
		State.quickMenuScrollQueued = 0
		State.quickMenuSelectRequest = nil
	end
	local wasOpen = State.quickMenuOpen
	State.quickMenuOpen = isOpen
	if isOpen and not wasOpen then
		State.quickMenuSelectRequest = State.quickMenuTabIndex
	end
end

function M.OnTick()
	expire_pending_text_confirmations(os.clock() * 1000)
	M.CheckQuickMenuKey()
	M.processHotkeys()
	runScheduler()
end

-- === Init / Deinit ===
function M.init(c)
	ctx = c
	ensure_condition_language_cache()
end

function M.getActiveCoroutines()
	return active_coroutines
end

function M.deinit()
	for _, hk in ipairs(ctx.hotkeys or {}) do
		clear_text_confirmation_wait(hk, true)
	end
	outgoingTriggerGuard.chat = {}
	outgoingTriggerGuard.command = {}
	incomingEchoTriggerGuard = {}
	for i = #active_coroutines, 1, -1 do
		local item = active_coroutines[i]
		if item and item.hk then
			item.hk.is_running = false
			item.hk._co_state = nil
			item.hk._awaiting_input = false
		end
		active_coroutines[i] = nil
	end
end

return M
