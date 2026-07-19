@echo off
setlocal enabledelayedexpansion

:: parse args
set NOBUILD=0
for %%A in (%*) do (
    set "arg=%%A"
    if /i "!arg!"=="nobuild" set NOBUILD=1
)

:: SET PATHS HERE!!
set "ROOT=%~dp0.."
set "GODOT=C:\Users\keithu\Documents\github\gtaeditor\bin\godot\Godot_v4.7.1-stable_win64.exe"
set "PROJECT=%ROOT%\project"

:: verify if godot exists
if not exist "%GODOT%" (
    echo ERROR: Not found godot executable at %GODOT%
    echo Please place the godot binary there.
    pause
    exit /b 1
)

:: compile if its on building mode
if "%NOBUILD%"=="0" (
    echo Compiling extension...
    cd /d "%ROOT%"
    scons platform=windows target=template_debug
    if errorlevel 1 (
        echo ERROR: Failed to compile
        pause
        exit /b 1
    )
)

:: open godot
echo Opening project in godot...
"%GODOT%" --path "%PROJECT%" --editor
