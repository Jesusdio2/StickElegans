call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl /EHsc /MT /std:c++17 llamador.cpp /Fe:llamador.exe ws2_32.lib winmm.lib
pause
