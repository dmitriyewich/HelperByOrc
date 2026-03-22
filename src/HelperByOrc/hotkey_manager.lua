local HotkeyManager = {}

local imgui = require("mimgui")
local bit = require("bit")
local vk = require("vkeys")
local wm = require("windows.message")
local funcs = require("HelperByOrc.funcs")
local language = require("language")

local function tr(key, params)
	return language.getText(key, params)
end

local helpers = funcs.getHotkeyHelpers(vk, tr("hotkey_manager.capture.placeholder"))
local normalizeKey = helpers.normalizeKey
local isKeyboardKey = helpers.isKeyboardKey
local isMouseKey = helpers.isMouseKey or function()
	return false
end
local isHotkeyKey = helpers.isHotkeyKey or isKeyboardKey

local function hotkeyToString(keys)
	return funcs.hotkeyToString(keys, vk, tr("hotkey_manager.capture.placeholder"))
end

HotkeyManager.normalizeKey = normalizeKey
HotkeyManager.isKeyboardKey = isKeyboardKey
HotkeyManager.isMouseKey = isMouseKey
HotkeyManager.isHotkeyKey = isHotkeyKey
HotkeyManager.hotkeyToString = hotkeyToString

local WM_SYSKEYDOWN = wm.WM_SYSKEYDOWN or 0x0104
local WM_SYSKEYUP = wm.WM_SYSKEYUP or 0x0105
local WM_LBUTTONDOWN = wm.WM_LBUTTONDOWN or 0x0201
local WM_LBUTTONUP = wm.WM_LBUTTONUP or 0x0202
local WM_LBUTTONDBLCLK = wm.WM_LBUTTONDBLCLK or 0x0203
local WM_RBUTTONDOWN = wm.WM_RBUTTONDOWN or 0x0204
local WM_RBUTTONUP = wm.WM_RBUTTONUP or 0x0205
local WM_RBUTTONDBLCLK = wm.WM_RBUTTONDBLCLK or 0x0206
local WM_MBUTTONDOWN = wm.WM_MBUTTONDOWN or 0x0207
local WM_MBUTTONUP = wm.WM_MBUTTONUP or 0x0208
local WM_MBUTTONDBLCLK = wm.WM_MBUTTONDBLCLK or 0x0209
local WM_XBUTTONDOWN = wm.WM_XBUTTONDOWN or 0x020B
local WM_XBUTTONUP = wm.WM_XBUTTONUP or 0x020C
local WM_XBUTTONDBLCLK = wm.WM_XBUTTONDBLCLK or 0x020D

HotkeyManager.MODE_MODIFIER_TRIGGER = "modifier_trigger"
HotkeyManager.MODE_ORDERED = "ordered_combo"

