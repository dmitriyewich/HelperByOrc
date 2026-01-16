local module = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local new = imgui.new

local mimgui_funcs
local ok2, fa = pcall(require, "HelperByOrc.fAwesome7")
local funcs

function module.attachModules(mod)
	mimgui_funcs = mod.mimgui_funcs
	funcs = mod.funcs
end
local json_path = getWorkingDirectory() .. "\\HelperByOrc\\notepad.json"
local base_path = getWorkingDirectory() .. "\\HelperByOrc\\notepad"

local folder_icon = ok2 and fa and fa.FOLDER or ""
local file_icon = ok2 and fa and fa.FILE_LINES or ""
local note_icon = ok2 and fa and fa.BOOK or ""
local plus_icon = ok2 and fa and fa.SQUARE_PLUS or "+"
local edit_icon = ok2 and fa and fa.PEN or ""
local save_icon = ok2 and fa and fa.FLOPPY_DISK or ""
local cancel_icon = ok2 and fa and fa.XMARK or ""
local delete_icon = ok2 and fa and fa.TRASH or ""
local search_icon = ok2 and fa and fa.MAGNIFYING_GLASS or ""
local star_icon = ok2 and fa and fa.STAR or ""
local exp_icon = ok2 and fa and fa.FILE_EXPORT or ""
local copy_icon = ok2 and fa and fa.COPY or ""
local arrows_icon = ok2 and fa and fa.ARROWS_LEFT_RIGHT or "⇄"
local folder_plus_icon = ok2 and fa and fa.FOLDER_PLUS or "+"
local close_icon = ok2 and fa and fa.XMARK or "[X]"

local filter = imgui.ImGuiTextFilter()

-- ====== Case-insensitive (кириллица) ======
local utf8_lower_map = {
	["А"] = "а",
	["Б"] = "б",
	["В"] = "в",
	["Г"] = "г",
	["Д"] = "д",
	["Е"] = "е",
	["Ё"] = "ё",
	["Ж"] = "ж",
	["З"] = "з",
	["И"] = "и",
	["Й"] = "й",
	["К"] = "к",
	["Л"] = "л",
	["М"] = "м",
	["Н"] = "н",
	["О"] = "о",
	["П"] = "п",
	["Р"] = "р",
	["С"] = "с",
	["Т"] = "т",
	["У"] = "у",
	["Ф"] = "ф",
	["Х"] = "х",
	["Ц"] = "ц",
	["Ч"] = "ч",
	["Ш"] = "ш",
	["Щ"] = "щ",
	["Ъ"] = "ъ",
	["Ы"] = "ы",
	["Ь"] = "ь",
	["Э"] = "э",
	["Ю"] = "ю",
	["Я"] = "я",
}
local function string_lower(str)
	return (str or ""):gsub("[%z\1-\127\194-\244][\128-\191]*", function(c)
		return utf8_lower_map[c] or c:lower()
	end)
end
local function passFilter(str, filterRaw)
	local target = string_lower(str or "")
	filterRaw = string_lower(filterRaw or "")
	if filterRaw == "" then
		return true
	end

	local hasInclude = false
	local includeMatched = false
	for word in filterRaw:gmatch("[^,]+") do
		word = word:match("^%s*(.-)%s*$")
		if word ~= "" then
			local isExclude = word:sub(1, 1) == "-"
			local val = isExclude and word:sub(2) or word
			if val ~= "" then
				local found = target:find(val, 1, true)
				if isExclude then
					if found then
						return false
					end
				else
					hasInclude = true
					if found then
						includeMatched = true
					end
				end
			end
		end
	end

	if hasInclude then
		return includeMatched
	end

	return true
end

local txt_cache, txt_threads, tree_root, tree_flat = {}, {}, {}, {}
local notes, selectedNode, editingText, editingBufSize = {}, nil, nil, 4096
local newNoteTitle, newNoteCat = imgui.new.char[64](), imgui.new.char[128]()
local editingMode = new.bool(false)
local favorites, history = {}, {}
local draft = { text = "", idx = -1, isJson = true }
local fade_alpha = {}
local move_cat_buf = imgui.new.char[128]()
local popup_move = new.bool(false)
local popup_warning = new.bool(false)
local popup_rename = new.bool(false)
local rename_buf = imgui.new.char[128]()
local popup_newfolder = new.bool(false)
local newfolder_buf = imgui.new.char[128]()
local current_folder = nil
local scrollToLine = nil

