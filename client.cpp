#include "tlv_functions.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

const int PORT = 5000;

bool handleServer(SOCKET sock)
{
    TLV msg;

    cout << "[CLIENT] Sending HELLO...\n";
    vector<uint8_t> hello = {'H', 'E', 'L', 'L', 'O'};
    sendTLV(sock, 0x00, hello);

    if (!recvTLV(sock, msg) || msg.type != 0x01)
    {
        cerr << "[-] Expected WELCOME" << endl;
        return false;
    }
    cout << "[CLIENT] Received WELCOME\n";

    cout << "[CLIENT] Sending SET_CONFIG (threads = 8)...\n";
    vector<uint8_t> threadsPayload;
    writeUint32(threadsPayload, 8);
    sendTLV(sock, 0x02, threadsPayload);

    cout << "[CLIENT] Sending SET_SIZE (100x100)...\n";
    uint32_t size = 100;
    vector<uint8_t> sizePayload;
    writeUint32(sizePayload, size);
    sendTLV(sock, 0x03, sizePayload);

    cout << "[CLIENT] Sending matrix data...\n";
    vector<vector<int>> matrix(size, vector<int>(size));
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 99);

    vector<uint8_t> matrixPayload;
    for (int i = 0; i < size; ++i)
        for (int j = 0; j < size; ++j)
        {
            matrix[i][j] = dis(gen);
            writeUint32(matrixPayload, matrix[i][j]);
        }
    sendTLV(sock, 0x04, matrixPayload);

    if (!recvTLV(sock, msg) || msg.type != 0x05)
    {
        cerr << "[-] Expected EXEC_STARTED" << endl;
        return false;
    }
    cout << "[CLIENT] Received EXEC_STARTED\n";

    if (!recvTLV(sock, msg) || msg.type != 0x06 || msg.length != 4)
    {
        cerr << "[-] Expected EXEC_RESULT" << endl;
        return false;
    }
    uint32_t timeMs = readUint32(msg.value.data());
    cout << "[CLIENT] Execution time: " << timeMs << " ms\n";

    if (!recvTLV(sock, msg) || msg.type != 0x07 || msg.length != size * size * 4)
    {
        cerr << "[-] Invalid matrix data" << endl;
        return false;
    }

    cout << "[CLIENT] Sending CLIENT_EXIT..." << endl;
    sendTLV(sock, 0x08, {});

    if (!recvTLV(sock, msg) || msg.type != 0x09)
    {
        cerr << "[-] Expected BYE" << endl;
        return false;
    }
    cout << "[CLIENT] Received BYE. Closing connection.\n";
    return true;
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sock, (sockaddr *)&server, sizeof(server)) < 0)
    {
        cerr << "[-] Cannot connect to server." << endl;
        return 1;
    }

    handleServer(sock);

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    WSACleanup();

    return 0;
}
