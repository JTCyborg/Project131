@echo off
setlocal
cd /d "%~dp0.."

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Windows PowerShell was not found.
    echo Install Windows PowerShell 5.1 or run tools\build_486_pcem.ps1 manually.
    pause
    exit /b 1
)

echo ==========================================
echo   Project131 - Intel 486 / PCem build
echo ==========================================
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_486_pcem.ps1" %*
set "BUILD_EXIT=%ERRORLEVEL%"

echo.
if not "%BUILD_EXIT%"=="0" (
    echo [ERROR] PCem build failed with exit code %BUILD_EXIT%.
) else (
    echo [OK] PCem build completed.
)

pause
exit /b %BUILD_EXIT%
