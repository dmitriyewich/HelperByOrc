local HotkeyManager = {}

local imgui = require("mimgui")
local vk = require("vkeys")
local wm = require("windows.message")
local funcs = require("HelperByOrc.funcs")

local helpers = funcs.getHotkeyHelpers(vk, "[KEY]")
local normalizeKey = helpers.normalizeKey
local isKeyboardKey = helpers.isKeyboardKey
local hotkeyToString = helpers.hotkeyToString

HotkeyManager.normalizeKey = normalizeKey
HotkeyManager.isKeyboardKey = isKeyboardKey
HotkeyManager.hotkeyToString = hotkeyToString

local WM_SYSKEYDOWN = wm.WM_SYSKEYDOWN or 0x0104
local WM_SYSKEYUP = wm.WM_SYSKEYUP or 0x0105

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
		if not isKeyboardKey(nk) then
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
		local isDown = msg == wm.WM_KEYDOWN or msg == WM_SYSKEYDOWN
		local isUp = msg == wm.WM_KEYUP or msg == WM_SYSKEYUP
		if not (isDown or isUp) then
			return false
		end
		local nk = normalizeKey(wparam)
		if not isKeyboardKey(nk) then
			return false
		end
		if isDown then
			self:keyDown(nk)
		else
			self:keyUp(nk)
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
	}

	function capture:start(initial_keys)
		self._active = true
		self._tracker:reset()
		self._last_combo = cloneKeys(initial_keys)
		self._start_time = os.clock()
	end

	function capture:stop()
		local was = self._active
		self._active = false
		self._tracker:reset()
		self._last_combo = {}
		self._start_time = 0
		if was and self._on_cancel then
			self._on_cancel()
		end
	end

	function capture:isActive()
		return self._active
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
		if self._on_save and #keys > 0 then
			self._on_save(keys)
		end
	end

	function capture:clear()
		self._tracker:reset()
		self._last_combo = {}
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

		local isDown = msg == wm.WM_KEYDOWN or msg == WM_SYSKEYDOWN
		local isUp = msg == wm.WM_KEYUP or msg == WM_SYSKEYUP
		if not (isDown or isUp) then
			return false
		end

		local kc = normalizeKey(wparam)
		if not isKeyboardKey(kc) then
			return false
		end

		if isDown then
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
		elseif isUp then
			self._tracker:keyUp(kc)
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

--- Inline-виджет: подсказка + текущая комбинация + кнопки.
function HotkeyManager.drawCaptureUI(capture, label, imgui_text_colored_fn)
	if not capture:isActive() then
		return
	end
	local text_colored = imgui_text_colored_fn or function(_, text)
		imgui.Text(text)
	end

	imgui.Separator()
	local held = capture._tracker:count()
	if held > 0 then
		imgui.TextWrapped("Зажмите нужные клавиши. Отпустите — комбинация запомнится. Enter / кнопка — сохранить.")
	else
		imgui.TextWrapped("Зажмите нужные клавиши. Backspace — очистить, Esc — отмена.")
	end
	imgui.Text("Текущая комбинация:")
	imgui.SameLine()
	text_colored(imgui.ImVec4(0.6, 1.0, 0.6, 1), capture:getDraftString())
	if imgui.SmallButton("Сохранить##hkm_save_" .. label) then
		capture:save()
	end
	imgui.SameLine()
	if imgui.SmallButton("Очистить##hkm_clear_" .. label) then
		capture:clear()
	end
	imgui.SameLine()
	if imgui.SmallButton("Отмена##hkm_cancel_" .. label) then
		capture:stop()
	end
end

--- Модальный попап захвата (для binder).
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
		local kb_icon = fa.KEYBOARD or ""
		text_safe(kb_icon .. "\t" .. "Зажмите нужные клавиши")
		text_safe(capture:getDraftString())
		local xmark = fa.XMARK or "X"
		local floppy = fa.FLOPPY_DISK or "S"
		if imgui.Button(xmark .. " [CANCEL]") then
			capture:stop()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button(floppy .. " [SAVE]") then
			capture:save()
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
		return true
	end
	return false
end

return HotkeyManager
