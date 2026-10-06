@echo off
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RSE_VS=%%i"
if not defined RSE_VS exit /b 1
call "%RSE_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts mkdir artifacts
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DRSE_ANALYZE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > artifacts\release-configure.log 2>&1
if errorlevel 1 exit /b 1
cmake --build build-release > artifacts\release-build.log 2>&1
if errorlevel 1 exit /b 1
ctest --test-dir build-release --output-on-failure > artifacts\release-ctest.log 2>&1
if errorlevel 1 exit /b 1
cmake -S . -B build-independent -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > artifacts\independent-configure.log 2>&1
if errorlevel 1 exit /b 1
cmake --build build-independent --clean-first > artifacts\independent-build.log 2>&1
if errorlevel 1 exit /b 1
ctest --test-dir build-independent --output-on-failure > artifacts\independent-ctest.log 2>&1
if errorlevel 1 exit /b 1
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRSE_ASAN=ON > artifacts\asan-configure.log 2>&1
if errorlevel 1 exit /b 1
cmake --build build-asan > artifacts\asan-build.log 2>&1
if errorlevel 1 exit /b 1
copy /y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" build-asan\ > artifacts\asan-runtime.log 2>&1
if errorlevel 1 exit /b 1
ctest --test-dir build-asan --output-on-failure > artifacts\asan-ctest.log 2>&1
if errorlevel 1 exit /b 1
cmake --install build-release --prefix install > artifacts\install.log 2>&1
if errorlevel 1 exit /b 1
install\bin\route-selection-explainer.exe --version >> artifacts\install.log 2>&1
if errorlevel 1 exit /b 1
dumpbin /imports build-release\route-selection-explainer.exe > artifacts\imports.log
node tools\audit.js > artifacts\network-audit.log 2>&1
if errorlevel 1 exit /b 1
"C:\Program Files\Cppcheck\cppcheck.exe" --enable=warning,style,performance,portability --std=c++17 --platform=win64 --inline-suppr --error-exitcode=1 -D_WIN32 -DUNICODE -D_UNICODE -DNOMINMAX -Iinclude src tests\tests.cpp > artifacts\cppcheck.log 2>&1
if errorlevel 1 exit /b 1
type nul > artifacts\clang-tidy.log
for %%f in (src\*.cpp tests\tests.cpp) do (
  "C:\Program Files\LLVM\bin\clang-tidy.exe" %%f -p build-independent --extra-arg=-Wno-unused-command-line-argument --checks=clang-analyzer-*,bugprone-*,performance-*,portability-* --warnings-as-errors=* >> artifacts\clang-tidy.log 2>&1
  if errorlevel 1 exit /b 1
)
