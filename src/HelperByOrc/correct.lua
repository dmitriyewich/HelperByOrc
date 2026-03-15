local module = {}

local imgui = require("mimgui")

local encoding = require("encoding")

local wm = require("windows.message")

local vk = require("vkeys")

local paths = require("HelperByOrc.paths")
local HotkeyManager = require("HelperByOrc.hotkey_manager")

encoding.default = "CP1251"
local u8 = encoding.UTF8

local dlstatus = require("moonloader").download_status

local funcs
local mimgui_funcs
local samp_module
local config_manager
local event_bus
local toasts_module = {
	push = function() end,
}

local hotkey_helpers
local normalizeHotkeyTable = function(tbl)
	return tbl
end
local hotkeyToString = function(keys)
	local out = {}
	for i = 1, #(keys or {}) do
		out[#out + 1] = tostring(keys[i])
	end
	if #out == 0 then
		return "[не назначено]"
	end
	return table.concat(out, " + ")
end

local CONFIG_PATH_REL = "correct.json"

local PROVIDER_YANDEX = "yandex"
local PROVIDER_LANGUAGETOOL = "languagetool"

module.PROVIDER_YANDEX = PROVIDER_YANDEX
module.PROVIDER_LANGUAGETOOL = PROVIDER_LANGUAGETOOL

local HOTKEY_TARGET_YANDEX = "yandex"
local HOTKEY_TARGET_LANGUAGETOOL = "languagetool"

local DEFAULT_HOTKEY_YANDEX = { vk.VK_MENU, vk.VK_X }
local DEFAULT_HOTKEY_LANGUAGETOOL = { vk.VK_MENU, vk.VK_D }

local function cloneKeys(list)
	local result = {}
	for i = 1, #(list or {}) do
		if type(list[i]) == "number" then
			result[#result + 1] = list[i]
		end
	end
	return result
end

local cfgDefaults = {
	provider = PROVIDER_YANDEX,
	hotkeyYandex = cloneKeys(DEFAULT_HOTKEY_YANDEX),
	hotkeyLanguageTool = cloneKeys(DEFAULT_HOTKEY_LANGUAGETOOL),
}

local cfg = {
	provider = cfgDefaults.provider,
	hotkeyYandex = cloneKeys(cfgDefaults.hotkeyYandex),
	hotkeyLanguageTool = cloneKeys(cfgDefaults.hotkeyLanguageTool),
}

local cacheSpeller = {}
local cacheLT = {}

local ltLimits = {
	windowStart = 0,
	reqCount = 0,
	bytesThisMin = 0,
	MAX_REQ_PER_MIN = 20,
	MAX_BYTES_PER_MIN = 75 * 1024,
	MAX_BYTES_PER_REQ = 20 * 1024,
}

local correctKeyTracker = HotkeyManager.newKeyTracker()
local yandexHotkeyActive = false
local languageToolHotkeyActive = false

local hotkeyProcessingEnabled = false
local captureYandex
local captureLT
local activeCaptureTarget = nil
local configLoaded = false
local moduleEnabled = true

local function trim(s)
	if funcs and type(funcs.trim) == "function" then
		return funcs.trim(s)
	end
	return tostring(s or ""):match("^%s*(.-)%s*$")
end

local function normalizeProvider(value)
	local provider = tostring(value or ""):lower()
	if provider == "lt" or provider == "language_tool" or provider == PROVIDER_LANGUAGETOOL then
		return PROVIDER_LANGUAGETOOL
	end
	return PROVIDER_YANDEX
end

local function providerLabel(provider)
	provider = normalizeProvider(provider)
	if provider == PROVIDER_LANGUAGETOOL then
		return "LanguageTool"
	end
	return "Yandex Speller"
end

local function getTargetLabel(target)
	if target == HOTKEY_TARGET_LANGUAGETOOL then
		return "LanguageTool"
	end
	return "Yandex Speller"
end

local function pushToast(message, kind)
	local msg = tostring(message or "")
	local k = kind or "warn"
	if event_bus then
		event_bus.emit("toast", msg, k)
		return
	end
	if toasts_module and type(toasts_module.push) == "function" then
		toasts_module.push(msg, k)
		return
	end
	if sampAddChatMessage then
		sampAddChatMessage(u8:decode("[Автокоррект] " .. msg), 0xFFFFAA00)
	end
end

local function refreshHotkeyHelpers()
	if funcs and type(funcs.getHotkeyHelpers) == "function" then
		hotkey_helpers = funcs.getHotkeyHelpers(vk, "[не назначено]")
		normalizeHotkeyTable = hotkey_helpers.normalizeHotkeyTable
		hotkeyToString = hotkey_helpers.hotkeyToString
	else
	end
end

local function normalizeCombo(value, fallback)
	local combo = normalizeHotkeyTable and normalizeHotkeyTable(value) or nil
	if type(combo) ~= "table" or #combo == 0 then
		combo = cloneKeys(fallback)
	end
	return combo
end

local function syncDependencies(modules)
	modules = modules or {}

	funcs = modules.funcs or funcs or require("HelperByOrc.funcs")

	mimgui_funcs = modules.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")

	samp_module = modules.samp or samp_module
	toasts_module = modules.toasts or toasts_module

	refreshHotkeyHelpers()
end

local function normalizeLoadedConfig(loaded)
	if type(loaded) ~= "table" then
		loaded = cfgDefaults
	end
	return {
		provider = normalizeProvider(loaded.provider),
		hotkeyYandex = cloneKeys(normalizeCombo(loaded.hotkeyYandex or loaded.hotkey_yandex, DEFAULT_HOTKEY_YANDEX)),
		hotkeyLanguageTool = cloneKeys(normalizeCombo(
			loaded.hotkeyLanguageTool or loaded.hotkey_languagetool or loaded.hotkeyLT,
			DEFAULT_HOTKEY_LANGUAGETOOL
		)),
	}
end

local function serializeConfig(data)
	return {
		provider = data.provider,
		hotkeyYandex = cloneKeys(data.hotkeyYandex),
		hotkeyLanguageTool = cloneKeys(data.hotkeyLanguageTool),
	}
end

local function saveConfig()
	if config_manager then
		config_manager.markDirty("correct")
	elseif funcs and type(funcs.saveTableToJson) == "function" then
		funcs.saveTableToJson(serializeConfig(cfg), paths.dataPath(CONFIG_PATH_REL))
	end
end

local function loadConfig()
	if config_manager then
		cfg = config_manager.get("correct") or cfg
	else
		local loaded = cfgDefaults
		if funcs and type(funcs.loadTableFromJson) == "function" then
			loaded = funcs.loadTableFromJson(CONFIG_PATH_REL, cfgDefaults)
		end
		local normalized = normalizeLoadedConfig(loaded)
		cfg.provider = normalized.provider
		cfg.hotkeyYandex = normalized.hotkeyYandex
		cfg.hotkeyLanguageTool = normalized.hotkeyLanguageTool
	end
	configLoaded = true
end

local function setTargetHotkey(target, combo)
	local normalized
	if target == HOTKEY_TARGET_LANGUAGETOOL then
		normalized = normalizeCombo(combo, DEFAULT_HOTKEY_LANGUAGETOOL)
		cfg.hotkeyLanguageTool = normalized
	else
		normalized = normalizeCombo(combo, DEFAULT_HOTKEY_YANDEX)
		cfg.hotkeyYandex = normalized
	end
	saveConfig()
	return cloneKeys(normalized)
end

local function getTargetHotkey(target)
	if target == HOTKEY_TARGET_LANGUAGETOOL then
		return cfg.hotkeyLanguageTool
	end
	return cfg.hotkeyYandex
end

local function makeCaptureForTarget(target)
	return HotkeyManager.new({
		max_keys = 4,
		timeout_sec = 10,
		on_save = function(keys)
			local label = getTargetLabel(target)
			setTargetHotkey(target, keys)
			pushToast("Горячая клавиша обновлена: " .. label .. ".", "ok")
		end,
		on_timeout = function()
			pushToast("Режим захвата клавиш завершен по таймауту.", "warn")
		end,
	})
end

captureYandex = makeCaptureForTarget(HOTKEY_TARGET_YANDEX)
captureLT = makeCaptureForTarget(HOTKEY_TARGET_LANGUAGETOOL)

local function startCapture(target)
	if target == HOTKEY_TARGET_LANGUAGETOOL then
		captureYandex:stop()
		captureLT:start(getTargetHotkey(target))
	else
		captureLT:stop()
		captureYandex:start(getTargetHotkey(target))
	end
	activeCaptureTarget = target
end

local function stopCapture()
	captureYandex:stop()
	captureLT:stop()
	activeCaptureTarget = nil
end

local function getActiveCapture()
	if captureYandex:isActive() then
		return captureYandex
	end
	if captureLT:isActive() then
		return captureLT
	end
	return nil
end

local function urlencode(text)
	text = tostring(text)
	text = text
		:gsub("{......}", "")
		:gsub(" ", "+")
		:gsub("\n", "%%0A")
		:gsub("&gt;", ">")
		:gsub("&lt;", "<")
		:gsub("&quot;", '"')
	return text
end

local _reqCounter = 0
local function asyncRequest(url, resolve, reject)
	reject = reject or function(err)
		pushToast(err or "Ошибка запроса.", "err")
	end

	_reqCounter = _reqCounter + 1
	local tmpName = string.format("cw_req_%d_%d.tmp", _reqCounter, math.random(100000, 999999))
	local tmpPath = paths.join(paths.projectRoot(), tmpName)

	downloadUrlToFile(url, tmpPath, function(id, status, p1, p2)
		if status == dlstatus.STATUS_ENDDOWNLOADDATA then
			local f = io.open(tmpPath, "rb")
			if not f then
				os.remove(tmpPath)
				reject("Не удалось открыть временный файл.")
				return
			end

			local data = f:read("*a") or ""
			f:close()
			os.remove(tmpPath)

			if #data > 0 then
				resolve(data)
			else
				reject("Пустой ответ от сервера.")
			end
		elseif status == dlstatus.STATUS_ERROR then
			os.remove(tmpPath)
			reject("Ошибка загрузки.")
		end
	end)
end

local function ltReserve(bytes)
	local now = os.clock()
	if ltLimits.windowStart == 0 or (now - ltLimits.windowStart) >= 60.0 then
		ltLimits.windowStart = now
		ltLimits.reqCount = 0
		ltLimits.bytesThisMin = 0
	end

	if bytes > ltLimits.MAX_BYTES_PER_REQ then
		return false, "text_too_long"
	end
	if ltLimits.reqCount + 1 > ltLimits.MAX_REQ_PER_MIN then
		return false, "too_many_requests"
	end
	if ltLimits.bytesThisMin + bytes > ltLimits.MAX_BYTES_PER_MIN then
		return false, "too_much_text"
	end

	ltLimits.reqCount = ltLimits.reqCount + 1
	ltLimits.bytesThisMin = ltLimits.bytesThisMin + bytes
	return true
end

local function ltSanitizeMessage(message)
	message = trim(message or "")
	if message == "" then
		return ""
	end
	return message:gsub("{......}", "")
end

local function urlencode_utf8(str)
	return (
		str:gsub("([^%w%-_%.%~ ])", function(c)
			return string.format("%%%02X", string.byte(c))
		end):gsub(" ", "+")
	)
end

local function buildUtf8Index(str)
	local index = {}
	local len = 0
	local i = 1
	local strLen = #str

	while i <= strLen do
		len = len + 1
		index[len] = i
		local c = str:byte(i)
		if c < 0x80 then
			i = i + 1
		elseif c < 0xE0 then
			i = i + 2
		elseif c < 0xF0 then
			i = i + 3
		else
			i = i + 4
		end
	end

	index[len + 1] = strLen + 1
	return index
end

local function applyLanguageToolMatches(originalUtf8, matches)
	if not matches or #matches == 0 then
		return originalUtf8
	end

	local index = buildUtf8Index(originalUtf8)
	local strLenBytes = #originalUtf8

	table.sort(matches, function(a, b)
		return (a.offset or 0) < (b.offset or 0)
	end)

	local parts = {}
	local currBytePos = 1
	local used = 0

	for _, match in ipairs(matches) do
		if used >= 30 then
			break
		end

		local repls = match.replacements
		local offset = match.offset
		local length = match.length

		if type(offset) == "number" and type(length) == "number" and repls and repls[1] and repls[1].value then
			local repl = repls[1].value
			local charStart = offset + 1
			local charEnd = offset + length

			local byteStart = index[charStart]
			local byteEndPlus1 = index[charEnd + 1] or (strLenBytes + 1)

			if byteStart and byteEndPlus1 and byteStart >= currBytePos then
				table.insert(parts, originalUtf8:sub(currBytePos, byteStart - 1))
				table.insert(parts, repl)
				currBytePos = byteEndPlus1
				used = used + 1
			end
		end
	end

	table.insert(parts, originalUtf8:sub(currBytePos))
	return table.concat(parts)
end

local function applyToChat(provider)
	if
		type(sampIsChatInputActive) ~= "function"
		or type(sampGetChatInputText) ~= "function"
		or type(sampSetChatInputText) ~= "function"
	then
		return false
	end

	if not sampIsChatInputActive() then
		return false
	end

	local message = sampGetChatInputText() or ""
	module.handleAuto(message, function(newText)
		if type(newText) == "string" then
			sampSetChatInputText(newText)
		end
	end, provider)
	return true
end

local function applyToDialogViaModule(provider)
	if type(samp_module) ~= "table" then
		return false
	end
	if type(samp_module.isDialogActive) ~= "function" or not samp_module.isDialogActive() then
		return false
	end
	if type(samp_module.pEditBox_active_func) == "function" and not samp_module.pEditBox_active_func() then
		return false
	end
	if type(samp_module.sampGetDialogEditboxText) ~= "function" then
		return false
	end
	if type(samp_module.sampSetDialogEditboxText) ~= "function" then
		return false
	end

	local message = samp_module.sampGetDialogEditboxText() or ""
	module.handleAuto(message, function(newText)
		if type(newText) == "string" then
			samp_module.sampSetDialogEditboxText(newText)
		end
	end, provider)
	return true
end

local function applyToDialogViaGlobals(provider)
	if
		type(sampIsDialogActive) ~= "function"
		or type(sampGetCurrentDialogEditboxText) ~= "function"
		or type(sampSetCurrentDialogEditboxText) ~= "function"
	then
		return false
	end
	if not sampIsDialogActive() then
		return false
	end

	local message = sampGetCurrentDialogEditboxText() or ""
	module.handleAuto(message, function(newText)
		if type(newText) == "string" then
			sampSetCurrentDialogEditboxText(newText)
		end
	end, provider)
	return true
end

local function clearPressedKeys()
	correctKeyTracker:reset()
	yandexHotkeyActive = false
	languageToolHotkeyActive = false
end

function module.resetInputState(reason)
	clearPressedKeys()
	if getActiveCapture() then
		stopCapture()
	end
end

local function processConfiguredHotkeys(isKeyDownMsg, msg, wparam)
	correctKeyTracker:onWindowMessage(msg, wparam)

	if not moduleEnabled then
		yandexHotkeyActive = false
		languageToolHotkeyActive = false
		return
	end

	local pressed = correctKeyTracker:getOrdered()
	local yandexNow = HotkeyManager.comboMatchOrdered(pressed, cfg.hotkeyYandex)
	local languageToolNow = HotkeyManager.comboMatchOrdered(pressed, cfg.hotkeyLanguageTool)

	if not yandexNow then
		yandexHotkeyActive = false
	end
	if not languageToolNow then
		languageToolHotkeyActive = false
	end

	if not isKeyDownMsg then
		return
	end

	local triggered = false

	if yandexNow and not yandexHotkeyActive then
		yandexHotkeyActive = true
		triggered = module.applyToActiveInput(PROVIDER_YANDEX)
	end

	if languageToolNow and not languageToolHotkeyActive then
		languageToolHotkeyActive = true
		if not triggered then
			module.applyToActiveInput(PROVIDER_LANGUAGETOOL)
		end
	end
end

local function onWindowMessage(msg, wparam)
	if not hotkeyProcessingEnabled and not getActiveCapture() then
		return false
	end

	local isKeyDownMsg = msg == wm.WM_KEYDOWN or msg == wm.WM_SYSKEYDOWN
	local isKeyUpMsg = msg == wm.WM_KEYUP or msg == wm.WM_SYSKEYUP
	if not (isKeyDownMsg or isKeyUpMsg) then
		return false
	end

	local cwm = type(consumeWindowMessage) == "function" and consumeWindowMessage or nil
	local activeCapture = getActiveCapture()
	if activeCapture then
		if activeCapture:onWindowMessage(msg, wparam, cwm) then
			return true
		end
		if not activeCapture:isActive() then
			activeCaptureTarget = nil
		end
	end

	processConfiguredHotkeys(isKeyDownMsg, msg, wparam)
	return false
end

function module.clearCache()
	for key in pairs(cacheSpeller) do
		cacheSpeller[key] = nil
	end
	for key in pairs(cacheLT) do
		cacheLT[key] = nil
	end
end

function module.getProvider()
	return cfg.provider
end

function module.getProviderLabel(provider)
	return providerLabel(provider)
end

function module.getActiveProviderLabel()
	return providerLabel(cfg.provider)
end

function module.setProvider(provider)
	local normalized = normalizeProvider(provider)
	if cfg.provider ~= normalized then
		cfg.provider = normalized
		saveConfig()
	end
	return cfg.provider
end

function module.getYandexHotkey()
	return cloneKeys(cfg.hotkeyYandex)
end

function module.getLanguageToolHotkey()
	return cloneKeys(cfg.hotkeyLanguageTool)
end

function module.setYandexHotkey(combo)
	return setTargetHotkey(HOTKEY_TARGET_YANDEX, combo)
end

function module.setLanguageToolHotkey(combo)
	return setTargetHotkey(HOTKEY_TARGET_LANGUAGETOOL, combo)
end

function module.applyToActiveInput(provider)
	if applyToChat(provider) then
		return true
	end
	if applyToDialogViaModule(provider) then
		return true
	end
	if applyToDialogViaGlobals(provider) then
		return true
	end
	return false
end

function module.handleCorrection(message, setText)
	if type(setText) ~= "function" then
		return
	end

	message = trim(message or "")
	if message == "" then
		return
	end

	if cacheSpeller[message] then
		setText(cacheSpeller[message])
		return
	end

	local url = "http://speller.yandex.net/services/spellservice.json/checkText?text=" .. u8(urlencode(message))

	asyncRequest(url, function(response)
		local words = decodeJson(response)
		if words and type(words) == "table" and #words > 0 then
			local used = {}
			local corrected = message

			for _, word_data in ipairs(words) do
				local incorrect = u8:decode(word_data.word)
				local correct = word_data.s and word_data.s[1] and u8:decode(word_data.s[1]) or incorrect
				if not used[incorrect] and correct then
					corrected = corrected:gsub(incorrect, correct)
					used[incorrect] = true
				end
			end

			corrected = corrected:gsub("//", "/")
			cacheSpeller[message] = corrected
			setText(corrected)
		end
	end, function(err)
		pushToast(("Yandex Speller: %s"):format(err or "ошибка запроса."), "err")
	end)
end

function module.handleLanguageTool(message, setText)
	if type(setText) ~= "function" then
		return
	end

	message = ltSanitizeMessage(message)
	if message == "" then
		return
	end

	if cacheLT[message] then
		setText(cacheLT[message])
		return
	end

	local textUtf8 = u8(message)
	local textBytes = #textUtf8

	local allowed, reason = ltReserve(textBytes)
	if not allowed then
		local messageText
		if reason == "text_too_long" then
			messageText = "LanguageTool: текст длиннее 20KB, запрос не отправлен."
		elseif reason == "too_many_requests" then
			messageText = "LanguageTool: превышен лимит 20 запросов в минуту."
		elseif reason == "too_much_text" then
			messageText = "LanguageTool: превышен лимит 75KB текста в минуту."
		else
			messageText = "LanguageTool: лимит, запрос отклонён."
		end
		pushToast(messageText, "warn")
		return
	end

	local encodedText = urlencode_utf8(textUtf8)
	local url = "https://api.languagetool.org/v2/check?language=ru-RU&text=" .. encodedText

	asyncRequest(url, function(response)
		local data = decodeJson(response)
		if not data or type(data) ~= "table" or type(data.matches) ~= "table" or #data.matches == 0 then
			return
		end

		local fixedUtf8 = applyLanguageToolMatches(textUtf8, data.matches)
		local resultCp = u8:decode(fixedUtf8)

		cacheLT[message] = resultCp
		setText(resultCp)
	end, function(err)
		pushToast(("LanguageTool: %s"):format(err or "ошибка запроса."), "err")
	end)
end

function module.handleAuto(message, setText, provider)
	local selected = normalizeProvider(provider or cfg.provider)
	if selected == PROVIDER_LANGUAGETOOL then
		return module.handleLanguageTool(message, setText)
	end
	return module.handleCorrection(message, setText)
end

function module.DrawSettingsInline()
	if not (imgui and imgui.CollapsingHeader) then
		return
	end
	local headerOpen = imgui.CollapsingHeader("Автокорректор")

	-- Автоматическое завершение захвата при закрытии секции
	if not headerOpen and activeCaptureTarget then
		stopCapture()
	end

	if not headerOpen then
		return
	end

	imgui.Text("Провайдер по кнопке \"Автокоррекция\":")
	if imgui.RadioButtonBool("Yandex Speller##autocorrect_provider_yandex", cfg.provider == PROVIDER_YANDEX) then
		module.setProvider(PROVIDER_YANDEX)
	end
	if imgui.RadioButtonBool(
		"LanguageTool##autocorrect_provider_languagetool",
		cfg.provider == PROVIDER_LANGUAGETOOL
	) then
		module.setProvider(PROVIDER_LANGUAGETOOL)
	end

	local currentProviderText = "Текущий: " .. module.getActiveProviderLabel()
	if mimgui_funcs and type(mimgui_funcs.imgui_text_colored_safe) == "function" then
		mimgui_funcs.imgui_text_colored_safe(imgui.ImVec4(0.75, 0.9, 1, 1), currentProviderText)
	else
		imgui.Text(currentProviderText)
	end

	imgui.Separator()
	imgui.Text("Горячие клавиши (чат/диалог):")

	imgui.Text("Yandex Speller:")
	imgui.SameLine()
	if mimgui_funcs and type(mimgui_funcs.imgui_text_colored_safe) == "function" then
		mimgui_funcs.imgui_text_colored_safe(imgui.ImVec4(0.9, 0.9, 0.6, 1), hotkeyToString(cfg.hotkeyYandex))
	else
		imgui.Text(hotkeyToString(cfg.hotkeyYandex))
	end
	imgui.SameLine()
	if imgui.SmallButton("Изменить##correct_hotkey_edit_yandex") then
		startCapture(HOTKEY_TARGET_YANDEX)
	end
	imgui.SameLine()
	if imgui.SmallButton("Сброс##correct_hotkey_reset_yandex") then
		module.setYandexHotkey(DEFAULT_HOTKEY_YANDEX)
	end

	imgui.Text("LanguageTool:")
	imgui.SameLine()
	if mimgui_funcs and type(mimgui_funcs.imgui_text_colored_safe) == "function" then
		mimgui_funcs.imgui_text_colored_safe(imgui.ImVec4(0.9, 0.9, 0.6, 1), hotkeyToString(cfg.hotkeyLanguageTool))
	else
		imgui.Text(hotkeyToString(cfg.hotkeyLanguageTool))
	end
	imgui.SameLine()
	if imgui.SmallButton("Изменить##correct_hotkey_edit_languagetool") then
		startCapture(HOTKEY_TARGET_LANGUAGETOOL)
	end
	imgui.SameLine()
	if imgui.SmallButton("Сброс##correct_hotkey_reset_languagetool") then
		module.setLanguageToolHotkey(DEFAULT_HOTKEY_LANGUAGETOOL)
	end

	local activeCapture = getActiveCapture()
	if activeCapture then
		local text_colored_fn = (mimgui_funcs and type(mimgui_funcs.imgui_text_colored_safe) == "function")
			and mimgui_funcs.imgui_text_colored_safe or nil
		imgui.Text("Запись хоткея: " .. getTargetLabel(activeCaptureTarget))
		HotkeyManager.drawCaptureUI(activeCapture, "correct_hotkey", text_colored_fn)
	end
end

function module.start()
	hotkeyProcessingEnabled = true
end

function module.setEnabled(state)
	moduleEnabled = not not state
	if not moduleEnabled then
		module.resetInputState("disabled")
	end
end

function module.isEnabled()
	return moduleEnabled
end

function module.onTerminate(reason)
	module.resetInputState("terminate")
	hotkeyProcessingEnabled = false
	if event_bus then
		event_bus.offByOwner("correct")
	end
end

function module.onWindowMessage(msg, wparam, lparam)
	return onWindowMessage(msg, wparam, lparam)
end

function module.attachModules(modules)
	syncDependencies(modules)
	config_manager = modules.config_manager
	event_bus = modules.event_bus
	if event_bus then
		event_bus.offByOwner("correct")
	end
	if config_manager and not configLoaded then
		cfg = config_manager.register("correct", {
			path = CONFIG_PATH_REL,
			defaults = cfgDefaults,
			normalize = normalizeLoadedConfig,
			serialize = serializeConfig,
		})
		configLoaded = true
	elseif not configLoaded then
		loadConfig()
	end
end

syncDependencies()

if not configLoaded then
	loadConfig()
end

return module
