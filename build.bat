@echo off
rem Builds sotn-native in WSL: runs build.py in this folder, then opens out\.
rem Options go to build.py: build.bat --pc-only, --switch-only, --clean, --install-deps.
rem Another distribution: set SOTN_WSL_DISTRO=<name> before build.bat.
setlocal
set "HERE=%~dp0"
where wsl >nul 2>nul || goto nowsl
if defined SOTN_WSL_DISTRO goto run
for %%D in (Ubuntu-24.04 Ubuntu Ubuntu-22.04) do (
    wsl -d %%D -- true >nul 2>nul && set "SOTN_WSL_DISTRO=%%D" && goto run
)
goto nowsl

:run
echo Building in WSL (%SOTN_WSL_DISTRO%): %HERE%
wsl -d %SOTN_WSL_DISTRO% --cd "%HERE%." python3 build.py %*
set "RC=%ERRORLEVEL%"
if exist "%HERE%out\" start "" explorer "%HERE%out"
exit /b %RC%

:nowsl
echo WSL 2 with Ubuntu is not installed. Install it from an administrator PowerShell:
echo     wsl --install -d Ubuntu-24.04
echo Restart Windows, open Ubuntu once to make a user, then run build.bat again.
exit /b 1
