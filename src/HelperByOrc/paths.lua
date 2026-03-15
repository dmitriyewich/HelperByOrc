local module = {}

local cached_project_root = nil
local cached_resource_root = nil

local function normalize(path)
	path = tostring(path or "")
	path = path:gsub("/", "\\")
	path = path:gsub("\\+", "\\")
	return path
end

local function is_absolute(path)
	return path:match("^%a:[\\]") ~= nil or path:sub(1, 2) == "\\\\"
end

local function trim_separators(path)
	path = normalize(path)
	path = path:gsub("^\\+", "")
	path = path:gsub("\\+$", "")
	return path
end

local function file_exists(path)
	local f = io.open(path, "rb")
	if f then
		f:close()
		return true
	end
	return false
end

local function dir_exists(path)
	if type(doesDirectoryExist) == "function" then
		return doesDirectoryExist(path)
	end
	return false
end

local function push_unique(list, value)
	value = normalize(value)
	if value == "" then
		return
	end
	for i = 1, #list do
		if list[i] == value then
			return
		end
	end
	list[#list + 1] = value
end

function module.normalize(path)
	return normalize(path)
end

function module.join(...)
	local parts = { ... }
	local result = ""

	for i = 1, #parts do
		local raw = tostring(parts[i] or "")
		if raw ~= "" then
			local current = normalize(raw)
			if result == "" or is_absolute(current) then
				result = current
			else
				local left = result:gsub("\\+$", "")
				local right = current:gsub("^\\+", "")
				if right ~= "" then
					result = left .. "\\" .. right
				else
					result = left
				end
			end
		end
	end

	return normalize(result)
end

function module.projectRoot()
	if cached_project_root then
		return cached_project_root
	end

	local candidates = {}
	local root = nil
	if type(getMoonloaderDirectory) == "function" then
		push_unique(candidates, getMoonloaderDirectory())
	end
	if type(getWorkingDirectory) == "function" then
		push_unique(candidates, getWorkingDirectory())
	end
	push_unique(candidates, "moonloader")

	for i = 1, #candidates do
		local candidate = candidates[i]
		if dir_exists(module.join(candidate, "HelperByOrc", "resource"))
			or dir_exists(module.join(candidate, "HelperByOrc")) then
			root = candidate
			break
		end
	end

	if not root then
		root = candidates[1] or "moonloader"
	end

	cached_project_root = normalize(root)
	return cached_project_root
end

function module.dataRoot()
	return module.join(module.projectRoot(), "HelperByOrc")
end

function module.resourceRoot()
	if cached_resource_root then
		return cached_resource_root
	end

	cached_resource_root = module.join(module.projectRoot(), "HelperByOrc", "resource")
	return cached_resource_root
end

function module.dataPath(relative_path)
	relative_path = trim_separators(relative_path)
	if relative_path == "" then
		return module.dataRoot()
	end
	return module.join(module.dataRoot(), relative_path)
end

function module.resourcePath(relative_path)
	relative_path = trim_separators(relative_path)
	if relative_path == "" then
		return module.resourceRoot()
	end
	return module.join(module.resourceRoot(), relative_path)
end

function module.remapLegacyDataPath(path)
	path = normalize(path)
	local lower = path:lower()
	local legacy_prefix = "moonloader\\helperbyorc\\"
	if lower:sub(1, #legacy_prefix) == legacy_prefix then
		local relative = path:sub(#legacy_prefix + 1)
		return module.dataPath(relative)
	end
	return path
end

function module.findExistingResourceFile(file_name)
	file_name = trim_separators(file_name)
	if file_name == "" then
		return nil
	end

	return module.resourcePath(file_name)
end

return module
