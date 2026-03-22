local module = {}

local ffi = require("ffi")
local encoding = require("encoding")
local bit = require("bit")
local language = require("language")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local function L(key, params)
	return language.getText(key, params)
end

local tags
local SMIHelp
local SMILive
local VIPandADchat
local funcs
local unwanted
local samp_mod
local binder

local server_message_listeners = {}
local is_initialized = false

function module.attachModules(mod)
	tags = mod.tags
	SMIHelp = mod.SMIHelp
	SMILive = mod.SMILive
	VIPandADchat = mod.VIPandADchat
	funcs = mod.funcs
	unwanted = mod.unwanted
	samp_mod = mod.samp
	binder = mod.binder
end

function module.addServerMessageListener(listener)
	if type(listener) ~= "function" then
		return
	end
	for _, existing in ipairs(server_message_listeners) do
		if existing == listener then
			return
		end
	end
	table.insert(server_message_listeners, listener)
end

function module.removeServerMessageListener(listener)
	if type(listener) ~= "function" then
		return false
	end
	for index = #server_message_listeners, 1, -1 do
		if server_message_listeners[index] == listener then
			table.remove(server_message_listeners, index)
			return true
		end
	end
	return false
end

-- 1. SAMP EVENTS HOOK (через samp.events)
local sampev = nil
local sampev_error = nil
local has_sampfuncs = false
local samp_events_available = false
local use_samp_events_server_message = false
local use_samp_events_show_dialog = false
local use_samp_events_outgoing = false
local keep_chat_add_entry_hook = false
module.BACKEND_STANDARD = "standard"
module.BACKEND_SAMPFUNCS = "sampfuncs"
module.BACKEND_ARIZONA = "arizona"
module._hook_backend_mode = module.BACKEND_STANDARD
module._hook_backend_active = module.BACKEND_STANDARD

local function refresh_samp_events_backend()
	local sf_handle = 0
	if type(getModuleHandle) == "function" then
		local ok, result = pcall(getModuleHandle, "SAMPFUNCS.asi")
		if ok then
			sf_handle = tonumber(result) or 0
		end
	end
	has_sampfuncs = sf_handle ~= 0

	local ok, result = pcall(require, "samp.events")
	if ok and type(result) == "table" then
		sampev = result
		sampev_error = nil
		samp_events_available = true
		return
	end

	sampev = nil
	sampev_error = tostring(result)
	samp_events_available = false
end

local function normalize_hook_backend_mode(mode)
	mode = tostring(mode or ""):lower()
	if mode == module.BACKEND_SAMPFUNCS then
		return module.BACKEND_SAMPFUNCS
	end
	if mode == module.BACKEND_ARIZONA then
		return module.BACKEND_ARIZONA
	end
	return module.BACKEND_STANDARD
end

local function resolve_hook_backend_plan()
	local can_use_samp_events = has_sampfuncs and samp_events_available and sampev ~= nil
	local plan = {
		useSampEventsServerMessage = false,
		useSampEventsShowDialog = false,
		useSampEventsOutgoing = false,
		keepChatAddEntryHook = false,
		active = module.BACKEND_STANDARD,
	}

	if not can_use_samp_events then
		return plan
	end

	if module._hook_backend_mode == module.BACKEND_SAMPFUNCS then
		plan.useSampEventsServerMessage = true
		plan.useSampEventsShowDialog = true
		plan.useSampEventsOutgoing = true
		plan.active = module.BACKEND_SAMPFUNCS
	elseif module._hook_backend_mode == module.BACKEND_ARIZONA then
		plan.useSampEventsServerMessage = true
		plan.useSampEventsShowDialog = true
		plan.keepChatAddEntryHook = true
		plan.active = module.BACKEND_ARIZONA
	end

	return plan
end

local function apply_hook_backend_plan(plan)
	plan = plan or {}
	use_samp_events_server_message = plan.useSampEventsServerMessage and true or false
	use_samp_events_show_dialog = plan.useSampEventsShowDialog and true or false
	use_samp_events_outgoing = plan.useSampEventsOutgoing and true or false
	keep_chat_add_entry_hook = plan.keepChatAddEntryHook and true or false
	module._hook_backend_active = plan.active or module.BACKEND_STANDARD
