@echo off
REM build_msvc.bat — Build with Visual Studio + vcpkg.
REM
REM Prereqs (one-time setup):
REM   1. Install Visual Studio 2022 with "Desktop development with C++" workload.
REM   2. Install vcpkg:
REM        git clone https://github.com/microsoft/vcpkg C:\vcpkg
REM        C:\vcpkg\bootstrap-vcpkg.bat
REM        C:\vcpkg\vcpkg integrate install
REM        C:\vcpkg\vcpkg install glfw3:x64-windows glew:x64-windows
REM
REM Then open "x64 Native Tools Command Prompt for VS 2022" in this folder
REM and run:   build_msvc.bat
REM
REM Adjust VCPKG_ROOT below if vcpkg lives elsewhere.

setlocal
set VCPKG_ROOT=C:\vcpkg
set INCLUDES=/I "%VCPKG_ROOT%\installed\x64-windows\include"
set LIBDIRS=/LIBPATH:"%VCPKG_ROOT%\installed\x64-windows\lib"

cl /std:c++17 /O2 /EHsc /nologo ^
   %INCLUDES% ^
   main.cpp camera.cpp recorder.cpp overlay.cpp input.cpp ^
   /Fe:feedback.exe ^
   /link %LIBDIRS% ^
         glfw3dll.lib glew32.lib opengl32.lib gdi32.lib ^
         mfplat.lib mfreadwrite.lib mf.lib mfuuid.lib ole32.lib oleaut32.lib ^
         winmm.lib zlib.lib

REM Copy the GLFW and GLEW DLLs next to the exe so the app can launch.
copy /Y "%VCPKG_ROOT%\installed\x64-windows\bin\glfw3.dll" . >nul
copy /Y "%VCPKG_ROOT%\installed\x64-windows\bin\glew32.dll" . >nul

echo.
echo Built feedback.exe.  Run: feedback.exe
endlocal
