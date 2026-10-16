import socket
import random
import time
import json

def run_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', 7777))
    server.listen(5) # Permite múltiples conexiones simultáneas (Combat Tool y Terminal)
    
    print("[SERVIDOR] Outernet activo en 127.0.0.1:7777 - Servidor Unificado (Terreno + Portales + Terminal) Operativo.")
    
    # Lista completa de portales con sus dominios y estados originales
    portals = [
        {"id": 1, "name": "youtube.com", "status": "OPEN - TETHERED", "active": True},
        {"id": 2, "name": "twitter.com", "status": "OPEN", "active": True},
        {"id": 3, "name": "reddit.com", "status": "OPEN", "active": True},
        {"id": 4, "name": "discord.com", "status": "OPEN", "active": True},
        {"id": 5, "name": "twitch.tv", "status": "OPEN", "active": True},
        {"id": 6, "name": "wikipedia.org", "status": "OPEN", "active": True},
        {"id": 7, "name": "google.com", "status": "OPEN", "active": True}
    ]
    
    # Generación de la cuadrícula geométrica estática de 16x16 (meseta, acantilado y agua)[cite: 12]
    static_grid = []
    for x in range(16):
        row = []
        for y in range(16):
            if y < 7:
                # Parte superior: Meseta de pasto verde (elevación alta y plana)[cite: 12]
                h = 0.6 + (random.uniform(-0.02, 0.02))
            elif y == 7:
                # El borde del acantilado (corte vertical pronunciado)[cite: 12]
                h = -0.4
            else:
                # Parte inferior: Bahía / Nivel del agua (plano y bajo)[cite: 12]
                h = -0.9 + (random.uniform(-0.01, 0.01))
            row.append(float(round(h, 2)))
        static_grid.append(row)

    while True:
        conn, addr = server.accept()
        client_ip = addr[0]
        print(f"[SERVIDOR] Enlace establecido con cliente en {client_ip}.")
        try:
            while True:
                # Posición del cursor en el Outernet triangulada originalmente[cite: 12]
                cursor_x = 0.0
                cursor_y = 0.8
                cursor_z = 0.0

                # Paquete JSON unificado que contiene absolutamente todas las propiedades requeridas
                packet_data = {
                    "alt": 1.0,
                    "mat": {"name": "Acantilado Costero", "r": 108, "g": 122, "b": 137, "pen_r": 46, "pen_g": 204, "pen_b": 113},
                    "portals": portals,
                    "cursor": {"x": cursor_x, "y": cursor_y, "z": cursor_z},
                    "grid": static_grid,
                    "tether_matrix": {
                        "active_links": "1 to 7",
                        "sync_rate": "0.0%"
                    }
                }
                
                packet = json.dumps(packet_data) + "\n"
                conn.sendall(packet.encode('utf-8'))
                time.sleep(1.0) # Transmisión estable por socket
        except (ConnectionResetError, BrokenPipeError):
            print(f"[SERVIDOR] Cliente en {client_ip} se ha desconectado.")
            conn.close()

if __name__ == "__main__":
    run_server()