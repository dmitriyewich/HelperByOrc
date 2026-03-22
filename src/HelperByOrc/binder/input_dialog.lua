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

local activeInputDialog = nil
local currentMultiInputHK = nil
local multiInputResizeCallbackPtr = nil

function M.init(c)
	ctx = c

	local C = ctx.C
	if C.INPUTTEXT_CALLBACK_RESIZE and not multiInputResizeCallbackPtr then
		local function multiInputResizeCallback(data)
			if not currentMultiInputHK or data.EventFlag ~= C.INPUTTEXT_CALLBACK_RESIZE then
				return 0
			end
			local hk = currentMultiInputHK
			local len = data.BufTextLen or 0
			local text = ffi.string(data.Buf, len)
			local desired = math.max(C.MULTI_BUF_MIN, len + 1 + C.MULTI_BUF_PAD)
			hk._multiBuf = imgui.new.char[desired](text)
			hk._multiBufSize = desired
			hk._multiBufText = text
			data.Buf = hk._multiBuf
			data.BufSize = desired
			return 0
		end
		multiInputResizeCallbackPtr = ffi.cast("int (*)(ImGuiInputTextCallbackData*)", multiInputResizeCallback)
	end
end

function M.getActiveInputDialog()
	return activeInputDialog
end

function M.setCurrentMultiInputHK(hk)
	currentMultiInputHK = hk
end

function M.getMultiInputResizeCallbackPtr()
	return multiInputResizeCallbackPtr
end

-- === Dialog helpers ===

local function ensureDialogTables(dialog)
	dialog.buffers = dialog.buffers or {}
	dialog.list_selected = dialog.list_selected or {}
	dialog.list_multi_selected = dialog.list_multi_selected or {}
	dialog.search_buffers = dialog.search_buffers or {}
end

local function ensureDialogBuffer(dialog, idx)
	local C = ctx.C
	local buf = dialog.buffers[idx]
	if not buf then
		buf = imgui.new.char[C.INPUT_BUF_SIZE]()
		dialog.buffers[idx] = buf
		imgui.StrCopy(buf, "", C.INPUT_BUF_SIZE)
	end
	return buf
end

local function ensureDialogSearchBuffer(dialog, idx)
	local C = ctx.C
	local buf = dialog.search_buffers[idx]
	if not buf then
		buf = imgui.new.char[C.DIALOG_SEARCH_BUF_SIZE]()
		dialog.search_buffers[idx] = buf
		imgui.StrCopy(buf, "", C.DIALOG_SEARCH_BUF_SIZE)
	end
	return buf
end

local function dialog_button_label(btn, index)
	local trim = ctx.trim
	local label = trim((btn and btn.label) or "")
	if label == "" then
		label = (L("binder.input_dialog.text.number")):format(index)
	end
	return label
end

