from flask import Flask, jsonify, request
import time

app = Flask(__name__)

# Estado mundial centralizado y sincronizado en tiempo real
world_state = {
    "world": "Outernet-Canyon-3D",
    "active_region": "canyon_base",
    "avatars": {},  # Almacena las posiciones en tiempo real de los avatares conectados
    "portals": [
        {"id": 1, "name": "youtube.com", "status": "OPEN - TETHERED", "active": True, "x": 0.0, "y": 0.4, "z": -2.0},
        {"id": 2, "name": "twitter.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.28, "z": -2.0},
        {"id": 3, "name": "reddit.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.16, "z": -2.0},
        {"id": 4, "name": "discord.com", "status": "OPEN", "active": True, "x": 0.0, "y": 0.04, "z": -2.0},
        {"id": 5, "name": "twitch.tv", "status": "OPEN", "active": True, "x": 0.0, "y": -0.08, "z": -2.0},
        {"id": 6, "name": "wikipedia.org", "status": "OPEN", "active": True, "x": 0.0, "y": -0.2, "z": -2.0},
        {"id": 7, "name": "google.com", "status": "OPEN", "active": True, "x": 0.0, "y": -0.32, "z": -2.0}
    ],
    "server_timestamp": 0.0
}

@app.route('/get_outernet_data', methods=['GET'])
def get_outernet_data():
    world_state["server_timestamp"] = time.time()
    return jsonify(world_state)

@app.route('/update_avatar', methods=['POST'])
def update_avatar():
    data = request.json
    if data and "id" in data:
        avatar_id = str(data["id"])
        world_state["avatars"][avatar_id] = {
            "x": data.get("x", 0.0),
            "y": data.get("y", 1.1),
            "z": data.get("z", -3.8),
            "yaw": data.get("yaw", 0.0),
            "pitch": data.get("pitch", 0.0),
            "last_update": time.time()
        }
        return jsonify({"status": "success", "synced": True})
    return jsonify({"status": "error", "message": "invalid data"}), 400

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)