local module = {}

-- ===================== ЗАВИСИМОСТИ =====================
local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

local imgui = require 'mimgui'
local ffi		= require 'ffi'
local mimgui_funcs = require 'HelperByOrc.mimgui_funcs'

local funcs
local deepcopy = function(t) return t end
local json_path = getWorkingDirectory().."\\HelperByOrc\\VIPandADchat.json"

local samp

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
		pos_x = 800, pos_y = 500,
		width = 900,
		vip_height = 7, ad_height = 7,
		highlightWords = { "Walcher_Flett", "Admin_John", "VIP_News" },
		vip = {
				'%[VIP ADV%]', '%[VIP%]', '%[PREMIUM%]', '%[FOREVER%]', '%[SERVER%]',
				'%[ADMIN%]', u8:decode('%{......%}%[Семья%]'), u8:decode('%[Альянс%]'), '%[Family Car%]',
				u8:decode('%[Дальнобойщик]'), u8:decode('%(%( %[Дальнобойщик%]')
		},
		table_config = { vip_text = {}, ad_text = {} },

		-- прозрачности (0..1)
		bg_alpha_chat		= 0.50,	 -- фон при is_chat=true
		bg_alpha_idle		= 0.00,	 -- фон при is_chat=false
		text_alpha_chat = 1.00,	 -- текст при is_chat=true
		text_alpha_idle = 0.50,	 -- текст при is_chat=false
}

-- ===================== УТИЛИТЫ =====================
-- убрать цветовые теги {RRGGBB[AA]}
local function strip_color_tags(str)
		return (tostring(str or ""):gsub("{[%xX]+}", ""))
end

local function clamp(v, lo, hi)
		if v < lo then return lo end
		if v > hi then return hi end
		return v
end

local function hex2rgba_vec4(hex, force_alpha) -- hex: "RRGGBB" или "RRGGBBAA"
		local r = tonumber(hex:sub(1,2),16) or 255
		local g = tonumber(hex:sub(3,4),16) or 255
		local b = tonumber(hex:sub(5,6),16) or 255
		local a = (#hex >= 8) and (tonumber(hex:sub(7,8),16) or 255) or 255
		if force_alpha ~= nil then a = clamp(math.floor(force_alpha * 255 + 0.5), 0, 255) end
		return imgui.ImVec4(r/255, g/255, b/255, a/255)
end

local function mul_alpha(col, alpha) -- умножить альфу цвета
		return imgui.ImVec4(col.x, col.y, col.z, clamp(col.w * alpha, 0.0, 1.0))
end

-- ширина текста
local function text_size(s, font, fsize)
		return font:CalcTextSizeA(fsize, 10000, -1, s).x
end

-- высота строки с интервалом — чтобы не «резало» низ символов
local function line_height()
		return imgui.GetTextLineHeightWithSpacing()
end

-- ===================== ПОДСВЕТКА ТЕКСТА =====================
local function draw_text_with_highlight(text, highlightWordsLower, rect_color, text_alpha)
		local draw = imgui.GetWindowDrawList()
		local font = imgui.GetFont()
		local fsize = imgui.GetFontSize()
		local lh = line_height()

		local pos = imgui.GetCursorScreenPos()
		local x, y = pos.x, pos.y
		local style = imgui.GetStyle()

		local default_col = mul_alpha(imgui.GetStyle().Colors[ffi.C.ImGuiCol_Text], text_alpha or 1.0)
		local cur_col = default_col

		local lower = text:lower()
		local i, n = 1, #text
		while i <= n do
				local tag_s, tag_e, tag = text:find("{([%xX]+)}", i)
				if tag_s == i then
						cur_col = mul_alpha(hex2rgba_vec4(tag), text_alpha or 1.0)
						i = tag_e + 1
				else
						local next_tag_s = text:find("{[%xX]+}", i)
						local hit_s, hit_e
						for _, w in ipairs(highlightWordsLower) do
								local s, e = lower:find(w, i, true)
								if s and (not hit_s or s < hit_s) then hit_s, hit_e = s, e end
						end
						if hit_s and (not next_tag_s or hit_s < next_tag_s) then
								if hit_s > i then
										local part = text:sub(i, hit_s-1)
										draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(cur_col), part)
										x = x + text_size(part, font, fsize)
								end
								local wtxt = text:sub(hit_s, hit_e)
								local ww = text_size(wtxt, font, fsize)
								draw:AddRectFilled(imgui.ImVec2(x, y), imgui.ImVec2(x + ww, y + lh), imgui.GetColorU32Vec4(rect_color), style.FrameRounding)
								local white = mul_alpha(imgui.ImVec4(1,1,1,1), text_alpha or 1.0)
								draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(white), wtxt)
								x = x + ww
								i = hit_e + 1
						else
								local next_pos = next_tag_s or (n + 1)
								local part = text:sub(i, next_pos-1)
								draw:AddText(imgui.ImVec2(x, y), imgui.ColorConvertFloat4ToU32(cur_col), part)
								x = x + text_size(part, font, fsize)
								i = next_pos
						end
				end
		end
		imgui.SetCursorScreenPos(imgui.ImVec2(pos.x, pos.y + lh))