end

local function cstring_or_empty(value)
	if value == nil then
		return ""
	end
	local ok, result = pcall(ffi.string, value)
	if ok and type(result) == "string" then
		return result
	end
	return tostring(value)
end

local routeServerMessageHook
local onShowDialog
local onSendCommand
local onSendChat
local onSendDialogResponse

local function safe_call(fn, ...)
	if type(fn) ~= "function" then
		return false, nil
	end
	local ok, res = pcall(fn, ...)
	if not ok then
		print(L("my_hooks.log.callback_error", {
			error = tostring(res),
		}))
		return false, nil
	end
	return true, res
end

local function safe_match(text, pattern)
	local ok, found = pcall(string.match, text, pattern)
	return ok and found ~= nil
end

local function safe_match_any(text, patterns, prefix)
	if type(patterns) ~= "table" then
		return false
	end
	prefix = prefix or ""
	for i = 1, #patterns do
		local pattern = tostring(patterns[i] or "")
		if pattern ~= "" and safe_match(text, prefix .. pattern) then
			return true
		end
	end
	return false
end

local function is_news_command_text(text)
	local value = tostring(text or "")
	value = value:gsub("^%s+", "")
	local lower = value:lower()
	if lower:sub(1, 5) ~= "/news" then
		return false
	end
	local next_char = value:sub(6, 6)
	return next_char == "" or next_char:match("%s") ~= nil
end

local function dispatchBinderOutgoingText(msg, opts)
	opts = opts or {}
	if not binder then
		return false
	end

	local handler = nil
	if opts.command then
		handler = binder.onOutgoingCommandInput or binder.onPlayerCommand
	else
		handler = binder.onOutgoingChatInput
	end

	if type(handler) == "function" then
		local ok, handled = safe_call(handler, msg)
		return ok and handled == true
	end

	return false
end

local function start_send_cooldown()
	if not (SMIHelp and SMIHelp.timer_send_enabled) then
		return
	end
	lua_thread.create(function()
		SMIHelp.timer_send_clock = os.clock()
		SMIHelp.timer_send = false
		repeat
			wait(50)
		until os.clock() - SMIHelp.timer_send_clock >= SMIHelp.timer_send_delay
		SMIHelp.timer_send = true
	end)
end

local function fix_invalid_json_escapes(str)
	-- Заменить \9 (таб как в некоторых клиентах) на пробел
	str = str:gsub("\\9", "	") -- четыре пробела вместо каждого \9 (или "\t" если хочешь таб)
	-- Заменить прочие невалидные слэши (кроме разрешённых в json)
	str = str:gsub('\\\\([^nrtbf/\\\\"])', " %1")
	str = str:gsub("[%z\1-\8\11\12\14-\31]", "") -- удалить любые управляющие символы (0x00..0x1F), кроме \n \r \t если надо

	return str
end

local function apply_server_message_fallback(color, text)
	local result = routeServerMessageHook(color, text)
	if result == false then
		return false
	end

	local new_color = color
	local new_text = text
	if type(result) == "table" then
		if result[1] ~= nil then
			local parsed_color = tonumber(result[1])
			if parsed_color ~= nil then
				new_color = parsed_color
			end
		end
		if result[2] ~= nil then
			new_text = tostring(result[2])
		end
	end

	return true, new_color, new_text
end

