@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl "avatar.cpp" /EHsc /link ws2_32.lib user32.lib gdi32.lib wininet.lib opengl32.lib glu32.lib
pause