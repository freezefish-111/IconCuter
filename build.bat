@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist ".output" mkdir ".output"

g++ -std=c++17 -O2 -municode -mwindows src\main.cpp -o .output\IconCuter.exe -luser32 -lgdi32 -lcomdlg32 -lshell32
if errorlevel 1 (
    echo [FAILED] Compile error.
    exit /b 1
)
echo [OK] Built: %cd%\.output\IconCuter.exe
