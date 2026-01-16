local imgui = require("mimgui")

local module = {}

local toasts = {} -- { {text, kind='ok'|'warn'|'err', t, dur} }

function module.push(text, kind, dur)
	toasts[#toasts + 1] = {
		text = tostring(text or ""),
		kind = kind or "ok",
		t = os.clock(),
		dur = dur or 3.0,
	}
end

local function pruneToasts(now)
	for i = #toasts, 1, -1 do
		local toast = toasts[i]
		if now - toast.t > toast.dur then
			table.remove(toasts, i)
		end
	end
end

local function getViewport()
	if imgui.GetMainViewport then
		local vp = imgui.GetMainViewport()
		return vp.Pos.x, vp.Pos.y, vp.Size.x, vp.Size.y
	end
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

	local now = os.clock()
	pruneToasts(now)
	if #toasts == 0 then
		return
	end

	local vpPosX, vpPosY, vpW = getViewport()
	local pad = 8
	imgui.SetNextWindowPos(imgui.ImVec2(vpPosX + vpW - 350 - pad, vpPosY + pad), imgui.Cond.Always)
	imgui.SetNextWindowSize(imgui.ImVec2(350, 0), imgui.Cond.Always)

	imgui.Begin(
		"Notifications##toasts",
		nil,
		imgui.WindowFlags.NoCollapse
			+ imgui.WindowFlags.NoResize
			+ imgui.WindowFlags.NoMove
			+ imgui.WindowFlags.AlwaysAutoResize
			+ imgui.WindowFlags.NoNav
			+ imgui.WindowFlags.NoFocusOnAppearing
	)
	for i, toast in ipairs(toasts) do
		imgui.PushStyleColor(imgui.Col.Text, toastColor(toast.kind))
		imgui.TextWrapped(toast.text)
		imgui.PopStyleColor()
		if i < #toasts then
			imgui.Separator()
		end
	end
	imgui.End()
end

return module
