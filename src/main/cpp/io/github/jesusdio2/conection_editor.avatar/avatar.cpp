#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

struct PortalInfo {
    int id;
    std::string name;
    std::string status;
    bool active;
    float y_pos;
};

// Variables globales protegidas por hilo
std::mutex g_netMutex;
bool g_serverConnected = false;
long g_realPingMs = 0;
long g_realJitterMs = 0;
int g_realFps = 0;
std::vector<PortalInfo> g_serverPortals;

// Posición, rotación y físicas de la cámara (Newton)
float g_camX = 0.0f;
float g_camY = 1.1f;
float g_camZ = -3.8f;
float g_yaw = 0.0f;   
float g_pitch = 0.0f; 

float g_velocityY = 0.0f;       
const float g_gravity = -0.0035f; 
const float g_groundY = 1.1f;   
float g_targetHeight = 1.1f;    

bool g_isResettingCursor = false;
HWND g_hHudLabel = nullptr;

// Envía la posición actual al servidor en tiempo real (POST)
void SendAvatarDataToServer() {
    HINTERNET hInternet = InternetOpenA("AvatarEXE/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return;

    HINTERNET hConnect = InternetConnectA(hInternet, "jesusdio2.pythonanywhere.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
    if (hConnect) {
        HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/update_avatar", NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 1);
        if (hRequest) {
            std::string jsonData = "{\"id\":\"FAES\",\"x\":" + std::to_string(g_camX) + 
                                   ",\"y\":" + std::to_string(g_camY) + 
                                   ",\"z\":" + std::to_string(g_camZ) + 
                                   ",\"yaw\":" + std::to_string(g_yaw) + 
                                   ",\"pitch\":" + std::to_string(g_pitch) + "}";
            
            const char* headers = "Content-Type: application/json\r\n";
            HttpSendRequestA(hRequest, headers, (DWORD)strlen(headers), (LPVOID)jsonData.c_str(), (DWORD)jsonData.length());
            InternetCloseHandle(hRequest);
        }
        InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hInternet);
}

// Hilo de red 24/7 para sincronizar datos GET y enviar POST
void NetworkClientThread() {
    long previousPing = 0;
    while (true) {
        ULONGLONG startTick = GetTickCount64();
        
        HINTERNET hInternet = InternetOpenA("AvatarEXE/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet) {
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
            HINTERNET hConnect = InternetOpenUrlA(hInternet, "https://jesusdio2.pythonanywhere.com/get_outernet_data", NULL, 0, flags, 0);
            if (hConnect) {
                std::string jsonResponse = "";
                char buffer[4096];
                DWORD bytesRead = 0;

                while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    jsonResponse += buffer;
                }
                InternetCloseHandle(hConnect);

                if (!jsonResponse.empty()) {
                    std::lock_guard<std::mutex> lock(g_netMutex);
                    g_serverConnected = true;

                    size_t portalsKey = jsonResponse.find("\"portals\":");
                    if (portalsKey != std::string::npos) {
                        g_serverPortals.clear();
                        size_t startArray = jsonResponse.find('[', portalsKey);
                        size_t endArray = jsonResponse.find(']', startArray);
                        if (startArray != std::string::npos && endArray != std::string::npos) {
                            std::string portalsArrayStr = jsonResponse.substr(startArray + 1, endArray - startArray - 1);
                            
                            size_t objPos = 0;
                            int index = 0;
                            while ((objPos = portalsArrayStr.find('{', objPos)) != std::string::npos) {
                                size_t endObj = portalsArrayStr.find('}', objPos);
                                if (endObj == std::string::npos) break;

                                std::string objStr = portalsArrayStr.substr(objPos, endObj - objPos);
                                PortalInfo p;
                                p.active = true;
                                p.y_pos = 0.4f - (index * 0.12f);
                                
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
                                index++;
                            }
                        }
                    }
                }
            }
            InternetCloseHandle(hInternet);
        }

        SendAvatarDataToServer();

        ULONGLONG endTick = GetTickCount64();
        long currentPing = (long)(endTick - startTick);
        long jitter = (previousPing != 0) ? abs(currentPing - previousPing) : 0;
        previousPing = currentPing;

        {
            std::lock_guard<std::mutex> lock(g_netMutex);
            g_realPingMs = currentPing;
            g_realJitterMs = jitter;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

bool InitOpenGL(HWND hwnd, HDC& hdc, HGLRC& hrc) {
    hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (!format || !SetPixelFormat(hdc, format, &pfd)) return false;

    hrc = wglCreateContext(hdc);
    if (!hrc || !wglMakeCurrent(hdc, hrc)) return false;

    glEnable(GL_DEPTH_TEST);
    return true;
}

// Movimiento, gravedad, Shift (agacharse) y Espacio (saltar)
void UpdateCameraPhysics() {
    float speed = 0.025f; 
    float rad = g_yaw * 3.14159265f / 180.0f;

    float forwardX = sin(rad);
    float forwardZ = cos(rad);
    float rightX = cos(rad);
    float rightZ = -sin(rad);

    if (GetAsyncKeyState('W') & 0x8000) { g_camX += forwardX * speed; g_camZ += forwardZ * speed; }
    if (GetAsyncKeyState('S') & 0x8000) { g_camX -= forwardX * speed; g_camZ -= forwardZ * speed; }
    if (GetAsyncKeyState('D') & 0x8000) { g_camX += rightX * speed;   g_camZ += rightZ * speed; }
    if (GetAsyncKeyState('A') & 0x8000) { g_camX -= rightX * speed;   g_camZ -= rightZ * speed; }
    
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        g_targetHeight = 0.7f;
    } else {
        g_targetHeight = 1.1f;
    }

    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && g_camY <= g_groundY) {
        g_velocityY = 0.085f; 
    }

    g_velocityY += g_gravity;
    g_camY += g_velocityY;

    float effectiveGround = g_groundY - (1.1f - g_targetHeight);
    if (g_camY <= effectiveGround) {
        g_camY = effectiveGround;
        g_velocityY = 0.0f;
    }
}

void RenderAvatarView(int width, int height) {
    UpdateCameraPhysics();

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(65.0f, (float)width / (float)height, 0.1f, 100.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    float radYaw = g_yaw * 3.14159265f / 180.0f;
    float radPitch = g_pitch * 3.14159265f / 180.0f;

    float lookX = g_camX + sin(radYaw) * cos(radPitch);
    float lookY = g_camY + sin(radPitch);
    float lookZ = g_camZ + cos(radYaw) * cos(radPitch);

    gluLookAt(g_camX, g_camY, g_camZ, lookX, lookY, lookZ, 0.0f, 1.0f, 0.0f);

    glClearColor(0.06f, 0.10f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColor3f(0.18f, 0.22f, 0.28f);
    glBegin(GL_QUADS);
        glVertex3f(-2.2f, -0.5f, 1.0f); glVertex3f(-1.6f, -0.5f, 1.0f);
        glVertex3f(-1.6f,  2.5f, 1.0f); glVertex3f(-2.2f,  2.5f, 1.0f);

        glVertex3f( 1.6f, -0.5f, 1.0f); glVertex3f( 2.2f, -0.5f, 1.0f);
        glVertex3f( 2.2f,  2.5f, 1.0f); glVertex3f( 1.6f,  2.5f, 1.0f);

        glVertex3f(-2.2f,  2.0f, 1.0f); glVertex3f( 2.2f,  2.0f, 1.0f);
        glVertex3f( 2.2f,  2.5f, 1.0f); glVertex3f(-2.2f,  2.5f, 1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glColor3f(0.20f, 0.25f, 0.32f);
        glVertex3f(-4.0f, -0.2f, -1.0f);
        glVertex3f( 4.0f, -0.2f, -1.0f);
        glVertex3f( 4.0f, -0.2f,  5.0f);
        glVertex3f(-4.0f, -0.2f,  5.0f);
    glEnd();

    glPushMatrix();
    glTranslatef(0.0f, 0.4f, 3.2f);
    glColor3f(0.12f, 0.14f, 0.18f);
    glBegin(GL_QUADS);
        glVertex3f(-0.08f, -0.6f, 0.0f); glVertex3f(0.08f, -0.6f, 0.0f);
        glVertex3f( 0.08f,  0.1f, 0.0f); glVertex3f(-0.08f, 0.1f, 0.0f);
    glEnd();

    glColor3f(0.0f, 0.75f, 0.25f);
    glBegin(GL_QUADS);
        glVertex3f(-0.22f, 0.1f, 0.02f); glVertex3f(0.22f, 0.1f, 0.02f);
        glVertex3f( 0.22f, 0.4f, 0.02f); glVertex3f(-0.22f, 0.4f, 0.02f);
    glEnd();
    glPopMatrix();
}

HBRUSH hStaticBgBrush = CreateSolidBrush(RGB(6, 10, 20));

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hHudLabel = CreateWindowExA(
            0, "STATIC", "Iniciando telemetria...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 15, 180, 60,
            hwnd, (HMENU)101, ((LPCREATESTRUCT)lParam)->hInstance, nullptr
        );
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 40, 40));
        SetBkColor(hdcStatic, RGB(6, 10, 20));
        return (INT_PTR)hStaticBgBrush;
    }

    case WM_ERASEBKGND:
        return 1; 

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0); 
        }
        break;

    case WM_MOUSEMOVE: {
        if (g_isResettingCursor) {
            g_isResettingCursor = false;
            break;
        }

        RECT rc;
        GetClientRect(hwnd, &rc);
        int centerX = (rc.right - rc.left) / 2;
        int centerY = (rc.bottom - rc.top) / 2;

        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);

        int deltaX = mouseX - centerX;
        int deltaY = mouseY - centerY;

        if (deltaX != 0 || deltaY != 0) {
            g_yaw += deltaX * 0.15f;
            g_pitch -= deltaY * 0.15f;

            if (g_pitch > 89.0f) g_pitch = 89.0f;
            if (g_pitch < -89.0f) g_pitch = -89.0f;

            POINT pt = { centerX, centerY };
            ClientToScreen(hwnd, &pt);
            g_isResettingCursor = true;
            SetCursorPos(pt.x, pt.y);
        }
        break;
    }

    case WM_DESTROY:
        DeleteObject(hStaticBgBrush);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "AvatarExeClass";
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS); // Cursor en cruz (+) nativo del sistema
    wc.hbrBackground = NULL; 
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "avatar.exe",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    HDC hdc = nullptr;
    HGLRC hrc = nullptr;
    if (!InitOpenGL(hwnd, hdc, hrc)) {
        return 0;
    }

    std::thread(NetworkClientThread).detach();

    ULONGLONG lastFpsTick = GetTickCount64();
    int frameCount = 0;

    MSG msg = {};
    while (true) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            if (w > 0 && h > 0) {
                wglMakeCurrent(hdc, hrc);
                RenderAvatarView(w, h);
                SwapBuffers(hdc);

                long pVal, jVal;
                {
                    std::lock_guard<std::mutex> lock(g_netMutex);
                    pVal = g_realPingMs;
                    jVal = g_realJitterMs;
                }
                std::string hudText = "Ping  " + std::to_string(pVal) + "ms\r\n" +
                                      "Jitter " + std::to_string(jVal) + "ms\r\n" +
                                      "FPS " + std::to_string(g_realFps);
                SetWindowTextA(g_hHudLabel, hudText.c_str());

                frameCount++;
                ULONGLONG currentTick = GetTickCount64();
                if (currentTick - lastFpsTick >= 1000) {
                    g_realFps = frameCount;
                    frameCount = 0;
                    lastFpsTick = currentTick;
                }
            }
        }
    }

    if (hrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hrc);
    }

    return 0;
}