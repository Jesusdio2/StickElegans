#!/usr/bin/env python3
from __future__ import annotations

import sys
import math
import ctypes
import subprocess
import threading
from pathlib import Path
from ctypes import wintypes

from PyQt6.QtCore import Qt, QTimer, QPoint
from PyQt6.QtGui import QPainter, QColor, QPen, QCursor
from PyQt6.QtWidgets import QApplication, QWidget


# ============================================================
# C302 / OPENWORM
# ============================================================

class C302Adapter:

    """
    Adaptador inicial de StickElegans para OpenWorm/c302.

    IMPORTANTE:

    Esta clase NO inventa una red neuronal.
    NO crea h.Section().
    NO crea neuronas HH artificiales.
    NO utiliza senos para simular neuronas.

    Ejecuta el modelo C302 real mediante:

        pyNeuroML
            ↓
        jNeuroML
            ↓
        LEMS_c302_A_Full.xml
            ↓
        c302_A_Full.dat
        c302_A_Full.muscles.dat

    En esta primera etapa solamente comprobamos que C302
    puede ejecutarse desde StickElegans.

    Las salidas motoras permanecen en 0 hasta que tengamos
    comprobado el formato real de los archivos de salida.
    """

    def __init__(self):

        # ----------------------------------------------------
        # ESTADO
        # ----------------------------------------------------

        self.activo = False
        self.ejecutando = False
        self.error = None

        # ----------------------------------------------------
        # SALIDAS MOTORAS
        #
        # Se mantienen en cero durante esta primera prueba.
        # ----------------------------------------------------

        self.izquierda = 0.0
        self.derecha = 0.0

        self.brazo_izquierdo = 0.0
        self.brazo_derecho = 0.0

        self.impulso_vertical = 0.0

        # ----------------------------------------------------
        # TIEMPO
        # ----------------------------------------------------

        self.tiempo = 0.0

        # ----------------------------------------------------
        # ACTIVIDAD C302
        #
        # Todavía no interpretamos los .dat.
        # ----------------------------------------------------

        self.neuronas = {}
        self.musculos = {}

        # ----------------------------------------------------
        # RUTAS DE C302
        # ----------------------------------------------------

        self.c302_dir = Path(
            r"C:\Users\Usuario\Downloads\c302-master\c302-master"
        )

        self.examples_dir = (
            self.c302_dir /
            "examples"
        )

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

        # ----------------------------------------------------
        # PROCESO
        # ----------------------------------------------------

        self.proceso = None

        # ----------------------------------------------------
        # COMPROBAR
        # ----------------------------------------------------

        self._comprobar_instalacion()

    # ========================================================
    # COMPROBAR INSTALACIÓN
    # ========================================================

    def _comprobar_instalacion(self):

        print()
        print("=" * 70)
        print("StickElegans - comprobando OpenWorm / C302")
        print("=" * 70)

        # ----------------------------------------------------
        # DIRECTORIO
        # ----------------------------------------------------

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

        # ----------------------------------------------------
        # EXAMPLES
        # ----------------------------------------------------

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

        # ----------------------------------------------------
        # LEMS
        # ----------------------------------------------------

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

        # ----------------------------------------------------
        # PREPARADO
        # ----------------------------------------------------

        print()
        print("[C302] Instalación preparada.")
        print("=" * 70)
        print()

    # ========================================================
    # EJECUTAR C302
    # ========================================================

    def ejecutar(self):

        if self.error is not None:

            print(
                "[C302] No se puede ejecutar porque "
                "la instalación no está preparada."
            )

            return False

        if self.ejecutando:

            print(
                "[C302] Ya existe una simulación "
                "en ejecución."
            )

            return False

        self.ejecutando = True

        hilo = threading.Thread(
            target=self._ejecutar_hilo,
            daemon=True
        )

        hilo.start()

        return True

    # ========================================================
    # HILO C302
    # ========================================================

    def _ejecutar_hilo(self):

        try:

            print()
            print("=" * 70)
            print("StickElegans - ejecutando C302")
            print("=" * 70)

            # ------------------------------------------------
            # MISMO COMANDO QUE YA FUNCIONÓ MANUALMENTE
            #
            # python -m pyneuroml.pynml
            # LEMS_c302_A_Full.xml
            # ------------------------------------------------

            comando = [

                sys.executable,

                "-m",
                "pyneuroml.pynml",

                "LEMS_c302_A_Full.xml"
            ]

            print("[C302] Python utilizado:")
            print("       ", sys.executable)

            print()
            print("[C302] Comando:")

            print(
                "       ",
                " ".join(comando)
            )

            print()
            print("[C302] Directorio de ejecución:")
            print("       ", self.examples_dir)

            print()
            print(
                "[C302] Ejecutando jNeuroML..."
            )

            # ------------------------------------------------
            # EJECUCIÓN
            # ------------------------------------------------

            self.proceso = subprocess.run(

                comando,

                cwd=str(
                    self.examples_dir
                ),

                capture_output=True,

                text=True,

                encoding="utf-8",

                errors="replace"
            )

            # ------------------------------------------------
            # ERROR
            # ------------------------------------------------

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
                    print(
                        "---------------- STDOUT ----------------"
                    )

                    print(
                        self.proceso.stdout[
                            -10000:
                        ]
                    )

                if self.proceso.stderr:

                    print()
                    print(
                        "---------------- STDERR ----------------"
                    )

                    print(
                        self.proceso.stderr[
                            -10000:
                        ]
                    )

                self.activo = False

                return

            # ------------------------------------------------
            # TERMINÓ CORRECTAMENTE
            # ------------------------------------------------

            print()
            print(
                "[C302] jNeuroML terminó correctamente."
            )

            # ------------------------------------------------
            # MOSTRAR INFORMACIÓN IMPORTANTE
            # ------------------------------------------------

            if self.proceso.stdout:

                print()
                print(
                    "---------------- C302 OUTPUT ----------------"
                )

                print(
                    self.proceso.stdout[
                        -5000:
                    ]
                )

            # ------------------------------------------------
            # COMPROBAR RESULTADOS
            # ------------------------------------------------

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
            print(
                "[C302] ERROR inesperado:"
            )

            print(e)

            self.activo = False

        finally:

            self.proceso = None

            self.ejecutando = False

            print()
            print("=" * 70)
            print()

    # ========================================================
    # COMPROBAR RESULTADOS
    # ========================================================

    def _comprobar_resultados(self):

        print()
        print(
            "[C302] Comprobando archivos generados..."
        )

        neuron_ok = (
            self.neuron_file.exists()
        )

        muscle_ok = (
            self.muscle_file.exists()
        )

        # ----------------------------------------------------
        # NEURONAS
        # ----------------------------------------------------

        if neuron_ok:

            tamano = (
                self.neuron_file.stat().st_size
            )

            print(
                "[C302] OK - actividad neuronal:"
            )

            print(
                "       ",
                self.neuron_file
            )

            print(
                "       Tamaño:",
                f"{tamano:,}",
                "bytes"
            )

        else:

            print(
                "[C302] ADVERTENCIA:"
            )

            print(
                "       No apareció:",
                self.neuron_file
            )

        # ----------------------------------------------------
        # MÚSCULOS
        # ----------------------------------------------------

        if muscle_ok:

            tamano = (
                self.muscle_file.stat().st_size
            )

            print(
                "[C302] OK - actividad muscular:"
            )

            print(
                "       ",
                self.muscle_file
            )

            print(
                "       Tamaño:",
                f"{tamano:,}",
                "bytes"
            )

        else:

            print(
                "[C302] ADVERTENCIA:"
            )

            print(
                "       No apareció:",
                self.muscle_file
            )

        # ----------------------------------------------------
        # RESULTADO
        # ----------------------------------------------------

        if neuron_ok or muscle_ok:

            self.activo = True

            print()
            print(
                "********************************************************"
            )

            print(
                "[C302] CEREBRO C302 EJECUTADO CORRECTAMENTE"
            )

            print(
                "********************************************************"
            )

            print()
            print(
                "[C302] La siguiente etapa será leer "
                "la actividad neuronal real."
            )

        else:

            self.activo = False

            print()
            print(
                "[C302] C302 terminó, pero no se "
                "encontraron los archivos de salida."
            )

    # ========================================================
    # ACTUALIZAR
    # ========================================================

    def actualizar(
        self,
        dt,
        distancia_mouse,
        velocidad_mouse
    ):

        self.tiempo += dt

        # ----------------------------------------------------
        # IMPORTANTE
        #
        # TODAVÍA NO INTERPRETAMOS LA ACTIVIDAD C302.
        #
        # Por eso las salidas permanecen en cero.
        #
        # Esto evita volver a introducir una red neuronal
        # artificial mientras comprobamos C302.
        # ----------------------------------------------------

        self.izquierda = 0.0
        self.derecha = 0.0

        self.brazo_izquierdo = 0.0
        self.brazo_derecho = 0.0

        self.impulso_vertical = 0.0

    # ========================================================
    # CERRAR
    # ========================================================

    def cerrar(self):

        # ----------------------------------------------------
        # Si jNeuroML todavía estuviera ejecutándose,
        # intentamos terminarlo.
        # ----------------------------------------------------

        if self.proceso is not None:

            try:

                if (
                    self.proceso.poll()
                    is None
                ):

                    self.proceso.terminate()

            except Exception:
                pass

        self.proceso = None


