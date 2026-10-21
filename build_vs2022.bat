@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer / vswhere.exe was not found.
  echo Install Visual Studio 2022 with "Desktop development with C++".
  pause
  exit /b 1
)

set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo [ERROR] Visual Studio 2022 is installed, but the MSVC C++ x86/x64 tools were not found.
  echo Open Visual Studio Installer - Modify - enable "Desktop development with C++".
  pause
  exit /b 2
)

set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
  echo [ERROR] MSBuild.exe was not found under:
  echo %VSROOT%
  pause
  exit /b 3
)

echo Visual Studio: %VSROOT%
echo Building KichRay Material Lab - Release / Win32...
"%MSBUILD%" "%~dp0KichRay_MaterialLab.sln" /m /t:Build /p:Configuration=Release /p:Platform=Win32 /v:minimal
if errorlevel 1 (
  echo.
  echo [ERROR] Build failed. Copy the error lines above and send them to me.
  pause
  exit /b 4
)

set "EXE=%~dp0bin\Release\Win32\MaterialLab.exe"
if not exist "%EXE%" (
  echo [ERROR] Build reported success but MaterialLab.exe was not found:
  echo %EXE%
  pause
  exit /b 5
)

echo.
echo [OK] Build complete:
echo %EXE%
start "" "%EXE%"
endlocal
