#include "voip_audio.h"
#include <conio.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>

static std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static std::string trimCrLf(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// RFC 3261: branch con magic cookie "z9hG4bK" único por nueva transacción
static std::string generateBranch() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "z9hG4bK" << std::hex << dist(gen);
    return oss.str();
}

static std::string readSipMessage(SOCKET sock) {
    std::string raw;
    char ch;
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int r = recv(sock, &ch, 1, 0);
        if (r <= 0) return "";
        raw += ch;
    }

    size_t headerEnd = raw.find("\r\n\r\n");
    std::string headersPart = raw.substr(0, headerEnd);
    std::string lowerHeaders = toLowerAscii(headersPart);

    int contentLength = 0;
    size_t clPos = lowerHeaders.find("\ncontent-length:");
    if (clPos == std::string::npos && lowerHeaders.rfind("content-length:", 0) == 0) {
        clPos = 0;
    } else if (clPos != std::string::npos) {
        clPos += 1;
    }
    if (clPos != std::string::npos) {
        size_t colon = headersPart.find(':', clPos);
        if (colon != std::string::npos) {
            size_t numStart = headersPart.find_first_of("0123456789", colon + 1);
            size_t lineEnd = headersPart.find("\r\n", colon);
            if (numStart != std::string::npos && (lineEnd == std::string::npos || numStart < lineEnd)) {
                contentLength = std::stoi(headersPart.substr(numStart));
            }
        }
    }

    if (contentLength > 0) {
        std::vector<char> body(contentLength);
        int total = 0;
        while (total < contentLength) {
            int r = recv(sock, body.data() + total, contentLength - total, 0);
            if (r <= 0) break;
            total += r;
        }
        raw.append(body.data(), total);
    }
    return raw;
}

// Busca cabecera ignorando mayúsculas/minúsculas y desdoblando líneas plegadas (SP/HT)
static std::string extractHeaderValue(const std::string& msg, const std::string& headerName) {
    size_t bodySep = msg.find("\r\n\r\n");
    std::string head = (bodySep == std::string::npos) ? msg : msg.substr(0, bodySep);
    std::string lowerHead = toLowerAscii(head);
    std::string target = toLowerAscii(headerName);
    if (!target.empty() && target.back() == ':') {
        target.pop_back();
    }

    size_t pos = 0;
    while (pos < head.size()) {
        size_t lineEnd = head.find("\r\n", pos);
        std::string line = (lineEnd == std::string::npos) ? head.substr(pos) : head.substr(pos, lineEnd - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = trimCrLf(toLowerAscii(line.substr(0, colon)));
            if (name == target) {
                std::string val = trimCrLf(line.substr(colon + 1));
                size_t nextPos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
                while (nextPos < head.size()) {
                    size_t nextEnd = head.find("\r\n", nextPos);
                    std::string cont = (nextEnd == std::string::npos)
                        ? head.substr(nextPos)
                        : head.substr(nextPos, nextEnd - nextPos);
                    if (!cont.empty() && (cont[0] == ' ' || cont[0] == '\t')) {
                        val += " " + trimCrLf(cont);
                        nextPos = (nextEnd == std::string::npos) ? head.size() : nextEnd + 2;
                    } else {
                        break;
                    }
                }
                return val;
            }
        }
        if (lineEnd == std::string::npos) break;
        pos = lineEnd + 2;
    }
    return "";
}

static std::string extractTagParam(const std::string& headerVal) {
    std::string lowerVal = toLowerAscii(headerVal);
    size_t tagPos = lowerVal.find(";tag=");
    if (tagPos == std::string::npos) {
        tagPos = lowerVal.find("; tag=");
        if (tagPos == std::string::npos) return "";
        tagPos += 1;
    }
    size_t valStart = tagPos + 5;
    std::string tag = headerVal.substr(valStart);
    size_t semi = tag.find(';');
    if (semi != std::string::npos) tag = tag.substr(0, semi);
    return trimCrLf(tag);
}

