#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <thread>
#include <sstream>
#include <mutex>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

using namespace Gdiplus;

// Definiciones de los IDs de recursos internos
#define IDR_CANDADO          102
#define IDR_CANDADO_ABIERTO  103
#define IDR_ERROR            104
#define IDR_CURSOR           105

bool g_showBiosScreen = true;     
std::string g_passwordBuffer = "";
bool g_unlocked = false;
bool g_showError = false;
int g_animStep = 0;              
bool g_showTerminalWindow = false; 

// Variables para la animación progresiva del BIOS
int g_biosStep = 0;
unsigned long long g_currentMem = 0;
unsigned long long g_targetMem = 32768;
bool g_cursorBlink = true;

// Variable de tiempo global para animar el movimiento fluido de la UI de fondo
float g_uiTimeTick = 0.0f;

std::string g_terminalInput = "";  
std::vector<std::string> g_terminalOutputLines;
std::mutex g_netMutex;

// Estructura para almacenar los portales recibidos desde el servidor Python
struct PortalInfo {
    int id;
    std::string name;
    std::string status;
    bool active;
};
std::vector<PortalInfo> g_serverPortals;
bool g_serverConnected = false;

// --- HILO CLIENTE SOCKET PARA CONECTARSE A server.py ---
void NetworkClientThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_netMutex);
        g_serverConnected = true;
    }

    char buffer[4096];
    std::string streamData = "";

    while (true) {
        int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) break;

        buffer[bytesReceived] = '\0';
        streamData += buffer;

        size_t pos = 0;
        while ((pos = streamData.find('\n')) != std::string::npos) {
            std::string jsonLine = streamData.substr(0, pos);
            streamData.erase(0, pos + 1);

            size_t portalsKey = jsonLine.find("\"portals\":");
            if (portalsKey != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_netMutex);
                g_serverPortals.clear();

                size_t startArray = jsonLine.find('[', portalsKey);
                size_t endArray = jsonLine.find(']', startArray);
                if (startArray != std::string::npos && endArray != std::string::npos) {
                    std::string portalsArrayStr = jsonLine.substr(startArray + 1, endArray - startArray - 1);
                    
                    size_t objPos = 0;
                    while ((objPos = portalsArrayStr.find('{', objPos)) != std::string::npos) {
                        size_t endObj = portalsArrayStr.find('}', objPos);
                        if (endObj == std::string::npos) break;

                        std::string objStr = portalsArrayStr.substr(objPos, endObj - objPos);
                        PortalInfo p;
                        
                        size_t idPos = objStr.find("\"id\":");
                        if (idPos != std::string::npos) p.id = std::stoi(objStr.substr(idPos + 5));

                        size_t namePos = objStr.find("\"name\":");
                        if (namePos != std::string::npos) {
                            size_t q1 = objStr.find('"', namePos + 7);
                            size_t q2 = objStr.find('"', q1 + 1);
                            p.name = objStr.substr(q1 + 1, q2 - q1 - 1);
                        }

                        size_t statusPos = objStr.find("\"status\":");
                        if (statusPos != std::string::npos) {
                            size_t q1 = objStr.find('"', statusPos + 9);
                            size_t q2 = objStr.find('"', q1 + 1);
                            p.status = objStr.substr(q1 + 1, q2 - q1 - 1);
                        }

                        g_serverPortals.push_back(p);
                        objPos = endObj + 1;
                    }
                }
            }
        }
    }

    closesocket(sock);
    WSACleanup();
}

