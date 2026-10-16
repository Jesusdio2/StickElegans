#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <thread>

using namespace Gdiplus;

std::string g_passwordBuffer = "";
bool g_unlocked = false;
bool g_showError = false;
int g_animStep = 0;              
bool g_showAdvancedUI = false;     
bool g_showTerminalWindow = false; 

std::string g_terminalInput = "";  
std::vector<std::string> g_terminalOutputLines;
HANDLE g_hChildStdinWr = NULL;
HANDLE g_hChildStdoutRd = NULL;
bool g_shellInitialized = false;

// Hilo secundario para leer la salida del cmd.exe en segundo plano
void BackgroundReaderThread(HWND hwnd) {
    char buffer[4096];
    DWORD bytesRead;
    while (true) {
        if (ReadFile(g_hChildStdoutRd, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string output(buffer);
            
            // Procesar líneas y añadirlas al buffer de la consola visual
            size_t pos = 0;
            std::string token;
            while ((pos = output.find("\n")) != std::string::npos) {
                token = output.substr(0, pos);
                if (!token.empty() && token.back() == '\r') token.pop_back();
                g_terminalOutputLines.push_back(token);
                output.erase(0, pos + 1);
            }
            if (!output.empty()) {
                g_terminalOutputLines.push_back(output);
            }

            // Limitar las líneas almacenadas para mantener el rendimiento visual
            if (g_terminalOutputLines.size() > 50) {
                g_terminalOutputLines.erase(g_terminalOutputLines.begin(), g_terminalOutputLines.end() - 50);
            }

            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            break;
        }
    }
}

// Inicializar el proceso cmd.exe oculto mediante Pipes
void InitBackgroundShell(HWND hwnd) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStdoutWr;
    CreatePipe(&g_hChildStdoutRd, &hChildStdoutWr, &saAttr, 0);
    SetHandleInformation(g_hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);

    HANDLE hChildStdinRd;
    CreatePipe(&hChildStdinRd, &g_hChildStdinWr, &saAttr, 0);
    SetHandleInformation(g_hChildStdinWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = hChildStdoutWr;
    si.hStdInput = hChildStdinRd;
    si.wShowWindow = SW_HIDE; // Ocultar la ventana nativa de cmd.exe

    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA("C:\\Windows\\System32\\cmd.exe", NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hChildStdoutWr);
        CloseHandle(hChildStdinRd);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        g_shellInitialized = true;

        // Lanzar hilo de escucha
        std::thread(BackgroundReaderThread, hwnd).detach();
    }
}

