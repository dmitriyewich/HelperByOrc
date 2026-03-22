local imgui = require("mimgui")
local mimgui_funcs
local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local module = {}

local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local history = {}
local funcs
local imgui_text_wrapped_safe
local config_manager
local event_bus

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	imgui_text_wrapped_safe = mimgui_funcs.imgui_text_wrapped_safe
end

syncDependencies()

local cfgDefaults = {
	anchor = "top_center",
	maxQueue = 8,
	maxVisible = 8,
	historyLimit = 200,
	width = 420,
	offsetX = 12,
	offsetY = 12,
	durOk = 3.0,
	durWarn = 3.5,
	durErr = 4.0,
	fadeIn = 0.12,
	fadeOut = 0.35,
	bgAlpha = 0.9,
	rounding = 10,
	padX = 12,
	padY = 10,
	enabled = true,
}

local function normalizeConfig(raw)
	local out = {}
	local anchor = raw.anchor
	if
		anchor == "top_left"
		or anchor == "top_center"
		or anchor == "top_right"
		or anchor == "bottom_left"
		or anchor == "bottom_center"
		or anchor == "bottom_right"
	then
		out.anchor = anchor
	else
		out.anchor = cfgDefaults.anchor
	end
	out.maxQueue = math.max(1, tonumber(raw.maxQueue) or cfgDefaults.maxQueue)
	out.maxVisible = math.max(1, tonumber(raw.maxVisible) or cfgDefaults.maxVisible)
	out.historyLimit = math.max(0, tonumber(raw.historyLimit) or cfgDefaults.historyLimit)
	out.width = math.max(1, tonumber(raw.width) or cfgDefaults.width)
	out.offsetX = tonumber(raw.offsetX) or cfgDefaults.offsetX
	out.offsetY = tonumber(raw.offsetY) or cfgDefaults.offsetY
	out.durOk = tonumber(raw.durOk) or cfgDefaults.durOk
	out.durWarn = tonumber(raw.durWarn) or cfgDefaults.durWarn
	out.durErr = tonumber(raw.durErr) or cfgDefaults.durErr
	out.fadeIn = math.max(0, tonumber(raw.fadeIn) or cfgDefaults.fadeIn)
	out.fadeOut = math.max(0, tonumber(raw.fadeOut) or cfgDefaults.fadeOut)
	out.bgAlpha = tonumber(raw.bgAlpha) or cfgDefaults.bgAlpha
	out.rounding = math.max(0, tonumber(raw.rounding) or cfgDefaults.rounding)
	out.padX = tonumber(raw.padX) or cfgDefaults.padX
	out.padY = tonumber(raw.padY) or cfgDefaults.padY
	if type(raw.enabled) == "boolean" then out.enabled = raw.enabled else out.enabled = cfgDefaults.enabled end
	return out
end

local cfg = normalizeConfig(cfgDefaults)
local COL_OK = imgui.ImVec4(0.4, 0.9, 0.4, 1.0)
local COL_WARN = imgui.ImVec4(0.95, 0.75, 0.2, 1.0)
local COL_ERR = imgui.ImVec4(0.9, 0.3, 0.3, 1.0)
local settings_ui_cache = { bools = {}, ints = {}, floats = {} }

local function ui_bool(id, value)
	local b = settings_ui_cache.bools[id]
	if not b then
		b = imgui.new.bool(false)
		settings_ui_cache.bools[id] = b
	end
	b[0] = value and true or false
	return b
end

local function ui_int(id, value)
	local b = settings_ui_cache.ints[id]
	if not b then
		b = imgui.new.int(0)
		settings_ui_cache.ints[id] = b
	end
	b[0] = math.floor(tonumber(value) or 0)
	return b
end

local function ui_float(id, value)
	local b = settings_ui_cache.floats[id]
	if not b then
		b = imgui.new.float(0)
		settings_ui_cache.floats[id] = b
	end
	b[0] = tonumber(value) or 0
	return b
end

local function toastColor(kind)
	if kind == "err" then
		return COL_ERR
	end
	if kind == "warn" then
		return COL_WARN
	end
	return COL_OK
end

