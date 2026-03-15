-- SMIHelp/main_frame.lua — Главное окно редактора объявлений
local M = {}

local ffi = require("ffi")
local imgui = require("mimgui")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local vk = require("vkeys")

local str = ffi.string
local sizeof = ffi.sizeof
local floor = math.floor
local min = math.min
local max = math.max

local ImVec2 = imgui.ImVec2
local ImVec4 = imgui.ImVec4
local InputTextFlags = imgui.InputTextFlags
local StyleVar = imgui.StyleVar
local Col = imgui.Col
local WindowFlags = imgui.WindowFlags
local bit_bor = require("bit").bor

local ctx -- устанавливается через M.init
local constructor -- ссылка на модуль конструктора

local pass_filter_opts = { filter_prepared = true, require_include = true }

-- ========= UI УТИЛИТЫ =========
local function LabelSeparator(text)
	local label = string.format("-- %s ", ctx.safe(text))
	local draw = imgui.GetWindowDrawList()
	local pos = imgui.GetCursorScreenPos()
	local avail = imgui.GetContentRegionAvail().x
	local style = imgui.GetStyle()
	local txtsz = imgui.CalcTextSize(label)
	local center_x = pos.x + avail * 0.5
	local line_y = pos.y + imgui.GetTextLineHeight() * 0.5
	local pad = style.ItemSpacing.x
	local left_x1 = pos.x
	local left_x2 = center_x - txtsz.x * 0.5 - pad
	local right_x1 = center_x + txtsz.x * 0.5 + pad
	local right_x2 = pos.x + avail
	local col = imgui.GetColorU32(Col.Separator)

	if left_x2 > left_x1 then
		draw:AddLine(ImVec2(left_x1, line_y), ImVec2(left_x2, line_y), col, 1.0)
	end
	if right_x2 > right_x1 then
		draw:AddLine(ImVec2(right_x1, line_y), ImVec2(right_x2, line_y), col, 1.0)
	end

	imgui.SetCursorScreenPos(ImVec2(center_x - txtsz.x * 0.5, pos.y))
	ctx.imgui_text_safe(label)
	imgui.SetCursorScreenPos(ImVec2(pos.x, pos.y + imgui.GetTextLineHeight() + style.ItemSpacing.y))
end

local function ButtonGrid(id, items, btnH, columns, onClick)
	local spacing = imgui.GetStyle().ItemSpacing.x
	local start_x = imgui.GetCursorPosX()
	local availW = imgui.GetContentRegionAvail().x
	columns = max(1, columns or 3)

	local btnW = floor((availW - spacing * (columns - 1)))
	if columns > 1 then
		btnW = floor(btnW / columns)
	end

	local col = 0
	for i, val in ipairs(items) do
		local label = tostring(val) .. "##" .. id .. "_" .. i
		if imgui.Button(label, ImVec2(btnW, btnH)) then
			if onClick then
				onClick(val, i)
			end
		end
		col = col + 1
		if col < columns and i ~= #items then
			imgui.SameLine()
		else
			col = 0
		end
	end
	imgui.SetCursorPosX(start_x)
end