# ============================================================
# WINDOWS API
# ============================================================

user32 = ctypes.windll.user32

EnumWindowsProc = ctypes.WINFUNCTYPE(
    wintypes.BOOL,
    wintypes.HWND,
    wintypes.LPARAM
)


# ============================================================
# CONFIGURACIÓN DE PANTALLA
# ============================================================

SCREEN_WIDTH = 1920
SCREEN_HEIGHT = 1080

GRAVITY = 0.35

PARTICLE_RADIUS = 7

WINDOW_REFRESH_MS = 50
PHYSICS_REFRESH_MS = 16


# ============================================================
# PARTÍCULA
# ============================================================

class Particula:

    def __init__(
        self,
        x: float,
        y: float
    ):

        self.x = x
        self.y = y

        self.vx = 0.0
        self.vy = 0.0

        self.prev_x = x
        self.prev_y = y

    def guardar_posicion(self):

        self.prev_x = self.x
        self.prev_y = self.y


# ============================================================
# RESTRICCIÓN
# ============================================================

class Restriccion:

    def __init__(
        self,
        a: Particula,
        b: Particula,
        rigidez: float = 1.0
    ):

        self.a = a
        self.b = b

        self.distancia = math.hypot(
            b.x - a.x,
            b.y - a.y
        )

        self.rigidez = rigidez

    def resolver(self):

        dx = self.b.x - self.a.x
        dy = self.b.y - self.a.y

        distancia = math.hypot(
            dx,
            dy
        )

        if distancia < 0.0001:
            return

        diferencia = (
            distancia - self.distancia
        ) / distancia

        correccion_x = (
            dx *
            diferencia *
            0.5 *
            self.rigidez
        )

        correccion_y = (
            dy *
            diferencia *
            0.5 *
            self.rigidez
        )

        self.a.x += correccion_x
        self.a.y += correccion_y

        self.b.x -= correccion_x
        self.b.y -= correccion_y


