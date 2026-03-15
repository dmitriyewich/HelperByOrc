-- SMIHelp/constructor.lua — Конструктор объявлений: AD-объект, парсинг, курсор, колбэк
local M = {}

local ffi = require("ffi")
local imgui = require("mimgui")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local str = ffi.string
local floor = math.floor
local max = math.max
local InputTextFlags = imgui.InputTextFlags

local ctx -- устанавливается через M.init

-- ========= AD-объект (конструктор объявления) =========
local AD = {
	type = nil,
	object = nil,
	object_full = nil,
	object_value = nil,
	price_label = nil,
	value = nil,
	currency = nil,
	addon = nil,
}
function AD:reset()
	self.type = nil
	self.object = nil
	self.object_full = nil
	self.object_value = nil
	self.price_label = nil
	self.value = nil
	self.currency = nil
	self.addon = nil
end

M.AD = AD

local function ad_build()
	local function build_with_obj(obj_text)
		local s = AD.type or ""
		if AD.object then
			s = s .. " " .. obj_text
			s = s .. ' "' .. (AD.object_value or "") .. '"'
			if AD.addon then
				s = s .. " " .. AD.addon
			end
			s = s .. "."
		end
		if AD.price_label then
			s = s .. " " .. AD.price_label
			if AD.value or AD.currency then
				s = s .. " " .. (AD.value or "")
				if AD.currency then
					s = s .. " " .. AD.currency
				end
			end
		end
		s = s:gsub("  +", " ")
		return ctx.trim(s)
	end

	-- Если режим «авто» и есть полная версия — пробуем её, откатываемся на сокращение при превышении лимита
	if ctx.Config.data.objects_use_full and AD.object and AD.object_full and AD.object_full ~= AD.object then
		local full_result = build_with_obj(AD.object_full)
		if ctx.utf8_len(full_result) <= ctx.INPUT_MAX then
			return full_result
		end
	end
	return build_with_obj(AD.object or "")
end
M.ad_build = ad_build

local function refresh_object_value_from_editbuf()
	local txt = str(ctx.State.edit_buf)
	local q = txt:match('%b""')
	if q then
		AD.object_value = q:sub(2, -2)
	end
end
M.refresh_object_value_from_editbuf = refresh_object_value_from_editbuf

local function ad_commit_to_editbuf()
	refresh_object_value_from_editbuf()
	local built = ad_build()
	built = ctx.clamp80(built)
	imgui.StrCopy(ctx.State.edit_buf, built)
end
M.ad_commit_to_editbuf = ad_commit_to_editbuf

