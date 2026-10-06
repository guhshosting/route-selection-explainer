@echo off
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RSE_VS=%%i"
call "%RSE_VS%\VC\Auxiliary\Build\vcvars64.bat"
type nul > artifacts\clang-tidy.log
for %%f in (src\*.cpp tests\tests.cpp) do (
  "C:\Program Files\LLVM\bin\clang-tidy.exe" %%f -p build-independent --extra-arg=-Wno-unused-command-line-argument --checks=clang-analyzer-*,bugprone-*,performance-*,portability-* --warnings-as-errors=* >> artifacts\clang-tidy.log 2>&1
  if errorlevel 1 exit /b 1
)
