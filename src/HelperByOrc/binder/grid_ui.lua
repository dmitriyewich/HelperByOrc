local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local ctx

function M.init(c)
	ctx = c
end

-- === Local helpers ===

local function utf8_trim_last_char(s)
	s = tostring(s or "")
	local len = #s
	while len > 0 do
		local byte = s:byte(len)
		len = len - 1
		if byte < 0x80 or byte >= 0xC0 then
			break
		end
	end
	return s:sub(1, len)
end

local function ellipsize_utf8(text, maxWidth)
	text = tostring(text or "")
	if maxWidth == nil then return text end
	if maxWidth <= 0 then return "..." end
	if imgui.CalcTextSize(text).x <= maxWidth then return text end
	local ell = "..."
	local ell_w = imgui.CalcTextSize(ell).x
	local available = maxWidth - ell_w
	if available <= 0 then return ell end
	local base = text
	if base:sub(-3) == ell then base = base:sub(1, -4) end
	while base ~= "" and imgui.CalcTextSize(base).x > available do
		base = utf8_trim_last_char(base)
	end
	if base == "" then return ell end
	return base .. ell
end

local VirtualizedGrid = {
	item_width = 138,
	item_height = 56,
	spacing_x = 16,
	spacing_y = 16,
	calculate_visible = function(self, scroll_y, window_height)
		local start_index = math.floor(scroll_y / (self.item_height + self.spacing_y))
		local visible_rows = math.ceil(window_height / (self.item_height + self.spacing_y)) + 2
		return {
			start = math.max(1, start_index),
			count = visible_rows,
		}
	end,
	get_card_position = function(self, index, columns)
		local row = math.floor((index - 1) / columns)
		local col = (index - 1) % columns
		local x = col * (self.item_width + self.spacing_x)
		local y = row * (self.item_height + self.spacing_y)
		return x, y, self.item_width, self.item_height
	end,
}

local _globalSearchBuf
local function getGlobalSearchBuffer()
	if not _globalSearchBuf then
		_globalSearchBuf = imgui.new.char[128]()
	end
	return _globalSearchBuf
end

local function cloneHotkey(hk)
	local funcs = ctx.funcs
	local copy = funcs.deepcopy(hk)
	copy.is_running = false
	copy._co_state = nil
	copy.lastActivated = 0
	copy._bools = {}
	copy._cond_bools = {}
	copy._quick_cond_bools = {}
	copy._comboActive = false
	copy._debounce_until = nil
	return copy
end

-- === drawBindsGrid ===

local bindsSelectedIndex = nil
local dnd_active = false

