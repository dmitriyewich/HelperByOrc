local module = {}
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ffi = require("ffi")
local memory = require("memory")
local paths = require("HelperByOrc.paths")

local imgui = require("mimgui")
local new, str, sizeof = imgui.new, ffi.string, ffi.sizeof
local ok_faicons, faicons = pcall(require, "fAwesome7")
local FA_SOLID_FONT_CTX_KEY = "__helperbyorc_fa_solid_ctx"

local allHints = {}

local function ImGuiEnum(name)
	return setmetatable({ __name = name }, {
		__index = function(t, k)
			return imgui.lib[t.__name .. k]
		end,
	})
end

module.TabBarFlags = ImGuiEnum("ImGuiTabBarFlags_")
module.TabItemFlags = ImGuiEnum("ImGuiTabItemFlags_")

local samp
local funcs

function module.attachModules(mod)
	samp = mod.samp
	funcs = mod.funcs
end

local function default_escape_imgui_text(text)
	text = tostring(text or "")
	if text:find("%%") then
		text = text:gsub("%%", "%%%%")
	end
	return text
end

function module.escape_imgui_text(text)
	if funcs and funcs.escape_imgui_text then
		return funcs.escape_imgui_text(text)
	end
	return default_escape_imgui_text(text)
end

function module.imgui_text_safe(text)
	text = tostring(text or "")
	if imgui.TextUnformatted then
		imgui.TextUnformatted(text)
	else
		imgui.Text(module.escape_imgui_text(text))
	end
end

function module.imgui_text_wrapped_safe(text)
	imgui.TextWrapped(module.escape_imgui_text(text))
end

function module.imgui_text_disabled_safe(text)
	imgui.TextDisabled(module.escape_imgui_text(text))
end

function module.imgui_text_colored_safe(color, text)
	imgui.TextColored(color, module.escape_imgui_text(text))
end

function module.imgui_bullet_text_safe(text)
	imgui.BulletText(module.escape_imgui_text(text))
end

function module.imgui_set_tooltip_safe(text)
	imgui.SetTooltip(module.escape_imgui_text(text))
end

function module.imgui_hover_tooltip_safe(text, hovered)
	if hovered == nil then
		hovered = imgui.IsItemHovered()
	end
	if hovered then
		module.imgui_set_tooltip_safe(text)
	end
end

-- Инициализация модуля
local function ensureFontAwesomeSolidMerged()
	if not ok_faicons or type(faicons) ~= "table" then
		return
	end
	if type(faicons.get_font_data_base85) ~= "function" then
		return
	end

	local ctx = imgui.GetCurrentContext and imgui.GetCurrentContext() or nil
	if ctx and rawget(_G, FA_SOLID_FONT_CTX_KEY) == ctx then
		return
	end

	local io = imgui.GetIO()
	if not io or not io.Fonts then
		return
	end

	local minRange = math.max(tonumber(faicons.min_range) or 0xE000, 0xE000)
	local maxRange = tonumber(faicons.max_range) or 0xF8FF
	if maxRange < minRange then
		maxRange = 0xF8FF
	end
	local iconRanges = imgui.new.ImWchar[3](minRange, maxRange, 0)
	local fontCfg = imgui.ImFontConfig()
	fontCfg.MergeMode = true
	fontCfg.PixelSnapH = true

	local iconFont = io.Fonts:AddFontFromMemoryCompressedBase85TTF(
		faicons.get_font_data_base85("solid"),
		14,
		fontCfg,
		iconRanges
	)
	if iconFont and ctx then
		rawset(_G, FA_SOLID_FONT_CTX_KEY, ctx)
	end
end

local _initSub = imgui.OnInitialize(function()
	imgui.GetIO().IniFilename = nil
	ensureFontAwesomeSolidMerged()

	module.logo = imgui.CreateTextureFromFile(paths.findExistingResourceFile("logo.png"))

	module.weapon_standard = imgui.CreateTextureFromFile(paths.findExistingResourceFile("standard_gun.png"))
end)

function module.GetMiddleButtonX(count)
	local width = imgui.GetWindowContentRegionWidth() -- ширины контекста окно
	local space = imgui.GetStyle().ItemSpacing.x
	return count == 1 and width or width / count - ((space * (count - 1)) / count) -- вернется средние ширины по количеству
end

-- function module.resetIO()
-- for i = 0, 511 do
-- imgui.GetIO().KeysDown[i] = false
-- end
-- for i = 0, 4 do
-- imgui.GetIO().MouseDown[i] = false
-- end
-- imgui.GetIO().KeyCtrl = false
-- imgui.GetIO().KeyShift = false
-- imgui.GetIO().KeyAlt = false
-- imgui.GetIO().KeySuper = false
-- end

