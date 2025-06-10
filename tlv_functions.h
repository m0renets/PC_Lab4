#pragma once

#include <cstdint>
#include <vector>
#include <winsock2.h>

struct TLV {
    uint8_t type;
    uint16_t length;
    std::vector<uint8_t> value;
};

inline uint32_t readUint32(const uint8_t *data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

inline void writeUint32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back((value >> 24) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back(value & 0xFF);
}

inline bool recvTLV(SOCKET client, TLV &msg) {
    uint8_t header[3];
    int r = recv(client, (char *)header, 3, MSG_WAITALL);
    if (r != 3)
        return false;

    msg.type = header[0];
    msg.length = (header[1] << 8) | header[2];

    if (msg.length == 0) {
        msg.value.clear();
        return true;
    }

    msg.value.resize(msg.length);
    r = recv(client, (char *)msg.value.data(), msg.length, MSG_WAITALL);
    return r == msg.length;
}

inline bool sendTLV(SOCKET client, uint8_t type, const std::vector<uint8_t> &value) {
    uint8_t header[3] = {type, (uint8_t)(value.size() >> 8), (uint8_t)(value.size() & 0xFF)};
    send(client, (char *)header, 3, 0);
    return send(client, (char *)value.data(), value.size(), 0) == value.size();
}
