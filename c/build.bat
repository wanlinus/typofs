@echo off
REM typofs one-click build for MSVC (Windows SDK only).
REM Auto-locates vcvars64.bat so you can run it from any shell (no need for the
REM dedicated "x64 Native Tools" prompt first).
setlocal
cd /d "%~dp0"

set "VCVARS="
for %%p in (
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VCVARS if exist "%%~p" set "VCVARS=%%~p"

if not defined VCVARS (
  echo [ERROR] vcvars64.bat not found. Install Visual Studio 2022/2019 C++ workload, or use CMake.
  exit /b 1
)

REM Load MSVC (cl.exe, link.exe) into this shell.
call "%VCVARS%" >nul

cl /utf-8 /std:c11 /O2 main.c /Fe:typofs.exe winhttp.lib bcrypt.lib
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)

echo.
echo [OK] output: %~dp0typofs.exe
echo     Put config.ini next to the exe to use it.