function module.resetIO()
	if imgui.GetCurrentContext() == nil then
		return
	end
	local ioo = imgui.GetIO()
	ffi.fill(ioo.KeysDown, 512)
	ffi.fill(ioo.MouseDown, 5)
	ioo.KeyCtrl = false
	ioo.KeyShift = false
	ioo.KeyAlt = false
	ioo.KeySuper = false
end

-- tab_idx: номер вкладки (от 1 до 6)
-- texture_id: id текстуры, загруженной через imgui.CreateTextureFromFile(...)
-- size: imgui.ImVec2(ширина, высота области для одного логотипа, например, 133, 245)

-- tab_idx: номер вкладки (от 1 до 6)
-- texture_id: id текстуры через imgui.CreateTextureFromFile(...)
-- size: imgui.ImVec2(ширина, высота) — например, 256, 256 для уменьшенной версии

function module.drawOrcLogoZoom(texture_id, tab_idx, size, zoom)
	zoom = zoom or 1.6
	local cols, rows = 3, 2
	local cell_w, cell_h = 512, 512
	local tex_w, tex_h = 1536, 1024

	local col = ((tab_idx - 1) % cols)
	local row = math.floor((tab_idx - 1) / cols)

	-- Границы ячейки в текстуре
	local cell_x0 = col * cell_w
	local cell_y0 = row * cell_h

	-- Центр ячейки
	local cx = cell_x0 + cell_w / 2
	local cy = cell_y0 + cell_h / 2

	-- Размер области для зума
	local zoom_w = cell_w / zoom
	local zoom_h = cell_h / zoom

	local zx0 = cx - zoom_w / 2
	local zy0 = cy - zoom_h / 2
	local zx1 = cx + zoom_w / 2
	local zy1 = cy + zoom_h / 2

	-- UV-координаты
	local uv0 = imgui.ImVec2(zx0 / tex_w, zy0 / tex_h)
	local uv1 = imgui.ImVec2(zx1 / tex_w, zy1 / tex_h)

	imgui.Image(texture_id, size, uv0, uv1)
end

function module.drawWeaponZoom(texture_id, idx, size, zoom)
	-- спрайт-лист: 540x100, 9 колонок, 5 строк, без отступов и промежутков
	local tex_w, tex_h = 540, 100
	local cols, rows = 9, 5
	local cell_w, cell_h = 60, 20
	local gap_x, gap_y = 0, 0
	local margin_x, margin_y = 0, 0

	zoom = (zoom and zoom > 0) and zoom or 1
	idx = math.max(1, math.min(idx or 1, cols * rows))
	size = size or imgui.ImVec2(cell_w, cell_h)

	local col = (idx - 1) % cols
	local row = math.floor((idx - 1) / cols)

	-- верхний левый угол ячейки в пикселях текстуры
	local cell_x0 = margin_x + col * (cell_w + gap_x)
	local cell_y0 = margin_y + row * (cell_h + gap_y)
	local cell_x1 = cell_x0 + cell_w
	local cell_y1 = cell_y0 + cell_h

	-- центр ячейки
	local cx = cell_x0 + cell_w * 0.5
	local cy = cell_y0 + cell_h * 0.5

	-- область зума внутри ячейки
	local zoom_w = cell_w / zoom
	local zoom_h = cell_h / zoom

	local zx0 = cx - zoom_w * 0.5
	local zy0 = cy - zoom_h * 0.5
	local zx1 = cx + zoom_w * 0.5
	local zy1 = cy + zoom_h * 0.5

	-- на всякий случай держим рамку внутри ячейки
	if zx0 < cell_x0 then
		zx0 = cell_x0
	end
	if zy0 < cell_y0 then
		zy0 = cell_y0
	end
	if zx1 > cell_x1 then
		zx1 = cell_x1
	end
	if zy1 > cell_y1 then
		zy1 = cell_y1
	end

	-- UV
	local uv0 = imgui.ImVec2(zx0 / tex_w, zy0 / tex_h)
	local uv1 = imgui.ImVec2(zx1 / tex_w, zy1 / tex_h)

	imgui.Image(texture_id, size, uv0, uv1)
end