# ============================================================
# SUPERFICIE DE VENTANA
# ============================================================

class Superficie:

    def __init__(
        self,
        hwnd,
        x1,
        y1,
        x2,
        y2
    ):

        self.hwnd = hwnd

        self.x1 = x1
        self.y1 = y1
        self.x2 = x2
        self.y2 = y2

        self.vx = 0.0
        self.vy = 0.0

    @property
    def width(self):

        return self.x2 - self.x1

    @property
    def height(self):

        return self.y2 - self.y1


# ============================================================
# OBTENER RECTÁNGULO
# ============================================================

def obtener_rectangulo(hwnd):

    rect = wintypes.RECT()

    if user32.GetWindowRect(
        hwnd,
        ctypes.byref(rect)
    ):

        return (
            rect.left,
            rect.top,
            rect.right,
            rect.bottom
        )

    return None


# ============================================================
# RAGDOLL
# ============================================================

class Ragdoll:

    def __init__(
        self,
        x,
        y
    ):

        # ----------------------------------------------------
        # CABEZA
        # ----------------------------------------------------

        self.cabeza = Particula(
            x,
            y
        )

        self.cuello = Particula(
            x,
            y + 32
        )

        self.pelvis = Particula(
            x,
            y + 82
        )

        # ----------------------------------------------------
        # BRAZO IZQUIERDO
        # ----------------------------------------------------

        self.hombro_izq = Particula(
            x - 24,
            y + 38
        )

        self.codo_izq = Particula(
            x - 48,
            y + 65
        )

        self.mano_izq = Particula(
            x - 62,
            y + 92
        )

        # ----------------------------------------------------
        # BRAZO DERECHO
        # ----------------------------------------------------

        self.hombro_der = Particula(
            x + 24,
            y + 38
        )

        self.codo_der = Particula(
            x + 48,
            y + 65
        )

        self.mano_der = Particula(
            x + 62,
            y + 92
        )

        # ----------------------------------------------------
        # PIERNA IZQUIERDA
        # ----------------------------------------------------

        self.cadera_izq = Particula(
            x - 14,
            y + 86
        )

        self.rodilla_izq = Particula(
            x - 20,
            y + 135
        )

        self.pie_izq = Particula(
            x - 20,
            y + 180
        )

        # ----------------------------------------------------
        # PIERNA DERECHA
        # ----------------------------------------------------

        self.cadera_der = Particula(
            x + 14,
            y + 86
        )

        self.rodilla_der = Particula(
            x + 20,
            y + 135
        )

        self.pie_der = Particula(
            x + 20,
            y + 180
        )

        # ----------------------------------------------------
        # LISTA
        # ----------------------------------------------------

        self.particulas = [

            self.cabeza,
            self.cuello,
            self.pelvis,

            self.hombro_izq,
            self.codo_izq,
            self.mano_izq,

            self.hombro_der,
            self.codo_der,
            self.mano_der,

            self.cadera_izq,
            self.rodilla_izq,
            self.pie_izq,

            self.cadera_der,
            self.rodilla_der,
            self.pie_der
        ]

        # ----------------------------------------------------
        # RESTRICCIONES
        # ----------------------------------------------------

        self.restricciones = [

            Restriccion(
                self.cabeza,
                self.cuello
            ),

            Restriccion(
                self.cuello,
                self.pelvis
            ),

            Restriccion(
                self.cuello,
                self.hombro_izq
            ),

            Restriccion(
                self.cuello,
                self.hombro_der
            ),

            Restriccion(
                self.hombro_izq,
                self.codo_izq
            ),

            Restriccion(
                self.codo_izq,
                self.mano_izq
            ),

            Restriccion(
                self.hombro_der,
                self.codo_der
            ),

            Restriccion(
                self.codo_der,
                self.mano_der
            ),

            Restriccion(
                self.pelvis,
                self.cadera_izq
            ),

            Restriccion(
                self.cadera_izq,
                self.rodilla_izq
            ),

            Restriccion(
                self.rodilla_izq,
                self.pie_izq
            ),

            Restriccion(
                self.pelvis,
                self.cadera_der
            ),

            Restriccion(
                self.cadera_der,
                self.rodilla_der
            ),

            Restriccion(
                self.rodilla_der,
                self.pie_der
            )
        ]

        self.radio = PARTICLE_RADIUS

    # ========================================================
    # CONTROL NEURONAL
    # ========================================================

    def aplicar_control_neuronal(
        self,
        motor
    ):

        fuerza_pierna = 0.16

        self.pie_izq.vx += (
            motor.izquierda *
            fuerza_pierna
        )

        self.pie_der.vx += (
            motor.derecha *
            fuerza_pierna
        )

        self.rodilla_izq.vx += (
            motor.izquierda *
            0.08
        )

        self.rodilla_der.vx += (
            motor.derecha *
            0.08
        )

        self.mano_izq.vx += (
            motor.brazo_izquierdo *
            0.05
        )

        self.mano_der.vx += (
            motor.brazo_derecho *
            0.05
        )

        if motor.impulso_vertical > 0.55:

            self.pelvis.vy -= (
                motor.impulso_vertical *
                0.10
            )

    # ========================================================
    # GRAVEDAD
    # ========================================================

    def aplicar_gravedad(self):

        for p in self.particulas:

            p.vy += GRAVITY

    # ========================================================
    # AIRE
    # ========================================================

    def aplicar_resistencia_aire(self):

        for p in self.particulas:

            p.vx *= 0.998
            p.vy *= 0.998

    # ========================================================
    # INTEGRACIÓN
    # ========================================================

    def integrar(self):

        for p in self.particulas:

            p.guardar_posicion()

            p.x += p.vx
            p.y += p.vy

    # ========================================================
    # RESTRICCIONES
    # ========================================================

    def resolver_restricciones(self):

        for _ in range(8):

            for r in self.restricciones:

                r.resolver()

    # ========================================================
    # LÍMITES
    # ========================================================

    def resolver_limites_pantalla(self):

        for p in self.particulas:

            if p.x < self.radio:

                p.x = self.radio

                if p.vx < 0:

                    p.vx *= -0.15

            if p.x > SCREEN_WIDTH - self.radio:

                p.x = (
                    SCREEN_WIDTH -
                    self.radio
                )

                if p.vx > 0:

                    p.vx *= -0.15

            if p.y < self.radio:

                p.y = self.radio

                if p.vy < 0:

                    p.vy = 0.0

            if p.y > SCREEN_HEIGHT - self.radio:

                p.y = (
                    SCREEN_HEIGHT -
                    self.radio
                )

                if p.vy > 0:

                    p.vy *= -0.10

                p.vx *= 0.82

    # ========================================================
    # COLISIONES CON VENTANAS
    # ========================================================

    def resolver_colisiones_ventanas(
        self,
        superficies
    ):

        for superficie in superficies:

            x1 = superficie.x1
            y1 = superficie.y1
            x2 = superficie.x2
            y2 = superficie.y2

            if x2 <= x1 or y2 <= y1:

                continue

            for p in self.particulas:

                pie_anterior = p.prev_y

                techo = y1

                dentro_x = (

                    p.x >= x1 - self.radio

                    and

                    p.x <= x2 + self.radio
                )

                cruzo_techo = (

                    pie_anterior + self.radio <= techo

                    and

                    p.y + self.radio >= techo
                )

                cayendo = p.vy >= 0

                if (

                    dentro_x

                    and

                    cruzo_techo

                    and

                    cayendo
                ):

                    p.y = (
                        techo -
                        self.radio
                    )

                    p.vy = (
                        p.vy *
                        -0.08
                    )

                    p.vx *= 0.90

                    continue

                dentro_y = (

                    p.y >= y1

                    and

                    p.y <= y2
                )

                if not dentro_y:

                    continue

                if (

                    p.x + self.radio > x1

                    and

                    p.x < x1

                    and

                    p.vx > 0
                ):

                    p.x = (
                        x1 -
                        self.radio
                    )

                    p.vx *= -0.15

                elif (

                    p.x - self.radio < x2

                    and

                    p.x > x2

                    and

                    p.vx < 0
                ):

                    p.x = (
                        x2 +
                        self.radio
                    )

                    p.vx *= -0.15

    # ========================================================
    # ACTUALIZAR
    # ========================================================

    def actualizar(
        self,
        motor,
        superficies
    ):

        self.aplicar_gravedad()

        self.aplicar_resistencia_aire()

        self.aplicar_control_neuronal(
            motor
        )

        self.integrar()

        self.resolver_restricciones()

        self.resolver_colisiones_ventanas(
            superficies
        )

        self.resolver_limites_pantalla()

        self.resolver_restricciones()

        self.resolver_colisiones_ventanas(
            superficies
        )

        self.resolver_limites_pantalla()


