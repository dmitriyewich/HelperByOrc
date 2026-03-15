registerVariable("obs_start", "Начинает запись OBS", function()
	local obs = import('obs.lua')
	if obs then return obs.obs_start_recording() else return 'OBS включен/нет скрипта' end
end)
registerVariable("obs_stop", "Заканчивает запись OBS", function()
	local obs = import('obs.lua')
	if obs then return obs.obs_stop_recording() else return 'OBS включен/нет скрипта' end
end)
registerVariable("obs_save_replay", "Сохраняет повтор OBS", function()
	local obs = import('obs.lua')
	if obs then return obs.obs_save_replay() else return 'OBS включен/нет скрипта' end
end)