local module = {}

-- ===================== ЗАВИСИМОСТИ =====================
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local imgui = require("mimgui")
local ffi = require("ffi")
local mimgui_funcs = require("HelperByOrc.mimgui_funcs")

local ok_fa, fa = pcall(require, "HelperByOrc.fAwesome7")
if not ok_fa or type(fa) ~= "table" then
	fa = setmetatable({}, {
		__index = function()
			return ""
		end,
	})
end

local ok_bit, bit = pcall(require, "bit")
local ok_bit32, bit32 = pcall(require, "bit32")

local function bor(...)
	local n = select("#", ...)
	if n == 0 then
		return 0
	end
	local v = select(1, ...)
	for i = 2, n do
		local a = select(i, ...)
		if ok_bit and bit and bit.bor then
			v = bit.bor(v, a)
		elseif ok_bit32 and bit32 and bit32.bor then
			v = bit32.bor(v, a)
		else
			v = v + a
		end
	end
	return v
end

local funcs
local deepcopy = function(t)
	return t
end

local function clone_table(tbl)
	if type(tbl) ~= "table" then
		return tbl
	end
	local res = {}
	for k, v in pairs(tbl) do
		res[k] = clone_table(v)
	end
	return res
end

local function merge_defaults(dst, defaults)
	if type(dst) ~= "table" or type(defaults) ~= "table" then
		return
	end
	for k, v in pairs(defaults) do
		if dst[k] == nil then
			dst[k] = clone_table(v)
		elseif type(v) == "table" and type(dst[k]) == "table" then
			merge_defaults(dst[k], v)
		end
	end
end

local json_path = getWorkingDirectory() .. "\\HelperByOrc\\VIPandADchat.json"
local samp

local feedPos = imgui.ImVec2(800, 500)
local feedSize = imgui.ImVec2(900, 200)

function module.attachModules(mod)
	funcs = mod.funcs
	samp = mod.samp
	if funcs and funcs.deepcopy then
		deepcopy = funcs.deepcopy
	end
	module.load()
end

-- ===================== КОНФИГ =====================
local config = {}

local default_config = {
	enabled = true,
	pos_x = 800,
	pos_y = 500,
	width = 900,
	vip_height = 7,
	ad_height = 7,
	ui_mode = "chatbox",
	vip_limit = 100,
	ad_limit = 100,
	all_limit = 200,
	highlightWords = { "Walcher_Flett", "Admin_John", "VIP_News" },
	timestamp = {
		enabled = true,
		scale = 0.5,
		align_baseline = true,
		offset_y = 0.0,
		padding = 0.0,
	},
	vip = {
		"%[VIP ADV%]",
		"%[VIP%]",
		"%[PREMIUM%]",
		"%[FOREVER%]",
		"%[SERVER%]",
		"%[ADMIN%]",
		u8:decode("%{......%}%[Семья%]"),
		u8:decode("%[Альянс%]"),
		"%[Family Car%]",
		u8:decode("%[Дальнобойщик]"),
		u8:decode("%(%( %[Дальнобойщик%]"),
	},
	table_config = { vip_text = {}, ad_text = {}, all = {} },

	bg_alpha_chat = 0.50,
	bg_alpha_idle = 0.00,
	text_alpha_chat = 1.00,
	text_alpha_idle = 0.50,

	popup = {
		auto_select_all = false, -- false = не выделять всё при фокусе
		focus_on_open = false, -- true = фокусить инпут при открытии
		min_w = 320,
		max_w = 900,
		min_lines = 3,
		max_lines = 14,
		chars_per_line = 70,
	},
	chatbox = {
		enabled = true,
		pos_x = 30,
		pos_y = 600,
		width = 520,
		height = 210,
		bg_alpha = 0.35,
		rounding = 8,
	},
}

function module.isEnabled()
	return config.enabled
end

-- ===================== УТИЛИТЫ =====================
local function strip_color_tags(str)
	return (tostring(str or ""):gsub("{[%xX]+}", ""))
end

local function clamp(v, lo, hi)
	if v < lo then
		return lo
	end
	if v > hi then
		return hi
	end
	return v
end

