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

-- === Normalization helpers ===

function M.cp1251_lower_text(text)
	local value = tostring(text or "")
	if value == "" then
		return value
	end

	value = value:gsub("%a", string.lower)
	value = value:gsub("[\192-\223]", function(ch)
		return string.char(ch:byte(1) + 32)
	end)
	value = value:gsub(string.char(168), string.char(184))

	return value
end

function M.is_valid_utf8(text)
	if type(text) ~= "string" then
		return false
	end

	local i = 1
	local len = #text
	while i <= len do
		local c = text:byte(i)
		if c < 0x80 then
			i = i + 1
		elseif c >= 0xC2 and c <= 0xDF then
			local c2 = text:byte(i + 1)
			if not c2 or c2 < 0x80 or c2 > 0xBF then
				return false
			end
			i = i + 2
		elseif c >= 0xE0 and c <= 0xEF then
			local c2 = text:byte(i + 1)
			local c3 = text:byte(i + 2)
			if not c2 or not c3 or c2 < 0x80 or c2 > 0xBF or c3 < 0x80 or c3 > 0xBF then
				return false
			end
			i = i + 3
		elseif c >= 0xF0 and c <= 0xF4 then
			local c2 = text:byte(i + 1)
			local c3 = text:byte(i + 2)
			local c4 = text:byte(i + 3)
			if
				not c2
				or not c3
				or not c4
				or c2 < 0x80
				or c2 > 0xBF
				or c3 < 0x80
				or c3 > 0xBF
				or c4 < 0x80
				or c4 > 0xBF
			then
				return false
			end
			i = i + 4
		else
			return false
		end
	end

	return true
end

function M.normalize_country_key(value)
	local trim = ctx.trim
	local cleaned = trim(value)
	if cleaned == "" then
		return ""
	end
	cleaned = cleaned:gsub("%s+", " ")
	if M.is_valid_utf8(cleaned) and u8 and type(u8.decode) == "function" then
		local ok_decoded, decoded = pcall(function()
			return u8:decode(cleaned)
		end)
		if ok_decoded and type(decoded) == "string" and decoded ~= "" then
			cleaned = decoded
		end
	end
	cleaned = M.cp1251_lower_text(cleaned)
	return cleaned
end

function M.find_entry_index_by_country(country)
	local target = M.normalize_country_key(country)
	if target == "" then
		return nil
	end
	for idx, entry in ipairs(ctx.CapitalsQuiz.entries or {}) do
		if M.normalize_country_key(entry and entry.country) == target then
			return idx
		end
	end
	return nil
end

function M.count_cp1251_chars(text)
	local value = tostring(text or "")
	local ok, decoded = pcall(function()
		return u8:decode(value)
	end)
	if ok and type(decoded) == "string" then
		return #decoded
	end
	return #value
end

