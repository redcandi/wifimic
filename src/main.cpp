#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <thread>
#include <deque>
#include <mutex>
#include <algorithm>

std::deque<int16_t> audioQueue;
std::mutex audioMutex;

uint64_t producedSamples = 0;
uint64_t consumedSamples = 0;

void dataCallback(
    ma_device* device,
    void* output,
    const void* input,
    ma_uint32 frameCount
)
{

    int16_t* out = (int16_t*)output;

    std::lock_guard<std::mutex> lock(audioMutex);

    for (ma_uint32 i = 0; i < frameCount; i++)
    {
        if (!audioQueue.empty())
        {
            out[i] = audioQueue.front();
            audioQueue.pop_front();

             consumedSamples++;
        }
        else
        {
            out[i] = 0;
        }
    }

    static auto last = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::seconds>(now - last).count() >= 1)
    {
        std::cout
            << "Produced: " << producedSamples
            << "  Consumed: " << consumedSamples
            << "  Queue: " << audioQueue.size()
            << '\n';

        producedSamples = 0;
        consumedSamples = 0;

        last = now;
    }

    (void)input;
}

void runTCPServer();
void runUDPServer();

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);

    config.playback.format = ma_format_s16;
    config.playback.channels = 1;

    config.sampleRate = 44800; 

    config.dataCallback = dataCallback;

    ma_context context;

    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
    {
        std::cerr << "Failed to initialize context\n";
        return 1;
    }

    ma_device_info* playbackInfos;
    ma_uint32 playbackCount;

    ma_device_info* captureInfos;
    ma_uint32 captureCount;

    if (ma_context_get_devices(
            &context,
            &playbackInfos,
            &playbackCount,
            &captureInfos,
            &captureCount
        ) != MA_SUCCESS)
    {
        std::cerr << "Failed to enumerate devices\n";
        return 1;
    }

    std::cout << "\nPlayback Devices:\n";

    for (ma_uint32 i = 0; i < playbackCount; i++)
    {
        std::cout << i << " : " << playbackInfos[i].name << '\n';
    }

    for (ma_uint32 i = 0; i < playbackCount; i++)
    {
        std::string name = playbackInfos[i].name;

        if (name.find("CABLE Input") != std::string::npos)
        {
            config.playback.pDeviceID = &playbackInfos[3].id;

            std::cout << "Using VB-CABLE.\n";

            break;
        }
    }

    ma_device device;

    if (ma_device_init(&context, &config, &device) != MA_SUCCESS)
    {
        std::cerr << "Failed to initialize playback device\n";
        return 1;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        std::cerr << "Failed to start playback device\n";
        ma_device_uninit(&device);
        return 1;
    }

    std::cout << "Playback device started.\n";

    std::thread tcpThread(runTCPServer);

    runUDPServer();

    tcpThread.join();

    ma_device_uninit(&device);
    ma_context_uninit(&context);

    WSACleanup();

    return 0;
}

void runUDPServer()
{
    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_DGRAM,
        IPPROTO_UDP
    );

    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "UDP socket creation failed\n";
        return;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(45454);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        std::cerr << "UDP bind failed\n";
        closesocket(serverSocket);
        return;
    }

    std::cout << "Listening for UDP discovery on 45454...\n";

    char buffer[1024];

    while (true)
    {
        sockaddr_in clientAddress{};
        int clientLength = sizeof(clientAddress);

        int bytesReceived = recvfrom(
            serverSocket,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength
        );

        if (bytesReceived == SOCKET_ERROR)
            continue;

        buffer[bytesReceived] = '\0';

        char ip[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &clientAddress.sin_addr,
            ip,
            sizeof(ip)
        );

        std::cout << "\nUDP Packet\n";
        std::cout << "From: " << ip << '\n';
        std::cout << "Message: " << buffer << '\n';

        if (std::string(buffer) == "WHO_IS_WIFIMIC")
        {
            const char* response = "I_AM_WIFIMIC";

            sendto(
                serverSocket,
                response,
                strlen(response),
                0,
                reinterpret_cast<sockaddr*>(&clientAddress),
                clientLength
            );

            std::cout << "Discovery response sent.\n";
        }
    }

    closesocket(serverSocket);
}

void runTCPServer()
{
    SOCKET tcpSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (tcpSocket == INVALID_SOCKET)
    {
        std::cerr << "TCP socket creation failed\n";
        return;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(45455);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(
            tcpSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        std::cerr << "TCP bind failed\n";
        closesocket(tcpSocket);
        return;
    }

    if (listen(tcpSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "Listen failed\n";
        closesocket(tcpSocket);
        return;
    }

    std::cout << "Waiting for TCP connection on 45455...\n";

    while (true)
    {
        SOCKET clientSocket = accept(
            tcpSocket,
            nullptr,
            nullptr
        );

        if (clientSocket == INVALID_SOCKET)
        {
            std::cerr << "Accept failed\n";
            continue;
        }

        std::cout << "Android connected!\n";

    char buffer[4096];

    while (true)
    {
        int bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytesReceived == 0)
        {
            std::cout << "Android disconnected.\n";
            break;
        }

        if (bytesReceived == SOCKET_ERROR)
        {
            std::cerr << "Receive failed.\n";
            break;
        }

        int16_t* samples = reinterpret_cast<int16_t*>(buffer);

        int sampleCount = bytesReceived / sizeof(int16_t);
        producedSamples += sampleCount;

        const size_t MAX_QUEUE = 4096;

        std::lock_guard<std::mutex> lock(audioMutex);

        for (int i = 0; i < sampleCount; i++)
        {
            /*
            while (audioQueue.size() >= MAX_QUEUE)
            {
                audioQueue.pop_front();
            }
            */
            

            audioQueue.push_back(samples[i]);
        }
    }

    closesocket(clientSocket);

        std::cout << "Android disconnected.\n";
    }

    closesocket(tcpSocket);
}