end

-- ===================== СОСТОЯНИЕ ПОПАПА ВЫДЕЛЕНИЯ =====================
-- Для каждого элемента (vip/ad + индекс) держим независимый буфер/флаги
local select_states = {}

local function get_select_state(key)
		local st = select_states[key]
		if not st then
				st = {
						show_raw = false,									-- источник: с тегами или без
						buf = imgui.new.char[4096](),			-- общий рабочий буфер UTF-8
						initialized = false,							-- заполняли ли стартовый текст
						last_src = "",										-- чтобы уметь «Сбросить»
				}
				select_states[key] = st
		end
		return st
end

local function fill_char_buffer_from_string(buf, s_cp1251)
		local us = u8(s_cp1251)									 -- CP1251 -> UTF-8
		local maxlen = ffi.sizeof(buf)
		local n = math.min(#us, maxlen - 1)
		if n > 0 then ffi.copy(buf, us, n) end
		buf[n] = 0
end

-- ===================== ЗАГРУЗКА/СОХРАНЕНИЕ =====================
function module.load()
	config = deepcopy(default_config)
        local loaded = funcs and funcs.loadTableFromJson and funcs.loadTableFromJson(json_path) or nil
	if type(loaded) == "table" and next(loaded) then
		for k, v in pairs(loaded) do config[k] = v end
	end
	config.table_config = config.table_config or { vip_text = {}, ad_text = {} }
	config.table_config.vip_text = config.table_config.vip_text or {}
	config.table_config.ad_text = config.table_config.ad_text or {}
end


function module.save()
        if funcs and funcs.saveTableToJson then
                funcs.saveTableToJson(config, json_path)
        end
end


-- ===================== ПУБЛИЧНОЕ API =====================
function module.AddVIPMessage(text)
		local t = config.table_config.vip_text
		t[#t+1] = text
		if #t > 100 then table.remove(t, 1) end
		module.save()
end

function module.AddADMessage(main, edited, toredact)
		local t = config.table_config.ad_text
		t[#t+1] = { main, edited or "", toredact or "" }
		if #t > 100 then table.remove(t, 1) end
		module.save()
end

function module.SetLastADEdited(text)
		local ad = config.table_config.ad_text
		if #ad > 0 then ad[#ad][2] = text; module.save() end
end

function module.SetLastADPreEdit(text)
		local ad = config.table_config.ad_text
		if #ad > 0 then ad[#ad][3] = text; module.save() end
end

function module.ClearVIP() config.table_config.vip_text = {}; module.save() end
function module.ClearAD()	 config.table_config.ad_text	= {}; module.save() end

function module.VIP() return config.vip end

-- ===================== ОКНО ЛЕНТЫ =====================
module.showFeedWindow = imgui.new.bool(false)

imgui.OnFrame(
		function() return module.showFeedWindow[0] end,
		function(VIPandADchat)
                                local is_chat = samp and samp.is_chat_opened and samp.is_chat_opened() or false
				VIPandADchat.HideCursor = not is_chat
				if not config then return end

				-- Альфы по ТЗ
				local bg_alpha	 = is_chat and config.bg_alpha_chat		or config.bg_alpha_idle
				local text_alpha = is_chat and config.text_alpha_chat or config.text_alpha_idle

				local font	= imgui.GetFont()
				local fsize = imgui.GetFontSize()
				local lh		= line_height()

				-- Динамическая ширина окна по самым длинным "чистым" строкам
				local max_width = config.width
				do
						local vip = config.table_config.vip_text
						for i = 1, #vip do
								local s = strip_color_tags(u8(vip[i]))
								local w = text_size(s, font, fsize) + 24
								if w > max_width then max_width = w end
						end
						local ad = config.table_config.ad_text
						for i = 1, #ad do
								local s = strip_color_tags(u8(ad[i][1] or ""))
								local w = text_size(s, font, fsize) + 24
								if w > max_width then max_width = w end
						end
				end

				-- Геометрия: расширяемся только вправо (pivot 0,0)
				imgui.SetNextWindowPos(imgui.ImVec2(config.pos_x, config.pos_y), imgui.Cond.FirstUseEver, imgui.ImVec2(0, 0))
				imgui.SetNextWindowSize(imgui.ImVec2(max_width, (config.vip_height + config.ad_height) * lh + 100), imgui.Cond.FirstUseEver)

				-- Стили
				imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding, 7)
				imgui.PushStyleVarFloat(imgui.StyleVar.WindowBorderSize, 1)

				imgui.PushStyleColor(imgui.Col.WindowBg, imgui.ImVec4(0, 0, 0, bg_alpha))
				local base_text = imgui.GetStyle().Colors[ffi.C.ImGuiCol_Text]
				imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(base_text.x, base_text.y, base_text.z, text_alpha))
				imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0, 0, 0, 0))
				imgui.PushStyleColor(imgui.Col.Border, imgui.ImVec4(0, 0, 0, 0))
				imgui.PushStyleColor(imgui.Col.ScrollbarBg, imgui.ImVec4(0, 0, 0, bg_alpha))
				imgui.PushStyleColor(imgui.Col.ScrollbarGrab, imgui.ImVec4(0, 0, 0, bg_alpha))
				imgui.PushStyleColor(imgui.Col.ScrollbarGrabHovered, imgui.ImVec4(0, 0, 0, bg_alpha))
				imgui.PushStyleColor(imgui.Col.ScrollbarGrabActive, imgui.ImVec4(0, 0, 0, bg_alpha))

				local flags = bit.bor(
						imgui.WindowFlags.NoCollapse,
						imgui.WindowFlags.NoTitleBar,
						imgui.WindowFlags.NoScrollbar,
						imgui.WindowFlags.NoBackground
				)
				if not is_chat then
						flags = bit.bor(flags, imgui.WindowFlags.NoInputs)
				end

				if imgui.Begin("##VIPADFEED", module.showFeedWindow, flags) then
                if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
                        mimgui_funcs.clampWindowToScreen(5)
                end
						local child_flags = not is_chat and imgui.WindowFlags.NoInputs or 0

						-- список слов для подсветки (lower)
						local highlightLower = {}
						do
								local src = config.highlightWords or {}
								for i=1,#src do
										highlightLower[i] = tostring(src[i] or ""):lower()
								end
						end
						local rect_highlight = imgui.ImVec4(1, 1, 0, 0.38)

						-- ===== VIP =====
						imgui.BeginChild("VIP",
								imgui.ImVec2(max_width, config.vip_height*lh + 4),
								true,
								child_flags
						)
						local vip = config.table_config.vip_text
						local vip_lines = #vip
						local vip_at_bottom = (imgui.GetScrollY() >= imgui.GetScrollMaxY() - 1.0)

						if vip_lines > 0 then
								local clipper = imgui.ImGuiListClipper(vip_lines)
								while clipper:Step() do
										for i = clipper.DisplayStart+1, clipper.DisplayEnd do
												local text_cp = vip[i] or ""
												local text = u8(text_cp)
												imgui.PushIDInt(i)

												local row_pos = imgui.GetCursorScreenPos()
												draw_text_with_highlight(text, highlightLower, rect_highlight, text_alpha)

												if is_chat then
														local clean = strip_color_tags(text)
														local w = text_size(clean, font, fsize)
														imgui.SetCursorScreenPos(row_pos)
														imgui.InvisibleButton("##vip"..i, imgui.ImVec2(w, lh))

														-- ЛКМ — копируем всю строку
														if imgui.IsItemHovered() and imgui.IsItemClicked(0) then
																setClipboardText(text_cp)
														end

														-- ПКМ — контекст-попап с инпутом (здесь вместо «Удалить»)
														if imgui.BeginPopupContextItem("ctx_vip_"..i) then
																local st = get_select_state("vip"..i)
																if not st.initialized then
																		st.show_raw = false
																		st.last_src = strip_color_tags(text_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																		st.initialized = true
																end

																imgui.Text("Выделите фрагмент и используйте Ctrl+C.")
																imgui.Spacing()

																local label = st.show_raw and "Источник: С ТЕГАМИ" or "Источник: БЕЗ ТЕГОВ"
																if imgui.SmallButton(label) then
																		st.show_raw = not st.show_raw
																		st.last_src = st.show_raw and text_cp or strip_color_tags(text_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																end
																imgui.SameLine()
																if imgui.SmallButton("Сбросить") then
																		st.last_src = st.show_raw and text_cp or strip_color_tags(text_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																end
																imgui.SameLine()
																if imgui.SmallButton("Копировать всё") then
																		setClipboardText(u8:decode(ffi.string(st.buf)))
																end

																imgui.PushItemWidth(560)
																imgui.InputTextMultiline("##sel_vip"..i, st.buf, ffi.sizeof(st.buf), imgui.ImVec2(560, 150))
																imgui.PopItemWidth()

																if imgui.Button("Закрыть", imgui.ImVec2(120, 0)) then
																		imgui.CloseCurrentPopup()
																end
																imgui.EndPopup()
														end

														imgui.SetCursorScreenPos(imgui.ImVec2(row_pos.x, row_pos.y + lh))
												end

												imgui.PopID()
										end
								end
						end

						if (not is_chat) or vip_at_bottom then
								imgui.SetScrollHereY(1.0)
						end
						imgui.Dummy(imgui.ImVec2(0, imgui.GetStyle().ItemSpacing.y * 0.5))
						imgui.EndChild()

						-- ===== AD =====
						imgui.BeginChild("AD",
								imgui.ImVec2(max_width, config.ad_height*lh + 4),
								true,
								child_flags
						)
						local ad = config.table_config.ad_text
						local ad_lines = #ad
						local ad_at_bottom = (imgui.GetScrollY() >= imgui.GetScrollMaxY() - 1.0)

						if ad_lines > 0 then
								local clipper = imgui.ImGuiListClipper(ad_lines)
								while clipper:Step() do
										for i = clipper.DisplayStart+1, clipper.DisplayEnd do
												local entry = ad[i] or {}
												local main_cp		= entry[1] or ""
												local edited_cp = entry[2] or ""
												local prer_cp		= entry[3] or ""

												local main		 = u8(main_cp)
												local edited	 = u8(edited_cp)
												local toredact = u8(prer_cp)

												imgui.PushIDInt(i)

												local row_pos = imgui.GetCursorScreenPos()
												draw_text_with_highlight(main, highlightLower, rect_highlight, text_alpha)

												if is_chat then
														local clean = strip_color_tags(main)
														local w = text_size(clean, font, fsize)

														imgui.SetCursorScreenPos(row_pos)
														imgui.InvisibleButton("##ad"..i, imgui.ImVec2(w, lh))

														if imgui.IsItemHovered() then
																if (edited_cp ~= "") or (prer_cp ~= "") then
																		imgui.BeginTooltip()
																		if edited_cp ~= "" then
																				imgui.Text("Отредактировано:")
																				imgui.Text(edited)
																		end
																		if prer_cp ~= "" then
																				imgui.Text("До редакции:")
																				imgui.Text(toredact)
																		end
																		imgui.EndTooltip()
																end
														end

														-- ЛКМ — копируем всю строку
														if imgui.IsItemClicked(0) then
																setClipboardText(main_cp)
														end

														-- ПКМ — контекст-попап с инпутом
														if imgui.BeginPopupContextItem("ctx_ad_"..i) then
																local st = get_select_state("ad"..i)
																if not st.initialized then
																		st.show_raw = false
																		st.last_src = strip_color_tags(main_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																		st.initialized = true
																end

																imgui.Text("Выделите фрагмент и используйте Ctrl+C.")
																imgui.Spacing()

																local label = st.show_raw and "Источник: С ТЕГАМИ" or "Источник: БЕЗ ТЕГОВ"
																if imgui.SmallButton(label) then
																		st.show_raw = not st.show_raw
																		st.last_src = st.show_raw and main_cp or strip_color_tags(main_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																end
																imgui.SameLine()
																if imgui.SmallButton("Сбросить") then
																		st.last_src = st.show_raw and main_cp or strip_color_tags(main_cp)
																		fill_char_buffer_from_string(st.buf, st.last_src)
																end
																imgui.SameLine()
																if imgui.SmallButton("Копировать всё") then
																		setClipboardText(u8:decode(ffi.string(st.buf)))
																end

																imgui.PushItemWidth(560)
																imgui.InputTextMultiline("##sel_ad"..i, st.buf, ffi.sizeof(st.buf), imgui.ImVec2(560, 150))
																imgui.PopItemWidth()

																if imgui.Button("Закрыть", imgui.ImVec2(120, 0)) then
																		imgui.CloseCurrentPopup()
																end
																imgui.EndPopup()
														end

														imgui.SetCursorScreenPos(imgui.ImVec2(row_pos.x, row_pos.y + lh))
												end

												imgui.PopID()
										end
								end
						end

						if (not is_chat) or ad_at_bottom then
								imgui.SetScrollHereY(1.0)
						end
						imgui.Dummy(imgui.ImVec2(0, imgui.GetStyle().ItemSpacing.y * 0.5))
						imgui.EndChild()
				end

				imgui.End()
				imgui.PopStyleVar(2)
				imgui.PopStyleColor(8)
		end
)

-- ===================== ОКНО НАСТРОЕК =====================
local settings_open = imgui.new.bool(false)
module.showSettingsWindow = settings_open

local function draw_settings_content()
                imgui.Text("Позиция/размер ленты")
                imgui.PushItemWidth(70)

				local pos_x = ffi.new("int[1]", config.pos_x)
				local pos_y = ffi.new("int[1]", config.pos_y)
				local width = ffi.new("int[1]", config.width)
				local vip_h = ffi.new("int[1]", config.vip_height)
				local ad_h	= ffi.new("int[1]", config.ad_height)

				local changed_basic = false
				if imgui.InputInt("X##posx", pos_x) then config.pos_x = math.max(0, pos_x[0]); changed_basic = true end
				imgui.SameLine()
				if imgui.InputInt("Y##posy", pos_y) then config.pos_y = math.max(0, pos_y[0]); changed_basic = true end
				imgui.SameLine()
				if imgui.InputInt("Ширина", width) then config.width = math.max(200, width[0]); changed_basic = true end
				imgui.SameLine()
				if imgui.InputInt("VIP-строк", vip_h) then config.vip_height = math.max(1, vip_h[0]); changed_basic = true end
				imgui.SameLine()
				if imgui.InputInt("AD-строк", ad_h) then config.ad_height = math.max(1, ad_h[0]); changed_basic = true end
				imgui.PopItemWidth()

				if changed_basic and imgui.IsMouseReleased(0) then module.save() end

				imgui.Separator()
				imgui.Text("Ключевые слова для подсветки:")
				for i, word in ipairs(config.highlightWords) do
						imgui.PushIDInt(i)
						local buf = imgui.new.char[64](word)
						if imgui.InputText("##w", buf, ffi.sizeof(buf)) then
								config.highlightWords[i] = ffi.string(buf); module.save()
						end
						imgui.SameLine()
						if imgui.SmallButton("Удалить##w"..i) then
								table.remove(config.highlightWords, i); module.save()
						end
						imgui.PopID()
				end
				local newWord = imgui.new.char[64]()
				if imgui.InputText("Добавить ключевое слово", newWord, ffi.sizeof(newWord)) then end
				imgui.SameLine()
				if imgui.SmallButton("Добавить") and ffi.string(newWord)~="" then
						table.insert(config.highlightWords, ffi.string(newWord)); module.save()
				end

				imgui.Separator()
				if imgui.Button("Очистить VIP-ленту") then module.ClearVIP() end
				imgui.SameLine()
				if imgui.Button("Очистить AD-ленту") then module.ClearAD() end

				imgui.Separator()
				imgui.Text("Прозрачность")
				imgui.PushItemWidth(220)
				local a1 = ffi.new("float[1]", config.bg_alpha_chat)
				local a2 = ffi.new("float[1]", config.bg_alpha_idle)
				local a3 = ffi.new("float[1]", config.text_alpha_chat)
				local a4 = ffi.new("float[1]", config.text_alpha_idle)

				local changed_alpha = false
				changed_alpha = imgui.SliderFloat("Фон при is_chat",		a1, 0.0, 1.0, "%.2f") or changed_alpha
				changed_alpha = imgui.SliderFloat("Фон при !is_chat",		a2, 0.0, 1.0, "%.2f") or changed_alpha
				changed_alpha = imgui.SliderFloat("Текст при is_chat",	a3, 0.0, 1.0, "%.2f") or changed_alpha
				changed_alpha = imgui.SliderFloat("Текст при !is_chat", a4, 0.0, 1.0, "%.2f") or changed_alpha
				imgui.PopItemWidth()

				if changed_alpha then
						config.bg_alpha_chat	 = a1[0]
						config.bg_alpha_idle	 = a2[0]
						config.text_alpha_chat = a3[0]
						config.text_alpha_idle = a4[0]
						module.save()
				end

				imgui.Separator()
				-- Экспорт / Импорт (опционально)
					if imgui.Button("Экспортировать конфиг") then
                                                if funcs and funcs.saveTableToJson then
                                                        funcs.saveTableToJson(config, json_path..".backup")
                                                end
                                        end
                                        imgui.SameLine()
                                        if imgui.Button("Импортировать из backup") then
                                                if funcs and doesFileExist(json_path..".backup") then
                                                        local tbl = funcs.loadTableFromJson(json_path..".backup")
                                                        if type(tbl) == "table" then
                                                                for k, v in pairs(tbl) do config[k] = v end
                                                                config.table_config = config.table_config or { vip_text = {}, ad_text = {} }
                                                                config.table_config.vip_text = config.table_config.vip_text or {}
                                                                config.table_config.ad_text = config.table_config.ad_text or {}
                                                                module.save()
                                                        end
                                                end
                                        end

                imgui.Separator()
                imgui.TextDisabled("• ЛКМ по строке — копировать всю строку; ПКМ — открыть окно для выделения нужного фрагмента.\n• Подсветка поддерживает цветовые теги {RRGGBB[AA]}.\n• Фон/текст меняют прозрачность в зависимости от is_chat.\n• Когда чат закрыт: окно и элементы сквозные (NoInputs).\n• Окно расширяется только вправо от позиции (X,Y).")
                imgui.Spacing()
end

function module.DrawSettingsWindow()
                if not settings_open[0] then return end

                imgui.SetNextWindowSize(imgui.ImVec2(600, 460), imgui.Cond.FirstUseEver)
                if imgui.Begin("VIP/AD чат — настройки", settings_open) then
                                draw_settings_content()
                end
                imgui.End()
end

function module.DrawSettingsInline()
                draw_settings_content()
end

return module
