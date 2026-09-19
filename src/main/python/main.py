#!/usr/bin/env python3

from __future__ import annotations

import sys
import math
import ctypes
import threading
import time
from pathlib import Path
from ctypes import wintypes

from lems.model.model import Model
from lems.parser.LEMS import LEMSFileParser
from lems.sim.build import SimulationBuilder

from PyQt6.QtCore import Qt, QTimer, QPoint
from PyQt6.QtGui import QPainter, QColor, QPen, QCursor
from PyQt6.QtWidgets import QApplication, QWidget


# ============================================================
# CONFIGURACIÓN C302
# ============================================================

# Ceder el procesador periódicamente para que Windows y Qt
# puedan seguir procesando la interfaz sin detener C302.
C302_YIELD_EVERY = 200

# Leer los músculos cada N pasos de C302.
# C302 sigue avanzando en TODOS los pasos.
MUSCLE_READ_EVERY = 4

# Mostrar diagnóstico de rendimiento cada N segundos reales.
PERFORMANCE_INTERVAL = 2.0

# Debug detallado.
DEBUG_C302 = False


# ============================================================
# C302 / OPENWORM
# ============================================================

class C302Adapter:
    """
    Adaptador de StickElegans para OpenWorm / c302.

    Utiliza el modelo C302 REAL.

    No crea una red neuronal artificial.
    No sustituye C302 por una aproximación matemática.
    No utiliza osciladores artificiales.

    Flujo:

        Física
          |
          v
        contacto / material
          |
          v
        PLML / PLMR
          |
          v
        C302 / LEMS
          |
          v
        músculos C302
          |
          v
        control del ragdoll

    Optimización:

    - No se escriben sensores si no han cambiado.
    - Se memoriza el método de escritura correcto.
    - No se generan TypeError repetidamente.
    - Los músculos se agrupan una sola vez.
    - La actividad muscular no se reconstruye mediante listas
      en cada paso.
    - El hilo C302 cede CPU periódicamente.
    - C302 continúa avanzando en todos sus pasos.
    """

    def __init__(self):

        # ----------------------------------------------------
        # ESTADO
        # ----------------------------------------------------

        self.activo = False
        self.ejecutando = False
        self.error = None

        self.hilo = None
        self.detener = threading.Event()
        self.lock = threading.RLock()

        # ----------------------------------------------------
        # DEBUG
        # ----------------------------------------------------

        self.debug = DEBUG_C302

        self.debug_steps = 0
        self.debug_ultimo_tiempo = 0.0
        self.debug_intervalo = 0.5

        self.debug_ultimo_sensor = None
        self.debug_ultimo_plml = None
        self.debug_ultimo_plmr = None

        # ----------------------------------------------------
        # RENDIMIENTO
        # ----------------------------------------------------

        self.performance_wall = time.perf_counter()
        self.performance_steps = 0
        self.performance_tiempo_c302 = 0.0

        # ----------------------------------------------------
        # CACHE DE ESCRITURA
        # ----------------------------------------------------

        self._setter_cache = {}

        self._ultimo_amplitude_plml = None
        self._ultimo_amplitude_plmr = None

        # ----------------------------------------------------
        # SALIDAS MOTORAS
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
        # ----------------------------------------------------

        self.neuronas = {}
        self.musculos = {}

        # ----------------------------------------------------
        # SENSORES
        # ----------------------------------------------------

        self.contacto = False
        self.material = "AIR"
        self.intensidad_contacto = 0.0

        self.corriente_plml = 0.0
        self.corriente_plmr = 0.0

        # ----------------------------------------------------
        # MODELO LEMS
        # ----------------------------------------------------

        self.modelo = None
        self.modelo_resuelto = None
        self.simulacion = None
        self.runnable_raiz = None

        self.pulso_plml = None
        self.pulso_plmr = None

        # ----------------------------------------------------
        # MÚSCULOS
        # ----------------------------------------------------

        self.runnables_musculos = {}

        # Grupos cacheados.
        self.musculos_mdl_l = []
        self.musculos_mdl_r = []
        self.musculos_mvl_l = []
        self.musculos_mvr_r = []

        # ----------------------------------------------------
        # RUTAS C302
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
        # COMPROBACIÓN
        # ----------------------------------------------------

        self._diagnostico_python()
        self._comprobar_instalacion()

    # ========================================================
    # DEBUG
    # ========================================================

    def _debug(self, mensaje):

        if self.debug:

            print(
                f"[C302][DEBUG] {mensaje}",
                flush=True
            )

    # ========================================================
    # DEBUG EXCEPCIÓN
    # ========================================================

    def _debug_excepcion(
        self,
        contexto,
        excepcion
    ):

        print()
        print(
            "[C302][DEBUG][EXCEPCIÓN]"
        )

        print(
            "Contexto:",
            contexto
        )

        print(
            "Tipo:",
            type(excepcion).__name__
        )

        print(
            "Mensaje:",
            excepcion
        )

        print()

    # ========================================================
    # DIAGNÓSTICO PYTHON
    # ========================================================

    def _diagnostico_python(self):

        print()
        print("=" * 70)
        print(
            "C302 - DIAGNÓSTICO DEL ENTORNO PYTHON"
        )
        print("=" * 70)

        print(
            "[C302][DEBUG] Python:",
            sys.version
        )

        print(
            "[C302][DEBUG] Ejecutable:",
            sys.executable
        )

        try:

            import lems

            print(
                "[C302][DEBUG] LEMS:",
                lems.__file__
            )

        except Exception as e:

            self._debug_excepcion(
                "import lems",
                e
            )

        try:

            import c302

            print(
                "[C302][DEBUG] c302:",
                getattr(
                    c302,
                    "__version__",
                    "versión no expuesta"
                )
            )

            print(
                "[C302][DEBUG] c302 archivo:",
                c302.__file__
            )

        except Exception as e:

            self._debug_excepcion(
                "import c302",
                e
            )

        try:

            import pyneuroml

            print(
                "[C302][DEBUG] pyNeuroML:",
                getattr(
                    pyneuroml,
                    "__version__",
                    "versión no expuesta"
                )
            )

            print(
                "[C302][DEBUG] pyNeuroML archivo:",
                pyneuroml.__file__
            )

        except Exception as e:

            self._debug_excepcion(
                "import pyneuroml",
                e
            )

        try:

            import numpy

            print(
                "[C302][DEBUG] NumPy:",
                numpy.__version__
            )

        except Exception as e:

            self._debug_excepcion(
                "import numpy",
                e
            )

        try:

            import scipy

            print(
                "[C302][DEBUG] SciPy:",
                scipy.__version__
            )

        except Exception as e:

            self._debug_excepcion(
                "import scipy",
                e
            )

        print("=" * 70)
        print()

    # ========================================================
    # COMPROBAR INSTALACIÓN
    # ========================================================

    def _comprobar_instalacion(self):

        print()
        print("=" * 70)
        print(
            "StickElegans - comprobando OpenWorm / C302"
        )
        print("=" * 70)

        if not self.c302_dir.exists():

            self.error = (
                "No existe el directorio de C302:\n"
                f"{self.c302_dir}"
            )

            print("[C302] ERROR:")
            print(self.error)

            return

        print(
            "[C302] Directorio encontrado:",
            self.c302_dir
        )

        if not self.examples_dir.exists():

            self.error = (
                "No existe el directorio examples:\n"
                f"{self.examples_dir}"
            )

            print("[C302] ERROR:")
            print(self.error)

            return

        print(
            "[C302] examples encontrado:",
            self.examples_dir
        )

        if not self.lems_file.exists():

            self.error = (
                "No se encontró:\n"
                f"{self.lems_file}"
            )

            print("[C302] ERROR:")
            print(self.error)

            return

        print(
            "[C302] Modelo LEMS encontrado:",
            self.lems_file
        )

        print(
            "[C302][DEBUG] Tamaño XML:",
            self.lems_file.stat().st_size,
            "bytes"
        )

        if self.neuron_file.exists():

            print(
                "[C302][DEBUG] Archivo neuronal encontrado:",
                self.neuron_file
            )

        else:

            print(
                "[C302][DEBUG] ADVERTENCIA: "
                "no existe c302_A_Full.dat"
            )

        if self.muscle_file.exists():

            print(
                "[C302][DEBUG] Archivo muscular encontrado:",
                self.muscle_file
            )

        else:

            print(
                "[C302][DEBUG] ADVERTENCIA: "
                "no existe c302_A_Full.muscles.dat"
            )

        print()
        print(
            "[C302] Instalación básica preparada."
        )
        print("=" * 70)
        print()

    # ========================================================
    # CARGAR MODELO LEMS
    # ========================================================

    def _cargar_modelo(self):

        print()
        print("=" * 70)
        print(
            "C302 - INICIO DE CARGA PY LEMS"
        )
        print("=" * 70)

        try:

            from lems.model.model import Model
            from lems.parser.LEMS import LEMSFileParser
            from lems.sim.build import SimulationBuilder

            print(
                "[C302] Creando Model()..."
            )

            modelo = Model()

            print(
                "[C302] Model creado."
            )

            # ------------------------------------------------
            # CORE
            # ------------------------------------------------

            print(
                "[C302] Buscando NeuroML2CoreTypes..."
            )

            candidatos_core = [

                self.c302_dir /
                "lems_core_test" /
                "NeuroML2CoreTypes",

                self.c302_dir /
                "NeuroML2CoreTypes",

                self.c302_dir /
                "examples" /
                "NeuroML2CoreTypes",

                self.c302_dir /
                "lems" /
                "NeuroML2CoreTypes"
            ]

            core_dir = None

            for candidato in candidatos_core:

                if candidato.exists():

                    core_dir = candidato

                    print(
                        "[C302] NeuroML2CoreTypes encontrado:",
                        core_dir
                    )

                    break

            if core_dir is None:

                print(
                    "[C302] Buscando NeuroML2CoreTypes..."
                )

                encontrados = []

                for ruta in self.c302_dir.rglob(
                    "NeuroML2CoreTypes"
                ):

                    if ruta.is_dir():

                        encontrados.append(ruta)

                if encontrados:

                    core_dir = encontrados[0]

                    print(
                        "[C302] Utilizando:",
                        core_dir
                    )

                else:

                    raise RuntimeError(
                        "No se encontró ningún directorio "
                        "NeuroML2CoreTypes."
                    )

            modelo.include_dirs = [

                str(core_dir),
                str(self.examples_dir)

            ]

            # ------------------------------------------------
            # PARSER
            # ------------------------------------------------

            parser = LEMSFileParser(
                modelo
            )

            print(
                "[C302] Leyendo XML..."
            )

            with open(
                self.lems_file,
                "r",
                encoding="utf-8"
            ) as archivo:

                xml = archivo.read()

            print(
                "[C302] Parseando XML..."
            )

            parser.parse(
                xml
            )

            print(
                "[C302] XML parseado correctamente."
            )

            # ------------------------------------------------
            # RESOLVE
            # ------------------------------------------------

            print(
                "[C302] Resolviendo modelo..."
            )

            modelo_resuelto = modelo.resolve()

            print(
                "[C302] Modelo LEMS resuelto."
            )

            # ------------------------------------------------
            # BUILDER
            # ------------------------------------------------

            print(
                "[C302] Construyendo simulación..."
            )

            builder = SimulationBuilder(
                modelo_resuelto
            )

            simulacion = builder.build()

            print(
                "[C302] Simulación PyLEMS construida."
            )

            # ------------------------------------------------
            # INIT
            # ------------------------------------------------

            print(
                "[C302] Inicializando runtime..."
            )

            simulacion.init_run()

            print(
                "[C302] Runtime LEMS inicializado."
            )

            # ------------------------------------------------
            # ROOT
            # ------------------------------------------------

            raiz = None

            for runnable in (
                simulacion.runnables.values()
            ):

                rid = getattr(
                    runnable,
                    "id",
                    ""
                )

                if rid == (
                    "c302_A_Full_sim_c302_A_Full"
                ):

                    raiz = runnable
                    break

            if raiz is None:

                for runnable in (
                    simulacion.runnables.values()
                ):

                    try:

                        plml = runnable.resolve_path(
                            "PLML"
                        )

                        if plml is not None:

                            raiz = runnable
                            break

                    except Exception:
                        pass

            if raiz is None:

                raise RuntimeError(
                    "No se encontró el runtime "
                    "principal de C302."
                )

            self.modelo = modelo
            self.modelo_resuelto = modelo_resuelto
            self.simulacion = simulacion
            self.runnable_raiz = raiz

            # ------------------------------------------------
            # PLML
            # ------------------------------------------------

            self.pulso_plml = (
                self._buscar_pulse_generator(
                    raiz,
                    "PLML"
                )
            )

            # ------------------------------------------------
            # PLMR
            # ------------------------------------------------

            self.pulso_plmr = (
                self._buscar_pulse_generator(
                    raiz,
                    "PLMR"
                )
            )

            if self.pulso_plml is None:

                raise RuntimeError(
                    "No se encontró el pulseGenerator "
                    "real de PLML."
                )

            if self.pulso_plmr is None:

                raise RuntimeError(
                    "No se encontró el pulseGenerator "
                    "real de PLMR."
                )

            print(
                "[C302] PulseGenerator PLML:",
                getattr(
                    self.pulso_plml,
                    "id",
                    "<sin id>"
                )
            )

            print(
                "[C302] PulseGenerator PLMR:",
                getattr(
                    self.pulso_plmr,
                    "id",
                    "<sin id>"
                )
            )

            # ------------------------------------------------
            # CONFIGURAR
            # ------------------------------------------------

            self._configurar_pulse_generator(
                self.pulso_plml
            )

            self._configurar_pulse_generator(
                self.pulso_plmr
            )

            # ------------------------------------------------
            # MÚSCULOS
            # ------------------------------------------------

            print(
                "[C302] Localizando músculos..."
            )

            self._localizar_musculos()

            print(
                "[C302] Músculos localizados:",
                len(
                    self.runnables_musculos
                )
            )

            print()
            print("=" * 70)
            print(
                "C302 - MODELO CARGADO CORRECTAMENTE"
            )
            print("=" * 70)
            print()

        except Exception as e:

            self.error = (
                "No fue posible cargar C302 mediante PyLEMS:\n"
                f"{type(e).__name__}: {e}"
            )

            print()
            print("=" * 70)
            print(
                "[C302] ERROR AL CARGAR"
            )
            print("=" * 70)
            print(self.error)
            print("=" * 70)
            print()

            self._debug_excepcion(
                "_cargar_modelo",
                e
            )

            raise

    # ========================================================
    # BUSCAR PULSE GENERATOR
    # ========================================================

    def _buscar_pulse_generator(
        self,
        raiz,
        poblacion
    ):

        try:

            poblacion_runnable = (
                raiz.resolve_path(
                    poblacion
                )
            )

        except Exception:

            poblacion_runnable = None

        if poblacion_runnable is None:
            return None

        def condicion(r):

            try:

                component = getattr(
                    r,
                    "component",
                    None
                )

                tipo = getattr(
                    component,
                    "type",
                    ""
                )

                return tipo == "pulseGenerator"

            except Exception:

                return False

        return self._buscar_recursivo(
            poblacion_runnable,
            condicion
        )

    # ========================================================
    # BÚSQUEDA RECURSIVA
    # ========================================================

    def _buscar_recursivo(
        self,
        runnable,
        condicion,
        profundidad=0
    ):

        if runnable is None:
            return None

        if profundidad > 100:
            return None

        try:

            if condicion(runnable):
                return runnable

        except Exception:
            pass

        children = getattr(
            runnable,
            "children",
            {}
        )

        if isinstance(
            children,
            dict
        ):

            valores = children.values()

        elif children is None:

            valores = []

        else:

            valores = children

        for child in valores:

            encontrado = (
                self._buscar_recursivo(
                    child,
                    condicion,
                    profundidad + 1
                )
            )

            if encontrado is not None:
                return encontrado

        return None

    # ========================================================
    # CONFIGURAR PULSE GENERATOR
    # ========================================================

    def _configurar_pulse_generator(
        self,
        runnable
    ):

        if runnable is None:

            raise RuntimeError(
                "Intento de configurar "
                "pulseGenerator None."
            )

        self._poner_variable(
            runnable,
            "delay",
            0.0
        )

        self._poner_variable(
            runnable,
            "duration",
            1.0e9
        )

        self._poner_variable(
            runnable,
            "amplitude",
            0.0
        )

    # ========================================================
    # PONER VARIABLE OPTIMIZADO
    # ========================================================

    def _poner_variable(
        self,
        runnable,
        nombre,
        valor
    ):

        if runnable is None:
            return False

        rid = getattr(
            runnable,
            "id",
            "<sin id>"
        )

        clave = (
            id(runnable),
            nombre
        )

        # ----------------------------------------------------
        # MÉTODO CACHEADO
        # ----------------------------------------------------

        metodo = self._setter_cache.get(
            clave
        )

        if metodo is not None:

            try:

                metodo(
                    runnable,
                    valor
                )

                return True

            except Exception:

                # Si el método dejó de funcionar,
                # olvidamos el cache y volvemos a detectar.
                self._setter_cache.pop(
                    clave,
                    None
                )

        # ----------------------------------------------------
        # SETATTR
        # ----------------------------------------------------

        try:

            def usar_setattr(
                obj,
                value
            ):

                setattr(
                    obj,
                    nombre,
                    value
                )

            usar_setattr(
                runnable,
                valor
            )

            self._setter_cache[
                clave
            ] = usar_setattr

            return True

        except Exception:
            pass

        # ----------------------------------------------------
        # SETITEM
        # ----------------------------------------------------

        try:

            def usar_setitem(
                obj,
                value
            ):

                obj[
                    nombre
                ] = value

            usar_setitem(
                runnable,
                valor
            )

            self._setter_cache[
                clave
            ] = usar_setitem

            return True

        except Exception as e:

            # Solo informar si realmente fallaron
            # ambos mecanismos.
            self._debug(
                f"{rid}.{nombre}: "
                f"no se pudo escribir "
                f"({type(e).__name__})"
            )

        return False

    # ========================================================
    # OBTENER VARIABLE
    # ========================================================

    def _obtener_variable(
        self,
        runnable,
        nombre
    ):

        if runnable is None:
            return None

        try:

            return getattr(
                runnable,
                nombre
            )

        except Exception:
            pass

        try:

            return runnable[
                nombre
            ]

        except Exception:
            pass

        return None

    # ========================================================
    # LOCALIZAR MÚSCULOS
    # ========================================================

    def _localizar_musculos(self):

        grupos = [
            "MDL",
            "MDR",
            "MVL",
            "MVR"
        ]

        encontrados_por_grupo = {
            grupo: 0
            for grupo in grupos
        }

        for grupo in grupos:

            maximo = 24

            if grupo == "MVL":
                maximo = 23

            for indice in range(
                1,
                maximo + 1
            ):

                nombre = (
                    f"{grupo}{indice:02d}"
                )

                try:

                    poblacion = (
                        self.runnable_raiz.resolve_path(
                            nombre
                        )
                    )

                except Exception:

                    continue

                if poblacion is None:
                    continue

                musculo = (
                    self._buscar_recursivo(
                        poblacion,
                        lambda r: hasattr(
                            r,
                            "v"
                        )
                    )
                )

                if musculo is None:
                    continue

                self.runnables_musculos[
                    nombre
                ] = musculo

                encontrados_por_grupo[
                    grupo
                ] += 1

        # ----------------------------------------------------
        # CACHEAR GRUPOS
        # ----------------------------------------------------

        self.musculos_mdl_l = []
        self.musculos_mdl_r = []
        self.musculos_mvl_l = []
        self.musculos_mvr_r = []

        for nombre, runnable in (
            self.runnables_musculos.items()
        ):

            if nombre.startswith("MDL"):
                self.musculos_mdl_l.append(runnable)

            elif nombre.startswith("MDR"):
                self.musculos_mdl_r.append(runnable)

            elif nombre.startswith("MVL"):
                self.musculos_mvl_l.append(runnable)

            elif nombre.startswith("MVR"):
                self.musculos_mvr_r.append(runnable)

        print(
            "[C302] Músculos por grupo:"
        )

        for grupo in grupos:

            print(
                "       ",
                grupo,
                ":",
                encontrados_por_grupo[
                    grupo
                ]
            )

    # ========================================================
    # ACTIVIDAD DE UN GRUPO
    # ========================================================

    @staticmethod
    def _promedio_musculos(
        grupo
    ):

        if not grupo:
            return 0.0

        total = 0.0

        for runnable in grupo:

            try:

                total += float(
                    runnable.v
                )

            except Exception:
                pass

        return (
            total /
            len(grupo)
        )

    # ========================================================
    # LEER ACTIVIDAD MUSCULAR
    # ========================================================

    def _leer_actividad_muscular(self):

        return (

            self._promedio_musculos(
                self.musculos_mdl_l
            ),

            self._promedio_musculos(
                self.musculos_mdl_r
            ),

            self._promedio_musculos(
                self.musculos_mvl_l
            ),

            self._promedio_musculos(
                self.musculos_mvr_r
            )
        )

    # ========================================================
    # NORMALIZAR ACTIVIDAD
    # ========================================================

    @staticmethod
    def _normalizar_actividad(
        valor
    ):

        minimo = -0.080
        maximo = -0.030

        if valor <= minimo:
            return 0.0

        if valor >= maximo:
            return 1.0

        return (
            (valor - minimo)
            /
            (maximo - minimo)
        )

    # ========================================================
    # ACTUALIZAR SALIDAS MOTORAS
    # ========================================================

    def _actualizar_salidas_musculares(
        self
    ):

        (
            md_l,
            md_r,
            mv_l,
            mv_r
        ) = self._leer_actividad_muscular()

        raw = (
            md_l,
            md_r,
            mv_l,
            mv_r
        )

        md_l = self._normalizar_actividad(
            md_l
        )

        md_r = self._normalizar_actividad(
            md_r
        )

        mv_l = self._normalizar_actividad(
            mv_l
        )

        mv_r = self._normalizar_actividad(
            mv_r
        )

        # ----------------------------------------------------
        # PIERNAS
        # ----------------------------------------------------

        izquierda = max(
            -1.0,
            min(
                1.0,
                md_l - mv_l
            )
        )

        derecha = max(
            -1.0,
            min(
                1.0,
                md_r - mv_r
            )
        )

        # ----------------------------------------------------
        # BRAZOS
        # ----------------------------------------------------

        brazo_izq = (
            md_l +
            mv_l
        ) * 0.5

        brazo_der = (
            md_r +
            mv_r
        ) * 0.5

        # ----------------------------------------------------
        # IMPULSO
        # ----------------------------------------------------

        actividad_total = (
            md_l +
            md_r +
            mv_l +
            mv_r
        ) * 0.25

        with self.lock:

            self.izquierda = (
                izquierda * 8.0
            )

            self.derecha = (
                derecha * 8.0
            )

            self.brazo_izquierdo = (
                brazo_izq * 4.0
            )

            self.brazo_derecho = (
                brazo_der * 4.0
            )

            self.impulso_vertical = (
                actividad_total
            )

            self.musculos = {
                "MDL": md_l,
                "MDR": md_r,
                "MVL": mv_l,
                "MVR": mv_r
            }

        # ----------------------------------------------------
        # DEBUG
        # ----------------------------------------------------

        tiempo_actual = self.tiempo

        if (
            self.debug
            and
            tiempo_actual -
            self.debug_ultimo_tiempo
            >=
            self.debug_intervalo
        ):

            self.debug_ultimo_tiempo = (
                tiempo_actual
            )

            self._debug(
                "Músculos raw: "
                f"MDL={raw[0]:.6g} "
                f"MDR={raw[1]:.6g} "
                f"MVL={raw[2]:.6g} "
                f"MVR={raw[3]:.6g}"
            )

            self._debug(
                "Motor: "
                f"izq={self.izquierda:.3f} "
                f"der={self.derecha:.3f} "
                f"brazoI={self.brazo_izquierdo:.3f} "
                f"brazoD={self.brazo_derecho:.3f}"
            )

    # ========================================================
    # SENSOR DE CONTACTO
    # ========================================================

    def establecer_contacto(
        self,
        contacto,
        material="AIR",
        intensidad=0.0
    ):

        material = str(
            material
        ).upper()

        intensidad = max(
            0.0,
            min(
                1.0,
                float(intensidad)
            )
        )

        if not contacto:

            material = "AIR"
            intensidad = 0.0

        with self.lock:

            self.contacto = bool(
                contacto
            )

            self.material = material

            self.intensidad_contacto = (
                intensidad
            )

            # ------------------------------------------------
            # AIR
            # ------------------------------------------------

            if (
                not contacto
                or
                material == "AIR"
            ):

                self.corriente_plml = 0.0
                self.corriente_plmr = 0.0

            # ------------------------------------------------
            # PLASTIC
            # ------------------------------------------------

            elif material == "PLASTIC":

                # Corrección: No estimular falsamente los mecanorreceptores de la cola (PLML / PLMR)
                # al tener contacto o apoyarse en superficies/ventanas.
                self.corriente_plml = 0.0
                self.corriente_plmr = 0.0

            # ------------------------------------------------
            # DESCONOCIDO
            # ------------------------------------------------

            else:

                self.corriente_plml = 0.0
                self.corriente_plmr = 0.0

        estado = (
            material,
            round(
                intensidad,
                3
            )
        )

        if estado != self.debug_ultimo_sensor:

            self.debug_ultimo_sensor = (
                estado
            )

            self._debug(
                "Sensor cambiado: "
                f"contacto={contacto} "
                f"material={material} "
                f"intensidad={intensidad:.3f}"
            )

    # ========================================================
    # APLICAR SENSORES OPTIMIZADO
    # ========================================================

    def _aplicar_sensores(self):

        with self.lock:

            corriente_plml = (
                self.corriente_plml
            )

            corriente_plmr = (
                self.corriente_plmr
            )

        # ----------------------------------------------------
        # PLML
        # ----------------------------------------------------

        if (
            corriente_plml
            !=
            self._ultimo_amplitude_plml
        ):

            ok_plml = self._poner_variable(
                self.pulso_plml,
                "amplitude",
                corriente_plml
            )

            if ok_plml:

                self._ultimo_amplitude_plml = (
                    corriente_plml
                )

        # ----------------------------------------------------
        # PLMR
        # ----------------------------------------------------

        if (
            corriente_plmr
            !=
            self._ultimo_amplitude_plmr
        ):

            ok_plmr = self._poner_variable(
                self.pulso_plmr,
                "amplitude",
                corriente_plmr
            )

            if ok_plmr:

                self._ultimo_amplitude_plmr = (
                    corriente_plmr
                )

    # ========================================================
    # EJECUTAR
    # ========================================================

    def ejecutar(self):

        print()
        print(
            "[C302] Solicitud de ejecución."
        )

        if self.error is not None:

            print(
                "[C302] ERROR: "
                "no se puede ejecutar."
            )

            print(
                self.error
            )

            return False

        if self.ejecutando:

            print(
                "[C302] Ya existe "
                "una simulación."
            )

            return False

        self.detener.clear()

        self.ejecutando = True

        self.hilo = threading.Thread(
            target=self._ejecutar_hilo,
            name="C302-LEMS",
            daemon=True
        )

        self.hilo.start()

        return True

    # ========================================================
    # RENDIMIENTO
    # ========================================================

    def _mostrar_rendimiento(self):

        ahora = time.perf_counter()

        transcurrido = (
            ahora -
            self.performance_wall
        )

        if transcurrido < PERFORMANCE_INTERVAL:
            return

        pasos = (
            self.debug_steps -
            self.performance_steps
        )

        with self.lock:

            tiempo_c302 = self.tiempo

        delta_c302 = (
            tiempo_c302 -
            self.performance_tiempo_c302
        )

        pasos_por_segundo = (
            pasos /
            transcurrido
            if transcurrido > 0
            else 0.0
        )

        ratio = (
            delta_c302 /
            (transcurrido * 1000.0)
            if transcurrido > 0
            else 0.0
        )

        print(
            "[C302][PERFORMANCE] "
            f"{pasos_por_segundo:.1f} steps/s | "
            f"avance biológico={delta_c302:.3f} ms | "
            f"ratio={ratio:.3f}x",
            flush=True
        )

        self.performance_wall = ahora
        self.performance_steps = (
            self.debug_steps
        )
        self.performance_tiempo_c302 = (
            tiempo_c302
        )

    # ========================================================
    # HILO C302
    # ========================================================

    def _ejecutar_hilo(self):

        print()
        print(
            "[C302] HILO C302 INICIADO"
        )

        try:

            self._cargar_modelo()

            self.activo = True

            print()
            print("=" * 70)
            print(
                "StickElegans - C302 INTERACTIVO INICIADO"
            )
            print("=" * 70)

            print(
                "[C302] Sensores: PLML / PLMR"
            )

            print(
                "[C302] Entorno: AIR / PLASTIC"
            )

            print(
                "[C302] Sin simulación de agua."
            )

            print()

            self.performance_wall = (
                time.perf_counter()
            )

            self.performance_steps = 0

            self.performance_tiempo_c302 = (
                self.tiempo
            )

            pasos_desde_musculos = 0

            # ------------------------------------------------
            # LOOP REAL DE LEMS
            # ------------------------------------------------

            while not self.detener.is_set():

                self.debug_steps += 1

                # --------------------------------------------
                # SENSOR
                # --------------------------------------------

                self._aplicar_sensores()

                # --------------------------------------------
                # STEP REAL
                # --------------------------------------------

                continuar = (
                    self.simulacion.step()
                )

                # --------------------------------------------
                # TIEMPO
                # --------------------------------------------

                with self.lock:

                    self.tiempo = (
                        getattr(
                            self.simulacion,
                            "current_time",
                            self.tiempo
                        )
                    )

                # --------------------------------------------
                # MÚSCULOS
                # --------------------------------------------

                pasos_desde_musculos += 1

                if (
                    pasos_desde_musculos
                    >=
                    MUSCLE_READ_EVERY
                ):

                    self._actualizar_salidas_musculares()

                    pasos_desde_musculos = 0

                # --------------------------------------------
                # RENDIMIENTO
                # --------------------------------------------

                self._mostrar_rendimiento()

                # --------------------------------------------
                # YIELD CPU
                # --------------------------------------------

                if (
                    self.debug_steps
                    %
                    C302_YIELD_EVERY
                    ==
                    0
                ):

                    time.sleep(0)

                # --------------------------------------------
                # FIN
                # --------------------------------------------

                if not continuar:

                    print(
                        "[C302] "
                        "simulacion.step() devolvió False."
                    )

                    break

            self.activo = False

        except Exception as e:

            self.error = (
                f"{type(e).__name__}: {e}"
            )

            self.activo = False

            print()
            print("=" * 70)
            print(
                "[C302] ERROR EN SIMULACIÓN"
            )
            print("=" * 70)

            print(
                "Tipo:",
                type(e).__name__
            )

            print(
                "Mensaje:",
                e
            )

            print(
                "Steps ejecutados:",
                self.debug_steps
            )

            print(
                "Tiempo C302:",
                self.tiempo
            )

            print("=" * 70)
            print()

        finally:

            self.ejecutando = False

            print(
                "[C302] Hilo C302 terminado."
            )

            print(
                "[C302][DEBUG] Steps:",
                self.debug_steps
            )

            print(
                "[C302][DEBUG] Tiempo:",
                self.tiempo
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

        with self.lock:

            if not self.activo:

                if self.error is None:

                    self.izquierda = 0.0
                    self.derecha = 0.0

                    self.brazo_izquierdo = 0.0
                    self.brazo_derecho = 0.0

                    self.impulso_vertical = 0.0

    # ========================================================
    # CERRAR
    # ========================================================

    def cerrar(self):

        print(
            "[C302] Cerrando motor..."
        )

        self.detener.set()

        if (
            self.hilo is not None
            and
            self.hilo.is_alive()
        ):

            self.hilo.join(
                timeout=1.0
            )

        self.hilo = None

        self.activo = False
        self.ejecutando = False

        print(
            "[C302] Motor cerrado."
        )


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
# CONFIGURACIÓN
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
            distancia -
            self.distancia
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

        self.material = "PLASTIC"

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
    # RESISTENCIA DEL AIRE
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
    # COLISIONES
    # ========================================================

    def resolver_colisiones_ventanas(
        self,
        superficies
    ):

        contactos = []

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
                    pie_anterior +
                    self.radio
                    <=
                    techo
                    and
                    p.y +
                    self.radio
                    >=
                    techo
                )

                cayendo = p.vy >= 0

                if (
                    dentro_x
                    and
                    cruzo_techo
                    and
                    cayendo
                ):

                    velocidad_impacto = max(
                        0.0,
                        p.vy
                    )

                    intensidad = min(
                        1.0,
                        velocidad_impacto / 10.0
                    )

                    p.y = (
                        techo -
                        self.radio
                    )

                    p.vy *= -0.08
                    p.vx *= 0.90

                    contactos.append({

                        "particula": p,

                        "superficie": superficie,

                        "material":
                            superficie.material,

                        "intensidad":
                            intensidad
                    })

                    continue

                dentro_y = (
                    p.y >= y1
                    and
                    p.y <= y2
                )

                if not dentro_y:
                    continue

                if (
                    p.x +
                    self.radio
                    >
                    x1
                    and
                    p.x < x1
                    and
                    p.vx > 0
                ):

                    velocidad_impacto = abs(
                        p.vx
                    )

                    intensidad = min(
                        1.0,
                        velocidad_impacto / 10.0
                    )

                    p.x = (
                        x1 -
                        self.radio
                    )

                    p.vx *= -0.15

                    contactos.append({

                        "particula": p,

                        "superficie": superficie,

                        "material":
                            superficie.material,

                        "intensidad":
                            intensidad
                    })

                elif (
                    p.x -
                    self.radio
                    <
                    x2
                    and
                    p.x > x2
                    and
                    p.vx < 0
                ):

                    velocidad_impacto = abs(
                        p.vx
                    )

                    intensidad = min(
                        1.0,
                        velocidad_impacto / 10.0
                    )

                    p.x = (
                        x2 +
                        self.radio
                    )

                    p.vx *= -0.15

                    contactos.append({

                        "particula": p,

                        "superficie": superficie,

                        "material":
                            superficie.material,

                        "intensidad":
                            intensidad
                    })

        return contactos

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

        contactos = (
            self.resolver_colisiones_ventanas(
                superficies
            )
        )

        self.resolver_limites_pantalla()

        self.resolver_restricciones()

        return contactos