function module.Standart()
	imgui.SwitchContext()
	local s = imgui.GetStyle()
	local c = s.Colors
	local ImVec2, ImVec4 = imgui.ImVec2, imgui.ImVec4

	-- Миниатюрная строгая геометрия
	s.WindowPadding = ImVec2(5, 5)
	s.FramePadding = ImVec2(5, 3)
	s.ItemSpacing = ImVec2(5, 4)
	s.ItemInnerSpacing = ImVec2(3, 3)
	s.TouchExtraPadding = ImVec2(0, 0)
	s.IndentSpacing = 8
	s.ScrollbarSize = 10
	s.GrabMinSize = 12
	s.WindowBorderSize = 2
	s.ChildBorderSize = 1
	s.PopupBorderSize = 2
	s.FrameBorderSize = 2
	s.TabBorderSize = 1.5
	s.WindowRounding = 6
	s.ChildRounding = 6
	s.FrameRounding = 4
	s.PopupRounding = 5
	s.ScrollbarRounding = 4
	s.GrabRounding = 4
	s.TabRounding = 4
	s.WindowTitleAlign = ImVec2(0.5, 0.5)
	s.ButtonTextAlign = ImVec2(0.5, 0.5)
	s.SelectableTextAlign = ImVec2(0.5, 0.5)

	-- Тёмно-металлическая палитра
	c[imgui.Col.Text] = ImVec4(0.90, 0.92, 0.97, 1.00)
	c[imgui.Col.TextDisabled] = ImVec4(0.36, 0.39, 0.46, 1.00)
	c[imgui.Col.WindowBg] = ImVec4(0.10, 0.12, 0.15, 1.00)
	c[imgui.Col.ChildBg] = ImVec4(0.12, 0.14, 0.18, 0.98)
	c[imgui.Col.PopupBg] = ImVec4(0.14, 0.16, 0.20, 0.97)
	c[imgui.Col.Border] = ImVec4(0.34, 0.39, 0.48, 0.82) -- Стальной для акцентов
	c[imgui.Col.BorderShadow] = ImVec4(0.00, 0.00, 0.00, 0.30)
	c[imgui.Col.FrameBg] = ImVec4(0.19, 0.20, 0.24, 1.00)
	c[imgui.Col.FrameBgHovered] = ImVec4(0.23, 0.28, 0.38, 1.00)
	c[imgui.Col.FrameBgActive] = ImVec4(0.27, 0.36, 0.51, 1.00)

	-- Кнопки (тёмно-металлические, объёмные, с обводкой)
	c[imgui.Col.Button] = ImVec4(0.20, 0.22, 0.26, 1.00)
	c[imgui.Col.ButtonHovered] = ImVec4(0.34, 0.39, 0.48, 1.00)
	c[imgui.Col.ButtonActive] = ImVec4(0.47, 0.60, 0.80, 1.00)
	-- Обводка кнопок (через Col.Border + FrameBorderSize)

	-- Чекмарки, ползунки - металлический акцент
	c[imgui.Col.CheckMark] = ImVec4(0.62, 0.72, 0.92, 1.00)
	c[imgui.Col.SliderGrab] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.SliderGrabActive] = ImVec4(0.62, 0.72, 0.92, 1.00)

	-- Title и табы - металл с синим нюансом
	c[imgui.Col.TitleBg] = ImVec4(0.13, 0.16, 0.21, 1.00)
	c[imgui.Col.TitleBgActive] = ImVec4(0.19, 0.22, 0.28, 1.00)
	c[imgui.Col.TitleBgCollapsed] = ImVec4(0.13, 0.16, 0.21, 0.75)
	c[imgui.Col.MenuBarBg] = ImVec4(0.13, 0.15, 0.19, 1.00)
	c[imgui.Col.ScrollbarBg] = ImVec4(0.17, 0.19, 0.23, 0.90)
	c[imgui.Col.ScrollbarGrab] = ImVec4(0.26, 0.29, 0.38, 0.75)
	c[imgui.Col.ScrollbarGrabHovered] = ImVec4(0.34, 0.39, 0.48, 0.90)
	c[imgui.Col.ScrollbarGrabActive] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.Header] = ImVec4(0.18, 0.20, 0.24, 1.00)
	c[imgui.Col.HeaderHovered] = ImVec4(0.38, 0.45, 0.62, 1.00)
	c[imgui.Col.HeaderActive] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.Separator] = ImVec4(0.27, 0.33, 0.44, 1.00)
	c[imgui.Col.SeparatorHovered] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.SeparatorActive] = ImVec4(0.62, 0.72, 0.92, 1.00)
	c[imgui.Col.ResizeGrip] = ImVec4(0.47, 0.60, 0.80, 0.25)
	c[imgui.Col.ResizeGripHovered] = ImVec4(0.47, 0.60, 0.80, 0.67)
	c[imgui.Col.ResizeGripActive] = ImVec4(0.62, 0.72, 0.92, 0.95)
	c[imgui.Col.Tab] = ImVec4(0.15, 0.17, 0.21, 1.00)
	c[imgui.Col.TabHovered] = ImVec4(0.34, 0.39, 0.48, 0.90)
	c[imgui.Col.TabActive] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.TabUnfocused] = ImVec4(0.13, 0.14, 0.17, 1.00)
	c[imgui.Col.TabUnfocusedActive] = ImVec4(0.16, 0.20, 0.29, 1.00)
	c[imgui.Col.PlotLines] = ImVec4(0.60, 0.60, 0.62, 1.00)
	c[imgui.Col.PlotLinesHovered] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.PlotHistogram] = ImVec4(0.85, 0.85, 0.30, 1.00)
	c[imgui.Col.PlotHistogramHovered] = ImVec4(0.90, 0.90, 0.60, 1.00)
	c[imgui.Col.TextSelectedBg] = ImVec4(0.47, 0.60, 0.80, 0.35)
	c[imgui.Col.DragDropTarget] = ImVec4(1.00, 1.00, 0.00, 0.90)
	c[imgui.Col.NavHighlight] = ImVec4(0.47, 0.60, 0.80, 1.00)
	c[imgui.Col.NavWindowingHighlight] = ImVec4(0.47, 0.60, 0.80, 0.70)
	c[imgui.Col.NavWindowingDimBg] = ImVec4(0.15, 0.18, 0.22, 0.20)
	c[imgui.Col.ModalWindowDimBg] = ImVec4(0.15, 0.18, 0.22, 0.70)
