#include "voip_audio.h"
#include <string>

std::string readSipMessage(SOCKET sock) {
    std::string raw;
    char ch;
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int r = recv(sock, &ch, 1, 0);
        if (r <= 0) return "";
        raw += ch;
    }
    size_t clPos = raw.find("Content-Length:");
    int contentLength = (clPos != std::string::npos) ? std::stoi(raw.substr(clPos + 15)) : 0;
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

std::string getHeader(const std::string& msg, const std::string& name) {
    size_t pos = msg.find(name);
    if (pos == std::string::npos) return "";
    size_t end = msg.find("\r\n", pos);
    return msg.substr(pos, end - pos);
}

int extractSdpPort(const std::string& sdp) {
    size_t pos = sdp.find("m=audio ");
    return (pos != std::string::npos) ? std::stoi(sdp.substr(pos + 8)) : 0;
}

std::string extractSdpIp(const std::string& sdp) {
    size_t pos = sdp.find("c=IN IP4 ");
    if (pos == std::string::npos) return "";
    size_t end = sdp.find("\r\n", pos);
    return sdp.substr(pos + 9, end - (pos + 9));
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srvAddr{};
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_addr.s_addr = INADDR_ANY;
    srvAddr.sin_port = htons(5060);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&srvAddr), sizeof(srvAddr)) == SOCKET_ERROR) {
        std::cout << "[Error] Puerto TCP 5060 ocupado.\n";
        return 1;
    }
    listen(listenSock, 5);
    std::cout << "=== RECEPTOR VoIP (UAS) CON AUDIO EN VIVO (TCP 5060) ===\n";

    sockaddr_in cliAddr{};
    int cliLen = sizeof(cliAddr);
    SOCKET cliSock = accept(listenSock, reinterpret_cast<sockaddr*>(&cliAddr), &cliLen);

    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliAddr.sin_addr, ipBuf, INET_ADDRSTRLEN);
    std::string peerIp(ipBuf);

    sockaddr_in locAddr{};
    int locLen = sizeof(locAddr);
    getsockname(cliSock, reinterpret_cast<sockaddr*>(&locAddr), &locLen);
    char locBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &locAddr.sin_addr, locBuf, INET_ADDRSTRLEN);
    std::string localIp(locBuf);

    SOCKET rtpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in rtpAddr{};
    rtpAddr.sin_family = AF_INET;
    rtpAddr.sin_addr.s_addr = INADDR_ANY;
    rtpAddr.sin_port = htons(40002);
    bind(rtpSock, reinterpret_cast<sockaddr*>(&rtpAddr), sizeof(rtpAddr));

    DWORD rcvTimeout = 200;
    setsockopt(rtpSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&rcvTimeout), sizeof(rcvTimeout));

    std::atomic<bool> callActive{false};
    std::thread txThread, rxThread;
    std::string remoteRtpIp = peerIp;
    int remoteRtpPort = 0;

    while (true) {
        std::string msg = readSipMessage(cliSock);
        if (msg.empty()) break;

        std::string firstLine = msg.substr(0, msg.find("\r\n"));
        std::cout << "[SIP <--] " << firstLine << "\n";

        std::string via = getHeader(msg, "Via:");
        std::string from = getHeader(msg, "From:");
        std::string to = getHeader(msg, "To:");
        std::string callId = getHeader(msg, "Call-ID:");
        std::string cseq = getHeader(msg, "CSeq:");

        if (msg.find("INVITE ") == 0) {
            std::string sdpIp = extractSdpIp(msg);
            if (!sdpIp.empty() && sdpIp != "127.0.0.1") remoteRtpIp = sdpIp;
            remoteRtpPort = extractSdpPort(msg);

            std::string trying = "SIP/2.0 100 Trying\r\n" + via + "\r\n" + from + "\r\n" +
                                 to + "\r\n" + callId + "\r\n" + cseq + "\r\nContent-Length: 0\r\n\r\n";
            send(cliSock, trying.c_str(), static_cast<int>(trying.size()), 0);

            std::string ringing = "SIP/2.0 180 Ringing\r\n" + via + "\r\n" + from + "\r\n" +
                                  to + ";tag=noogai3\r\n" + callId + "\r\n" + cseq + "\r\nContent-Length: 0\r\n\r\n";
            send(cliSock, ringing.c_str(), static_cast<int>(ringing.size()), 0);

            std::string sdp = "v=0\r\no=noogai3 1 1 IN IP4 " + localIp + "\r\n" +
                              "s=AvA14-Live\r\nc=IN IP4 " + localIp + "\r\n" +
                              "t=0 0\r\nm=audio 40002 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\na=sendrecv\r\n";

            std::string ok = "SIP/2.0 200 OK\r\n" + via + "\r\n" + from + "\r\n" +
                             to + ";tag=noogai3\r\n" + callId + "\r\n" + cseq + "\r\n" +
                             "Contact: <sip:noogai3@" + localIp + ":5060;transport=tcp>\r\n" +
                             "Content-Type: application/sdp\r\n" +
                             "Content-Length: " + std::to_string(sdp.size()) + "\r\n\r\n" + sdp;

            send(cliSock, ok.c_str(), static_cast<int>(ok.size()), 0);
            std::cout << "[SIP -->] 200 OK enviado. Esperando ACK...\n";
        }
        else if (msg.find("ACK ") == 0) {
            if (!callActive && remoteRtpPort > 0) {
                callActive = true;
                std::cout << "\n>>> LLAMADA CONECTADA EN VIVO (Full-Duplex) <<<\n";
                txThread = std::thread(audioCaptureAndSendThread, rtpSock, remoteRtpIp, remoteRtpPort, std::ref(callActive));
                rxThread = std::thread(audioReceiveAndPlayThread, rtpSock, std::ref(callActive));
            }
        }
        else if (msg.find("BYE ") == 0) {
            callActive = false;
            if (txThread.joinable()) txThread.join();
            if (rxThread.joinable()) rxThread.join();

            std::string byeOk = "SIP/2.0 200 OK\r\n" + via + "\r\n" + from + "\r\n" +
                                to + "\r\n" + callId + "\r\n" + cseq + "\r\nContent-Length: 0\r\n\r\n";
            send(cliSock, byeOk.c_str(), static_cast<int>(byeOk.size()), 0);
            std::cout << "[SIP -->] 200 OK (Llamada colgada).\n";
            break;
        }
    }

    callActive = false;
    if (txThread.joinable()) txThread.join();
    if (rxThread.joinable()) rxThread.join();
    closesocket(rtpSock);
    closesocket(cliSock);
    closesocket(listenSock);
    WSACleanup();
    return 0;
}