local function drawBindsGrid()
	local useColumnsList = true
	local availWidth = imgui.GetContentRegionAvail().x
	local cardWidth, cardHeight = VirtualizedGrid.item_width, VirtualizedGrid.item_height
	local spacingX, spacingY = VirtualizedGrid.spacing_x, VirtualizedGrid.spacing_y
	local columns = math.max(1, math.floor((availWidth + spacingX) / (cardWidth + spacingX)))
	local x0 = imgui.GetCursorScreenPos().x
	local y = imgui.GetCursorScreenPos().y
	local mouseDown = false
	if imgui.IsMouseDown then
		mouseDown = imgui.IsMouseDown(0)
	else
		local io = imgui.GetIO and imgui.GetIO()
		if io and io.MouseDown then
			mouseDown = io.MouseDown[0]
		end
	end
	local dragActive = imgui.IsDragDropActive and imgui.IsDragDropActive() or false
	local need_reset = not mouseDown and not dragActive
	local q = string.lower(ffi.string(getGlobalSearchBuffer()))

	-- LOCAL REFS from ctx (cache at top of function for readability)
	local hotkeys = ctx.hotkeys
	local fa = ctx.fa
	local _d = ctx._d
	local trim = ctx.trim
	local State = ctx.State
	local perf_state = ctx.perf_state
	local markHotkeysDirty = ctx.markHotkeysDirty
	local folderFullPath = ctx.folderFullPath
	local pathKey = ctx.pathKey
	local pathEquals = ctx.pathEquals

	if State.hotkeysDirty then
		ctx.refreshHotkeyNumbers()
		State.hotkeysDirty = false
	end

	local curPath = folderFullPath(State.selectedFolder)
	local curPathKey = pathKey(curPath)
	local cacheHit = perf_state.binds_grid_cache.revision == perf_state.hotkeys_revision
		and perf_state.binds_grid_cache.hotkeys_count == #hotkeys
		and perf_state.binds_grid_cache.folder_key == curPathKey
		and perf_state.binds_grid_cache.query == q
	local cards = perf_state.binds_grid_cache.cards
	if not cacheHit then
		cards = {}
		for i, hk in ipairs(hotkeys) do
			if pathEquals(hk.folderPath, curPath) then
				if q ~= "" then
					local label = string.lower(trim(hk.label or ""))
					if not label:find(q, 1, true) then
						goto continue
					end
				end
				cards[#cards + 1] = { hk = hk, idx = i }
			end
			::continue::
		end
		perf_state.binds_grid_cache.revision = perf_state.hotkeys_revision
		perf_state.binds_grid_cache.hotkeys_count = #hotkeys
		perf_state.binds_grid_cache.folder_key = curPathKey
		perf_state.binds_grid_cache.query = q
		perf_state.binds_grid_cache.cards = cards
	end

	local addLabel = (fa.SQUARE_PLUS ~= "" and (fa.SQUARE_PLUS .. " ") or "")
		.. L("binder.grid_ui.text.add_bind_cols")
	local addSize = imgui.CalcTextSize(addLabel)
	local rightX = imgui.GetCursorPosX()
		+ imgui.GetContentRegionAvail().x
		- addSize.x
		- imgui.GetStyle().FramePadding.x * 2
	if rightX > imgui.GetCursorPosX() then
		imgui.SetCursorPosX(rightX)
	end
	if imgui.SmallButton(addLabel) then
		local hk = ctx.newHotkeyBase()
		hk.folderPath = folderFullPath(State.selectedFolder)
		table.insert(hotkeys, hk)
		State.hotkeysDirty = true
		markHotkeysDirty()
	end
	imgui.Spacing()
	imgui.Columns(5, "binds_cols", false)
	local tableMinX = imgui.GetCursorScreenPos().x
	local baseOffset = imgui.GetColumnOffset(0)
	local s = imgui.GetStyle()
	local btnH = imgui.GetFrameHeight()
	local btnW = btnH + 6
	local border = (s.FrameBorderSize or 0)
	local btnVisualW = btnW + (border * 2) + 2
	local colBtnPad = 6
	local col1W = btnVisualW + (colBtnPad * 2)
	local col2W = btnVisualW + (colBtnPad * 2)
	local availableWidth = imgui.GetWindowContentRegionMax().x - imgui.GetWindowContentRegionMin().x

	-- 3-я колонка примерно в 2 раза уже
	local col3W = math.min(140, math.max(70, math.floor(availableWidth * 0.11)))

	-- 5-я колонка под кнопки
	local btnSpacing = imgui.GetStyle().ItemSpacing.x
	local maxButtons = 5
	local col5W = (btnW * maxButtons) + (btnSpacing * (maxButtons - 1)) + 6

	-- 4-я колонка остаток
	local col4W = math.max(130, availableWidth - (col1W + col2W + col3W + col5W))
	local contentWidth = col1W + col2W + col3W + col4W + col5W
	imgui.SetColumnOffset(1, baseOffset + col1W)
	imgui.SetColumnOffset(2, baseOffset + col1W + col2W)
	imgui.SetColumnOffset(3, baseOffset + col1W + col2W + col3W)
	imgui.SetColumnOffset(4, baseOffset + col1W + col2W + col3W + col4W)
	imgui.SetColumnOffset(5, baseOffset + contentWidth)
	local x1 = tableMinX + col1W
	local x2 = tableMinX + col1W + col2W
	local x3 = tableMinX + col1W + col2W + col3W
	local x4 = tableMinX + col1W + col2W + col3W + col4W
	local headerTopY = imgui.GetCursorScreenPos().y
	local function drawHeaderCentered(label, tooltipOpt)
		local colW = imgui.GetColumnWidth()
		local textW = imgui.CalcTextSize(label).x
		local startX = imgui.GetCursorPosX()
		imgui.SetCursorPosX(startX + (colW - textW) * 0.5)
		_d.imgui_text_disabled_safe(label)
		if tooltipOpt ~= nil and imgui.IsItemHovered() then
			_d.imgui_set_tooltip_safe(tooltipOpt)
		end
	end

	local activeLabel = (fa.TOGGLE_ON ~= "" and fa.TOGGLE_ON) or (fa.POWER_OFF ~= "" and fa.POWER_OFF) or "A"
	local menuLabel = (fa.BOLT ~= "" and fa.BOLT) or (fa.STAR ~= "" and fa.STAR) or "M"
	drawHeaderCentered(activeLabel, L("binder.grid_ui.text.text"))
	imgui.NextColumn()
	drawHeaderCentered(menuLabel, L("binder.grid_ui.text.text_1"))
	imgui.NextColumn()
	drawHeaderCentered(L("binder.grid_ui.text.text_2"))
	imgui.NextColumn()
	drawHeaderCentered(L("binder.grid_ui.text.text_3"))
	imgui.NextColumn()
	drawHeaderCentered(L("binder.grid_ui.text.text_4"))
	imgui.NextColumn()
	local headerLine = imgui.GetCursorScreenPos()
	local headerBottomY = headerLine.y
	local headerBorder = imgui.GetStyle().Colors[imgui.Col.Border]
	local headerBorderCol = imgui.ImVec4(headerBorder.x, headerBorder.y, headerBorder.z, headerBorder.w * 0.3)
	local headerU32 = imgui.GetColorU32Vec4(headerBorderCol)
	local dl = imgui.GetWindowDrawList()
	local y = headerBottomY
	imgui.PushClipRect(imgui.ImVec2(tableMinX, y - 2), imgui.ImVec2(tableMinX + contentWidth, y + 2), false)
	dl:AddLine(imgui.ImVec2(tableMinX, y), imgui.ImVec2(tableMinX + contentWidth, y), headerU32, 1)
	imgui.PopClipRect()
	imgui.Dummy(imgui.ImVec2(0, 1))
	imgui.SetCursorScreenPos(imgui.ImVec2(tableMinX, imgui.GetCursorScreenPos().y))
	local rowsStartY = imgui.GetCursorScreenPos().y

	local style = imgui.GetStyle()
	local rowContentH = math.max(imgui.GetFrameHeight(), imgui.GetTextLineHeight()) + 6
	local rowStep = rowContentH
	local clipBottomY = rowsStartY + (#cards * rowStep)
	do
		local borderCol = style.Colors[imgui.Col.Border]
		local vcol = imgui.GetColorU32Vec4(imgui.ImVec4(borderCol.x, borderCol.y, borderCol.z, borderCol.w * 0.3))
		local dl2 = imgui.GetWindowDrawList()
		local winPos = imgui.GetWindowPos and imgui.GetWindowPos() or imgui.ImVec2(0, 0)
		local winSize = imgui.GetWindowSize and imgui.GetWindowSize() or imgui.ImVec2(0, 0)
		local clipTop = math.max(headerTopY, winPos.y)
		local clipBottom = math.min(clipBottomY, winPos.y + winSize.y)
		if clipBottom > clipTop then
			imgui.PushClipRect(
				imgui.ImVec2(tableMinX, clipTop),
				imgui.ImVec2(tableMinX + contentWidth, clipBottom),
				false
			)
			dl2:AddLine(imgui.ImVec2(x1, headerTopY), imgui.ImVec2(x1, clipBottomY), vcol, 1)
			dl2:AddLine(imgui.ImVec2(x2, headerTopY), imgui.ImVec2(x2, clipBottomY), vcol, 1)
			dl2:AddLine(imgui.ImVec2(x3, headerTopY), imgui.ImVec2(x3, clipBottomY), vcol, 1)
			dl2:AddLine(imgui.ImVec2(x4, headerTopY), imgui.ImVec2(x4, clipBottomY), vcol, 1)
			imgui.PopClipRect()
		end
	end
	local clipper = imgui.ImGuiListClipper(#cards, rowStep)
	while clipper:Step() do
		for localIndex = clipper.DisplayStart, clipper.DisplayEnd - 1 do
			local card = cards[localIndex + 1]
			if not card then
				break
			end
			local hk, i = card.hk, card.idx
			local rowIndex = localIndex
			local rowStart = imgui.GetCursorScreenPos()
			local rowEnd = imgui.ImVec2(rowStart.x + contentWidth, rowStart.y + rowContentH)
			local clickedOnWidget = false
			local yBtn = rowStart.y + (rowContentH - imgui.GetFrameHeight()) / 2
			local yTxt = rowStart.y + (rowContentH - imgui.GetTextLineHeight()) / 2
			local function set_col_y(y)
				local p = imgui.GetCursorScreenPos()
				imgui.SetCursorScreenPos(imgui.ImVec2(p.x, y))
			end
			local function mark_widget_clicked(clicked)
				if clicked or imgui.IsItemClicked(0) then
					clickedOnWidget = true
				end
			end
			local function action_btn(id, icon, enabled, tooltip, size)
				local clicked = false
				if not enabled and imgui.BeginDisabled then
					imgui.BeginDisabled(true)
				end
				clicked = imgui.Button(icon .. id, size)
				if not enabled and imgui.BeginDisabled then
					imgui.EndDisabled()
				end
				if imgui.IsItemHovered() and tooltip then
					_d.imgui_set_tooltip_safe(tooltip)
				end
				return enabled and clicked
			end
			local function centerInColumn(widgetW, minPad)
				local colW = imgui.GetColumnWidth()
				local localX = imgui.GetCursorPosX()
				local pad = (colW - widgetW) * 0.5
				if minPad then
					pad = math.max(minPad, pad)
				end
				imgui.SetCursorPosX(localX + pad)
			end
			local actionBtnSize = imgui.ImVec2(imgui.GetFrameHeight() + 6, imgui.GetFrameHeight())
			imgui.SetCursorScreenPos(rowStart)

			local dl = imgui.GetWindowDrawList()
			local fullMin = imgui.ImVec2(tableMinX, rowStart.y)
			local fullMax = imgui.ImVec2(tableMinX + contentWidth, rowStart.y + rowContentH)
			if dnd_active then
				local savedPos = imgui.GetCursorScreenPos()
				local dropW = contentWidth - col5W
				imgui.PushClipRect(
					imgui.ImVec2(tableMinX, rowStart.y),
					imgui.ImVec2(tableMinX + dropW, rowStart.y + rowContentH),
					false
				)
				imgui.SetCursorScreenPos(imgui.ImVec2(tableMinX, rowStart.y))
				imgui.PushIDInt(i)
				imgui.InvisibleButton("row_drop", imgui.ImVec2(dropW, rowContentH))
				if imgui.BeginDragDropTarget() then
					local acceptFlags = 1024 + 2048
					local payload = imgui.AcceptDragDropPayload("BINDER_HOTKEY", acceptFlags)
					if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
						local mp = (imgui.GetMousePos and imgui.GetMousePos()) or imgui.GetIO().MousePos
						local before = mp.y < (rowStart.y + rowContentH * 0.5)
						local lineY = before and (rowStart.y + 1) or (rowStart.y + rowContentH - 1)
						local borderCol = style.Colors[imgui.Col.Border]
						local lineCol =
							imgui.ImVec4(borderCol.x, borderCol.y, borderCol.z, math.min(1, borderCol.w * 1.3))
						dl:AddLine(
							imgui.ImVec2(tableMinX, lineY),
							imgui.ImVec2(tableMinX + dropW, lineY),
							imgui.GetColorU32Vec4(lineCol),
							1
						)
						local delivered = payload.Delivery
						if delivered == nil and imgui.IsMouseReleased then
							delivered = imgui.IsMouseReleased(0)
						end
						if delivered then
							local src_idx = ffi.cast("int*", payload.Data)[0]
							local dst_idx = before and i or (i + 1)
							if dst_idx < 1 then
								dst_idx = 1
							end
							if dst_idx > (#hotkeys + 1) then
								dst_idx = #hotkeys + 1
							end
							if src_idx >= 1 and src_idx <= #hotkeys then
								if dst_idx > src_idx then
									dst_idx = dst_idx - 1
								end
								if dst_idx ~= src_idx then
									local moved = table.remove(hotkeys, src_idx)
									table.insert(hotkeys, dst_idx, moved)
									State.hotkeysDirty = true
									markHotkeysDirty()
								end
							end
							dnd_active = false
						end
					end
					imgui.EndDragDropTarget()
				end
				imgui.PopID()
				imgui.SetCursorScreenPos(savedPos)
				imgui.PopClipRect()
			end

			local mp = (imgui.GetMousePos and imgui.GetMousePos()) or imgui.GetIO().MousePos
			local mx, my = mp.x, mp.y

			-- важно: IsWindowHovered, чтобы не ловить клики в другом окне
			local inWindow = imgui.IsWindowHovered and imgui.IsWindowHovered() or true
			local rowHovered = inWindow and mx >= fullMin.x and mx <= fullMax.x and my >= fullMin.y and my <= fullMax.y

			local rowClicked = rowHovered and imgui.IsMouseClicked(0)
			local rowDbl = rowHovered and imgui.IsMouseDoubleClicked(0)
			imgui.PushClipRect(fullMin, imgui.ImVec2(fullMax.x, fullMax.y + 2), false)
			if (rowIndex % 2) == 1 then
				local baseCol = imgui.GetStyle().Colors[imgui.Col.FrameBg]
				local zebra = imgui.ImVec4(baseCol.x, baseCol.y, baseCol.z, baseCol.w * 0.25)
				dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(zebra))
			end
			if bindsSelectedIndex == i then
				local selCol = imgui.GetStyle().Colors[imgui.Col.Header]
				local sel = imgui.ImVec4(selCol.x, selCol.y, selCol.z, selCol.w * 0.35)
				dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(sel))
			elseif rowHovered then
				local hoverCol = imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
				local hover = imgui.ImVec4(hoverCol.x, hoverCol.y, hoverCol.z, hoverCol.w * 0.2)
				dl:AddRectFilled(fullMin, fullMax, imgui.GetColorU32Vec4(hover))
			end
			local borderCol = imgui.GetStyle().Colors[imgui.Col.Border]
			local border = imgui.ImVec4(borderCol.x, borderCol.y, borderCol.z, borderCol.w * 0.3)
			local lineY = fullMax.y - 1
			dl:AddLine(
				imgui.ImVec2(tableMinX, lineY),
				imgui.ImVec2(tableMinX + contentWidth, lineY),
				imgui.GetColorU32Vec4(border),
				1
			)
			imgui.PopClipRect()

			local displayNumber = hk._number or i
			local bindName = hk.label or ("bind" .. displayNumber)
			local isEnabled = hk.enabled == nil and true or hk.enabled
			if not isEnabled then
				imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, imgui.GetStyle().Alpha * 0.75)
				local textCol = imgui.GetStyle().Colors[imgui.Col.Text]
				imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(textCol.x, textCol.y, textCol.z, textCol.w * 0.8))
				local disabledCol = imgui.GetStyle().Colors[imgui.Col.TextDisabled]
				imgui.PushStyleColor(
					imgui.Col.TextDisabled,
					imgui.ImVec4(disabledCol.x, disabledCol.y, disabledCol.z, disabledCol.w * 0.8)
				)
			end

			set_col_y(yBtn)
			centerInColumn(actionBtnSize.x, 4)
			local toggleOnIcon = (fa.TOGGLE_ON ~= "" and fa.TOGGLE_ON)
				or (fa.POWER_OFF ~= "" and fa.POWER_OFF)
				or fa.CHECK_CIRCLE
				or ""
			local toggleOffIcon = (fa.TOGGLE_OFF ~= "" and fa.TOGGLE_OFF)
				or (fa.BAN ~= "" and fa.BAN)
				or fa.TIMES_CIRCLE
				or ""
			local toggleIcon = isEnabled and toggleOnIcon or toggleOffIcon
			local toggleClicked = action_btn(
				"##hit_toggle_" .. i,
				toggleIcon,
				true,
				L("binder.grid_ui.text.text_5"),
				actionBtnSize
			)
			mark_widget_clicked(toggleClicked)
			if toggleClicked then
				local nextEnabled = not isEnabled
				hk.enabled = nextEnabled
				if not nextEnabled then
					hk.quick_menu = false
				end
				markHotkeysDirty()
			end
			imgui.NextColumn()
			local isQuickMenu = hk.quick_menu and true or false
			set_col_y(yBtn)
			centerInColumn(actionBtnSize.x, 4)
			local quickIcon = (fa.BOLT ~= "" and fa.BOLT) or (fa.STAR ~= "" and fa.STAR) or ""
			if isEnabled and not isQuickMenu then
				local disabledCol = imgui.GetStyle().Colors[imgui.Col.TextDisabled]
				imgui.PushStyleColor(
					imgui.Col.Text,
					imgui.ImVec4(disabledCol.x, disabledCol.y, disabledCol.z, disabledCol.w)
				)
			end
			local quickClicked = action_btn(
				"##hit_quick_" .. i,
				quickIcon,
				isEnabled,
				L("binder.grid_ui.text.text_6"),
				actionBtnSize
			)
			if isEnabled and not isQuickMenu then
				imgui.PopStyleColor()
			end
			mark_widget_clicked(quickClicked)
			if quickClicked then
				hk.quick_menu = not isQuickMenu
				markHotkeysDirty()
			end
			imgui.NextColumn()
			set_col_y(yTxt)
			local padX = 6
			local startX = imgui.GetCursorPosX()
			imgui.SetCursorPosX(startX + padX)
			local activationWidth = imgui.GetColumnWidth() - (padX * 2)
			local trig = hk.text_trigger
			local parts = {}
			local hasCmd = hk.command_enabled and hk.command and hk.command ~= ""
			local hasKeys = hk.keys and #hk.keys > 0
			local hasTrig = trig and trig.enabled and trig.text and trig.text ~= ""
			if hasTrig then
				local icon = fa.COMMENT ~= "" and fa.COMMENT or "TXT"
				table.insert(parts, {
					icon = icon,
					text = trig.text,
					tooltip = L("binder.grid_ui.text.text_7") .. tostring(trig.text),
					kind = "trig",
				})
			end
			if hasCmd then
				local icon = fa.TERMINAL ~= "" and fa.TERMINAL or "CMD"
				table.insert(parts, {
					icon = icon,
					text = hk.command,
					tooltip = L("binder.grid_ui.text.text_8") .. tostring(hk.command),
					kind = "cmd",
				})
			end
			if hasKeys then
				local icon = fa.KEYBOARD ~= "" and fa.KEYBOARD or "KEY"
				local keysText = _d.hotkeyToString(hk.keys)
				table.insert(parts, {
					icon = icon,
					text = keysText,
					tooltip = L("binder.grid_ui.text.text_9") .. keysText,
					kind = "keys",
				})
			end
			local partsCount = #parts
			if partsCount > 0 then
				local available = activationWidth - style.ItemSpacing.x * (partsCount - 1)
				local widths = {}
				if hasCmd and hasKeys then
					local remaining = available
					for idx, part in ipairs(parts) do
						if part.kind == "trig" then
							part.text = nil
							widths[idx] = imgui.CalcTextSize(part.icon).x
							remaining = math.max(0, remaining - widths[idx])
							break
						end
					end
					local wCmd = math.floor(remaining * 0.60)
					local wKeys = math.max(0, remaining - wCmd)
					for idx, part in ipairs(parts) do
						if part.kind == "cmd" then
							widths[idx] = wCmd
						elseif part.kind == "keys" then
							widths[idx] = wKeys
						end
					end
				else
					local base = partsCount > 0 and math.floor(available / partsCount) or 0
					local remainder = available - base * partsCount
					for idx = 1, partsCount do
						widths[idx] = base + (idx == partsCount and remainder or 0)
					end
				end
				for idx, part in ipairs(parts) do
					if idx > 1 then
						imgui.SameLine()
					end
					local width = widths[idx] or available
					local label = part.icon
					if part.text and part.text ~= "" then
						label = label .. " " .. part.text
					end
					label = ellipsize_utf8(label, width)
					_d.imgui_text_disabled_safe(label)
					if imgui.IsItemHovered() then
						_d.imgui_set_tooltip_safe(part.tooltip)
					end
				end
			end
			imgui.NextColumn()
			set_col_y(yTxt)
			local rowCount = #(hk.messages or {})
			local countText = " (" .. tostring(rowCount) .. ")"
			local colPos = imgui.GetCursorScreenPos()
			local colWidth = imgui.GetColumnWidth()
			local numberText = tostring(displayNumber)
			local numLabel = "№" .. numberText
			local numSize = imgui.CalcTextSize(numLabel)
			local countSize = imgui.CalcTextSize(countText)
			local innerPad = 8
			local gap = 6
			local innerMinX = colPos.x + innerPad
			local innerMaxX = colPos.x + colWidth - innerPad
			local nameMinX = innerMinX + numSize.x + gap
			local nameMaxX = innerMaxX - countSize.x - gap
			local nameAvail = math.max(0, nameMaxX - nameMinX)
			if not hk._name_cache then
				hk._name_cache = {}
			end
			local cache = hk._name_cache
			if cache.text ~= bindName or cache.width ~= nameAvail then
				cache.text = bindName
				cache.width = nameAvail
				cache.output = ellipsize_utf8(bindName, nameAvail)
			end
			local displayName = cache.output or bindName
			local nameSize = imgui.CalcTextSize(displayName)
			local colCenterX = colPos.x + (colWidth * 0.5)
			local nameX = colCenterX - (nameSize.x * 0.5)
			local nameClampMax = nameMaxX - nameSize.x
			if nameX < nameMinX then
				nameX = nameMinX
			end
			if nameX > nameClampMax then
				nameX = nameClampMax
			end
			local disabledColor = style.Colors[imgui.Col.TextDisabled]
			dl:AddText(imgui.ImVec2(innerMinX, colPos.y), imgui.GetColorU32Vec4(disabledColor), numLabel)
			dl:AddText(imgui.ImVec2(innerMaxX - countSize.x, colPos.y), imgui.GetColorU32Vec4(disabledColor), countText)
			dl:AddText(imgui.ImVec2(nameX, colPos.y), imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]), displayName)
			imgui.SetCursorScreenPos(colPos)
			imgui.InvisibleButton("##bind_name_" .. i, imgui.ImVec2(colWidth, rowContentH))
			if imgui.BeginDragDropSource() then
				dnd_active = true
				local payload = ffi.new("int[1]", i)
				imgui.SetDragDropPayload("BINDER_HOTKEY", payload, ffi.sizeof(payload))
				local dragLabelNumber = hk._number or i
				local dragLabel = hk.label or ("bind" .. dragLabelNumber)
				_d.imgui_text_safe(dragLabel)
				_d.imgui_text_disabled_safe(string.format("#%d", dragLabelNumber))
				imgui.EndDragDropSource()
			end
			if displayName ~= bindName and imgui.IsItemHovered() then
				_d.imgui_set_tooltip_safe(bindName)
			end
			imgui.NextColumn()
			set_col_y(yBtn)
			local canAction = isEnabled
			local playPauseCount = 1
			local runningExtra = hk.is_running and 1 or 0
			local buttonsCount = 3 + playPauseCount + runningExtra
			local groupW = (buttonsCount * actionBtnSize.x) + ((buttonsCount - 1) * style.ItemSpacing.x)
			centerInColumn(groupW, 4)
			if not hk.is_running then
				local playClicked =
					action_btn("##play_" .. i, fa.PLAY, canAction, L("binder.grid_ui.text.text_10"), actionBtnSize)
				mark_widget_clicked(playClicked)
				if playClicked then
					ctx.execution.enqueueHotkey(hk)
				end
			else
				if hk._co_state and hk._co_state.paused then
					local resumeClicked =
						action_btn("##resume_" .. i, fa.PLAY, canAction, L("binder.grid_ui.text.text_11"), actionBtnSize)
					mark_widget_clicked(resumeClicked)
					if resumeClicked then
						hk._co_state.paused = false
					end
				else
					local pauseClicked = action_btn("##pause_" .. i, fa.PAUSE, canAction, L("binder.grid_ui.text.text_12"), actionBtnSize)
					mark_widget_clicked(pauseClicked)
					if pauseClicked then
						hk._co_state = hk._co_state or {}
						hk._co_state.paused = true
					end
				end
				imgui.SameLine()
				local stopClicked = action_btn("##stop_" .. i, fa.STOP, canAction, L("binder.grid_ui.text.text_13"), actionBtnSize)
				mark_widget_clicked(stopClicked)
				if stopClicked then
					ctx.execution.stopHotkey(hk)
				end
			end
			imgui.SameLine()
			local editClicked = action_btn("##edit_" .. i, fa.PEN, true, L("binder.grid_ui.text.text_14"), actionBtnSize)
			mark_widget_clicked(editClicked)
			if editClicked then
				State.editHotkey.active = true
				State.editHotkey.idx = i
			end
			imgui.SameLine()
			local delClicked = action_btn("##del_" .. i, fa.TRASH, true, L("binder.grid_ui.text.text_15"), actionBtnSize)
			mark_widget_clicked(delClicked)
			if delClicked then
				_G.deleteBindPopup.idx = i
				_G.deleteBindPopup.from_edit = false
				_G.deleteBindPopup.active = true
			end
			imgui.SameLine()
			local ctxClicked = action_btn("##ctx_" .. i, fa.BARS, true, L("binder.grid_ui.text.text_16"), actionBtnSize)
			mark_widget_clicked(ctxClicked)
			if ctxClicked then
				imgui.OpenPopup("ctx_card_" .. i)
			end
			imgui.NextColumn()

			if imgui.BeginPopup("ctx_card_" .. i) then
				local dupClicked = imgui.MenuItemBool(L("binder.grid_ui.text.text_17"), false)
				mark_widget_clicked(dupClicked)
				if dupClicked then
					local newhk = cloneHotkey(hk)
					table.insert(hotkeys, i + 1, newhk)
					State.hotkeysDirty = true
					markHotkeysDirty()
				end
				local moveClicked = imgui.MenuItemBool(L("binder.grid_ui.text.text_18"), false)
				mark_widget_clicked(moveClicked)
					if moveClicked then
						_G.moveBindPopup.active = true
						_G.moveBindPopup.hkidx = i
						imgui.CloseCurrentPopup()
					end
					local popupClicked = imgui.MenuItemBool(L("binder.grid_ui.text.text_19"), false)
					mark_widget_clicked(popupClicked)
					if popupClicked then
						local ok_open, open_err = ctx.popups.requestBindLinesPopup(hk)
						if not ok_open then
							if open_err == "bind_no_messages" then
								ctx.pushToast(L("binder.grid_ui.text.text_20"), "warn", 2.5)
							else
								ctx.pushToast(L("binder.grid_ui.text.text_21"), "err", 2.5)
							end
						else
							imgui.CloseCurrentPopup()
						end
					end
					imgui.EndPopup()
				end

			if rowClicked and not clickedOnWidget then
				bindsSelectedIndex = i
			end
			if rowDbl and not clickedOnWidget then
				State.editHotkey.active = true
				State.editHotkey.idx = i
			end
			if imgui.SetColumnIndex then
				imgui.SetColumnIndex(0)
			end
			imgui.SetCursorScreenPos(imgui.ImVec2(tableMinX, rowStart.y + rowStep))

			if not isEnabled then
				imgui.PopStyleColor(2)
				imgui.PopStyleVar()
			end
		end
	end

	imgui.Columns(1)
	if need_reset then
		dnd_active = false
	end
