call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl /EHsc /O2 /GS /guard:cf /MD the_dark_lord_console.cpp resource.res user32.lib gdi32.lib gdiplus.lib advapi32.lib ole32.lib ws2_32.lib /link /DYNAMICBASE /NXCOMPAT
pause