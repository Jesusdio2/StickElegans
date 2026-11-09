#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR" || exit 1

echo "Compilando avatar.cpp para Windows (x86_64 / MinGW)..."

# Intento 1: Usando x86_64-w64-mingw32-g++ si está instalado (Homebrew o Linux apt)
if command -v x86_64-w64-mingw32-g++ &> /dev/null; then
    x86_64-w64-mingw32-g++ -O2 avatar.cpp \
        -o avatar.exe \
        -lws2_32 -lwininet -lopengl32 -lglu32 -luser32 -lgdi32 \
        -static
else
    # Intento 2: Usando clang++ con sysroot de mingw
    SYSROOT="/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32"
    if [ -d "$SYSROOT" ]; then
        clang++ -O2 -target x86_64-pc-windows-gnu -w \
            -fuse-ld=lld \
            --sysroot="$SYSROOT" \
            avatar.cpp \
            -o avatar.exe \
            -lws2_32 -lwininet -lopengl32 -lglu32 -luser32 -lgdi32 \
            -static
    else
        echo "Error: No se encontró un compilador MinGW (x86_64-w64-mingw32-g++ o toolchain MinGW en Homebrew)."
        exit 1
    fi
fi

if [ $? -eq 0 ]; then
    echo "¡Éxito! Generado: avatar.exe"
else
    echo "Error en la compilación de avatar.cpp."
fi

echo "Presione cualquier tecla para continuar . . ."
read -n 1 -s
