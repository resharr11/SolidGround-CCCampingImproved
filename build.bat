@echo off
setlocal
rem Solid Ground Camping (CC Camping Improved) SKSE plugin: builds SolidGroundCamping.dll (build\Release). If the MO2 mod folder below exists, the DLL is also copied there.
rem Stands on its own (2026-11-10): camp code, CommonLibSSE and the vcpkg port all live in this folder.
rem First run downloads and builds the libraries (vcpkg); that takes a while. Later runs are fast.
set "HERE=%~dp0"
rem Edit MOD to point at your own mod folder if you want the DLL copied after each build.
set "MOD=%LOCALAPPDATA%\ModOrganizer\Skyrim Special Edition\mods\Solid Ground Camping"
rem never take a ready-built library from vcpkg's shared cache: one built elsewhere still holds that folder's path
set "VCPKG_BINARY_SOURCES=clear"
set "LOG=%HERE%build_log.txt"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio with C++ tools was not found.> "%LOG%"
  type "%LOG%"
  pause
  exit /b 1
)
call "%VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if not defined VCPKG_ROOT set "VCPKG_ROOT=%VS%\VC\vcpkg"
echo Visual Studio: %VS%> "%LOG%"
echo VCPKG_ROOT: %VCPKG_ROOT%>> "%LOG%"
where git >> "%LOG%" 2>&1
cmake --version >> "%LOG%" 2>&1

if not exist "%HERE%extern\CommonLibSSE\CMakeLists.txt" (
  if not exist "%HERE%extern" mkdir "%HERE%extern"
  tar -xf "%HERE%CommonLibSSE.zip" -C "%HERE%extern" >> "%LOG%" 2>&1
)
if not exist "%HERE%extern\CommonLibSSE\CMakeLists.txt" (
  echo CommonLibSSE could not be unpacked from CommonLibSSE.zip.>> "%LOG%"
  goto fail
)

cd /d "%HERE%"
rem one-time reset of the CMake cache when the build settings changed (2026-10-31: options that keep folder paths out of
rem the DLL; 2026-11-10: CommonLibSSE and the camp code moved into this project)
if not exist "%HERE%build\standalone.flag" (
  if exist "%HERE%build\CMakeCache.txt" del /q "%HERE%build\CMakeCache.txt"
  if exist "%HERE%build\CMakeFiles" rmdir /s /q "%HERE%build\CMakeFiles"
  if exist "%HERE%build\Release" rmdir /s /q "%HERE%build\Release"
  if not exist "%HERE%build" mkdir "%HERE%build"
  echo standalone> "%HERE%build\standalone.flag"
)
echo Configuring (first time: downloading and building libraries, please wait)...
cmake --preset release >> "%LOG%" 2>&1 || goto fail
echo Building...
cmake --build --preset release >> "%LOG%" 2>&1 || goto fail

if exist "%MOD%" (
  if not exist "%MOD%\SKSE\Plugins" mkdir "%MOD%\SKSE\Plugins"
  copy /y "%HERE%build\Release\SolidGroundCamping.dll" "%MOD%\SKSE\Plugins\SolidGroundCamping.dll" >> "%LOG%" 2>&1 || goto fail
)
echo BUILD OK>> "%LOG%"
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%check_paths.ps1" -Files "%HERE%build\Release\SolidGroundCamping.dll" > "%HERE%path_check.txt" 2>&1
set "PATHS=%ERRORLEVEL%"
type "%HERE%path_check.txt" >> "%LOG%"
echo ============================================================
echo  BUILD OK - build\Release\SolidGroundCamping.dll
if "%PATHS%"=="0" (
  echo  Path check: clean, no folder paths or user name in the DLL.
) else (
  echo  Path check: FOLDER PATHS LEFT IN THE DLL - DO NOT RELEASE. See path_check.txt.
)
echo ============================================================
pause
exit /b 0

:fail
echo BUILD FAILED>> "%LOG%"
echo.
echo BUILD FAILED - see build_log.txt.
pause
exit /b 1
