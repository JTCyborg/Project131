@echo off
setlocal
title Project131 - Intel 486 / PCem build

echo ==========================================
echo   Project131 - Intel 486 / PCem build
echo ==========================================
echo.

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%build_486_pcem.ps1"

if not exist "%PS_SCRIPT%" (
    echo [ERROR] Build script not found:
    echo "%PS_SCRIPT%"
    set "BUILD_EXIT=1"
    goto :finish
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Windows PowerShell was not found.
    set "BUILD_EXIT=1"
    goto :finish
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
set "BUILD_EXIT=%ERRORLEVEL%"

if not "%BUILD_EXIT%"=="0" (
    echo.
    echo [ERROR] PCem build failed with exit code %BUILD_EXIT%.
) else (
    echo.
    echo [OK] PCem build completed successfully.
)

:finish
echo.
pause
exit /b %BUILD_EXIT%