local function apply_show_dialog_fallback(dialogid, style, title, button1, button2, text, placeholder)
	local result = onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
	if result == false then
		return false
	end

	local new_dialogid = dialogid
	local new_style = style
	local new_title = title
	local new_button1 = button1
	local new_button2 = button2
	local new_text = text
	local new_placeholder = placeholder

	if type(result) == "table" then
		if result[1] ~= nil then
			local parsed_dialogid = tonumber(result[1])
			if parsed_dialogid ~= nil then
				new_dialogid = parsed_dialogid
			end
		end
		if result[2] ~= nil then
			local parsed_style = tonumber(result[2])
			if parsed_style ~= nil then
				new_style = parsed_style
			end
		end
		if result[3] ~= nil then
			new_title = tostring(result[3])
		end
		if result[4] ~= nil then
			new_button1 = tostring(result[4])
		end
		if result[5] ~= nil then
			new_button2 = tostring(result[5])
		end
		if result[6] ~= nil then
			new_text = tostring(result[6])
		end
		if result[7] ~= nil then
			new_placeholder = result[7]
		end
	end

	return true, new_dialogid, new_style, new_title, new_button1, new_button2, new_text, new_placeholder
end

-- Low-level incoming text hook/router. Binder business logic is called via onIncomingTextMessage.
routeServerMessageHook = function(color, text)
	local text2 = tostring(u8(text) or "")
	local text3 = tostring(u8:decode(text) or "")
	-- local color1 = bit.tohex(funcs.ARGBtoRGB(color)):gsub('^00', '')
	local color1 = bit.tohex(color)

	if binder then
		local handler = binder.onIncomingTextMessage or binder.onServerMessage
		if type(handler) == "function" then
			safe_call(handler, text2)
		end
	end

	local suppress = false
	if #server_message_listeners > 0 then
		local listeners_snapshot = {}
		for i = 1, #server_message_listeners do
			listeners_snapshot[i] = server_message_listeners[i]
		end
		for _, listener in ipairs(listeners_snapshot) do
			local ok, result = safe_call(listener, color1, text2)
			if ok and result == false then
				suppress = true
			end
		end
	end

	if
		SMIHelp
		and SMIHelp.timer_send_enabled
		and (
			string.match(text2, "^%[VIP%] Объявление:.")
			or string.match(text2, "^{FCAA4D}%[VIP%] Объявление:.")
		)
	then
		start_send_cooldown()
	end

	if VIPandADchat and VIPandADchat.isEnabled and VIPandADchat.isEnabled() then
		local vip = VIPandADchat.VIP()
		if type(vip) ~= "table" then
			vip = {}
		end
		local ad = type(VIPandADchat.AD) == "function" and VIPandADchat.AD() or {}
		if type(ad) ~= "table" then
			ad = {}
		end

		if safe_match_any(text2, vip, "^") then
			local text2 = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text2)
			safe_call(VIPandADchat.AddVIPMessage, text2)
			return false
		end

		if safe_match_any(text2, ad.main) then
			local text2 = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text2)
			text2 = fix_invalid_json_escapes(text2)
			safe_call(VIPandADchat.AddADMessage, text2)
			return false
		end

		if safe_match_any(text2, ad.edited) then
			local text2 = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text2)
			text2 = fix_invalid_json_escapes(text2)
			safe_call(VIPandADchat.SetLastADEdited, text2)
			return false
		end

		if safe_match_any(text2, ad.pre_edit) then
			local text2 = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text2)
			text2 = fix_invalid_json_escapes(text2)
			safe_call(VIPandADchat.SetLastADPreEdit, text2)
			return false
		end
	end

	if unwanted and unwanted.isEnabled and unwanted.isEnabled() and unwanted.should_ignore(text) then
		return false -- глушим сообщение
	end

	if suppress then
		return false
	end
	return { color, text }
end

onShowDialog = function(dialogid, style, title, button1, button2, text, placeholder)
	-- Вызывается КАЖДЫЙ раз, когда сервер показывает диалоговое окно
	if SMIHelp and SMIHelp.onShowDialog then
		local ok, ret = safe_call(SMIHelp.onShowDialog, dialogid, style, title, button1, button2, text, placeholder)
		if ok and ret ~= nil then
			return ret
		end
	end
	return { dialogid, style, title, button1, button2, text, placeholder }
end

-- 2. JMP HOOK (через hooks.jmp на AddChatEntry)
local lhook, hook = pcall(require, "hooks")