local function nowSec()
	-- Deliberately avoid imgui.GetTime() here:
	-- this function is called from worker coroutines during startup.
	return os.clock()
end

local function setCfg(key, value)
	if cfg[key] ~= value then
		cfg[key] = value
		if config_manager then
			config_manager.markDirty("toasts")
		end
	end
end

local function addHistory(toast, reason)
	history[#history + 1] = {
		text = toast.text,
		kind = toast.kind,
		t = toast.t,
		dur = toast.dur,
		count = toast.count or 1,
		reason = reason,
	}
	local limit = cfg.historyLimit
	if limit > 0 and #history > limit then
		local over = #history - limit
		for i = 1 + over, #history do
			history[i - over] = history[i]
		end
		for i = #history - over + 1, #history do
			history[i] = nil
		end
	end
end

function module.setAnchor(a)
	if
		a == "top_left"
		or a == "top_center"
		or a == "top_right"
		or a == "bottom_left"
		or a == "bottom_center"
		or a == "bottom_right"
	then
		setCfg("anchor", a)
	end
end

function module.getHistory()
	return history
end

function module.clearHistory()
	for i = #history, 1, -1 do
		history[i] = nil
	end
end

function module.DrawSettingsInline()
	if not (imgui and imgui.CollapsingHeader) then
		return
	end
	if not imgui.CollapsingHeader(L("toasts.text.text")) then
		return
	end

	local enabled = ui_bool("enabled", cfg.enabled)
	if imgui.Checkbox(L("toasts.text.text_1"), enabled) then
		setCfg("enabled", enabled[0])
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_2"))
	local anchorOptions = {
		{ label = L("toasts.text.text_3"), value = "top_left" },
		{ label = L("toasts.text.text_4"), value = "top_center" },
		{ label = L("toasts.text.text_5"), value = "top_right" },
		{ label = L("toasts.text.text_6"), value = "bottom_left" },
		{ label = L("toasts.text.text_7"), value = "bottom_center" },
		{ label = L("toasts.text.text_8"), value = "bottom_right" },
	}
	for i, opt in ipairs(anchorOptions) do
		if imgui.RadioButtonBool(opt.label, cfg.anchor == opt.value) then
			setCfg("anchor", opt.value)
		end
		if i % 2 == 1 and i < #anchorOptions then
			imgui.SameLine()
		end
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_9"))
	local width = ui_int("width", cfg.width)
	if imgui.InputInt(L("toasts.text.text_10"), width) then
		setCfg("width", math.max(1, width[0]))
	end
	local offsetX = ui_int("offset_x", cfg.offsetX)
	if imgui.InputInt(L("toasts.text.x"), offsetX) then
		setCfg("offsetX", math.max(0, offsetX[0]))
	end
	local offsetY = ui_int("offset_y", cfg.offsetY)
	if imgui.InputInt(L("toasts.text.y"), offsetY) then
		setCfg("offsetY", math.max(0, offsetY[0]))
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_11"))
	local durOk = ui_float("dur_ok", cfg.durOk)
	if imgui.InputFloat(L("toasts.text.ok"), durOk) then
		setCfg("durOk", math.max(0, durOk[0]))
	end
	local durWarn = ui_float("dur_warn", cfg.durWarn)
	if imgui.InputFloat(L("toasts.text.warn"), durWarn) then
		setCfg("durWarn", math.max(0, durWarn[0]))
	end
	local durErr = ui_float("dur_err", cfg.durErr)
	if imgui.InputFloat(L("toasts.text.err"), durErr) then
		setCfg("durErr", math.max(0, durErr[0]))
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_12"))
	local fadeIn = ui_float("fade_in", cfg.fadeIn)
	if imgui.InputFloat(L("toasts.text.fade_in"), fadeIn) then
		setCfg("fadeIn", math.max(0, fadeIn[0]))
	end
	local fadeOut = ui_float("fade_out", cfg.fadeOut)
	if imgui.InputFloat(L("toasts.text.fade_out"), fadeOut) then
		setCfg("fadeOut", math.max(0, fadeOut[0]))
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_13"))
	local maxVisible = ui_int("max_visible", cfg.maxVisible)
	if imgui.InputInt(L("toasts.text.max_visible"), maxVisible) then
		setCfg("maxVisible", math.max(1, maxVisible[0]))
	end
	local maxQueue = ui_int("max_queue", cfg.maxQueue)
	if imgui.InputInt(L("toasts.text.max_queue"), maxQueue) then
		setCfg("maxQueue", math.max(1, maxQueue[0]))
	end
	local historyLimit = ui_int("history_limit", cfg.historyLimit)
	if imgui.InputInt(L("toasts.text.history_limit"), historyLimit) then
		setCfg("historyLimit", math.max(0, historyLimit[0]))
	end

	imgui.Separator()
	imgui.Text(L("toasts.text.text_14"))
	local bgAlpha = ui_float("bg_alpha", cfg.bgAlpha)
	if imgui.InputFloat(L("toasts.text.text_15"), bgAlpha) then
		setCfg("bgAlpha", math.max(0, math.min(1, bgAlpha[0])))
	end
	local rounding = ui_float("rounding", cfg.rounding)
	if imgui.InputFloat(L("toasts.text.text_16"), rounding) then
		setCfg("rounding", math.max(0, rounding[0]))
	end
	local padX = ui_float("pad_x", cfg.padX)
	if imgui.InputFloat(L("toasts.text.padding_x"), padX) then
		setCfg("padX", math.max(0, padX[0]))
	end
	local padY = ui_float("pad_y", cfg.padY)
	if imgui.InputFloat(L("toasts.text.padding_y"), padY) then
		setCfg("padY", math.max(0, padY[0]))
	end

	imgui.Separator()
	if imgui.TreeNodeStr(L("toasts.text.text_17")) then
		if imgui.Button(L("toasts.text.text_18")) then
			module.clearHistory()
		end
		local listHeight = 140
		if imgui.BeginChild("ToastHistoryList", imgui.ImVec2(0, listHeight), true) then
			if #history == 0 then
				imgui.Text(L("toasts.text.text_19"))
			else
				local limit = cfg.historyLimit > 0 and cfg.historyLimit or 200
				local first = math.max(1, #history - limit + 1)
				for i = first, #history do
					local entry = history[i]
					imgui.PushStyleColor(imgui.Col.Text, toastColor(entry.kind))
					local suffix = entry.count and entry.count > 1 and L("toasts.text.count_suffix", { count = entry.count }) or ""
					imgui_text_wrapped_safe(entry.text .. suffix)
					imgui.PopStyleColor()
				end
			end
		end
		imgui.EndChild()
		imgui.TreePop()
	end
end

function module.push(text, kind, dur)
	if not cfg.enabled then
		return
	end
	text = tostring(text or "")
	kind = kind or "ok"
	local now = nowSec()
	local defaultDur = cfg.durOk
	if kind == "err" then
		defaultDur = cfg.durErr
	elseif kind == "warn" then
		defaultDur = cfg.durWarn
	end
	local last = toasts[#toasts]
	if last and last.text == text and last.kind == kind then
		last.count = (last.count or 1) + 1
		last.t = now
		last.dur = dur or last.dur or defaultDur
		return
	end
	if #toasts >= cfg.maxQueue then
		local removed = toasts[1]
		if removed then
			addHistory(removed, "queue")
			table.remove(toasts, 1)
		end
	end
	toasts[#toasts + 1] = {
		text = text,
		kind = kind,
		t = now,
		dur = dur or defaultDur,
		count = 1,
	}
end

local function pruneToasts(now)
	local write = 1
	for read = 1, #toasts do
		local toast = toasts[read]
		if now - toast.t <= toast.dur then
			if write ~= read then
				toasts[write] = toast
			end
			write = write + 1
		else
			addHistory(toast, "expired")
		end
	end
	for i = write, #toasts do
		toasts[i] = nil
	end
end

local function getViewport()
	local io = imgui.GetIO()
	return 0, 0, io.DisplaySize.x, io.DisplaySize.y
end

function module.draw()
	if not cfg.enabled then
		return
	end
	if #toasts == 0 then
		return
	end

	local now = nowSec()
	pruneToasts(now)
	if #toasts == 0 then
		return
	end

	local vpX, vpY, vpW, vpH = getViewport()
	local windowW = cfg.width
	windowW = math.max(200, math.min(windowW, vpW - cfg.offsetX * 2))
	local posX = vpX + cfg.offsetX
	local posY = vpY + cfg.offsetY
	local pivotX = 0
	local pivotY = 0
	if cfg.anchor == "top_center" then
		posX = vpX + vpW * 0.5
		pivotX = 0.5
	elseif cfg.anchor == "top_right" then
		posX = vpX + vpW - cfg.offsetX
		pivotX = 1
	elseif cfg.anchor == "bottom_left" then
		posY = vpY + vpH - cfg.offsetY
		pivotY = 1
	elseif cfg.anchor == "bottom_center" then
		posX = vpX + vpW * 0.5
		posY = vpY + vpH - cfg.offsetY
		pivotX = 0.5
		pivotY = 1
	elseif cfg.anchor == "bottom_right" then
		posX = vpX + vpW - cfg.offsetX
		posY = vpY + vpH - cfg.offsetY
		pivotX = 1
		pivotY = 1
	end
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, cfg.rounding)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(cfg.padX, cfg.padY))
	imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0.08, 0.08, 0.08, cfg.bgAlpha))
	imgui.SetNextWindowPos(imgui.ImVec2(posX, posY), imgui.Cond.Always, imgui.ImVec2(pivotX, pivotY))
	imgui.SetNextWindowSize(imgui.ImVec2(windowW, 0), imgui.Cond.Always)

	imgui.Begin(
		L("toasts.text.text") .. "##toasts",
		nil,
		imgui.WindowFlags.NoDecoration
			+ imgui.WindowFlags.NoMove
			+ imgui.WindowFlags.AlwaysAutoResize
			+ imgui.WindowFlags.NoNav
			+ imgui.WindowFlags.NoFocusOnAppearing
			+ imgui.WindowFlags.NoInputs
	)
	local startIndex = math.max(1, #toasts - cfg.maxVisible + 1)
	for i = startIndex, #toasts do
		local toast = toasts[i]
		local life = now - toast.t
		local alpha = 1.0
		if cfg.fadeIn > 0 and life < cfg.fadeIn then
			alpha = life / cfg.fadeIn
		else
			local remain = toast.dur - life
			if cfg.fadeOut > 0 and remain < cfg.fadeOut then
				alpha = remain / cfg.fadeOut
			end
		end
		alpha = math.max(0.0, math.min(1.0, alpha))
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
		imgui.PushStyleColor(imgui.Col.Text, toastColor(toast.kind))
		local suffix = toast.count and toast.count > 1 and L("toasts.text.count_suffix", { count = toast.count }) or ""
		imgui_text_wrapped_safe(toast.text .. suffix)
		imgui.PopStyleColor()
		imgui.PopStyleVar(1)
		if i < #toasts then
			imgui.Spacing()
		end
	end
	imgui.End()
	imgui.PopStyleColor()
	imgui.PopStyleVar(2)
end

function module.attachModules(mod)
	syncDependencies(mod)
	config_manager = mod.config_manager
	event_bus = mod.event_bus
	if event_bus then
		event_bus.offByOwner("toasts")
		event_bus.on("toast", function(text, kind, dur)
			module.push(text, kind, dur)
		end, "toasts")
	end
	if config_manager then
		cfg = config_manager.register("toasts", {
			path = "toasts.json",
			defaults = cfgDefaults,
			normalize = normalizeConfig,
		})
	else
		local loaded = funcs.loadTableFromJson("toasts.json", cfgDefaults)
		cfg = normalizeConfig(type(loaded) == "table" and loaded or cfgDefaults)
	end
end

local _toastFrame

function module.onTerminate()
	if _toastFrame and type(_toastFrame.Unsubscribe) == "function" then
		pcall(_toastFrame.Unsubscribe, _toastFrame)
		_toastFrame = nil
	end
	if event_bus then
		event_bus.offByOwner("toasts")
	end
end

if imgui and imgui.OnFrame then
	_toastFrame = imgui.OnFrame(function()
		return cfg.enabled and #toasts > 0
	end, function()
		module.draw()
	end)
	if _toastFrame then
		_toastFrame.HideCursor = true
	end
end

return module
