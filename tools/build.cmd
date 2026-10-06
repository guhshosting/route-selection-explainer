@echo off
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RSE_VS=%%i"
if not defined RSE_VS exit /b 1
call "%RSE_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRSE_ANALYZE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 exit /b 1
cmake --build build-debug