static bool hasMultipleVias(const std::string& msg) {
    size_t bodySep = msg.find("\r\n\r\n");
    std::string head = (bodySep == std::string::npos) ? msg : msg.substr(0, bodySep);
    std::string lowerHead = toLowerAscii(head);

    int viaCount = 0;
    size_t pos = 0;
    while (pos < head.size()) {
        size_t lineEnd = head.find("\r\n", pos);
        std::string line = (lineEnd == std::string::npos) ? head.substr(pos) : head.substr(pos, lineEnd - pos);
        if (!line.empty() && line[0] != ' ' && line[0] != '\t') {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = trimCrLf(toLowerAscii(line.substr(0, colon)));
                if (name == "via") {
                    ++viaCount;
                }
            }
        }
        if (lineEnd == std::string::npos) break;
        pos = lineEnd + 2;
    }
    return viaCount > 1;
}

static int extractSdpPort(const std::string& sdp) {
    size_t pos = sdp.find("m=audio ");
    if (pos == std::string::npos) return 0;
    size_t start = pos + 8;
    while (start < sdp.size() && sdp[start] == ' ') ++start;
    if (start >= sdp.size()) return 0;
    return std::stoi(sdp.substr(start));
}

static std::string extractSdpIp(const std::string& sdp) {
    size_t pos = sdp.find("c=IN IP4 ");
    if (pos == std::string::npos) return "";
    size_t start = pos + 9;
    size_t end = sdp.find("\r\n", start);
    std::string ip = (end == std::string::npos) ? sdp.substr(start) : sdp.substr(start, end - start);
    return trimCrLf(ip);
}

static std::string buildAckMessage(
    const std::string& targetUri,
    const std::string& localIp,
    const std::string& fromTag,
    const std::string& toTag,
    const std::string& callId,
    int cseqNum
) {
    std::string toHeader = toTag.empty()
        ? ("To: <" + targetUri + ">")
        : ("To: <" + targetUri + ">;tag=" + toTag);

    return "ACK " + targetUri + " SIP/2.0\r\n"
           "Via: SIP/2.0/TCP " + localIp + ":5060;branch=" + generateBranch() + ";rport\r\n"
           "Max-Forwards: 70\r\n"
           "From: \"Windows\" <sip:windows@" + localIp + ">;tag=" + fromTag + "\r\n"
           + toHeader + "\r\n"
           "Call-ID: " + callId + "\r\n"
           "CSeq: " + std::to_string(cseqNum) + " ACK\r\n"
           "Content-Length: 0\r\n\r\n";
}

