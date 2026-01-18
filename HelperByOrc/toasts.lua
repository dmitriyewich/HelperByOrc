local imgui = require("mimgui")
local funcs = require("HelperByOrc.funcs")

local module = {}

local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local CONFIG_PATH = getWorkingDirectory() .. "\\HelperByOrc\\toasts.json"
local cfgDefaults = {
	anchor = "top_center",
	maxQueue = 8,
	maxVisible = 8,
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
	out.anchor = (raw.anchor == "top_center" or raw.anchor == "top_right") and raw.anchor or cfgDefaults.anchor
	out.maxQueue = math.max(1, tonumber(raw.maxQueue) or cfgDefaults.maxQueue)
	out.maxVisible = math.max(1, tonumber(raw.maxVisible) or cfgDefaults.maxVisible)
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

local cfg = loadConfig()
local COL_OK = imgui.ImVec4(0.4, 0.9, 0.4, 1.0)
local COL_WARN = imgui.ImVec4(0.95, 0.75, 0.2, 1.0)
local COL_ERR = imgui.ImVec4(0.9, 0.3, 0.3, 1.0)

local function nowSec()
	if imgui.GetTime then
		return imgui.GetTime()
	end
	return os.clock()
end

function module.setAnchor(a)
	if a == "top_center" or a == "top_right" then
		cfg.anchor = a
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
		for i = 2, #toasts do
			toasts[i - 1] = toasts[i]
		end
		toasts[#toasts] = nil
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

local function toastColor(kind)
	if kind == "err" then
		return COL_ERR
	end
	if kind == "warn" then
		return COL_WARN
	end
	return COL_OK
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
	windowW = math.max(1, math.min(windowW, vpW - cfg.offsetX * 2))
	local windowX
	if cfg.anchor == "top_right" then
		windowX = vpX + vpW - windowW - cfg.offsetX
	else
		windowX = vpX + (vpW - windowW) * 0.5
	end
	local windowY = vpY + cfg.offsetY
	local fadeOut = cfg.fadeOut
	local fadeIn = cfg.fadeIn
	local newestToast = toasts[#toasts]
	local alpha = 1.0
	if newestToast and newestToast.dur and newestToast.t then
		local age = now - newestToast.t
		local remain = newestToast.dur - age
		if fadeOut > 0 and remain < fadeOut then
			alpha = math.max(0.0, remain / fadeOut)
		end
		if fadeIn > 0 and age < fadeIn then
			alpha = math.min(alpha, age / fadeIn)
		end
	end
	imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, cfg.rounding)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(cfg.padX, cfg.padY))
	imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0.08, 0.08, 0.08, cfg.bgAlpha))
	imgui.SetNextWindowPos(imgui.ImVec2(windowX, windowY), imgui.Cond.Always)
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
		imgui.PushStyleColor(imgui.Col.Text, toastColor(toast.kind))
		local suffix = toast.count and toast.count > 1 and (" x" .. tostring(toast.count)) or ""
		imgui.TextWrapped(toast.text .. suffix)
		imgui.PopStyleColor()
		if i < #toasts then
			imgui.Spacing()
		end
	end
	imgui.End()
	imgui.PopStyleColor()
	imgui.PopStyleVar(3)
end

if imgui and imgui.OnFrame then
	local frame = imgui.OnFrame(function()
		return #toasts > 0
	end, function()
		module.draw()
	end)
	if frame then
		frame.HideCursor = true
	end
end

return module