local function cloneKeys(list)
	local result = {}
	for i = 1, #(list or {}) do
		if type(list[i]) == "number" then
			result[#result + 1] = list[i]
		end
	end
	return result
end

HotkeyManager.cloneKeys = cloneKeys

local modifierSortOrder = {
	[vk.VK_CONTROL] = 1,
	[vk.VK_SHIFT] = 2,
	[vk.VK_MENU] = 3,
	[vk.VK_LWIN] = 4,
	[vk.VK_RWIN] = 5,
}

local mouseDownMessages = {
	[WM_LBUTTONDOWN] = vk.VK_LBUTTON,
	[WM_LBUTTONDBLCLK] = vk.VK_LBUTTON,
	[WM_RBUTTONDOWN] = vk.VK_RBUTTON,
	[WM_RBUTTONDBLCLK] = vk.VK_RBUTTON,
	[WM_MBUTTONDOWN] = vk.VK_MBUTTON,
	[WM_MBUTTONDBLCLK] = vk.VK_MBUTTON,
}

local mouseUpMessages = {
	[WM_LBUTTONUP] = vk.VK_LBUTTON,
	[WM_RBUTTONUP] = vk.VK_RBUTTON,
	[WM_MBUTTONUP] = vk.VK_MBUTTON,
}

local function normalizeMode(mode)
	if mode == HotkeyManager.MODE_ORDERED then
		return HotkeyManager.MODE_ORDERED
	end
	return HotkeyManager.MODE_MODIFIER_TRIGGER
end

HotkeyManager.normalizeMode = normalizeMode

local function isModifierKey(keyCode)
	local nk = normalizeKey(keyCode)
	return modifierSortOrder[nk] ~= nil
end

HotkeyManager.isModifierKey = isModifierKey

local function comboHasMouse(keys)
	for i = 1, #(keys or {}) do
		if isMouseKey(normalizeKey(keys[i])) then
			return true
		end
	end
	return false
end

HotkeyManager.comboHasMouse = comboHasMouse

local function sortModifierKeys(a, b)
	local ao = modifierSortOrder[a] or 100
	local bo = modifierSortOrder[b] or 100
	if ao ~= bo then
		return ao < bo
	end
	return a < b
end

local function resolveXButtonKey(wparam)
	local value = tonumber(wparam) or 0
	local button = bit.rshift(bit.band(value, 0xFFFF0000), 16)
	if button == 1 then
		return vk.VK_XBUTTON1
	end
	if button == 2 then
		return vk.VK_XBUTTON2
	end
	return nil
end

function HotkeyManager.getMessageKeyInfo(msg, wparam)
	local keyCode
	local isDown = false
	local isUp = false

	if msg == wm.WM_KEYDOWN or msg == WM_SYSKEYDOWN then
		keyCode = wparam
		isDown = true
	elseif msg == wm.WM_KEYUP or msg == WM_SYSKEYUP then
		keyCode = wparam
		isUp = true
	elseif mouseDownMessages[msg] then
		keyCode = mouseDownMessages[msg]
		isDown = true
	elseif mouseUpMessages[msg] then
		keyCode = mouseUpMessages[msg]
		isUp = true
	elseif msg == WM_XBUTTONDOWN or msg == WM_XBUTTONDBLCLK then
		keyCode = resolveXButtonKey(wparam)
		isDown = true
	elseif msg == WM_XBUTTONUP then
		keyCode = resolveXButtonKey(wparam)
		isUp = true
	end

	if not keyCode then
		return nil
	end

	local nk = normalizeKey(keyCode)
	if not isHotkeyKey(nk) then
		return nil
	end

	return {
		keyCode = nk,
		isDown = isDown,
		isUp = isUp,
	}
end

---------------------------------------------------------------------------
-- KeyTracker: отслеживает зажатые клавиши с сохранением порядка нажатия.
-- Используется и для захвата, и для детекции комбинаций.
---------------------------------------------------------------------------

function HotkeyManager.newKeyTracker()
	local kt = {
		_held = {},      -- keyCode -> press_order (number)
		_ordered = {},   -- список зажатых в порядке нажатия
		_counter = 0,    -- монотонный счётчик
	}

	function kt:keyDown(keyCode)
		local nk = normalizeKey(keyCode)
		if not isHotkeyKey(nk) then
			return
		end
		if not self._held[nk] then
			self._counter = self._counter + 1
			self._held[nk] = self._counter
			self:_rebuild()
		end
	end

	function kt:keyUp(keyCode)
		local nk = normalizeKey(keyCode)
		if self._held[nk] then
			self._held[nk] = nil
			self:_rebuild()
			if self:count() == 0 then
				self._counter = 0
			end
		end
	end

	function kt:_rebuild()
		local list = {}
		for k, ord in pairs(self._held) do
			list[#list + 1] = { key = k, order = ord }
		end
		table.sort(list, function(a, b) return a.order < b.order end)
		self._ordered = {}
		for i = 1, #list do
			self._ordered[i] = list[i].key
		end
	end

	function kt:getOrdered()
		return self._ordered
	end

	function kt:count()
		return #self._ordered
	end

	function kt:isHeld(keyCode)
		return self._held[normalizeKey(keyCode)] ~= nil
	end

	function kt:reset()
		self._held = {}
		self._ordered = {}
		self._counter = 0
	end

	--- Обработать WM сообщение (keydown/keyup).
	-- Возвращает true если состояние изменилось.
	function kt:onWindowMessage(msg, wparam)
		local keyInfo = HotkeyManager.getMessageKeyInfo(msg, wparam)
		if not keyInfo then
			return false
		end
		if keyInfo.isDown then
			self:keyDown(keyInfo.keyCode)
		else
			self:keyUp(keyInfo.keyCode)
		end
		return true
	end

	return kt
end

---------------------------------------------------------------------------
-- comboMatchOrdered: порядок клавиш имеет значение.
-- Сравнивает упорядоченный список зажатых клавиш с сохранённой комбинацией.
-- Совпадение: одинаковая длина + одинаковый порядок (с нормализацией).
---------------------------------------------------------------------------

function HotkeyManager.comboMatchOrdered(pressedOrdered, combo)
	if type(combo) ~= "table" or #combo == 0 then
		return false
	end
	if #pressedOrdered ~= #combo then
		return false
	end
	for i = 1, #combo do
		if normalizeKey(pressedOrdered[i]) ~= normalizeKey(combo[i]) then
			return false
		end
	end
	return true
end

function HotkeyManager.normalizeComboForMode(keys, mode)
	local normalized = {}
	local seen = {}

	for i = 1, #(keys or {}) do
		local nk = normalizeKey(keys[i])
		if isHotkeyKey(nk) and not seen[nk] then
			normalized[#normalized + 1] = nk
			seen[nk] = true
		end
	end

	mode = normalizeMode(mode)
	if mode == HotkeyManager.MODE_ORDERED then
		return normalized
	end

	local modifiers = {}
	local triggers = {}
	for i = 1, #normalized do
		local nk = normalized[i]
		if isModifierKey(nk) then
			modifiers[#modifiers + 1] = nk
		else
			triggers[#triggers + 1] = nk
		end
	end

	table.sort(modifiers, sortModifierKeys)

	local result = {}
	for i = 1, #modifiers do
		result[#result + 1] = modifiers[i]
	end
	for i = 1, #triggers do
		result[#result + 1] = triggers[i]
	end
	return result
end

function HotkeyManager.comboSignature(keys, mode)
	local normalized = HotkeyManager.normalizeComboForMode(keys, mode)
	return normalizeMode(mode) .. ":" .. table.concat(normalized, ",")
end

function HotkeyManager.comboMatch(pressedOrdered, combo, mode)
	mode = normalizeMode(mode)
	if mode == HotkeyManager.MODE_ORDERED then
		return HotkeyManager.comboMatchOrdered(pressedOrdered, combo)
	end

	if type(combo) ~= "table" or #combo == 0 then
		return false
	end

	local pressedNormalized = HotkeyManager.normalizeComboForMode(pressedOrdered, mode)
	local comboNormalized = HotkeyManager.normalizeComboForMode(combo, mode)
	if #pressedNormalized ~= #comboNormalized then
		return false
	end

	for i = 1, #comboNormalized do
		if pressedNormalized[i] ~= comboNormalized[i] then
			return false
		end
	end
	return true
end

---------------------------------------------------------------------------
-- Capture (захват комбинаций): hold-based.
-- Пользователь зажимает клавиши — они отображаются в UI.
-- При отпускании всех клавиш комбинация запоминается (но НЕ автосохраняется).
-- Сохранение: Enter / кнопка Save.
-- Очистка: Backspace.  Отмена: Esc.
--
-- opts:
--   timeout_sec  (number|nil) — таймаут (0 = нет)
--   on_save      (function)   — fn(keys)
--   on_cancel    (function|nil)
--   on_timeout   (function|nil)
--   enter_saves  (bool, default true) — Enter сохраняет комбинацию
---------------------------------------------------------------------------

function HotkeyManager.new(opts)
	opts = opts or {}
	local capture = {
		_active = false,
		_tracker = HotkeyManager.newKeyTracker(),
		_last_combo = {},
		_start_time = 0,
		_timeout_sec = opts.timeout_sec or 0,
		_on_save = opts.on_save,
		_on_cancel = opts.on_cancel,
		_on_timeout = opts.on_timeout,
		_enter_saves = opts.enter_saves ~= false,
		_mouse_capture_armed = false,
		_mouse_capture_pending_key = nil,
	}

	function capture:start(initial_keys)
		self._active = true
		self._tracker:reset()
		self._last_combo = cloneKeys(initial_keys)
		self._start_time = os.clock()
		self._mouse_capture_armed = false
		self._mouse_capture_pending_key = nil
	end

	function capture:stop()
		local was = self._active
		self._active = false
		self._tracker:reset()
		self._last_combo = {}
		self._start_time = 0
		self._mouse_capture_armed = false
		self._mouse_capture_pending_key = nil
		if was and self._on_cancel then
			self._on_cancel()
		end
	end

	function capture:isActive()
		return self._active
	end

	function capture:armMouseCapture()
		if not self._active then
			return
		end
		self._mouse_capture_armed = true
		self._mouse_capture_pending_key = nil
	end

	function capture:isMouseCaptureArmed()
		return self._mouse_capture_armed == true
	end

	function capture:getDraft()
		if self._tracker:count() > 0 then
			return self._tracker:getOrdered()
		end
		return self._last_combo
	end

	function capture:getDraftString()
		return hotkeyToString(self:getDraft())
	end

	function capture:save()
		if not self._active then
			return
		end
		local keys = cloneKeys(self:getDraft())
		self._active = false
		self._tracker:reset()
		self._last_combo = {}
		self._start_time = 0
		self._mouse_capture_armed = false
		self._mouse_capture_pending_key = nil
		if self._on_save and #keys > 0 then
			self._on_save(keys)
		end
	end

	function capture:clear()
		self._tracker:reset()
		self._last_combo = {}
		self._mouse_capture_armed = false
		self._mouse_capture_pending_key = nil
	end

	function capture:checkTimeout()
		if not self._active then
			return false
		end
		if self._timeout_sec > 0 and self._start_time > 0 then
			if (os.clock() - self._start_time) > self._timeout_sec then
				self._active = false
				self._tracker:reset()
				self._last_combo = {}
				self._start_time = 0
				self._mouse_capture_armed = false
				self._mouse_capture_pending_key = nil
				if self._on_timeout then
					self._on_timeout()
				end
				return true
			end
		end
		return false
	end

	--- Обработать оконное сообщение. Возвращает true если consumed.
	-- consumeFn — необязательная функция consumeWindowMessage(game, scripts).
	function capture:onWindowMessage(msg, wparam, consumeFn)
		if not self._active then
			return false
		end
		if self:checkTimeout() then
			return true
		end

		local keyInfo = HotkeyManager.getMessageKeyInfo(msg, wparam)
		if not keyInfo then
			return false
		end
		local kc = keyInfo.keyCode
		local mouseKey = isMouseKey(kc)

		if mouseKey and not self._mouse_capture_armed then
			return false
		end

		if keyInfo.isDown then
			if kc == vk.VK_ESCAPE then
				self:stop()
				if consumeFn then
					consumeFn(true, true)
				end
				return true
			end
			if self._enter_saves and (kc == vk.VK_RETURN or kc == vk.VK_NUMPADENTER) then
				self:save()
				if consumeFn then
					consumeFn(true, true)
				end
				return true
			end
			if kc == vk.VK_BACK then
				self:clear()
				if consumeFn then
					consumeFn(true, true)
				end
				return true
			end
			self._tracker:keyDown(kc)
			if self._tracker:count() > 0 then
				self._last_combo = cloneKeys(self._tracker:getOrdered())
			end
			if mouseKey then
				self._mouse_capture_pending_key = kc
			end
		elseif keyInfo.isUp then
			self._tracker:keyUp(kc)
			if mouseKey and self._mouse_capture_pending_key == kc then
				self._mouse_capture_armed = false
				self._mouse_capture_pending_key = nil
			end
		end

		if consumeFn then
			consumeFn(true, true)
		end
		return true
	end

	return capture
end

---------------------------------------------------------------------------
-- UI
---------------------------------------------------------------------------

function HotkeyManager.drawCaptureUI(capture, label, imgui_text_colored_fn)
	if not capture:isActive() then
		return
	end
	local text_colored = imgui_text_colored_fn or function(_, text)
		imgui.Text(text)
	end
	local mouseCaptureArmed = capture:isMouseCaptureArmed()

	imgui.Separator()
	local held = capture._tracker:count()
	if mouseCaptureArmed then
		imgui.TextWrapped(tr("hotkey_manager.capture.mouse_mode"))
	elseif held > 0 then
		imgui.TextWrapped(tr("hotkey_manager.capture.hold_mode"))
	else
		imgui.TextWrapped(tr("hotkey_manager.capture.idle_mode"))
	end
	imgui.Text(tr("hotkey_manager.capture.current_combo"))
	imgui.SameLine()
	text_colored(imgui.ImVec4(0.6, 1.0, 0.6, 1), capture:getDraftString())
	if not mouseCaptureArmed then
		if imgui.SmallButton(tr("hotkey_manager.capture.save") .. '##hkm_save_' .. label) then
			capture:save()
		end
		imgui.SameLine()
		if imgui.SmallButton(tr("hotkey_manager.capture.clear") .. '##hkm_clear_' .. label) then
			capture:clear()
		end
		imgui.SameLine()
		if imgui.SmallButton(tr("hotkey_manager.capture.mouse") .. '##hkm_mouse_' .. label) then
			capture:armMouseCapture()
		end
		imgui.SameLine()
		if imgui.SmallButton(tr("hotkey_manager.capture.cancel") .. '##hkm_cancel_' .. label) then
			capture:stop()
		end
	end
end

function HotkeyManager.drawCapturePopup(capture, popup_name, fa, imgui_text_safe_fn)
	local text_safe = imgui_text_safe_fn or function(text)
		imgui.Text(text)
	end
	fa = fa or {}
	if imgui.BeginPopupModal(
		popup_name,
		nil,
		imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoMove
	) then
		if not capture:isActive() then
			imgui.CloseCurrentPopup()
			imgui.EndPopup()
			return true
		end
		local mouseCaptureArmed = capture:isMouseCaptureArmed()
		local kb_icon = fa.KEYBOARD or ''
		text_safe(kb_icon .. '\t' .. tr("hotkey_manager.popup.press_hotkey"))
		text_safe(capture:getDraftString())
		if mouseCaptureArmed then
			text_safe(tr("hotkey_manager.popup.mouse_mode"))
			text_safe(tr("hotkey_manager.popup.controls"))
		else
			local xmark = fa.XMARK or 'X'
			local floppy = fa.FLOPPY_DISK or 'S'
			local mouseLabel = (fa.COMPUTER_MOUSE or fa.MOUSE_POINTER or 'M') .. ' [' .. tr("hotkey_manager.popup.mouse_button") .. ']'
			if imgui.Button(xmark .. ' [' .. tr("hotkey_manager.popup.cancel_button") .. ']') then
				capture:stop()
				imgui.CloseCurrentPopup()
			end
			imgui.SameLine()
			if imgui.Button(floppy .. ' [' .. tr("hotkey_manager.popup.save_button") .. ']') then
				capture:save()
				imgui.CloseCurrentPopup()
			end
			imgui.SameLine()
			if imgui.Button(mouseLabel) then
				capture:armMouseCapture()
			end
		end
		imgui.EndPopup()
		return true
	end
	return false
end

return HotkeyManager
