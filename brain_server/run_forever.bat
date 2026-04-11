@echo off
setlocal enabledelayedexpansion

cd /d %~dp0

if not exist ".venv\Scripts\python.exe" (
  echo Virtual environment missing in brain_server\.venv
  echo Create it with: py -3 -m venv .venv
  exit /b 1
)

:loop
echo [%date% %time%] Starting Brain Server...
call .venv\Scripts\activate.bat
set BRAIN_CONFIG=%cd%\config.json
python server.py
echo [%date% %time%] Brain Server exited with code %errorlevel%. Restarting in 5 seconds...
timeout /t 5 /nobreak >nul
goto loop
