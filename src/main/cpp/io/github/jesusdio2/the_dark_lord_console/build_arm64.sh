#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE}" )" && pwd )"
cd "$DIR"

export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
SYSROOT="/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32"

echo "Compilando recursos e imágenes para ARM64 Windows..."
llvm-rc resource.rc /FO resource_mac_arm64.res

if [ ! -f resource_mac_arm64.res ]; then
    echo "Error: No se pudo generar el archivo de recursos resource_mac_arm64.res"
    echo "Presione cualquier tecla para continuar . . ."
    read -n 1 -s
    exit 1
fi

echo "Compilando the_dark_lord_console.cpp para Windows .exe (ARM64 Nativo)..."
GCC_VER=$(basename $(ls -d /opt/homebrew/opt/mingw-w64/toolchain-x86_64/lib/gcc/x86_64-w64-mingw32/* 2>/dev/null | head -n 1))

# Añadimos las rutas de librerías específicas de MinGW para que lld encuentre libgcc si las pide
GCC_LIB_DIR="/opt/homebrew/opt/mingw-w64/toolchain-x86_64/lib/gcc/x86_64-w64-mingw32/$GCC_VER"

clang++ -O2 -target aarch64-pc-windows-gnu -w \
    -fuse-ld=lld \
    --sysroot="$SYSROOT" \
    -I "$SYSROOT/include/c++/$GCC_VER" \
    -I "$SYSROOT/include/c++/$GCC_VER/x86_64-w64-mingw32" \
    -L "$GCC_LIB_DIR" \
    -L "$SYSROOT/lib" \
    the_dark_lord_console.cpp resource_mac_arm64.res \
    -o the_dark_lord_console_arm64.exe \
    -luser32 -lgdi32 -lgdiplus -ladvapi32 -lole32 -lws2_32 \
    -static

if [ $? -eq 0 ]; then
    echo "¡Éxito total! Generado ejecutable Windows ARM64 Puro: the_dark_lord_console_arm64.exe"
else
    echo "Error en la compilación ARM64."
fi

echo "Presione cualquier tecla para continuar . . ."
read -n 1 -s