local function split_match_tokens(raw)
	local trim = ctx.trim
	raw = tostring(raw or "")
	if raw == "" then
		return {}
	end
	local out = {}
	for token in raw:gmatch("[^|,]+") do
		token = trim(token)
		if token ~= "" then
			out[#out + 1] = string.lower(token)
		end
	end
	return out
end

local function getDialogFieldIndexByKey(dialog, key, exclude_idx)
	local normalize_input_key_ref = ctx.normalize_input_key_ref
	key = normalize_input_key_ref(key)
	if key == "" then
		return nil
	end
	for idx, field in ipairs(dialog.fields or {}) do
		if idx ~= exclude_idx and normalize_input_key_ref(field.key) == key then
			return idx
		end
	end
	return nil
end

local function getDialogFieldSelectedTokens(dialog, idx)
	local trim = ctx.trim
	local field = dialog.fields and dialog.fields[idx]
	if not field or not field.buttons then
		return {}
	end

	local seen = {}
	local out = {}
	local function push_token(v)
		v = trim(v or "")
		if v == "" then
			return
		end
		v = string.lower(v)
		if not seen[v] then
			seen[v] = true
			out[#out + 1] = v
		end
	end

	local multi = dialog.list_multi_selected and dialog.list_multi_selected[idx] or nil
	if type(multi) == "table" then
		for source_idx, selected in pairs(multi) do
			if selected and field.buttons[source_idx] then
				local btn = field.buttons[source_idx]
				push_token(dialog_button_label(btn, source_idx))
				push_token(btn.text or "")
			end
		end
	end

	if field.multi_select ~= true then
		local single_idx = dialog.list_selected and dialog.list_selected[idx] or nil
		if single_idx and field.buttons[single_idx] then
			local btn = field.buttons[single_idx]
			push_token(dialog_button_label(btn, single_idx))
			push_token(btn.text or "")
		end
	end

	return out
end

local function dialog_button_matches_cascade(dialog, idx, field, btn)
	local normalize_input_key_ref = ctx.normalize_input_key_ref
	local parent_key = normalize_input_key_ref(field and field.cascade_parent_key)
	if parent_key == "" then
		return true
	end

	local parent_idx = getDialogFieldIndexByKey(dialog, parent_key, idx)
	if not parent_idx then
		return true
	end

	local trim = ctx.trim
	local allowed_raw = trim((btn and btn.when) or "")
	if allowed_raw == "" then
		return true
	end

	local parent_tokens = getDialogFieldSelectedTokens(dialog, parent_idx)
	if #parent_tokens == 0 then
		return false
	end

	local allowed_tokens = split_match_tokens(allowed_raw)
	if #allowed_tokens == 0 then
		return true
	end

	local parent_set = {}
	for i = 1, #parent_tokens do
		parent_set[parent_tokens[i]] = true
	end
	for i = 1, #allowed_tokens do
		if parent_set[allowed_tokens[i]] then
			return true
		end
	end
	return false
end

local function getDialogFilteredButtons(dialog, idx, field)
	local out = {}
	for source_idx, btn in ipairs(field.buttons or {}) do
		if dialog_button_matches_cascade(dialog, idx, field, btn) then
			out[#out + 1] = {
				index = source_idx,
				button = btn,
			}
		end
	end
	return out
end

local function rebuildDialogMultiBuffer(dialog, idx, field)
	local C = ctx.C
	local trim = ctx.trim
	local normalize_multi_separator = ctx.normalize_multi_separator
	local pushToast = ctx.pushToast

	local buf = ensureDialogBuffer(dialog, idx)
	local separator = normalize_multi_separator(field and field.multi_separator)
	local selected = dialog.list_multi_selected and dialog.list_multi_selected[idx] or nil
	if type(selected) ~= "table" then
		selected = {}
		dialog.list_multi_selected[idx] = selected
	end

	local parts = {}
	local maxLen = C.INPUT_BUF_SIZE - 1
	local totalLen = 0
	local sepLen = #separator
	local truncated = false
	for source_idx, btn in ipairs(field.buttons or {}) do
		if selected[source_idx] then
			local text = trim(btn.text or "")
			if text ~= "" then
				local addLen = #text + (totalLen > 0 and sepLen or 0)
				if totalLen + addLen > maxLen then
					truncated = true
					break
				end
				parts[#parts + 1] = text
				totalLen = totalLen + addLen
			end
		end
	end
	imgui.StrCopy(buf, table.concat(parts, separator), C.INPUT_BUF_SIZE)
	if truncated then
		pushToast(L("binder.input_dialog.text.text"), "warn", 3.0)
	end
	return buf
end

local function normalizeDialogSubmitValue(value)
	value = tostring(value or "")
	value = value:gsub("\r\n", "\n")
	value = value:gsub("\r", "\n")
	value = value:gsub("[ \t]*\n+[ \t]*", " ")
	return value
end

local function collectInputDialogValues(dialog, flatten_multiline)
	local values = {}
	for i, field in ipairs(dialog.fields or {}) do
		local key = field.key
		local b = dialog.buffers and dialog.buffers[i]
		local value = b and ffi.string(b) or ""
		if flatten_multiline then
			value = normalizeDialogSubmitValue(value)
		end

		if key and key ~= "" then
			values[key] = value
			values[key:lower()] = value
			values[key:upper()] = value
		end
		values[tostring(i)] = value
	end
	return values
end

local function dialog_button_matches_query(btn, index, query)
	local trim = ctx.trim
	query = trim(query or "")
	if query == "" then
		return true
	end
	query = string.lower(query)
	local label = string.lower(dialog_button_label(btn, index))
	if label:find(query, 1, true) then
		return true
	end
	local text = string.lower(tostring((btn and btn.text) or ""))
	if text:find(query, 1, true) then
		return true
	end
	local hint = string.lower(tostring((btn and btn.hint) or ""))
	return hint:find(query, 1, true) ~= nil
end

local function preview_method_label(method)
	method = tonumber(method) or 0
	if method == 1 then return L("binder.input_dialog.text.sa_mp") end
	if method == 2 then return L("binder.input_dialog.text.text_1") end
	if method == 3 then return L("binder.input_dialog.text.text_2") end
	if method == 4 then return L("binder.input_dialog.text.text_3") end
	if method == 5 then return L("binder.input_dialog.text.text_4") end
	if method == 6 then return L("binder.input_dialog.text.text_5") end
	if method == 7 then return L("binder.input_dialog.text.text_6") end
	if method == 8 then return L("binder.input_dialog.text.sf") end
	if method == 9 then return L("binder.input_dialog.text.text_7") end
	return L("binder.input_dialog.text.text_8")
end

M.preview_method_label = preview_method_label

local function buildInputDialogPreviewLines(dialog)
	local hk = dialog and dialog.hk
	if not hk then
		return {}
	end

	local values = collectInputDialogValues(dialog, true)
	local function apply_preview_values(text)
		if not text or text == "" then
			return text
		end
		return tostring(text):gsub("{{([%w_]+)}}", function(key)
			local replacement = values[key] or values[key:lower()] or values[key:upper()]
			if replacement == nil then
				return ""
			end
			return replacement
		end)
	end
	local out = {}
	for i, msg in ipairs(hk.messages or {}) do
		local src = tostring((msg and msg.text) or "")
		local preview = apply_preview_values(src)
		out[#out + 1] = {
			index = i,
			method = tonumber(msg and msg.method) or 0,
			text = preview,
		}
	end
	return out
end

local function countCharsForUI(text)
	if u8 and u8.decode then
		local decoded = u8:decode(text or "")
		return #(decoded or "")
	end
	return #(text or "")
end

local function calcAutoHeight(dialog, text, widthForWrap)
	local style = imgui.GetStyle()
	local lineH = imgui.GetTextLineHeight()

	if not dialog._charW then
		dialog._charW = imgui.CalcTextSize("0").x
	end

	local usableW = math.max(80, (widthForWrap or 320) - style.FramePadding.x * 2 - 6)
	local charPerLine = math.max(10, math.floor(usableW / dialog._charW))

	local lines = 0
	for line in (text .. "\n"):gmatch("(.-)\n") do
		local len = countCharsForUI(line)
		local wraps = 1
		if len > 0 then
			wraps = math.max(1, math.ceil(len / charPerLine))
		end
		lines = lines + wraps
	end

	lines = math.max(3, math.min(12, lines))
	return lines * lineH + style.FramePadding.y * 2 + 10
end

local function getDialogPreferredSingleLineInputWidth(dialog)
	local style = imgui.GetStyle()
	local C = ctx.C
	if not dialog._charW then
		dialog._charW = imgui.CalcTextSize("0").x
	end
	local charsWidth = dialog._charW * 144
	local counterWidth = imgui.CalcTextSize(tostring(math.max(0, (C.INPUT_BUF_SIZE or 1) - 1))).x
	local overlayReserve = counterWidth + style.FramePadding.x * 2 + 12
	return charsWidth + overlayReserve + style.FramePadding.x * 2
end

local function getDialogListHeight(dialog, mode)
	local normalize_runtime_input_mode = ctx.normalize_runtime_input_mode
	local INPUT_MODE = ctx.INPUT_MODE
	local listFields = 0

	for _, field in ipairs(dialog.fields or {}) do
		local fieldMode = normalize_runtime_input_mode(field.mode, field.buttons)
		if fieldMode == INPUT_MODE.BUTTONS_LIST or fieldMode == INPUT_MODE.BUTTONS_LIST_TEXT then
			listFields = listFields + 1
		end
	end

	if mode == INPUT_MODE.BUTTONS_LIST then
		if listFields >= 3 then
			return 130
		end
		if listFields == 2 then
			return 160
		end
		return 220
	end

	if listFields >= 3 then
		return 96
	end
	if listFields == 2 then
		return 110
	end
	return 170
end

local function getDialogViewportSize()
	if imgui.GetMainViewport then
		local vp = imgui.GetMainViewport()
		return imgui.ImVec2(vp.Size.x, vp.Size.y)
	end

	local io = imgui.GetIO()
	return imgui.ImVec2(io.DisplaySize.x, io.DisplaySize.y)
end

local function getDialogPreferredRegularWindowHeight(dialog, hk)
	local style = imgui.GetStyle()
	local frameH = imgui.GetFrameHeight()
	local textLineH = imgui.GetTextLineHeightWithSpacing()
	local normalize_runtime_input_mode = ctx.normalize_runtime_input_mode
	local INPUT_MODE = ctx.INPUT_MODE
	local height = style.WindowPadding.y * 2 + 28

	if hk and hk.label and hk.label ~= "" then
		height = height + textLineH + style.ItemSpacing.y + 6
	end

	for _, field in ipairs(dialog.fields or {}) do
		local mode = normalize_runtime_input_mode(field.mode, field.buttons)
		local hasList = mode == INPUT_MODE.BUTTONS_LIST or mode == INPUT_MODE.BUTTONS_LIST_TEXT
		local allowsText = mode == INPUT_MODE.TEXT or mode == INPUT_MODE.BUTTONS_LIST_TEXT

		if field.label and field.label ~= "" then
			height = height + textLineH
		end
		if field.hint and field.hint ~= "" then
			height = height + textLineH + style.ItemSpacing.y
		end
		if hasList then
			height = height + frameH + style.ItemSpacing.y
			height = height + getDialogListHeight(dialog, mode) + style.ItemSpacing.y
			if mode == INPUT_MODE.BUTTONS_LIST then
				height = height + textLineH + style.ItemSpacing.y
			end
		end
		if allowsText then
			height = height + frameH + style.ItemSpacing.y * 4 + 10
		end
		height = height + style.ItemSpacing.y + 2
	end

	height = height + textLineH * 2 + frameH + style.ItemSpacing.y * 6 + 18
	return math.max(320, math.floor(height))
end

local function drawCharCountOverlay(countStr, rectMin, rectMax)
	local style = imgui.GetStyle()
	local textSize = imgui.CalcTextSize(countStr)
	local padding = style.FramePadding
	local pos = imgui.ImVec2(rectMax.x - padding.x - textSize.x, rectMin.y + padding.y)
	local col = style.Colors[imgui.Col.TextDisabled]
	imgui.GetWindowDrawList():AddText(pos, imgui.GetColorU32Vec4(col), countStr)
end

local function drawInputMultilineWithCounter(id, buf, bufSize, height)
	local style = imgui.GetStyle()
	local text = ffi.string(buf)
	local countStr = tostring(countCharsForUI(text))
	local fullW = imgui.GetContentRegionAvail().x

	imgui.PushItemWidth(fullW)
	local changed = imgui.InputText(id, buf, bufSize)
	imgui.PopItemWidth()

	local rectMin = imgui.GetItemRectMin()
	local rectMax = imgui.GetItemRectMax()

	drawCharCountOverlay(countStr, rectMin, rectMax)

	return changed
end

M.drawInputMultilineWithCounter = drawInputMultilineWithCounter
M.countCharsForUI = countCharsForUI

function M.cancelInputDialog()
	if not activeInputDialog then
		return
	end
	if activeInputDialog.hk then
		activeInputDialog.hk._awaiting_input = false
		activeInputDialog.hk._pending_chat_trigger = nil
		activeInputDialog.hk._pending_command_trigger = nil
	end
	activeInputDialog = nil
end

function M.openInputDialog(hk, delay_ms)
	local fields = ctx.normalize_runtime_inputs(hk.inputs or {})
	if #fields == 0 then
		return false
	end
	activeInputDialog = {
		hk = hk,
		delay = delay_ms,
		fields = fields,
		buffers = {},
		open = imgui.new.bool(true),
		focus_requested = true,
	}
	hk._awaiting_input = true
	return true
end

local function submitInputDialog(dialog)
	local hk = dialog and dialog.hk
	if not hk then
		return false
	end
	local values = collectInputDialogValues(dialog, true)
	local startHotkeyCoroutine = ctx.startHotkeyCoroutine
	if startHotkeyCoroutine and startHotkeyCoroutine(hk, dialog.delay, values) then
		M.cancelInputDialog()
		return true
	end
	if startHotkeyCoroutine then
		ctx.pushToast(L("binder.input_dialog.text.text_10"), "err", 3.0)
	end
	return false
end

-- === Main draw ===
function M.drawInputDialog()
	local dialog = activeInputDialog
	if not dialog then
		return
	end

	local hk = dialog.hk
	if not hk or #(dialog.fields or {}) == 0 then
		M.cancelInputDialog()
		return
	end

	local _d = ctx._d
	local fa = ctx.fa
	local C = ctx.C
	local INPUT_MODE = ctx.INPUT_MODE
	local normalize_runtime_input_mode = ctx.normalize_runtime_input_mode

	ensureDialogTables(dialog)

	if not dialog.open then
		dialog.open = imgui.new.bool(true)
		dialog.focus_requested = true
	end

	if not dialog.pos_set then
		local centerX, centerY = 0, 0
		if imgui.GetMainViewport then
			local vp = imgui.GetMainViewport()
			centerX = vp.Pos.x + vp.Size.x * 0.5
			centerY = vp.Pos.y + vp.Size.y * 0.5
		else
			local io = imgui.GetIO()
			centerX = io.DisplaySize.x * 0.5
			centerY = io.DisplaySize.y * 0.5
		end

		local pos = imgui.ImVec2(centerX, centerY)
		local pivot = imgui.ImVec2(0.5, 0.5)
		local ok = pcall(imgui.SetNextWindowPos, pos, imgui.Cond.Appearing, pivot)
		if not ok then
			imgui.SetNextWindowPos(pos, imgui.Cond.Appearing)
		end
		dialog.pos_set = true
	end

	local compactField = nil
	local compactMode = nil
	if #dialog.fields == 1 then
		compactField = dialog.fields[1]
		compactMode = normalize_runtime_input_mode(compactField.mode, compactField.buttons)
		compactField.mode = compactMode
	end
	local compactButtonsOnly = compactField and compactMode == INPUT_MODE.BUTTONS_LIST
	local compactButtonsWithText = compactField and compactMode == INPUT_MODE.BUTTONS_LIST_TEXT
	local compactPicker = compactButtonsOnly or compactButtonsWithText

	if compactPicker then
		local sendFromCompact = compactButtonsWithText or (compactButtonsOnly and compactField.multi_select == true)
		local viewportSize = getDialogViewportSize()
		local preferredWindowW = compactButtonsWithText and 520 or 430
		local maxWindowSize = imgui.ImVec2(
			math.max(320, viewportSize.x - 60),
			math.max(220, viewportSize.y - 60)
		)
		local minWindowSize = imgui.ImVec2(
			math.min(maxWindowSize.x, compactButtonsWithText and 440 or 380),
			math.min(maxWindowSize.y, sendFromCompact and 260 or 220)
		)
		local desiredWindowSize = imgui.ImVec2(
			math.min(maxWindowSize.x, math.max(minWindowSize.x, preferredWindowW)),
			math.min(maxWindowSize.y, sendFromCompact and 340 or 260)
		)
		imgui.SetNextWindowSizeConstraints(minWindowSize, maxWindowSize)
		imgui.SetNextWindowSize(desiredWindowSize, imgui.Cond.Appearing)
		imgui.PushStyleVarVec2(imgui.StyleVar.WindowMinSize, minWindowSize)
	else
		local viewportSize = getDialogViewportSize()
		local preferredWindowH = getDialogPreferredRegularWindowHeight(dialog, hk)
		local maxWindowSize = imgui.ImVec2(
			math.max(420, viewportSize.x - 40),
			math.max(260, viewportSize.y - 100)
		)
		local minWindowSize = imgui.ImVec2(
			math.min(maxWindowSize.x, 420),
			math.min(maxWindowSize.y, math.max(260, preferredWindowH))
		)
		local desiredWindowSize = imgui.ImVec2(
			math.min(maxWindowSize.x, 460),
			math.min(maxWindowSize.y, math.max(minWindowSize.y, preferredWindowH))
		)
		imgui.SetNextWindowSizeConstraints(minWindowSize, maxWindowSize)
		imgui.SetNextWindowSize(desiredWindowSize, imgui.Cond.Appearing)
		imgui.PushStyleVarVec2(imgui.StyleVar.WindowMinSize, minWindowSize)
	end

	if imgui.Begin(L("binder.input_dialog.text.binder_input"), dialog.open, imgui.WindowFlags.NoCollapse) then
		local function draw_preview_block()
			local previewLines = buildInputDialogPreviewLines(dialog)
			_d.imgui_text_wrapped_safe(L("binder.input_dialog.text.text_11"))
			if #previewLines == 0 then
				imgui.PushTextWrapPos()
				_d.imgui_text_disabled_safe(L("binder.input_dialog.text.text_12"))
				imgui.PopTextWrapPos()
				return
			end
			imgui.PushTextWrapPos()
			_d.imgui_text_disabled_safe(L("binder.input_dialog.text.key"))
			imgui.PopTextWrapPos()
			for i = 1, #previewLines do
				local line = previewLines[i]
				local txt = tostring(line.text or ""):gsub("[\r\n]+", " ")
				_d.imgui_text_wrapped_safe(string.format("%d. [%s] %s", i, preview_method_label(line.method), txt))
			end
		end

		local function draw_buttons_list_field(idx, field, buf, listHeight, showSearch, visibleEntries)
			visibleEntries = visibleEntries or {}
			local chosenHint = nil
			local picked = false
			local changed = false
			local multiSelect = field.multi_select == true
			local selectedMap = dialog.list_multi_selected[idx]
			if type(selectedMap) ~= "table" then
				selectedMap = {}
				dialog.list_multi_selected[idx] = selectedMap
			end

			local searchBuf = ensureDialogSearchBuffer(dialog, idx)
			if showSearch ~= false then
				local searchHint = L("binder.input_dialog.text.text_13")
				if fa.MAGNIFYING_GLASS and fa.MAGNIFYING_GLASS ~= "" then
					searchHint = fa.MAGNIFYING_GLASS .. L("binder.input_dialog.text.text_14")
				end
				imgui.PushItemWidth(-1)
				imgui.InputTextWithHint("##dialog_search" .. idx, searchHint, searchBuf, C.DIALOG_SEARCH_BUF_SIZE)
				imgui.PopItemWidth()
				imgui.Spacing()
			end

			local query = ffi.string(searchBuf)
			listHeight = math.max(100, listHeight or 180)

			imgui.BeginChild("dialog_buttons_list_" .. idx, imgui.ImVec2(0, listHeight), true)
			local shown = 0
			for _, entry in ipairs(visibleEntries) do
				local sourceIdx = entry.index
				local btn = entry.button
				if dialog_button_matches_query(btn, sourceIdx, query) then
					shown = shown + 1
					local selected
					if multiSelect then
						selected = selectedMap[sourceIdx] == true
					else
						selected = dialog.list_selected[idx] == sourceIdx
					end
					if selected then
						local activeColor = imgui.GetStyle().Colors[imgui.Col.ButtonHovered]
						imgui.PushStyleColor(imgui.Col.Button, activeColor)
						imgui.PushStyleColor(imgui.Col.ButtonHovered, activeColor)
					end

					local label = dialog_button_label(btn, sourceIdx)
					if selected and multiSelect then
						label = (fa.CHECK or "*") .. " " .. label
					end

					if imgui.Button(label .. "##dialog_pick_" .. idx .. "_" .. sourceIdx, imgui.ImVec2(-1, 0)) then
						picked = true
						changed = true
						chosenHint = ctx.trim((btn and btn.hint) or "")

						if multiSelect then
							dialog.list_selected[idx] = nil
							if selectedMap[sourceIdx] then
								selectedMap[sourceIdx] = nil
							else
								selectedMap[sourceIdx] = true
							end
							rebuildDialogMultiBuffer(dialog, idx, field)
						else
							dialog.list_selected[idx] = sourceIdx
							for k in pairs(selectedMap) do
								selectedMap[k] = nil
							end
							selectedMap[sourceIdx] = true
							imgui.StrCopy(buf, (btn and btn.text) or "", C.INPUT_BUF_SIZE)
						end
					end

					if selected then
						imgui.PopStyleColor(2)
					end

					if btn and btn.hint and btn.hint ~= "" and imgui.IsItemHovered() then
						_d.imgui_set_tooltip_safe(btn.hint)
					end
				end
			end

			if shown == 0 then
				imgui.TextDisabled(L("binder.input_dialog.text.text_15"))
			end
			imgui.EndChild()

			return picked, chosenHint, changed
		end

		local function draw_buttons_inline_field(idx, field, buf, showSearch, visibleEntries, maxColumns)
			visibleEntries = visibleEntries or {}
			local chosenHint = nil
			local picked = false
			local changed = false
			local multiSelect = field.multi_select == true
			local selectedMap = dialog.list_multi_selected[idx]
			if type(selectedMap) ~= "table" then
				selectedMap = {}
				dialog.list_multi_selected[idx] = selectedMap
			end

			local searchBuf = ensureDialogSearchBuffer(dialog, idx)
			if showSearch ~= false then
				local searchHint = L("binder.input_dialog.text.text_13")
				if fa.MAGNIFYING_GLASS and fa.MAGNIFYING_GLASS ~= "" then
					searchHint = fa.MAGNIFYING_GLASS .. L("binder.input_dialog.text.text_14")
				end
				imgui.PushItemWidth(-1)
				imgui.InputTextWithHint("##dialog_inline_search" .. idx, searchHint, searchBuf, C.DIALOG_SEARCH_BUF_SIZE)
				imgui.PopItemWidth()
				imgui.Spacing()
			end

			local query = ffi.string(searchBuf)
			local shownEntries = {}
			for _, entry in ipairs(visibleEntries) do
				local sourceIdx = entry.index
				local btn = entry.button
				if dialog_button_matches_query(btn, sourceIdx, query) then
					shownEntries[#shownEntries + 1] = entry
				end
			end

			if #shownEntries == 0 then
				imgui.TextDisabled(L("binder.input_dialog.text.text_15"))
				return picked, chosenHint, changed
			end

			local columns = 1
			local availW = imgui.GetContentRegionAvail().x
			if maxColumns and maxColumns > 1 and #shownEntries >= 6 and availW >= 260 then
				columns = math.min(maxColumns, 2)
			end
			local spacingX = imgui.GetStyle().ItemSpacing.x
			local buttonW = columns > 1 and math.max(110, (availW - spacingX * (columns - 1)) / columns) or -1

			for shownIdx, entry in ipairs(shownEntries) do
				local sourceIdx = entry.index
				local btn = entry.button
				local selected
				if multiSelect then
					selected = selectedMap[sourceIdx] == true
				else
					selected = dialog.list_selected[idx] == sourceIdx
				end
				if selected then
					local activeColor = imgui.GetStyle().Colors[imgui.Col.ButtonHovered]
					imgui.PushStyleColor(imgui.Col.Button, activeColor)
					imgui.PushStyleColor(imgui.Col.ButtonHovered, activeColor)
				end

				local label = dialog_button_label(btn, sourceIdx)
				if selected and multiSelect then
					label = (fa.CHECK or "*") .. " " .. label
				end

				if imgui.Button(label .. "##dialog_inline_pick_" .. idx .. "_" .. sourceIdx, imgui.ImVec2(buttonW, 0)) then
					picked = true
					changed = true
					chosenHint = ctx.trim((btn and btn.hint) or "")

					if multiSelect then
						dialog.list_selected[idx] = nil
						if selectedMap[sourceIdx] then
							selectedMap[sourceIdx] = nil
						else
							selectedMap[sourceIdx] = true
						end
						rebuildDialogMultiBuffer(dialog, idx, field)
					else
						dialog.list_selected[idx] = sourceIdx
						for k in pairs(selectedMap) do
							selectedMap[k] = nil
						end
						selectedMap[sourceIdx] = true
						imgui.StrCopy(buf, (btn and btn.text) or "", C.INPUT_BUF_SIZE)
					end
				end

				if selected then
					imgui.PopStyleColor(2)
				end

				if btn and btn.hint and btn.hint ~= "" and imgui.IsItemHovered() then
					_d.imgui_set_tooltip_safe(btn.hint)
				end

				if columns > 1 and (shownIdx % columns) ~= 0 and shownIdx < #shownEntries then
					imgui.SameLine()
				end
			end

			return picked, chosenHint, changed
		end

		if hk.label and hk.label ~= "" then
			_d.imgui_text_safe((fa.KEYBOARD or "") .. " " .. hk.label)
			imgui.Separator()
		end

		if compactPicker then
			local field = compactField
			local buf = ensureDialogBuffer(dialog, 1)
			local visibleButtons = getDialogFilteredButtons(dialog, 1, field)
			local sendFromCompact = compactButtonsWithText or (compactButtonsOnly and field.multi_select == true)
			if field.label and field.label ~= "" then
				_d.imgui_text_wrapped_safe(field.label)
				imgui.Spacing()
			end
			if field.hint and field.hint ~= "" then
				imgui.PushTextWrapPos()
				_d.imgui_text_disabled_safe(field.hint)
				imgui.PopTextWrapPos()
				imgui.Spacing()
			end

			local buttonCols = imgui.GetContentRegionAvail().x >= 560 and 2 or 1
			local picked = draw_buttons_inline_field(1, field, buf, true, visibleButtons, buttonCols)
			if compactButtonsOnly and field.multi_select ~= true and picked then
				submitInputDialog(dialog)
			end

			if compactButtonsWithText then
				local inputBg = imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
				imgui.Separator()
				imgui.Spacing()
				if dialog.focus_requested then
					imgui.SetKeyboardFocusHere()
					dialog.focus_requested = false
				end

				imgui.PushStyleColor(imgui.Col.FrameBg, inputBg)
				local changed = drawInputMultilineWithCounter(
					"##dialog_input_compact",
					buf,
					C.INPUT_BUF_SIZE,
					imgui.GetFrameHeight()
				)
				imgui.PopStyleColor()
				if changed then
					dialog.list_selected[1] = nil
					dialog.list_multi_selected[1] = {}
				end
				imgui.Spacing()
			end

			if sendFromCompact then
				draw_preview_block()
				imgui.Spacing()
				if imgui.Button((fa.PAPER_PLANE or fa.CHECK or "") .. L("binder.input_dialog.text.dialog_send_compact"), imgui.ImVec2(-1, 0)) then
					submitInputDialog(dialog)
				end
			end

			imgui.Spacing()
			local closeText = (fa.XMARK or "X") .. L("binder.input_dialog.text.text_16")
			local closeW = 130
			local availW = imgui.GetContentRegionAvail().x
			if availW > closeW then
				imgui.SetCursorPosX(imgui.GetCursorPosX() + (availW - closeW) * 0.5)
			end
			if imgui.Button(closeText .. "##dialog_close_compact", imgui.ImVec2(closeW, 0)) then
				M.cancelInputDialog()
			end
		else
			local normalize_input_key_ref = ctx.normalize_input_key_ref
			local normalize_multi_separator = ctx.normalize_multi_separator
			local normalize_input_mode = ctx.normalize_input_mode
			local input_mode_uses_buttons = ctx.input_mode_uses_buttons

			for idx, field in ipairs(dialog.fields) do
				field.mode = normalize_runtime_input_mode(field.mode, field.buttons)
				field.multi_select = field.multi_select == true
				field.multi_separator = normalize_multi_separator(field.multi_separator)
				field.cascade_parent_key = normalize_input_key_ref(field.cascade_parent_key)
				local mode = field.mode

				if field.label and field.label ~= "" then
					_d.imgui_text_wrapped_safe(field.label)
				end

				local buf = ensureDialogBuffer(dialog, idx)
				local visibleButtons = getDialogFilteredButtons(dialog, idx, field)
				local visibleSet = {}
				for _, entry in ipairs(visibleButtons) do
					visibleSet[entry.index] = true
				end

				local selectedMap = dialog.list_multi_selected[idx]
				if type(selectedMap) ~= "table" then
					selectedMap = {}
					dialog.list_multi_selected[idx] = selectedMap
				end
				for sourceIdx in pairs(selectedMap) do
					if not visibleSet[sourceIdx] then
						selectedMap[sourceIdx] = nil
					end
				end
				if dialog.list_selected[idx] and not visibleSet[dialog.list_selected[idx]] then
					dialog.list_selected[idx] = nil
				end
				if field.multi_select and (mode == INPUT_MODE.BUTTONS_LIST or mode == INPUT_MODE.BUTTONS_LIST_TEXT) then
					rebuildDialogMultiBuffer(dialog, idx, field)
				end

				local hasList = mode == INPUT_MODE.BUTTONS_LIST or mode == INPUT_MODE.BUTTONS_LIST_TEXT
				local allowsText = mode == INPUT_MODE.TEXT or mode == INPUT_MODE.BUTTONS_LIST_TEXT

				if field.hint and field.hint ~= "" then
					imgui.PushTextWrapPos()
					_d.imgui_text_disabled_safe(field.hint)
					imgui.PopTextWrapPos()
					imgui.Spacing()
				end

				if hasList then
					local listHeight = getDialogListHeight(dialog, mode)
					local _, pickedHint = draw_buttons_list_field(idx, field, buf, listHeight, true, visibleButtons)
					if mode == INPUT_MODE.BUTTONS_LIST and pickedHint and pickedHint ~= "" then
						imgui.Spacing()
						imgui.PushTextWrapPos()
						_d.imgui_text_disabled_safe(pickedHint)
						imgui.PopTextWrapPos()
					end
					imgui.Spacing()
				end

				if allowsText then
					local inputBg = imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]
					imgui.Separator()
					imgui.Spacing()
					if dialog.focus_requested then
						imgui.SetKeyboardFocusHere()
						dialog.focus_requested = false
					end

					imgui.PushStyleColor(imgui.Col.FrameBg, inputBg)
					local changed = drawInputMultilineWithCounter(
						"##dialog_input" .. idx,
						buf,
						C.INPUT_BUF_SIZE,
						imgui.GetFrameHeight()
					)
					imgui.PopStyleColor()
					if changed then
						dialog.list_selected[idx] = nil
						dialog.list_multi_selected[idx] = {}
					end
					imgui.Spacing()
				end
			end

			imgui.Separator()
			draw_preview_block()

			if imgui.Button((fa.PAPER_PLANE or fa.CHECK or "") .. L("binder.input_dialog.text.text_17")) then
				submitInputDialog(dialog)
			end

			imgui.SameLine()
			if imgui.Button((fa.XMARK or "X") .. L("binder.input_dialog.text.text_18")) then
				M.cancelInputDialog()
			end
		end
		if _d.mimgui_funcs and _d.mimgui_funcs.clampCurrentWindowToScreen then
			_d.mimgui_funcs.clampCurrentWindowToScreen(5)
		end
	end

	imgui.End()
	imgui.PopStyleVar()

	if dialog.open and not dialog.open[0] then
		M.cancelInputDialog()
	end
end

function M.deinit()
	if multiInputResizeCallbackPtr and type(multiInputResizeCallbackPtr.free) == "function" then
		pcall(multiInputResizeCallbackPtr.free, multiInputResizeCallbackPtr)
		multiInputResizeCallbackPtr = nil
	end
	currentMultiInputHK = nil
	activeInputDialog = nil
end

return M
