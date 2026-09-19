#!/usr/bin/env python3

from future import annotations

import math
import subprocess
import sys
import threading
from pathlib import Path

class C302Adapter:

    def __init__(self):
        self.activo = False
        self.ejecutando = False
        self.error = None

    # --------------------------------------------------
    # SALIDAS HACIA EL CUERPO
    # --------------------------------------------------

    self.izquierda = 0.0
    self.derecha = 0.0
    self.brazo_izquierdo = 0.0
    self.brazo_derecho = 0.0
    self.impulso_vertical = 0.0

    # --------------------------------------------------
    # TIEMPO
    # --------------------------------------------------

    self.tiempo = 0.0

    # --------------------------------------------------
    # DATOS DE C302
    # --------------------------------------------------

    self.neuronas = {}
    self.musculos = {}

    self._datos_musculares = []
    self._columnas_musculares = []
    self._indice_tiempo = 0
    self._tiempo_datos = []
    self._duracion_datos = 0.0

    # --------------------------------------------------
    # RUTAS
    # --------------------------------------------------

    self.c302_dir = Path(
        r"C:\Users\Usuario\Downloads\c302-master\c302-master"
    )

    self.examples_dir = self.c302_dir / "examples"

    self.lems_file = (
        self.examples_dir /
        "LEMS_c302_A_Full.xml"
    )

    self.neuron_file = (
        self.examples_dir /
        "c302_A_Full.dat"
    )

    self.muscle_file = (
        self.examples_dir /
        "c302_A_Full.muscles.dat"
    )

    # --------------------------------------------------
    # PROCESO
    # --------------------------------------------------

    self.proceso = None

    self._comprobar_instalacion()

# ======================================================
# COMPROBAR INSTALACIÓN
# ======================================================

def _comprobar_instalacion(self):
    print()
    print("=" * 70)
    print("StickElegans - comprobando OpenWorm / C302")
    print("=" * 70)

    if not self.c302_dir.exists():
        self.error = (
            "No existe el directorio de C302:\n"
            f"{self.c302_dir}"
        )

        print("[C302] ERROR:")
        print(self.error)
        return

    print("[C302] Directorio encontrado:")
    print("       ", self.c302_dir)

    if not self.examples_dir.exists():
        self.error = (
            "No existe el directorio examples:\n"
            f"{self.examples_dir}"
        )

        print("[C302] ERROR:")
        print(self.error)
        return

    print("[C302] examples encontrado:")
    print("       ", self.examples_dir)

    if not self.lems_file.exists():
        self.error = (
            "No se encontró:\n"
            f"{self.lems_file}"
        )

        print("[C302] ERROR:")
        print(self.error)
        return

    print("[C302] Modelo LEMS encontrado:")
    print("       ", self.lems_file)

    print()
    print("[C302] Instalación preparada.")
    print("=" * 70)
    print()

# ======================================================
# EJECUTAR
# ======================================================

def ejecutar(self):
    if self.error is not None:
        print(
            "[C302] No se puede ejecutar porque "
            "la instalación no está preparada."
        )
        return False

    if self.ejecutando:
        print("[C302] Ya existe una simulación en ejecución.")
        return False

    self.ejecutando = True

    hilo = threading.Thread(
        target=self._ejecutar_hilo,
        daemon=True
    )

    hilo.start()

    return True

# ======================================================
# HILO
# ======================================================