function M.split_text_by_limit(text, limit)
	local trim = ctx.trim
	local cleaned = trim(text)
	if cleaned == "" then
		return nil, nil
	end

	if M.count_cp1251_chars(cleaned) <= limit then
		return cleaned, nil
	end

	local words = {}
	for word in cleaned:gmatch("%S+") do
		words[#words + 1] = word
	end

	if #words > 1 then
		local first = words[1]
		local last_ok = 1
		for idx = 2, #words do
			local candidate = first .. " " .. words[idx]
			if M.count_cp1251_chars(candidate) <= limit then
				first = candidate
				last_ok = idx
			else
				break
			end
		end
		if last_ok < #words then
			return first, trim(table.concat(words, " ", last_ok + 1))
		end
	end

	local first = ""
	local consumed = 0
	for ch in cleaned:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
		local candidate = first .. ch
		if M.count_cp1251_chars(candidate) > limit then
			break
		end
		first = candidate
		consumed = consumed + #ch
	end
	if first == "" then
		return cleaned, nil
	end
	local second = trim(cleaned:sub(consumed + 1))
	if second == "" then
		return first, nil
	end
	return first, second
end

function M.split_fact_row_into_parts(raw_row)
	local trim = ctx.trim
	local row = trim(raw_row)
	if row == "" then
		return nil
	end

	local CapitalsEditor = ctx.CapitalsEditor
	local separator_at = row:find("||", 1, true)
	if separator_at then
		local left = trim(row:sub(1, separator_at - 1))
		local right = trim(row:sub(separator_at + 2))
		local parts = {}
		if left ~= "" then
			parts[#parts + 1] = left
		end
		if right ~= "" then
			parts[#parts + 1] = right
		end
		if #parts == 0 then
			return nil
		end
		if #parts == 1 then
			return { parts[1] }
		end
		return { parts[1], parts[2] }
	end

	if M.count_cp1251_chars(row) > CapitalsEditor.fact_row_limit then
		local first, second = M.split_text_by_limit(row, CapitalsEditor.fact_row_limit)
		local parts = {}
		if first and first ~= "" then
			parts[#parts + 1] = first
		end
		if second and second ~= "" then
			parts[#parts + 1] = second
		end
		if #parts == 0 then
			return nil
		end
		if #parts == 1 then
			return { parts[1] }
		end
		return { parts[1], parts[2] }
	end

	return { row }
end

-- === CRUD operations ===

function M.clear_inputs()
	local CapitalsEditor = ctx.CapitalsEditor
	imgui.StrCopy(CapitalsEditor.country_buf, "", CapitalsEditor.country_buf_size)
	imgui.StrCopy(CapitalsEditor.capital_buf, "", CapitalsEditor.capital_buf_size)
	imgui.StrCopy(CapitalsEditor.facts_buf, "", CapitalsEditor.facts_buf_size)
	CapitalsEditor.error = nil
end

function M.collect_entry_facts(entry)
	local trim = ctx.trim
	local facts = {}
	if type(entry) ~= "table" or type(entry.facts) ~= "table" then
		return facts
	end

	for _, fact in ipairs(entry.facts) do
		if type(fact) == "string" then
			local cleaned = trim(fact)
			if cleaned ~= "" then
				facts[#facts + 1] = cleaned
			end
		elseif type(fact) == "table" then
			local parts = {}
			for _, part in ipairs(fact) do
				local cleaned = trim(part)
				if cleaned ~= "" then
					parts[#parts + 1] = cleaned
				end
			end
			if #parts > 0 then
				facts[#facts + 1] = table.concat(parts, " ")
			end
		end
	end

	return facts
end

function M.collect_input_facts(raw_text)
	local CapitalsEditor = ctx.CapitalsEditor
	local facts = {}
	local raw = raw_text
	if type(raw) ~= "string" then
		raw = str(CapitalsEditor.facts_buf)
	end
	for line in raw:gmatch("[^\r\n]+") do
		local parts = M.split_fact_row_into_parts(line)
		if parts and #parts > 0 then
			facts[#facts + 1] = parts
		end
	end
	return facts
end

function M.serialize_entries(entries)
	local trim = ctx.trim
	local serialized = {}
	if type(entries) ~= "table" then
		return serialized
	end

	for _, entry in ipairs(entries) do
		if type(entry) == "table" then
			local country = trim(entry.country)
			local capital = trim(entry.capital)
			if country ~= "" and capital ~= "" then
				local row = { country, capital }

				if type(entry.facts) == "table" then
					for _, fact in ipairs(entry.facts) do
						if type(fact) == "string" then
							local cleaned = trim(fact)
							if cleaned ~= "" then
								row[#row + 1] = cleaned
							end
						elseif type(fact) == "table" then
							local parts = {}
							for _, part in ipairs(fact) do
								local cleaned = trim(part)
								if cleaned ~= "" then
									parts[#parts + 1] = cleaned
								end
							end
							if #parts == 1 then
								row[#row + 1] = parts[1]
							elseif #parts > 1 then
								row[#row + 1] = parts
							end
						end
					end
				end

				serialized[#serialized + 1] = row
			end
		end
	end

	return serialized
end

function M.save_entries()
	local Config = ctx.Config
	local CapitalsQuiz = ctx.CapitalsQuiz
	local data = Config.data or {}
	local capitals_cfg = type(data.capitals_quiz) == "table" and data.capitals_quiz or {}
	capitals_cfg.metropolis = M.serialize_entries(CapitalsQuiz.entries)
	data.capitals_quiz = capitals_cfg
	Config.data = data

	if #CapitalsQuiz.entries == 0 then
		CapitalsQuiz.data_error = "Таблица столиц пуста. Заполните capitals_quiz.metropolis в SMILive.json."
	else
		CapitalsQuiz.data_error = nil
	end

	Config:save()
end

function M.remove_entry(index)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local entry = CapitalsQuiz.entries and CapitalsQuiz.entries[index]
	if not entry then
		return false
	end
	local country = tostring(entry.country or "?")
	table.remove(CapitalsQuiz.entries, index)
	if CapitalsEditor.edit_index then
		if CapitalsEditor.edit_index == index then
			CapitalsEditor.edit_index = nil
			CapitalsEditor.edit_error = nil
		elseif CapitalsEditor.edit_index > index then
			CapitalsEditor.edit_index = CapitalsEditor.edit_index - 1
		end
	end
	M.save_entries()
	ctx.update_status("Удалена страна: %s.", country)
	return true
end

function M.add_entry()
	local trim = ctx.trim
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local country = trim(str(CapitalsEditor.country_buf))
	local capital = trim(str(CapitalsEditor.capital_buf))

	if country == "" then
		CapitalsEditor.error = "Введите страну."
		return false
	end

	if capital == "" then
		CapitalsEditor.error = "Введите столицу."
		return false
	end

	if M.find_entry_index_by_country(country) then
		CapitalsEditor.error = "Такая страна уже есть в таблице."
		return false
	end

	local facts = M.collect_input_facts()
	if #facts == 0 then
		CapitalsEditor.error = "Добавьте минимум один факт."
		return false
	end

	CapitalsQuiz.entries[#CapitalsQuiz.entries + 1] = {
		country = country,
		capital = capital,
		facts = facts,
	}
	M.save_entries()
	M.clear_inputs()
	ctx.update_status("Добавлена запись: %s - %s.", country, capital)
	return true
end

function M.entry_facts_to_multiline(entry)
	local trim = ctx.trim
	local lines = {}
	if type(entry) ~= "table" or type(entry.facts) ~= "table" then
		return ""
	end

	for _, fact in ipairs(entry.facts) do
		if type(fact) == "string" then
			local cleaned = trim(fact)
			if cleaned ~= "" then
				lines[#lines + 1] = cleaned
			end
		elseif type(fact) == "table" then
			local parts = {}
			for _, part in ipairs(fact) do
				local cleaned = trim(part)
				if cleaned ~= "" then
					parts[#parts + 1] = cleaned
				end
			end
			if #parts == 1 then
				lines[#lines + 1] = parts[1]
			elseif #parts > 1 then
				lines[#lines + 1] = parts[1] .. " || " .. parts[2]
			end
		end
	end

	return table.concat(lines, "\n")
end

function M.open_edit(index)
	local trim = ctx.trim
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local entry = CapitalsQuiz.entries and CapitalsQuiz.entries[index]
	if not entry then
		return false
	end

	CapitalsEditor.edit_index = index
	CapitalsEditor.edit_error = nil
	imgui.StrCopy(CapitalsEditor.edit_country_buf, trim(entry.country), CapitalsEditor.edit_country_buf_size)
	imgui.StrCopy(CapitalsEditor.edit_capital_buf, trim(entry.capital), CapitalsEditor.edit_capital_buf_size)
	imgui.StrCopy(
		CapitalsEditor.edit_facts_buf,
		M.entry_facts_to_multiline(entry),
		CapitalsEditor.edit_facts_buf_size
	)
	imgui.OpenPopup(CapitalsEditor.edit_popup_id)
	return true
end

function M.save_edit()
	local trim = ctx.trim
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local idx = CapitalsEditor.edit_index
	local entry = idx and CapitalsQuiz.entries and CapitalsQuiz.entries[idx]
	if not entry then
		CapitalsEditor.edit_error = "Запись не найдена."
		return false
	end

	local country = trim(str(CapitalsEditor.edit_country_buf))
	local capital = trim(str(CapitalsEditor.edit_capital_buf))
	local facts = M.collect_input_facts(str(CapitalsEditor.edit_facts_buf))

	if country == "" then
		CapitalsEditor.edit_error = "Введите страну."
		return false
	end
	if capital == "" then
		CapitalsEditor.edit_error = "Введите столицу."
		return false
	end
	if #facts == 0 then
		CapitalsEditor.edit_error = "Добавьте минимум один факт."
		return false
	end

	local duplicate_idx = M.find_entry_index_by_country(country)
	if duplicate_idx and duplicate_idx ~= idx then
		CapitalsEditor.edit_error = "Такая страна уже есть в таблице."
		return false
	end

	entry.country = country
	entry.capital = capital
	entry.facts = facts

	M.save_entries()
	CapitalsEditor.edit_error = nil
	CapitalsEditor.edit_index = nil
	ctx.update_status("Изменена запись: %s - %s.", country, capital)
	imgui.CloseCurrentPopup()
	return true
end

-- === UI ===

function M.draw_edit_modal()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local escape_imgui_text = ctx.escape_imgui_text
	if
		imgui.BeginPopupModal(CapitalsEditor.edit_popup_id, nil, imgui.WindowFlags.AlwaysAutoResize)
	then
		local idx = CapitalsEditor.edit_index
		local entry_exists = idx and CapitalsQuiz.entries and CapitalsQuiz.entries[idx]
		if not entry_exists then
			imgui.TextColored(imgui.ImVec4(1.0, 0.45, 0.45, 1), "Запись больше не существует.")
			if imgui.Button("Закрыть##capitals_editor_edit_close_missing") then
				CapitalsEditor.edit_index = nil
				CapitalsEditor.edit_error = nil
				imgui.CloseCurrentPopup()
			end
			imgui.EndPopup()
			return
		end

		imgui.Text("Редактирование страны")
		imgui.PushItemWidth(360)
		imgui.InputText(
			"Страна##capitals_editor_edit_country",
			CapitalsEditor.edit_country_buf,
			CapitalsEditor.edit_country_buf_size
		)
		imgui.InputText(
			"Столица##capitals_editor_edit_capital",
			CapitalsEditor.edit_capital_buf,
			CapitalsEditor.edit_capital_buf_size
		)
		imgui.TextDisabled(
			"Факты: каждая строка - отдельный факт. Если строка > 85 символов или есть ||, факт будет разбит на 2 части."
		)
		imgui.InputTextMultiline(
			"Факты##capitals_editor_edit_facts",
			CapitalsEditor.edit_facts_buf,
			CapitalsEditor.edit_facts_buf_size,
			imgui.ImVec2(0, CapitalsEditor.edit_facts_input_height)
		)
		imgui.PopItemWidth()

		if CapitalsEditor.edit_error then
			imgui.TextColored(imgui.ImVec4(1.0, 0.45, 0.45, 1), escape_imgui_text(CapitalsEditor.edit_error))
		end

		if imgui.Button("Сохранить##capitals_editor_edit_save") then
			M.save_edit()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##capitals_editor_edit_cancel") then
			CapitalsEditor.edit_index = nil
			CapitalsEditor.edit_error = nil
			imgui.CloseCurrentPopup()
		end

		imgui.EndPopup()
	end
end

function M.draw()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local CapitalsEditor = ctx.CapitalsEditor
	local escape_imgui_text = ctx.escape_imgui_text
	local count = CapitalsQuiz.entries and #CapitalsQuiz.entries or 0
	imgui.Text(string.format("Стран в таблице: %d", count))

	imgui.PushItemWidth(280)
	imgui.InputText("Страна##capitals_editor_country", CapitalsEditor.country_buf, CapitalsEditor.country_buf_size)
	imgui.InputText("Столица##capitals_editor_capital", CapitalsEditor.capital_buf, CapitalsEditor.capital_buf_size)
	imgui.PopItemWidth()

	imgui.TextDisabled(
		"Факты: каждая строка - отдельный факт. Если строка > 85 символов или есть ||, факт будет разбит на 2 части."
	)
	imgui.InputTextMultiline(
		"Факты##capitals_editor_facts",
		CapitalsEditor.facts_buf,
		CapitalsEditor.facts_buf_size,
		imgui.ImVec2(0, CapitalsEditor.facts_input_height)
	)

	if imgui.Button("Добавить страну##capitals_editor_add_country") then
		M.add_entry()
	end
	imgui.SameLine()
	if imgui.Button("Очистить форму##capitals_editor_clear_form") then
		M.clear_inputs()
	end

	if CapitalsEditor.error then
		imgui.TextColored(imgui.ImVec4(1.0, 0.45, 0.45, 1), escape_imgui_text(CapitalsEditor.error))
	end

	local preview_facts = M.collect_input_facts()
	imgui.Text(string.format("Факты для новой страны: %d", #preview_facts))
	if
		imgui.BeginChild(
			"capitals_editor_facts_preview",
			imgui.ImVec2(0, CapitalsEditor.facts_preview_height),
			true
		)
	then
		if #preview_facts == 0 then
			imgui.TextDisabled("Пока нет фактов.")
		else
			for _, parts in ipairs(preview_facts) do
				local preview = (#parts > 1) and (parts[1] .. " || " .. parts[2]) or parts[1]
				imgui.BulletText(escape_imgui_text(preview))
			end
		end
	end
	imgui.EndChild()

	imgui.Text("Записи таблицы")
	if
		imgui.BeginChild(
			"capitals_editor_entries_preview",
			imgui.ImVec2(0, CapitalsEditor.entries_preview_height),
			true,
			imgui.WindowFlags.HorizontalScrollbar
		)
	then
				imgui.Columns(4, "capitals_editor_entries_cols", true)
		imgui.Text("Страна")
		imgui.NextColumn()
		imgui.Text("Столица")
		imgui.NextColumn()
		imgui.Text("Фактов")
		imgui.NextColumn()
		imgui.Text("Действие")
		imgui.NextColumn()
		imgui.Separator()

		if count == 0 then
			imgui.TextDisabled("Таблица пустая.")
			imgui.NextColumn()
			imgui.TextDisabled("-")
			imgui.NextColumn()
			imgui.TextDisabled("0")
			imgui.NextColumn()
			imgui.TextDisabled("-")
			imgui.NextColumn()
		else
			for idx, entry in ipairs(CapitalsQuiz.entries) do
				local facts = M.collect_entry_facts(entry)
				imgui.Text(escape_imgui_text(entry.country or "-"))
				imgui.NextColumn()
				imgui.Text(escape_imgui_text(entry.capital or "-"))
				imgui.NextColumn()
				imgui.Text(tostring(#facts))
				imgui.NextColumn()
				if imgui.SmallButton("Изм.##capitals_editor_edit_" .. tostring(idx)) then
					CapitalsEditor.pending_open_edit_index = idx
				end
				imgui.SameLine()
				if imgui.SmallButton("Удалить##capitals_editor_remove_" .. tostring(idx)) then
					M.remove_entry(idx)
					break
				end
				imgui.NextColumn()
			end
		end
		imgui.Columns(1)
	end
	imgui.EndChild()
	if CapitalsEditor.pending_open_edit_index then
		local idx = CapitalsEditor.pending_open_edit_index
		CapitalsEditor.pending_open_edit_index = nil
		if not M.open_edit(idx) then
			CapitalsEditor.error = "Не удалось открыть редактор записи."
		end
	end
	M.draw_edit_modal()
end

return M