-- ========= ПОМОЩНИКИ ПО ТЕКСТУ/ПОИСКУ =========
local function merge_price_lists(list_a, list_b)
	local merged = {}
	local seen = {}
	local function push(list)
		for i = 1, #list do
			local item = list[i]
			if item and not seen[item] then
				merged[#merged + 1] = item
				seen[item] = true
			end
		end
	end
	if list_a then
		push(list_a)
	end
	if list_b then
		push(list_b)
	end
	return merged
end
M.merge_price_lists = merge_price_lists

local function apply_autocorrect_local(text)
	local ac = ctx.Config.data.autocorrect
	if type(ac) ~= "table" then
		return text
	end
	local has_match = false
	for _, pair in ipairs(ac) do
		local find = pair[1]
		if find and text:find(find, 1, true) then
			has_match = true
			break
		end
	end
	if not has_match then
		return text
	end
	for _, pair in ipairs(ac) do
		local find, repl = pair[1], pair[2]
		if find and repl then
			text = text:gsub(find, repl)
		end
	end
	return text
end
M.apply_autocorrect_local = apply_autocorrect_local

local function get_price_buttons_for_type(ad_type)
	local mode = (ctx.Config.data.price_type_map or {})[ad_type]
	if mode == "buy" then
		return ctx.Config.data.prices_buy or {}
	elseif mode == "sell" then
		return ctx.Config.data.prices_sell or {}
	end
	local prices = ctx.Config.data.prices
	if type(prices) ~= "table" then
		prices = merge_price_lists(ctx.Config.data.prices_buy or {}, ctx.Config.data.prices_sell or {})
		ctx.Config.data.prices = prices
	end
	return prices
end
M.get_price_buttons_for_type = get_price_buttons_for_type

local function price_label_in_list(label, list)
	if not label then
		return false
	end
	for _, item in ipairs(list or {}) do
		if item == label then
			return true
		end
	end
	return false
end
M.price_label_in_list = price_label_in_list

-- ========= ПАРСИНГ BODY =========
local function parse_dialog_body(body)
	local clean = body:gsub("{.-}", "")
	local nick = clean:match("Объявление от%s+([%w_]+)")
	local msg = clean:match("Сообщение:%s*(.-)\n")
	return nick or "", msg
end

local function extract_ad_text_from_dialog_colored(dialog_text)
	local clean = dialog_text:gsub("{.-}", "")
	local msg = clean:match("Сообщение:%s*(.-)$")
	return msg or ""
end

-- ========= ДИАЛОГИ (SAMP) =========
function M.onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
	local State = ctx.State
	local t = u8(title)
	local body = u8(text)
	if t:find("Редактирование") and (body:find("Объявление от") ~= nil) then
		State.show_dialog[0] = true
		State.last_dialog_id = dialogid
		State.last_dialog_title = t
		State.last_dialog_text = body

		local nick, msg = parse_dialog_body(body)
		if not msg or msg == "" then
			msg = extract_ad_text_from_dialog_colored(body)
		end
		State.sender_nick = nick or ""
		State.original_ad_text = msg or ""

		State.auto_memory_used = false
		local mem = ctx.nickmem_get_mem()
		local rec = mem[State.sender_nick]
		local incoming = ctx.trim(State.original_ad_text or "")
		if
			rec
			and rec.last_incoming
			and ctx.trim(rec.last_incoming) == incoming
			and rec.last_sent
			and rec.last_sent ~= ""
		then
			local paste = ctx.clamp80(rec.last_sent)
			imgui.StrCopy(State.edit_buf, paste)
			State.auto_memory_used = true
			State.cursor_action = "to_end"
			State.cursor_action_data = nil
			State.want_focus_input = true
			State.collapse_selection_after_focus = true
		else
			imgui.StrCopy(State.edit_buf, apply_autocorrect_local(State.original_ad_text))
			State.cursor_action = nil
		end

		ctx.history_reset_index()
		AD:reset()
		return false
	end
end

function M.OpenEditPreview(text, nick)
	local State = ctx.State
	State.show_dialog[0] = true
	State.last_dialog_id = nil
	State.last_dialog_title = "Редактирование объявления"
	State.last_dialog_text = ""
	State.sender_nick = nick or "Пример"
	State.original_ad_text = text or ""
	State.auto_memory_used = false
	State.cursor_action = nil
	imgui.StrCopy(State.edit_buf, ctx.clamp80(text or ""))
	State.want_focus_input = true
	State.collapse_selection_after_focus = true
	ctx.history_reset_index()
	AD:reset()
end

-- ========= КЭШИ ШАБЛОНОВ/КАТЕГОРИЙ =========
local Cache = { cats = nil, cats_key = "" }

local function rebuild_cats_if_needed()
	local tpls = ctx.Config.data.templates or {}
	local key = {}
	for i = 1, #tpls do
		key[#key + 1] = tostring(tpls[i].category or "Прочее")
	end
	local key_str = table.concat(key, "\n")
	if Cache.cats and Cache.cats_key == key_str then
		return
	end
	local cats_set, cats = {}, {}
	for _, t in ipairs(tpls) do
		local c = t.category or "Прочее"
		if not cats_set[c] then
			cats_set[c] = true
			table.insert(cats, c)
		end
	end
	table.sort(cats)
	table.insert(cats, 1, "Все")
	Cache.cats, Cache.cats_key = cats, key_str
end
M.rebuild_cats_if_needed = rebuild_cats_if_needed
M.Cache = Cache

-- ========= ШАБЛОНЫ: ГРУППЫ =========
local seeded = false
local function seed_once()
	if not seeded then
		math.randomseed(os.clock() * 100000 % 1 * 1e9)
		seeded = true
	end
end
M.seed_once = seed_once

local function tpl_groups(tpl)
	if type(tpl) ~= "table" then
		return {}
	end
	if type(tpl.text) == "string" then
		return { { tpl.text } }
	end
	if type(tpl.texts) == "table" then
		local flat = true
		for _, v in ipairs(tpl.texts) do
			if type(v) == "table" then
				flat = false
				break
			end
		end
		if flat then
			local group = {}
			for _, s in ipairs(tpl.texts) do
				if type(s) == "string" then
					table.insert(group, s)
				end
			end
			if #group > 0 then
				return { group }
			else
				return {}
			end
		else
			local groups = {}
			for _, g in ipairs(tpl.texts) do
				if type(g) == "table" then
					local group = {}
					for _, s in ipairs(g) do
						if type(s) == "string" then
							table.insert(group, s)
						end
					end
					if #group > 0 then
						table.insert(groups, group)
					end
				elseif type(g) == "string" then
					table.insert(groups, { g })
				end
			end
			return groups
		end
	end
	return {}
end
M.tpl_groups = tpl_groups

-- ========= КУРСОР: поиск позиций с циклическим обходом =========
local function normalize_index(pos, len)
	pos = tonumber(pos) or 0
	len = tonumber(len) or 0
	if pos < 0 then
		pos = 0
	elseif pos > len then
		pos = len
	end
	return floor(pos)
end

local function set_cursor_position(data, pos, buf_len_override)
	local len = max(0, floor(tonumber(buf_len_override or data.BufTextLen) or 0))
	local new_pos = normalize_index(pos or 0, len)
	data.CursorPos = new_pos
	data.SelectionStart = new_pos
	data.SelectionEnd = new_pos
	if buf_len_override ~= nil then
		data.BufTextLen = len
	end
	return new_pos
end

local function find_next_quote_pos_cyclic(s, from_pos0)
	local len = #s
	if len == 0 then
		return nil
	end
	local from = normalize_index(from_pos0, len) + 1
	local p = s:find('"', from + 1, true) or s:find('"', 1, true)
	return p and (p) or nil
end

local function find_first_empty_quotes_pos_cyclic(s, from_pos0)
	local len = #s
	if len == 0 then
		return nil
	end
	local from = normalize_index(from_pos0, len) + 1
	local p = s:find('""', from, true) or s:find('""', 1, true)
	return p and p or nil
end

local function find_addon_end_pos_cyclic(s, addon_text, from_pos0)
	local len = #s
	if not addon_text or addon_text == "" then
		return len - 1
	end
	local from = normalize_index(from_pos0, len) + 1
	local p = s:find(addon_text, from, true) or s:find(addon_text, 1, true)
	if not p then
		return len - 1
	end
	local q_rel = addon_text:find('""', 1, true) or addon_text:find('"', 1, true)
	if q_rel then
		return p + q_rel - 1
	end
	return p + #addon_text - 1
end

local function finalize_constructor_action(cursor_action, cursor_data)
	local State = ctx.State
	State.cursor_action = cursor_action
	State.cursor_action_data = cursor_data
	ctx.history_reset_index()
	State.want_focus_input = true
	State.collapse_selection_after_focus = true
end
M.finalize_constructor_action = finalize_constructor_action

-- ========= INPUTTEXT CALLBACK =========
local function EditBufCallback(data)
	local State = ctx.State
	local flag = data.EventFlag

	if flag == InputTextFlags.CallbackHistory then
		local up, down = 3, 4
		local H = ctx.Config.data.history or {}
		if data.EventKey == up then
			if State.hist_index == nil then
				ctx.history_reset_index()
			end
			if State.hist_index > 1 then
				State.hist_index = State.hist_index - 1
				local s = H[State.hist_index] or ""
				s = ctx.clamp80(s)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, s)
				State.last_edit_text = s
			end
			return 1
		elseif data.EventKey == down then
			if State.hist_index == nil then
				ctx.history_reset_index()
			end
			local max_pos = #H + 1
			if State.hist_index < max_pos then
				State.hist_index = State.hist_index + 1
				local s = (State.hist_index <= #H) and (H[State.hist_index] or "") or ""
				s = ctx.clamp80(s)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, s)
				State.last_edit_text = s
			else
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, "")
				State.last_edit_text = ""
			end
			return 1
		end
	end

	if flag == InputTextFlags.CallbackCharFilter then
		return 0
	end

	if flag == InputTextFlags.CallbackAlways then
		if State.collapse_selection_after_focus then
			if data.SelectionStart == 0 and data.SelectionEnd == data.BufTextLen and data.BufTextLen > 0 then
				set_cursor_position(data, data.BufTextLen)
			end
			State.collapse_selection_after_focus = false
		end

		local cur = str(data.Buf)
		if data.BufTextLen > ctx.INPUT_MAX then
			local chars = ctx.utf8_len(cur)
			if chars > ctx.INPUT_MAX then
				local truncated = ctx.utf8_truncate(cur, ctx.INPUT_MAX)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, truncated)
				set_cursor_position(data, #truncated, #truncated)
				State.last_edit_text = truncated
				return 1
			end
		end

		if State.last_edit_text ~= cur then
			local replaced = apply_autocorrect_local(cur)
			if replaced ~= cur then
				replaced = ctx.clamp80(replaced)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, replaced)
				set_cursor_position(data, #replaced, #replaced)
				State.last_edit_text = replaced
				return 1
			end
			State.last_edit_text = cur
		end

		if State.want_place_cursor or State.cursor_action ~= nil then
			if State.want_place_cursor then
				local p = find_next_quote_pos_cyclic(cur, data.CursorPos or 0)
				set_cursor_position(data, p or data.BufTextLen)
				State.want_place_cursor = false
				return 1
			end

			if State.cursor_action == "to_next_quote" then
				local nxt = find_next_quote_pos_cyclic(cur, data.CursorPos or 0)
				set_cursor_position(data, nxt or data.BufTextLen)
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == "to_end" then
				set_cursor_position(data, data.BufTextLen)
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == "to_first_empty_quotes" then
				local p = find_first_empty_quotes_pos_cyclic(cur, data.CursorPos or 0)
				set_cursor_position(data, p or data.BufTextLen)
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == "to_addon_end" then
				local pos = find_addon_end_pos_cyclic(cur, State.cursor_action_data or "", data.CursorPos or 0)
				set_cursor_position(data, pos)
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			end
		end
	end

	return 0
end

local EditBufCallbackPtr = ffi.cast("int (*)(ImGuiInputTextCallbackData* data)", EditBufCallback)
M.EditBufCallbackPtr = EditBufCallbackPtr

function M.deinit()
	if EditBufCallbackPtr and type(EditBufCallbackPtr.free) == "function" then
		pcall(EditBufCallbackPtr.free, EditBufCallbackPtr)
	end
end

function M.init(parent_ctx)
	ctx = parent_ctx
end

return M