local originalChatAddEntry = nil
local CDialog_Show_orig = nil
local CDialog_Close_orig = nil
local CInput_Send_orig = nil
local CInput_SendSay_orig = nil
local CDamageManager_ApplyDamage_orig = nil

local function ChatAddEntryHooked(chat, type, szText, szPrefix, textColor, prefixColor)
	if not use_samp_events_server_message and szText ~= nil then
		local keep, new_text_color, new_text = apply_server_message_fallback(textColor, cstring_or_empty(szText))
		if not keep then
			return false
		end
		textColor = new_text_color
		szText = new_text
	end
	-- Твоя логика обработки текста чата
	-- Например, можно изменить szText и т.д.
	-- Для примера - ничего не меняем:

	return originalChatAddEntry(chat, type, szText, szPrefix, textColor, prefixColor)
end

local function CDialog_Show_hook(this, dialogid, style, title, text, button1, button2, serverside)
	if use_samp_events_show_dialog or not serverside then
		return CDialog_Show_orig(this, dialogid, style, title, text, button1, button2, serverside)
	end
	if samp_mod and type(samp_mod.isDialogHookBypassActive) == "function" and samp_mod.isDialogHookBypassActive() then
		return CDialog_Show_orig(this, dialogid, style, title, text, button1, button2, serverside)
	end

	local keep, new_dialogid, new_style, new_title, new_button1, new_button2, new_text =
		apply_show_dialog_fallback(
			dialogid,
			style,
			cstring_or_empty(title),
			cstring_or_empty(button1),
			cstring_or_empty(button2),
			cstring_or_empty(text),
			nil
		)
	if not keep then
		return false
	end

	return CDialog_Show_orig(this, new_dialogid, new_style, new_title, new_text, new_button1, new_button2, serverside)
end

local function CDialog_Close_hook(this, button)
	if samp_mod and type(samp_mod.isDialogHookBypassActive) == "function" and samp_mod.isDialogHookBypassActive() then
		return CDialog_Close_orig(this, button)
	end

	if not (samp_mod and SMIHelp) then
		return CDialog_Close_orig(this, button)
	end

	local caption = nil
	if samp_mod.get_dialog_caption then
		local _, result = safe_call(samp_mod.get_dialog_caption)
		caption = result
	end
	if
		samp_mod.isDialogActive()
		and samp_mod.pDialogInput_pEditBox_active_func()
		and caption
		and caption:find(u8:decode("Редактирование"))
		and button == 1
	then
		if SMIHelp.timer_send_enabled and not SMIHelp.timer_send then
			return false
		end

		local input = samp_mod.sampGetDialogEditboxText()
		if input and not input:match("^%s*$") then
			safe_call(SMIHelp.AddToHistory, u8(input))
		end

		if SMIHelp.timer_send_enabled then
			start_send_cooldown()
		end
	end

	return CDialog_Close_orig(this, button)
end

local function CInput_Send_hook(this, text)
	if text == nil then
		return CInput_Send_orig(this, text)
	end
	local raw = ffi.string(text) -- CP1251 от клиента
	local msg = u8(raw) -- теперь UTF-8, можно юзать tags.change_tags
	if tags and tags.change_tags then
		local ok, changed = safe_call(tags.change_tags, msg)
		if ok and type(changed) == "string" then
			msg = changed
		end
	end
	if dispatchBinderOutgoingText(msg, { command = true }) then
		return false
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	local result = CInput_Send_orig(this, back)
	if is_news_command_text(msg) and SMILive and type(SMILive._mark_news_send_timestamp) == "function" then
		safe_call(SMILive._mark_news_send_timestamp)
	end
	return result
	-- CInput_Send_hook(this, text)
end

local function CInput_SendSay_hook(this, text)
	if text == nil then
		return CInput_SendSay_orig(this, text)
	end
	local raw = ffi.string(text) -- CP1251 от клиента
	local msg = u8(raw) -- теперь UTF-8, можно юзать tags.change_tags
	if tags and tags.change_tags then
		local ok, changed = safe_call(tags.change_tags, msg)
		if ok and type(changed) == "string" then
			msg = changed
		end
	end
	if dispatchBinderOutgoingText(msg, { command = false }) then
		return false
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	return CInput_SendSay_orig(this, back)
	-- CInput_SendSay_hook(this, text)