// --- FUNCIÓN PARA CARGAR PNG DESDE RECURSOS INTERNOS ---
Bitmap* LoadBitmapFromResource(UINT resourceID) {
    HMODULE hInstance = GetModuleHandle(nullptr);
    HRSRC hResInfo = FindResourceW(hInstance, MAKEINTRESOURCEW(resourceID), MAKEINTRESOURCEW(10));
    if (!hResInfo) return nullptr;

    HGLOBAL hResData = LoadResource(hInstance, hResInfo);
    if (!hResData) return nullptr;

    void* pData = LockResource(hResData);
    DWORD dataSize = SizeofResource(hInstance, hResInfo);
    if (!pData || dataSize == 0) return nullptr;

    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, dataSize);
    if (!hGlobal) return nullptr;

    void* pBuffer = GlobalLock(hGlobal);
    if (pBuffer) {
        memcpy(pBuffer, pData, dataSize);
        GlobalUnlock(hGlobal);
    }

    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
        Bitmap* bitmap = new Bitmap(pStream);
        pStream->Release();
        return bitmap;
    }
    GlobalFree(hGlobal);
    return nullptr;
}

// --- FUNCIÓN ROBUSTA PARA CARGAR O CONVERTIR EL CURSOR CON RESPALDOS ---
HCURSOR LoadCustomCursor(HINSTANCE hInstance) {
    HCURSOR hCursor = (HCURSOR)LoadImageA(hInstance, MAKEINTRESOURCEA(IDR_CURSOR), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hCursor) return hCursor;

    hCursor = (HCURSOR)LoadImageA(nullptr, "TDLCursor.cur", IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (hCursor) return hCursor;

    Bitmap* pngBitmap = Bitmap::FromFile(L"TDLCursor.png");
    if (pngBitmap && pngBitmap->GetLastStatus() == Ok) {
        int width = pngBitmap->GetWidth();
        int height = pngBitmap->GetHeight();
        HBITMAP hbmColor = nullptr;
        pngBitmap->GetHBITMAP(Color(0, 0, 0, 0), &hbmColor);
        delete pngBitmap;

        if (hbmColor) {
            HBITMAP hbmMask = CreateBitmap(width, height, 1, 1, nullptr);
            ICONINFO iconInfo = {};
            iconInfo.fIcon = FALSE;
            iconInfo.hbmMask = hbmMask;
            iconInfo.hbmColor = hbmColor;
            HCURSOR hPngCursor = (HCURSOR)CreateIconIndirect(&iconInfo);
            DeleteObject(hbmMask);
            DeleteObject(hbmColor);
            if (hPngCursor) return hPngCursor;
        }
    }
    if (pngBitmap) delete pngBitmap;

    return LoadCursor(nullptr, IDC_ARROW);
}

// --- FUNCIONES PARA DETECTAR ESPECIFICACIONES REALES DEL PC ---
unsigned long long GetRealMemoryTotalKB() {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys / 1024;
    }
    return 32768;
}

std::string GetRealMemoryString() {
    std::ostringstream ss;
    ss << "Memory Test : " << g_currentMem << "K OK";
    return ss.str();
}

std::string GetRealCpuString() {
    HKEY hKey;
    DWORD mhz = 0;
    DWORD size = sizeof(mhz);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            std::ostringstream ss;
            ss << "CPU at " << mhz << "MHz";
            return ss.str();
        }
        RegCloseKey(hKey);
    }
    return "CPU at 3200MHz";
}

