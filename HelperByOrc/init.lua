local modules = {}

modules.funcs = require("HelperByOrc.funcs")
modules.tags = require("HelperByOrc.tags")
modules.binder = require("HelperByOrc.binder")
modules.unwanted = require("HelperByOrc.unwanted")
modules.VIPandADchat = require("HelperByOrc.VIPandADchat")
modules.mimgui_funcs = require("HelperByOrc.mimgui_funcs")
modules.notepad = require("HelperByOrc.notepad")
modules.SMIHelp = require("HelperByOrc.SMIHelp")
modules.SMILive = require("HelperByOrc.SMILive")
modules.weapon_rp = require("HelperByOrc.weapon_rp")

for _, m in pairs(modules) do
	if type(m) == "table" and m.attachModules then
		m.attachModules(modules)
	end
end

function modules.loadHeavyModules()
	modules.samp = require("HelperByOrc.samp")
	modules.memory_picture = require("HelperByOrc.memory_picture")
	modules.my_hooks = require("HelperByOrc.my_hooks")

	for _, m in pairs(modules) do
		if type(m) == "table" and m.attachModules then
			m.attachModules(modules)
		end
	end
end

return modules