end

function module.CustomInput(name, hint, buffer, bufferSize, flags, width)
	-- local width = width or imgui.GetWindowSize().x / 2;
	local width = width or imgui.GetContentRegionAvail().x
	local DL = imgui.GetWindowDrawList()
	local pos = imgui.GetCursorScreenPos()
	local nameSize = imgui.CalcTextSize(name)
	local padding = imgui.GetStyle().FramePadding
	DL:AddRectFilled(
		pos,
		imgui.ImVec2(pos.x + padding.x * 2 + nameSize.x, pos.y + nameSize.y + padding.y * 2),
		imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBgHovered]),
		imgui.GetStyle().FrameRounding,
		1 + 4
	)
	DL:AddRectFilled(
		imgui.ImVec2(pos.x + padding.x * 2 + nameSize.x, pos.y),
		imgui.ImVec2(pos.x + padding.x * 2 + nameSize.x + width, pos.y + nameSize.y + padding.y * 2),
		imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBg]),
		imgui.GetStyle().FrameRounding,
		10
	)
	DL:AddText(
		imgui.ImVec2(pos.x + padding.x, pos.y + padding.y),
		imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Text]),
		name
	)
	imgui.SetCursorScreenPos(imgui.ImVec2(pos.x + padding.x * 2 + nameSize.x, pos.y))
	imgui.PushItemWidth(width)
	imgui.PushStyleColor(imgui.Col.FrameBg, imgui.ImVec4(0, 0, 0, 0))
	local input = imgui.InputTextWithHint("##customInput_" .. tostring(name), hint or "", buffer, bufferSize, flags)
	imgui.PopStyleColor()
	imgui.PopItemWidth()

	return input
end