local function hex2rgba_vec4(hex, force_alpha)
	local r = tonumber(hex:sub(1, 2), 16) or 255
	local g = tonumber(hex:sub(3, 4), 16) or 255
	local b = tonumber(hex:sub(5, 6), 16) or 255
	local a = (#hex >= 8) and (tonumber(hex:sub(7, 8), 16) or 255) or 255
	if force_alpha ~= nil then
		a = clamp(math.floor(force_alpha * 255 + 0.5), 0, 255)
	end
	return imgui.ImVec4(r / 255, g / 255, b / 255, a / 255)
end

local function mul_alpha(col, alpha)
	return imgui.ImVec4(col.x, col.y, col.z, clamp(col.w * alpha, 0.0, 1.0))
end

local function text_size(s, font, fsize)
	return font:CalcTextSizeA(fsize, 10000, -1, s).x
end

local function is_color_tag(tag)
	return type(tag) == "string"
		and (tag:match("^%{%x%x%x%x%x%x%}$") or tag:match("^%{%x%x%x%x%x%x%x%x%}$"))
end

local function line_height()
	return imgui.GetTextLineHeightWithSpacing()
end

local function wrap_to_lines(text, max_px)
	local lines = {}
	local cleaned = tostring(text or "")
	if cleaned == "" then
		lines[1] = ""
		return lines
	end
	local words = {}
	for word in cleaned:gmatch("%S+") do
		words[#words + 1] = word
	end
	if #words == 0 then
		lines[1] = ""
		return lines
	end

	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local current = ""
	for i = 1, #words do
		local word = words[i]
		local next_line = current == "" and word or (current .. " " .. word)
		if text_size(next_line, font, fsize) <= max_px or current == "" then
			current = next_line
		else
			lines[#lines + 1] = current
			current = word
		end
	end
	if current ~= "" then
		lines[#lines + 1] = current
	end
	if #lines == 0 then
		lines[1] = ""
	end
	return lines
end

local function wrap_to_lines_keep_tags(text_with_tags, max_px)
	local lines = {}
	local cleaned = tostring(text_with_tags or "")
	if cleaned == "" then
		lines[1] = ""
		return lines
	end

	local words = {}
	local current_tag = ""
	local i = 1
	while i <= #cleaned do
		while i <= #cleaned and cleaned:sub(i, i):match("%s") do
			i = i + 1
		end
		if i > #cleaned then
			break
		end

		local raw = ""
		local visible = ""
		local last_tag = nil
		while i <= #cleaned and not cleaned:sub(i, i):match("%s") do
				if cleaned:sub(i, i) == "{" then
					local tag = cleaned:match("^%b{}", i)
					if is_color_tag(tag) then
						raw = raw .. tag
						last_tag = tag
						i = i + #tag
				else
					local ch = cleaned:sub(i, i)
					raw = raw .. ch
					visible = visible .. ch
					i = i + 1
				end
			else
				local ch = cleaned:sub(i, i)
				raw = raw .. ch
				visible = visible .. ch
				i = i + 1
			end
		end
		words[#words + 1] = { raw = raw, visible = visible, last_tag = last_tag }
	end

	if #words == 0 then
		lines[1] = ""
		return lines
	end

	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local first_word_is_ts = false
	if words[1] and words[1].visible then
		first_word_is_ts = words[1].visible:match("^%[%d%d:%d%d:%d%d%]$") ~= nil
	end
	local current_visible = ""
	local current_raw = ""
	current_tag = ""
	local is_first_line = true
	for idx = 1, #words do
		local word = words[idx]
		local word_visible = word.visible or ""
		local word_raw = word.raw or ""
		local next_visible = current_visible == "" and word_visible or (current_visible .. " " .. word_visible)

		local measure_visible = next_visible
		if is_first_line and first_word_is_ts then
			measure_visible = measure_visible:gsub("^%[%d%d:%d%d:%d%d%]%s*", "")
		end
		if text_size(measure_visible, font, fsize) <= max_px or current_visible == "" then
			if current_raw == "" then
				current_raw = (current_tag ~= "" and current_tag or "") .. word_raw
			else
				current_raw = current_raw .. " " .. word_raw
			end
			current_visible = next_visible
		else
			lines[#lines + 1] = current_raw
			current_raw = (current_tag ~= "" and current_tag or "") .. word_raw
			current_visible = word_visible
			is_first_line = false
		end

		if word.last_tag and word.last_tag ~= "" then
			current_tag = word.last_tag
		end
	end

	if current_raw ~= "" then
		lines[#lines + 1] = current_raw
	end
	if #lines == 0 then
		lines[1] = ""
	end

	local active_tag = ""
	for idx = 1, #lines do
		local line = lines[idx] or ""
		local last_tag = nil
		for tag in line:gmatch("%b{}") do
			if is_color_tag(tag) then
				last_tag = tag
			end
		end
		if active_tag ~= "" and not is_color_tag(line:match("^(%b{})")) then
			line = active_tag .. line
			lines[idx] = line
		end
		if last_tag and last_tag ~= "" then
			active_tag = last_tag
		end
	end
	return lines
end

local function get_is_chat_open()
	if samp and samp.is_chat_opened then
		local ok, v = pcall(samp.is_chat_opened)
		if ok then
			return v and true or false
		end
	end
	return false
end

local function push_style_var_vec2(idx, v)
	if imgui.PushStyleVarVec2 then
		imgui.PushStyleVarVec2(idx, v)
	else
		pcall(imgui.PushStyleVar, idx, v)
	end
end

local function item_right_clicked()
	if imgui.IsItemClicked then
		local ok, v = pcall(imgui.IsItemClicked, 1)
		if ok then
			return v
		end
	end
	if imgui.IsMouseClicked then
		local ok, v = pcall(imgui.IsMouseClicked, 1)
		if ok then
			return v
		end
	end
	return false
end

local function tooltip_text(s)
	s = tostring(s or "")
	if s == "" then
		return
	end
	if imgui.TextUnformatted then
		imgui.TextUnformatted(s)
	else
		imgui.Text(s)
	end
end

local function begin_tooltip_wrap(px)
	if imgui.PushTextWrapPos then
		imgui.PushTextWrapPos(px or 520)
	end
end

local function end_tooltip_wrap()
	if imgui.PopTextWrapPos then
		imgui.PopTextWrapPos()
	end
end

local data_rev = { all = 0, vip = 0, ad = 0 }

local function fill_buf_utf8(buf, s)
	local text = tostring(s or "")
	local max_len = ffi.sizeof(buf) - 1
	ffi.fill(buf, ffi.sizeof(buf))
	if max_len <= 0 then
		return
	end
	local copy_len = math.min(#text, max_len)
	ffi.copy(buf, text, copy_len)
end

-- ===================== ДОБАВЛЕНИЕ ТЕКСТА С РАЗМЕРОМ =====================
local add_text_with_font
do
	local function try_get(name)
		local ok, fn = pcall(function()
			return imgui.lib[name]
		end)
		if ok and fn then
			return fn
		end
	end

	local add_text_fontptr = try_get("ImDrawList_AddText_FontPtr")
	if add_text_fontptr then
		add_text_with_font = function(draw, font, font_size, pos, col, text)
			add_text_fontptr(draw, font, font_size, pos, col, text, nil, 0.0, nil)
		end
	else
		local add_text_vec2 = try_get("ImDrawList_AddText")
		if add_text_vec2 then
			add_text_with_font = function(draw, font, font_size, pos, col, text)
				if not font then
					draw:AddText(pos, col, text)
					return
				end
				local current_size = imgui.GetFontSize()
				if current_size <= 0 then
					draw:AddText(pos, col, text)
					return
				end

				local ratio = font_size / current_size
				if ratio <= 0 or math.abs(ratio - 1.0) < 0.001 then
					draw:AddText(pos, col, text)
					return
				end

				if not imgui.SetWindowFontScale then
					draw:AddText(pos, col, text)
					return
				end

				local io = imgui.GetIO()
				local window_scale = 1.0

				local ok_fontsize, base_font_size = pcall(function()
					return font.FontSize
				end)
				if not ok_fontsize or not base_font_size or base_font_size == 0 then
					base_font_size = current_size
				end

				local ok_fontscale, font_scale = pcall(function()
					return font.Scale
				end)
				if not ok_fontscale or not font_scale or font_scale == 0 then
					font_scale = 1.0
				end

				local denom = base_font_size * font_scale * (io.FontGlobalScale ~= 0 and io.FontGlobalScale or 1)
				if denom ~= 0 then
					window_scale = current_size / denom
				end
				if window_scale <= 0 then
					window_scale = 1.0
				end

				local new_window_scale = window_scale * ratio

				imgui.PushFont(font)
				imgui.SetWindowFontScale(new_window_scale)
				local ok, err = pcall(add_text_vec2, draw, pos, col, text, nil)
				imgui.SetWindowFontScale(window_scale)
				imgui.PopFont()
				if not ok then
					error(err, 0)
				end
			end
		else
			add_text_with_font = function(draw, _, _, pos, col, text)
				draw:AddText(pos, col, text)
			end
		end
	end
end

-- ===================== ПОДСВЕТКА =====================
local function draw_text_with_highlight_at(draw, start_pos, text, highlightWordsLower, rect_color, text_alpha)
	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local lh = line_height()
	local style = imgui.GetStyle()

	local timestamp_cfg = config.timestamp or default_config.timestamp or {}
	local ts_enabled = not (timestamp_cfg.enabled == false)
	local ts_scale = clamp(tonumber(timestamp_cfg.scale) or 0.5, 0.2, 1.0)
	local ts_padding = math.max(0.0, tonumber(timestamp_cfg.padding) or 0.0)
	local ts_offset_y = tonumber(timestamp_cfg.offset_y) or 0.0
	local ts_align_baseline = timestamp_cfg.align_baseline ~= false
	local ts_font_size = fsize * ts_scale
	local ts_baseline_shift = ts_align_baseline and (fsize - ts_font_size) or 0.0

	local x, y = start_pos.x, start_pos.y

	local base_text = imgui.GetStyle().Colors[ffi.C.ImGuiCol_Text]
	local default_col = mul_alpha(imgui.ImVec4(base_text.x, base_text.y, base_text.z, base_text.w), text_alpha or 1.0)
	local cur_col = default_col

	local lower = text:lower()
	local i, n = 1, #text

	while i <= n do
		local tag_s, tag_e, tag = text:find("{([%xX]+)}", i)
		if tag_s == i and (tag and (#tag == 6 or #tag == 8)) then
			cur_col = mul_alpha(hex2rgba_vec4(tag), text_alpha or 1.0)
			i = tag_e + 1
		elseif tag_s == i then
			local ch = text:sub(i, i)
			draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(cur_col), ch)
			x = x + text_size(ch, font, fsize)
			i = i + 1
		else
			local timestamp = ts_enabled and text:sub(i):match("^%[%d%d:%d%d:%d%d%]")
			if timestamp and ts_font_size > 0 then
				local draw_pos = imgui.ImVec2(x, y + ts_baseline_shift + ts_offset_y)
				local col_u32 = imgui.ColorConvertFloat4ToU32(cur_col)
				add_text_with_font(draw, font, ts_font_size, draw_pos, col_u32, timestamp)
				x = x + text_size(timestamp, font, ts_font_size) + ts_padding
				i = i + #timestamp
				goto continue
			end

			local next_tag_s = text:find("{[%xX]+}", i)

			local hit_s, hit_e
			for _, w in ipairs(highlightWordsLower) do
				local s, e = lower:find(w, i, true)
				if s and (not hit_s or s < hit_s) then
					hit_s, hit_e = s, e
				end
			end

			if hit_s and (not next_tag_s or hit_s < next_tag_s) then
				if hit_s > i then
					local part = text:sub(i, hit_s - 1)
					draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(cur_col), part)
					x = x + text_size(part, font, fsize)
				end

				local wtxt = text:sub(hit_s, hit_e)
				local ww = text_size(wtxt, font, fsize)

				draw:AddRectFilled(
					imgui.ImVec2(x, y),
					imgui.ImVec2(x + ww, y + lh),
					imgui.GetColorU32Vec4(rect_color),
					style.FrameRounding
				)

				local white = mul_alpha(imgui.ImVec4(1, 1, 1, 1), text_alpha or 1.0)
				draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(white), wtxt)

				x = x + ww
				i = hit_e + 1
			else
				local next_pos = next_tag_s or (n + 1)
				local part = text:sub(i, next_pos - 1)
				draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(cur_col), part)
				x = x + text_size(part, font, fsize)
				i = next_pos
			end
		end
		::continue::
	end

	return lh
end

-- ===================== POPUP STATE =====================
local select_states = {}

local function get_select_state(key)
	local st = select_states[key]
	if not st then
		st = {
			show_raw = false,
			buf = imgui.new.char[4096](),
			initialized = false,
			last_src = "",
		}
		select_states[key] = st
	end
	return st
end

local function fill_char_buffer_from_string(buf, s_cp1251)
	local us = u8(s_cp1251 or "")
	local maxlen = ffi.sizeof(buf)
	local n = math.min(#us, maxlen - 1)
	if n > 0 then
		ffi.copy(buf, us, n)
	end
	buf[n] = 0
end

local function utf8_len(s)
	local _, count = tostring(s or ""):gsub("[^\128-\193]", "")
	return count
end

local function calc_popup_input_size(text_utf8, max_w)
	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local lh = line_height()

	local cfg = config.popup or default_config.popup or {}
	local min_w = tonumber(cfg.min_w) or 320
	local max_w2 = tonumber(cfg.max_w) or 900
	local min_lines = tonumber(cfg.min_lines) or 3
	local max_lines = tonumber(cfg.max_lines) or 14
	local cpl = tonumber(cfg.chars_per_line) or 70

	local max_allowed_w = math.min(max_w2, max_w or max_w2)

	local longest_w = 0
	local lines = 0
	local s = tostring(text_utf8 or "")
	for line in (s .. "\n"):gmatch("(.-)\n") do
		lines = lines + 1
		local w = text_size(line, font, fsize)
		if w > longest_w then
			longest_w = w
		end
	end
	if lines <= 0 then
		lines = 1
	end

	local len = utf8_len(s)
	local est_lines = math.max(lines, math.ceil(len / math.max(1, cpl)))

	local w = clamp(longest_w + 40, min_w, max_allowed_w)
	local h = clamp(est_lines * lh + 12, min_lines * lh + 12, max_lines * lh + 12)

	return imgui.ImVec2(w, h)
end

local popup_target = {
	key = nil,
	kind = nil,
	index = nil,
	src_cp = "",

	pending_open = false,
	pos = imgui.ImVec2(0, 0),

	size = imgui.ImVec2(0, 0),
	size_dirty = false,

	open_try_frames = 0,

	was_open_last = false,
}

local function open_line_popup(kind, index, src_cp)
	popup_target.key = kind .. tostring(index)
	popup_target.kind = kind
	popup_target.index = index
	popup_target.src_cp = src_cp or ""

	local mp = imgui.GetIO().MousePos
	popup_target.pos = imgui.ImVec2(mp.x + 8, mp.y + 8)

	popup_target.pending_open = true
	popup_target.size_dirty = true
	popup_target.open_try_frames = 0

	local st = get_select_state(popup_target.key)
	st.initialized = false
end

local function draw_line_popup(anchor_max_w)
	if popup_target.key == nil then
		return false
	end

	local popup_open_this_frame = false
	local st = get_select_state(popup_target.key)
	if not st.initialized then
		st.show_raw = false
		st.last_src = strip_color_tags(popup_target.src_cp)
		fill_char_buffer_from_string(st.buf, st.last_src)
		st.initialized = true
	end

	local max_w = anchor_max_w or 520
	if popup_target.size_dirty then
		local cur_text = ffi.string(st.buf)
		local input_sz = calc_popup_input_size(cur_text, max_w)
		popup_target.size = imgui.ImVec2(input_sz.x + 28, input_sz.y + 86)
	end

	if popup_target.pending_open then
		imgui.OpenPopup("##VIPAD_LINE_POPUP")
		imgui.SetNextWindowPos(popup_target.pos, imgui.Cond.Appearing)
		imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Appearing)
	elseif popup_target.size_dirty then
		imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Always)
	end

	if imgui.BeginPopup("##VIPAD_LINE_POPUP") then
		popup_open_this_frame = true
		popup_target.pending_open = false
		popup_target.size_dirty = false
		popup_target.open_try_frames = 0

		imgui.Text("Выдели фрагмент и Ctrl+C. Или копируй всё кнопкой.")
		imgui.Spacing()

		local label = st.show_raw and "Источник: С ТЕГАМИ"
			or "Источник: БЕЗ ТЕГОВ"
		if imgui.SmallButton(label) then
			st.show_raw = not st.show_raw
			st.last_src = st.show_raw and popup_target.src_cp or strip_color_tags(popup_target.src_cp)
			fill_char_buffer_from_string(st.buf, st.last_src)
			popup_target.size_dirty = true
		end
		imgui.SameLine()
		if imgui.SmallButton("Сбросить") then
			st.last_src = st.show_raw and popup_target.src_cp or strip_color_tags(popup_target.src_cp)
			fill_char_buffer_from_string(st.buf, st.last_src)
			popup_target.size_dirty = true
		end
		imgui.SameLine()
		if imgui.SmallButton("Копировать всё") then
			setClipboardText(u8:decode(ffi.string(st.buf)))
		end

		local cur_text = ffi.string(st.buf)
		local input_sz = calc_popup_input_size(cur_text, max_w)

		local itf = 0
		local ITF = imgui.InputTextFlags or {}
		if (config.popup and config.popup.auto_select_all) == true and ITF.AutoSelectAll ~= nil then
			itf = bor(itf, ITF.AutoSelectAll)
		end

		if (config.popup and config.popup.focus_on_open) == true then
			imgui.SetKeyboardFocusHere()
		end

		imgui.InputTextMultiline("##sel_input", st.buf, ffi.sizeof(st.buf), input_sz, itf)

		imgui.Spacing()
		if imgui.Button("Закрыть", imgui.ImVec2(120, 0)) then
			imgui.CloseCurrentPopup()
		end

		imgui.EndPopup()
	else
		if popup_target.pending_open then
			popup_target.open_try_frames = popup_target.open_try_frames + 1
			if popup_target.open_try_frames > 2 then
				popup_target.key = nil
				popup_target.pending_open = false
				popup_target.size_dirty = false
				popup_target.open_try_frames = 0
			end
		else
			popup_target.key = nil
		end
	end

	return popup_open_this_frame
end

-- ===================== ЗАГРУЗКА/СОХРАНЕНИЕ =====================
function module.load()
	config = deepcopy(default_config)
	local loaded = funcs and funcs.loadTableFromJson and funcs.loadTableFromJson(json_path) or nil
	if type(loaded) == "table" and next(loaded) then
		for k, v in pairs(loaded) do
			config[k] = v
		end
	end

	merge_defaults(config, default_config)
	config.table_config = config.table_config or { vip_text = {}, ad_text = {}, all = {} }
	config.table_config.vip_text = config.table_config.vip_text or {}
	config.table_config.ad_text = config.table_config.ad_text or {}
	config.table_config.all = config.table_config.all or {}
	config.timestamp = config.timestamp or clone_table(default_config.timestamp)
	merge_defaults(config.timestamp, default_config.timestamp)
	config.popup = config.popup or clone_table(default_config.popup)
	merge_defaults(config.popup, default_config.popup)
	config.chatbox = config.chatbox or clone_table(default_config.chatbox)
	merge_defaults(config.chatbox, default_config.chatbox)

	data_rev.vip = data_rev.vip + 1
	data_rev.ad = data_rev.ad + 1
	data_rev.all = data_rev.all + 1
end

function module.save()
	if funcs and funcs.saveTableToJson then
		funcs.saveTableToJson(config, json_path)
	end
end

-- ===================== ПУБЛИЧНОЕ API =====================
function module.AddVIPMessage(text)
	if not config.enabled then
		return
	end
	local t = config.table_config.vip_text
	t[#t + 1] = text
	local vip_limit = tonumber(config.vip_limit) or default_config.vip_limit or 100
	while #t > vip_limit do
		table.remove(t, 1)
	end
	local all = config.table_config.all
	all[#all + 1] = { kind = "vip", text = text, src_index = #t }
	local all_limit = tonumber(config.all_limit) or default_config.all_limit or 200
	while #all > all_limit do
		table.remove(all, 1)
	end
	data_rev.vip = data_rev.vip + 1
	data_rev.all = data_rev.all + 1
	module.save()
end

function module.AddADMessage(main, edited, toredact)
	if not config.enabled then
		return
	end
	local t = config.table_config.ad_text
	t[#t + 1] = { main, edited or "", toredact or "" }
	local ad_limit = tonumber(config.ad_limit) or default_config.ad_limit or 100
	while #t > ad_limit do
		table.remove(t, 1)
	end
	local all = config.table_config.all
	all[#all + 1] = { kind = "ad", text = main, edited = edited or "", toredact = toredact or "", src_index = #t }
	local all_limit = tonumber(config.all_limit) or default_config.all_limit or 200
	while #all > all_limit do
		table.remove(all, 1)
	end
	data_rev.ad = data_rev.ad + 1
	data_rev.all = data_rev.all + 1
	module.save()
end

function module.SetLastADEdited(text)
	if not config.enabled then
		return
	end
	local ad = config.table_config.ad_text
	if #ad > 0 then
		ad[#ad][2] = text
		module.save()
	end
end

function module.SetLastADPreEdit(text)
	if not config.enabled then
		return
	end
	local ad = config.table_config.ad_text
	if #ad > 0 then
		ad[#ad][3] = text
		module.save()
	end
end

function module.ClearVIP()
	config.table_config.vip_text = {}
	data_rev.vip = data_rev.vip + 1
	module.save()
end

function module.ClearAD()
	config.table_config.ad_text = {}
	data_rev.ad = data_rev.ad + 1
	module.save()
end

function module.VIP()
	if not config.enabled then
		return {}
	end
	return config.vip
end

-- ===================== HUD ЛЕНТА + ПРОКРУТКА + POPUP =====================
module.showFeedWindow = imgui.new.bool(false)
local scroll = { vip = 0.0, ad = 0.0 }
local vip_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local ad_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local all_wrap_cache = { width = 0, src_count = 0, rev = 0, cfg_key = "", lines = {} }
local all_autoscroll = true
local all_last_rev = 0
local vip_autoscroll = true
local vip_last_rev = 0
local ad_autoscroll = true
local ad_last_rev = 0

local function get_canvas_flags()
	local wf = imgui.WindowFlags
	return bor(
		wf.NoTitleBar,
		wf.NoResize,
		wf.NoMove,
		wf.NoScrollbar,
		wf.NoSavedSettings,
		wf.NoBringToFrontOnFocus,
		wf.NoFocusOnAppearing
	)
end

local function get_interact_flags()
	local wf = imgui.WindowFlags
	return bor(
		wf.NoTitleBar,
		wf.NoResize,
		wf.NoMove,
		wf.NoScrollbar,
		wf.NoSavedSettings,
		wf.NoBringToFrontOnFocus,
		wf.NoFocusOnAppearing,
		wf.NoBackground
	)
end

local function draw_feed()
	if not config then
		return false, false
	end

	local is_chat_open = get_is_chat_open()

	if not is_chat_open then
		popup_target.key = nil
		popup_target.pending_open = false
		popup_target.size_dirty = false
	end

	local bg_alpha = is_chat_open and config.bg_alpha_chat or config.bg_alpha_idle
	local text_alpha = is_chat_open and config.text_alpha_chat or config.text_alpha_idle

	local io = imgui.GetIO()
	local font = imgui.GetFont()
	local fsize = imgui.GetFontSize()
	local lh = line_height()

	local highlightLower = {}
	do
		local src = config.highlightWords or {}
		for i = 1, #src do
			highlightLower[i] = tostring(src[i] or ""):lower()
		end
	end
	local rect_highlight = imgui.ImVec4(1, 1, 0, 0.38)

	-- ширина по длинным строкам
	local max_width = math.max(200, tonumber(config.width) or 900)
	do
		local vip = config.table_config.vip_text or {}
		for i = 1, #vip do
			local s = strip_color_tags(u8(vip[i]))
			local w = text_size(s, font, fsize) + 24
			if w > max_width then
				max_width = w
			end
		end
		local ad = config.table_config.ad_text or {}
		for i = 1, #ad do
			local s = strip_color_tags(u8(ad[i] and ad[i][1] or ""))
			local w = text_size(s, font, fsize) + 24
			if w > max_width then
				max_width = w
			end
		end
	end

	-- геометрия
	local pad_in = 8
	local pad_top = 6
	local pad_bottom = 6
	local gap = 6
	local rounding = 7

	local vip_h = math.max(1, tonumber(config.vip_height) or 7)
	local ad_h = math.max(1, tonumber(config.ad_height) or 7)

	local total_h = pad_top + vip_h * lh + gap + ad_h * lh + pad_bottom
	feedSize = imgui.ImVec2(max_width, total_h)
	feedPos = imgui.ImVec2(tonumber(config.pos_x) or 800, tonumber(config.pos_y) or 500)

	-- clamp
	if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
		feedPos, feedSize = mimgui_funcs.clampWindowToScreen(feedPos, feedSize, 5)
	else
		local sw, sh = io.DisplaySize.x, io.DisplaySize.y
		feedPos.x = clamp(feedPos.x, 0, math.max(0, sw - feedSize.x))
		feedPos.y = clamp(feedPos.y, 0, math.max(0, sh - feedSize.y))
	end

	config.pos_x = feedPos.x
	config.pos_y = feedPos.y
	config.width = feedSize.x

	-- прокрутка: только при открытом чате
	if is_chat_open then
		local wheel = io.MouseWheel or 0.0
		if wheel ~= 0.0 then
			local step = 3.0
			local vip_lines = #(config.table_config.vip_text or {})
			local ad_lines = #(config.table_config.ad_text or {})
			local vip_max = math.max(0, vip_lines - vip_h)
			local ad_max = math.max(0, ad_lines - ad_h)
			scroll.vip = clamp(scroll.vip + wheel * step, 0.0, vip_max)
			scroll.ad = clamp(scroll.ad + wheel * step, 0.0, ad_max)
		end
	else
		scroll.vip = 0.0
		scroll.ad = 0.0
	end

	-- ===================== CANVAS (рисуем, без ввода) =====================
	imgui.SetNextWindowPos(imgui.ImVec2(0, 0), imgui.Cond.Always)
	imgui.SetNextWindowSize(io.DisplaySize, imgui.Cond.Always)

	push_style_var_vec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(0, 0))
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, 0)
	imgui.PushStyleVarFloat(imgui.StyleVar.WindowBorderSize, 0)
	imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0, 0, 0, 0))

	imgui.Begin("##VIPADHUD_CANVAS", nil, bor(get_canvas_flags(), imgui.WindowFlags.NoInputs))
	local draw = imgui.GetWindowDrawList()

	if bg_alpha > 0.001 then
		local bg = imgui.ImVec4(0, 0, 0, bg_alpha)
		draw:AddRectFilled(
			feedPos,
			imgui.ImVec2(feedPos.x + feedSize.x, feedPos.y + feedSize.y),
			imgui.GetColorU32Vec4(bg),
			rounding
		)

		local br = imgui.ImVec4(0, 0, 0, clamp(bg_alpha + 0.15, 0.0, 1.0))
		draw:AddRect(
			feedPos,
			imgui.ImVec2(feedPos.x + feedSize.x, feedPos.y + feedSize.y),
			imgui.GetColorU32Vec4(br),
			rounding,
			0,
			1.0
		)
	end

	local x0 = feedPos.x + pad_in
	local y0 = feedPos.y + pad_top

	-- VIP диапазон
	local vip = config.table_config.vip_text or {}
	local vip_count = #vip
	local vip_scroll = math.floor(scroll.vip + 0.5)
	local vip_first = math.max(1, vip_count - vip_h - vip_scroll + 1)
	local vip_last = math.min(vip_count, vip_first + vip_h - 1)

	local y = y0
	for i = vip_first, vip_last do
		local text_cp = vip[i] or ""
		local text = u8(text_cp)
		draw_text_with_highlight_at(draw, imgui.ImVec2(x0, y), text, highlightLower, rect_highlight, text_alpha)
		y = y + lh
	end

	-- разделитель
	local sep_y = y0 + vip_h * lh + (gap * 0.5)
	if bg_alpha > 0.001 then
		local sep = imgui.ImVec4(1, 1, 1, clamp(bg_alpha * 0.15, 0.0, 1.0))
		draw:AddLine(
			imgui.ImVec2(feedPos.x + 6, sep_y),
			imgui.ImVec2(feedPos.x + feedSize.x - 6, sep_y),
			imgui.GetColorU32Vec4(sep),
			1.0
		)
	end

	-- AD диапазон
	local ad = config.table_config.ad_text or {}
	local ad_count = #ad
	local ad_scroll = math.floor(scroll.ad + 0.5)
	local ad_first = math.max(1, ad_count - ad_h - ad_scroll + 1)
	local ad_last = math.min(ad_count, ad_first + ad_h - 1)

	y = y0 + vip_h * lh + gap
	for i = ad_first, ad_last do
		local entry = ad[i] or {}
		local main_cp = entry[1] or ""
		local main = u8(main_cp)
		draw_text_with_highlight_at(draw, imgui.ImVec2(x0, y), main, highlightLower, rect_highlight, text_alpha)
		y = y + lh
	end

	imgui.End()
	imgui.PopStyleColor(1)
	imgui.PopStyleVar(3)

	-- ===================== INTERACT (только когда чат открыт) =====================
	local popup_open_this_frame = false

	if is_chat_open then
		imgui.SetNextWindowPos(feedPos, imgui.Cond.Always)
		imgui.SetNextWindowSize(feedSize, imgui.Cond.Always)

		imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0, 0, 0, 0))
		push_style_var_vec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(0, 0))

		if imgui.Begin("##VIPADHUD_INTERACT", nil, get_interact_flags()) then
			local row_w_limit = feedSize.x - pad_in * 2 - 10

			-- VIP hitboxes
			do
				local row = 0
				for i = vip_first, vip_last do
					row = row + 1
					local text_cp = vip[i] or ""
					local text_u8 = u8(text_cp)
					local clean = strip_color_tags(text_u8)
					local w = math.min(row_w_limit, text_size(clean, font, fsize) + 6)

					local ry = y0 + (row - 1) * lh
					imgui.SetCursorScreenPos(imgui.ImVec2(x0, ry))
					imgui.InvisibleButton("##vip_line_" .. i, imgui.ImVec2(w, lh))

					if imgui.IsItemHovered() and item_right_clicked() then
						open_line_popup("vip", i, text_cp)
					end
				end
			end

			-- AD hitboxes + TOOLTIP
			do
				local base_y = y0 + vip_h * lh + gap
				local row = 0
				for i = ad_first, ad_last do
					row = row + 1

					local entry = ad[i] or {}
					local main_cp = entry[1] or ""
					local edited_cp = entry[2] or ""
					local pre_cp = entry[3] or ""

					local main_u8 = strip_color_tags(u8(main_cp))
					local edited_u8 = strip_color_tags(u8(edited_cp))
					local pre_u8 = strip_color_tags(u8(pre_cp))

					local w = math.min(row_w_limit, text_size(main_u8, font, fsize) + 6)

					local ry = base_y + (row - 1) * lh
					imgui.SetCursorScreenPos(imgui.ImVec2(x0, ry))
					imgui.InvisibleButton("##ad_line_" .. i, imgui.ImVec2(w, lh))

					if imgui.IsItemHovered() then
						-- подсказка по наведению
						if imgui.BeginTooltip then
							imgui.BeginTooltip()
							begin_tooltip_wrap(560)

							imgui.Text("Main:")
							tooltip_text(main_u8)

							if edited_cp ~= "" then
								imgui.Separator()
								imgui.Text("Edited:")
								tooltip_text(edited_u8)
							end

							if pre_cp ~= "" then
								imgui.Separator()
								imgui.Text("ToRedact:")
								tooltip_text(pre_u8)
							end

							end_tooltip_wrap()
							imgui.EndTooltip()
						end
					end

					if imgui.IsItemHovered() and item_right_clicked() then
						open_line_popup("ad", i, main_cp)
					end
				end
			end

		popup_open_this_frame = draw_line_popup(feedSize.x)
	end

		imgui.End()
		imgui.PopStyleVar(1)
		imgui.PopStyleColor(1)
	else
		popup_target.key = nil
	end

	local allow_process = is_chat_open
		or popup_target.pending_open
		or popup_open_this_frame
		or popup_target.was_open_last
	local want_cursor = popup_target.pending_open or popup_open_this_frame

	return allow_process, want_cursor, popup_open_this_frame
end

local draw_settings_content

local function draw_chatbox_window()
	if not config then
		return false, false, false
	end

	local is_chat_open = get_is_chat_open()
	local cfg = config.chatbox or default_config.chatbox
	if not cfg or cfg.enabled == false then
		return false, false, false
	end

	local pos = imgui.ImVec2(tonumber(cfg.pos_x) or 30, tonumber(cfg.pos_y) or 600)
	local size = imgui.ImVec2(tonumber(cfg.width) or 520, tonumber(cfg.height) or 210)

	imgui.SetNextWindowPos(pos, imgui.Cond.Always)
	imgui.SetNextWindowSize(size, imgui.Cond.Always)

	local flags = bor(
		imgui.WindowFlags.NoTitleBar,
		imgui.WindowFlags.NoResize,
		imgui.WindowFlags.NoSavedSettings
	)

	local highlightLower = {}
	do
		local src = config.highlightWords or {}
		for i = 1, #src do
			highlightLower[i] = tostring(src[i] or ""):lower()
		end
	end
	local rect_highlight = imgui.ImVec4(1, 1, 0, 0.38)

	local hovered = false
	local popup_open = false
	local open_settings_popup = false
	if imgui.Begin("##VIPAD_CHATBOX", nil, flags) then
		if imgui.BeginTabBar("##VIPAD_CHATBOX_TABS") then
			if imgui.BeginTabItem("ALL") then
				local all = config.table_config.all or {}
				if imgui.BeginChild("##all_scroll", imgui.ImVec2(0, 0), false) then
					local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
					local lh = line_height()
					local ts_cfg = config.timestamp or default_config.timestamp or {}
					local cfg_key = string.format(
						"%s|%.3f|%.3f",
						tostring(ts_cfg.enabled ~= false),
						tonumber(ts_cfg.scale) or 0.5,
						tonumber(ts_cfg.padding) or 0
					)
					if all_wrap_cache.width ~= max_px
						or all_wrap_cache.rev ~= data_rev.all
						or all_wrap_cache.cfg_key ~= cfg_key
					then
						local lines = {}
						for i = 1, #all do
							local entry = all[i] or {}
							local text_cp = entry.text or ""
							local text = u8(text_cp)
							local wrapped = wrap_to_lines_keep_tags(text, max_px)
							for j = 1, #wrapped do
								lines[#lines + 1] = {
									text = wrapped[j],
									kind = entry.kind,
									src_index = entry.src_index or i,
									src_cp = text_cp,
								}
							end
						end
						all_wrap_cache.width = max_px
						all_wrap_cache.src_count = #all
						all_wrap_cache.rev = data_rev.all
						all_wrap_cache.cfg_key = cfg_key
						all_wrap_cache.lines = lines
					end

					local clipper = imgui.ImGuiListClipper(#all_wrap_cache.lines)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local line = all_wrap_cache.lines[i] or {}
							local row_w = imgui.GetContentRegionAvail().x
							local start = imgui.GetCursorScreenPos()
							local draw = imgui.GetWindowDrawList()
							imgui.InvisibleButton("##all_line_" .. i, imgui.ImVec2(row_w, lh))
							if imgui.IsItemHovered() and item_right_clicked() then
								open_line_popup(line.kind or "vip", line.src_index or i, line.src_cp or "")
							end
							imgui.SetCursorScreenPos(start)
							draw_text_with_highlight_at(
								draw,
								imgui.ImVec2(start.x, start.y),
								line.text or "",
								highlightLower,
								rect_highlight,
								1.0
							)
							imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
						end
					end

					local did_autoscroll = false
					if all_last_rev ~= data_rev.all and all_autoscroll then
						imgui.SetScrollY(1e9)
						did_autoscroll = true
					end

					local maxY = imgui.GetScrollMaxY()
					local y = imgui.GetScrollY()
					local at_bottom = did_autoscroll or (maxY <= 0) or (y >= maxY - 1)
					all_autoscroll = at_bottom
					all_last_rev = data_rev.all
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end

			if imgui.BeginTabItem("VIP") then
				local vip = config.table_config.vip_text or {}
				if imgui.BeginChild("##vip_scroll", imgui.ImVec2(0, 0), false) then
					local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
					local lh = line_height()
					local ts_cfg = config.timestamp or default_config.timestamp or {}
					local cfg_key = string.format(
						"%s|%.3f|%.3f",
						tostring(ts_cfg.enabled ~= false),
						tonumber(ts_cfg.scale) or 0.5,
						tonumber(ts_cfg.padding) or 0
					)
					if vip_wrap_cache.width ~= max_px
						or vip_wrap_cache.rev ~= data_rev.vip
						or vip_wrap_cache.cfg_key ~= cfg_key
					then
						local lines = {}
						for i = 1, #vip do
							local text_cp = vip[i] or ""
							local text = u8(text_cp)
							local wrapped = wrap_to_lines_keep_tags(text, max_px)
							for j = 1, #wrapped do
								lines[#lines + 1] = {
									text = wrapped[j],
									kind = "vip",
									src_index = i,
									src_cp = text_cp,
								}
							end
						end
						vip_wrap_cache.width = max_px
						vip_wrap_cache.src_count = #vip
						vip_wrap_cache.rev = data_rev.vip
						vip_wrap_cache.cfg_key = cfg_key
						vip_wrap_cache.lines = lines
					end

					local clipper = imgui.ImGuiListClipper(#vip_wrap_cache.lines)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local line = vip_wrap_cache.lines[i] or {}
							local row_w = imgui.GetContentRegionAvail().x
							local start = imgui.GetCursorScreenPos()
							local draw = imgui.GetWindowDrawList()
							imgui.InvisibleButton("##vip_line_" .. i, imgui.ImVec2(row_w, lh))
							if imgui.IsItemHovered() and item_right_clicked() then
								open_line_popup(line.kind or "vip", line.src_index or i, line.src_cp or "")
							end
							imgui.SetCursorScreenPos(start)
							draw_text_with_highlight_at(
								draw,
								imgui.ImVec2(start.x, start.y),
								line.text or "",
								highlightLower,
								rect_highlight,
								1.0
							)
							imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
						end
					end

					local did_autoscroll = false
					if vip_last_rev ~= data_rev.vip and vip_autoscroll then
						imgui.SetScrollY(1e9)
						did_autoscroll = true
					end

					local maxY = imgui.GetScrollMaxY()
					local y = imgui.GetScrollY()
					local at_bottom = did_autoscroll or (maxY <= 0) or (y >= maxY - 1)
					vip_autoscroll = at_bottom
					vip_last_rev = data_rev.vip
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end

			if imgui.BeginTabItem("AD") then
				local ad = config.table_config.ad_text or {}
				if imgui.BeginChild("##ad_scroll", imgui.ImVec2(0, 0), false) then
					local max_px = math.max(0, imgui.GetContentRegionAvail().x - 6)
					local lh = line_height()
					local ts_cfg = config.timestamp or default_config.timestamp or {}
					local cfg_key = string.format(
						"%s|%.3f|%.3f",
						tostring(ts_cfg.enabled ~= false),
						tonumber(ts_cfg.scale) or 0.5,
						tonumber(ts_cfg.padding) or 0
					)
					if ad_wrap_cache.width ~= max_px
						or ad_wrap_cache.rev ~= data_rev.ad
						or ad_wrap_cache.cfg_key ~= cfg_key
					then
						local lines = {}
						for i = 1, #ad do
							local entry = ad[i] or {}
							local text_cp = entry[1] or ""
							local text = u8(text_cp)
							local wrapped = wrap_to_lines_keep_tags(text, max_px)
							for j = 1, #wrapped do
								lines[#lines + 1] = {
									text = wrapped[j],
									kind = "ad",
									src_index = i,
									src_cp = text_cp,
								}
							end
						end
						ad_wrap_cache.width = max_px
						ad_wrap_cache.src_count = #ad
						ad_wrap_cache.rev = data_rev.ad
						ad_wrap_cache.cfg_key = cfg_key
						ad_wrap_cache.lines = lines
					end

					local clipper = imgui.ImGuiListClipper(#ad_wrap_cache.lines)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local line = ad_wrap_cache.lines[i] or {}
							local row_w = imgui.GetContentRegionAvail().x
							local start = imgui.GetCursorScreenPos()
							local draw = imgui.GetWindowDrawList()
							imgui.InvisibleButton("##ad_line_" .. i, imgui.ImVec2(row_w, lh))
							if imgui.IsItemHovered() and item_right_clicked() then
								open_line_popup(line.kind or "ad", line.src_index or i, line.src_cp or "")
							end
							imgui.SetCursorScreenPos(start)
							draw_text_with_highlight_at(
								draw,
								imgui.ImVec2(start.x, start.y),
								line.text or "",
								highlightLower,
								rect_highlight,
								1.0
							)
							imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + lh))
						end
					end

					local did_autoscroll = false
					if ad_last_rev ~= data_rev.ad and ad_autoscroll then
						imgui.SetScrollY(1e9)
						did_autoscroll = true
					end

					local maxY = imgui.GetScrollMaxY()
					local y = imgui.GetScrollY()
					local at_bottom = did_autoscroll or (maxY <= 0) or (y >= maxY - 1)
					ad_autoscroll = at_bottom
					ad_last_rev = data_rev.ad
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end
			local gear_label = (fa and fa.GEAR ~= "" and fa.GEAR) or "⚙"
			local gear_text_size = imgui.CalcTextSize(gear_label)
			local gear_w = gear_text_size.x + imgui.GetStyle().FramePadding.x * 2
			imgui.SameLine()
			imgui.SetCursorPosX(imgui.GetWindowContentRegionMax().x - gear_w)
			if imgui.SmallButton(gear_label .. "##vipad_settings") then
				open_settings_popup = true
			end

			imgui.EndTabBar()
		end

		if open_settings_popup then
			imgui.OpenPopup("##vipad_settings_popup")
		end

		if imgui.BeginPopup("##vipad_settings_popup") then
			draw_settings_content()
			imgui.EndPopup()
		end

		popup_open = draw_line_popup(cfg.width or 520)

		local wpos = imgui.GetWindowPos()
		local wsize = imgui.GetWindowSize()
		cfg.pos_x = wpos.x
		cfg.pos_y = wpos.y
		cfg.width = wsize.x
		cfg.height = wsize.y

	hovered = imgui.IsWindowHovered()
	end
	imgui.End()

	local allow_process = is_chat_open
		or popup_target.pending_open
		or popup_open
		or popup_target.was_open_last
	local want_cursor = popup_target.pending_open or popup_open or hovered

	return allow_process, want_cursor, popup_open
end

-- ===================== ONFRAME =====================
local hudFrame = imgui.OnFrame(function()
	return module.showFeedWindow[0] and config and config.enabled
end, function(frame)
	local chat_open = get_is_chat_open()

	imgui.Process = chat_open or popup_target.pending_open or popup_target.was_open_last

	local ui_mode = (config and (config.ui_mode or default_config.ui_mode)) or "chatbox"
	local draw_fn = ui_mode == "lines" and draw_feed or draw_chatbox_window
	local allow_process, want_cursor, popup_open = draw_fn()
	imgui.Process = allow_process

	popup_target.was_open_last = popup_open

	if frame then
		frame.HideCursor = not want_cursor
		frame.LockPlayer = false
	end
end)

-- ===================== ОКНО НАСТРОЕК (опционально) =====================
local settings_open = imgui.new.bool(false)
module.showSettingsWindow = settings_open

local highlight_words_buf = imgui.new.char[2048]("")
local highlight_words_last_serialized = ""

draw_settings_content = function()
	local en = imgui.new.bool(config.enabled and true or false)
	if imgui.Checkbox("Включить модуль", en) then
		config.enabled = en[0] and true or false
		module.save()
	end

	imgui.Separator()
	imgui.Text("Режим интерфейса")
	local ui_mode = config.ui_mode or default_config.ui_mode or "chatbox"
	if imgui.RadioButtonBool("ChatBox", ui_mode == "chatbox") then
		config.ui_mode = "chatbox"
		module.save()
	end
	imgui.SameLine()
	if imgui.RadioButtonBool("Строки", ui_mode == "lines") then
		config.ui_mode = "lines"
		module.save()
	end

	imgui.Separator()
	imgui.Text("ChatBox")
	config.chatbox = config.chatbox or clone_table(default_config.chatbox)
	local chatbox = config.chatbox
	local chatbox_enabled = imgui.new.bool(chatbox.enabled ~= false)
	if imgui.Checkbox("enabled##chatbox", chatbox_enabled) then
		chatbox.enabled = chatbox_enabled[0] and true or false
		module.save()
	end

	local chatbox_pos_x = ffi.new("int[1]", tonumber(chatbox.pos_x) or 0)
	if imgui.DragInt("pos_x##chatbox", chatbox_pos_x) then
		chatbox.pos_x = chatbox_pos_x[0]
		module.save()
	end

	local chatbox_pos_y = ffi.new("int[1]", tonumber(chatbox.pos_y) or 0)
	if imgui.DragInt("pos_y##chatbox", chatbox_pos_y) then
		chatbox.pos_y = chatbox_pos_y[0]
		module.save()
	end

	local chatbox_width = ffi.new("int[1]", tonumber(chatbox.width) or 520)
	if imgui.DragInt("width##chatbox", chatbox_width) then
		chatbox.width = clamp(chatbox_width[0], 200, 1200)
		module.save()
	end

	local chatbox_height = ffi.new("int[1]", tonumber(chatbox.height) or 210)
	if imgui.DragInt("height##chatbox", chatbox_height) then
		chatbox.height = clamp(chatbox_height[0], 120, 700)
		module.save()
	end

	local chatbox_bg_alpha = imgui.new.float(chatbox.bg_alpha or 0)
	if imgui.SliderFloat("bg_alpha##chatbox", chatbox_bg_alpha, 0, 1) then
		chatbox.bg_alpha = clamp(chatbox_bg_alpha[0], 0, 1)
		module.save()
	end

	local chatbox_rounding = ffi.new("int[1]", tonumber(chatbox.rounding) or 0)
	if imgui.SliderInt("rounding##chatbox", chatbox_rounding, 0, 20) then
		chatbox.rounding = clamp(chatbox_rounding[0], 0, 20)
		module.save()
	end

	imgui.Separator()
	imgui.Text("Лента (режим Строки)")
	local pos_x = ffi.new("int[1]", tonumber(config.pos_x) or 0)
	if imgui.DragInt("pos_x", pos_x) then
		config.pos_x = pos_x[0]
		module.save()
	end

	local pos_y = ffi.new("int[1]", tonumber(config.pos_y) or 0)
	if imgui.DragInt("pos_y", pos_y) then
		config.pos_y = pos_y[0]
		module.save()
	end

	local width = ffi.new("int[1]", tonumber(config.width) or 900)
	if imgui.DragInt("width", width) then
		config.width = clamp(width[0], 200, 2000)
		module.save()
	end

	local vip_height = ffi.new("int[1]", tonumber(config.vip_height) or 7)
	if imgui.SliderInt("vip_height", vip_height, 1, 30) then
		config.vip_height = clamp(vip_height[0], 1, 30)
		module.save()
	end

	local ad_height = ffi.new("int[1]", tonumber(config.ad_height) or 7)
	if imgui.SliderInt("ad_height", ad_height, 1, 30) then
		config.ad_height = clamp(ad_height[0], 1, 30)
		module.save()
	end

	imgui.Separator()
	imgui.Text("Прозрачность (лента)")
	local bg_alpha_chat = imgui.new.float(config.bg_alpha_chat or 0)
	if imgui.SliderFloat("bg_alpha_chat", bg_alpha_chat, 0, 1) then
		config.bg_alpha_chat = clamp(bg_alpha_chat[0], 0, 1)
		module.save()
	end

	local bg_alpha_idle = imgui.new.float(config.bg_alpha_idle or 0)
	if imgui.SliderFloat("bg_alpha_idle", bg_alpha_idle, 0, 1) then
		config.bg_alpha_idle = clamp(bg_alpha_idle[0], 0, 1)
		module.save()
	end

	local text_alpha_chat = imgui.new.float(config.text_alpha_chat or 0)
	if imgui.SliderFloat("text_alpha_chat", text_alpha_chat, 0, 1) then
		config.text_alpha_chat = clamp(text_alpha_chat[0], 0, 1)
		module.save()
	end

	local text_alpha_idle = imgui.new.float(config.text_alpha_idle or 0)
	if imgui.SliderFloat("text_alpha_idle", text_alpha_idle, 0, 1) then
		config.text_alpha_idle = clamp(text_alpha_idle[0], 0, 1)
		module.save()
	end

	imgui.Separator()
	imgui.Text("Timestamp")
	config.timestamp = config.timestamp or clone_table(default_config.timestamp)
	local timestamp = config.timestamp
	local timestamp_enabled = imgui.new.bool(timestamp.enabled ~= false)
	if imgui.Checkbox("Показывать время", timestamp_enabled) then
		timestamp.enabled = timestamp_enabled[0] and true or false
		module.save()
	end

	local timestamp_scale = imgui.new.float(timestamp.scale or 0.5)
	if imgui.SliderFloat("Scale", timestamp_scale, 0.2, 1.0) then
		timestamp.scale = clamp(timestamp_scale[0], 0.2, 1.0)
		module.save()
	end

	local timestamp_padding = imgui.new.float(timestamp.padding or 0)
	if imgui.SliderFloat("Padding", timestamp_padding, 0, 10) then
		timestamp.padding = clamp(timestamp_padding[0], 0, 10)
		module.save()
	end

	local timestamp_offset_y = imgui.new.float(timestamp.offset_y or 0)
	if imgui.SliderFloat("Offset Y", timestamp_offset_y, -10, 10) then
		timestamp.offset_y = clamp(timestamp_offset_y[0], -10, 10)
		module.save()
	end

	local timestamp_align_baseline = imgui.new.bool(timestamp.align_baseline ~= false)
	if imgui.Checkbox("Align baseline", timestamp_align_baseline) then
		timestamp.align_baseline = timestamp_align_baseline[0] and true or false
		module.save()
	end

	imgui.Separator()
	imgui.Text("Лимиты")
	local vip_limit = ffi.new("int[1]", tonumber(config.vip_limit) or default_config.vip_limit or 100)
	if imgui.InputInt("vip_limit", vip_limit) then
		config.vip_limit = clamp(vip_limit[0], 10, 2000)
		module.save()
	end

	local ad_limit = ffi.new("int[1]", tonumber(config.ad_limit) or default_config.ad_limit or 100)
	if imgui.InputInt("ad_limit", ad_limit) then
		config.ad_limit = clamp(ad_limit[0], 10, 2000)
		module.save()
	end

	local all_limit = ffi.new("int[1]", tonumber(config.all_limit) or default_config.all_limit or 200)
	if imgui.InputInt("all_limit", all_limit) then
		config.all_limit = clamp(all_limit[0], 10, 2000)
		module.save()
	end

	imgui.Separator()
	imgui.Text("Подсветка")
	local serialized = table.concat(config.highlightWords or {}, "\n")
	if serialized ~= highlight_words_last_serialized then
		fill_buf_utf8(highlight_words_buf, serialized)
		highlight_words_last_serialized = serialized
	end
	imgui.InputTextMultiline(
		"highlightWords (по 1 на строку)",
		highlight_words_buf,
		ffi.sizeof(highlight_words_buf),
		imgui.ImVec2(0, 120)
	)
	if imgui.Button("Применить##highlight_words") then
		local raw = ffi.string(highlight_words_buf)
		local next_words = {}
		local seen = {}
		for line in raw:gmatch("[^\r\n]+") do
			local trimmed = line:match("^%s*(.-)%s*$")
			if trimmed ~= "" and not seen[trimmed] then
				seen[trimmed] = true
				next_words[#next_words + 1] = trimmed
			end
		end
		config.highlightWords = next_words
		highlight_words_last_serialized = table.concat(next_words, "\n")
		module.save()
	end

	imgui.Separator()
	imgui.Text("Popup")
	local p = config.popup or default_config.popup

	local auto_sel = imgui.new.bool(p.auto_select_all == true)
	if imgui.Checkbox("Авто-выделение всего текста", auto_sel) then
		p.auto_select_all = auto_sel[0]
		module.save()
	end

	local foc = imgui.new.bool(p.focus_on_open == true)
	if imgui.Checkbox("Фокус на инпут при открытии", foc) then
		p.focus_on_open = foc[0]
		module.save()
	end
end

function module.DrawSettingsWindow()
	if not settings_open[0] then
		return
	end
	imgui.SetNextWindowSize(imgui.ImVec2(520, 220), imgui.Cond.FirstUseEver)
	if imgui.Begin("VIP/AD чат - настройки", settings_open) then
		draw_settings_content()
	end
	imgui.End()
end

function module.DrawSettingsInline()
	draw_settings_content()
end

return module