# ============================================================
# DETECTOR DE VENTANAS
# ============================================================

class DetectorVentanas:

    def __init__(self):

        self.superficies = {}

        self.hwnd_propio = None

        self.hwnd_taskbar = user32.FindWindowW(
            "Shell_TrayWnd",
            None
        )

    def actualizar(self):

        encontradas = set()

        def callback(
            hwnd,
            lParam
        ):

            if not user32.IsWindowVisible(
                hwnd
            ):

                return True

            if hwnd == self.hwnd_propio:

                return True

            if hwnd == self.hwnd_taskbar:

                return True

            if user32.IsIconic(hwnd):

                return True

            rect = obtener_rectangulo(
                hwnd
            )

            if rect is None:

                return True

            x1, y1, x2, y2 = rect

            ancho = x2 - x1
            alto = y2 - y1

            if ancho < 80 or alto < 30:

                return True

            if x2 <= 0:

                return True

            if y2 <= 0:

                return True

            if x1 >= SCREEN_WIDTH:

                return True

            if y1 >= SCREEN_HEIGHT:

                return True

            encontradas.add(hwnd)

            if hwnd in self.superficies:

                s = self.superficies[hwnd]

                old_x1 = s.x1
                old_y1 = s.y1

                s.x1 = x1
                s.y1 = y1
                s.x2 = x2
                s.y2 = y2

                s.vx = x1 - old_x1
                s.vy = y1 - old_y1

            else:

                self.superficies[hwnd] = Superficie(

                    hwnd,
                    x1,
                    y1,
                    x2,
                    y2
                )

            return True

        user32.EnumWindows(
            EnumWindowsProc(callback),
            0
        )

        # ----------------------------------------------------
        # BARRA DE TAREAS
        # ----------------------------------------------------

        if self.hwnd_taskbar:

            rect = obtener_rectangulo(
                self.hwnd_taskbar
            )

            if rect:

                x1, y1, x2, y2 = rect

                encontradas.add(
                    self.hwnd_taskbar
                )

                if (
                    self.hwnd_taskbar
                    not in self.superficies
                ):

                    self.superficies[
                        self.hwnd_taskbar
                    ] = Superficie(

                        self.hwnd_taskbar,

                        x1,
                        y1,
                        x2,
                        y2
                    )

                else:

                    s = self.superficies[
                        self.hwnd_taskbar
                    ]

                    s.x1 = x1
                    s.y1 = y1
                    s.x2 = x2
                    s.y2 = y2

        # ----------------------------------------------------
        # ELIMINAR
        # ----------------------------------------------------

        eliminadas = [

            hwnd

            for hwnd
            in self.superficies

            if hwnd not in encontradas
        ]

        for hwnd in eliminadas:

            del self.superficies[
                hwnd
            ]

    def obtener_superficies(self):

        return list(
            self.superficies.values()
        )


