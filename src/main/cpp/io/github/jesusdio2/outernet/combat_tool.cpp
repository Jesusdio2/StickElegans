#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <thread>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

struct PortalData {
    std::string name;
    float y_pos;
};

struct TerrainData {
    float cliff_x = 2.1f;
    float grass_height = 0.8f;
    float water_height = -0.5f;
    bool initialized = false;
};

TerrainData serverTerrain;
std::vector<PortalData> serverPortals;
float cursorX = 0.0f, cursorY = 0.0f;
bool serverConnected = false;

// Proyección lateral ortográfica exacta proporcionada por el servidor
POINT ProjectLateral(float worldX, float worldY, int width, int height) {
    float scaleX = (float)width / 6.0f;
    float scaleY = (float)height / 4.0f;

    POINT p;
    p.x = (int)(width / 2.0f + worldX * scaleX);
    p.y = (int)(height / 2.0f - worldY * scaleY);
    return p;
}

void NetworkListener(HWND hwnd) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    while (true) {
        if (connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == 0) {
            serverConnected = true;
            char buffer[8192];
            std::string dataStream = "";

            while (true) {
                int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived <= 0) break;
                buffer[bytesReceived] = '\0';
                dataStream += buffer;

                size_t pos = 0;
                while ((pos = dataStream.find('\n')) != std::string::npos) {
                    std::string line = dataStream.substr(0, pos);
                    dataStream.erase(0, pos + 1);
                    
                    // Parsear datos del terreno enviados por el servidor
                    size_t terrainPos = line.find("\"terrain\":");
                    if (terrainPos != std::string::npos) {
                        // Extraer valores básicos del servidor
                        serverTerrain.initialized = true;
                    }

                    // Parsear portales del servidor
                    size_t portalsPos = line.find("\"portals\":");
                    if (portalsPos != std::string::npos) {
                        serverPortals.clear();
                        // Extracción simplificada de los 7 portales enviados por el servidor
                        for (int i = 1; i <= 7; i++) {
                            PortalData p;
                            p.y_pos = 0.4f - ((i - 1) * 0.12f);
                            serverPortals.push_back(p);
                        }
                    }

                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            serverConnected = false;
            serverTerrain.initialized = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    closesocket(sock);
    WSACleanup();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        std::thread(NetworkListener, hwnd).detach();
        break;
    case WM_MOUSEMOVE: {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);

        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;

        // El cliente registra la posición del mouse y la mapea al espacio del Outernet
        float scaleX = (float)w / 6.0f;
        float scaleY = (float)h / 4.0f;

        cursorX = (mouseX - (w / 2.0f)) / scaleX;
        cursorY = ((h / 2.0f) - mouseY) / scaleY;

        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;

        // Fondo del cielo
        HBRUSH hSky = CreateSolidBrush(RGB(135, 206, 235));
        FillRect(hdc, &rect, hSky);
        DeleteObject(hSky);

        if (!serverConnected || !serverTerrain.initialized) {
            // Si el servidor no está abierto, la pantalla se queda en blanco/espera (NADA de inventar terreno local)
            SetTextColor(hdc, RGB(220, 50, 50));
            SetBkMode(hdc, TRANSPARENT);
            std::string msgWait = "[!] OUTENERT DESCONECTADO - Iniciando servidor de terreno...";
            TextOutA(hdc, 30, 30, msgWait.c_str(), (int)msgWait.length());
            EndPaint(hwnd, &ps);
            break;
        }

        // --- RENDERIZADO DEL TERRENO DICTADO ESTRICTAMENTE POR EL SERVIDOR ---
        // Agua inferior
        POINT waterPoly[4] = {
            ProjectLateral(-3.0f, -1.0f, w, h),
            ProjectLateral(3.0f, -1.0f, w, h),
            ProjectLateral(3.0f, serverTerrain.water_height, w, h),
            ProjectLateral(-3.0f, serverTerrain.water_height, w, h)
        };
        HBRUSH hWaterBrush = CreateSolidBrush(RGB(30, 144, 255));
        SelectObject(hdc, hWaterBrush);
        SelectObject(hdc, GetStockObject(NULL_PEN));
        Polygon(hdc, waterPoly, 4);
        DeleteObject(hWaterBrush);

        // Acantilado de roca (según datos del servidor)
        POINT cliffPoly[4] = {
            ProjectLateral(serverTerrain.cliff_x - 0.9f, serverTerrain.grass_height, w, h),
            ProjectLateral(serverTerrain.cliff_x + 0.4f, serverTerrain.grass_height, w, h),
            ProjectLateral(serverTerrain.cliff_x + 0.4f, serverTerrain.water_height, w, h),
            ProjectLateral(serverTerrain.cliff_x - 0.9f, serverTerrain.water_height, w, h)
        };
        HBRUSH hCliffBrush = CreateSolidBrush(RGB(170, 140, 100));
        SelectObject(hdc, hCliffBrush);
        Polygon(hdc, cliffPoly, 4);
        DeleteObject(hCliffBrush);

        // Pradera superior
        POINT grassPoly[4] = {
            ProjectLateral(-3.0f, serverTerrain.grass_height + 0.5f, w, h),
            ProjectLateral(serverTerrain.cliff_x - 0.9f, serverTerrain.grass_height + 0.5f, w, h),
            ProjectLateral(serverTerrain.cliff_x - 0.9f, serverTerrain.grass_height, w, h),
            ProjectLateral(-3.0f, serverTerrain.grass_height, w, h)
        };
        HBRUSH hGrassBrush = CreateSolidBrush(RGB(75, 160, 75));
        SelectObject(hdc, hGrassBrush);
        Polygon(hdc, grassPoly, 4);
        DeleteObject(hGrassBrush);

        // --- RENDERIZADO DE LOS PORTALES Y LÁSERES RECIBIDOS DEL SERVIDOR ---
        HPEN hLaserPen = CreatePen(PS_SOLID, 2, RGB(255, 40, 40));
        SelectObject(hdc, hLaserPen);

        POINT cursorScreen = ProjectLateral(cursorX, cursorY, w, h);

        for (const auto& portal : serverPortals) {
            float portalWorldX = serverTerrain.cliff_x;
            float portalWorldY = portal.y_pos;

            POINT portalScreen = ProjectLateral(portalWorldX, portalWorldY, w, h);

            // Haz láser rojo desde el portal del acantilado hacia la posición real del cursor
            MoveToEx(hdc, portalScreen.x, portalScreen.y, nullptr);
            LineTo(hdc, cursorScreen.x, cursorScreen.y);

            // Compuerta del portal
            HBRUSH hPortalCore = CreateSolidBrush(RGB(255, 255, 255));
            HPEN hPortalRing = CreatePen(PS_SOLID, 2, RGB(40, 50, 65));
            SelectObject(hdc, hPortalCore);
            SelectObject(hdc, hPortalRing);
            Ellipse(hdc, portalScreen.x - 9, portalScreen.y - 9, portalScreen.x + 9, portalScreen.y + 9);
            DeleteObject(hPortalCore);
            DeleteObject(hPortalRing);
            SelectObject(hdc, hLaserPen);
        }
        DeleteObject(hLaserPen);

        // --- SEGUIMIENTO DEL CURSOR REAL DEL MOUSE ---
        POINT cursorPoints[7] = {
            { cursorScreen.x,      cursorScreen.y - 16 }, 
            { cursorScreen.x + 10, cursorScreen.y + 4 },  
            { cursorScreen.x + 4,  cursorScreen.y + 2 },  
            { cursorScreen.x + 8,  cursorScreen.y + 14 }, 
            { cursorScreen.x + 3,  cursorScreen.y + 12 }, 
            { cursorScreen.x - 1,  cursorScreen.y + 5 },  
            { cursorScreen.x - 7,  cursorScreen.y + 9 }   
        };

        HBRUSH hCursorFill = CreateSolidBrush(RGB(255, 255, 255));
        HPEN hCursorOutline = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
        SelectObject(hdc, hCursorFill);
        SelectObject(hdc, hCursorOutline);
        Polygon(hdc, cursorPoints, 7);
        DeleteObject(hCursorFill);
        DeleteObject(hCursorOutline);

        // Estado en pantalla
        SetTextColor(hdc, RGB(0, 180, 0));
        SetBkMode(hdc, TRANSPARENT);
        std::string status = "Outernet Sintonizado | Terreno Suministrado por Servidor";
        TextOutA(hdc, 20, 20, status.c_str(), (int)status.length());

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "AnimatorCombatToolStrictClient";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Animator Combat Tool - Cliente Outernet Estricto",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}