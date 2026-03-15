local module = {}

local ffi = require("ffi")
local encoding = require("encoding")
local bit = require("bit")
encoding.default = "CP1251"
local u8 = encoding.UTF8

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
local sampev = require("samp.events")

local function safe_call(fn, ...)
	if type(fn) ~= "function" then
		return false, nil
	end
	local ok, res = pcall(fn, ...)
	if not ok then
		print(("[my_hooks] callback error: %s"):format(tostring(res)))
		return false, nil
	end
	return true, res
end

local function safe_match(text, pattern)
	local ok, found = pcall(string.match, text, pattern)
	return ok and found ~= nil
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

local function handleBinderPlayerInput(msg)
	if binder and binder.onPlayerCommand then
		local ok, handled = safe_call(binder.onPlayerCommand, msg)
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

local function onServerMessage(color, text)
	local text2 = tostring(u8(text) or "")
	-- local color1 = bit.tohex(funcs.ARGBtoRGB(color)):gsub('^00', '')
	local color1 = bit.tohex(color)

	if binder and binder.onServerMessage then
		safe_call(binder.onServerMessage, text2)
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
		for i = 1, #vip do
			if safe_match(text, "^" .. tostring(vip[i] or "")) then
				local text = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text)
				safe_call(VIPandADchat.AddVIPMessage, text)
				return false
			end
		end

		if
			string.match(text2, "^Объявление:")
			or string.match(text2, "^{079C1C}Объявление:")
			or string.match(text2, "^%[VIP%] Объявление:.")
			or string.match(text2, "^{FCAA4D}%[VIP%] Объявление:.")
			or string.match(text2, "^{FCAA4D}%[Реклама Бизнеса%] Объявление:")
			or string.match(text2, "^%[Реклама Бизнеса%] Объявление:")
		then
			local text = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text)
			text = fix_invalid_json_escapes(text)
			safe_call(VIPandADchat.AddADMessage, text)
			return false
		end

		if string.match(text2, "Отредактировал сотрудник СМИ %[") then
			local text = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text)
			text = fix_invalid_json_escapes(text)
			safe_call(VIPandADchat.SetLastADEdited, text)
			return false
		end

		if string.match(text2, "^Сообщение до редакции:") then
			local text = string.format("{FFFFFF}%s{%s} %s", os.date("[%H:%M:%S]"), color1, text)
			text = fix_invalid_json_escapes(text)
			safe_call(VIPandADchat.SetLastADPreEdit, text)
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

local function onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
	-- Вызывается КАЖДЫЙ раз, когда сервер показывает диалоговое окно
	if SMIHelp and SMIHelp.onShowDialog then
		local ret = SMIHelp.onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
		if ret ~= nil then
			return ret
		end
	end
	return { dialogid, style, title, button1, button2, text, placeholder }
end

-- 2. JMP HOOK (через hooks.jmp на AddChatEntry)
local lhook, hook = pcall(require, "hooks")

local originalChatAddEntry = nil
local CDialog_Close_orig = nil
local CInput_Send_orig = nil
local CInput_SendSay_orig = nil
local CDamageManager_ApplyDamage_orig = nil

local function ChatAddEntryHooked(chat, type, szText, szPrefix, textColor, prefixColor)
	local text = u8(ffi.string(szText))
	-- Твоя логика обработки текста чата
	-- Например, можно изменить szText и т.д.
	-- Для примера - ничего не меняем:

	return originalChatAddEntry(chat, type, szText, szPrefix, textColor, prefixColor)
end

