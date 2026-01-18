local imgui = require("mimgui")

local module = {}

local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }
local MAX_TOASTS = 8
local anchor = "top_center"

local function nowSec()
	if imgui.GetTime then
		return imgui.GetTime()
	end
	return os.clock()
end

function module.setAnchor(a)
	if a == "top_center" or a == "top_right" then
		anchor = a
	end
end

function module.push(text, kind, dur)
	text = tostring(text or "")
	kind = kind or "ok"
	local now = nowSec()
	local last = toasts[#toasts]
	if last and last.text == text and last.kind == kind then
		last.count = (last.count or 1) + 1
		last.t = now
		last.dur = dur or last.dur or 3.0
		return
	end
	if #toasts >= MAX_TOASTS then
		for i = 2, #toasts do
			toasts[i - 1] = toasts[i]
		end
		toasts[#toasts] = nil
	end
	toasts[#toasts + 1] = {
		text = text,
		kind = kind,
		t = now,
		dur = dur or 3.0,
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
		return imgui.ImVec4(0.9, 0.3, 0.3, 1.0)
	end
	if kind == "warn" then
		return imgui.ImVec4(0.95, 0.75, 0.2, 1.0)
	end
	return imgui.ImVec4(0.4, 0.9, 0.4, 1.0)
end

function module.draw()
	if #toasts == 0 then
		return
	end

	local now = nowSec()
	pruneToasts(now)
	if #toasts == 0 then
		return
	end

	local vpPosX, vpPosY, vpW = getViewport()
	local pad = 12
	local windowW = 420
	windowW = math.max(1, math.min(windowW, vpW - pad * 2))
	local windowX
	if anchor == "top_right" then
		windowX = vpPosX + vpW - windowW - pad
	else
		windowX = vpPosX + (vpW - windowW) * 0.5
	end
	local windowY = vpPosY + pad
	local fadeDuration = 0.35
	local newestToast = toasts[#toasts]
	local alpha = 1.0
	if newestToast and newestToast.dur and newestToast.t then
		local remain = newestToast.dur - (now - newestToast.t)
		if remain < fadeDuration then
			alpha = math.max(0.0, remain / fadeDuration)
		end
	end
	imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, 10)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(12, 10))
	imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0.08, 0.08, 0.08, 0.9))
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
	for i, toast in ipairs(toasts) do
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
