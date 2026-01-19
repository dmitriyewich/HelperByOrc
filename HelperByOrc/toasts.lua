local imgui = require("mimgui")
local ffi = require("ffi")

local module = {}

local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local history = {}
local CONFIG_PATH = getWorkingDirectory() .. "\\HelperByOrc\\toasts.json"
local funcs
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
	out.enabled = type(raw.enabled) == "boolean" and raw.enabled or cfgDefaults.enabled
	return out
end

local function loadConfig()
	if funcs and funcs.loadTableFromJson then
		local loaded = funcs.loadTableFromJson(CONFIG_PATH, cfgDefaults)
		return normalizeConfig(type(loaded) == "table" and loaded or cfgDefaults)
	end
	return normalizeConfig(cfgDefaults)
end

local cfg = normalizeConfig(cfgDefaults)
local cfgDirty = false
local cfgDirtyAt = 0
local COL_OK = imgui.ImVec4(0.4, 0.9, 0.4, 1.0)
local COL_WARN = imgui.ImVec4(0.95, 0.75, 0.2, 1.0)
local COL_ERR = imgui.ImVec4(0.9, 0.3, 0.3, 1.0)

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
	if imgui.GetTime then
		return imgui.GetTime()
	end
	return os.clock()
end

local function markConfigDirty()
	cfgDirty = true
	cfgDirtyAt = nowSec()
end

local function flushConfigDirty()
	if cfgDirty and (nowSec() - cfgDirtyAt) >= 0.5 then
		if funcs and funcs.saveTableToJson then
			funcs.saveTableToJson(cfg, CONFIG_PATH)
		end
		cfgDirty = false
	end
end

local function setCfg(key, value)
	if cfg[key] ~= value then
		cfg[key] = value
		markConfigDirty()
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
	if not imgui.CollapsingHeader("Уведомления") then
		return
	end

	local enabled = imgui.new.bool(cfg.enabled)
	if imgui.Checkbox("Включить", enabled) then
		setCfg("enabled", enabled[0])
	end

	imgui.Separator()
	imgui.Text("Якорь")
	local anchorOptions = {
		{ label = "Слева сверху", value = "top_left" },
		{ label = "По центру сверху", value = "top_center" },
		{ label = "Справа сверху", value = "top_right" },
		{ label = "Слева снизу", value = "bottom_left" },
		{ label = "По центру снизу", value = "bottom_center" },
		{ label = "Справа снизу", value = "bottom_right" },
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
	imgui.Text("Позиционирование и размеры")
	local width = ffi.new("int[1]", cfg.width)
	if imgui.InputInt("Ширина", width) then
		setCfg("width", math.max(1, width[0]))
	end
	local offsetX = ffi.new("int[1]", cfg.offsetX)
	if imgui.InputInt("Отступ X", offsetX) then
		setCfg("offsetX", math.max(0, offsetX[0]))
	end
	local offsetY = ffi.new("int[1]", cfg.offsetY)
	if imgui.InputInt("Отступ Y", offsetY) then
		setCfg("offsetY", math.max(0, offsetY[0]))
	end

	imgui.Separator()
	imgui.Text("Длительность")
	local durOk = ffi.new("float[1]", cfg.durOk)
	if imgui.InputFloat("OK (сек)", durOk) then
		setCfg("durOk", math.max(0, durOk[0]))
	end
	local durWarn = ffi.new("float[1]", cfg.durWarn)
	if imgui.InputFloat("WARN (сек)", durWarn) then
		setCfg("durWarn", math.max(0, durWarn[0]))
	end
	local durErr = ffi.new("float[1]", cfg.durErr)
	if imgui.InputFloat("ERR (сек)", durErr) then
		setCfg("durErr", math.max(0, durErr[0]))
	end

	imgui.Separator()
	imgui.Text("Плавность")
	local fadeIn = ffi.new("float[1]", cfg.fadeIn)
	if imgui.InputFloat("Fade In", fadeIn) then
		setCfg("fadeIn", math.max(0, fadeIn[0]))
	end
	local fadeOut = ffi.new("float[1]", cfg.fadeOut)
	if imgui.InputFloat("Fade Out", fadeOut) then
		setCfg("fadeOut", math.max(0, fadeOut[0]))
	end

	imgui.Separator()
	imgui.Text("Лимиты")
	local maxVisible = ffi.new("int[1]", cfg.maxVisible)
	if imgui.InputInt("Max Visible", maxVisible) then
		setCfg("maxVisible", math.max(1, maxVisible[0]))
	end
	local maxQueue = ffi.new("int[1]", cfg.maxQueue)
	if imgui.InputInt("Max Queue", maxQueue) then
		setCfg("maxQueue", math.max(1, maxQueue[0]))
	end
	local historyLimit = ffi.new("int[1]", cfg.historyLimit)
	if imgui.InputInt("History Limit", historyLimit) then
		setCfg("historyLimit", math.max(0, historyLimit[0]))
	end

	imgui.Separator()
	imgui.Text("Внешний вид")
	local bgAlpha = ffi.new("float[1]", cfg.bgAlpha)
	if imgui.InputFloat("Прозрачность фона", bgAlpha) then
		setCfg("bgAlpha", math.max(0, math.min(1, bgAlpha[0])))
	end
	local rounding = ffi.new("float[1]", cfg.rounding)
	if imgui.InputFloat("Скругление", rounding) then
		setCfg("rounding", math.max(0, rounding[0]))
	end
	local padX = ffi.new("float[1]", cfg.padX)
	if imgui.InputFloat("Padding X", padX) then
		setCfg("padX", math.max(0, padX[0]))
	end
	local padY = ffi.new("float[1]", cfg.padY)
	if imgui.InputFloat("Padding Y", padY) then
		setCfg("padY", math.max(0, padY[0]))
	end

	imgui.Separator()
	if imgui.TreeNodeStr("История") then
		if imgui.Button("Очистить") then
			module.clearHistory()
		end
		local listHeight = 140
		if imgui.BeginChild("ToastHistoryList", imgui.ImVec2(0, listHeight), true) then
			if #history == 0 then
				imgui.Text("Записей нет")
			else
				local limit = cfg.historyLimit > 0 and cfg.historyLimit or 200
				local first = math.max(1, #history - limit + 1)
				for i = first, #history do
					local entry = history[i]
					imgui.PushStyleColor(imgui.Col.Text, toastColor(entry.kind))
					local suffix = entry.count and entry.count > 1 and (" x" .. tostring(entry.count)) or ""
					imgui.TextWrapped(entry.text .. suffix)
					imgui.PopStyleColor()
				end
			end
		end
		imgui.EndChild()
		imgui.TreePop()
	end
	flushConfigDirty()
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
	flushConfigDirty()
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
		"Notifications##toasts",
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
		local suffix = toast.count and toast.count > 1 and (" x" .. tostring(toast.count)) or ""
		imgui.TextWrapped(toast.text .. suffix)
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
	funcs = mod.funcs
	cfg = loadConfig()
end

if imgui and imgui.OnFrame then
	local frame = imgui.OnFrame(function()
		return cfg.enabled and #toasts > 0
	end, function()
		module.draw()
	end)
	if frame then
		frame.HideCursor = true
	end
end

return module
