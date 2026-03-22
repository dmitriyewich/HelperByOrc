local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}

local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local ctx

function M.init(c)
	ctx = c
end

-- === Popup state (global for hot-reload safety) ===

if not _G.moveBindPopup then
	_G.moveBindPopup = { active = false, hkidx = nil }
end
if not _G.deleteBindPopup then
	_G.deleteBindPopup = { active = false, idx = nil, from_edit = false }
end
if not _G.deleteFolderPopup then
	_G.deleteFolderPopup = { active = false, folder = nil }
end
if not _G.bindLinesPopup then
	_G.bindLinesPopup = {
		active = false,
		pending_open = false,
		hk = nil,
		searchBuf = nil,
		open = nil,
	}
end

local function getBindLinesPopupState()
	local st = _G.bindLinesPopup
	if not st.searchBuf then
		st.searchBuf = imgui.new.char[256]("")
	end
	if not st.open then
		st.open = imgui.new.bool(true)
	end
	return st
end

function M.requestBindLinesPopup(hk)
	if not hk then
		return false, "bind_not_found"
	end
	if not (hk.messages and #hk.messages > 0) then
		return false, "bind_no_messages"
	end
	local st = getBindLinesPopupState()
	st.hk = hk
	st.active = true
	st.pending_open = true
	st.open[0] = true
	imgui.StrCopy(st.searchBuf, "", 256)
	return true, nil
end

function M.drawBindLinesPopup()
	local st = getBindLinesPopupState()
	if st.pending_open then
		imgui.OpenPopup("binder_bind_lines_popup")
		st.pending_open = false
	end

	if st.active and st.open and not st.open[0] then
		st.active = false
		st.hk = nil
		st.pending_open = false
	end

	if imgui.SetNextWindowSize then
		imgui.SetNextWindowSize(imgui.ImVec2(560, 440), imgui.Cond.Appearing)
	end

	if imgui.BeginPopupModal("binder_bind_lines_popup", st.open, imgui.WindowFlags.NoResize) then
		local hk = st.hk
		local trim = ctx.trim
		local _d = ctx._d
		local fa = ctx.fa

		local label = trim((hk and hk.label) or "")
		if label == "" then
			label = L("binder.popups.text.text")
		end
		_d.imgui_text_safe(L("binder.popups.text.text_1") .. label)
		imgui.Separator()

		local searchHint = L("binder.popups.text.text_2")
		if fa.MAGNIFYING_GLASS and fa.MAGNIFYING_GLASS ~= "" then
			searchHint = fa.MAGNIFYING_GLASS .. L("binder.popups.text.text_3")
		end
		imgui.PushItemWidth(-1)
		imgui.InputTextWithHint("##bind_popup_search", searchHint, st.searchBuf, 256)
		imgui.PopItemWidth()
		imgui.Spacing()

		local query = string.lower(trim(ffi.string(st.searchBuf)))
		local sent = false
		local doSend = ctx.doSend
		local preview_method_label = ctx.preview_method_label

		imgui.BeginChild("bind_popup_lines", imgui.ImVec2(0, 300), true)
		if hk and hk.messages and #hk.messages > 0 then
			local shown = 0
			for i, msg in ipairs(hk.messages) do
				local rawText = tostring((msg and msg.text) or "")
				local oneLine = rawText:gsub("[\r\n]+", " ")
				local method = preview_method_label(msg and msg.method or 0)
				local itemText = string.format("%d. [%s] %s", i, method, oneLine)
				local hay = string.lower(itemText)
				if query == "" or hay:find(query, 1, true) then
					shown = shown + 1
					if imgui.Selectable(itemText .. "##bind_popup_line_" .. i, false) then
						if trim(rawText) ~= "" then
							doSend(rawText, msg.method or 0, hk)
						end
						sent = true
					end
					if rawText ~= "" and imgui.IsItemHovered() then
						_d.imgui_set_tooltip_safe(rawText)
					end
				end
			end
			if shown == 0 then
				imgui.TextDisabled(L("binder.popups.text.text_4"))
			end
		else
			imgui.TextDisabled(L("binder.popups.text.text_5"))
		end
		imgui.EndChild()

		if imgui.Button(((fa.XMARK or "X") .. L("binder.popups.text.text_6")), imgui.ImVec2(140, 0)) or sent then
			st.active = false
			st.hk = nil
			st.open[0] = false
			imgui.CloseCurrentPopup()
		end

		imgui.EndPopup()
	end
end

function M.drawDeletePopups()
	local hotkeys = ctx.hotkeys
	local folders = ctx.folders
	local trim = ctx.trim
	local _d = ctx._d
	local fa = ctx.fa
	local State = ctx.State
	local markHotkeysDirty = ctx.markHotkeysDirty
	local pushToast = ctx.pushToast
	local folderFullPath = ctx.folderFullPath
	local pathStartsWith = ctx.pathStartsWith
	local moveHotkeysFromFolderPath = ctx.moveHotkeysFromFolderPath
	local removeFolder = ctx.removeFolder
	local isProtectedRootFolder = ctx.isProtectedRootFolder

	if _G.deleteBindPopup.active then
		imgui.OpenPopup("binder_delete_bind")
		_G.deleteBindPopup.active = false
	end
	if imgui.BeginPopupModal("binder_delete_bind", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local idx = _G.deleteBindPopup.idx
		local hk = idx and hotkeys[idx]
		local label = hk and trim(hk.label or "")
		if not label or label == "" then
			label = string.format("#%d", idx or 0)
		end
		_d.imgui_text_safe((L("binder.popups.text.format_ya")):format(label))
		imgui.Separator()
		if imgui.Button(L("binder.popups.text.bind_confirm"), imgui.ImVec2(100, 0)) then
			if idx and hotkeys[idx] then
				table.remove(hotkeys, idx)
				State.hotkeysDirty = true
				markHotkeysDirty(true)
			end
			if _G.deleteBindPopup.from_edit then
				State.editHotkey.active = false
				State.editHotkey.idx = nil
			end
			_G.deleteBindPopup.idx = nil
			_G.deleteBindPopup.from_edit = false
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button(L("binder.popups.text.bind_confirm_7"), imgui.ImVec2(100, 0)) then
			_G.deleteBindPopup.idx = nil
			_G.deleteBindPopup.from_edit = false
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end

	if _G.deleteFolderPopup.active then
		imgui.OpenPopup("binder_delete_folder")
		_G.deleteFolderPopup.active = false
	end
	if imgui.BeginPopupModal("binder_delete_folder", nil, imgui.WindowFlags.AlwaysAutoResize) then
		local folder = _G.deleteFolderPopup.folder
		local name = folder and folder.name or ""
		_d.imgui_text_safe((L("binder.popups.text.format_ya_8")):format(name))
		imgui.TextDisabled(L("binder.popups.text.text_9"))
		imgui.Separator()
		if imgui.Button(L("binder.popups.text.folder_confirm"), imgui.ImVec2(100, 0)) then
			if folder then
				local isOnlyRoot = (folder.parent == nil and #folders <= 1)
				if isProtectedRootFolder(folder) then
					pushToast(L("binder.popups.text.text_10"), "warn", 3.0)
				elseif isOnlyRoot then
					pushToast(L("binder.popups.text.text_11"), "warn", 3.0)
				else
					local removedPath = folderFullPath(folder)
					local selectedPath = State.selectedFolder and folderFullPath(State.selectedFolder) or nil
					local fallbackFolder = folder.parent
					if not fallbackFolder then
						for _, root in ipairs(folders) do
							if root ~= folder then
								fallbackFolder = root
								break
							end
						end
					end
					local fallbackPath = fallbackFolder and folderFullPath(fallbackFolder) or nil

					removeFolder(folder.parent and folder.parent.children or folders, folder)

					if selectedPath and pathStartsWith(selectedPath, removedPath) then
						State.selectedFolder = fallbackFolder or folders[1]
					elseif State.selectedFolder == folder then
						State.selectedFolder = fallbackFolder or folders[1]
					end

					if fallbackPath and moveHotkeysFromFolderPath(removedPath, fallbackPath) > 0 then
						State.hotkeysDirty = true
					end
					markHotkeysDirty(true)
				end
			end
			_G.deleteFolderPopup.folder = nil
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button(L("binder.popups.text.folder_confirm_12"), imgui.ImVec2(100, 0)) then
			_G.deleteFolderPopup.folder = nil
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

function M.drawMoveBindPopup()
	local hotkeys = ctx.hotkeys
	local folders = ctx.folders
	local _d = ctx._d
	local fa = ctx.fa
	local State = ctx.State
	local markHotkeysDirty = ctx.markHotkeysDirty
	local folderFullPath = ctx.folderFullPath
	local ensureFolderIds = ctx.ensureFolderIds

	if _G.moveBindPopup.active then
		imgui.OpenPopup("binder_move_bind")
		_G.moveBindPopup.active = false
	end
	if imgui.SetNextWindowSize then
		imgui.SetNextWindowSize(imgui.ImVec2(420, 360), imgui.Cond.Always)
	end
	if imgui.BeginPopupModal("binder_move_bind", nil, imgui.WindowFlags.NoResize) then
		local idx = _G.moveBindPopup.hkidx
		local hk = idx and hotkeys[idx]
		imgui.Text(L("binder.popups.text.text_13"))
		imgui.Separator()

		local function applyFolder(folder)
			if hk and folder then
				hk.folderPath = folderFullPath(folder)
				State.hotkeysDirty = true
				markHotkeysDirty()
				imgui.CloseCurrentPopup()
			end
		end

		ensureFolderIds(folders)

		local hasTreeNode = imgui.TreeNodeEx ~= nil or imgui.TreeNode ~= nil
		local function drawFolderNodeSimple(f, depth)
			imgui.PushIDInt(f._id or 0)
			local hasChildren = f.children and #f.children > 0
			local opened = false
			if hasTreeNode and imgui.TreeNodeEx then
				local flags = imgui.TreeNodeFlags.OpenOnArrow + imgui.TreeNodeFlags.OpenOnDoubleClick
				if not hasChildren then
					flags = flags + imgui.TreeNodeFlags.Leaf + imgui.TreeNodeFlags.NoTreePushOnOpen
				end
				opened = imgui.TreeNodeEx(fa.FOLDER .. " " .. tostring(f.name or "") .. "##move_tree", flags)
			elseif hasTreeNode then
				opened = imgui.TreeNode(fa.FOLDER .. " " .. tostring(f.name or "") .. "##move_tree")
			else
				local indentPx = (depth or 0) * 16
				imgui.Indent(indentPx)
				_d.imgui_text_safe(fa.FOLDER .. " " .. tostring(f.name or ""))
				imgui.Unindent(indentPx)
			end

			if imgui.IsItemClicked() then
				applyFolder(f)
			end

			if hasTreeNode then
				if opened and hasChildren then
					for _, child in ipairs(f.children) do
						drawFolderNodeSimple(child, (depth or 0) + 1)
					end
				end
				if imgui.TreeNodeEx then
					if opened and hasChildren then
						imgui.TreePop()
					end
				else
					if opened then
						imgui.TreePop()
					end
				end
			elseif hasChildren then
				for _, child in ipairs(f.children) do
					drawFolderNodeSimple(child, (depth or 0) + 1)
				end
			end
			imgui.PopID()
		end

		imgui.BeginChild("move_bind_folders", imgui.ImVec2(0, 240), true)
		for _, f in ipairs(folders) do
			drawFolderNodeSimple(f, 0)
		end
		imgui.EndChild()

		imgui.Separator()
		if imgui.Button(L("binder.popups.text.text_14")) then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

function M.isBindLinesPopupActive()
	local st = _G.bindLinesPopup
	return st and (st.active or st.pending_open)
end

return M
