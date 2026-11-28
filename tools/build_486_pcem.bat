@echo off
setlocal EnableExtensions
title Project131 - Intel 486 / PCem

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_DIR=%%~fI"
set "PS_SCRIPT=%SCRIPT_DIR%build_486_pcem.ps1"
set "EXIT_CODE=0"

echo ==========================================
echo   Project131 - Intel 486 / PCem
echo ==========================================
echo.
echo   [1] Build PCem
echo   [2] Update project from Git
echo   [3] Update project and build PCem
echo   [0] Exit
echo.
choice /C 1230 /N /M "Select action [1-3, 0]: "
if errorlevel 4 goto :finish
if errorlevel 3 goto :update_then_build
if errorlevel 2 goto :update_only
if errorlevel 1 goto :build

:update_then_build
call :update_project
if errorlevel 1 (
    set "EXIT_CODE=1"
    goto :finish
)
goto :build

:update_only
call :update_project
set "EXIT_CODE=%ERRORLEVEL%"
goto :finish

:update_project
echo.
echo [GIT] Updating Project131...
where git.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Git was not found in PATH.
    exit /b 1
)
git -C "%REPO_DIR%" rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Project directory is not a Git repository:
    echo "%REPO_DIR%"
    exit /b 1
)
git -C "%REPO_DIR%" pull --ff-only
if errorlevel 1 (
    echo.
    echo [ERROR] Git update failed. Check your branch, remote and local changes.
    exit /b 1
)
echo.
echo [GIT] Project update completed.
exit /b 0

:build
echo.
echo [486/PCem] Starting build...
if not exist "%PS_SCRIPT%" (
    echo [ERROR] Build script not found:
    echo "%PS_SCRIPT%"
    set "EXIT_CODE=1"
    goto :finish
)
where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Windows PowerShell was not found.
    set "EXIT_CODE=1"
    goto :finish
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo [ERROR] PCem build failed with exit code %EXIT_CODE%.
) else (
    echo.
    echo [OK] PCem build completed successfully.
)
goto :finish

:finish
echo.
if not "%EXIT_CODE%"=="0" echo [ERROR] Operation failed with exit code %EXIT_CODE%.
pause
exit /b %EXIT_CODE%