-- ========= ЧИЛДЫ/ПАНЕЛИ =========
local function DrawTemplatesPanel()
	local State = ctx.State
	constructor.rebuild_cats_if_needed()

	imgui.Text("Категория:")
	imgui.SameLine()
	local cur_label = State.selected_category or "Все"
	if imgui.BeginCombo("##cat_combo", cur_label) then
		for _, cat in ipairs(constructor.Cache.cats or { "Все" }) do
			local sel = (State.selected_category == cat)
			if imgui.Selectable(cat, sel) then
				State.selected_category = cat
			end
		end
		imgui.EndCombo()
	end

	imgui.Spacing()
	LabelSeparator("Шаблоны")

	imgui.BeginChild("templates_list", ImVec2(0, 0), true)
	local filter_lower = ctx.tolower_utf8(str(State.filter_buf))
	local has_filter = filter_lower ~= ""

	for _, tpl in ipairs(ctx.Config.data.templates or {}) do
		local cat = tpl.category or "Прочее"
		if State.selected_category == "Все" or State.selected_category == cat then
			for _, group in ipairs(constructor.tpl_groups(tpl)) do
				local display = group[1] or ""
				local line = ((cat ~= "" and (cat .. ": ") or "") .. (display or ""))

				local show = true
				if has_filter then
					local combined = table.concat(group, " ")
					local filter_line = ((cat ~= "" and (cat .. ": ") or "") .. combined)
					show = ctx.funcs.passFilter(filter_line, filter_lower, pass_filter_opts)
				end

				if show then
					if imgui.Selectable(line, false) then
						constructor.seed_once()
						local pick = group[math.random(1, #group)] or ""
						pick = ctx.clamp80(pick)
						imgui.StrCopy(State.edit_buf, pick)
						State.cursor_action = "to_first_empty_quotes"
						State.cursor_action_data = nil
						State.want_place_cursor = false
						ctx.history_reset_index()
					end
					if imgui.IsItemClicked(1) then
						imgui.SetClipboardText(line)
					end
					if imgui.IsItemHovered() then
						ctx.imgui_set_tooltip_safe(line)
					end
				end
			end
		end
	end

	imgui.EndChild()
end

local function DrawHistoryPanel()
	local State = ctx.State
	LabelSeparator("История")
	imgui.BeginChild("history_list", ImVec2(0, 0), true)
	local filter_lower = ctx.tolower_utf8(str(State.filter_buf))
	for _, v in ipairs(ctx.Config.data.history or {}) do
		if ctx.funcs.passFilter(v, filter_lower, pass_filter_opts) then
			if imgui.Selectable(v, false) then
				local txt = ctx.clamp80(v)
				imgui.StrCopy(State.edit_buf, txt)
				State.cursor_action = "to_end"
				State.cursor_action_data = nil
				ctx.history_reset_index()
			end
			if imgui.IsItemClicked(1) then
				imgui.SetClipboardText(v)
			end
			if imgui.IsItemHovered() then
				ctx.imgui_set_tooltip_safe(v)
			end
		end
	end
	imgui.EndChild()
end

-- ========= CHAR LIMIT BAR =========
local function DrawCharLimitBar(current_chars, max_chars)
	local percent = 0.0
	if max_chars > 0 then
		percent = min(1.0, current_chars / max_chars)
	end
	if percent >= ctx.LIMIT_WARN_RATIO then
		imgui.PushStyleColor(Col.PlotHistogram, ImVec4(1, 0.3, 0.3, 1))
	end
	imgui.ProgressBar(percent, ImVec2(-1, 8), "")
	if percent >= ctx.LIMIT_WARN_RATIO then
		imgui.PopStyleColor()
	end
end

-- ========= СБРОС UI-СОСТОЯНИЯ =========
local function reset_ui_state()
	ctx.State.selected_category = "Все"
	imgui.StrCopy(ctx.State.filter_buf, "")
end
M.reset_ui_state = reset_ui_state

-- ========= Таймеры отправки =========
local function vip_timer_remaining()
	local SMIHelp = ctx.SMIHelp
	if not SMIHelp.timer_send_enabled or SMIHelp.timer_send then
		return 0
	end
	local elapsed = os.clock() - (SMIHelp.timer_send_clock or 0)
	local rem = (SMIHelp.timer_send_delay or 0) - elapsed
	if rem <= 0 then
		SMIHelp.timer_send = true
		rem = 0
	end
	return rem
end

local function btn_timer_remaining()
	local SMIHelp = ctx.SMIHelp
	if not SMIHelp.btn_timer_enabled then
		SMIHelp.btn_timer = true
		return 0
	end
	local elapsed = os.clock() - (SMIHelp.btn_timer_clock or 0)
	local rem = (SMIHelp.btn_timer_delay or 0) - elapsed
	if rem <= 0 then
		SMIHelp.btn_timer = true
		rem = 0
	else
		SMIHelp.btn_timer = false
	end
	return rem
end

-- ========= Центрированный ввод =========
local function DrawCenteredEditInput()
	local State = ctx.State
	local style = imgui.GetStyle()
	local availX = imgui.GetContentRegionAvail().x
	if ctx.bigFont then
		imgui.PushFont(ctx.bigFont)
	end
	local char_w = imgui.CalcTextSize("W").x
	local targetW = floor(char_w * 85 + style.FramePadding.x * 2)
	local toggleIcon = ctx.right_panel_collapsed[0] and "<" or ">"
	local toggleH = imgui.GetFrameHeight()
	local toggleW = toggleH
	local spacing = style.ItemSpacing.x
	local inputW = min(targetW, max(0, availX - toggleW - spacing))
	local totalW = inputW + spacing + toggleW

	local x0 = imgui.GetCursorPosX()
	imgui.SetCursorPosX(x0 + max(0, (availX - totalW) / 2))

	imgui.PushItemWidth(inputW)

	local focus_requested = State.want_focus_input
	if focus_requested and not imgui.IsMouseDown(0) then
		imgui.SetKeyboardFocusHere(0)
		State.want_focus_input = false
		State.collapse_selection_after_focus = true
	end

	local flags = bit_bor(InputTextFlags.CallbackHistory, InputTextFlags.CallbackAlways, InputTextFlags.CallbackCharFilter)
	local edit_buf = State.edit_buf
	imgui.InputText("##editad_center", edit_buf, sizeof(edit_buf), flags, constructor.EditBufCallbackPtr)

	if focus_requested and State.want_focus_input and imgui.IsItemActive() then
		State.want_focus_input = false
		State.collapse_selection_after_focus = false
	end

	imgui.PopItemWidth()

	imgui.SameLine(0, spacing)
	if imgui.Button(toggleIcon .. "##right_panel_toggle", ImVec2(toggleW, toggleH)) then
		ctx.right_panel_collapsed[0] = not ctx.right_panel_collapsed[0]
	end
	if ctx.bigFont then
		imgui.PopFont()
	end
end

local function DrawCenteredFilter()
	local State = ctx.State
	local style = imgui.GetStyle()
	local availX = imgui.GetContentRegionAvail().x
	local inputW = floor(availX * 0.80)
	local clearW = 70
	local show_clear = str(State.filter_buf) ~= ""
	if show_clear then
		inputW = min(inputW, availX - (style.ItemSpacing.x + clearW))
	end
	inputW = max(0, inputW)
	local totalW = inputW + (show_clear and (style.ItemSpacing.x + clearW) or 0)

	local x0 = imgui.GetCursorPosX()
	imgui.SetCursorPosX(x0 + max(0, (availX - totalW) / 2))

	imgui.PushItemWidth(inputW)
	local _ = imgui.InputText("##filter", State.filter_buf, sizeof(State.filter_buf))
	imgui.PopItemWidth()

	if show_clear then
		imgui.SameLine()
		if imgui.Button("Clear", ImVec2(clearW, 0)) then
			imgui.StrCopy(State.filter_buf, "")
		end
	end
end

-- ========= Блок «От кого и что прислано» =========
local function DrawMetaPanel()
	local State = ctx.State
	imgui.BeginChild("meta_panel", ImVec2(0, 92), true)
	imgui.Text("Отправитель:")
	imgui.SameLine()
	ctx.imgui_text_colored_safe(ImVec4(0.8, 1.0, 0.8, 1), State.sender_nick ~= "" and State.sender_nick or "-")
	imgui.SameLine()
	if imgui.SmallButton("Скопировать ник") then
		imgui.SetClipboardText(State.sender_nick or "")
	end
	if State.auto_memory_used then
		imgui.SameLine()
		imgui.TextColored(ImVec4(0.4, 0.95, 0.4, 1), "[Автовставка из памяти]")
	end

	imgui.Text("Исходное сообщение:")
	local startX = imgui.GetCursorPosX()
	imgui.SetCursorPosX(startX + 4)
	imgui.BeginChild("orig_box", ImVec2(0, 25), true)
	ctx.imgui_text_wrapped_safe(State.original_ad_text ~= "" and State.original_ad_text or "-")
	imgui.EndChild()
	if imgui.SmallButton("Скопировать исходник") then
		imgui.SetClipboardText(State.original_ad_text or "")
	end
	imgui.EndChild()
end

-- ========= ОСНОВНОЙ FRAME =========
function M.createOnFrame()
	local was_dialog_open = false

	return imgui.OnFrame(function()
		return ctx.State.show_dialog[0]
	end, function()
		local State = ctx.State
		local SMIHelp = ctx.SMIHelp
		local AD = constructor.AD

		imgui.PushStyleVarFloat(StyleVar.FrameRounding, 6.0)
		imgui.PushStyleVarFloat(StyleVar.GrabRounding, 6.0)
		imgui.PushStyleVarFloat(StyleVar.WindowRounding, 6.0)
		imgui.PushStyleVarFloat(StyleVar.ScrollbarRounding, 6.0)

		local style = imgui.GetStyle()
		local item_spacing_x = style.ItemSpacing.x

		if not State.compact_applied then
			State.win_size = ImVec2(floor(State.win_size.x * (1 - ctx.SIDE_PANEL_RATIO)), State.win_size.y)
			State.compact_applied = true
		end

		if ctx.mimgui_funcs and ctx.mimgui_funcs.clampWindowToScreen then
			State.win_pos, State.win_size = ctx.mimgui_funcs.clampWindowToScreen(State.win_pos, State.win_size, 5)
		end
		do
			local io = imgui.GetIO()
			if not was_dialog_open and io and io.DisplaySize and io.DisplaySize.x > 0 and io.DisplaySize.y > 0 then
				local cx = max(0, floor((io.DisplaySize.x - State.win_size.x) * 0.5))
				local cy = max(0, floor((io.DisplaySize.y - State.win_size.y) * 0.5))
				State.win_pos = ImVec2(cx, cy)
			end
		end
		imgui.SetNextWindowPos(State.win_pos, imgui.Cond.Always)
		imgui.SetNextWindowSize(State.win_size, imgui.Cond.Always)
		local opened = imgui.Begin("СМИ Хелпер", State.show_dialog, WindowFlags.NoCollapse)
		State.win_pos = imgui.GetWindowPos()
		State.win_size = imgui.GetWindowSize()

		if not State.show_dialog[0] then
			reset_ui_state()
			was_dialog_open = false
		else
			was_dialog_open = true
		end

		ctx.imgui_text_colored_safe(
			ImVec4(1, 0.95, 0.2, 1),
			(
				State.last_dialog_title ~= "" and State.last_dialog_title
				or "Редактирование объявления"
			)
		)
		imgui.Separator()
		DrawMetaPanel()
		imgui.Separator()

		local availX = imgui.GetContentRegionAvail().x
		local availY = imgui.GetContentRegionAvail().y
		local rightW_expanded = floor(availX * ctx.SIDE_PANEL_RATIO * ctx.RIGHT_PANEL_WIDTH_MULT) + style.WindowPadding.x
		local right_visible = not ctx.right_panel_collapsed[0]
		local rightW = right_visible and rightW_expanded or ctx.RIGHT_PANEL_W_COLLAPSED
		local midW = availX - (right_visible and (rightW + item_spacing_x) or 0)
		if midW < 0 then
			midW = 0
		end

		-- CENTER
		imgui.BeginGroup()
		imgui.BeginChild("center", ImVec2(midW, availY), true)

		DrawCenteredEditInput()
		imgui.Spacing()

		imgui.BeginChild("##centered_input_zone", ImVec2(0, 60), true)
		if ctx.bigFont then
			imgui.PushFont(ctx.bigFont)
		end
		imgui.PushItemWidth(-1)
		local edit_buf = State.edit_buf
		local edit_buf_text = str(edit_buf)
		local buf_changed = false

		imgui.Spacing()
		if imgui.SmallButton("Копировать текст") then
			imgui.SetClipboardText(edit_buf_text)
		end
		imgui.SameLine()
		if imgui.SmallButton("Автокоррекция") then
			local handler = ctx.correct_module and ctx.correct_module.handleAuto
			if type(handler) == "function" then
				handler(u8:decode(edit_buf_text), function(newText)
					imgui.StrCopy(edit_buf, u8(newText))
				end)
				buf_changed = true
			end
		end
		imgui.SameLine()
		if imgui.SmallButton("К следующей кавычке") then
			State.cursor_action = "to_next_quote"
			State.cursor_action_data = nil
			State.want_focus_input = true
			State.collapse_selection_after_focus = true
		end
		imgui.SameLine()
		if imgui.SmallButton("Курсор в конец") then
			State.cursor_action = "to_end"
			State.cursor_action_data = nil
			State.want_focus_input = true
			State.collapse_selection_after_focus = true
		end

		local edit_buf_text_after = (buf_changed and str(edit_buf)) or edit_buf_text
		local char_count = ctx.utf8_len(edit_buf_text_after)
		imgui.Spacing()
		DrawCharLimitBar(char_count, ctx.INPUT_MAX)

		imgui.PopItemWidth()
		if ctx.bigFont then
			imgui.PopFont()
		end

		imgui.EndChild()

		-- Кнопки действий + таймер блокировки и сохранение памяти по нику
		do
			local vip_rem = 0
			if not SMIHelp.timer_send then
				vip_rem = vip_timer_remaining()
			end
			local btn_rem = 0
			if SMIHelp.btn_timer_enabled and not SMIHelp.btn_timer then
				btn_rem = btn_timer_remaining()
			end
			local can_send = SMIHelp.timer_send and (not SMIHelp.btn_timer_enabled or SMIHelp.btn_timer)
			local avail = imgui.GetContentRegionAvail().x
			local btnW = floor((avail - item_spacing_x) / 2)
			local enter_pressed = wasKeyPressed(vk.VK_RETURN) or wasKeyPressed(vk.VK_NUMPADENTER)
			local btn_send_clicked = imgui.Button("Отправить", ImVec2(btnW, 0)) or enter_pressed
			imgui.SameLine()
			if imgui.Button("Отклонить", ImVec2(btnW, 0)) then
				if State.last_dialog_id then
					local to_send_utf8 = str(State.edit_buf)
					local to_send_cp = u8:decode(to_send_utf8)
					sampSendDialogResponse(State.last_dialog_id, 0, 0, to_send_cp)
					State.show_dialog[0] = false
					AD:reset()
					reset_ui_state()
				end
			end
			if imgui.Button("Сбросить к оригиналу", ImVec2(btnW, 0)) then
				local orig = ctx.clamp80(State.original_ad_text or "")
				imgui.StrCopy(State.edit_buf, orig)
				AD:reset()
				ctx.history_reset_index()
				State.want_focus_input = true
				State.collapse_selection_after_focus = true
			end
			imgui.SameLine()
			imgui.Text(string.format("Симв.: %d/%d", char_count, ctx.INPUT_MAX))
			if not SMIHelp.timer_send then
				imgui.SameLine()
				imgui.TextColored(
					ImVec4(1, 0.45, 0.45, 1),
					string.format(" | Таймер VIP: %.1f c", vip_rem)
				)
			elseif SMIHelp.btn_timer_enabled and not SMIHelp.btn_timer then
				imgui.SameLine()
				imgui.TextColored(
					ImVec4(1, 0.45, 0.45, 1),
					string.format(" | Таймер отправки: %.1f c", btn_rem)
				)
			end
			if btn_send_clicked then
				if not can_send then
					-- блокируем отправку
				else
					if State.last_dialog_id then
						local to_send_utf8 = str(State.edit_buf)
						local to_send_cp = u8:decode(to_send_utf8)
						sampSendDialogResponse(State.last_dialog_id, 1, 0, to_send_cp)
						ctx.add_to_history(to_send_utf8)
						ctx.nickmem_save(State.sender_nick, State.original_ad_text, to_send_utf8)
						State.show_dialog[0] = false
						AD:reset()
						reset_ui_state()
						if SMIHelp.btn_timer_enabled then
							SMIHelp.btn_timer = false
							SMIHelp.btn_timer_clock = os.clock()
						end
					end
				end
			end
		end

		imgui.Spacing()
		LabelSeparator("Конструктор")

		-- Конструктор: вычисление ширин секций
		local c_availX = imgui.GetContentRegionAvail().x
		local spacing = imgui.GetStyle().ItemSpacing.x

		local NEED_OBJ_W3 = 3 * ctx.OBJ_BTN_MIN_W + 2 * spacing
		local NEED_KBD_W3 = 3 * ctx.KBD_BTN_MIN_W + 2 * spacing

		local typeW = ctx.TYPEW_BASE
		local priceW = ctx.PRICEW_BASE
		local remain = c_availX - typeW - priceW - spacing * 3 + (ctx.OBJ_PANEL_TRIM - ctx.PANEL_PAD * 2)

		if remain < (NEED_OBJ_W3 + NEED_KBD_W3) then
			local deficit = (NEED_OBJ_W3 + NEED_KBD_W3) - remain
			local cutType = min(deficit * 0.5, typeW - ctx.TYPEW_MIN)
			typeW = typeW - cutType
			deficit = deficit - cutType
			local cutPrice = min(deficit, priceW - ctx.PRICEW_MIN)
			priceW = priceW - cutPrice
			deficit = deficit - cutPrice
			remain = c_availX - typeW - priceW - spacing * 3 + (ctx.OBJ_PANEL_TRIM - ctx.PANEL_PAD * 2)
		end

		local objW = max(NEED_OBJ_W3, floor(remain * 0.58))
		local kbdW = max(NEED_KBD_W3, remain - objW)

		local over = objW + kbdW - remain
		if over > 0 then
			local cutO = min(over * 0.5, objW - NEED_OBJ_W3)
			objW = objW - cutO
			over = over - cutO
			local cutK = min(over, kbdW - NEED_KBD_W3)
			kbdW = kbdW - cutK
		end

		local type_btns = ctx.Config.data.type_buttons
		local obj_btns = ctx.Config.data.objects
		local price_btns = constructor.get_price_buttons_for_type(AD.type)
		local numpad = ctx.NUMPAD
		local currencies = ctx.Config.data.currencies
		local addons = ctx.Config.data.addons

		-- Тип
		imgui.BeginChild("##type", ImVec2(typeW + ctx.PANEL_PAD, ctx.SECTION_H), true)
		ButtonGrid("type", type_btns, ctx.BTN_H, 1, function(val)
			local prev_price = AD.price_label
			local next_price_btns = constructor.get_price_buttons_for_type(val)
			AD:reset()
			AD.type = val
			if constructor.price_label_in_list(prev_price, next_price_btns) then
				AD.price_label = prev_price
			end
			constructor.ad_commit_to_editbuf()
			constructor.finalize_constructor_action(nil, nil)
		end)
		imgui.EndChild()
		imgui.SameLine()

		-- Объект (3 колонки)
		imgui.BeginChild("##object", ImVec2(objW - ctx.OBJ_PANEL_TRIM, ctx.SECTION_H), true)
		do
			local _sp = imgui.GetStyle().ItemSpacing.x
			local _sx = imgui.GetCursorPosX()
			local _aw = imgui.GetContentRegionAvail().x
			local _cols = 3
			local _bw = floor((_aw - _sp * (_cols - 1)) / _cols)
			local _col = 0
			for _i, _pair in ipairs(obj_btns) do
				local _short = type(_pair) == "table" and tostring(_pair[1] or "") or tostring(_pair)
				local _full = type(_pair) == "table" and tostring(_pair[2] or _short) or _short
				if imgui.Button(_short .. "##object_" .. _i, ImVec2(_bw, ctx.BTN_H)) then
					constructor.refresh_object_value_from_editbuf()
					AD.object = _short
					AD.object_full = _full
					AD.addon = nil
					constructor.ad_commit_to_editbuf()
					local _txt = str(State.edit_buf)
					if not _txt:find('%b""') then
						_txt = ctx.clamp80(_txt .. ' ""')
						imgui.StrCopy(State.edit_buf, _txt)
					end
					State.want_place_cursor = true
					constructor.finalize_constructor_action(nil, nil)
				end
				if _full ~= _short and imgui.IsItemHovered() then
					ctx.imgui_set_tooltip_safe(_full)
				end
				_col = _col + 1
				if _col < _cols and _i ~= #obj_btns then
					imgui.SameLine()
				else
					_col = 0
				end
			end
			imgui.SetCursorPosX(_sx)
		end
		imgui.EndChild()
		imgui.SameLine()

		-- Цена
		imgui.BeginChild("##price", ImVec2(priceW + ctx.PANEL_PAD, ctx.SECTION_H), true)
		ButtonGrid("price", price_btns, ctx.BTN_H, 1, function(val)
			constructor.refresh_object_value_from_editbuf()
			AD.price_label = val
			constructor.ad_commit_to_editbuf()
			constructor.finalize_constructor_action(nil, nil)
		end)
		imgui.EndChild()
		imgui.SameLine()

		-- Numpad + валюта + дополнения (3 колонки)
		imgui.BeginChild("##kbd", ImVec2(kbdW, ctx.SECTION_H), true)
		ButtonGrid("numpad", numpad, ctx.BTN_H, 3, function(key)
			constructor.refresh_object_value_from_editbuf()
			AD.value = (AD.value or "") .. key
			constructor.ad_commit_to_editbuf()
			constructor.finalize_constructor_action(nil, nil)
		end)

		imgui.Spacing()
		imgui.Separator()
		imgui.Spacing()
		imgui.BeginChild("##currency_addon", ImVec2(0, 0), false, 0)
		if imgui.BeginTabBar("##currency_addon_tabs") then
			if imgui.BeginTabItem("Валюта") then
				for _, item in ipairs(currencies) do
					local sel = (AD.currency == item)
					if imgui.Selectable(item, sel) then
						constructor.refresh_object_value_from_editbuf()
						AD.currency = item
						constructor.ad_commit_to_editbuf()
						constructor.finalize_constructor_action("to_end", nil)
					end
				end
				imgui.EndTabItem()
			end
			if imgui.BeginTabItem("Дополнения") then
				for _, item in ipairs(addons) do
					local sel = (AD.addon == item)
					if imgui.Selectable(item, sel) then
						constructor.refresh_object_value_from_editbuf()
						AD.addon = item
						constructor.ad_commit_to_editbuf()
						constructor.finalize_constructor_action("to_addon_end", item)
					end
				end
				imgui.EndTabItem()
			end
			imgui.EndTabBar()
		end
		imgui.EndChild()
		imgui.EndChild()

		imgui.EndChild()
		imgui.EndGroup()

		if right_visible then
			imgui.SameLine()

			-- RIGHT
			imgui.BeginGroup()
			imgui.BeginChild("right_panel", ImVec2(rightW, availY), true)
			DrawCenteredFilter()
			if imgui.BeginTabBar("##right_tabs") then
				if imgui.BeginTabItem("Шаблоны") then
					DrawTemplatesPanel()
					imgui.EndTabItem()
				end
				if imgui.BeginTabItem("История") then
					DrawHistoryPanel()
					imgui.EndTabItem()
				end
				imgui.EndTabBar()
			end
			imgui.EndChild()
			imgui.EndGroup()
		end

		imgui.End()
		imgui.PopStyleVar(4)
	end)
end

function M.init(parent_ctx)
	ctx = parent_ctx
	constructor = ctx.constructor
end

return M
