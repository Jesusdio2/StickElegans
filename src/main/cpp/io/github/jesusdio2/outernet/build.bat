@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl "Hes In Trouble.cpp" /EHsc /link ws2_32.lib user32.lib gdi32.lib wininet.lib opengl32.lib glu32.lib gdiplus.lib advapi32.lib ole32.lib
pause