end

local function process_outgoing_text(raw_text, opts)
	opts = opts or {}
	local raw = tostring(raw_text or "")
	local normalized = raw
	local command_without_slash = false

	if opts.command and normalized ~= "" and normalized:sub(1, 1) ~= "/" then
		normalized = "/" .. normalized
		command_without_slash = true
	end

	local msg = u8(normalized)
	if tags and tags.change_tags then
		local ok, changed = safe_call(tags.change_tags, msg)
		if ok and type(changed) == "string" then
			msg = changed
		end
	end

	if dispatchBinderOutgoingText(msg, { command = opts.command == true }) then
		return false
	end

	if opts.command and is_news_command_text(msg) and SMILive and type(SMILive._mark_news_send_timestamp) == "function" then
		safe_call(SMILive._mark_news_send_timestamp)
	end

	local back = u8:decode(msg)
	if command_without_slash and back:sub(1, 1) == "/" then
		back = back:sub(2)
	end
	return back
end

local function should_process_smi_dialog_response(button)
	if not (samp_mod and SMIHelp) then
		return false
	end
	if tonumber(button) ~= 1 then
		return false
	end
	local caption = nil
	if samp_mod.get_dialog_caption then
		local _, result = safe_call(samp_mod.get_dialog_caption)
		caption = result
	end
	return type(caption) == "string" and caption:find(u8:decode("Редактирование")) ~= nil
end

local function process_dialog_response_before_send(button, input_text)
	if not should_process_smi_dialog_response(button) then
		return true
	end

	if SMIHelp.timer_send_enabled and not SMIHelp.timer_send then
		return false
	end

	local input = tostring(input_text or "")
	if input ~= "" and not input:match("^%s*$") then
		safe_call(SMIHelp.AddToHistory, u8(input))
	end

	if SMIHelp.timer_send_enabled then
		start_send_cooldown()
	end
	return true
end

CDialog_Close_hook = function(this, button)
	if samp_mod and type(samp_mod.isDialogHookBypassActive) == "function" and samp_mod.isDialogHookBypassActive() then
		return CDialog_Close_orig(this, button)
	end

	if not CDialog_Close_orig then
		return CDialog_Close_orig(this, button)
	end

	if should_process_smi_dialog_response(button) then
		local input = samp_mod and samp_mod.sampGetDialogEditboxText and samp_mod.sampGetDialogEditboxText() or ""
		if not process_dialog_response_before_send(button, input) then
			return false
		end
	end

	return CDialog_Close_orig(this, button)
end

onSendCommand = function(command)
	local back = process_outgoing_text(command, { command = true })
	if back == false then
		return false
	end
	return { back }
end

onSendChat = function(message)
	local back = process_outgoing_text(message, { command = false })
	if back == false then
		return false
	end
	return { back }
end

onSendDialogResponse = function(dialogId, button, listboxId, input)
	if not process_dialog_response_before_send(button, input) then
		return false
	end
	return { dialogId, button, listboxId, input }
end

local function CDamageManager_ApplyDamage_hook(this, car, component, intensity, arg3)
	if not (component >= 1 and component <= 4) then
		return false
	end
	return CDamageManager_ApplyDamage_orig(this, car, component, intensity, arg3)
end

local function safe_stop_hook(h)
	if type(h) ~= "table" then
		return
	end
	if type(h.stop) == "function" then
		pcall(h.stop, h)
	end
	if type(h.destroy) == "function" then
		pcall(h.destroy, h)
	elseif type(h.disable) == "function" then
		pcall(h.disable, h)
	elseif type(h.uninstall) == "function" then
		pcall(h.uninstall, h)
	elseif type(h.remove) == "function" then
		pcall(h.remove, h)
	end
end

