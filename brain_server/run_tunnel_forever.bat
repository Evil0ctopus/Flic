@echo off
setlocal

if "%~1"=="" (
  echo Usage: run_tunnel_forever.bat ^<tunnel-name^>
  echo Example: run_tunnel_forever.bat flic-brain
  exit /b 1
)

set TUNNEL_NAME=%~1

:loop
echo [%date% %time%] Starting cloudflared tunnel: %TUNNEL_NAME%
cloudflared tunnel run %TUNNEL_NAME%
echo [%date% %time%] cloudflared exited with code %errorlevel%. Restarting in 5 seconds...
timeout /t 5 /nobreak >nul
goto loop
