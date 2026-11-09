from flask import Flask, jsonify, request
import time
import math

app = Flask(__name__)

# Estado mundial centralizado y sincronizado en tiempo real (Servidor Autoritativo)
world_state = {
    "world": "Outernet-Canyon-3D",
    "active_region": "canyon_base",

    # --- REGLAS Y CONSTANTES FÍSICAS AUTORITATIVAS ---
    # Centralizadas aquí para evitar inconsistencias entre subproyectos (C++ y Python)
    "physics": {
        "gravity": -0.0035,
        "ground_y": 1.1,
        "grid_size_meters": 12.5,
        "stick_height": 1.5,
        "time_scale": 1.0
    },

    # Avatares conectados
    "avatars": {},

    # Portales con sus coordenadas espaciales fijas en el cañón
    "portals": [
        {"id": 1, "name": "youtube.com", "status": "OPEN - TETHERED", "active": True, "x": 0.0, "y": 0.4, "z": -2.0},
        {"id": 2, "name": "twitter.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.28, "z": -2.0},
        {"id": 3, "name": "reddit.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.16, "z": -2.0},
        {"id": 4, "name": "discord.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.04, "z": -2.0},
        {"id": 5, "name": "twitch.tv", "status": "OPEN", "active": True, "x": 0.0, "y": -0.08, "z": -2.0},
        {"id": 6, "name": "wikipedia.org", "status": "OPEN", "active": True, "x": 0.0, "y": -0.2, "z": -2.0},
        {"id": 7, "name": "google.com", "status": "OPEN", "active": True, "x": 0.0, "y": -0.32, "z": -2.0}
    ],

    # Órbitas y satélites calculados de forma centralizada
    "orbitals": [
        {"id": "dish_1", "radius": 5.0, "speed": 0.5, "altitude": 10.0},
        {"id": "satellite_ip", "radius": 15.0, "speed": 0.2, "altitude": 25.0}
    ],

    "server_timestamp": 0.0
}

@app.route('/get_outernet_data', methods=['GET'])
def get_outernet_data():
    current_time = time.time()
    world_state["server_timestamp"] = current_time

    # Calcular posiciones orbitales en tiempo real basadas en el servidor
    # para que todos los clientes vean los satélites exactamente en el mismo sitio.
    calculated_orbitals = []
    for sat in world_state["orbitals"]:
        angle = current_time * sat["speed"]
        calc_x = math.cos(angle) * sat["radius"]
        calc_z = math.sin(angle) * sat["radius"]
        calculated_orbitals.append({
            "id": sat["id"],
            "x": round(calc_x, 3),
            "y": sat["altitude"],
            "z": round(calc_z, 3)
        })

    # Inyectar las posiciones orbitales calculadas en la respuesta
    response_data = world_state.copy()
    response_data["active_orbitals"] = calculated_orbitals

    return jsonify(response_data)

@app.route('/update_avatar', methods=['POST'])
def update_avatar():
    data = request.json
    if data and "id" in data:
        avatar_id = str(data["id"])
        # El servidor valida y almacena la posición del avatar de forma autoritativa
        world_state["avatars"][avatar_id] = {
            "x": data.get("x", 0.0),
            "y": data.get("y", world_state["physics"]["ground_y"]),
            "z": data.get("z", -3.8),
            "yaw": data.get("yaw", 0.0),
            "pitch": data.get("pitch", 0.0),
            "last_update": time.time()
        }
        return jsonify({"status": "success", "synced": True, "server_gravity": world_state["physics"]["gravity"]})
    return jsonify({"status": "error", "message": "invalid data"}), 400

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)