local function install_jmp_hook(label, cast, callback, addr)
	local ok, hook_or_err = pcall(hook.jmp.new, cast, callback, addr)
	if ok then
		return hook_or_err
	end
	print(L("my_hooks.log.install_failed", {
		label = tostring(label),
		error = tostring(hook_or_err),
	}))
	return nil
end

local function clear_samp_events_handlers()
	if sampev and sampev.onServerMessage == routeServerMessageHook then
		sampev.onServerMessage = nil
	end
	if sampev and sampev.onShowDialog == onShowDialog then
		sampev.onShowDialog = nil
	end
	if sampev and sampev.onSendCommand == onSendCommand then
		sampev.onSendCommand = nil
	end
	if sampev and sampev.onSendChat == onSendChat then
		sampev.onSendChat = nil
	end
	if sampev and sampev.onSendDialogResponse == onSendDialogResponse then
		sampev.onSendDialogResponse = nil
	end
end

function module.init()
	if is_initialized then
		module.deinit()
	end

	refresh_samp_events_backend()
	local backend_plan = resolve_hook_backend_plan()
	apply_hook_backend_plan(backend_plan)

	if module._hook_backend_mode ~= module.BACKEND_STANDARD and not has_sampfuncs then
		print(L("my_hooks.log.sampfuncs_not_found"))
	end

	clear_samp_events_handlers()
	if sampev then
		sampev.onServerMessage = use_samp_events_server_message and routeServerMessageHook or nil
		sampev.onShowDialog = use_samp_events_show_dialog and onShowDialog or nil
		sampev.onSendCommand = use_samp_events_outgoing and onSendCommand or nil
		sampev.onSendChat = use_samp_events_outgoing and onSendChat or nil
		sampev.onSendDialogResponse = use_samp_events_outgoing and onSendDialogResponse or nil
	end

	if module._hook_backend_mode ~= module.BACKEND_STANDARD and module._hook_backend_active == module.BACKEND_STANDARD then
		print(
			L("my_hooks.log.backend_unavailable", {
				mode = tostring(module._hook_backend_mode),
				suffix = sampev_error and sampev_error ~= "" and (": " .. tostring(sampev_error)) or "",
			})
		)
	end

	if lhook and samp_mod and samp_mod.sampModule and samp_mod.main_offsets and samp_mod.currentVersion then
		local offsets = samp_mod.main_offsets
		local version = samp_mod.currentVersion
		local add_entry_off = offsets.AddEntry and offsets.AddEntry[version]
		local cdialog_show_off = offsets.CDialog_Show and offsets.CDialog_Show[version]
		local cdialog_close_off = offsets.CDialog_Close and offsets.CDialog_Close[version]
		local cinput_send_off = offsets.CInput_Send and offsets.CInput_Send[version]
		local cinput_say_off = offsets.CInput_SendSay and offsets.CInput_SendSay[version]
		if (not use_samp_events_server_message) or keep_chat_add_entry_hook then
			if add_entry_off then
				originalChatAddEntry = install_jmp_hook(
					"AddChatEntry",
					"void(__thiscall*)(void* chat, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor)",
					ChatAddEntryHooked,
					samp_mod.sampModule + add_entry_off
				)
			else
				print(L("my_hooks.log.missing_add_entry_offset", {
					version = tostring(version),
				}))
			end
		end

		if not use_samp_events_show_dialog then
			if cdialog_show_off then
				CDialog_Show_orig = install_jmp_hook(
					"CDialog_Show",
					"void(__thiscall *)(uintptr_t, int, int, const char*, const char*, const char*, const char*, bool)",
					CDialog_Show_hook,
					samp_mod.sampModule + cdialog_show_off
				)
			else
				print(L("my_hooks.log.missing_cdialog_show_offset", {
					version = tostring(version),
				}))
			end
		end

		if not use_samp_events_outgoing then
			if cdialog_close_off then
				CDialog_Close_orig = install_jmp_hook(
					"CDialog_Close",
					"void(__thiscall *)(uintptr_t, char)",
					CDialog_Close_hook,
					samp_mod.sampModule + cdialog_close_off
				)
			else
				print(L("my_hooks.log.missing_cdialog_close_offset", {
					version = tostring(version),
				}))
			end

			if cinput_send_off then
				CInput_Send_orig = install_jmp_hook(
					"CInput_Send",
					"void(__thiscall *)(uintptr_t, const char*)",
					CInput_Send_hook,
					samp_mod.sampModule + cinput_send_off
				)
			else
				print(L("my_hooks.log.missing_cinput_send_offset", {
					version = tostring(version),
				}))
			end

			if cinput_say_off then
				CInput_SendSay_orig = install_jmp_hook(
					"CInput_SendSay",
					"void(__thiscall *)(uintptr_t, const char*)",
					CInput_SendSay_hook,
					samp_mod.sampModule + cinput_say_off
				)
			else
				print(L("my_hooks.log.missing_cinput_send_say_offset", {
					version = tostring(version),
				}))
			end
		end

		CDamageManager_ApplyDamage_orig = install_jmp_hook(
			"CDamageManager_ApplyDamage",
			"bool(__thiscall*)(uintptr_t this, uintptr_t car, int component, float intensity, float arg3)",
			CDamageManager_ApplyDamage_hook,
			0x6C24B0
		)

		-- AttachObjectToBone = hook.jmp.new("void(__cdecl*)(uintptr_t, uintptr_t, int)", AttachObjectToBone, 0x5B0450)
	end
	is_initialized = true
