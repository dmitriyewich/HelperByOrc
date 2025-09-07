local modules = {}

modules.funcs          = require('HelperByOrc.funcs')
modules.tags           = require('HelperByOrc.tags')
modules.binder         = require('HelperByOrc.binder')
modules.unwanted       = require('HelperByOrc.unwanted')
modules.VIPandADchat   = require('HelperByOrc.VIPandADchat')
modules.samp           = require('HelperByOrc.samp')
modules.memory_picture = require('HelperByOrc.memory_picture')
modules.mimgui_funcs   = require('HelperByOrc.mimgui_funcs')
modules.my_hooks       = require('HelperByOrc.my_hooks')
modules.notepad        = require('HelperByOrc.notepad')
modules.SMIHelp        = require('HelperByOrc.SMIHelp')

for _, m in pairs(modules) do
    if type(m) == 'table' and m.attachModules then
        m.attachModules(modules)
    end
end

return modules