local function saveNotes()
	funcs.saveTableToJson(notes, json_path)
end
local function loadNotes()
	notes = funcs.loadTableFromJson(json_path, {})
	favorites, history = {}, {}
	for i, note in ipairs(notes) do
		note._fav = note._fav or false
		note._ctime = note._ctime or os.time()
		note._mtime = note._mtime or os.time()
	end
end
local function getAllTxtFilesRecursive(path)
	local result = {}
	local function scan(current, rel)
		local handle, file = findFirstFile(current .. "\\*")
		while file do
			if file ~= "." and file ~= ".." then
				local full = current .. "\\" .. file
				local sub, subfile = findFirstFile(full .. "\\*")
				if subfile then
					findClose(sub)
					scan(full, rel .. file .. "\\")
				elseif file:lower():match("%.txt$") then
					table.insert(result, rel .. file)
				end
			end
			file = findNextFile(handle)
		end
		findClose(handle)
	end
	scan(path, "")
	return result
end
local function loadTreeFromFiles()
	txt_cache = {}
	tree_flat = getAllTxtFilesRecursive(base_path)
	tree_root = {}
	for _, path in ipairs(tree_flat) do
		local node = tree_root
		for part in path:gmatch("([^\\]+)") do
			if not node[part] then
				node[part] = {}
			end
			node = node[part]
		end
		node.__is_file = path
	end
end
local function readTxtFile(path, callback)
	if txt_cache[path] then
		return callback(txt_cache[path])
	end
	local full = base_path .. "\\" .. path
	local attr = io.open(full, "rb")
	if attr then
		attr:seek("end")
		local size = attr:seek()
		attr:close()
		if size > 1024 * 1024 then
			if not txt_threads[path] then
				txt_threads[path] = lua_thread.create(function(full, path)
					local f = io.open(full, "r")
					local c = f and f:read("*a") or ""
					if f then
						f:close()
					end
					txt_cache[path] = c
				end, full, path)
			end
			return callback("[Загрузка большого файла...]")
		end
	end
	local f = io.open(full, "r")
	local content = ""
	if f then
		content = f:read("*a")
		f:close()
	end
	txt_cache[path] = content or ""
	callback(txt_cache[path])
end

local max_history = 7
local function addToHistory(idx)
	for i = #history, 1, -1 do
		if history[i] == idx then
			table.remove(history, i)
		end
	end
	table.insert(history, 1, idx)
	if #history > max_history then
		table.remove(history)
	end
end
local function isDuplicateTitle(title)
	for i, n in ipairs(notes) do
		if n.title == title then
			return true
		end
	end
	return false
