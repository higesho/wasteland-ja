@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist ..\..\build mkdir ..\..\build
if not exist ..\..\build\patch.bin echo.>..\..\build\patch.bin
rc /nologo /fo ..\..\build\wl_jp.res wl_jp.rc || exit /b 1
cl /nologo /O2 /MT /W3 /utf-8 /LD /D_CRT_SECURE_NO_WARNINGS /Fo..\..\build\ proxy.c wl_jp.c testio.c ..\..\build\wl_jp.res /Fe..\..\build\winmm.dll /link /DEF:winmm.def kernel32.lib user32.lib || exit /b 1
