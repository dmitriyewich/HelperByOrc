local module = {}

-- ===================== ЗАВИСИМОСТИ =====================
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local imgui = require("mimgui")
local ffi = require("ffi")
local mimgui_funcs = require("HelperByOrc.mimgui_funcs")

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

local function line_height()
	return imgui.GetTextLineHeightWithSpacing()
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
		if tag_s == i then
			cur_col = mul_alpha(hex2rgba_vec4(tag), text_alpha or 1.0)
			i = tag_e + 1
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

	local st = get_select_state(popup_target.key)
	st.initialized = false

	imgui.OpenPopup("##VIPAD_LINE_POPUP")
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
	if #t > 100 then
		table.remove(t, 1)
	end
	local all = config.table_config.all
	all[#all + 1] = { kind = "vip", text = text }
	if #all > 200 then
		table.remove(all, 1)
	end
	module.save()
end

function module.AddADMessage(main, edited, toredact)
	if not config.enabled then
		return
	end
	local t = config.table_config.ad_text
	t[#t + 1] = { main, edited or "", toredact or "" }
	if #t > 100 then
		table.remove(t, 1)
	end
	local all = config.table_config.all
	all[#all + 1] = { kind = "ad", text = main, edited = edited or "", toredact = toredact or "" }
	if #all > 200 then
		table.remove(all, 1)
	end
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
	module.save()
end

function module.ClearAD()
	config.table_config.ad_text = {}
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

			-- POPUP
			if popup_target.key ~= nil then
				local st = get_select_state(popup_target.key)
				if not st.initialized then
					st.show_raw = false
					st.last_src = strip_color_tags(popup_target.src_cp)
					fill_char_buffer_from_string(st.buf, st.last_src)
					st.initialized = true
				end

				if popup_target.size_dirty then
					local cur_text = ffi.string(st.buf)
					local input_sz = calc_popup_input_size(cur_text, feedSize.x)
					popup_target.size = imgui.ImVec2(input_sz.x + 28, input_sz.y + 86)
				end

				if popup_target.pending_open then
					imgui.SetNextWindowPos(popup_target.pos, imgui.Cond.Appearing)
					imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Appearing)
				elseif popup_target.size_dirty then
					imgui.SetNextWindowSize(popup_target.size, imgui.Cond.Always)
				end

				if imgui.BeginPopup("##VIPAD_LINE_POPUP") then
					popup_open_this_frame = true
					popup_target.pending_open = false
					popup_target.size_dirty = false

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
					local input_sz = calc_popup_input_size(cur_text, feedSize.x)

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
					if not popup_target.pending_open then
						popup_target.key = nil
					end
				end
			end
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

local function draw_chatbox_window()
	if not config then
		return false, false, false
	end

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

	local hovered = false
	if imgui.Begin("##VIPAD_CHATBOX", nil, flags) then
		if imgui.BeginTabBar("##VIPAD_CHATBOX_TABS") then
			if imgui.BeginTabItem("ALL") then
				local all = config.table_config.all or {}
				if imgui.BeginChild("##all_scroll", imgui.ImVec2(0, 0), false) then
					local clipper = imgui.ImGuiListClipper(#all)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local entry = all[i] or {}
							local text_cp = entry.text or ""
							local prefix = entry.kind == "ad" and "AD: " or "VIP: "
							local text = strip_color_tags(u8(text_cp))
							imgui.TextUnformatted(prefix .. text)
						end
					end
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end

			if imgui.BeginTabItem("VIP") then
				local vip = config.table_config.vip_text or {}
				if imgui.BeginChild("##vip_scroll", imgui.ImVec2(0, 0), false) then
					local clipper = imgui.ImGuiListClipper(#vip)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local text_cp = vip[i] or ""
							local text = strip_color_tags(u8(text_cp))
							imgui.TextUnformatted(text)
						end
					end
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end

			if imgui.BeginTabItem("AD") then
				local ad = config.table_config.ad_text or {}
				if imgui.BeginChild("##ad_scroll", imgui.ImVec2(0, 0), false) then
					local clipper = imgui.ImGuiListClipper(#ad)
					while clipper:Step() do
						for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
							local entry = ad[i] or {}
							local text_cp = entry[1] or ""
							local text = strip_color_tags(u8(text_cp))
							imgui.TextUnformatted(text)
						end
					end
				end
				imgui.EndChild()
				imgui.EndTabItem()
			end

			imgui.EndTabBar()
		end

		local wpos = imgui.GetWindowPos()
		local wsize = imgui.GetWindowSize()
		cfg.pos_x = wpos.x
		cfg.pos_y = wpos.y
		cfg.width = wsize.x
		cfg.height = wsize.y

		hovered = imgui.IsWindowHovered()
	end
	imgui.End()

	return hovered, hovered, false
end

-- ===================== ONFRAME =====================
local hudFrame = imgui.OnFrame(function()
	return module.showFeedWindow[0] and config and config.enabled
end, function(frame)
	local chat_open = get_is_chat_open()

	imgui.Process = chat_open or popup_target.pending_open or popup_target.was_open_last

	local allow_process, want_cursor, popup_open = draw_chatbox_window()
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

local function draw_settings_content()
	local en = imgui.new.bool(config.enabled and true or false)
	if imgui.Checkbox("Включить модуль", en) then
		config.enabled = en[0] and true or false
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