local function CDialog_Close_hook(this, button)
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
		and samp_mod.pEditBox_active_func()
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
	if handleBinderPlayerInput(msg) then
		return false
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	local result = CInput_Send_orig(this, back)
	if is_news_command_text(msg) and SMILive and type(SMILive._mark_news_send_timestamp) == "function" then
		safe_call(SMILive._mark_news_send_timestamp)
	end
	return result
	-- local text = ffi.string(text)
	-- local text = tags.change_tags(text)
	-- -- local text = u8:decode(text)
	-- -- if string.find(text, u8:decode('^/news .+'))  then
	-- --	 if SMIHelp.timer_news then
	-- --		 lua_thread.create(function()
	-- --			 SMIHelp.timer_news_clock = os.clock()
	-- --			 SMIHelp.timer_news = false
	-- --			 repeat
	-- --				 wait(0)
	-- --			 until (os.clock() - SMIHelp.timer_news_clock >= SMIHelp.timer_news_delay)
	-- --			 SMIHelp.timer_news = true
	-- --		 end)
	-- --	 end
	-- -- end
	-- -- if string.find(text, u8:decode('^/r .+')) then binder.walkie_talkie(1) end
	-- -- if string.find(text, u8:decode('^/d .+')) then binder.walkie_talkie(2) end
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
	if handleBinderPlayerInput(msg) then
		return false
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	return CInput_SendSay_orig(this, back)
	-- local text = ffi.string(text)
	-- local text = tags.change_tags(text)
	-- -- local text = u8:decode(text)
	-- -- local text = u8(text)
	-- -- local text = u8(text)
	-- CInput_SendSay_hook(this, text)
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
	print(("[my_hooks] failed to install %s: %s"):format(tostring(label), tostring(hook_or_err)))
	return nil
end

function module.init()
	if is_initialized then
		module.deinit()
	end

	-- Включить hook на samp().events
	sampev.onServerMessage = onServerMessage
	sampev.onShowDialog = onShowDialog

	-- Включить JMP hook (detour) на AddChatEntry
	if lhook and samp_mod and samp_mod.sampModule and samp_mod.main_offsets and samp_mod.currentVersion then
		local offsets = samp_mod.main_offsets
		local version = samp_mod.currentVersion
		local add_entry_off = offsets.AddEntry and offsets.AddEntry[version]
		local cdialog_close_off = offsets.CDialog_Close and offsets.CDialog_Close[version]
		local cinput_send_off = offsets.CInput_Send and offsets.CInput_Send[version]
		local cinput_say_off = offsets.CInput_SendSay and offsets.CInput_SendSay[version]
		if not (add_entry_off and cdialog_close_off and cinput_send_off and cinput_say_off) then
			print(("[my_hooks] missing one or more offsets for version: %s"):format(tostring(version)))
			is_initialized = true
			return
		end
		originalChatAddEntry = install_jmp_hook(
			"AddChatEntry",
			"void(__thiscall*)(void* chat, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor)",
			ChatAddEntryHooked,
			samp_mod.sampModule + add_entry_off
		)
		CDialog_Close_orig = install_jmp_hook(
			"CDialog_Close",
			"void(__thiscall *)(uintptr_t, char)",
			CDialog_Close_hook,
			samp_mod.sampModule + cdialog_close_off
		)

		-- CDialog_Show = hook.jmp.new("void(__thiscall *)(uintptr_t, int, int, const char*, const char*, const char*, const char*, bool)", CDialog_Show, samp().sampModule + samp().main_offsets.CDialog_Show[samp().currentVersion])
		CInput_Send_orig = install_jmp_hook(
			"CInput_Send",
			"void(__thiscall *)(uintptr_t, const char*)",
			CInput_Send_hook,
			samp_mod.sampModule + cinput_send_off
		)
		CInput_SendSay_orig = install_jmp_hook(
			"CInput_SendSay",
			"void(__thiscall *)(uintptr_t, const char*)",
			CInput_SendSay_hook,
			samp_mod.sampModule + cinput_say_off
		)
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

function module.deinit()
	if sampev.onServerMessage == onServerMessage then
		sampev.onServerMessage = nil
	end
	if sampev.onShowDialog == onShowDialog then
		sampev.onShowDialog = nil
	end

	safe_stop_hook(CDamageManager_ApplyDamage_orig)
	safe_stop_hook(CInput_SendSay_orig)
	safe_stop_hook(CInput_Send_orig)
	safe_stop_hook(CDialog_Close_orig)
	safe_stop_hook(originalChatAddEntry)

	CDamageManager_ApplyDamage_orig = nil
	CInput_SendSay_orig = nil
	CInput_Send_orig = nil
	CDialog_Close_orig = nil
	originalChatAddEntry = nil
	is_initialized = false
	-- Отключить хуки (чтобы можно было перезагрузить без рестарта)
end

addEventHandler("onScriptTerminate", function(scr, quitGame)
	if scr == thisScript() then
		module.deinit()
	end
end)

return module
