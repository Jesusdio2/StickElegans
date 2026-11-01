@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl the_dark_lord_console.cpp resource.res user32.lib gdi32.lib gdiplus.lib /link /STUB:tu_programa_dos.exe /OUT:the_dark_lord_console.com
pause
