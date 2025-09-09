-- my_hooks.lua

local module = {}

local ffi = require 'ffi'
local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

local tags
local SMIHelp
local VIPandADchat
local funcs
local unwanted
local samp_mod

function module.attachModules(mod)
	tags = mod.tags
	SMIHelp = mod.SMIHelp
	VIPandADchat = mod.VIPandADchat
	funcs = mod.funcs
	unwanted = mod.unwanted
	samp_mod = mod.samp
end

-- 1. SAMP EVENTS HOOK (через samp.events)
local sampev = require('samp.events')

local function fix_invalid_json_escapes(str)
	-- Заменить \9 (таб как в некоторых клиентах) на пробел
	str = str:gsub("\\9", "	") -- четыре пробела вместо каждого \9 (или "\t" если хочешь таб)
	-- Заменить прочие невалидные слэши (кроме разрешённых в json)
	str = str:gsub("\\\\([^nrtbf/\\\\\"])", " %1")
	str = str:gsub("[%z\1-\31]", "") -- удалить любые управляющие символы (0x00..0x1F), кроме \n \r \t если надо

	return str
end

local function onServerMessage(color, text)
	local text2 = u8(text)
	-- local color1 = bit.tohex(funcs.ARGBtoRGB(color)):gsub('^00', '')
	local color1 = bit.tohex(color)

	if string.match(text2, '^%[VIP%] Объявление:.') or string.match(text2, '^{FCAA4D}%[VIP%] Объявление:.') then
		lua_thread.create(function()
			SMIHelp.timer_send_clock = os.clock()
			SMIHelp.timer_send = false
			repeat
				wait(0)
			until (os.clock() - SMIHelp.timer_send_clock >= SMIHelp.timer_send_delay)
			SMIHelp.timer_send = true
		end)
	end

	if VIPandADchat then
		local vip = VIPandADchat.VIP()
		for i = 1, #vip do
			if string.match(text, '^' .. vip[i]) then
				local text = string.format('{FFFFFF}%s{%s} %s', os.date('[%H:%M:%S]'), color1, text)
				VIPandADchat.AddVIPMessage(text)
				return false
			end
		end

		if string.match(text2, '^Объявление:')
			or string.match(text2, '^{079C1C}Объявление:')
			or string.match(text2, '^%[VIP%] Объявление:.')
			or string.match(text2, '^{FCAA4D}%[VIP%] Объявление:.')
			or string.match(text2, '^{FCAA4D}%[Реклама Бизнеса%] Объявление:')
			or string.match(text2, '^%[Реклама Бизнеса%] Объявление:') then
			local text = string.format('{FFFFFF}%s{%s} %s', os.date('[%H:%M:%S]'), color1, text)
			text = fix_invalid_json_escapes(text)
			VIPandADchat.AddADMessage(text)
			return false
		end

		if string.match(text2, 'Отредактировал сотрудник СМИ %[') then
			local text = string.format('{FFFFFF}%s{%s} %s', os.date('[%H:%M:%S]'), color1, text)
			text = fix_invalid_json_escapes(text)
			VIPandADchat.SetLastADEdited(text)
			return false
		end

		if string.match(text2, '^Сообщение до редакции:') then
			local text = string.format('{FFFFFF}%s{%s} %s', os.date('[%H:%M:%S]'), color1, text)
			text = fix_invalid_json_escapes(text)
			VIPandADchat.SetLastADPreEdit(text)
			return false
		end
	end

	-- Здесь твоя логика обработки серверного сообщения
	-- например: фильтрация, изменение, логирование и т.д.
	-- text = string.gsub(text, "замена", "на что-то")
	-- print(text)
	if unwanted and unwanted.should_ignore(text) then
		return false -- глушим сообщение
	end

	return { color, text }
end

function onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
	-- Вызывается КАЖДЫЙ раз, когда сервер показывает диалоговое окно
	-- print(dialogid)
	-- print(style)
	-- print(title)
	-- print(button1)
	-- print(button2)
	-- print(text)
	return {dialogid, style, title, button1, button2, text, placeholder}
end

-- 2. JMP HOOK (через hooks.jmp на AddChatEntry)
local lhook, hook = pcall(require, 'hooks')

local function samp()
	return samp_mod
end

local originalChatAddEntry = nil
local CDialog_Close = nil

local function ChatAddEntryHooked(chat, type, szText, szPrefix, textColor, prefixColor)
	local text = ffi.string(szText)
	local text = u8(text)
	local text3 = u8:decode(text)
	local text_gsub = string.gsub(text, '[%p%c%s]', '')
	-- Твоя логика обработки текста чата
	-- Например, можно изменить szText и т.д.
	-- Для примера - ничего не меняем:

	return originalChatAddEntry(chat, type, szText, szPrefix, textColor, prefixColor)
end