def _ejecutar_hilo(self):
    try:
        print()
        print("=" * 70)
        print("StickElegans - ejecutando C302")
        print("=" * 70)

        comando = [
            sys.executable,
            "-m",
            "pyneuroml.pynml",
            "-nogui",
            "LEMS_c302_A_Full.xml",
        ]

        print("[C302] Python utilizado:")
        print("       ", sys.executable)

        print()
        print("[C302] Comando:")
        print("       ", " ".join(comando))

        print()
        print("[C302] Directorio:")
        print("       ", self.examples_dir)

        print()
        print("[C302] Ejecutando jNeuroML sin GUI...")

        self.proceso = subprocess.run(
            comando,
            cwd=str(self.examples_dir),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

        # --------------------------------------------------
        # ERROR
        # --------------------------------------------------

        if self.proceso.returncode != 0:
            self.error = (
                "jNeuroML terminó con código "
                f"{self.proceso.returncode}"
            )

            print()
            print("[C302] ERROR")
            print(self.error)

            if self.proceso.stdout:
                print()
                print("--------------- STDOUT ---------------")
                print(self.proceso.stdout[-10000:])

            if self.proceso.stderr:
                print()
                print("--------------- STDERR ---------------")
                print(self.proceso.stderr[-10000:])

            self.activo = False
            return

        # --------------------------------------------------
        # CORRECTO
        # --------------------------------------------------

        print()
        print("[C302] jNeuroML terminó correctamente.")

        self._comprobar_resultados()

    except FileNotFoundError as e:
        self.error = (
            "No fue posible ejecutar Python "
            "para iniciar pyNeuroML."
        )

        print()
        print("[C302] ERROR:")
        print(self.error)
        print(e)

        self.activo = False

    except Exception as e:
        self.error = str(e)

        print()
        print("[C302] ERROR inesperado:")
        print(e)

        self.activo = False

    finally:
        self.proceso = None
        self.ejecutando = False

        print()
        print("=" * 70)
        print()

# ======================================================
# COMPROBAR RESULTADOS
# ======================================================

def _comprobar_resultados(self):
    print()
    print("[C302] Comprobando archivos generados...")

    neuron_ok = self.neuron_file.exists()
    muscle_ok = self.muscle_file.exists()

    if neuron_ok:
        tamano = self.neuron_file.stat().st_size

        print("[C302] OK - actividad neuronal:")
        print("       ", self.neuron_file)
        print(
            "       Tamaño:",
            f"{tamano:,}",
            "bytes"
        )
    else:
        print("[C302] ADVERTENCIA:")
        print("       No apareció:", self.neuron_file)

    if muscle_ok:
        tamano = self.muscle_file.stat().st_size

        print("[C302] OK - actividad muscular:")
        print("       ", self.muscle_file)
        print(
            "       Tamaño:",
            f"{tamano:,}",
            "bytes"
        )
    else:
        print("[C302] ADVERTENCIA:")
        print("       No apareció:", self.muscle_file)

    if muscle_ok:
        if self._cargar_datos_musculares():
            self.activo = True

            print()
            print("=" * 70)
            print("[C302] ACTIVIDAD MUSCULAR CARGADA")
            print("=" * 70)

            print(
                "[C302] Muestras:",
                len(self._datos_musculares)
            )

            print(
                "[C302] Columnas:",
                len(self._columnas_musculares)
            )

            print(
                "[C302] Duración:",
                f"{self._duracion_datos:.4f}"
            )

            print()
            print(
                "[C302] El Ragdoll comenzará a recibir "
                "actividad muscular real."
            )

            print("=" * 70)
        else:
            self.activo = False

            print()
            print(
                "[C302] Se encontró el archivo muscular, "
                "pero no pudo interpretarse."
            )

    elif neuron_ok:
        self.activo = True

        print()
        print("=" * 70)
        print(
            "[C302] ACTIVIDAD NEURONAL ENCONTRADA"
        )
        print("=" * 70)

        print(
            "[C302] Todavía no se utilizará directamente "
            "para controlar el cuerpo."
        )

    else:
        self.activo = False

        print()
        print(
            "[C302] C302 terminó, pero no se "
            "encontraron archivos de salida."
        )

# ======================================================
# CARGAR DATOS MUSCULARES
# ======================================================

def _cargar_datos_musculares(self):
    """
    Lee el archivo .muscles.dat.

    Se intenta detectar automáticamente la cabecera.
    """

    try:
        with self.muscle_file.open(
            "r",
            encoding="utf-8",
            errors="replace"
        ) as archivo:

            lineas = archivo.readlines()

    except Exception as e:
        print("[C302] Error leyendo músculos:", e)
        return False

    filas = []

    for linea in lineas:
        linea = linea.strip()

        if not linea:
            continue

        if linea.startswith("#"):
            continue

        partes = linea.replace(",", " ").split()

        valores = []

        valido = True

        for parte in partes:
            try:
                valores.append(float(parte))
            except ValueError:
                valido = False
                break

        if valido and valores:
            filas.append(valores)

    if not filas:
        return False

    # --------------------------------------------------
    # DETECTAR CABECERA
    # --------------------------------------------------

    nombres = self._buscar_cabecera(lineas)

    cantidad_columnas = len(filas[0])

    if nombres is None:
        print(
            "[C302] No se encontró una cabecera "
            "explícita en el archivo muscular."
        )

        # Si no conocemos los nombres, no inventamos
        # el significado de las columnas.
        return False

    if len(nombres) != cantidad_columnas:
        print(
            "[C302] La cabecera contiene",
            len(nombres),
            "columnas, pero los datos contienen",
            cantidad_columnas
        )

        return False

    self._columnas_musculares = nombres
    self._datos_musculares = filas

    # --------------------------------------------------
    # TIEMPO
    # --------------------------------------------------

    self._indice_tiempo = self._detectar_columna_tiempo(
        nombres
    )

    self._tiempo_datos = [
        fila[self._indice_tiempo]
        for fila in filas
    ]

    if self._tiempo_datos:
        self._duracion_datos = (
            self._tiempo_datos[-1]
            -
            self._tiempo_datos[0]
        )

    print()
    print(
        "[C302] Columnas musculares detectadas:"
    )

    for nombre in nombres:
        print("       ", nombre)

    return True

# ======================================================
# CABECERA
# ======================================================

def _buscar_cabecera(self, lineas):
    """
    Busca una línea de cabecera.

    Acepta formatos del tipo:

        time MDL MDR MVL MVR

    o:

        time    MDL    MDR    MVL    MVR
    """

    for linea in lineas:
        original = linea.strip()

        if not original:
            continue

        texto = original.lstrip("#").strip()

        partes = texto.replace(",", " ").split()

        if not partes:
            continue

        cantidad_numericos = 0

        for parte in partes:
            try:
                float(parte)
                cantidad_numericos += 1
            except ValueError:
                pass

        # Una cabecera debe contener nombres.
        if cantidad_numericos == 0 and len(partes) >= 2:
            return partes

    return None

# ======================================================
# COLUMNA DE TIEMPO
# ======================================================

def _detectar_columna_tiempo(self, nombres):
    for i, nombre in enumerate(nombres):
        nombre_limpio = nombre.lower()

        if (
            "time" in nombre_limpio
            or nombre_limpio == "t"
        ):
            return i

    return 0

# ======================================================
# BUSCAR COLUMNA
# ======================================================

def _buscar_columnas(self, nombres, patrones):
    encontradas = []

    for i, nombre in enumerate(nombres):
        limpio = nombre.upper()

        for patron in patrones:
            if patron.upper() in limpio:
                encontradas.append(i)
                break

    return encontradas

# ======================================================
# VALOR MUSCULAR
# ======================================================

def _valor_columna(self, fila, indices):
    if not indices:
        return 0.0

    valores = []

    for indice in indices:
        if indice >= len(fila):
            continue

        valor = fila[indice]

        if math.isfinite(valor):
            valores.append(valor)

    if not valores:
        return 0.0

    return max(valores)

# ======================================================
# NORMALIZAR
# ======================================================

def _normalizar_valor(self, valor):
    """
    Convierte la actividad muscular en un rango 0..1.

    Se utiliza únicamente sobre los datos reales de C302.
    """

    if not math.isfinite(valor):
        return 0.0

    # Muchos archivos de actividad muscular están
    # expresados alrededor de valores negativos/positivos.
    #
    # Esta transformación evita mandar directamente
    # voltajes a la física.

    if valor <= -80.0:
        return 0.0

    if valor >= -20.0:
        return 1.0

    resultado = (
        (valor + 80.0)
        /
        60.0
    )

    return max(
        0.0,
        min(1.0, resultado)
    )

# ======================================================
# ACTIVIDAD ACTUAL
# ======================================================

def _obtener_fila_actual(self):
    if not self._datos_musculares:
        return None

    if not self._tiempo_datos:
        return self._datos_musculares[
            self._indice_tiempo
        ]

    objetivo = (
        self._tiempo_datos[0]
        +
        self.tiempo
    )

    # El archivo ya terminó.
    if objetivo > self._tiempo_datos[-1]:
        objetivo = self._tiempo_datos[-1]

    # Buscar la muestra correspondiente.
    indice = self._indice_tiempo

    for i, tiempo in enumerate(self._tiempo_datos):
        if tiempo >= objetivo:
            indice = i
            break

    return self._datos_musculares[indice]

# ======================================================
# ACTUALIZAR
# ======================================================

def actualizar(
    self,
    dt,
    distancia_mouse,
    velocidad_mouse
):
    self.tiempo += dt

    # Mientras C302 está calculando:
    if not self.activo:
        self.izquierda = 0.0
        self.derecha = 0.0
        self.brazo_izquierdo = 0.0
        self.brazo_derecho = 0.0
        self.impulso_vertical = 0.0
        return

    fila = self._obtener_fila_actual()

    if fila is None:
        return

    nombres = self._columnas_musculares

    # --------------------------------------------------
    # MÚSCULOS DEL CUERPO DE C. ELEGANS
    #
    # Buscamos MDL/MDR/MVL/MVR.
    # --------------------------------------------------

    mdl = self._buscar_columnas(
        nombres,
        ["MDL"]
    )

    mdr = self._buscar_columnas(
        nombres,
        ["MDR"]
    )

    mvl = self._buscar_columnas(
        nombres,
        ["MVL"]
    )

    mvr = self._buscar_columnas(
        nombres,
        ["MVR"]
    )

    dorsal_izq = self._valor_columna(
        fila,
        mdl
    )

    dorsal_der = self._valor_columna(
        fila,
        mdr
    )

    ventral_izq = self._valor_columna(
        fila,
        mvl
    )

    ventral_der = self._valor_columna(
        fila,
        mvr
    )

    # --------------------------------------------------
    # ACTIVIDAD REAL → ACTIVACIÓN
    # --------------------------------------------------

    izquierda_dorsal = self._normalizar_valor(
        dorsal_izq
    )

    derecha_dorsal = self._normalizar_valor(
        dorsal_der
    )

    izquierda_ventral = self._normalizar_valor(
        ventral_izq
    )

    derecha_ventral = self._normalizar_valor(
        ventral_der
    )

    # --------------------------------------------------
    # CONTROL LATERAL
    #
    # No se genera ningún patrón artificial.
    # El valor procede de las muestras de C302.
    # --------------------------------------------------

    self.izquierda = (
        izquierda_dorsal
        +
        izquierda_ventral
    ) * 0.5

    self.derecha = (
        derecha_dorsal
        +
        derecha_ventral
    ) * 0.5

    # --------------------------------------------------
    # BRAZOS
    #
    # Para esta primera etapa usamos la actividad
    # muscular lateral disponible como señal corporal.
    # --------------------------------------------------

    self.brazo_izquierdo = self.izquierda
    self.brazo_derecho = self.derecha

    # --------------------------------------------------
    # IMPULSO VERTICAL
    #
    # No se inventa un salto. Se mantiene desactivado
    # hasta que tengamos una correspondencia muscular
    # suficiente para justificarlo.
    # --------------------------------------------------

    self.impulso_vertical = 0.0

# ======================================================
# CERRAR
# ======================================================

def cerrar(self):
    if self.proceso is not None:
        try:
            if self.proceso.poll() is None:
                self.proceso.terminate()
        except Exception:
            pass

    self.proceso = None