end

-- === Folder search input ===

local function drawFolderSearchInput()
	local searchBuf = getGlobalSearchBuffer()
	if imgui.InputTextWithHint then
		imgui.InputTextWithHint(
			"##folder_search",
			L("binder.grid_ui.text.text_22"),
			searchBuf,
			ffi.sizeof(searchBuf)
		)
	else
		imgui.InputText("##folder_search", searchBuf, ffi.sizeof(searchBuf))
	end
end

-- === Breadcrumbs ===

local function drawOpenPathBreadcrumbs()
	local folderFullPath = ctx.folderFullPath
	local folders = ctx.folders
	local path = folderFullPath(ctx.State.selectedFolder)
	imgui.Text(L("binder.grid_ui.text.text_23"))
	imgui.SameLine()
	local node = nil
	local style = imgui.GetStyle()
	for i, name in ipairs(path) do
		if i == 1 then
			node = nil
			for _, f in ipairs(folders) do
				if f.name == name then
					node = f
					break
				end
			end
		else
			for _, c in ipairs(node.children) do
				if c.name == name then
					node = c
					break
				end
			end
		end
		local pos = imgui.GetCursorScreenPos()
		imgui.GetWindowDrawList():AddText(pos, imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text]), name)
		local text_size = imgui.CalcTextSize(name)
		imgui.SetCursorScreenPos(pos)
		imgui.InvisibleButton("##crumb" .. i, imgui.ImVec2(text_size.x, text_size.y))
		local rect_min = pos
		local rect_max = imgui.ImVec2(pos.x + text_size.x, pos.y + text_size.y)
		if imgui.IsItemHovered() then
			imgui.GetWindowDrawList():AddLine(
				imgui.ImVec2(rect_min.x, rect_max.y),
				imgui.ImVec2(rect_max.x, rect_max.y),
				imgui.GetColorU32Vec4(style.Colors[imgui.Col.ButtonHovered]),
				2
			)
			imgui.SetMouseCursor(imgui.MouseCursor.Hand)
		end
		if imgui.IsItemClicked(0) then
			ctx.State.selectedFolder = node
		end
		imgui.SetCursorScreenPos(imgui.ImVec2(rect_max.x, pos.y))
		if i < #path then
			imgui.Text(" / ")
			imgui.SameLine()
		end
	end