# ============================================================
# DETECTOR DE VENTANAS
# ============================================================

class DetectorVentanas:

    def __init__(self):

        self.superficies = {}

        self.hwnd_propio = None

        self.hwnd_taskbar = (
            user32.FindWindowW(
                "Shell_TrayWnd",
                None
            )
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

            if user32.IsIconic(
                hwnd
            ):
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

            encontradas.add(
                hwnd
            )

            if hwnd in self.superficies:

                s = self.superficies[
                    hwnd
                ]

                old_x1 = s.x1
                old_y1 = s.y1

                s.x1 = x1
                s.y1 = y1
                s.x2 = x2
                s.y2 = y2

                s.vx = x1 - old_x1
                s.vy = y1 - old_y1

            else:

                self.superficies[
                    hwnd
                ] = Superficie(
                    hwnd,
                    x1,
                    y1,
                    x2,
                    y2
                )

            return True

        user32.EnumWindows(
            EnumWindowsProc(
                callback
            ),
            0
        )

        # ----------------------------------------------------
        # TASKBAR
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
                    not in
                    self.superficies
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
        # ELIMINAR CERRADAS
        # ----------------------------------------------------

        eliminadas = [

            hwnd

            for hwnd
            in self.superficies

            if hwnd
            not in encontradas
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
            self.mouse_velocidad *
            0.8
            +
            velocidad *
            0.2
        )

        self.mouse_anterior = actual

        cabeza = self.ragdoll.cabeza

        distancia = math.hypot(
            actual.x() - cabeza.x,
            actual.y() - cabeza.y
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

        self.motor.actualizar(
            dt,
            distancia_mouse,
            self.mouse_velocidad
        )

        contactos = (
            self.ragdoll.actualizar(
                self.motor,
                self.detector.obtener_superficies()
            )
        )

        # ----------------------------------------------------
        # SENSOR
        # ----------------------------------------------------

        if contactos:

            contacto_principal = max(
                contactos,
                key=lambda c:
                    c["intensidad"]
            )

            self.motor.establecer_contacto(
                True,
                contacto_principal[
                    "material"
                ],
                contacto_principal[
                    "intensidad"
                ]
            )

        else:

            self.motor.establecer_contacto(
                False,
                "AIR",
                0.0
            )

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
            min_x +
            margen * 2
        )

        alto = int(
            max_y -
            min_y +
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

    def paintEvent(
        self,
        event
    ):

        painter = QPainter(
            self
        )

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

    print()
    print("=" * 70)
    print(
        "StickElegans iniciado."
    )
    print("=" * 70)

    print(
        f"Resolución física: "
        f"{SCREEN_WIDTH}x{SCREEN_HEIGHT}"
    )

    print(
        "Motor neuronal: OpenWorm / C302 REAL"
    )

    print(
        "Sensores: PLML / PLMR"
    )

    print(
        "Entorno: AIR / PLASTIC"
    )

    print(
        "Agua: NO REQUERIDA"
    )

    print(
        "Backend: PyLEMS"
    )

    print(
        "Python:",
        sys.executable
    )

    print("=" * 70)

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