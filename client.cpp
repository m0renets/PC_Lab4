// client.cpp (with protocol logs and proper shutdown)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

const int PORT = 5000;

struct TLV
{
    uint8_t type;
    uint16_t length;
    vector<uint8_t> value;
};

uint32_t readUint32(const uint8_t *data)
{
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

void writeUint32(vector<uint8_t> &out, uint32_t value)
{
    out.push_back((value >> 24) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back(value & 0xFF);
}

bool recvTLV(SOCKET sock, TLV &msg)
{
    uint8_t header[3];
    int r = recv(sock, (char *)header, 3, MSG_WAITALL);
    if (r != 3)
        return false;
    msg.type = header[0];
    msg.length = (header[1] << 8) | header[2];
    msg.value.resize(msg.length);
    r = recv(sock, (char *)msg.value.data(), msg.length, MSG_WAITALL);
    return r == msg.length;
}

bool sendTLV(SOCKET sock, uint8_t type, const vector<uint8_t> &value)
{
    uint8_t header[3] = {type, (uint8_t)(value.size() >> 8), (uint8_t)(value.size() & 0xFF)};
    send(sock, (char *)header, 3, 0);
    return send(sock, (char *)value.data(), value.size(), 0) == value.size();
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

    TLV msg;

    // 1. HELLO
    cout << "[CLIENT] Sending HELLO...\n";
    vector<uint8_t> hello = {'H', 'E', 'L', 'L', 'O'};
    sendTLV(sock, 0x00, hello);

    if (!recvTLV(sock, msg) || msg.type != 0xFF)
    {
        cerr << "[-] Expected WELCOME" << endl;
        return 1;
    }
    cout << "[CLIENT] Received WELCOME\n";

    // 2. SET_CONFIG
    cout << "[CLIENT] Sending SET_CONFIG (threads = 8)...\n";
    vector<uint8_t> threadsPayload;
    writeUint32(threadsPayload, 8);
    sendTLV(sock, 0x01, threadsPayload);

    // 3. SET_SIZE
    cout << "[CLIENT] Sending SET_SIZE (100x100)...\n";
    uint32_t size = 100;
    vector<uint8_t> sizePayload;
    writeUint32(sizePayload, size);
    sendTLV(sock, 0x02, sizePayload);

    // 4. SEND_DATA
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
    sendTLV(sock, 0x03, matrixPayload);

    // 5. EXEC_STARTED
    if (!recvTLV(sock, msg) || msg.type != 0x06)
    {
        cerr << "[-] Expected EXEC_STARTED" << endl;
        return 1;
    }
    cout << "[CLIENT] Received EXEC_STARTED\n";

    // 6. EXEC_RESULT
    if (!recvTLV(sock, msg) || msg.type != 0x0A || msg.length != 4)
    {
        cerr << "[-] Expected EXEC_RESULT" << endl;
        return 1;
    }
    uint32_t timeMs = readUint32(msg.value.data());
    cout << "[CLIENT] Execution time: " << timeMs << " ms\n";

    // 7. MATRIX_RESULT
    if (!recvTLV(sock, msg) || msg.type != 0x08 || msg.length != size * size * 4)
    {
        cerr << "[-] Invalid matrix data" << endl;
        return 1;
    }
    cout << "[CLIENT] Updated matrix (first 5x5):" << endl;
    for (int i = 0; i < min(5u, size); ++i)
    {
        for (int j = 0; j < min(5u, size); ++j)
        {
            int val = readUint32(&msg.value[4 * (i * size + j)]);
            cout << val << "\t";
        }
        cout << endl;
    }

    // 8. CLIENT_EXIT
    cout << "[CLIENT] Sending CLIENT_EXIT...\n";
    sendTLV(sock, 0x0B, {});

    // 9. Wait for BYE
    if (!recvTLV(sock, msg) || msg.type != 0x0B)
    {
        cerr << "[-] Expected BYE" << endl;
        return 1;
    }
    cout << "[CLIENT] Received BYE. Closing connection.\n";

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    WSACleanup();
    return 0;
}
