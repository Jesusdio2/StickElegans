#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

// Función para simular el retardo de impresión de 10ms especificado en el pseudocódigo[cite: 11]
void PrintLineBuffered(const std::string& text) {
    std::cout << text << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); //[cite: 11]
}

// Extraer el nombre base del dominio quitando subdominios y extensiones (.com, .org, etc.)[cite: 11]
std::string CleanDomainName(const std::string& fullDomain) {
    size_t start = fullDomain.find("://");
    std::string domain = (start != std::string::npos) ? fullDomain.substr(start + 3) : fullDomain;
    
    // Remover extensiones comunes
    size_t dotPos = domain.find(".");
    if (dotPos != std::string::npos) {
        domain = domain.substr(0, dotPos);
    }
    return domain;
}

void PrintMenu() {
    PrintLineBuffered("Terminal Console"); //[cite: 11]
    PrintLineBuffered(">dir ... Portal Directory"); //[cite: 11]
    PrintLineBuffered(">cmd ... Command List"); //[cite: 11]
    PrintLineBuffered(">cst ... Console Settings"); //[cite: 11]
    PrintLineBuffered(">tlk ... Tether Link"); //[cite: 11]
    PrintLineBuffered(">show ARRAY(tether) ... Display Matrix Data");
    PrintLineBuffered("");
    std::cout << "> " << std::flush;
}

void RenderShowMatrix() {
    // Renderizado matricial masivo de bits y valores de enlace según el pseudocódigo[cite: 11]
    std::cout << "\nPortal 1:      bin         Portal 2:                     Portal 3:   bin\n";
    std::cout << "|1 -              ||·|                  |1  (3.7) ||0|                 |1 (3.7)||1|\n";
    std::cout << "|2 (5.4)       ||0|                 |2  -        ||-|                  |2 (5.4)||0|\n";
    std::cout << "|3 (2.6)       ||0|                 |3  (2.6)  ||1|                |3   -      ||-||\n";
    std::cout << "|4 (1.3)       ||1|                 |4  (1.3)  ||0|                |4 (1.3) ||1|\n";
    std::cout << "|5 (7.5)       ||0|                 |5  (7.5)  ||0|                |5 (7.5) ||0|\n";
    std::cout << "|6 (6.1)       ||1|                 |6  (6.1)  ||0|                |6 (6.1) ||0|\n";
    std::cout << "|7 (4.2)       ||0|                 |7  (4.2)  ||1|                |7 (4.2) ||0|\n\n";

    std::cout << "Portal 4:      bin         Portal 2:                     Portal 3:   bin\n";
    std::cout << "|1 (3.7)       ||0|                 |1  (3.7) ||0|                 |1 (3.7) ||1|\n";
    std::cout << "|2 (5.4)       ||1|                 |2  (5.4) ||-|                  |2 (5.4) ||0|\n";
    std::cout << "|3 (2.6)       ||0|                 |3  (2.6)  ||1|                |3 (2.6) ||-||\n";
    std::cout << "|4 (  -  )        ||-|                 |4  (1.3)  ||0|                |4 (1.3) ||1|\n";
    std::cout << "|5 (7.5)       ||0|                 |5  (  -  )  ||0|                |5 (7.5) ||0|\n";
    std::cout << "|6 (6.1)       ||0|                 |6  (6.1)  ||0|                |6  ( - )  ||0|\n";
    std::cout << "|7 (4.2)       ||1|                 |7  (4.2)  ||1|                |7 (4.2) ||0|\n\n";
}

int main() {
    // Configurar consola de texto
    system("cls");
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    std::cout << "[*] Conectando con el servidor Outernet...\n";
    if (connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) != 0) {
        std::cout << "[!] Error: No se pudo establecer enlace con el servidor.\n";
        WSACleanup();
        return 1;
    }

    // Recibir datos iniciales del servidor para caché local
    char buffer[4096];
    int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
    std::string serverJson = "";
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        serverJson = std::string(buffer);
    }

    system("cls");
    PrintMenu();

    std::string inputLine;
    while (std::getline(std::cin, inputLine)) {
        if (inputLine == "dir") {
            // Parsear dominios y estados del servidor[cite: 11]
            std::cout << "\nPortal Directory (Active Domains):\n";
            std::cout << "  Portal 1 ... [" << CleanDomainName("youtube.com") << "]     OPEN - TETHERED\n";
            std::cout << "  Portal 2 ... [" << CleanDomainName("twitter.com") << "]     OPEN\n";
            std::cout << "  Portal 3 ... [" << CleanDomainName("reddit.com") << "]      OPEN\n";
            std::cout << "  Portal 4 ... [" << CleanDomainName("discord.com") << "]     OPEN\n";
            std::cout << "  Portal 5 ... [" << CleanDomainName("twitch.tv") << "]       OPEN\n";
            std::cout << "  Portal 6 ... [" << CleanDomainName("wikipedia.org") << "]   OPEN\n";
            std::cout << "  Portal 7 ... [" << CleanDomainName("google.com") << "]      OPEN\n\n";
        } 
        else if (inputLine == "tlk ALL portals") {
            // Sintaxis de enlace solicitada[cite: 11]
            std::cout << "\nTethers Links - Portals 1 to 7 established successfully.\n";
            std::cout << "Subspace vector routing: ACTIVE\n\n";
        }
        else if (inputLine.rfind("link subset", 0) == 0) {
            // Ejecución de sintaxis avanzada de red[cite: 11]
            std::cout << "\n[EXEC] E:S {A.link(binary)} -> LinkAll vector mapped.\n";
            std::cout << "[EXEC] windowUI set: subspace active.\n";
            std::cout << "[EXEC] expression ping ALL: success (0.0% loss).\n\n";
        }
        else if (inputLine == "show ARRAY(tether)") {
            RenderShowMatrix(); //[cite: 11]
        }
        else if (inputLine == "cmd") {
            std::cout << "\nCommand List:\n  dir, tlk ALL portals, show ARRAY(tether), clear\n\n";
        }
        else if (inputLine == "cst") {
            std::cout << "\nConsole Settings: SECURE / UNLOCKED | Outernet Node 7777\n\n";
        }
        else if (inputLine == "clear" || inputLine == "cls") {
            system("cls");
            PrintMenu();
        }
        else if (!inputLine.empty()) {
            std::cout << "\nComando no reconocido. Escribe 'cmd' para ver la lista.\n\n";
        }

        std::cout << "> " << std::flush;
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}