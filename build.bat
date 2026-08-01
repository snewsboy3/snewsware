@echo off
setlocal
where cmake >nul 2>nul || (echo CMake is required.& exit /b 1)
where cl >nul 2>nul || (echo Run this from an x64 Native Tools Command Prompt for Visual Studio 2022.& exit /b 1)
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
copy /Y build\Release\snewsware.exe .\snewsware.exe >nul
echo Built: %CD%\snewsware.exe
