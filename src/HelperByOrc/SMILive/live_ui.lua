local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local str = ffi.string
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ctx

function M.init(c)
	ctx = c
end

-- === Win message buffer management ===

local function ensure_win_message_buffer(key)
	local entry = ctx.WinMessageBuffers[key]
	if not entry then
		entry = { buf = imgui.new.char[ctx.WIN_MESSAGE_MIN_BUFFER](), size = ctx.WIN_MESSAGE_MIN_BUFFER }
		ctx.WinMessageBuffers[key] = entry
	end
	return entry
end

local function maybe_grow_win_buffer(entry)
	if not entry or not entry.buf or not entry.size then
		return
	end

	local content = str(entry.buf)
	if #content + 1 >= entry.size then
		local new_size = entry.size * 2
		entry.buf = imgui.new.char[new_size](content)
		entry.size = new_size
	end
end

local function update_win_messages_from_buffer(key, entry)
	if not entry or not entry.buf then
		return
	end

	local parsed = {}
	for line in str(entry.buf):gmatch("[^\r\n]+") do
		local cleaned = ctx.trim(line)
		if cleaned ~= "" then
			parsed[#parsed + 1] = cleaned
		end
	end

	local math_cfg = ctx.Config.data.math_quiz
	local win_cfg = math_cfg.win_messages
	if type(win_cfg) ~= "table" then
		win_cfg = {}
		math_cfg.win_messages = win_cfg
	end
	win_cfg[key] = parsed
	M.set_win_message_buffer(key, table.concat(parsed, "\n"))
	ctx.Config:save()
end

function M.set_win_message_buffer(key, text)
	local entry = ctx.WinMessageBuffers[key] or { size = ctx.WIN_MESSAGE_MIN_BUFFER }
	local safe_text = tostring(text or "")
	local required = math.max(ctx.WIN_MESSAGE_MIN_BUFFER, #safe_text + 1)
	if not entry.buf or entry.size < required then
		entry.buf = imgui.new.char[required]()
		entry.size = required
	end
	imgui.StrCopy(entry.buf, safe_text)
	entry.size = required
	ctx.WinMessageBuffers[key] = entry
end

function M.load_win_message_buffers_from_config()
	local math_cfg = ctx.Config.data.math_quiz
	local win_cfg = math_cfg.win_messages
	if type(win_cfg) ~= "table" then
		win_cfg = {}
		math_cfg.win_messages = win_cfg
	end
	M.set_win_message_buffer("male", table.concat(win_cfg.male or {}, "\n"))
	M.set_win_message_buffer("female", table.concat(win_cfg.female or {}, "\n"))
end

function M.get_win_messages_for_gender(gender)
	local math_cfg = ctx.Config.data and ctx.Config.data.math_quiz or {}
	local win_cfg = math_cfg.win_messages or {}
	if gender == "female" then
		return ctx.sanitize_message_list(win_cfg.female)
	end

	return ctx.sanitize_message_list(win_cfg.male)
end

-- === Tooltip helpers ===

function M.normalize_tooltip_line(line)
	line = tostring(line or "")
	line = line:gsub("\r\n", "\n")
	line = line:gsub("\r", "\n")
	line = line:gsub("\n+", " ")
	line = line:gsub("^%s*(.-)%s*$", "%1")
	if line == "" then
		line = "-"
	end
	if #line > 180 then
		line = line:sub(1, 177) .. "..."
	end
	return line
end

function M.build_send_tooltip(first_line)
	local line = M.normalize_tooltip_line(first_line)
	local send_state = ctx.State.send_sequence_running and "активна" or "нет"
	return string.format("Первая строка: %s\nОтправка: %s", line, send_state)
end

function M.combine_send_tooltip(base, first_line)
	local detail = M.build_send_tooltip(first_line)
	if base and base ~= "" then
		return base .. "\n" .. detail
	end
	return detail
end

-- === Preview helpers ===

local function get_first_news_line_from_text(text)
	local create_fn = ctx.news_popup_mod and ctx.news_popup_mod.create_news_messages_from_text
	if not create_fn then
		return nil
	end
	local messages = create_fn(text)
	if messages and messages[1] then
		return messages[1]
	end
	return nil
end

local function get_first_news_line_from_section(section, quiz_kind)
	local messages = ctx.SMILive._get_live_section_news_messages(section, quiz_kind)
	if messages and messages[1] then
		return messages[1]
	end
	return nil
end

-- === Screenshot message buffer ===

local function sync_screenshot_message_buffer()
	local cfg = ctx.Config.data.quiz or {}
	local msg = tostring(cfg.screenshot_message or "")
	if msg ~= ctx.screenshot_msg_last then
		imgui.StrCopy(ctx.screenshot_msg_buf, msg, ctx.SCREENSHOT_MSG_BUF_SIZE)
		ctx.screenshot_msg_last = msg
	end
end

-- === Broadcast controls drawing ===

function M._draw_live_broadcast_controls(broadcast, id_suffix)
	broadcast = broadcast or ctx.LiveBroadcast.math
	local suffix = id_suffix and tostring(id_suffix) or "math"
	local quiz_kind = broadcast == ctx.LiveBroadcast.capitals and "capitals" or "math"

	ctx.push_button_palette(ctx.COLOR_ACCENT_SUCCESS)
	if imgui.Button("Начать эфир##live_start_" .. suffix) then
		ctx.broadcast_mod.send_live_sequence_from_section(broadcast.intro, "Вступление", "live_intro_" .. suffix, quiz_kind)
	end
	ctx.mimgui_funcs.imgui_hover_tooltip_safe(M.build_send_tooltip(get_first_news_line_from_section(broadcast.intro, quiz_kind)))
	imgui.SameLine()
	ctx.news_popup_mod._draw_news_popup_editor_button({
		popup_scope_key = ctx.news_popup_mod._make_news_popup_scope_key(quiz_kind, "live_intro"),
		popup_id = "news_popup_live_intro_" .. suffix,
		section_name = "Вступление",
		small_button_label = "..",
		build_messages_fn = function()
			return ctx.SMILive._get_live_section_news_messages(broadcast.intro, quiz_kind)
		end,
		send_key = "live_intro_" .. suffix,
		source_meta = function()
			return {
				quiz_kind = quiz_kind,
				action = "live_intro",
				suffix = suffix,
			}
		end,
	})
	ctx.pop_button_palette()

	imgui.SameLine()
	ctx.push_button_palette(ctx.COLOR_ACCENT_DANGER)
	if imgui.Button("Закончить эфир##live_stop_" .. suffix) then
		ctx.broadcast_mod.send_live_sequence_from_section(broadcast.outro, "Завершение эфира", "live_outro_" .. suffix, quiz_kind)
	end
	ctx.mimgui_funcs.imgui_hover_tooltip_safe(M.build_send_tooltip(get_first_news_line_from_section(broadcast.outro, quiz_kind)))
	imgui.SameLine()
	ctx.news_popup_mod._draw_news_popup_editor_button({
		popup_scope_key = ctx.news_popup_mod._make_news_popup_scope_key(quiz_kind, "live_outro"),
		popup_id = "news_popup_live_outro_" .. suffix,
		section_name = "Завершение эфира",
		small_button_label = "..",
		build_messages_fn = function()
			return ctx.SMILive._get_live_section_news_messages(broadcast.outro, quiz_kind)
		end,
		send_key = "live_outro_" .. suffix,
		source_meta = function()
			return {
				quiz_kind = quiz_kind,
				action = "live_outro",
				suffix = suffix,
			}
		end,
	})
	ctx.pop_button_palette()

	imgui.SameLine()
	ctx.push_button_palette(ctx.COLOR_ACCENT_PRIMARY)
	if imgui.Button("Напоминание##live_reminder_" .. suffix) then
		ctx.broadcast_mod.send_live_sequence_from_section(broadcast.reminder, "Напоминание", "live_reminder_" .. suffix, quiz_kind)
	end
	ctx.mimgui_funcs.imgui_hover_tooltip_safe(
		M.build_send_tooltip(get_first_news_line_from_section(broadcast.reminder, quiz_kind))
	)
	imgui.SameLine()
	ctx.news_popup_mod._draw_news_popup_editor_button({
		popup_scope_key = ctx.news_popup_mod._make_news_popup_scope_key(quiz_kind, "live_reminder"),
		popup_id = "news_popup_live_reminder_" .. suffix,
		section_name = "Напоминание",
		small_button_label = "..",
		build_messages_fn = function()
			return ctx.SMILive._get_live_section_news_messages(broadcast.reminder, quiz_kind)
		end,
		send_key = "live_reminder_" .. suffix,
		source_meta = function()
			return {
				quiz_kind = quiz_kind,
				action = "live_reminder",
				suffix = suffix,
			}
		end,
	})
	ctx.pop_button_palette()
end

-- === SMS listener controls ===

function M._draw_sms_listener_controls()
	local controls_available = ctx.broadcast_mod.can_use_sms_listener()
	local button_label
	local action

	if ctx.State.sms_listener_active then
		button_label = "Закончить прием сообщений"
		action = function()
			ctx.broadcast_mod.stop_sms_listener(false)
		end
	else
		button_label = "Начать прием сообщений"
		action = function()
			ctx.broadcast_mod.start_sms_listener(false)
		end
	end

	local button_disabled = (not controls_available and not ctx.State.sms_listener_active)
	if button_disabled then
		local alpha = imgui.GetStyle().Alpha
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha * 0.5)
	end

	if imgui.Button(button_label) and not button_disabled then
		if controls_available or ctx.State.sms_listener_active then
			action()
		else
			ctx.broadcast_mod.update_status("Модуль my_hooks не поддерживает приём SMS-сообщений.")
		end
	end

	if button_disabled then
		imgui.PopStyleVar()
	end

	imgui.SameLine()
	local status_color = ctx.State.sms_listener_active and imgui.ImVec4(0.4, 1.0, 0.4, 1) or imgui.ImVec4(1.0, 0.6, 0.4, 1)
	local status_label = ctx.State.sms_listener_active and "активен" or "не активен"
	imgui.TextColored(status_color, status_label)
end

-- === Live text inputs ===

function M._draw_live_text_inputs(broadcast, id_suffix)
	broadcast = broadcast or ctx.LiveBroadcast.math
	local suffix = id_suffix and tostring(id_suffix) or "math"

	imgui.TextDisabled("{balls} - целевое количество очков в текущей игре.")
	imgui.Dummy(imgui.ImVec2(0, 2))

	local intro_changed =
		ctx.update_live_buffer_from_imgui(broadcast.intro, "Вступление##live_intro_text_" .. suffix, 80)
	if intro_changed then
		ctx.mark_live_save_dirty()
	end

	local outro_changed =
		ctx.update_live_buffer_from_imgui(broadcast.outro, "Завершение эфира##live_outro_text_" .. suffix, 80)
	if outro_changed then
		ctx.mark_live_save_dirty()
	end

	local reminder_changed =
		ctx.update_live_buffer_from_imgui(broadcast.reminder, "Напоминание##live_reminder_text_" .. suffix, 80)
	if reminder_changed then
		ctx.mark_live_save_dirty()
	end
end

-- === Win message settings UI ===

local function draw_win_message_settings()
	imgui.TextWrapped(
		"Каждое сообщение на новой строке. Используйте %%s для имени и %%s для счёта."
	)

	local male_buf = ensure_win_message_buffer("male")
	local female_buf = ensure_win_message_buffer("female")

	imgui.Text("Сообщения для победителя (м)")
	imgui.InputTextMultiline("##win_male", male_buf.buf, male_buf.size, imgui.ImVec2(0, 100))
	maybe_grow_win_buffer(male_buf)

	imgui.Text("Сообщения для победительницы (ж)")
	imgui.InputTextMultiline("##win_female", female_buf.buf, female_buf.size, imgui.ImVec2(0, 100))
	maybe_grow_win_buffer(female_buf)

	if imgui.Button("Сохранить настройки") then
		update_win_messages_from_buffer("male", male_buf)
		update_win_messages_from_buffer("female", female_buf)
	end

	imgui.SameLine()
	if imgui.Button("Сбросить к стандартным") then
		ctx.Config.data.math_quiz.win_messages = { male = {}, female = {} }
		M.load_win_message_buffers_from_config()
		ctx.Config:save()
	end

	imgui.TextDisabled(
		'Если список пустой, используется стандартное сообщение: "Викторина завершена! %%s набирает %%s и побеждает!".'
	)

	imgui.Separator()
	imgui.Text("Сообщения победителя/победительницы раунда")
	local math_cfg = ctx.Config.data and ctx.Config.data.math_quiz or {}
	local round_cfg = type(math_cfg.round_messages) == "table" and math_cfg.round_messages or {}
	local male_round = ctx.SMILive._normalize_round_message_templates_text(round_cfg.male, "male")
	local female_round = ctx.SMILive._normalize_round_message_templates_text(round_cfg.female, "female")
	if ctx.live_settings_ui.round_msg_male_last ~= male_round then
		imgui.StrCopy(ctx.live_settings_ui.round_msg_male_buf, male_round, ctx.SMILive._round_message_buffer_size)
		ctx.live_settings_ui.round_msg_male_last = male_round
	end
	if ctx.live_settings_ui.round_msg_female_last ~= female_round then
		imgui.StrCopy(ctx.live_settings_ui.round_msg_female_buf, female_round, ctx.SMILive._round_message_buffer_size)
		ctx.live_settings_ui.round_msg_female_last = female_round
	end

	imgui.Text("Шаблоны (м)")
	if
		imgui.InputTextMultiline(
			"##math_round_message_male",
			ctx.live_settings_ui.round_msg_male_buf,
			ctx.SMILive._round_message_buffer_size,
			imgui.ImVec2(0, 80)
		)
	then
		round_cfg.male = ctx.SMILive._normalize_round_message_templates_text(str(ctx.live_settings_ui.round_msg_male_buf), "male")
		math_cfg.round_messages = round_cfg
		ctx.Config.data.math_quiz = math_cfg
		ctx.live_settings_ui.round_msg_male_last = round_cfg.male
		ctx.Config:save()
	end

	imgui.Text("Шаблоны (ж)")
	if
		imgui.InputTextMultiline(
			"##math_round_message_female",
			ctx.live_settings_ui.round_msg_female_buf,
			ctx.SMILive._round_message_buffer_size,
			imgui.ImVec2(0, 80)
		)
	then
		round_cfg.female = ctx.SMILive._normalize_round_message_templates_text(str(ctx.live_settings_ui.round_msg_female_buf), "female")
		math_cfg.round_messages = round_cfg
		ctx.Config.data.math_quiz = math_cfg
		ctx.live_settings_ui.round_msg_female_last = round_cfg.female
		ctx.Config:save()
	end
	imgui.TextDisabled("Каждая строка - отдельный шаблон. Выбор шаблона происходит случайно.")
	imgui.TextDisabled("Подстановки: 1-й %%s - имя, 2-й %%s - фраза прогресса очков.")
end

function M.DrawWinMessageSettings()
	draw_win_message_settings()
end

-- === Settings sections ===

function M.draw_math_settings_section()
	imgui.Separator()
	if imgui.CollapsingHeader("Настройки викторины Математики") then
		if imgui.CollapsingHeader("Сообщения победителя и победительницы") then
			draw_win_message_settings()
		end

		if imgui.CollapsingHeader("Тексты эфира") then
			M._draw_live_text_inputs(ctx.LiveBroadcast.math, "math")
		end
	end

	ctx.flush_live_save_if_due(false)
end

function M.draw_capitals_settings_section()
	imgui.Separator()
	if imgui.CollapsingHeader("Настройки викторины Столиц") then
		if imgui.CollapsingHeader("Тексты эфира") then
			M._draw_live_text_inputs(ctx.LiveBroadcast.capitals, "capitals")
		end
	end

	ctx.flush_live_save_if_due(false)
end

function M.draw_live_settings_tab()
	imgui.Text("Общие настройки")

	imgui.PushItemWidth(200)
	local method_buf = ctx.live_settings_ui.method_buf
	method_buf[0] = math.floor(tonumber(ctx.MathQuiz.chat_method) or 0)
	local send_labels, send_labels_ffi = ctx.broadcast_mod.get_send_targets()
	local send_count = 0
	if type(send_labels) == "table" then
		send_count = #send_labels
	end
	if send_labels_ffi and send_count > 0 then
		if imgui.Combo("Метод отправки", method_buf, send_labels_ffi, send_count) then
			local max_index = math.max(0, send_count - 1)
			local new_method = method_buf[0]
			if new_method < 0 then
				new_method = 0
			elseif new_method > max_index then
				new_method = max_index
			end
			ctx.MathQuiz.chat_method = new_method
			ctx.Config:save()
		end
	else
		imgui.TextDisabled("Методы отправки недоступны")
	end
	imgui.PopItemWidth()

	imgui.PushItemWidth(200)
	local interval_buf = ctx.live_settings_ui.interval_buf
	interval_buf[0] = math.floor(tonumber(ctx.MathQuiz.chat_interval_ms) or 0)
	if imgui.InputInt("Интервал между сообщениями (мс)", interval_buf) then
		ctx.MathQuiz.chat_interval_ms = math.max(0, interval_buf[0])
		ctx.Config:save()
	end
	imgui.PopItemWidth()

	sync_screenshot_message_buffer()
	imgui.Text("Текст для отправки перед скриншотом")
	local msg_changed = imgui.InputText(
		"##quiz_screenshot_message",
		ctx.screenshot_msg_buf,
		ctx.SCREENSHOT_MSG_BUF_SIZE
	)
	if msg_changed then
		local raw = ffi.string(ctx.screenshot_msg_buf)
		local cfg = ctx.Config.data and ctx.Config.data.quiz or {}
		cfg.screenshot_message = raw
		ctx.Config.data.quiz = cfg
		ctx.screenshot_msg_last = raw
		ctx.Config:save()
	end
end

-- === Main window content ===

function M._draw_live_window_content()
	ctx.news_popup_mod._draw_news_send_cooldown_timer()
	imgui.Dummy(imgui.ImVec2(0, 2))
	local active_tab = ctx.State.active_live_tab or "math"

	if imgui.BeginTabBar("smilive_tabs") then
		if imgui.BeginTabItem("Математика") then
			active_tab = "math"
			M._draw_live_broadcast_controls(ctx.LiveBroadcast.math, "math")
			imgui.Dummy(imgui.ImVec2(0, 4))
			M._draw_sms_listener_controls()
			imgui.Separator()

			ctx.DrawMathQuiz(true)

			imgui.EndTabItem()
		end

		if imgui.BeginTabItem("Столицы") then
			active_tab = "capitals"
			M._draw_live_broadcast_controls(ctx.LiveBroadcast.capitals, "capitals")
			imgui.Dummy(imgui.ImVec2(0, 4))
			M._draw_sms_listener_controls()
			imgui.Separator()

			ctx.DrawCapitalsQuiz(true)

			imgui.EndTabItem()
		end

		if imgui.BeginTabItem("Настройки") then
			active_tab = "settings"
			M.draw_live_settings_tab()
			imgui.EndTabItem()
		end

		imgui.EndTabBar()
	end
	ctx.State.active_live_tab = active_tab
end

function M._draw_live_window()
	local bottom_height = ctx.NEWS_INPUT_PANEL_HEIGHT

	if imgui.BeginChild("smilive_main", imgui.ImVec2(0, -bottom_height - 10), true) then
		M._draw_live_window_content()
	end
	imgui.EndChild()

	imgui.Spacing()

	if imgui.BeginChild("smilive_news_input", imgui.ImVec2(0, bottom_height + 10), true) then
		ctx.news_popup_mod._draw_news_input_panel()
	end
	imgui.EndChild()
	local active_tab = ctx.State.active_live_tab or "math"
	if active_tab == "math" then
		M.draw_math_settings_section()
	elseif active_tab == "capitals" then
		if imgui.CollapsingHeader("Таблица столиц") then
			ctx.DrawCapitalsEditor()
		end
		M.draw_capitals_settings_section()
	end

	ctx.flush_runtime_save_if_due(false)
end

-- === Public API ===

function M.DrawHelperSection()
	imgui.TextWrapped(
		"Эфир-викторина доступна в отдельном окне. Нажмите кнопку, чтобы открыть помощника эфира."
	)
	if imgui.Button("Открыть эфир-викторину") then
		ctx.LiveWindow.show[0] = true
	end
	imgui.Spacing()
	imgui.Separator()
end

return M