end

-- === Folder tabs ===

local function drawFolderTabs()
	-- LOCAL REFS from ctx
	local fa = ctx.fa
	local _d = ctx._d
	local folders = ctx.folders
	local labelInputs = ctx.labelInputs
	local ensureFolderIds = ctx.ensureFolderIds
	local sanitizeFolderName = ctx.sanitizeFolderName
	local folderNameUnique = ctx.folderNameUnique
	local isProtectedRootFolder = ctx.isProtectedRootFolder
	local createFolder = ctx.createFolder
	local markHotkeysDirty = ctx.markHotkeysDirty
	local folderFullPath = ctx.folderFullPath
	local remapHotkeysFolderPrefix = ctx.remapHotkeysFolderPrefix
	local ensure_bool = ctx.ensure_bool

	local tabHeight = 22
	local hasContextItem = imgui.BeginPopupContextItem ~= nil
	local hasTreeNode = imgui.TreeNodeEx ~= nil or imgui.TreeNode ~= nil
	local panelW = imgui.GetContentRegionAvail().x
	local style = imgui.GetStyle()
	local searchBuf = getGlobalSearchBuffer()
	local searchQuery = ""
	local foldersStartX = imgui.GetCursorPosX()
	local fallbackLeftPadPx = 4
	local fallbackIndentStepPx = 12
	local fallbackToggleGapPx = math.max(1, style.ItemInnerSpacing.x)
	local fallbackToggleTextRight = (fa.CARET_RIGHT ~= nil and fa.CARET_RIGHT ~= "") and fa.CARET_RIGHT or ">"
	local fallbackToggleTextDown = (fa.CARET_DOWN ~= nil and fa.CARET_DOWN ~= "") and fa.CARET_DOWN or "v"
	local fallbackToggleWidthPx = math.max(
		12,
		math.floor(
			math.max(imgui.CalcTextSize(fallbackToggleTextRight).x, imgui.CalcTextSize(fallbackToggleTextDown).x)
				+ style.FramePadding.x * 2
				+ 0.5
		)
	)
	local fallbackReserveRightPx = 18
	local rootBuf = labelInputs["addroot"] or imgui.new.char[256]()
	labelInputs["addroot"] = rootBuf

	ensureFolderIds(folders)

	local function tryCreateRootFolder()
		local rootName = sanitizeFolderName(ffi.string(rootBuf))
		if #rootName == 0 or not folderNameUnique(folders, rootName) then
			return false
		end
		local node = createFolder(rootName, nil)
		table.insert(folders, node)
		ctx.State.selectedFolder = node
		imgui.StrCopy(rootBuf, "", ffi.sizeof(rootBuf))
		markHotkeysDirty()
		return true
	end

	local function folderMatchesSearch(f)
		if searchQuery == "" then
			return true
		end
		local nameLower = string.lower(tostring(f.name or ""))
		if nameLower:find(searchQuery, 1, true) then
			return true
		end
		for _, child in ipairs(f.children or {}) do
			if folderMatchesSearch(child) then
				return true
			end
		end
		return false
	end

	local function drawFolderPopup(f, isRoot)
		_d.imgui_text_safe(fa.FOLDER .. " " .. f.name)
		imgui.Separator()
		-- Добавить подпапку
		_d.imgui_text_safe(fa.SQUARE_PLUS .. " " .. L("binder.grid_ui.text.text_24"))
		local subBuf = labelInputs["addsub_" .. f._id] or imgui.new.char[256]()
		if imgui.InputText("##new_sub", subBuf, ffi.sizeof(subBuf), ctx._d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			labelInputs["addsub_" .. f._id] = subBuf
		end
		imgui.SameLine()
		if imgui.SmallButton(fa.SQUARE_PLUS .. "##addsubok") then
			local subName = sanitizeFolderName(ffi.string(subBuf))
			if #subName > 0 and folderNameUnique(f.children, subName) then
				table.insert(f.children, createFolder(subName, f))
				imgui.StrCopy(subBuf, "", ffi.sizeof(subBuf))
				markHotkeysDirty()
			end
		end
		imgui.Separator()
		-- Переименовать
		_d.imgui_text_safe(fa.PEN .. " " .. L("binder.grid_ui.text.text_25"))
		local renameBuf = labelInputs["ren_" .. f._id] or imgui.new.char[256](f.name)
		if imgui.InputText("##ren", renameBuf, ffi.sizeof(renameBuf), ctx._d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			labelInputs["ren_" .. f._id] = renameBuf
		end
		imgui.SameLine()
		if imgui.SmallButton(fa.FLOPPY_DISK .. "##save_rename") then
			local newName = sanitizeFolderName(ffi.string(renameBuf))
			if #newName > 0 and folderNameUnique(f.parent and f.parent.children or folders, newName) then
				local oldPath = folderFullPath(f)
				f.name = newName
				local newPath = folderFullPath(f)
				if remapHotkeysFolderPrefix(oldPath, newPath) > 0 then
					ctx.State.hotkeysDirty = true
				end
				markHotkeysDirty()
				imgui.CloseCurrentPopup()
			end
		end

		imgui.Separator()
		if imgui.SmallButton(fa.BOLT .. L("binder.grid_ui.text.text_26")) then
			ctx.module._folderSettingsTarget = f
			ctx.module._openFolderSettingsModal = true
			imgui.CloseCurrentPopup()
		end
		imgui.Separator()
		local canDelete = not isProtectedRootFolder(f) and not (f.parent == nil and #folders <= 1)
		if canDelete then
			if imgui.SmallButton(fa.TRASH .. L("binder.grid_ui.text.text_27")) then
				_G.deleteFolderPopup.folder = f
				_G.deleteFolderPopup.active = true
				imgui.CloseCurrentPopup()
			end
		else
			_d.imgui_text_disabled_safe(fa.TRASH .. L("binder.grid_ui.text.text_27"))
		end
	end

	local function handleFolderDnD(f)
		local hotkeys = ctx.hotkeys
		local rectMin = nil
		local rectMax = nil
		if imgui.GetItemRectMin then
			rectMin = imgui.GetItemRectMin()
			rectMax = imgui.GetItemRectMax()
		end
		if imgui.BeginDragDropTarget() then
			local payload = imgui.AcceptDragDropPayload("HK_IDX")
			if payload == nil then
				payload = imgui.AcceptDragDropPayload()
			end
			if payload ~= nil then
				if rectMin and rectMax then
					imgui.GetWindowDrawList():AddRect(
						rectMin,
						rectMax,
						imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.ButtonHovered]),
						0,
						0,
						1
					)
				else
					imgui.PushStyleColor(imgui.Col.Button, imgui.GetStyle().Colors[imgui.Col.ButtonHovered])
					imgui.PopStyleColor()
				end
			end
			if payload ~= nil and payload.Data ~= ffi.NULL and payload.DataSize >= ffi.sizeof("int") then
				local src_idx = ffi.cast("int*", payload.Data)[0]
				if hotkeys[src_idx] then
					hotkeys[src_idx].folderPath = folderFullPath(f)
					markHotkeysDirty()
				end
			end
			imgui.EndDragDropTarget()
		end
	end

	local function drawFolderNode(f, depth, isRoot)
		if not folderMatchesSearch(f) then
			return
		end
		imgui.PushIDInt(f._id or 0)
		local indentPx = depth * 16
		local originalName = tostring(f.name or "")
		local maxNameW = math.max(40, panelW - indentPx - 70)
		if not hasTreeNode then
			local rowBaseX = foldersStartX + fallbackLeftPadPx + depth * fallbackIndentStepPx
			local textStartX = rowBaseX + fallbackToggleWidthPx + fallbackToggleGapPx
			maxNameW = math.max(40, panelW - textStartX - fallbackReserveRightPx)
		end
		local shownName = ellipsize_utf8(originalName, maxNameW)
		local hasChildren = f.children and #f.children > 0
		local opened = false
		local itemRectMin = nil
		local itemRectMax = nil
		if hasTreeNode then
			local flags = imgui.TreeNodeFlags.OpenOnArrow + imgui.TreeNodeFlags.OpenOnDoubleClick
			if ctx.State.selectedFolder == f then
				flags = flags + imgui.TreeNodeFlags.Selected
			end
			if not hasChildren then
				flags = flags + imgui.TreeNodeFlags.Leaf + imgui.TreeNodeFlags.NoTreePushOnOpen
			end
			if imgui.TreeNodeEx then
				opened = imgui.TreeNodeEx(fa.FOLDER .. " " .. shownName .. "##tree", flags)
			else
				opened = imgui.TreeNode(fa.FOLDER .. " " .. shownName .. "##tree")
			end
			if imgui.IsItemClicked() then
				ctx.State.selectedFolder = f
			end
			if imgui.GetItemRectMin then
				itemRectMin = imgui.GetItemRectMin()
				itemRectMax = imgui.GetItemRectMax()
			end
		else
			local rowBaseX = foldersStartX + fallbackLeftPadPx + depth * fallbackIndentStepPx
			local textStartX = rowBaseX + fallbackToggleWidthPx + fallbackToggleGapPx
			imgui.SetCursorPosX(rowBaseX)
			if hasChildren then
				local arrow = f._open and fa.CARET_DOWN or fa.CARET_RIGHT
				if imgui.SmallButton(arrow .. "##toggle") then
					f._open = not f._open
				end
			else
				imgui.Dummy(imgui.ImVec2(fallbackToggleWidthPx, 0))
			end
			imgui.SameLine(0, fallbackToggleGapPx)
			if imgui.GetCursorPosX() < textStartX then
				imgui.SetCursorPosX(textStartX)
			end
			if imgui.Selectable(fa.FOLDER .. " " .. shownName .. "##tree", ctx.State.selectedFolder == f) then
				ctx.State.selectedFolder = f
			end
			if hasChildren and imgui.IsItemHovered() and imgui.IsMouseDoubleClicked(0) then
				f._open = not f._open
			end
			opened = hasChildren and f._open
			if imgui.GetItemRectMin then
				itemRectMin = imgui.GetItemRectMin()
				itemRectMax = imgui.GetItemRectMax()
			end
		end

		if shownName ~= originalName and imgui.IsItemHovered() then
			imgui.BeginTooltip()
			imgui.TextUnformatted(originalName)
			imgui.EndTooltip()
		end

		handleFolderDnD(f)

		if hasContextItem then
			if imgui.BeginPopupContextItem("popup_gear") then
				drawFolderPopup(f, isRoot)
				imgui.EndPopup()
			end
		else
			if imgui.IsItemHovered() then
				imgui.SameLine()
				if imgui.SmallButton(fa.ELLIPSIS_VERTICAL .. "##gear") then
					imgui.OpenPopup("popup_gear")
				end
			end
			if imgui.BeginPopup("popup_gear") then
				drawFolderPopup(f, isRoot)
				imgui.EndPopup()
			end
		end

		if hasTreeNode then
			if opened and hasChildren then
				for _, child in ipairs(f.children) do
					drawFolderNode(child, depth + 1, false)
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
		else
			if opened and hasChildren then
				for _, child in ipairs(f.children) do
					drawFolderNode(child, depth + 1, false)
				end
			end
		end
		imgui.PopID()
	end

	searchQuery = string.lower(ffi.string(searchBuf))

	local addRootLabel = ((fa.SQUARE_PLUS ~= nil and fa.SQUARE_PLUS ~= "") and (fa.SQUARE_PLUS .. " ") or "")
		.. L("binder.grid_ui.text.text_28")
	if imgui.SmallButton(addRootLabel .. "##add_root_folder") then
		imgui.OpenPopup("binder_add_root_folder_popup")
	end
	if imgui.BeginPopup("binder_add_root_folder_popup") then
		_d.imgui_text_safe(addRootLabel)
		if imgui.IsWindowAppearing and imgui.IsWindowAppearing() and imgui.SetKeyboardFocusHere then
			imgui.SetKeyboardFocusHere()
		end
		imgui.PushItemWidth(math.max(120, panelW - 36))
		if imgui.InputText("##new_root_folder", rootBuf, ffi.sizeof(rootBuf), ctx._d.flags_or(imgui.InputTextFlags.AutoSelectAll)) then
			labelInputs["addroot"] = rootBuf
		end
		imgui.PopItemWidth()
		imgui.SameLine()
		if imgui.SmallButton((fa.SQUARE_PLUS ~= nil and fa.SQUARE_PLUS ~= "" and fa.SQUARE_PLUS or "+") .. "##add_root_folder_ok") then
			if tryCreateRootFolder() then
				imgui.CloseCurrentPopup()
			end
		end
		imgui.EndPopup()
	end
	imgui.Separator()

	for _, f in ipairs(folders) do
		drawFolderNode(f, 0, true)
	end

	if ctx.module._openFolderSettingsModal then
		imgui.OpenPopup("folder_settings_modal")
		ctx.module._openFolderSettingsModal = false
	end

	if imgui.BeginPopupModal("folder_settings_modal") then
		local f = ctx.module._folderSettingsTarget
		if not f then
			imgui.TextDisabled(L("binder.grid_ui.text.text_29"))
			if imgui.Button(L("binder.grid_ui.text.text_30")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		else
			_d.imgui_text_safe(fa.FOLDER .. " " .. (f.name or ""))
			imgui.Separator()

			local quickMenuLabel = (fa.BOLT and fa.BOLT .. " " or "")
				.. L("binder.grid_ui.text.folder_quick_menu_modal")
			f._quick_menu_bool = ensure_bool(f._quick_menu_bool, f.quick_menu ~= false)
			if imgui.Checkbox(quickMenuLabel, f._quick_menu_bool) then
				f.quick_menu = f._quick_menu_bool[0]
				markHotkeysDirty()
			end

			imgui.Separator()
			imgui.TextDisabled(L("binder.grid_ui.text.text_31"))
			imgui.BeginChild("settings_scroll", imgui.ImVec2(0, 180), true)
			f._quick_cond_bools = f._quick_cond_bools or {}
			local labels = ctx.execution.getQuickCondLabels()
			local quick_cond_count = #labels
			f.quick_conditions = f.quick_conditions or {}
			for ii = 1, quick_cond_count do
				local cur = f.quick_conditions[ii] and true or false
				f._quick_cond_bools[ii] = ensure_bool(f._quick_cond_bools[ii], cur)
				if imgui.Checkbox(labels[ii] .. "##fq_modal" .. ii, f._quick_cond_bools[ii]) then
					-- Записываем ВСЕ индексы, чтобы массив был плотным для JSON
					for jj = 1, quick_cond_count do
						f.quick_conditions[jj] = f._quick_cond_bools[jj][0] and true or false
					end
					markHotkeysDirty()
				end
			end
			imgui.EndChild()

			imgui.Separator()
			if imgui.Button(L("binder.grid_ui.text.text_30")) then
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
		end
	end

end

-- === DrawBinder (exported) ===

function M.DrawBinder()
	local State = ctx.State
	local editHotkey = State.editHotkey

	if not editHotkey.active then
		local style = imgui.GetStyle()
		local gap = style.ItemSpacing.x
		local availW = imgui.GetContentRegionAvail().x
		local leftW = math.min(220, math.max(160, 180))
		local rightW = math.max(1, availW - leftW - gap)

		imgui.PushItemWidth(leftW)
		drawFolderSearchInput()
		imgui.PopItemWidth()

		imgui.SameLine(0, gap)
		local x0 = imgui.GetCursorPosX()
		imgui.SetCursorPosX(x0)
		drawOpenPathBreadcrumbs()

		imgui.NewLine()
		imgui.Spacing()

		imgui.BeginChild("folders_panel", imgui.ImVec2(leftW, 0), true)
		drawFolderTabs()
		imgui.EndChild()

		imgui.SameLine(0, gap)
		imgui.BeginChild("binds_panel##main", imgui.ImVec2(0, 0), true)
		drawBindsGrid()
		imgui.EndChild()
	else
		ctx.edit_form.drawEditHotkey(editHotkey.idx)
	end

	ctx.popups.drawMoveBindPopup()
	ctx.popups.drawDeletePopups()
	ctx.popups.drawBindLinesPopup()
	ctx.flushHotkeysDirty(false)
end

-- === Exported helpers ===

M.ellipsize_utf8 = ellipsize_utf8
M.cloneHotkey = cloneHotkey

return M
