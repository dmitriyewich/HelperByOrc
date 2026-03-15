registerVariable("mycarid", "Мой ID транспорта", function()
    if isCharInAnyCar(PLAYER_PED) then
        local result, id = sampGetVehicleIdByCarHandle(storeCarCharIsInNoSave(PLAYER_PED))
        return result and id or "No car"
	else
		return "No car"
    end
end)