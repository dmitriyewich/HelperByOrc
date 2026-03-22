local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}

local imgui = require("mimgui")
local ffi = require("ffi")

local ctx

local quickMenuPos = imgui.ImVec2(0, 0)
local quickMenuSize = imgui.ImVec2(260, 280)
local qsf = {
	open  = {},
	pos   = {},
	node  = {},
	sc    = {},
	paths = {},
	flags = nil,
}

function M.init(c)
	ctx = c
end

function M.DrawQuickMenu()
	local State = ctx.State
	if not State.quickMenuOpen then
		return
	end

	local hotkeys = ctx.hotkeys
	local folders = ctx.folders
	local _d = ctx._d
	local fa = ctx.fa
	local perf_state = ctx.perf_state
	local module = ctx.module
	local enqueueHotkey = ctx.enqueueHotkey

	if State.hotkeysDirty then
		ctx.refreshHotkeyNumbers()
		State.hotkeysDirty = false
	end

	local quickFrameState = perf_state.buildQuickMenuFrameState()
	local visibleFolders = {}
	for _, folder in ipairs(folders) do
		if quickFrameState.folderHasQuickBindsVisible(folder) then
			visibleFolders[#visibleFolders + 1] = folder
		end
	end
	if #visibleFolders == 0 then
		State.quickMenuOpen = false
		module.quickMenuOpen = false
		State.quickMenuScrollQueued = 0
		State.quickMenuSelectRequest = nil
		M.resetState()
		return
	end

	local resX, resY = getScreenResolution()
	if quickMenuPos.x == 0 and quickMenuPos.y == 0 then
		quickMenuPos = imgui.ImVec2(resX / 2 - quickMenuSize.x / 2, resY / 2 - quickMenuSize.y / 2)
	end
	if _d.mimgui_funcs and _d.mimgui_funcs.clampWindowToScreen then
		quickMenuPos, quickMenuSize = _d.mimgui_funcs.clampWindowToScreen(quickMenuPos, quickMenuSize, 5)
	end
	imgui.SetNextWindowPos(quickMenuPos, imgui.Cond.Always)
	imgui.SetNextWindowSize(quickMenuSize, imgui.Cond.Always)
	imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(8, 8))
	imgui.PushStyleVarVec2(imgui.StyleVar.FramePadding, imgui.ImVec2(4, 3))
	imgui.PushStyleVarVec2(imgui.StyleVar.ItemSpacing, imgui.ImVec2(4, 4))
	imgui.Begin(L("binder.quick_menu.text.text"), nil, imgui.WindowFlags.NoCollapse)
	quickMenuPos = imgui.GetWindowPos()
	quickMenuSize = imgui.GetWindowSize()

	local ICON_FOLDER = (fa.FOLDER ~= "" and (fa.FOLDER .. " ") or "")
	local ICON_KEYB   = (fa.KEYBOARD ~= "" and (fa.KEYBOARD .. " ") or "")
	local ICON_ARROW  = (fa.ANGLE_RIGHT ~= "" and (" " .. fa.ANGLE_RIGHT) or " >")
	local io = imgui.GetIO()

	for k in pairs(qsf.sc) do qsf.sc[k] = nil end
	for path in pairs(qsf.open) do
		qsf.sc[path] = true
	end
	if not qsf.flags then
		qsf.flags = _d.flags_or(
			imgui.WindowFlags.NoDecoration,
			imgui.WindowFlags.NoMove,
			imgui.WindowFlags.AlwaysAutoResize,
			imgui.WindowFlags.NoSavedSettings,
			imgui.WindowFlags.NoFocusOnAppearing,
			imgui.WindowFlags.NoNav
		)
	end

	local function quickMenuItem(label, shortcut, enabled)
		imgui.PushStyleVarVec2(imgui.StyleVar.SelectableTextAlign, imgui.ImVec2(0, 0.5))
		local clicked = imgui.MenuItemBool(label, shortcut, false, enabled)
		imgui.PopStyleVar()
		return clicked
	end

	local function drawRec(node)
		if not quickFrameState.isFolderVisible(node) then
			return
		end
		local nodeEntries = quickFrameState.quickEntriesFor(node)
		for _, entry in ipairs(nodeEntries) do
			local hk, i = entry.hk, entry.idx
			local displayNumber = hk._number or i
			local visibleLabel = ICON_KEYB .. (hk.label or ("bind" .. displayNumber))
			local label = visibleLabel .. "##quick_bind" .. i
			local shortcut
			if hk.keys and #hk.keys > 0 then
				shortcut = _d.hotkeyToString(hk.keys)
			else
				shortcut = ""
			end
			if quickMenuItem(label, shortcut, hk.enabled) then
				enqueueHotkey(hk)
			end
		end
		for _, child in ipairs(node.children or {}) do
			if quickFrameState.folderHasQuickBindsVisible(child) then
				local path = quickFrameState.folderKey(child)
				local isOpen = qsf.open[path] or false
				imgui.PushStyleVarVec2(imgui.StyleVar.SelectableTextAlign, imgui.ImVec2(0, 0.5))
				imgui.Selectable(ICON_FOLDER .. child.name .. ICON_ARROW .. "##qfs_" .. path, isOpen)
				imgui.PopStyleVar()
				local itemMin  = imgui.GetItemRectMin()
				local itemMax  = imgui.GetItemRectMax()
				local winRight = imgui.GetWindowPos().x + imgui.GetWindowSize().x
				qsf.pos[path] = imgui.ImVec2(winRight, itemMin.y - 10)
				local mx, my = io.MousePos.x, io.MousePos.y
				local inRow = mx >= itemMin.x and mx <= winRight
				            and my >= itemMin.y and my <= itemMax.y
				if imgui.IsItemHovered() or inRow then
					qsf.open[path] = true
					qsf.node[path] = child
					qsf.sc[path]   = false
				end
			end
		end
	end

	local hoveredQuickMenu = imgui.IsWindowHovered((imgui.HoveredFlags and imgui.HoveredFlags.RootAndChildWindows) or 0)

	local visibleCount = #visibleFolders
	if visibleCount == 0 then
		State.quickMenuTabIndex = 1
		State.quickMenuSelectRequest = nil
	else
		local clampedIndex = math.min(math.max(State.quickMenuTabIndex, 1), visibleCount)
		if clampedIndex ~= State.quickMenuTabIndex then
			State.quickMenuTabIndex = clampedIndex
			State.quickMenuSelectRequest = clampedIndex
		end

		local scrollSteps = State.quickMenuScrollQueued
		State.quickMenuScrollQueued = 0

		if hoveredQuickMenu then
			scrollSteps = io.MouseWheel
		end

		if scrollSteps ~= 0 then
			local previousIndex = State.quickMenuTabIndex
			State.quickMenuTabIndex = State.quickMenuTabIndex + scrollSteps
			State.quickMenuTabIndex = ((State.quickMenuTabIndex - 1) % visibleCount) + 1
			if State.quickMenuTabIndex ~= previousIndex then
				State.quickMenuSelectRequest = State.quickMenuTabIndex
			end
		end
	end

	if imgui.BeginTabBar("##quickbinder_tabbar") then
		for idx, folder in ipairs(visibleFolders) do
			local tabFlags = 0
			local hasSelectFlag = _d.mimgui_funcs and _d.mimgui_funcs.TabItemFlags and _d.mimgui_funcs.TabItemFlags.SetSelected
			if State.quickMenuSelectRequest == idx and hasSelectFlag then
				tabFlags = _d.flags_or(tabFlags, _d.mimgui_funcs.TabItemFlags.SetSelected)
			end
			local tabOpened = imgui.BeginTabItem(folder.name, nil, tabFlags)
			if tabOpened then
				if State.quickMenuTabIndex ~= idx then
					State.quickMenuTabIndex = idx
				end
				if State.quickMenuSelectRequest == idx then
					State.quickMenuSelectRequest = nil
				end
				drawRec(folder)
				imgui.EndTabItem()
			end
			if imgui.IsItemHovered() and imgui.IsMouseClicked(0) then
				if State.quickMenuTabIndex ~= idx then
					State.quickMenuTabIndex = idx
				end
				State.quickMenuSelectRequest = idx
			end
		end
		imgui.EndTabBar()
	end
	imgui.End()
	imgui.PopStyleVar(3)

	local n = 0
	for path in pairs(qsf.open) do
		n = n + 1
		qsf.paths[n] = path
	end
	for i = n + 1, #qsf.paths do qsf.paths[i] = nil end
	for _, path in ipairs(qsf.paths) do
		local node = qsf.node[path]
		local pos  = qsf.pos[path]
		if node and pos then
			imgui.SetNextWindowPos(pos, imgui.Cond.Always)
			imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(8, 8))
			imgui.PushStyleVarVec2(imgui.StyleVar.FramePadding, imgui.ImVec2(4, 3))
			imgui.PushStyleVarVec2(imgui.StyleVar.ItemSpacing, imgui.ImVec2(4, 4))
			imgui.Begin("##qfsub_" .. path, nil, qsf.flags)
			drawRec(node)
			local hflags = (imgui.HoveredFlags and
				_d.flags_or(imgui.HoveredFlags.AllowWhenOverlapped, imgui.HoveredFlags.AllowWhenBlockedByActiveItem)
			) or 96
			if imgui.IsWindowHovered(hflags) then
				qsf.sc[path] = false
			end
			imgui.End()
			imgui.PopStyleVar(3)
		end
	end

	for _ = 1, 3 do
		for _, path in ipairs(qsf.paths) do
			if qsf.sc[path] then
				local node = qsf.node[path]
				if node then
					for _, child in ipairs(node.children or {}) do
						local childPath = quickFrameState.folderKey(child)
						if qsf.open[childPath] and not qsf.sc[childPath] then
							qsf.sc[path] = false
							break
						end
					end
				end
			end
		end
	end

	for path, shouldClose in pairs(qsf.sc) do
		if shouldClose then
			qsf.open[path] = nil
		end
	end
end

function M.resetState()
	for k in pairs(qsf.open) do qsf.open[k] = nil end
	for k in pairs(qsf.pos)  do qsf.pos[k]  = nil end
	for k in pairs(qsf.node) do qsf.node[k] = nil end
end

return M