local function CDialog_Close(this, button)
	-- -- Проверяем основные условия
	if samp().isDialogActive()
		and samp().pEditBox_active_func()
		and samp().get_dialog_caption():find(u8:decode('Редактирование'))
		and button == 1
	then
		-- print(samp().sampGetDialogEditboxText())
		-- -- Если таймер не активен, выходим
		if SMIHelp.timer_send then
			-- -- Получаем текст ввода
			local input = samp().sampGetDialogEditboxText()
			if input and not input:match("^%s*$") then
				local inputU8 = u8(input)
				SMIHelp.AddToHistory(inputU8)
				-- -- -- Удаляем все дубликаты
				-- -- local i = #config.table_config.last_text
				-- -- while i >= 1 do
				-- --	 if u8:decode(config.table_config.last_text[i]) == input then
				-- --		 table.remove(config.table_config.last_text, i)
				-- --	 end
				-- --	 i = i - 1
				-- -- end
				-- -- -- Вставляем один экземпляр в конец
				-- -- table.insert(config.table_config.last_text, inputU8)
				-- -- if #config.table_config.last_text > 101 then
				-- --	 table.remove(config.table_config.last_text, 1)
				-- -- end
				-- -- config.save()
			end

			-- -- local input = samp().sampGetDialogEditboxText()

			-- -- if input and not input:match("^%s*$") then -- Проверяем, что строка не пустая и не состоит только из пробелов
			-- --	 -- print(button, input)

			-- --	 -- Проверяем, сохранён ли текст ранее
			-- --	 local isNewText = true
			-- --	 for _, savedText in ipairs(config.table_config.last_text) do
			-- --		 if u8:decode(savedText) == input then -- Точное сравнение текста
			-- --			 isNewText = false
			-- --			 break
			-- --		 end
			-- --	 end

			-- --	 -- Если текст новый, сохраняем его
			-- --	 if isNewText then
			-- --		 table.insert(config.table_config.last_text, u8(input))
			-- --		 if #config.table_config.last_text > 101 then
			-- --			 table.remove(config.table_config.last_text, 1)
			-- --		 end
			-- --		 config.save()
			-- --	 end
			-- -- end

			-- -- Сбрасываем фильтр и запускаем таймер
			-- SMIHelp.pasr_find = 'ALL'
			-- SMIHelp.filter_SMI:Clear()

			lua_thread.create(function()
				SMIHelp.timer_send_clock = os.clock()
				SMIHelp.timer_send = false
				repeat
					wait(0)
				until (os.clock() - SMIHelp.timer_send_clock >= SMIHelp.timer_send_delay)
				SMIHelp.timer_send = true
			end)
		else
			return false
		end
	end
	CDialog_Close(this, button)
end

local function CInput_Send_hook(this, text)
	local raw = ffi.string(text) -- CP1251 от клиента
	local msg = u8(raw) -- теперь UTF-8, можно юзать tags.change_tags
	if tags and tags.change_tags then
		msg = tags.change_tags(msg)
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	CInput_Send_hook(this, back)
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
	local raw = ffi.string(text) -- CP1251 от клиента
	local msg = u8(raw) -- теперь UTF-8, можно юзать tags.change_tags
	if tags and tags.change_tags then
		msg = tags.change_tags(msg)
	end
	local back = u8:decode(msg) -- возвращаем обратно в CP1251
	CInput_SendSay_hook(this, back)
	-- local text = ffi.string(text)
	-- local text = tags.change_tags(text)
	-- -- local text = u8:decode(text)
	-- -- local text = u8(text)
	-- -- local text = u8(text)
	-- CInput_SendSay_hook(this, text)
end

local function CDamageManager_ApplyDamage(this, car, component, intensity, arg3)
	if not (component >= 1 and component <= 4) then
		return false
	end
	return CDamageManager_ApplyDamage(this, car, component, intensity, arg3)
end

function module.init()
	-- Включить hook на samp().events
	sampev.onServerMessage = onServerMessage
	sampev.onShowDialog = onShowDialog

	-- Включить JMP hook (detour) на AddChatEntry
	if lhook and samp_mod and samp_mod.sampModule and samp_mod.main_offsets and samp_mod.currentVersion then
		originalChatAddEntry = hook.jmp.new(
			"void(__thiscall*)(void* chat, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor)",
			ChatAddEntryHooked,
			samp_mod.sampModule + samp_mod.main_offsets.AddEntry[samp_mod.currentVersion]
		)
		CDialog_Close = hook.jmp.new(
			"void(__thiscall *)(uintptr_t, char)",
			CDialog_Close,
			samp_mod.sampModule + samp_mod.main_offsets.CDialog_Close[samp_mod.currentVersion]
		)

		-- CDialog_Show = hook.jmp.new("void(__thiscall *)(uintptr_t, int, int, const char*, const char*, const char*, const char*, bool)", CDialog_Show, samp().sampModule + samp().main_offsets.CDialog_Show[samp().currentVersion])
		CInput_Send_hook = hook.jmp.new(
			"void(__thiscall *)(uintptr_t, const char*)",
			CInput_Send_hook,
			samp_mod.sampModule + samp_mod.main_offsets.CInput_Send[samp_mod.currentVersion]
		)
		CInput_SendSay_hook = hook.jmp.new(
			"void(__thiscall *)(uintptr_t, const char*)",
			CInput_SendSay_hook,
			samp_mod.sampModule + samp_mod.main_offsets.CInput_SendSay[samp_mod.currentVersion]
		)
		CDamageManager_ApplyDamage = hook.jmp.new(
			"bool(__thiscall*)(uintptr_t this, uintptr_t car, int component, float intensity, float arg3)",
			CDamageManager_ApplyDamage,
			0x6C24B0
		)

		-- AttachObjectToBone = hook.jmp.new("void(__cdecl*)(uintptr_t, uintptr_t, int)", AttachObjectToBone, 0x5B0450)
	end
end

function module.deinit()
	-- Отключить хуки (чтобы можно было перезагрузить без рестарта)
	-- sampev.onServerMessage = nil

	-- if originalChatAddEntry and originalChatAddEntry.disable then
	--	 originalChatAddEntry.stop()
	--	 originalChatAddEntry = nil
	-- end
end

return module
