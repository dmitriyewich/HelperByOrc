@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%fix-mojibake.ps1"

if not exist "%PS_SCRIPT%" (
	echo [ERROR] Missing script: "%PS_SCRIPT%"
	exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
	echo [ERROR] Conversion failed. Exit code: %EXIT_CODE%
)

exit /b %EXIT_CODE%