// --- INTÉRPRETE DE COMANDOS NATIVO ---
void ExecuteNativeCommand(const std::string& cmd) {
    if (cmd == "dir") {
        g_terminalOutputLines.push_back("Portal Directory (Outernet Node 7777):");
        std::lock_guard<std::mutex> lock(g_netMutex);
        if (!g_serverPortals.empty()) {
            for (const auto& p : g_serverPortals) {
                std::ostringstream line;
                line << "  Portal " << p.id << " ... [" << p.name << "]     " << p.status;
                g_terminalOutputLines.push_back(line.str());
            }
        } else {
            g_terminalOutputLines.push_back("  [WARN] No se pudo conectar a server.py (127.0.0.1:7777)");
            g_terminalOutputLines.push_back("  Portal 1 ... [youtube.com]     OPEN - TETHERED (Fallback)");
        }
    } 
    else if (cmd == "tlk ALL portals") {
        g_terminalOutputLines.push_back("Tethers Links - Portals synchronized with server.py.");
        g_terminalOutputLines.push_back("Subspace vector routing: ACTIVE");
    }
    else if (cmd.rfind("link subset", 0) == 0) {
        g_terminalOutputLines.push_back("[EXEC] E:S {A.link(binary)} -> LinkAll vector mapped.");
        g_terminalOutputLines.push_back("[EXEC] windowUI set: subspace active.");
    }
    else if (cmd == "show ARRAY(tether)") {
        g_terminalOutputLines.push_back("Portal Matrix Array (Live Socket Feed):");
        g_terminalOutputLines.push_back("|1 -              ||·|                  |1  (3.7) ||0|");
        g_terminalOutputLines.push_back("|2 (5.4)       ||0|                 |2  -        ||-|");
    }
    else if (cmd == "cmd") {
        g_terminalOutputLines.push_back("Command List:");
        g_terminalOutputLines.push_back("  dir, tlk ALL portals, show ARRAY(tether), clear, cst");
    }
    else if (cmd == "cst") {
        std::lock_guard<std::mutex> lock(g_netMutex);
        if (g_serverConnected) {
            g_terminalOutputLines.push_back("Console Settings: SECURE | Outernet Node 7777 [CONNECTED TO server.py]");
        } else {
            g_terminalOutputLines.push_back("Console Settings: SECURE | Outernet Node 7777 [OFFLINE]");
        }
    }
    else {
        g_terminalOutputLines.push_back("Comando no reconocido. Escribe 'cmd' para ver la lista.");
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_targetMem = GetRealMemoryTotalKB();
        SetTimer(hwnd, 99, 100, nullptr);
        SetTimer(hwnd, 98, 200, nullptr); // Parpadeo rápido del cursor (200ms)
        SetTimer(hwnd, 97, 33, nullptr);  // Animación continua de la UI (~30 FPS)
        std::thread(NetworkClientThread).detach();
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            HCURSOR hCustomCursor = LoadCustomCursor(hInst);
            if (hCustomCursor) {
                SetCursor(hCustomCursor);
                return TRUE;
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_KEYDOWN:
        if (g_showBiosScreen) {
            if (wParam == VK_F4) {
                KillTimer(hwnd, 99);
                KillTimer(hwnd, 98);
                // NOTA: No matamos el timer 97 para que la UI siga animándose de fondo
                g_showBiosScreen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (g_unlocked) {
            if (!g_showTerminalWindow) {
                g_showTerminalWindow = true;
                g_terminalOutputLines.push_back("Terminal Console (Outernet Live Feed)");
                g_terminalOutputLines.push_back("Escribe 'cmd' para ver la lista de comandos.");
            }
        }
        break;

    case WM_CHAR: {
        TCHAR ch = (TCHAR)wParam;

        if (g_showBiosScreen) {
            break; 
        }

        if (!g_unlocked) {
            if (g_animStep > 0) break; 

            if (ch == VK_BACK) {
                if (!g_passwordBuffer.empty()) {
                    g_passwordBuffer.pop_back();
                    g_showError = false;
                }
            }
            else if (ch == '\r' || ch == '\n') {
                if (g_passwordBuffer == "default") {
                    g_unlocked = true;
                    g_showError = false;
                    g_animStep = 1; 
                    SetTimer(hwnd, 1, 180, nullptr); // Tiempo para mostrar la animación del candado abierto
                } else {
                    g_unlocked = false;
                    g_showError = true;
                }
            }
            else if (ch >= 32 && ch <= 126) {
                if (g_passwordBuffer.length() < 25) {
                    g_passwordBuffer += ch;
                    g_showError = false;
                }
            }
        }
        else if (g_showTerminalWindow) {
            if (ch == VK_BACK) {
                if (!g_terminalInput.empty()) {
                    g_terminalInput.pop_back();
                }
            }
            else if (ch == '\r' || ch == '\n') {
                if (g_terminalInput == "clear" || g_terminalInput == "cls") {
                    g_terminalOutputLines.clear();
                } else {
                    g_terminalOutputLines.push_back("> " + g_terminalInput);
                    ExecuteNativeCommand(g_terminalInput);
                }
                g_terminalInput = ""; 
            }
            else if (ch >= 32 && ch <= 126) {
                if (g_terminalInput.length() < 35) {
                    g_terminalInput += ch;
                }
            }
        }
        
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_TIMER: {
        if (wParam == 99) {
            g_biosStep++;
            if (g_biosStep >= 2 && g_currentMem < g_targetMem) {
                g_currentMem += (g_targetMem / 5) + 2048;
                if (g_currentMem > g_targetMem) g_currentMem = g_targetMem;
            }
            if (g_biosStep > 22) {
                KillTimer(hwnd, 99);
                KillTimer(hwnd, 98);
                // Dejamos activo el timer 97 para la animación fluida de la UI
                g_showBiosScreen = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == 98) {
            g_cursorBlink = !g_cursorBlink;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == 97) {
            g_uiTimeTick += 0.15f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == 1) {
            g_animStep++;
            if (g_animStep > 6) { // Duración ampliada para apreciar el candado abierto
                KillTimer(hwnd, 1);
                g_animStep = 0; 
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // --- 1. PANTALLA DE BIOS ---
        if (g_showBiosScreen) {
            HBRUSH hBiosBg = CreateSolidBrush(RGB(20, 35, 30));
            FillRect(hdc, &clientRect, hBiosBg);
            DeleteObject(hBiosBg);

            HFONT hBiosFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                           DEFAULT_PITCH | FF_MODERN, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hBiosFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 230, 118));

            int startX = 40;
            int startY = 50;
            int lh = 22;
            std::string cursorStr = g_cursorBlink ? "_" : " ";

            TextOutA(hdc, startX, startY + lh * 14, "Press DEL to enter SETUP", 24);
            TextOutA(hdc, startX, startY + lh * 15, "12/10/97-i430VX,UMC8669-2A59GH2BC-00", 35);

            if (g_biosStep >= 1) {
                TextOutA(hdc, startX, startY, GetRealCpuString().c_str(), (int)GetRealCpuString().length());
            }
            if (g_biosStep >= 2) {
                TextOutA(hdc, startX, startY + lh, GetRealMemoryString().c_str(), (int)GetRealMemoryString().length());
            }
            if (g_biosStep >= 5) {
                TextOutA(hdc, startX, startY + lh * 3, "BIOS Extension v1.0A", 20);
            }

            std::string skipTag = "  [Press F4 to skip]";

            if (g_biosStep >= 7) {
                bool active = (g_biosStep >= 7 && g_biosStep < 10);
                std::string line = "    Detecting IDE Primary     ... PCemHD" + (active ? cursorStr : "");
                if (active) line += skipTag;
                TextOutA(hdc, startX, startY + lh * 4, line.c_str(), (int)line.length());
            }
            if (g_biosStep >= 10) {
                bool active = (g_biosStep >= 10 && g_biosStep < 13);
                std::string line = "    Detecting IDE Primary     ... PCemHD" + (active ? cursorStr : "");
                if (active) line += skipTag;
                TextOutA(hdc, startX, startY + lh * 5, line.c_str(), (int)line.length());
            }
            if (g_biosStep >= 13) {
                bool active = (g_biosStep >= 13 && g_biosStep < 16);
                std::string line = "    Detecting IDE Secondary   ... None" + (active ? cursorStr : "");
                if (active) line += skipTag;
                TextOutA(hdc, startX, startY + lh * 6, line.c_str(), (int)line.length());
            }
            if (g_biosStep >= 16) {
                bool active = (g_biosStep >= 16);
                std::string line = "    Detecting IDE Secondary   ... None" + (active ? cursorStr : "");
                if (active) line += skipTag;
                TextOutA(hdc, startX, startY + lh * 7, line.c_str(), (int)line.length());
            }

            SelectObject(hdc, hOldFont);
            DeleteObject(hBiosFont);
            EndPaint(hwnd, &ps);
            break;
        }

        // --- 2. RENDERIZADO DE LA PANTALLA PRINCIPAL ---
        HBRUSH hBgBrush = CreateSolidBrush(RGB(18, 30, 38));
        FillRect(hdc, &clientRect, hBgBrush);
        DeleteObject(hBgBrush);

        COLORREF greenColor = RGB(0, 230, 118);
        HPEN hGreenPen = CreatePen(PS_SOLID, 2, greenColor);
        HPEN hThinGreenPen = CreatePen(PS_SOLID, 1, greenColor);
        HBRUSH hGreenFill = CreateSolidBrush(greenColor);
        HBRUSH hDarkFill = CreateSolidBrush(RGB(10, 20, 15)); 
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);

        Graphics graphics(hdc);

        if (g_unlocked) {
            // ==========================================
            // INTERFAZ DE FONDO (MONITOREO TÉCNICO ANIMADO)
            // ==========================================
            SelectObject(hdc, hThinGreenPen);

            int portalCentersX[] = { 210, 310, 410, 510 };
            int portalY = 95;
            int r = 32;

            HFONT hPortalFont = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                            DEFAULT_PITCH | FF_SWISS, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hPortalFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, greenColor);

            for (int i = 0; i < 4; i++) {
                std::string label = std::to_string(i + 1) + "-OPEN";
                TextOutA(hdc, portalCentersX[i] - 18, portalY - r - 22, label.c_str(), (int)label.length());

                Ellipse(hdc, portalCentersX[i] - r, portalY - r, portalCentersX[i] + r, portalY + r);
                Ellipse(hdc, portalCentersX[i] - 10, portalY - 10, portalCentersX[i] + 10, portalY + 10);
                
                int pX = portalCentersX[i] + (int)(cos(g_uiTimeTick + i) * (r - 5));
                int pY = portalY + (int)(sin(g_uiTimeTick + i) * (r - 5));
                MoveToEx(hdc, portalCentersX[i], portalY, nullptr);
                LineTo(hdc, pX, pY);
            }

            MoveToEx(hdc, 150, portalY, nullptr);
            LineTo(hdc, 580, portalY);

            int gridStartX = 45;
            int gridStartY = 240;
            int wSize = 120;
            int hSize = 75;

            for (int rIndex = 0; rIndex < 2; rIndex++) {
                for (int cIndex = 0; cIndex < 2; cIndex++) {
                    int gx = gridStartX + cIndex * (wSize + 15);
                    int gy = gridStartY + rIndex * (hSize + 15);

                    SelectObject(hdc, hThinGreenPen);
                    SelectObject(hdc, hNullBrush);
                    Rectangle(hdc, gx, gy, gx + wSize, gy + hSize);

                    MoveToEx(hdc, gx + 8, gy + hSize - 15, nullptr);
                    for (int px = 12; px < wSize - 8; px += 15) {
                        int pyVal = (int)(sin(g_uiTimeTick + px * 0.1f + cIndex + rIndex) * 12);
                        LineTo(hdc, gx + px, gy + (hSize / 2) + pyVal);
                    }
                }
            }

            std::string statusTexts[4] = {
                "STATUS: OPEN - CHANNEL",
                "SUBSPACE: WorldAlign_2.56.841",
                "DIMENSION: [x+1.014,y-1.022,z+0.184,t-1.0",
                "ALIGNMENT: 0.9965"
            };

            int boxX = 310;
            int boxY = 240;
            int boxW = 310;
            int boxH = 35;
            int boxGap = 9;

            for (int i = 0; i < 4; i++) {
                int by = boxY + i * (boxH + boxGap);
                
                SelectObject(hdc, hThinGreenPen);
                SelectObject(hdc, hNullBrush);
                Rectangle(hdc, boxX, by, boxX + 28, by + boxH);
                Rectangle(hdc, boxX + 35, by, boxX + boxW, by + boxH);

                RECT textR = { boxX + 45, by + 8, boxX + boxW - 10, by + boxH - 2 };
                DrawTextA(hdc, statusTexts[i].c_str(), -1, &textR, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }

            SelectObject(hdc, hOldFont);
            DeleteObject(hPortalFont);

            // ==========================================
            // VENTANA FLOTANTE 1: CÓDIGO Y MATEMÁTICAS
            // ==========================================
            RECT codeWin = { 45, 65, 290, 225 };
            SelectObject(hdc, hGreenPen);
            SelectObject(hdc, hDarkFill);
            Rectangle(hdc, codeWin.left, codeWin.top, codeWin.right, codeWin.bottom);

            SelectObject(hdc, hGreenFill);
            RECT codeTitle = { codeWin.left, codeWin.top, codeWin.right, codeWin.top + 22 };
            FillRect(hdc, &codeTitle, hGreenFill);

            SetTextColor(hdc, RGB(18, 30, 38));
            RECT codeClose = { codeWin.right - 20, codeWin.top + 2, codeWin.right - 4, codeWin.top + 20 };
            RECT codeMin = { codeWin.right - 40, codeWin.top + 2, codeWin.right - 24, codeWin.top + 20 };
            DrawTextA(hdc, "X", 1, &codeClose, DT_CENTER | DT_SINGLELINE);
            DrawTextA(hdc, "_", 1, &codeMin, DT_CENTER | DT_SINGLELINE);

            HFONT hCodeFont = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                          DEFAULT_PITCH | FF_MODERN, "Consolas");
            SelectObject(hdc, hCodeFont);
            SetTextColor(hdc, greenColor);

            int cy = codeWin.top + 28;
            int cx = codeWin.left + 8;
            TextOutA(hdc, cx, cy, "track (vector, r){", 18); cy += 16;
            TextOutA(hdc, cx + 10, cy, "pin(vector,r.A);", 16); cy += 18;
            TextOutA(hdc, cx + 10, cy, "S: f / r+1", 12); cy += 16;
            TextOutA(hdc, cx + 10, cy, "E: Integral f(r,y)dA", 20); cy += 22;
            TextOutA(hdc, cx, cy, "report channeldata{", 19); cy += 16;
            TextOutA(hdc, cx + 10, cy, "hold all tether connections", 27); cy += 16;
            TextOutA(hdc, cx + 10, cy, "portals align to C(x,y,z,t)", 28);

            DeleteObject(hCodeFont);

            // ==========================================
            // VENTANA FLOTANTE 2: TERMINAL
            // ==========================================
            if (g_showTerminalWindow) {
                RECT termRect = { 130, 90, 520, 375 };
                FillRect(hdc, &termRect, hDarkFill);
                
                SelectObject(hdc, hGreenPen);
                SelectObject(hdc, hNullBrush);
                Rectangle(hdc, termRect.left, termRect.top, termRect.right, termRect.bottom);

                SelectObject(hdc, hGreenFill);
                RECT termTitleBar = { termRect.left, termRect.top, termRect.right, termRect.top + 24 };
                FillRect(hdc, &termTitleBar, hGreenFill);

                SetTextColor(hdc, RGB(18, 30, 38)); 
                RECT btnMin = { termRect.right - 44, termRect.top + 3, termRect.right - 24, termRect.top + 21 };
                RECT btnClose = { termRect.right - 22, termRect.top + 3, termRect.right - 3, termRect.top + 21 };
                DrawTextA(hdc, "_", 1, &btnMin, DT_CENTER | DT_SINGLELINE);
                DrawTextA(hdc, "X", 1, &btnClose, DT_CENTER | DT_SINGLELINE);

                SetTextColor(hdc, greenColor);
                TextOutA(hdc, termRect.left + 8, termRect.top + 5, "Terminal Outernet (Socket)", 24);

                int textY = termRect.top + 32;
                int textX = termRect.left + 10;
                int maxLines = 14;
                int startIdx = (int)g_terminalOutputLines.size() > maxLines ? (int)g_terminalOutputLines.size() - maxLines : 0;
                int currentY = textY;

                for (size_t i = startIdx; i < g_terminalOutputLines.size(); i++) {
                    RECT lineRect = { textX, currentY, termRect.right - 10, currentY + 14 };
                    DrawTextA(hdc, g_terminalOutputLines[i].c_str(), -1, &lineRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                    currentY += 14;
                }

                std::string promptLine = "> " + g_terminalInput + (g_cursorBlink ? "_" : " ");
                RECT promptRect = { textX, termRect.bottom - 25, termRect.right - 10, termRect.bottom - 5 };
                DrawTextA(hdc, promptLine.c_str(), -1, &promptRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }

        } else {
            // ==========================================
            // PANTALLA DE BLOQUEO ORIGINAL (CONSOLA CENTRAL)
            // ==========================================
            int lockOffsetY = 0;
            if (g_animStep == 1) lockOffsetY = -15; 
            else if (g_animStep == 2) lockOffsetY = -28; 
            else if (g_animStep == 3) lockOffsetY = -18;  
            else if (g_animStep == 4) lockOffsetY = -5;  
            else if (g_animStep >= 5) lockOffsetY = 0;   

            SelectObject(hdc, hGreenPen);
            SelectObject(hdc, hNullBrush);
            RECT consoleRect = { 120, 70, 520, 380 };
            Rectangle(hdc, consoleRect.left, consoleRect.top, consoleRect.right, consoleRect.bottom);

            SelectObject(hdc, hGreenFill);
            RECT titleBar = { 120, 70, 520, 102 };
            FillRect(hdc, &titleBar, hGreenFill);

            Bitmap* bitmap = nullptr;
            if (g_showError) {
                bitmap = LoadBitmapFromResource(IDR_ERROR);
            } else if (g_unlocked) {
                bitmap = LoadBitmapFromResource(IDR_CANDADO_ABIERTO);
            } else {
                bitmap = LoadBitmapFromResource(IDR_CANDADO);
            }

            if (bitmap) {
                int imgSize = 65; 
                int imgX = 320 - (imgSize / 2);
                int imgY = 175 + lockOffsetY;
                graphics.DrawImage(bitmap, imgX, imgY, imgSize, imgSize);
                delete bitmap;
            }

            // --- CAJA DE ENTRADA PRINCIPAL ---
            RECT inputRect = { 190, 290, 450, 330 };
            SelectObject(hdc, hGreenPen);
            SelectObject(hdc, hNullBrush);
            Rectangle(hdc, inputRect.left, inputRect.top, inputRect.right, inputRect.bottom);

            HFONT hFont = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                      DEFAULT_PITCH | FF_SWISS, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            std::string displayPassword(g_passwordBuffer.length(), '*');

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, greenColor); 

            RECT textRect = { inputRect.left + 10, inputRect.top + 5, inputRect.right - 10, inputRect.bottom - 5 };
            DrawTextA(hdc, displayPassword.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }

        DeleteObject(hGreenPen);
        DeleteObject(hThinGreenPen);
        DeleteObject(hGreenFill);
        DeleteObject(hDarkFill);

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
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    const char* CLASS_NAME = "TheDarkLordConsoleClass";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    
    HICON hCustomIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (hCustomIcon) {
        wc.hIcon = hCustomIcon;
        wc.hIconSm = hCustomIcon;
    } else {
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    }

    wc.hCursor = LoadCustomCursor(hInstance);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "The Dark Lord Console",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 470,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}