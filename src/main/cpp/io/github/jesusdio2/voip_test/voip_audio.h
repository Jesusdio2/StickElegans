#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

// Codificacion ITU-T G.711 u-law (PCM 16-bit -> 8-bit u-law)
inline uint8_t linearToUlaw(int16_t pcm) {
    const int BIAS = 0x84;
    const int CLIP = 32635;
    int sign = (pcm >> 8) & 0x80;
    if (sign != 0) pcm = -pcm;
    if (pcm > CLIP) pcm = CLIP;
    pcm += BIAS;

    int exponent = 7;
    for (int expMask = 0x4000; (pcm & expMask) == 0 && exponent > 0; exponent--, expMask >>= 1) {}
    int mantissa = (pcm >> (exponent + 3)) & 0x0F;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

// Decodificacion ITU-T G.711 u-law (8-bit u-law -> PCM 16-bit)
inline int16_t ulawToLinear(uint8_t ulaw) {
    ulaw = ~ulaw;
    int sign = (ulaw & 0x80);
    int exponent = (ulaw >> 4) & 0x07;
    int mantissa = ulaw & 0x0F;
    int sample = ((mantissa << 3) + 0x84) << exponent;
    sample -= 0x84;
    return static_cast<int16_t>(sign != 0 ? -sample : sample);
}

// Hilo 1: Captura microfono -> Codifica G.711 -> Envia RTP cada 20ms
inline void audioCaptureAndSendThread(SOCKET udpSock, std::string destIp, int destPort, std::atomic<bool>& running) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(destPort);
    inet_pton(AF_INET, destIp.c_str(), &dest.sin_addr);

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 8000;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = 16000;

    HWAVEIN hWaveIn = nullptr;
    if (waveInOpen(&hWaveIn, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        std::cout << "[Audio Error] No se detecto microfono disponible.\n";
        return;
    }

    const int NUM_BUFS = 4;
    const int SAMPLES_PER_PKT = 160; // 20 ms a 8000 Hz
    const int BYTES_PER_BUF = SAMPLES_PER_PKT * 2;

    std::vector<WAVEHDR> headers(NUM_BUFS);
    std::vector<std::vector<int16_t>> buffers(NUM_BUFS, std::vector<int16_t>(SAMPLES_PER_PKT));

    for (int i = 0; i < NUM_BUFS; ++i) {
        headers[i] = {};
        headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        headers[i].dwBufferLength = BYTES_PER_BUF;
        waveInPrepareHeader(hWaveIn, &headers[i], sizeof(WAVEHDR));
        waveInAddBuffer(hWaveIn, &headers[i], sizeof(WAVEHDR));
    }

    waveInStart(hWaveIn);
    std::cout << "[Microfono] Capturando voz y enviando RTP a " << destIp << ":" << destPort << "...\n";

    uint16_t seq = 1;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0x14AA2026;
    int idx = 0;

    while (running) {
        if (headers[idx].dwFlags & WHDR_DONE) {
            uint8_t pkt[172]{};
            pkt[0] = 0x80; // V=2
            pkt[1] = 0x00; // PT=0 (PCMU)
            pkt[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
            pkt[3] = static_cast<uint8_t>(seq & 0xFF);
            pkt[4] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
            pkt[5] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
            pkt[6] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
            pkt[7] = static_cast<uint8_t>(timestamp & 0xFF);
            pkt[8] = static_cast<uint8_t>((ssrc >> 24) & 0xFF);
            pkt[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
            pkt[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
            pkt[11] = static_cast<uint8_t>(ssrc & 0xFF);

            for (int s = 0; s < SAMPLES_PER_PKT; ++s) {
                pkt[12 + s] = linearToUlaw(buffers[idx][s]);
            }

            sendto(udpSock, reinterpret_cast<char*>(pkt), 172, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

            seq++;
            timestamp += SAMPLES_PER_PKT;

            headers[idx].dwFlags &= ~WHDR_DONE;
            waveInAddBuffer(hWaveIn, &headers[idx], sizeof(WAVEHDR));
            idx = (idx + 1) % NUM_BUFS;
        } else {
            Sleep(2);
        }
    }

    waveInStop(hWaveIn);
    waveInReset(hWaveIn);
    for (int i = 0; i < NUM_BUFS; ++i) {
        waveInUnprepareHeader(hWaveIn, &headers[i], sizeof(WAVEHDR));
    }
    waveInClose(hWaveIn);
    std::cout << "[Microfono] Detenido.\n";
}

// Hilo 2: Recibe RTP -> Decodifica G.711 -> Reproduce en bocina en vivo
inline void audioReceiveAndPlayThread(SOCKET udpSock, std::atomic<bool>& running) {
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 8000;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = 16000;

    HWAVEOUT hWaveOut = nullptr;
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        std::cout << "[Audio Error] No se detecto bocina o salida de audio.\n";
        return;
    }

    const int NUM_BUFS = 8;
    const int MAX_SAMPLES = 320;
    std::vector<WAVEHDR> headers(NUM_BUFS);
    std::vector<std::vector<int16_t>> buffers(NUM_BUFS, std::vector<int16_t>(MAX_SAMPLES));

    for (int i = 0; i < NUM_BUFS; ++i) {
        headers[i] = {};
        headers[i].dwFlags = WHDR_DONE;
    }

    std::cout << "[Bocina] Esperando flujo de audio RTP entrante...\n";
    uint8_t pkt[1500];
    int idx = 0;
    uint64_t rxPackets = 0;

    while (running) {
        int bytes = recvfrom(udpSock, reinterpret_cast<char*>(pkt), sizeof(pkt), 0, nullptr, nullptr);
        if (bytes > 12 && (pkt[0] & 0xC0) == 0x80) {
            uint8_t pt = pkt[1] & 0x7F;
            if (pt == 0) { // Solo PCMU
                int cc = pkt[0] & 0x0F;
                bool hasExt = (pkt[0] & 0x10) != 0;
                int offset = 12 + (4 * cc);
                if (hasExt && bytes >= offset + 4) {
                    int extWords = (pkt[offset + 2] << 8) | pkt[offset + 3];
                    offset += 4 + (extWords * 4);
                }
                int payloadLen = bytes - offset;
                if (payloadLen > 0 && payloadLen <= MAX_SAMPLES) {
                    ++rxPackets;
                    if (rxPackets == 1 || rxPackets % 50 == 0) {
                        std::cout << "[Bocina] Paquetes RTP PCMU recibidos: " << rxPackets << " (" << payloadLen << " bytes)\n";
                    }

                    // Esperar a que un buffer de bocina este libre
                    while (!(headers[idx].dwFlags & WHDR_DONE) && running) {
                        Sleep(1);
                    }
                    if (!running) break;

                    if (headers[idx].dwFlags & WHDR_PREPARED) {
                        waveOutUnprepareHeader(hWaveOut, &headers[idx], sizeof(WAVEHDR));
                    }

                    for (int i = 0; i < payloadLen; ++i) {
                        buffers[idx][i] = ulawToLinear(pkt[offset + i]);
                    }

                    headers[idx] = {};
                    headers[idx].lpData = reinterpret_cast<LPSTR>(buffers[idx].data());
                    headers[idx].dwBufferLength = payloadLen * 2;
                    waveOutPrepareHeader(hWaveOut, &headers[idx], sizeof(WAVEHDR));
                    waveOutWrite(hWaveOut, &headers[idx], sizeof(WAVEHDR));

                    idx = (idx + 1) % NUM_BUFS;
                }
            }
        }
    }

    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFS; ++i) {
        if (headers[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
        }
    }
    waveOutClose(hWaveOut);
    std::cout << "[Bocina] Detenida.\n";
}