end

function module.setHookBackendMode(mode)
	mode = normalize_hook_backend_mode(mode)
	module._hook_backend_mode = mode
	if is_initialized then
		module.init()
	else
		refresh_samp_events_backend()
		apply_hook_backend_plan(resolve_hook_backend_plan())
	end
	return true, module._hook_backend_active
end

function module.getHookBackendMode()
	return module._hook_backend_mode
end

function module.getHookBackendStatus()
	refresh_samp_events_backend()
	local backend_plan = resolve_hook_backend_plan()
	return {
		desired = module._hook_backend_mode,
		active = backend_plan.active,
		hasSampfuncs = has_sampfuncs,
		sampEventsAvailable = samp_events_available,
		sampEventsError = sampev_error,
		hooks = {
			onServerMessage = backend_plan.useSampEventsServerMessage and "samp_events" or "custom",
			ChatAddEntryHooked = ((not backend_plan.useSampEventsServerMessage) or backend_plan.keepChatAddEntryHook)
					and "custom"
					or "disabled",
			onShowDialog = backend_plan.useSampEventsShowDialog and "samp_events" or "custom",
			onSendCommand = backend_plan.useSampEventsOutgoing and "samp_events" or "custom",
			onSendChat = backend_plan.useSampEventsOutgoing and "samp_events" or "custom",
			onSendDialogResponse = backend_plan.useSampEventsOutgoing and "samp_events" or "custom",
		},
	}
end

module.setBackendMode = module.setHookBackendMode
module.getBackendMode = module.getHookBackendMode
module.getBackendStatus = module.getHookBackendStatus

function module.deinit()
	clear_samp_events_handlers()

	safe_stop_hook(CDamageManager_ApplyDamage_orig)
	safe_stop_hook(CInput_SendSay_orig)
	safe_stop_hook(CInput_Send_orig)
	safe_stop_hook(CDialog_Close_orig)
	safe_stop_hook(CDialog_Show_orig)
	safe_stop_hook(originalChatAddEntry)

	CDamageManager_ApplyDamage_orig = nil
	CInput_SendSay_orig = nil
	CInput_Send_orig = nil
	CDialog_Close_orig = nil
	CDialog_Show_orig = nil
	originalChatAddEntry = nil
	use_samp_events_server_message = false
	use_samp_events_show_dialog = false
	use_samp_events_outgoing = false
	keep_chat_add_entry_hook = false
	module._hook_backend_active = module.BACKEND_STANDARD
	is_initialized = false
end

module.onTerminate = module.deinit

addEventHandler("onScriptTerminate", function(scr, quitGame)
	if scr == thisScript() then
		module.deinit()
	end
end)

return module