# ============================================================
# STICK ELEGANS
# ============================================================

class StickElegans(QWidget):

    def __init__(self):

        super().__init__()

        # ----------------------------------------------------
        # VENTANA
        # ----------------------------------------------------

        self.setWindowFlags(

            Qt.WindowType.FramelessWindowHint

            |

            Qt.WindowType.WindowStaysOnTopHint

            |

            Qt.WindowType.Tool
        )

        self.setAttribute(
            Qt.WidgetAttribute.WA_TranslucentBackground
        )

        self.setMouseTracking(
            True
        )

        # ----------------------------------------------------
        # COMPONENTES
        # ----------------------------------------------------

        self.motor = C302Adapter()

        self.ragdoll = Ragdoll(
            500,
            250
        )

        self.detector = DetectorVentanas()

        self.detector.hwnd_propio = int(
            self.winId()
        )

        # ----------------------------------------------------
        # MOUSE
        # ----------------------------------------------------

        self.mouse_anterior = QCursor.pos()

        self.mouse_velocidad = 0.0

        # ----------------------------------------------------
        # DRAG
        # ----------------------------------------------------

        self.arrastrando = False

        self.drag_anterior = QPoint()

        # ----------------------------------------------------
        # TIMER FÍSICA
        # ----------------------------------------------------

        self.timer_fisica = QTimer(
            self
        )

        self.timer_fisica.timeout.connect(
            self.actualizar_fisica
        )

        self.timer_fisica.start(
            PHYSICS_REFRESH_MS
        )

        # ----------------------------------------------------
        # TIMER VENTANAS
        # ----------------------------------------------------

        self.timer_ventanas = QTimer(
            self
        )

        self.timer_ventanas.timeout.connect(
            self.actualizar_ventanas
        )

        self.timer_ventanas.start(
            WINDOW_REFRESH_MS
        )

        # ----------------------------------------------------
        # INICIALIZACIÓN
        # ----------------------------------------------------

        self.actualizar_ventanas()

        self.actualizar_geometria()

        # ----------------------------------------------------
        # INICIAR C302
        #
        # Se ejecuta en segundo plano para que PyQt y la
        # física no se congelen mientras jNeuroML trabaja.
        # ----------------------------------------------------

        if self.motor.error is None:

            self.motor.ejecutar()

    # ========================================================
    # VENTANAS
    # ========================================================

    def actualizar_ventanas(self):

        self.detector.actualizar()

    # ========================================================
    # MOUSE
    # ========================================================

    def obtener_mouse(self):

        actual = QCursor.pos()

        dx = (
            actual.x()
            -
            self.mouse_anterior.x()
        )

        dy = (
            actual.y()
            -
            self.mouse_anterior.y()
        )

        velocidad = math.hypot(
            dx,
            dy
        )

        self.mouse_velocidad = (

            self.mouse_velocidad * 0.8

            +

            velocidad * 0.2
        )

        self.mouse_anterior = actual

        cabeza = self.ragdoll.cabeza

        distancia = math.hypot(

            actual.x()
            -
            cabeza.x,

            actual.y()
            -
            cabeza.y
        )

        return distancia

    # ========================================================
    # FÍSICA
    # ========================================================

    def actualizar_fisica(self):

        distancia_mouse = (
            self.obtener_mouse()
        )

        dt = (
            PHYSICS_REFRESH_MS /
            1000.0
        )

        # ----------------------------------------------------
        # C302
        # ----------------------------------------------------

        self.motor.actualizar(

            dt,

            distancia_mouse,

            self.mouse_velocidad
        )

        # ----------------------------------------------------
        # CUERPO
        #
        # EXACTAMENTE EL MISMO RAGDOLL.
        # ----------------------------------------------------

        self.ragdoll.actualizar(

            self.motor,

            self.detector.obtener_superficies()
        )

        # ----------------------------------------------------
        # UI
        # ----------------------------------------------------

        self.actualizar_geometria()

        self.update()

    # ========================================================
    # GEOMETRÍA
    # ========================================================

    def actualizar_geometria(self):

        puntos = self.ragdoll.particulas

        min_x = min(
            p.x
            for p in puntos
        )

        max_x = max(
            p.x
            for p in puntos
        )

        min_y = min(
            p.y
            for p in puntos
        )

        max_y = max(
            p.y
            for p in puntos
        )

        margen = 25

        x = int(
            min_x -
            margen
        )

        y = int(
            min_y -
            margen
        )

        ancho = int(

            max_x -
            min_x
            +
            margen * 2
        )

        alto = int(

            max_y -
            min_y
            +
            margen * 2
        )

        ancho = max(
            ancho,
            20
        )

        alto = max(
            alto,
            20
        )

        self.setGeometry(

            x,
            y,
            ancho,
            alto
        )

    # ========================================================
    # COORDENADAS LOCALES
    # ========================================================

    def local(self, p):

        geo = self.geometry()

        return (

            p.x - geo.x(),

            p.y - geo.y()
        )

    # ========================================================
    # PAINT
    # ========================================================

    def paintEvent(self, event):

        painter = QPainter(self)

        painter.setRenderHint(
            QPainter.RenderHint.Antialiasing
        )

        pen = QPen(
            QColor(
                180,
                180,
                180
            ),
            8
        )

        pen.setCapStyle(
            Qt.PenCapStyle.RoundCap
        )

        painter.setPen(
            pen
        )

        # ----------------------------------------------------
        # HUESOS
        # ----------------------------------------------------

        for r in self.ragdoll.restricciones:

            ax, ay = self.local(
                r.a
            )

            bx, by = self.local(
                r.b
            )

            painter.drawLine(

                int(ax),
                int(ay),

                int(bx),
                int(by)
            )

        # ----------------------------------------------------
        # CABEZA
        # ----------------------------------------------------

        cx, cy = self.local(
            self.ragdoll.cabeza
        )

        painter.setPen(
            QPen(

                QColor(
                    230,
                    230,
                    230
                ),

                5
            )
        )

        painter.setBrush(
            Qt.BrushStyle.NoBrush
        )

        painter.drawEllipse(

            int(cx - 20),
            int(cy - 20),

            40,
            40
        )

    # ========================================================
    # MOUSE PRESS
    # ========================================================

    def mousePressEvent(
        self,
        event
    ):

        if (

            event.button()

            ==

            Qt.MouseButton.LeftButton
        ):

            self.arrastrando = True

            self.drag_anterior = (

                event.globalPosition()
                .toPoint()
            )

            for p in self.ragdoll.particulas:

                p.vx = 0.0
                p.vy = 0.0

    # ========================================================
    # MOUSE MOVE
    # ========================================================

    def mouseMoveEvent(
        self,
        event
    ):

        if not self.arrastrando:

            return

        actual = (

            event.globalPosition()
            .toPoint()
        )

        dx = (

            actual.x()
            -
            self.drag_anterior.x()
        )

        dy = (

            actual.y()
            -
            self.drag_anterior.y()
        )

        for p in self.ragdoll.particulas:

            p.x += dx
            p.y += dy

            p.prev_x += dx
            p.prev_y += dy

            p.vx = 0.0
            p.vy = 0.0

        self.drag_anterior = actual

        self.actualizar_geometria()

        self.update()

    # ========================================================
    # MOUSE RELEASE
    # ========================================================

    def mouseReleaseEvent(
        self,
        event
    ):

        if (

            event.button()

            ==

            Qt.MouseButton.LeftButton
        ):

            self.arrastrando = False

            for p in self.ragdoll.particulas:

                p.vx = 0.0
                p.vy = 0.0

    # ========================================================
    # CERRAR
    # ========================================================

    def closeEvent(
        self,
        event
    ):

        self.timer_fisica.stop()

        self.timer_ventanas.stop()

        self.motor.cerrar()

        event.accept()


# ============================================================
# MAIN
# ============================================================

def main():

    print(
        "StickElegans iniciado."
    )

    print(
        f"Resolución física: "
        f"{SCREEN_WIDTH}x{SCREEN_HEIGHT}"
    )

    print(
        "Motor neuronal: OpenWorm / C302"
    )

    app = QApplication(
        sys.argv
    )

    stick = StickElegans()

    stick.show()

    sys.exit(
        app.exec()
    )


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":

    main()