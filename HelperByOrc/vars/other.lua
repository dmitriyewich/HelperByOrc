registerFunctionalVariable('save_dialog', 'Сохранить текст активного диалога', function(param)
			if sampIsDialogActive() then
				local path = getGameDirectory() .. "\\moonloader\\HelperByOrc\\file.txt"
				local full_text = ""
				local file_read = io.open(path, "r")
				if file_read then
					full_text = file_read:read("*a") or ""
					file_read:close()
				end
				local file_write = io.open(path, "w")
				if not file_write then
					return "Не удалось открыть file.txt для записи"
				end
				local new_text = string.format(
					"%s\n-------------------\n-------------------\n-------------------\n[%s]%s\n%s",
					full_text,
					sampGetCurrentDialogId(),
					sampGetDialogCaption(),
					sampGetDialogText()
				)
				file_write:write(new_text)
				file_write:close()
				return ""
			else
				return "Нет активного диалого"
			end
end, { example = '[save_dialog()]' })

registerVariable("tphoto", "Скриншот как из фотоаппарта, сохранится в GTA San Andreas User Files Gallery", function()
    takePhoto()
	return "Фото сделано"
end)