end
local function createNote()
	local title = ffi.string(newNoteTitle)
	local cat = ffi.string(newNoteCat)
	if title == "" then
		return
	end
	if isDuplicateTitle(title) then
		imgui.OpenPopup("Дубликат")
		return
	end
	table.insert(
		notes,
		{ title = title, category = cat, text = "", _fav = false, _ctime = os.time(), _mtime = os.time() }
	)
	saveNotes()
	selectedNode = { isJson = true, idx = #notes }
	editingBufSize = math.max(4096, 2048)
	editingText = imgui.new.char[editingBufSize]("")
	imgui.StrCopy(newNoteTitle, "")
	imgui.StrCopy(newNoteCat, "")
	fade_alpha[#notes] = 0
end
local function duplicateNote(idx)
	local note = notes[idx]
	if note then
		local t = note.title .. "_копия"
		table.insert(notes, {
			title = t,
			category = note.category,
			text = note.text,
			_fav = false,
			_ctime = os.time(),
			_mtime = os.time(),
		})
		saveNotes()
	end
end
local function fadeAlpha(idx, speed, isActive)
	fade_alpha[idx] = fade_alpha[idx] or (isActive and 1 or 0.15)
	local delta = imgui.GetIO().DeltaTime or 0.017
	if isActive then
		fade_alpha[idx] = math.min(fade_alpha[idx] + speed * delta * 60, 1)
	else
		fade_alpha[idx] = math.max(fade_alpha[idx] - speed * delta * 60, 0.15)
	end
	return fade_alpha[idx]
end
local function exportNote(note)
	local name = note.title:gsub("[^%w%d_%-]", "_")
	-- local name = u8:decode(name)
	local out = base_path .. "\\export_" .. name .. ".txt"
	local f = io.open(out, "w+b")
	if f then
		f:write(note.text or "")
		f:close()
	end
end

ffi.cdef([[
int MultiByteToWideChar(unsigned int cp, unsigned long fl, const char* src, int cb, wchar_t* dst, int cch);
void* GlobalAlloc(unsigned int uFlags, size_t dwBytes);
void* GlobalLock(void* hMem);
int   GlobalUnlock(void* hMem);
int   OpenClipboard(void* hWndNewOwner);
int   CloseClipboard(void);
int   EmptyClipboard(void);
void* SetClipboardData(unsigned int uFormat, void* hMem);
enum { GMEM_MOVEABLE = 0x0002, CF_UNICODETEXT = 13 };
]])

function setClipboardTextUTF8(text)
	local str = text
	local len = ffi.C.MultiByteToWideChar(65001, 0, str, #str, nil, 0)
	if len < 1 then
		return false
	end
	local hMem = ffi.C.GlobalAlloc(2, (len + 1) * 2)
	local wstr = ffi.cast("wchar_t*", ffi.C.GlobalLock(hMem))
	ffi.C.MultiByteToWideChar(65001, 0, str, #str, wstr, len)
	wstr[len] = 0
	ffi.C.GlobalUnlock(hMem)
	if ffi.C.OpenClipboard(nil) == 0 then
		return false
	end
	ffi.C.EmptyClipboard()
	ffi.C.SetClipboardData(13, hMem)
	ffi.C.CloseClipboard()
	return true
end

local function copyToClipboard(str)
	setClipboardTextUTF8(str)
end

-- === Word-wrap+clipper ===
local function split_text_wrapped(text)
	local result = {}
	local pFont = imgui.GetFont()
	local scale = 1.0
	local avail = imgui.GetContentRegionAvail().x

	local start = 1
	while start <= #text do
		local line_end = text:find("\n", start) or (#text + 1)
		local line = text:sub(start, line_end - 1)
		local ffi_line = ffi.new("char[?]", #line + 1, line)
		local text_ptr = ffi_line
		local text_end = text_ptr + ffi.sizeof(ffi_line) - 1

		while text_ptr < text_end do
			local endPrevLine = pFont:CalcWordWrapPositionA(scale, text_ptr, text_end, avail)
			local len = tonumber(ffi.cast("intptr_t", endPrevLine) - ffi.cast("intptr_t", text_ptr))
			if len <= 0 then
				break
			end
			local wrapped = ffi.string(text_ptr, len)
			table.insert(result, wrapped)
			text_ptr = endPrevLine
			if text_ptr[0] == 32 then
				text_ptr = text_ptr + 1
			end
		end

		start = line_end + 1
		if line_end <= #text then
			table.insert(result, "")
		end
	end
	return result
end
local function showLongTextClipper(text)
	local wrapped_lines = split_text_wrapped(text)
	local count = #wrapped_lines
	local clipper = imgui.ImGuiListClipper(count)
	while clipper:Step() do
		for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
			imgui.TextUnformatted(wrapped_lines[i])
		end
	end
end

local function renderTree(node, prefix, filterRaw, draw_cb)
	prefix = prefix or ""
	for name, sub in pairs(node) do
		if name ~= "__is_file" then
			local id = prefix .. name
			if sub.__is_file then
				draw_cb(sub.__is_file, id)
			else
				if imgui.TreeNodeExStr(folder_icon .. " " .. u8(name), 0) then
					renderTree(sub, id .. "\\", filterRaw, draw_cb)
					imgui.TreePop()
				end
			end
		end
	end
end

-- ========== Глобальный поиск по заметкам и txt ===========
local function searchAllNotesAndTxt(filterRaw)
	if not filterRaw or filterRaw == "" then
		return {}
	end
	local results = {}
	for i, note in ipairs(notes) do
		if passFilter(note.title, filterRaw) then
			table.insert(
				results,
				{ idx = i, line_idx = 0, line = "", is_title = true, category = note.category, isJson = true }
			)
		end
		local line_num = 1
		for line in (note.text or ""):gmatch("([^\n]*)\n?") do
			if passFilter(line, filterRaw) then
				table.insert(
					results,
					{ idx = i, line_idx = line_num, line = line, is_title = false, category = note.category, isJson = true }
				)
			end
			line_num = line_num + 1
		end
	end
	for _, path in ipairs(tree_flat) do
		local name = path:match("([^\\]+)$")
		if passFilter(name, filterRaw) then
			table.insert(results, { path = path, line_idx = 0, line = "", is_title = true, isJson = false })
		end
		local content = txt_cache[path]
		if content then
			local line_num = 1
			for line in content:gmatch("([^\n]*)\n?") do
				if passFilter(line, filterRaw) then
					table.insert(
						results,
						{ path = path, line_idx = line_num, line = line, is_title = false, isJson = false }
					)
				end
				line_num = line_num + 1
			end
		end
	end
	return results
end

local function drawCat(cat, arr, star)
	local tree_open = imgui.TreeNodeExStr(folder_icon .. " " .. cat .. (star and (" " .. star_icon) or ""), 0)
	if imgui.BeginPopupContextItem(cat .. "popupcat") then
		if imgui.MenuItemBool("Переименовать папку") then
			imgui.StrCopy(rename_buf, cat)
			popup_rename[0] = true
			current_folder = cat
		end
		if imgui.MenuItemBool("Удалить все заметки в папке") then
			for i = #notes, 1, -1 do
				if notes[i].category == cat then
					table.remove(notes, i)
				end
			end
			saveNotes()
		end
		if imgui.MenuItemBool("Создать вложенную папку") then
			imgui.StrCopy(newfolder_buf, "")
			popup_newfolder[0] = true
			current_folder = cat
		end
		imgui.EndPopup()
	end
	if tree_open then
		local clipper2 = imgui.ImGuiListClipper(#arr)
		while clipper2:Step() do
			for j = clipper2.DisplayStart + 1, clipper2.DisplayEnd do
				local v = arr[j]
				local isSel = selectedNode and selectedNode.isJson and selectedNode.idx == v.idx
				local alpha = fadeAlpha(v.idx, 0.13, isSel)
				imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
				if isSel then
					imgui.PushStyleColor(imgui.Col.Header, imgui.ImVec4(0.2, 0.52, 1, 1))
				end
				imgui.SetCursorPosY(imgui.GetCursorPosY() + 2)
				if
					imgui.Selectable(
						(star and star_icon or note_icon or "") .. " " .. v.note.title,
						isSel,
						0,
						imgui.ImVec2(0, imgui.GetFontSize() + 8)
					)
				then
					selectedNode = { isJson = true, idx = v.idx }
					editingBufSize = math.max(4096, #(v.note.text or "") + 2048)
					editingText = imgui.new.char[editingBufSize](v.note.text or "")
				end
				if imgui.BeginPopupContextItem("note" .. v.idx) then
					if imgui.MenuItemBool("Переименовать") then
						imgui.StrCopy(rename_buf, v.note.title)
						popup_rename[0] = true
						current_folder = nil
						selectedNode = { isJson = true, idx = v.idx }
					end
					if imgui.MenuItemBool("Удалить") then
						table.remove(notes, v.idx)
						saveNotes()
					end
					if imgui.MenuItemBool("Дублировать") then
						duplicateNote(v.idx)
					end
					if imgui.MenuItemBool("Экспорт") then
						exportNote(v.note)
					end
					if
						imgui.MenuItemBool(
							v.note._fav and "Убрать из избранного" or "В избранное"
						)
					then
						v.note._fav = not v.note._fav
						saveNotes()
					end
					imgui.EndPopup()
				end
				imgui.PopStyleVar()
				if isSel then
					imgui.PopStyleColor()
				end
			end
		end
		imgui.TreePop()
	end
end

local function drawLeftPanel()
	imgui.BeginChild("notepad_list", imgui.ImVec2(288, 0), true)
	if imgui.Button((folder_plus_icon or "+") .. " Создать папку") then
		imgui.StrCopy(newfolder_buf, "")
		popup_newfolder[0] = true
		current_folder = nil
	end
	imgui.SameLine()
	imgui.TextDisabled("ПКМ: папка — меню, заметка — меню")

	imgui.Text((plus_icon or "+") .. " Новая заметка")
	imgui.InputTextWithHint("##ntitle", "Заголовок", newNoteTitle, ffi.sizeof(newNoteTitle))
	imgui.InputTextWithHint("##ncat", "Категория/папка", newNoteCat, ffi.sizeof(newNoteCat))
	imgui.SameLine()
	if imgui.Button((plus_icon or "+") .. "##addnote", imgui.ImVec2(28, 22)) then
		createNote()
	end
	imgui.Spacing()
	imgui.Separator()
	imgui.PushItemWidth(-38)
	filter:Draw((search_icon or "") .. " Поиск...", 190)
	imgui.SameLine()
	if filter:IsActive() then
		if imgui.Button(close_icon or "[X]", imgui.ImVec2(25, 0)) then
			filter:Clear()
		end
	end
	imgui.PopItemWidth()
	imgui.Spacing()
	if star_icon ~= "" then
		imgui.Text(star_icon .. " Последние")
	else
		imgui.Text("Последние")
	end
	local fixed_height = imgui.GetFontSize() + 8
	local clipper = imgui.ImGuiListClipper(#history)
	while clipper:Step() do
		for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
			local idx = history[i]
			local note = notes[idx]
			if note and passFilter(note.title .. " " .. note.text, ffi.string(filter.InputBuf)) then
				local isSel = selectedNode and selectedNode.isJson and selectedNode.idx == idx
				local alpha = fadeAlpha(idx, 0.18, isSel)
				imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
				if isSel then
					imgui.PushStyleColor(imgui.Col.Header, imgui.ImVec4(0.16, 0.41, 0.78, 1))
				end
				imgui.SetCursorPosY(imgui.GetCursorPosY() + 2)
				if
					imgui.Selectable((note_icon or "") .. " " .. note.title, isSel, 0, imgui.ImVec2(0, fixed_height))
				then
					selectedNode = { isJson = true, idx = idx }
					editingBufSize = math.max(4096, #(note.text or "") + 2048)
					editingText = imgui.new.char[editingBufSize](note.text or "")
				end
				if imgui.BeginPopupContextItem("note" .. idx) then
					if imgui.MenuItemBool("Переименовать") then
						imgui.StrCopy(rename_buf, note.title)
						popup_rename[0] = true
						current_folder = nil
						selectedNode = { isJson = true, idx = idx }
					end
					if imgui.MenuItemBool("Удалить") then
						table.remove(notes, idx)
						saveNotes()
					end
					if imgui.MenuItemBool("Дублировать") then
						duplicateNote(idx)
					end
					if imgui.MenuItemBool("Экспорт") then
						exportNote(note)
					end
					if
						imgui.MenuItemBool(
							note._fav and "Убрать из избранного" or "В избранное"
						)
					then
						note._fav = not note._fav
						saveNotes()
					end
					imgui.EndPopup()
				end
				if isSel then
					imgui.PopStyleColor()
				end
				imgui.PopStyleVar()
			end
		end
	end
	imgui.Spacing()
	local categories, pinned = {}, {}
	for i, note in ipairs(notes) do
		if not passFilter(note.title .. " " .. note.text, ffi.string(filter.InputBuf)) then
			goto continue
		end
		local cat = note.category or "Без категории"
		if note._fav then
			pinned[cat] = pinned[cat] or {}
			table.insert(pinned[cat], { note = note, idx = i })
		else
			categories[cat] = categories[cat] or {}
			table.insert(categories[cat], { note = note, idx = i })
		end
		::continue::
	end
	for cat, arr in pairs(pinned) do
		drawCat(cat, arr, true)
	end
	for cat, arr in pairs(categories) do
		drawCat(cat, arr, false)
	end
	imgui.Spacing()
	imgui.Separator()
	imgui.Text((folder_icon or "") .. " Импортированные .txt")
	renderTree(tree_root, "", nil, function(path, id)
		local name = path:match("([^\\]+)$")
		local name = u8(name)
		readTxtFile(path, function(content)
			if passFilter(name .. " " .. (content or ""), ffi.string(filter.InputBuf)) then
				local isSel = selectedNode and not selectedNode.isJson and selectedNode.path == path
				if imgui.Selectable((file_icon or "") .. " " .. name, isSel, 0, imgui.ImVec2(0, fixed_height)) then
					selectedNode = { isJson = false, path = path, idx = -1 }
					editingBufSize = math.max(4096, #(content or "") + 2048)
					editingText = imgui.new.char[editingBufSize](content or "")
				end
			end
		end)
	end)
	imgui.EndChild()
end

local function getDynamicTextHeight(txt)
	local lines = 0
	for _ in txt:gmatch("[^\n]*\n?") do
		lines = lines + 1
	end
	return math.max(120, math.min(800, lines * 22))
end

local function drawSearchResults(results)
	imgui.BeginChild("notepad_search", imgui.ImVec2(0, 0), true)
	imgui.TextDisabled(("Найдено: %d совпадений"):format(#results))
	imgui.Separator()
	for i, res in ipairs(results) do
		if res.isJson then
			local note = notes[res.idx]
			local cat = note and (note.category or "Без категории") or ""
			local title = note and note.title or "?"
			local title = u8(title)
			imgui.PushIDInt(i)
			if res.is_title then
				if imgui.Selectable(string.format("%s  [Заголовок]  [%s]", title, cat), false) then
					-- selectedNode = {isJson=true, idx=res.idx}
					-- editingBufSize = math.max(4096, #(note.text or "") + 2048)
					-- editingText = imgui.new.char[editingBufSize](note.text or "")
					-- editingMode[0] = false
					-- scrollToLine = 0
				end
			else
				local label = string.format("%s [%s]  строка %d:  %s", title, cat, res.line_idx, res.line)
				if imgui.Selectable(label, false) then
					copyToClipboard(res.line)
					-- selectedNode = {isJson=true, idx=res.idx}
					-- editingBufSize = math.max(4096, #(note.text or "") + 2048)
					-- editingText = imgui.new.char[editingBufSize](note.text or "")
					-- editingMode[0] = false
					-- scrollToLine = res.line_idx
				end
			end
			imgui.PopID()
		else
			local path = res.path
			local name = path:match("([^\\]+)$")
			local name = u8(name)
			local cat = path:match("^(.-)[^\\]+$") or ""
			imgui.PushIDInt(i)
			if res.is_title then
				if imgui.Selectable(string.format("%s [файл: %s]", name, cat), false) then
					-- readTxtFile(path, function(content)
					-- selectedNode = {isJson=false, path=path, idx=-1}
					-- editingBufSize = math.max(4096, #(content or "") + 2048)
					-- editingText = imgui.new.char[editingBufSize](content or "")
					-- editingMode[0] = false
					-- scrollToLine = 0
					-- end)
				end
			else
				local label = string.format("%s (строка %d): %s", name, res.line_idx, res.line)
				if imgui.Selectable(label, false) then
					copyToClipboard(res.line)
					-- readTxtFile(path, function(content)
					-- selectedNode = {isJson=false, path=path, idx=-1}
					-- editingBufSize = math.max(4096, #(content or "") + 2048)
					-- editingText = imgui.new.char[editingBufSize](content or "")
					-- editingMode[0] = false
					-- scrollToLine = res.line_idx
					-- end)
				end
			end
			imgui.PopID()
		end
	end
	imgui.EndChild()
end

local function drawRightPanel()
	imgui.BeginChild("notepad_content", imgui.ImVec2(0, 0), true)
	local filterRaw = ffi.string(filter.InputBuf)
	if filterRaw ~= "" then
		local search_results = searchAllNotesAndTxt(filterRaw)
		drawSearchResults(search_results)
	elseif not selectedNode then
		imgui.TextColored(imgui.ImVec4(0.6, 0.7, 0.9, 1), "Выберите или создайте заметку.")
	elseif selectedNode.isJson then
		local idx = selectedNode.idx
		local note = notes[idx]
		if note then
			addToHistory(idx)
			imgui.Text(note.title .. "  ")
			imgui.SameLine()
			if note._fav then
				imgui.TextColored(imgui.ImVec4(1, 1, 0.1, 1), star_icon)
				imgui.SameLine()
			end
			if imgui.SmallButton(star_icon .. (note._fav and " Открепить" or " Закрепить")) then
				note._fav = not note._fav
				saveNotes()
			end
			imgui.SameLine()
			if imgui.SmallButton(exp_icon .. " Экспорт") then
				exportNote(note)
			end
			imgui.SameLine()
			if imgui.SmallButton((copy_icon or "") .. " Скопировать") then
				copyToClipboard(note.text or "")
			end
			imgui.SameLine()
			if imgui.SmallButton((arrows_icon or "⇄") .. " Переместить") then
				imgui.StrCopy(move_cat_buf, note.category or "")
				popup_move[0] = true
				imgui.OpenPopup("moveNote")
			end
			imgui.SameLine()
			imgui.TextDisabled(
				os.date("Создано: %d.%m.%Y %H:%M", note._ctime)
					.. " | "
					.. os.date("Изменено: %d.%m.%Y %H:%M", note._mtime)
			)
			imgui.Spacing()
			if not editingMode[0] then
				imgui.Separator()
				if scrollToLine and scrollToLine > 0 then
					for i = 1, scrollToLine - 1 do
						imgui.TextUnformatted("")
					end
					scrollToLine = nil
				end
				showLongTextClipper(note.text or "")
				imgui.Spacing()
				imgui.TextDisabled(("Символов: %d"):format(#(note.text or "")))
				if imgui.Button((edit_icon or "") .. " Редактировать") then
					editingMode[0] = true
					editingBufSize = math.max(4096, #(note.text or "") + 2048)
					editingText = imgui.new.char[editingBufSize](note.text or "")
				end
			else
				imgui.Separator()
				local savePressed = imgui.Button((save_icon or "") .. " Сохранить   (Ctrl+S)")
					or (imgui.IsKeyDown(0x11) and imgui.IsKeyPressed(0x53))
				if savePressed then
					local len = #ffi.string(editingText)
					if len > 65535 then
						popup_warning[0] = true
						imgui.OpenPopup("longTextWarn")
					else
						note.text = ffi.string(editingText)
						note._mtime = os.time()
						saveNotes()
						editingMode[0] = false
					end
				end
				imgui.SameLine()
				if imgui.Button((cancel_icon or "") .. " Отмена") then
					editingMode[0] = false
				end
				imgui.SameLine()
				if imgui.Button((delete_icon or "") .. " Удалить") then
					table.remove(notes, idx)
					saveNotes()
					selectedNode = nil
					editingMode[0] = false
					return
				end
				imgui.InputTextMultiline(
					"##editnotetext",
					editingText,
					editingBufSize,
					imgui.ImVec2(-1, getDynamicTextHeight(ffi.string(editingText)))
				)
				imgui.TextDisabled(("Символов: %d"):format(#ffi.string(editingText)))
			end
		end
	else
		local name = selectedNode.path:match("([^\\]+)$")
		local name = u8(name)
		local content = ffi.string(editingText)
		imgui.Text(name)
		imgui.SameLine()
		if imgui.SmallButton(exp_icon .. " Экспорт") then
			exportNote({ title = name, text = content })
		end
		imgui.Separator()
		if not editingMode[0] then
			if scrollToLine and scrollToLine > 0 then
				for i = 1, scrollToLine - 1 do
					imgui.TextUnformatted("")
				end
				scrollToLine = nil
			end
			showLongTextClipper(content)
			imgui.Spacing()
			imgui.TextDisabled(("Символов: %d"):format(#content))
			if imgui.Button((edit_icon or "") .. " Редактировать") then
				editingMode[0] = true
			end
		else
			imgui.Separator()
			if imgui.Button((save_icon or "") .. " Сохранить как новую") then
				table.insert(
					notes,
					{ title = name, category = "Импорт", text = content, _fav = false, _ctime = os.time(), _mtime = os.time() }
				)
				saveNotes()
				editingMode[0] = false
			end
			imgui.SameLine()
			if imgui.Button((cancel_icon or "") .. " Отмена") then
				editingMode[0] = false
				editingText = imgui.new.char[#content + 2048](content)
			end
			imgui.InputTextMultiline(
				"##editimptxt",
				editingText,
				editingBufSize,
				imgui.ImVec2(-1, getDynamicTextHeight(ffi.string(editingText)))
			)
			imgui.TextDisabled(("Символов: %d"):format(#ffi.string(editingText)))
		end
	end

	if imgui.BeginPopup("moveNote") then
		imgui.InputTextWithHint("##movecat", "Новая категория", move_cat_buf, ffi.sizeof(move_cat_buf))
		if imgui.Button("OK##movecat") then
			local idx = selectedNode and selectedNode.idx
			if idx and notes[idx] then
				notes[idx].category = ffi.string(move_cat_buf)
				saveNotes()
			end
			imgui.CloseCurrentPopup()
			popup_move[0] = false
		end
		imgui.SameLine()
		if imgui.Button("Отмена##movecat") then
			imgui.CloseCurrentPopup()
			popup_move[0] = false
		end
		imgui.EndPopup()
	end
	if imgui.BeginPopup("longTextWarn") then
		imgui.TextColored(
			imgui.ImVec4(1, 0.45, 0.2, 1),
			"Слишком длинный текст!\nОграничение: 65535 символов."
		)
		if imgui.Button("OK##longwarn") then
			imgui.CloseCurrentPopup()
			popup_warning[0] = false
		end
		imgui.EndPopup()
	end
	if popup_rename[0] then
		imgui.OpenPopup("RenamePopup")
		popup_rename[0] = false
	end
	if imgui.BeginPopupModal("RenamePopup", nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.InputTextWithHint("##rename", "Новое имя", rename_buf, ffi.sizeof(rename_buf))
		if imgui.Button("OK##rename") then
			local new_name = ffi.string(rename_buf)
			if current_folder then
				for i, note in ipairs(notes) do
					if note.category == current_folder then
						note.category = new_name
					end
				end
			else
				if selectedNode and selectedNode.idx then
					notes[selectedNode.idx].title = new_name
				end
			end
			saveNotes()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##rename") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
	if popup_newfolder[0] then
		imgui.OpenPopup("NewFolderPopup")
		popup_newfolder[0] = false
	end
	if imgui.BeginPopupModal("NewFolderPopup", nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.InputTextWithHint("##newfolder", "Имя папки", newfolder_buf, ffi.sizeof(newfolder_buf))
		if imgui.Button("OK##newfolder") then
			local new_cat = ffi.string(newfolder_buf)
			if current_folder then
				new_cat = current_folder .. "/" .. new_cat
			end
			table.insert(
				notes,
				{
					title = "Новая заметка",
					category = new_cat,
					text = "",
					_fav = false,
					_ctime = os.time(),
					_mtime = os.time(),
				}
			)
			saveNotes()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##newfolder") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
	imgui.EndChild()
end

function module.drawNotepadPanel()
	imgui.BeginChild("notepad_main", imgui.ImVec2(0, 0), false)
	drawLeftPanel()
	imgui.SameLine()
	drawRightPanel()
	imgui.EndChild()
	if imgui.BeginPopup("Дубликат") then
		imgui.Text("Заметка с таким заголовком уже есть!")
		if imgui.Button("OK") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

imgui.OnInitialize(function()
	loadNotes()
	loadTreeFromFiles()
end)

return module
