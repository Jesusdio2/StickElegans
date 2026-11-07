#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE}" )" && pwd )"
cd "$DIR"

echo "Compilando recursos e imágenes para Win32..."
# Compila el archivo de recursos .rc a un formato compatible con MinGW 32-bit
i686-w64-mingw32-windres resource.rc -O coff -o resource_mac_32.res

echo "Compilando the_dark_lord_console.cpp para Windows Win32 (32-bit)..."
i686-w64-mingw32-g++ -O2 the_dark_lord_console.cpp resource_mac_32.res \
    -o the_dark_lord_console_32.exe \
    -luser32 -lgdi32 -lgdiplus -ladvapi32 -lole32 -lws2_32 \
    -static

if [ $? -eq 0 ]; then
    echo "¡Éxito! Generado con imágenes: the_dark_lord_console_32.exe"
else
    echo "Error en la compilación Win32."
fi

echo "Presione cualquier tecla para continuar . . ."
read -n 1 -s
