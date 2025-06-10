#include "tlv_functions.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <atomic>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;
using namespace std::chrono;

const int PORT = 5000;
const int MAX_THREADS = 64;
const int MAX_MATRIX_SIZE = 127;

mutex coutMutex;

atomic<bool> serverRunning(true);

void consoleControl(SOCKET serverSocket)
{
    string input;
    while (true)
    {
        cout << "Type 'stop' to stop server: \n";
        cin >> input;
        if (input == "stop")
        {
            serverRunning = false;
            closesocket(serverSocket);
            break;
        }
    }
}

int runMatrixTask(int threads, std::vector<std::vector<int>> &matrix)
{
    int size = matrix.size();
    std::vector<std::thread> workers;
    int rowsPerThread = size / threads;
    int extra = size % threads;
    int start = 0;
    auto begin = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < threads; ++i)
    {
        int end = start + rowsPerThread + (i < extra ? 1 : 0);
        workers.emplace_back([&, start, end]() {
            for (int row = start; row < end; ++row)
            {
                int minIdx = 0;
                for (int col = 1; col < size; ++col)
                {
                    if (matrix[row][col] < matrix[row][minIdx])
                        minIdx = col;
                }
                std::swap(matrix[row][minIdx], matrix[row][size - row - 1]);
            }
        });
        start = end;
    }

    for (auto &t : workers)
        t.join();

    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - begin)
        .count();
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
        sendTLV(clientSocket, 0x01, vector<uint8_t>{'W', 'E', 'L', 'C', 'O', 'M', 'E'});
        cout << "[SERVER] Sent WELCOME\n";

        cout << "[SERVER] Waiting SET_CONFIG...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x02 || msg.length != 4)
            throw "Invalid SET_CONFIG";
        uint32_t threads = readUint32(msg.value.data());
        if (threads == 0 || threads > MAX_THREADS)
            throw "Invalid thread count";
        cout << "[SERVER] Received thread count = " << threads << endl;

        cout << "[SERVER] Waiting SET_SIZE...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x03 || msg.length != 4)
            throw "Invalid SET_SIZE";
        uint32_t size = readUint32(msg.value.data());
        if (size == 0 || size > MAX_MATRIX_SIZE)
            throw "Invalid matrix size";
        cout << "[SERVER] Received matrix size = " << size << "x" << size << endl;

        cout << "[SERVER] Waiting SEND_DATA...\n";
        if (!recvTLV(clientSocket, msg) || msg.type != 0x04 || msg.length != size * size * 4)
            throw "Invalid matrix data";
        cout << "[SERVER] Received matrix data. Starting execution...\n";

        vector<vector<int>> matrix(size, vector<int>(size));
        const uint8_t *data = msg.value.data();
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; j < size; ++j)
                matrix[i][j] = readUint32(&data[4 * (i * size + j)]);

        sendTLV(clientSocket, 0x05, vector<uint8_t>{0x00});
        cout << "[SERVER] Sent EXEC_STARTED\n";

        int execTime = runMatrixTask(threads, matrix);
        cout << "[SERVER] Execution finished in " << execTime << " ms\n";

        vector<uint8_t> result;
        writeUint32(result, execTime);
        sendTLV(clientSocket, 0x06, result);
        cout << "[SERVER] Sent EXEC_RESULT\n";

        vector<uint8_t> updated;
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; j < size; ++j)
                writeUint32(updated, matrix[i][j]);
        sendTLV(clientSocket, 0x07, updated);
        cout << "[SERVER] Sent updated matrix\n";

        if (!recvTLV(clientSocket, msg) || msg.type != 0x08)
            throw "Invalid matrix data";
        cout << "[SERVER] Waiting CLIENT_EXIT...\n";

        sendTLV(clientSocket, 0x09, {});
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

void clientAcceptLoop(SOCKET serverSocket)
{
    while (serverRunning)
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            if (serverRunning)
                cerr << "[-] Accept failed.\n";
            break;
        }
        thread(handleClient, clientSocket).detach();
    }
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

    thread consoleThread(consoleControl, serverSocket);
    clientAcceptLoop(serverSocket);
    consoleThread.join();

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