function imgui_text_wrapped(clr, text)
	if clr then
		imgui.PushStyleColor(ffi.C.ImGuiCol_Text, clr)
	end

	text = ffi.new("char[?]", #text + 1, text)
	local text_end = text + ffi.sizeof(text) - 1
	local pFont = imgui.GetFont()

	local scale = 1.0
	local endPrevLine = pFont:CalcWordWrapPositionA(scale, text, text_end, imgui.GetContentRegionAvail().x)
	imgui.TextUnformatted(text, endPrevLine)

	while endPrevLine < text_end do
		text = endPrevLine
		if text[0] == 32 then
			text = text + 1
		end
		endPrevLine = pFont:CalcWordWrapPositionA(scale, text, text_end, imgui.GetContentRegionAvail().x)
		if text == endPrevLine then
			endPrevLine = endPrevLine + 1
		end
		imgui.TextUnformatted(text, endPrevLine)
	end

	if clr then
		imgui.PopStyleColor()
	end
end

function module.imgui_text_color(text, wrapped, aBool, number)
	text = text:gsub("{(%x%x%x%x%x%x)}", "{%1FF}")
	local render_func = wrapped and imgui_text_wrapped
		or function(clr, text)
			if clr then
				imgui.PushStyleColor(ffi.C.ImGuiCol_Text, clr)
			end
			imgui.TextUnformatted(text)
			if clr then
				imgui.PopStyleColor()
			end
		end
	local alpha = 0
	local colors = imgui.GetStyle().Colors
	local color = colors[ffi.C.ImGuiCol_Text]
	for _, w in ipairs(funcs.split(text, "\n")) do
		local start = 1
		local a, b = w:find("{........}", start)
		while a do
			local t = w:sub(start, a - 1)
			if #t > 0 then
				render_func(color, t)
				imgui.SameLine(nil, 0)
			end

			local clr = w:sub(a + 1, b - 1)
			if clr:upper() == "STANDART" then
				color = colors[ffi.C.ImGuiCol_Text]
			else
				clr = tonumber(clr, 16)
				if clr then
					local r = bit.band(bit.rshift(clr, 24), 0xFF)
					local g = bit.band(bit.rshift(clr, 16), 0xFF)
					local b = bit.band(bit.rshift(clr, 8), 0xFF)
					local alpha_channel = bit.band(clr, 0xFF)
					if aBool and samp and samp.is_chat_opened then
						alpha = samp.is_chat_opened() and 255 or 120
					else
						alpha = alpha_channel
					end
					-- local r, g, b, a = funcs.explode_argb(color)
					-- if aBool then alpha = samp.is_chat_opened() and 255 or 150 else alpha = a end
					-- color = imgui.ImVec4(r / 255, g / 255, b / 255, a / 255)
					color = imgui.ImVec4(r / 255, g / 255, b / 255, alpha / 255)
				end
			end

			start = b + 1
			a, b = w:find("{........}", start)
		end
		imgui.NewLine()
		if #w > start - 1 then
			imgui.SameLine(nil, 0)
			render_func(color, w:sub(start))
		end
	end
end

function module.TextColoredRGB(text, align, aBool)
	local width = imgui.GetWindowWidth()
	local style = imgui.GetStyle()
	local colors = style.Colors
	local ImVec4 = imgui.ImVec4
	local alpha = 0
	local col = imgui.Col

	local getcolor = function(color)
		if string.upper(color:sub(1, 6)) == "SSSSSS" then
			local r, g, b = colors[0].x, colors[0].y, colors[0].z
			local a = color:sub(7, 8) ~= "FF" and (tonumber(color:sub(7, 8), 16)) or (colors[0].w * 255)
			return ImVec4(r, g, b, a / 255)
		end
		local color = type(color) == "string" and tonumber(color, 16) or color
		if type(color) ~= "number" then
			return
		end
		local r, g, b, a = funcs.explode_argb(color)
		if aBool and samp and samp.is_chat_opened then
			alpha = samp.is_chat_opened() and 255 or 150
		else
			alpha = a
		end
		return ImVec4(r / 255, g / 255, b / 255, alpha / 255)
	end

	local render_text = function(text_)
		for w in string.gmatch(text_, "[^\r\n]+") do
			local textsize = string.gsub(w, "{.-}", "")
			local text_width = imgui.CalcTextSize(textsize)
			if align == 1 then
				imgui.SetCursorPosX(width / 2 - text_width.x / 2)
			elseif align == 2 then
				imgui.SetCursorPosX(
					imgui.GetCursorPosX()
						+ width
						- text_width.x
						- imgui.GetScrollX()
						- 2 * imgui.GetStyle().ItemSpacing.x
						- imgui.GetStyle().ScrollbarSize
				)
			end
			local text, colors_, m = {}, {}, 1
			w = string.gsub(w, "{(......)}", "{%1FF}")
			while string.find(w, "{........}") do
				local n, k = string.find(w, "{........}")
				local color = getcolor(w:sub(n + 1, k - 1))
				if color then
					text[#text], text[#text + 1] = w:sub(m, n - 1), w:sub(k + 1, #w)
					colors_[#colors_ + 1] = color
					m = n
				end
				w = w:sub(1, n - 1) .. w:sub(k + 1, #w)
			end
			if text[0] then
				for i = 0, #text do
					imgui.TextColored(colors_[i] or colors[0], module.escape_imgui_text(text[i] or ""))
					imgui.SameLine(nil, 0)
				end
				imgui.NewLine()
			else
				imgui.Text(module.escape_imgui_text(w))
			end
		end
	end
	render_text(text)
end



local function clamp01(f)
	return f < 0.0 and 0.0 or (f > 1.0 and 1.0 or f)
end

local function hint_check_show(str_id, hovered, animTime)
	if not allHints[str_id] then
		allHints[str_id] = { status = false, timer = 0 }
	end

	local show = true
	if hovered then
		for k, v in pairs(allHints) do
			if k ~= str_id and os.clock() - v.timer <= animTime then
				show = false
				break
			end
		end
	end

	if show and allHints[str_id].status ~= hovered then
		allHints[str_id].status = hovered
		allHints[str_id].timer = os.clock()
	end

	return show
end

function module.Hint(str_id, hint, delay)
	delay = delay or 0.0

	local hovered = imgui.IsItemHovered()
	local animTime = 0.2
	local show = hint_check_show(str_id, hovered, animTime)

	if not show then
		return
	end

	local elapsed = os.clock() - allHints[str_id].timer - delay
	if elapsed <= animTime then
		local alpha = hovered and clamp01(elapsed / animTime) or clamp01(1 - elapsed / animTime)
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
		imgui.SetTooltip(module.escape_imgui_text(hint))
		imgui.PopStyleVar()
	elseif hovered then
		imgui.SetTooltip(module.escape_imgui_text(hint))
	end
end

function module.Hint_test(str_id, hint_text, color, no_center, number)
	number = number or 1
	color = color or imgui.GetStyle().Colors[imgui.Col.PopupBg]
	local p_orig = imgui.GetCursorPos()
	local hovered = imgui.IsItemHovered()
	imgui.SameLine(nil, 0)

	local animTime = 0.1
	local show = hint_check_show(str_id, hovered, animTime)

	local getContrastColor = function(col)
		local luminance = 1 - (0.299 * col.x + 0.587 * col.y + 0.114 * col.z)
		return luminance < 0.5 and imgui.ImVec4(0, 0, 0, 1) or imgui.ImVec4(1, 1, 1, 1)
	end

	local rend_window = function(alpha)
		local size = imgui.GetItemRectSize()
		local scrPos = imgui.GetCursorScreenPos()
		local DL = imgui.GetWindowDrawList()
		local center = imgui.ImVec2(scrPos.x - (size.x / 2), scrPos.y + (size.y / 2) - (alpha * 4) + 10)
		local a = imgui.ImVec2(center.x - 7, center.y - size.y - 3)
		local b = imgui.ImVec2(center.x + 7, center.y - size.y - 3)
		local c = imgui.ImVec2(center.x, center.y - size.y + 3)
		local col = imgui.ColorConvertFloat4ToU32(imgui.ImVec4(color.x, color.y, color.z, alpha))

		DL:AddTriangleFilled(a, b, c, col)
		imgui.SetNextWindowPos(imgui.ImVec2(center.x, center.y - size.y - 3), imgui.Cond.Always, imgui.ImVec2(0.5, 1.0))
		imgui.PushStyleColor(imgui.Col.PopupBg, color)
		imgui.PushStyleColor(imgui.Col.Border, color)
		imgui.PushStyleColor(imgui.Col.Text, getContrastColor(color))
		imgui.PushStyleVarVec2(imgui.StyleVar.WindowPadding, imgui.ImVec2(8, 8))
		imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, 6)
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)

		local max_width = function(text)
			local result = 0
			for line in text:gmatch("[^\n]+") do
				local len = imgui.CalcTextSize(line).x
				if len > result then
					result = len
				end
			end
			return result
		end

		local hint_width = max_width(hint_text) + (imgui.GetStyle().WindowPadding.x * 2)
		imgui.SetNextWindowSize(imgui.ImVec2(hint_width, -1), imgui.Cond.Always)
		imgui.Begin(
			"##" .. str_id,
			_,
			imgui.WindowFlags.Tooltip
				+ imgui.WindowFlags.NoResize
				+ imgui.WindowFlags.NoScrollbar
				+ imgui.WindowFlags.NoTitleBar
		)
		for line in hint_text:gmatch("[^\n]+") do
			if no_center then
				module.imgui_text_color(line, true, true, number)
			else
				imgui.SetCursorPosX((hint_width - imgui.CalcTextSize(line).x) / 2)
				module.imgui_text_color(line, true, true, number)
			end
		end
		imgui.End()

		imgui.PopStyleVar(3)
		imgui.PopStyleColor(3)
	end

	if show then
		local between = os.clock() - allHints[str_id].timer
		if between <= animTime then
			local alpha = hovered and clamp01(between / animTime) or clamp01(1.00 - between / animTime)
			rend_window(alpha)
		elseif hovered then
			rend_window(1.00)
		end
	end

	imgui.SetCursorPos(p_orig)
end

function module.ItemSelector(name, items, selected, fixedSize, dontDrawBorders)
	assert(type(items) == "table" and #items > 1, "items must be array of strings")
	assert(selected and selected[0] ~= nil, 'Wrong argument #3. Selected must be "imgui.new.int"')

	name = tostring(name or "")
	local draw = imgui.GetWindowDrawList()
	local style = imgui.GetStyle()
	local rounding = style.FrameRounding
	local padding = style.FramePadding
	local border_col = imgui.GetColorU32Vec4(style.Colors[imgui.Col.Border])
	local text_col = imgui.GetColorU32Vec4(style.Colors[imgui.Col.Text])
	local corner_flags = imgui.DrawCornerFlags or {}
	local changed = false
	local old = selected[0]
	local border_thickness = style.FrameBorderSize
	if not border_thickness or border_thickness <= 0 then
		border_thickness = 1.0
	end

	imgui.PushIDStr("ItemSelector##" .. name .. "_" .. tostring(selected))
	imgui.BeginGroup()
	if name ~= "" then
		imgui.TextUnformatted(name)
		imgui.SameLine()
	end

	local selector_min, selector_max = nil, nil
	for index, item in ipairs(items) do
		item = tostring(item or "")
		local text_size = imgui.CalcTextSize(item)
		local item_w = ((fixedSize and fixedSize > 0) and fixedSize or text_size.x) + padding.x * 2
		local item_h = text_size.y + padding.y * 2

		if index > 1 then
			imgui.SameLine(0, 0)
		end

		imgui.PushIDInt(index)
		if imgui.InvisibleButton("##item", imgui.ImVec2(item_w, item_h)) then
			old = selected[0]
			selected[0] = index
			changed = true
		end

		local rect_min = imgui.GetItemRectMin()
		local rect_max = imgui.GetItemRectMax()
		if not selector_min then
			selector_min = rect_min
		end
		selector_max = rect_max

		local bg_col = style.Colors[imgui.Col.Button]
		if selected[0] == index or imgui.IsItemActive() then
			bg_col = style.Colors[imgui.Col.ButtonActive]
		elseif imgui.IsItemHovered() then
			bg_col = style.Colors[imgui.Col.ButtonHovered]
		end

		local corners = corner_flags.None or 0
		if #items == 1 then
			corners = corner_flags.All or 15
		elseif index == 1 then
			corners = corner_flags.Left or 5
		elseif index == #items then
			corners = corner_flags.Right or 10
		end

		draw:AddRectFilled(
			rect_min,
			rect_max,
			imgui.GetColorU32Vec4(bg_col),
			rounding,
			corners
		)

		if index > 1 and not dontDrawBorders then
			draw:AddLine(
				imgui.ImVec2(rect_min.x, rect_min.y + padding.y),
				imgui.ImVec2(rect_min.x, rect_max.y - padding.y),
				border_col,
				1.0
			)
		end

		draw:AddText(
			imgui.ImVec2(rect_min.x + (item_w - text_size.x) * 0.5, rect_min.y + padding.y),
			text_col,
			item
		)
		imgui.PopID()
	end

	if selector_min and selector_max then
		draw:AddRect(
			selector_min,
			selector_max,
			border_col,
			rounding,
			corner_flags.All or 15,
			border_thickness
		)
	end

	imgui.EndGroup()
	imgui.PopID()

	if changed then
		return selected[0], old
	end
	return nil, nil
end

imgui.ItemSelector = module.ItemSelector

local menu_animations = {}

function module.customVerticalMenu(items, current)
	-- Параметры
	local button_padding = 8
	local button_height = 36
	local min_button_width = 120
	local corner_radius = 7
	local anim_alpha_speed = 12 -- скорость анимации подсветки
	local anim_shift_speed = 8 -- скорость анимации сдвига
	local max_shift = 18 -- максимальный сдвиг текста (пиксели)

	-- Цвета
	local color_normal = imgui.ImVec4(0.13, 0.13, 0.13, 0.90)
	local color_hovered = imgui.ImVec4(0.35, 0.52, 0.74, 0.33)
	local color_selected = imgui.ImVec4(0.17, 0.32, 0.46, 0.74)
	local color_text = imgui.ImVec4(0.88, 0.88, 0.88, 0.98)
	local color_text_active = imgui.ImVec4(1.00, 1.00, 1.00, 1.0)

	-- 1. Динамическая ширина (по тексту и подсказке)
	local button_width = min_button_width
	for _, item in ipairs(items) do
		local text = item[1] or item.text or ""
		local textsize = imgui.CalcTextSize(text)
		local width = textsize.x + max_shift + 32 -- текст + макс. сдвиг + запас
		if item.hint then
			local hintsize = imgui.CalcTextSize(item.hint)
			width = math.max(width, hintsize.x + 32)
		end
		button_width = math.max(button_width, width)
	end

	imgui.BeginGroup()
	imgui.PushStyleVarVec2(imgui.StyleVar.ItemSpacing, imgui.ImVec2(0, button_padding))
	imgui.PushStyleVarFloat(imgui.StyleVar.FrameRounding, corner_radius)

	for i, item in ipairs(items) do
		local text = item[1] or item.text or ""
		local hint = item.hint

		-- Получаем предыдущее состояние
		local state = menu_animations[i] or { alpha = 0, shift = 0 }
		local is_selected = (current == i)
		local pos_before = imgui.GetCursorPosY()

		-- UI обработка
		local pressed = imgui.InvisibleButton("##btn" .. i, imgui.ImVec2(button_width, button_height))
		local is_hovered = imgui.IsItemHovered()
		-- Целевые значения анимации
		local target_alpha = (is_hovered or is_selected) and 1 or 0
		local target_shift = (is_hovered or is_selected) and max_shift or 0
		-- Анимация альфы
		state.alpha = state.alpha
			+ (target_alpha - state.alpha) * math.min(imgui.GetIO().DeltaTime * anim_alpha_speed, 1)
		-- Анимация сдвига
		state.shift = state.shift
			+ (target_shift - state.shift) * math.min(imgui.GetIO().DeltaTime * anim_shift_speed, 1)
		menu_animations[i] = state

		-- Фон с анимацией
		if state.alpha > 0.01 then
			local col = is_selected
					and imgui.ImVec4(
						color_selected.x,
						color_selected.y,
						color_selected.z,
						color_selected.w * state.alpha
					)
				or imgui.ImVec4(color_hovered.x, color_hovered.y, color_hovered.z, color_hovered.w * state.alpha)
			local min = imgui.GetItemRectMin()
			local max = imgui.GetItemRectMax()
			local draw = imgui.GetWindowDrawList()
			draw:AddRectFilled(min, max, imgui.GetColorU32Vec4(col), corner_radius)
		end

		-- Текст со сдвигом
		local min = imgui.GetItemRectMin()
		local y = min.y + (button_height - imgui.CalcTextSize(text).y) / 2
		imgui.SetCursorScreenPos(imgui.ImVec2(min.x + 20 + state.shift, y))
		imgui.PushStyleColor(imgui.Col.Text, (is_selected or is_hovered) and color_text_active or color_text)
		imgui.TextUnformatted(text)
		imgui.PopStyleColor()

		if pressed then
			current = i
		end

		-- Подсказка (tooltip)
		if is_hovered and hint then
			imgui.SetTooltip(module.escape_imgui_text(hint))
		end

		imgui.SetCursorPosY(pos_before + button_height + button_padding)
	end

	imgui.PopStyleVar(2)
	imgui.EndGroup()

	return current
end

-- Ограничение позиции и размера окна границами экрана
function module.clampWindowToScreen(pos, size, margin)
	margin = margin or 5
	local ds = imgui.GetIO().DisplaySize

	local newSizeX = math.min(size.x, ds.x - margin * 2)
	local newSizeY = math.min(size.y, ds.y - margin * 2)

	local newPosX = math.min(math.max(pos.x, margin), ds.x - newSizeX - margin)
	local newPosY = math.min(math.max(pos.y, margin), ds.y - newSizeY - margin)

	return imgui.ImVec2(newPosX, newPosY), imgui.ImVec2(newSizeX, newSizeY)
end

-- Удерживает текущее окно в пределах экрана, не фиксируя его в одной точке.
function module.clampCurrentWindowToScreen(margin)
	if not (imgui.GetWindowPos and imgui.GetWindowSize and module.clampWindowToScreen) then
		return
	end

	local pos = imgui.GetWindowPos()
	local size = imgui.GetWindowSize()
	local clampedPos, clampedSize = module.clampWindowToScreen(pos, size, margin)
	local eps = 0.1
	local condAlways = (imgui.Cond and imgui.Cond.Always) or 0
	local setWindowPos = imgui.SetWindowPosVec2 or imgui.SetWindowPos
	local setWindowSize = imgui.SetWindowSizeVec2 or imgui.SetWindowSize

	if setWindowPos and setWindowSize then
		if math.abs(clampedSize.x - size.x) > eps or math.abs(clampedSize.y - size.y) > eps then
			setWindowSize(clampedSize, condAlways)
		end
		if math.abs(clampedPos.x - pos.x) > eps or math.abs(clampedPos.y - pos.y) > eps then
			setWindowPos(clampedPos, condAlways)
		end
		return
	end

	-- fallback для сборок без SetWindowPos/SetWindowSize:
	-- применяем ограничение на следующий кадр через SetNextWindow*
	if imgui.SetNextWindowPos and imgui.SetNextWindowSize then
		imgui.SetNextWindowPos(clampedPos, condAlways)
		imgui.SetNextWindowSize(clampedSize, condAlways)
	end
end

function module.onTerminate()
	if _initSub and type(_initSub.Unsubscribe) == "function" then
		pcall(_initSub.Unsubscribe, _initSub)
		_initSub = nil
	end
end

return module