int main(int argc, char* argv[]) {
    std::string targetIp;
    if (argc > 1) {
        targetIp = argv[1];
    } else {
        std::cout << "Ingresa la IP local de Android (StickElegans): ";
        std::getline(std::cin, targetIp);
        targetIp = trimCrLf(targetIp);
        if (targetIp.empty()) targetIp = "127.0.0.1";
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cout << "[Error] Fallo al inicializar Winsock.\n";
        return 1;
    }

    SOCKET rtpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rtpSock == INVALID_SOCKET) {
        std::cout << "[Error] No se pudo crear socket RTP UDP.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in rtpLocal{};
    rtpLocal.sin_family = AF_INET;
    rtpLocal.sin_addr.s_addr = INADDR_ANY;
    rtpLocal.sin_port = 0;
    if (bind(rtpSock, reinterpret_cast<sockaddr*>(&rtpLocal), sizeof(rtpLocal)) == SOCKET_ERROR) {
        std::cout << "[Error] No se pudo enlazar puerto RTP local.\n";
        closesocket(rtpSock);
        WSACleanup();
        return 1;
    }

    int rtpLen = sizeof(rtpLocal);
    getsockname(rtpSock, reinterpret_cast<sockaddr*>(&rtpLocal), &rtpLen);
    int localRtpPort = ntohs(rtpLocal.sin_port);

    DWORD rcvTimeout = 200;
    setsockopt(rtpSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&rcvTimeout), sizeof(rcvTimeout));

    SOCKET tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSock == INVALID_SOCKET) {
        std::cout << "[Error] No se pudo crear socket SIP TCP.\n";
        closesocket(rtpSock);
        WSACleanup();
        return 1;
    }

    sockaddr_in srvAddr{};
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(5060);
    if (inet_pton(AF_INET, targetIp.c_str(), &srvAddr.sin_addr) != 1) {
        std::cout << "[Error] IP destino invalida: " << targetIp << "\n";
        closesocket(tcpSock);
        closesocket(rtpSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[SIP] Conectando a " << targetIp << ":5060...\n";
    if (connect(tcpSock, reinterpret_cast<sockaddr*>(&srvAddr), sizeof(srvAddr)) == SOCKET_ERROR) {
        std::cout << "[Error] No se pudo conectar con " << targetIp << ":5060\n";
        closesocket(tcpSock);
        closesocket(rtpSock);
        WSACleanup();
        return 1;
    }

    sockaddr_in locTcp{};
    int locTcpLen = sizeof(locTcp);
    getsockname(tcpSock, reinterpret_cast<sockaddr*>(&locTcp), &locTcpLen);
    char locIpBuf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &locTcp.sin_addr, locIpBuf, INET_ADDRSTRLEN);
    std::string localIp(locIpBuf);

    std::string callId = "stickelegans-call-001@" + localIp;
    std::string fromTag = "windows01";
    std::string targetUri = "sip:android@" + targetIp;
    int inviteCSeq = 1;

    std::string sdp = "v=0\r\n"
                      "o=windows 100 100 IN IP4 " + localIp + "\r\n"
                      "s=StickElegans-Call\r\n"
                      "c=IN IP4 " + localIp + "\r\n"
                      "t=0 0\r\n"
                      "m=audio " + std::to_string(localRtpPort) + " RTP/AVP 0\r\n"
                      "a=rtpmap:0 PCMU/8000\r\n"
                      "a=sendrecv\r\n";

    std::string invite = "INVITE " + targetUri + " SIP/2.0\r\n"
                         "Via: SIP/2.0/TCP " + localIp + ":5060;branch=" + generateBranch() + ";rport\r\n"
                         "Max-Forwards: 70\r\n"
                         "From: \"Windows\" <sip:windows@" + localIp + ">;tag=" + fromTag + "\r\n"
                         "To: <" + targetUri + ">\r\n"
                         "Call-ID: " + callId + "\r\n"
                         "CSeq: " + std::to_string(inviteCSeq) + " INVITE\r\n"
                         "Contact: <sip:windows@" + localIp + ":5060;transport=tcp>\r\n"
                         "Content-Type: application/sdp\r\n"
                         "Content-Length: " + std::to_string(sdp.size()) + "\r\n\r\n" + sdp;

    send(tcpSock, invite.c_str(), static_cast<int>(invite.size()), 0);
    std::cout << "[SIP -->] INVITE enviado a " << targetUri << " (RTP local UDP :" << localRtpPort << ")\n";

    std::string toTag;
    std::string remoteRtpIp = targetIp;
    int remoteRtpPort = 0;

    while (true) {
        std::string res = readSipMessage(tcpSock);
        if (res.empty()) {
            std::cout << "[SIP] Conexion cerrada antes de completar la llamada.\n";
            closesocket(tcpSock);
            closesocket(rtpSock);
            WSACleanup();
            return 1;
        }

        if (hasMultipleVias(res)) {
            std::cout << "[SIP] Respuesta descartada por contener multiples cabeceras Via.\n";
            continue;
        }

        size_t firstLineEnd = res.find("\r\n");
        std::string statusLine = (firstLineEnd == std::string::npos) ? res : res.substr(0, firstLineEnd);
        std::cout << "[SIP <--] " << statusLine << "\n";

        if (res.find("SIP/2.0 200") == 0) {
            toTag = extractTagParam(extractHeaderValue(res, "To"));
            std::string sdpIp = extractSdpIp(res);
            if (!sdpIp.empty() && sdpIp != "127.0.0.1" && sdpIp != "0.0.0.0") {
                remoteRtpIp = sdpIp;
            }
            remoteRtpPort = extractSdpPort(res);
            if (remoteRtpPort <= 0) {
                remoteRtpPort = 40002;
            }
            break;
        }
    }

    std::string ack = buildAckMessage(targetUri, localIp, fromTag, toTag, callId, inviteCSeq);
    send(tcpSock, ack.c_str(), static_cast<int>(ack.size()), 0);
    std::cout << "[SIP -->] ACK enviado. RTP destino: " << remoteRtpIp << ":" << remoteRtpPort << "\n";

    std::atomic<bool> callActive{true};
    std::thread txThread(audioCaptureAndSendThread, rtpSock, remoteRtpIp, remoteRtpPort, std::ref(callActive));
    std::thread rxThread(audioReceiveAndPlayThread, rtpSock, std::ref(callActive));

    std::cout << "\n>>> EN LLAMADA EN VIVO. Presiona cualquier tecla para colgar o espera a que cuelguen en Android... <<<\n";

    bool hungUpByLocalUser = false;
    std::string incomingBye;
    int nextLocalCSeq = inviteCSeq + 1;

    while (callActive) {
        if (_kbhit()) {
            (void)_getch();
            hungUpByLocalUser = true;
            break;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(tcpSock, &readSet);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(tcpSock, &readSet)) {
            std::string sipMsg = readSipMessage(tcpSock);
            if (sipMsg.empty()) {
                std::cout << "\n[SIP] Conexion cerrada desde Android.\n";
                break;
            }
            if (hasMultipleVias(sipMsg)) {
                continue;
            }
            size_t lineEnd = sipMsg.find("\r\n");
            std::string firstLine = (lineEnd == std::string::npos) ? sipMsg : sipMsg.substr(0, lineEnd);
            std::cout << "\n[SIP <--] " << firstLine << "\n";

            // Reenviar ACK si el remoto retransmite 200 OK
            if (sipMsg.find("SIP/2.0 200") == 0) {
                std::string dupAck = buildAckMessage(targetUri, localIp, fromTag, toTag, callId, inviteCSeq);
                send(tcpSock, dupAck.c_str(), static_cast<int>(dupAck.size()), 0);
            } else if (sipMsg.find("BYE ") == 0) {
                incomingBye = sipMsg;
                break;
            }
        } else if (sel == SOCKET_ERROR) {
            std::cout << "\n[SIP] Error en socket TCP durante la llamada.\n";
            break;
        }
    }

    callActive = false;
    if (txThread.joinable()) txThread.join();
    if (rxThread.joinable()) rxThread.join();

    if (!incomingBye.empty()) {
        std::string byeVia = extractHeaderValue(incomingBye, "Via");
        std::string byeFrom = extractHeaderValue(incomingBye, "From");
        std::string byeTo = extractHeaderValue(incomingBye, "To");
        std::string byeCallId = extractHeaderValue(incomingBye, "Call-ID");
        std::string byeCSeq = extractHeaderValue(incomingBye, "CSeq");

        std::string byeOk = "SIP/2.0 200 OK\r\n";
        if (!byeVia.empty()) byeOk += "Via: " + byeVia + "\r\n";
        if (!byeFrom.empty()) byeOk += "From: " + byeFrom + "\r\n";
        if (!byeTo.empty()) byeOk += "To: " + byeTo + "\r\n";
        if (!byeCallId.empty()) byeOk += "Call-ID: " + byeCallId + "\r\n";
        if (!byeCSeq.empty()) byeOk += "CSeq: " + byeCSeq + "\r\n";
        byeOk += "Content-Length: 0\r\n\r\n";

        send(tcpSock, byeOk.c_str(), static_cast<int>(byeOk.size()), 0);
        std::cout << "[SIP -->] 200 OK enviado a BYE remoto.\n";
    } else if (hungUpByLocalUser) {
        std::string toHeader = toTag.empty()
            ? ("To: <" + targetUri + ">")
            : ("To: <" + targetUri + ">;tag=" + toTag);

        std::string bye = "BYE " + targetUri + " SIP/2.0\r\n"
                          "Via: SIP/2.0/TCP " + localIp + ":5060;branch=" + generateBranch() + ";rport\r\n"
                          "Max-Forwards: 70\r\n"
                          "From: \"Windows\" <sip:windows@" + localIp + ">;tag=" + fromTag + "\r\n"
                          + toHeader + "\r\n"
                          "Call-ID: " + callId + "\r\n"
                          "CSeq: " + std::to_string(nextLocalCSeq++) + " BYE\r\n"
                          "Content-Length: 0\r\n\r\n";

        send(tcpSock, bye.c_str(), static_cast<int>(bye.size()), 0);
        std::string byeRes = readSipMessage(tcpSock);
        if (!byeRes.empty()) {
            size_t bEnd = byeRes.find("\r\n");
            std::cout << "[SIP <--] " << ((bEnd == std::string::npos) ? byeRes : byeRes.substr(0, bEnd)) << "\n";
        }
    }

    std::cout << "[SIP] Llamada finalizada correctamente.\n";

    closesocket(rtpSock);
    closesocket(tcpSock);
    WSACleanup();
    return 0;
}