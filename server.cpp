// server.cpp
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;
using namespace std::chrono;

const int PORT = 5000;
const int MAX_THREADS = 64;
const int MAX_MATRIX_SIZE = 1000;

mutex coutMutex;

// TLV Structure
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

bool recvTLV(SOCKET client, TLV &msg)
{
    uint8_t header[3];
    int r = recv(client, (char *)header, 3, MSG_WAITALL);
    if (r != 3)
        return false;
    msg.type = header[0];
    msg.length = (header[1] << 8) | header[2];
    msg.value.resize(msg.length);
    r = recv(client, (char *)msg.value.data(), msg.length, MSG_WAITALL);
    return r == msg.length;
}

bool sendTLV(SOCKET client, uint8_t type, const vector<uint8_t> &value)
{
    uint8_t header[3] = {type, (uint8_t)(value.size() >> 8), (uint8_t)(value.size() & 0xFF)};
    send(client, (char *)header, 3, 0);
    return send(client, (char *)value.data(), value.size(), 0) == value.size();
}

void processRows(vector<vector<int>> &matrix, int startRow, int endRow)
{
    int size = matrix.size();
    for (int i = startRow; i < endRow; ++i)
    {
        int minIdx = 0;
        for (int j = 1; j < size; ++j)
            if (matrix[i][j] < matrix[i][minIdx])
                minIdx = j;
        swap(matrix[i][minIdx], matrix[i][size - i - 1]);
    }
}

int runMatrixTask(int threads, vector<vector<int>> &matrix)
{
    int size = matrix.size();
    vector<thread> workers;
    int rowsPerThread = size / threads;
    int extra = size % threads;
    int start = 0;
    auto begin = high_resolution_clock::now();

    for (int i = 0; i < threads; ++i)
    {
        int end = start + rowsPerThread + (i < extra ? 1 : 0);
        workers.emplace_back(processRows, ref(matrix), start, end);
        start = end;
    }
    for (auto &t : workers)
        t.join();
    return duration_cast<milliseconds>(high_resolution_clock::now() - begin).count();
}

void handleClient(SOCKET clientSocket)
{
    try
    {
        TLV msg;

        cout << "[SERVER] Waiting HELLO...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x00)
            throw "Expected HELLO";
        cout << "[SERVER] Received HELLO\n";
        sendTLV(clientSocket, 0xFF, vector<uint8_t>{'W', 'E', 'L', 'C', 'O', 'M', 'E'});
        cout << "[SERVER] Sent WELCOME\n";

        cout << "[SERVER] Waiting SET_CONFIG...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x01 || msg.length != 4)
            throw "Invalid SET_CONFIG";
        uint32_t threads = readUint32(msg.value.data());
        if (threads == 0 || threads > MAX_THREADS)
            throw "Invalid thread count";
        cout << "[SERVER] Received thread count = " << threads << endl;

        cout << "[SERVER] Waiting SET_SIZE...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x02 || msg.length != 4)
            throw "Invalid SET_SIZE";
        uint32_t size = readUint32(msg.value.data());
        if (size == 0 || size > MAX_MATRIX_SIZE)
            throw "Invalid matrix size";
        cout << "[SERVER] Received matrix size = " << size << "x" << size << endl;

        cout << "[SERVER] Waiting SEND_DATA...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x03 || msg.length != size * size * 4)
            throw "Invalid matrix data";
        cout << "[SERVER] Received matrix data. Starting execution...\n";

        vector<vector<int>> matrix(size, vector<int>(size));
        const uint8_t *data = msg.value.data();
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; j < size; ++j)
                matrix[i][j] = readUint32(&data[4 * (i * size + j)]);

        sendTLV(clientSocket, 0x06, vector<uint8_t>{0x00});
        cout << "[SERVER] Sent EXEC_STARTED\n";

        int execTime = runMatrixTask(threads, matrix);
        cout << "[SERVER] Execution finished in " << execTime << " ms\n";

        vector<uint8_t> result;
        writeUint32(result, execTime);
        sendTLV(clientSocket, 0x0A, result);
        cout << "[SERVER] Sent EXEC_RESULT\n";

        vector<uint8_t> updated;
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; j < size; ++j)
                writeUint32(updated, matrix[i][j]);
        sendTLV(clientSocket, 0x08, updated);
        cout << "[SERVER] Sent updated matrix\n";

        cout << "[SERVER] Waiting CLIENT_EXIT...\n";

        sendTLV(clientSocket, 0x0B, {});
        cout << "[SERVER] Sent BYE" << endl;

        shutdown(clientSocket, SD_BOTH);
        closesocket(clientSocket);
    }
    catch (const char *err)
    {
        lock_guard<mutex> lock(coutMutex);
        cerr << "[!] Client error: " << err << endl;
    }
    closesocket(clientSocket);
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    cout << "[+] Server listening on port " << PORT << endl;

    while (true)
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        thread(handleClient, clientSocket).detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
