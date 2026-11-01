call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl /EHsc the_dark_lord_console.cpp resource.res user32.lib gdi32.lib gdiplus.lib advapi32.lib ole32.lib ws2_32.lib
pause