// Enviar comando al proceso cmd.exe en segundo plano
void SendCommandToShell(const std::string& cmd) {
    if (!g_shellInitialized) return;
    std::string fullCmd = cmd + "\n";
    DWORD written;
    WriteFile(g_hChildStdinWr, fullCmd.c_str(), (DWORD)fullCmd.length(), &written, NULL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CHAR: {
        TCHAR ch = (TCHAR)wParam;

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
                    SetTimer(hwnd, 1, 45, nullptr); 
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
                    SendCommandToShell(g_terminalInput);
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
        if (wParam == 1) {
            g_animStep++;
            if (g_animStep > 4) {
                KillTimer(hwnd, 1);
                g_animStep = 0; 
                SetTimer(hwnd, 2, 500, nullptr);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == 2) {
            KillTimer(hwnd, 2);
            g_showAdvancedUI = true; 
            SetTimer(hwnd, 3, 500, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == 3) {
            KillTimer(hwnd, 3);
            g_showTerminalWindow = true;
            InitBackgroundShell(hwnd); // Arranca el shell híbrido al abrir la terminal
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        HBRUSH hBgBrush = CreateSolidBrush(RGB(18, 30, 38));
        FillRect(hdc, &clientRect, hBgBrush);
        DeleteObject(hBgBrush);

        COLORREF greenColor = RGB(0, 230, 118);
        HPEN hGreenPen = CreatePen(PS_SOLID, 2, greenColor);
        HPEN hThinGreenPen = CreatePen(PS_SOLID, 1, greenColor);
        HBRUSH hGreenFill = CreateSolidBrush(greenColor);
        HBRUSH hDarkFill = CreateSolidBrush(RGB(10, 20, 15)); 
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);

        SelectObject(hdc, hGreenPen);
        SelectObject(hdc, hNullBrush);
        RECT consoleRect = { 120, 70, 520, 380 };
        Rectangle(hdc, consoleRect.left, consoleRect.top, consoleRect.right, consoleRect.bottom);

        SelectObject(hdc, hGreenFill);
        RECT titleBar = { 120, 70, 520, 102 };
        FillRect(hdc, &titleBar, hGreenFill);

        Graphics graphics(hdc);

        if (g_showAdvancedUI) {
            SelectObject(hdc, hThinGreenPen);
            SelectObject(hdc, hNullBrush);
            
            int circleY = 150;
            int radius = 28;
            int centersX[] = { 200, 320, 440 };

            for (int i = 0; i < 3; i++) {
                Ellipse(hdc, centersX[i] - radius, circleY - radius, centersX[i] + radius, circleY + radius);
                Ellipse(hdc, centersX[i] - 12, circleY - 12, centersX[i] + 12, circleY + 12);
                MoveToEx(hdc, centersX[i] - radius, circleY, nullptr); LineTo(hdc, centersX[i] + radius, circleY);
                MoveToEx(hdc, centersX[i], circleY - radius, nullptr); LineTo(hdc, centersX[i], circleY + radius);
            }
            MoveToEx(hdc, centersX[0] + radius, circleY, nullptr); LineTo(hdc, centersX[1] - radius, circleY);
            MoveToEx(hdc, centersX[1] + radius, circleY, nullptr); LineTo(hdc, centersX[2] - radius, circleY);

            int boxWidth = 125;
            int boxHeight = 70;
            int startX = 145;
            int startY = 225;
            int gapX = 15;
            int gapY = 15;

            HFONT hSmallFont = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                           DEFAULT_PITCH | FF_SWISS, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hSmallFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, greenColor);

            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 2; c++) {
                    int bx = startX + c * (boxWidth + gapX);
                    int by = startY + r * (boxHeight + gapY);

                    SelectObject(hdc, hThinGreenPen);
                    SelectObject(hdc, hNullBrush);
                    Rectangle(hdc, bx, by, bx + boxWidth, by + boxHeight);

                    MoveToEx(hdc, bx + 10, by + boxHeight - 20, nullptr);
                    LineTo(hdc, bx + 35, by + 25);
                    LineTo(hdc, bx + 65, by + 45);
                    LineTo(hdc, bx + 95, by + 15);
                    LineTo(hdc, bx + 115, by + 50);

                    TextOutA(hdc, bx + 5, by + 5, "Q", 1);
                }
            }

            if (g_showTerminalWindow) {
                RECT termRect = { 165, 115, 505, 365 };
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
                TextOutA(hdc, termRect.left + 8, termRect.top + 5, "Terminal Hybrid (cmd.exe)", 26);

                int textY = termRect.top + 32;
                int textX = termRect.left + 10;

                // Renderizar salida en tiempo real del shell híbrido
                int maxLines = 13;
                int startIdx = (int)g_terminalOutputLines.size() > maxLines ? (int)g_terminalOutputLines.size() - maxLines : 0;
                int currentY = textY;

                for (size_t i = startIdx; i < g_terminalOutputLines.size(); i++) {
                    TextOutA(hdc, textX, currentY, g_terminalOutputLines[i].c_str(), (int)g_terminalOutputLines[i].length());
                    currentY += 14;
                }

                std::string promptLine = "> " + g_terminalInput + "_";
                TextOutA(hdc, textX, termRect.bottom - 25, promptLine.c_str(), (int)promptLine.length());
            }

            SelectObject(hdc, hOldFont);
            DeleteObject(hSmallFont);

        } else {
            int lockOffsetY = 0;
            if (g_animStep == 1) lockOffsetY = -15; 
            else if (g_animStep == 2) lockOffsetY = -28; 
            else if (g_animStep == 3) lockOffsetY = -8;  
            else if (g_animStep == 4) lockOffsetY = 0;   

            const wchar_t* imageFilename = L"candado.png";
            if (g_showError) {
                imageFilename = L"incorrect_password.png";
            } else if (g_unlocked) {
                imageFilename = L"candado_abierto.png";
            }

            Bitmap bitmap(imageFilename);
            int imgSize = 65; 
            int imgX = 320 - (imgSize / 2);
            int imgY = 175 + lockOffsetY;
            
            graphics.DrawImage(&bitmap, imgX, imgY, imgSize, imgSize);

            SelectObject(hdc, hGreenPen);
            SelectObject(hdc, hNullBrush);
            RECT inputRect = { 190, 290, 450, 330 };
            Rectangle(hdc, inputRect.left, inputRect.top, inputRect.right, inputRect.bottom);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, greenColor);
            
            HFONT hFont = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                      DEFAULT_PITCH | FF_SWISS, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            std::string displayPassword(g_passwordBuffer.length(), '*');
            RECT textRect = { inputRect.left + 10, inputRect.top + 5, inputRect.right - 10, inputRect.bottom - 5 };
            DrawTextA(hdc, displayPassword.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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

    wc.hCursor = (HCURSOR)LoadImageA(
        nullptr, 
        "TDLCursor.cur", 
        IMAGE_CURSOR, 
        0, 0, 
        LR_LOADFROMFILE | LR_DEFAULTSIZE
    );

    if (!wc.hCursor) {
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    }

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