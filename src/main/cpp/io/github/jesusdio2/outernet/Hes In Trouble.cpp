#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <cmath>
#include <thread>
#include <vector>
#include <string>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wininet.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

struct PortalData {
    int id;
    std::string name;
    std::string status;
    bool active;
    float y_pos;
};

struct TerrainData {
    std::vector<std::vector<float>> grid;
    bool initialized = false;
};

TerrainData serverTerrain;
std::vector<PortalData> serverPortals;
float cursorX = 0.0f, cursorY = 0.0f;
bool serverConnected = false;

POINT ProjectFixedCamera(float x, float y, float z, int width, int height) {
    float camX = 0.0f;
    float camY = 0.0f;
    float camZ = -2.0f;

    float relX = x - camX;
    float relY = y - camY;
    float relZ = z - camZ;

    float scale = (float)width / 12.0f;

    POINT p;
    p.x = (int)(width / 2.0f + relX * scale);
    p.y = (int)(height / 2.0f - (relY + relZ * 0.3f) * scale);
    return p;
}

std::string FetchCloudData() {
    HINTERNET hInternet = InternetOpenA("AnimatorCombatTool/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "";

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hConnect = InternetOpenUrlA(hInternet, "https://jesusdio2.pythonanywhere.com/get_outernet_data", NULL, 0, flags, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return "";
    }

    std::string response = "";
    char buffer[4096];
    DWORD bytesRead = 0;

    while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return response;
}

void NetworkListener(HWND hwnd) {
    while (true) {
        std::string jsonResponse = FetchCloudData();

        if (!jsonResponse.empty()) {
            serverConnected = true;

            // 1. Parseo real de la cuadrícula (grid) enviada por Flask
            size_t gridPos = jsonResponse.find("\"grid\":[[");
            if (gridPos != std::string::npos) {
                serverTerrain.grid.clear();
                size_t bracketStart = jsonResponse.find("[[", gridPos);
                size_t bracketEnd = jsonResponse.find("]]", bracketStart);
                if (bracketStart != std::string::npos && bracketEnd != std::string::npos) {
                    std::string gridContent = jsonResponse.substr(bracketStart + 1, bracketEnd - bracketStart);
                    
                    std::stringstream ss(gridContent);
                    std::string rowStr;
                    while (std::getline(ss, rowStr, '[')) {
                        if (rowStr.length() < 3) continue;
                        std::vector<float> row;
                        std::stringstream rowSs(rowStr);
                        std::string valStr;
                        while (std::getline(rowSs, valStr, ',')) {
                            // Limpiar corchetes o comillas residuales
                            size_t cleanStart = valStr.find_first_of("-0123456789.");
                            if (cleanStart != std::string::npos) {
                                float val = std::stof(valStr.substr(cleanStart));
                                row.push_back(val);
                            }
                        }
                        if (!row.empty()) {
                            serverTerrain.grid.push_back(row);
                        }
                    }
                    if (!serverTerrain.grid.empty()) {
                        serverTerrain.initialized = true;
                    }
                }
            }

            // 2. Parseo real de los portales
            size_t portalsPos = jsonResponse.find("\"portals\":[");
            if (portalsPos != std::string::npos) {
                serverPortals.clear();
                size_t pStart = jsonResponse.find("[", portalsPos);
                size_t pEnd = jsonResponse.find("]", pStart);
                if (pStart != std::string::npos && pEnd != std::string::npos) {
                    std::string portalsStr = jsonResponse.substr(pStart, pEnd - pStart);
                    
                    size_t idPos = 0;
                    int portalIndex = 0;
                    while ((idPos = portalsStr.find("\"id\":", idPos)) != std::string::npos) {
                        PortalData p;
                        p.id = portalsStr[idPos + 5] - '0';
                        
                        // Extraer nombre del portal
                        size_t nameKey = portalsStr.find("\"name\":\"", idPos);
                        if (nameKey != std::string::npos) {
                            size_t q1 = portalsStr.find('"', nameKey + 7);
                            size_t q2 = portalsStr.find('"', q1 + 1);
                            p.name = portalsStr.substr(q1 + 1, q2 - q1 - 1);
                        }

                        p.y_pos = 0.4f - (portalIndex * 0.12f);
                        serverPortals.push_back(p);
                        portalIndex++;
                        idPos++;
                    }
                }
            }

            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            serverConnected = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
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

        float scaleX = (float)w / 12.0f;
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

        HBRUSH hSky = CreateSolidBrush(RGB(135, 206, 235));
        FillRect(hdc, &rect, hSky);
        DeleteObject(hSky);

        if (!serverConnected || !serverTerrain.initialized) {
            SetTextColor(hdc, RGB(220, 50, 50));
            SetBkMode(hdc, TRANSPARENT);
            std::string msgWait = "[!] SINCRONIZANDO CON PYTHONANYWHERE...";
            TextOutA(hdc, 30, 30, msgWait.c_str(), (int)msgWait.length());
            EndPaint(hwnd, &ps);
            break;
        }

        // --- RENDERIZADO DEL AGUA INFERIOR ---
        POINT waterPoly[4] = {
            ProjectFixedCamera(-6.0f, -1.2f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, -1.2f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, -0.4f, 0.0f, w, h),
            ProjectFixedCamera(-6.0f, -0.4f, 0.0f, w, h)
        };
        HBRUSH hWaterBrush = CreateSolidBrush(RGB(30, 144, 255));
        SelectObject(hdc, hWaterBrush);
        SelectObject(hdc, GetStockObject(NULL_PEN));
        Polygon(hdc, waterPoly, 4);
        DeleteObject(hWaterBrush);

        // --- RENDERIZADO DEL ACANTILADO Y PRADERA ---
        POINT cliffPoly[4] = {
            ProjectFixedCamera(-6.0f, 0.7f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, 0.7f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, -0.4f, 0.0f, w, h),
            ProjectFixedCamera(-6.0f, -0.4f, 0.0f, w, h)
        };
        HBRUSH hCliffBrush = CreateSolidBrush(RGB(108, 122, 137)); // Color extraído del JSON ("r":108, "g":122, "b":137)
        SelectObject(hdc, hCliffBrush);
        Polygon(hdc, cliffPoly, 4);
        DeleteObject(hCliffBrush);

        POINT grassPoly[4] = {
            ProjectFixedCamera(-6.0f, 1.2f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, 1.2f, 0.0f, w, h),
            ProjectFixedCamera(6.0f, 0.7f, 0.0f, w, h),
            ProjectFixedCamera(-6.0f, 0.7f, 0.0f, w, h)
        };
        HBRUSH hGrassBrush = CreateSolidBrush(RGB(75, 160, 75));
        SelectObject(hdc, hGrassBrush);
        Polygon(hdc, grassPoly, 4);
        DeleteObject(hGrassBrush);

        // --- RENDERIZADO DE LOS PORTALES Y LÁSERES ---
        HPEN hLaserPen = CreatePen(PS_SOLID, 2, RGB(46, 204, 113)); // ("pen_r":46, "pen_g":204, "pen_b":113)
        SelectObject(hdc, hLaserPen);

        POINT cursorScreen = ProjectFixedCamera(cursorX, cursorY, 0.0f, w, h);

        for (const auto& portal : serverPortals) {
            float portalWorldX = 0.0f;
            float portalWorldY = portal.y_pos;

            POINT portalScreen = ProjectFixedCamera(portalWorldX, portalWorldY, 0.0f, w , h);

            MoveToEx(hdc, portalScreen.x, portalScreen.y, nullptr);
            LineTo(hdc, cursorScreen.x, cursorScreen.y);

            HBRUSH hPortalCore = CreateSolidBrush(RGB(255, 255, 255));
            HPEN hPortalRing = CreatePen(PS_SOLID, 2, RGB(40, 50, 65));
            SelectObject(hdc, hPortalCore);
            SelectObject(hdc, hPortalRing);
            Ellipse(hdc, portalScreen.x - 8, portalScreen.y - 8, portalScreen.x + 8, portalScreen.y + 8);
            DeleteObject(hPortalCore);
            DeleteObject(hPortalRing);
            SelectObject(hdc, hLaserPen);
        }
        DeleteObject(hLaserPen);

        SetTextColor(hdc, RGB(0, 180, 0));
        SetBkMode(hdc, TRANSPARENT);
        std::string status = "Outernet Sintonizado (Datos Nube PythonAnywhere Activos)";
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
    MessageBoxA(NULL, "HELP!", "Urgent Message", MB_OK | MB_ICONWARNING);
    Sleep(2000);
    MessageBoxA(NULL, "PLEASE!", "Urgent Message", MB_OK | MB_ICONINFORMATION);

    const char* CLASS_NAME = "AnimatorCombatTool";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Animator Combat Tool",
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