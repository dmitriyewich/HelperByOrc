local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

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

-- === News popup state management ===

function M._make_news_popup_scope_key(quiz_kind, action_name)
	local scope_quiz = tostring(quiz_kind or ""):lower()
	if scope_quiz == "" then
		scope_quiz = "common"
	end
	local scope_action = tostring(action_name or ""):lower()
	if scope_action == "" then
		scope_action = "action"
	end
	return string.format("%s:%s", scope_quiz, scope_action)
end

function M._sanitize_news_popup_messages(messages)
	local sanitized = {}
	if type(messages) ~= "table" then
		return sanitized
	end

	for _, message in ipairs(messages) do
		if type(message) == "string" then
			sanitized[#sanitized + 1] = message
		else
			sanitized[#sanitized + 1] = tostring(message or "")
		end
	end

	return sanitized
end

function M._build_news_popup_fingerprint(messages)
	if type(messages) ~= "table" or #messages == 0 then
		return ""
	end

	local parts = {}
	for idx, message in ipairs(messages) do
		local text = tostring(message or "")
		parts[#parts + 1] = string.format("%d:%d:%s", idx, #text, text)
	end
	return table.concat(parts, "\n")
end

function M._copy_news_popup_meta(meta)
	if type(meta) ~= "table" then
		return {}
	end

	local copied = {}
	for k, v in pairs(meta) do
		copied[k] = v
	end
	return copied
end

function M._reset_news_popup_rows_from_messages(state, messages)
	if type(state) ~= "table" then
		return false
	end

	local sanitized = M._sanitize_news_popup_messages(messages)
	local source_messages = type(state.source_messages) == "table" and state.source_messages or {}
	local row_buffers = type(state.row_buffers) == "table" and state.row_buffers or {}
	local row_sizes = type(state.row_sizes) == "table" and state.row_sizes or {}

	for idx = #source_messages, 1, -1 do
		source_messages[idx] = nil
	end
	for idx = #row_buffers, 1, -1 do
		row_buffers[idx] = nil
	end
	for idx = #row_sizes, 1, -1 do
		row_sizes[idx] = nil
	end

	local SMILive = ctx.SMILive
	local runtime = SMILive._news_popup_runtime or {}
	local min_size = math.max(64, math.floor(tonumber(runtime.min_row_buffer_size) or 256))
	local pad = math.max(16, math.floor(tonumber(runtime.row_buffer_pad) or 64))

	for idx, message in ipairs(sanitized) do
		source_messages[idx] = message
		local required = math.max(min_size, #message + 1 + pad)
		row_sizes[idx] = required
		row_buffers[idx] = imgui.new.char[required](message)
	end

	state.source_messages = source_messages
	state.row_buffers = row_buffers
	state.row_sizes = row_sizes

	return true
end

function M._init_news_popup_rows_from_messages(state, messages)
	if type(state) ~= "table" then
		return false
	end
	if type(state.row_buffers) == "table" and #state.row_buffers > 0 then
		return false
	end

	return M._reset_news_popup_rows_from_messages(state, messages)
end

function M._get_news_popup_row_text(state, idx)
	if type(state) ~= "table" then
		return ""
	end

	local row_idx = math.floor(tonumber(idx) or 0)
	if row_idx < 1 then
		return ""
	end

	local row_buffers = state.row_buffers
	if type(row_buffers) == "table" then
		local buf = row_buffers[row_idx]
		if buf ~= nil then
			local ok, text = pcall(str, buf)
			if ok and type(text) == "string" then
				return text
			end
		end
	end

	local source_messages = state.source_messages
	if type(source_messages) == "table" and source_messages[row_idx] ~= nil then
		return tostring(source_messages[row_idx])
	end

	return ""
end

function M._get_news_popup_state(action_scope)
	local scope_key = tostring(action_scope or "")
	if scope_key == "" then
		return nil
	end

	local SMILive = ctx.SMILive
	local runtime = SMILive._news_popup_runtime
	if type(runtime) ~= "table" then
		runtime = {}
		SMILive._news_popup_runtime = runtime
	end

	local states = runtime.states
	if type(states) ~= "table" then
		states = {}
		runtime.states = states
	end

	local state = states[scope_key]
	if type(state) ~= "table" then
		state = {
			initialized = false,
			source_messages = {},
			row_buffers = {},
			row_sizes = {},
			source_fingerprint = "",
			source_current_messages = {},
			source_current_fingerprint = "",
			source_meta = {},
			source_changed = false,
		}
		states[scope_key] = state
	end

	return state
end

function M._init_news_popup_state(action_scope, source_messages, source_meta)
	local state = M._get_news_popup_state(action_scope)
	if not state then
		return nil, false
	end

	local messages = M._sanitize_news_popup_messages(source_messages)
	local fingerprint = M._build_news_popup_fingerprint(messages)
	state.source_current_messages = messages
	state.source_current_fingerprint = fingerprint

	local was_initialized = state.initialized and true or false
	local has_runtime_rows = type(state.row_buffers) == "table" and type(state.row_sizes) == "table"
	local source_changed = false

	if not was_initialized or not has_runtime_rows then
		M._init_news_popup_rows_from_messages(state, messages)
		state.source_fingerprint = fingerprint
		source_changed = false
	else
		source_changed = state.source_fingerprint ~= fingerprint
	end

	state.initialized = true
	state.source_meta = M._copy_news_popup_meta(source_meta)
	state.source_changed = source_changed

	return state, source_changed
end

function M._reset_news_popup_rows_from_current_source(state)
	if type(state) ~= "table" then
		return false
	end

	local current_messages = type(state.source_current_messages) == "table" and state.source_current_messages or {}
	M._reset_news_popup_rows_from_messages(state, current_messages)
	state.source_fingerprint = tostring(state.source_current_fingerprint or "")
	state.source_changed = false
	return true
end

function M._reset_news_popup_state(action_scope)
	local SMILive = ctx.SMILive
	local runtime = SMILive._news_popup_runtime
	local states = runtime and runtime.states
	if type(states) ~= "table" then
		return false
	end

	if action_scope == nil then
		for key in pairs(states) do
			states[key] = nil
		end
		return true
	end

	local scope_key = tostring(action_scope or "")
	if scope_key == "" then
		return false
	end

	if states[scope_key] ~= nil then
		states[scope_key] = nil
		return true
	end

	return false
end

function M._send_single_news_popup_row(popup_scope_key, line_index, section_name_for_status, send_key)
	local trim = ctx.trim
	local scope_key = tostring(popup_scope_key or "")
	local section_name = tostring(section_name_for_status or L("smi_live.news_popup.text.text"))
	if scope_key == "" then
		ctx.update_status(L("smi_live.news_popup.text.popup_format"), section_name)
		return false
	end

	local SMILive = ctx.SMILive
	local runtime = SMILive._news_popup_runtime
	local states = runtime and runtime.states
	local state = type(states) == "table" and states[scope_key] or nil
	if type(state) ~= "table" or not state.initialized then
		ctx.update_status(L("smi_live.news_popup.text.popup_state_format"), section_name)
		return false
	end

	local row_buffers_count = type(state.row_buffers) == "table" and #state.row_buffers or 0
	local source_count = type(state.source_messages) == "table" and #state.source_messages or 0
	local max_rows = math.max(row_buffers_count, source_count)
	local row_idx = math.floor(tonumber(line_index) or 0)
	if row_idx < 1 or row_idx > max_rows then
		ctx.update_status(
			L("smi_live.news_popup.text.format_number_1_number"),
			section_name,
			row_idx,
			max_rows
		)
		return false
	end

	local raw_line = M._get_news_popup_row_text(state, row_idx)
	local cleaned = trim and trim(raw_line) or tostring(raw_line or ""):gsub("^%s*(.-)%s*$", "%1")
	if cleaned == "" then
		ctx.update_status(L("smi_live.news_popup.text.number_format"), row_idx, section_name)
		return false
	end

	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local message = cleaned
	if cleaned:sub(1, #NEWS_PREFIX) ~= NEWS_PREFIX then
		message = string.format("%s %s", NEWS_PREFIX, cleaned)
	end

	if ctx.get_selected_method() == 3 then
		ctx.update_status(
			L("smi_live.news_popup.text.number_format_1"),
			row_idx,
			section_name
		)
		return false
	end

	local ok = ctx.broadcast_sequence({ message }, send_key)
	if not ok then
		return false
	end

	ctx.update_status(L("smi_live.news_popup.text.number_format_2"), row_idx, section_name)
	return true
end

-- === News popup editor button (draws popup) ===

function M._draw_news_popup_editor_button(options)
	options = type(options) == "table" and options or {}
	local mimgui_funcs = ctx.mimgui_funcs
	local trim = ctx.trim

	local popup_scope_key = tostring(options.popup_scope_key or "")
	local popup_id = tostring(options.popup_id or "")
	if popup_id == "" then
		popup_id = "news_popup_editor_" .. popup_scope_key:gsub("[^%w_]+", "_")
	end
	local section_name = tostring(options.section_name or L("smi_live.news_popup.text.text"))
	local small_button_label = tostring(options.small_button_label or "..")
	local reset_button_label = tostring(options.reset_button_label or L("smi_live.news_popup.text.text_3"))
	local popup_title = tostring(options.popup_title or L("smi_live.news_popup.text.text_4"))
	local send_key = options.send_key
	local build_messages_fn = options.build_messages_fn
	local send_one_fn = options.send_one_fn
	local source_meta_arg = options.source_meta

	local button_id = string.format("%s##open_news_popup_%s", small_button_label, popup_id)
	if imgui.SmallButton(button_id) then
		imgui.OpenPopup(popup_id)
	end
	mimgui_funcs.imgui_hover_tooltip_safe(
		L("smi_live.news_popup.text.news")
	)

	if not imgui.BeginPopup(popup_id) then
		return
	end

	imgui.Text(popup_title)
	imgui.Separator()

	local source_messages = {}
	if type(build_messages_fn) == "function" then
		local ok, messages_or_err = pcall(build_messages_fn)
		if ok then
			if type(messages_or_err) == "table" then
				source_messages = messages_or_err
			end
		else
			ctx.update_status(L("smi_live.news_popup.text.format_format"), section_name, tostring(messages_or_err))
		end
	end

	local source_meta = source_meta_arg
	if type(source_meta_arg) == "function" then
		local ok, meta_or_err = pcall(source_meta_arg)
		if ok then
			source_meta = meta_or_err
		else
			source_meta = nil
			ctx.update_status(L("smi_live.news_popup.text.meta_format_format"), section_name, tostring(meta_or_err))
		end
	end

	local state = M._init_news_popup_state(popup_scope_key, source_messages, source_meta)
	if not state then
		imgui.TextDisabled(L("smi_live.news_popup.text.popup_state"))
		imgui.EndPopup()
		return
	end

	if imgui.Button(reset_button_label .. "##news_popup_reset_" .. popup_id) then
		M._reset_news_popup_rows_from_current_source(state)
	end

	if state.source_changed then
		imgui.TextColored(
			imgui.ImVec4(1.0, 0.8, 0.3, 1.0),
			L("smi_live.news_popup.text.text_5")
		)
	end

	local row_buffers = type(state.row_buffers) == "table" and state.row_buffers or {}
	if #row_buffers == 0 then
		imgui.TextDisabled(L("smi_live.news_popup.text.text_6"))
		imgui.EndPopup()
		return
	end

	local style = imgui.GetStyle()
	local send_btn_text = L("smi_live.news_popup.text.text_7")
	local send_btn_width = imgui.CalcTextSize(send_btn_text).x + style.FramePadding.x * 2

	for idx = 1, #row_buffers do
		local buf = row_buffers[idx]
		local buf_size = (type(state.row_sizes) == "table" and state.row_sizes[idx]) or 0
		local row_text = M._get_news_popup_row_text(state, idx)
		local cleaned = trim and trim(row_text) or tostring(row_text or ""):gsub("^%s*(.-)%s*$", "%1")
		local row_empty = cleaned == ""

		imgui.PushIDInt(idx)
		imgui.Text(string.format("%d.", idx))
		imgui.SameLine()

		local avail = imgui.GetContentRegionAvail()
		local input_width = math.max(120, avail.x - send_btn_width - style.ItemSpacing.x)
		imgui.PushItemWidth(input_width)
		if buf ~= nil and buf_size > 1 then
			imgui.InputText("##news_popup_row_input", buf, buf_size)
		else
			imgui.TextDisabled("-")
		end
		imgui.PopItemWidth()

		imgui.SameLine()
		if row_empty then
			local alpha = style.Alpha
			imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha * 0.5)
		end
		local send_clicked = imgui.SmallButton(L("smi_live.news_popup.text.news_popup_row_send"))
		if row_empty then
			imgui.PopStyleVar()
		end
		if send_clicked and not row_empty then
			if type(send_one_fn) == "function" then
				local ok, sent_or_err = pcall(send_one_fn, idx, popup_scope_key, state)
				if not ok then
					ctx.update_status(
						L("smi_live.news_popup.text.number_format_format"),
						idx,
						section_name,
						tostring(sent_or_err)
					)
				end
			else
				M._send_single_news_popup_row(popup_scope_key, idx, section_name, send_key)
			end
		end
		if row_empty then
			imgui.SameLine()
			imgui.TextDisabled(L("smi_live.news_popup.text.text_8"))
		end

		imgui.PopID()
	end

	imgui.EndPopup()
end

-- === News send cooldown timer ===

function M._draw_news_send_cooldown_timer()
	local remaining_ms = ctx.broadcast_mod._get_news_send_cooldown_remaining_ms()
	if remaining_ms <= 0 then
		imgui.TextColored(imgui.ImVec4(0.45, 0.9, 0.45, 1), L("smi_live.news_popup.text.cooldown_ready"))
		return
	end
	imgui.TextColored(
		imgui.ImVec4(1.0, 0.75, 0.35, 1),
		(L("smi_live.news_popup.text.cooldown_remaining")):format(remaining_ms / 1000)
	)
end

-- === News input helpers ===

function M.count_news_length(text)
	local decoded = u8:decode(tostring(text or ""))
	return #decoded
end

function M.flatten_news_text(text)
	local trim = ctx.trim
	text = tostring(text or "")
	text = text:gsub("\t", " ")
	text = text:gsub("\r\n", "\n")
	text = text:gsub("\r", "\n")
	text = text:gsub("\n+", " ")
	return trim(text)
end

function M.strip_news_prefix(text)
	local trim = ctx.trim
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local cleaned = trim(text or "")
	local lower = cleaned:lower()
	if lower:sub(1, #NEWS_PREFIX) == NEWS_PREFIX then
		local next_char = cleaned:sub(#NEWS_PREFIX + 1, #NEWS_PREFIX + 1)
		if next_char == "" or next_char:match("%s") then
			return trim(cleaned:sub(#NEWS_PREFIX + 1)), true
		end
	end
	return cleaned, false
end

function M.apply_tags_for_news(text)
	local tags_module = ctx.tags_module
	if not text or text == "" then
		return "", nil
	end
	if tags_module and type(tags_module.change_tags) == "function" then
		local ok, result = pcall(tags_module.change_tags, text)
		if ok and type(result) == "string" then
			return result, nil
		elseif not ok then
			return text, tostring(result)
		end
	end
	return text, nil
end

function M.create_news_messages_from_text(text)
	local trim = ctx.trim
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local cleaned = trim(text)
	if cleaned == "" then
		return nil
	end

	local messages = {}
	for line in text:gmatch("[^\r\n]+") do
		local stripped = trim(line)
		if stripped ~= "" then
			if stripped:sub(1, #NEWS_PREFIX) == NEWS_PREFIX then
				messages[#messages + 1] = stripped
			else
				messages[#messages + 1] = string.format("%s %s", NEWS_PREFIX, stripped)
			end
		end
	end

	if #messages == 0 then
		return nil
	end

	return messages
end

function M.update_news_input_state()
	local NewsInput = ctx.NewsInput
	local NEWS_INPUT_MAX_LENGTH = ctx.NEWS_INPUT_MAX_LENGTH
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local buf = NewsInput.buf
	local raw = buf and str(buf) or ""
	NewsInput.raw_text = raw

	local flattened = M.flatten_news_text(raw)
	NewsInput.flattened_full = flattened

	local body, had_prefix = M.strip_news_prefix(flattened)
	NewsInput.body_text = body
	NewsInput.had_prefix = had_prefix

	local processed, tag_error = M.apply_tags_for_news(body)
	local processed_222 = u8:decode(processed)
	NewsInput.processed_text = processed
	NewsInput.tag_error = tag_error
	NewsInput.processed_len = #processed_222
	NewsInput.over_limit = NewsInput.processed_len > NEWS_INPUT_MAX_LENGTH

	if processed ~= "" then
		NewsInput.preview = string.format("%s %s", NEWS_PREFIX, processed)
	else
		NewsInput.preview = ""
	end
end

function M.run_news_autocorrection(handler, label)
	if type(handler) ~= "function" then
		ctx.update_status(L("smi_live.news_popup.text.autocorrect_unavailable", {
			label = label or "",
		}))
		return
	end

	local NewsInput = ctx.NewsInput
	local trim = ctx.trim
	local raw_utf8 = NewsInput.buf and str(NewsInput.buf) or ""
	local raw_text = u8:decode(raw_utf8)
	if trim(raw_text) == "" then
		ctx.update_status(L("smi_live.news_popup.text.enter_text_first"))
		return
	end

	handler(raw_text, function(new_text)
		if type(new_text) ~= "string" then
			return
		end
		imgui.StrCopy(NewsInput.buf, u8(new_text), NewsInput.buf_size or ctx.NEWS_INPUT_BUFFER_SIZE)
		M.update_news_input_state()
	end)
end

-- === Custom news send ===

function M._send_custom_news_message()
	M.update_news_input_state()

	local NewsInput = ctx.NewsInput
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local NEWS_INPUT_MAX_LENGTH = ctx.NEWS_INPUT_MAX_LENGTH
	local body = NewsInput.body_text or ""
	if body == "" then
		ctx.update_status(L("smi_live.news_popup.text.text_9"))
		return
	end

	if NewsInput.over_limit then
		ctx.update_status(
			L("smi_live.news_popup.text.number"),
			NEWS_INPUT_MAX_LENGTH
		)
		return
	end

	local method = ctx.get_selected_method()
	if method == 3 then
		ctx.update_status(L("smi_live.news_popup.text.text_10"))
		return
	end

	local message = string.format("%s %s", NEWS_PREFIX, body)
	local ok = ctx.broadcast_sequence({ message }, "custom_news")
	if not ok then
		return
	end
	ctx.update_status(L("smi_live.news_popup.text.text_11"))
end

-- === Append to news input ===

function M._append_to_news_input(text)
	local trim = ctx.trim
	local NewsInput = ctx.NewsInput
	local token = trim(text)
	if token == "" then
		return false
	end

	local current = NewsInput.buf and str(NewsInput.buf) or ""
	local separator = ""
	if current ~= "" then
		local last = current:sub(-1)
		if not last:match("[%s\r\n]") then
			separator = " "
		end
	end

	local next_text = current .. separator .. token
	local max_len = math.max(1, (NewsInput.buf_size or ctx.NEWS_INPUT_BUFFER_SIZE) - 1)
	if #next_text > max_len then
		ctx.update_status(L("smi_live.news_popup.text.number_12"), max_len)
		return false
	end

	imgui.StrCopy(NewsInput.buf, next_text, NewsInput.buf_size or ctx.NEWS_INPUT_BUFFER_SIZE)
	M.update_news_input_state()
	return true
end

function M._open_tags_help_window()
	local tags_module = ctx.tags_module
	if not tags_module or not tags_module.showTagsWindow then
		ctx.update_status(L("smi_live.news_popup.text.text_13"))
		return false
	end
	local ok, err = pcall(function()
		tags_module.showTagsWindow[0] = true
	end)
	if not ok then
		ctx.update_status(L("smi_live.news_popup.text.format"), err)
		return false
	end
	return true
end

-- === News input panel UI ===

function M._draw_news_input_panel()
	local mimgui_funcs = ctx.mimgui_funcs
	local correct_module = ctx.correct_module
	local NewsInput = ctx.NewsInput
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local NEWS_INPUT_MAX_LENGTH = ctx.NEWS_INPUT_MAX_LENGTH
	local NEWS_INPUT_FLAGS = ctx.NEWS_INPUT_FLAGS
	local NEWS_INPUT_PANEL_HEIGHT = ctx.NEWS_INPUT_PANEL_HEIGHT
	local MathQuiz = ctx.MathQuiz
	local CapitalsQuiz = ctx.CapitalsQuiz
	local SMILive = ctx.SMILive
	local trim = ctx.trim
	local escape_imgui_text = ctx.escape_imgui_text
	local push_button_palette = ctx.push_button_palette
	local pop_button_palette = ctx.pop_button_palette
	local COLOR_ACCENT_PRIMARY = ctx.COLOR_ACCENT_PRIMARY
	local COLOR_ACCENT_DANGER = ctx.COLOR_ACCENT_DANGER
	local State = ctx.State

	imgui.Text(L("smi_live.news_popup.text.news_14"))
	imgui.SameLine()
	local rpnick_tag = L("smi_live.news_popup.text.news_tag_rpnick")
	if imgui.SmallButton(rpnick_tag .. "##news_tag_rpnick") then
		M._append_to_news_input(rpnick_tag)
	end
	imgui.SameLine()
	local nickru_tag = L("smi_live.news_popup.text.news_tag_nickru")
	if imgui.SmallButton(nickru_tag .. "##news_tag_nickru") then
		M._append_to_news_input(nickru_tag)
	end
	imgui.SameLine()
	if imgui.SmallButton(L("smi_live.news_popup.text.news_tag_help")) then
		M._open_tags_help_window()
	end
	mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_15"))
	local active_quiz_kind = SMILive._active_live_tab == "capitals" and "capitals" or "math"
	local build_round_message_fn = active_quiz_kind == "capitals" and SMILive._build_capitals_round_answer_message
		or SMILive._build_math_round_answer_message
	local build_winner_message_fn = active_quiz_kind == "capitals" and SMILive._build_capitals_winner_message
		or SMILive._build_math_winner_message
	local winner_defined = false
	if active_quiz_kind == "capitals" then
		winner_defined = (not CapitalsQuiz.active) and type(CapitalsQuiz.winner) == "string" and trim(CapitalsQuiz.winner) ~= ""
	else
		winner_defined = (not MathQuiz.active) and type(MathQuiz.winner) == "string" and trim(MathQuiz.winner) ~= ""
	end
	imgui.SameLine()
	if imgui.SmallButton(L("smi_live.news_popup.text.news_math_round_male")) then
		local message, err = build_round_message_fn("male")
		if message and message ~= "" then
			M._append_to_news_input(message)
		elseif err then
			ctx.update_status(err)
		end
	end
	mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_16"))
	imgui.SameLine()
	if imgui.SmallButton(L("smi_live.news_popup.text.news_math_round_female")) then
		local message, err = build_round_message_fn("female")
		if message and message ~= "" then
			M._append_to_news_input(message)
		elseif err then
			ctx.update_status(err)
		end
	end
	mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_17"))
	if winner_defined then
		imgui.SameLine()
		if imgui.SmallButton(L("smi_live.news_popup.text.news_math_winner_male")) then
			local message, err = build_winner_message_fn("male")
			if message and message ~= "" then
				M._append_to_news_input(message)
			elseif err then
				ctx.update_status(err)
			end
		end
		mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_18"))
		imgui.SameLine()
		if imgui.SmallButton(L("smi_live.news_popup.text.news_math_winner_female")) then
			local message, err = build_winner_message_fn("female")
			if message and message ~= "" then
				M._append_to_news_input(message)
			elseif err then
				ctx.update_status(err)
			end
		end
		mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_19"))
	end
	imgui.Dummy(imgui.ImVec2(0, 2))

	local avail = imgui.GetContentRegionAvail()
	local input_height = math.max(50, math.min(80, avail.y - 50))
	local style = imgui.GetStyle()
	local shot_label = L("smi_live.news_popup.text.text_20")
	local shot_text_size = imgui.CalcTextSize(shot_label)
	local shot_w = shot_text_size.x + style.FramePadding.x * 2
	local shot_h = math.max(input_height, shot_text_size.y + style.FramePadding.y * 2)
	local input_w = math.max(50, avail.x - shot_w - style.ItemSpacing.x)

	imgui.InputTextMultiline(
		"##live_news_input",
		NewsInput.buf,
		NewsInput.buf_size,
		imgui.ImVec2(input_w, input_height),
		NEWS_INPUT_FLAGS
	)
	imgui.SameLine()
	if imgui.Button(shot_label, imgui.ImVec2(shot_w, shot_h)) then
		ctx.take_live_window_screenshot()
	end

	M.update_news_input_state()

	if NewsInput.had_prefix then
		imgui.TextColored(
			imgui.ImVec4(0.7, 0.7, 0.7, 1),
			L("smi_live.news_popup.text.news_21")
		)
	end

	if NewsInput.tag_error then
		imgui.TextColored(
			imgui.ImVec4(1.0, 0.6, 0.3, 1),
			L("smi_live.news_popup.text.text_22")
		)
	end

	if NewsInput.preview ~= "" then
		imgui.TextWrapped(escape_imgui_text(L("smi_live.news_popup.text.text_23") .. NewsInput.preview))
	end

	local len_color = NewsInput.over_limit and imgui.ImVec4(1.0, 0.4, 0.4, 1) or imgui.ImVec4(0.7, 0.9, 1.0, 1)
	imgui.TextColored(
		len_color,
		string.format(L("smi_live.news_popup.text.number_number"), NewsInput.processed_len, NEWS_INPUT_MAX_LENGTH)
	)
	imgui.SameLine()
	local autocorrect_label = correct_module
		and type(correct_module.getActiveProviderLabel) == "function"
		and correct_module.getActiveProviderLabel()
		or L("smi_live.news_popup.text.text_24")
	if imgui.Button(L("smi_live.news_popup.text.live_news_autocorrect")) then
		local handler = correct_module and correct_module.handleAuto
		M.run_news_autocorrection(handler, autocorrect_label)
	end
	mimgui_funcs.imgui_hover_tooltip_safe(L("smi_live.news_popup.text.text_25") .. tostring(autocorrect_label))
	imgui.SameLine()
	push_button_palette(COLOR_ACCENT_PRIMARY)
	if imgui.Button(L("smi_live.news_popup.text.news_14")) then
		M._send_custom_news_message()
	end
	local preview_line = ""
	if NewsInput.body_text ~= "" then
		preview_line = string.format("%s %s", NEWS_PREFIX, NewsInput.body_text)
	end
	mimgui_funcs.imgui_hover_tooltip_safe(ctx.build_send_tooltip(preview_line))
	pop_button_palette()

	imgui.SameLine()
	push_button_palette(COLOR_ACCENT_DANGER)
	if imgui.Button(L("smi_live.news_popup.text.text_26")) then
		ctx.cancel_send_queue()
	end
	local cancel_tooltip = string.format(L("smi_live.news_popup.text.format_27"), State.send_sequence_running and L("smi_live.news_popup.text.text_28") or L("smi_live.news_popup.text.text_29"))
	mimgui_funcs.imgui_hover_tooltip_safe(cancel_tooltip)
	pop_button_palette()

	if NewsInput.over_limit then
		imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1), L("smi_live.news_popup.text.text_30"))
	end
end

return M
