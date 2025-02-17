#pragma once
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define PACKET_SIZE 1024

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <string>

#include <jwt-cpp/jwt.h>
#include <sw/redis++/redis++.h>

#include "Packet.h"
#include "Define.h"

#pragma comment(lib, "ws2_32.lib") // 비주얼에서 소켓프로그래밍 하기 위한 것

class UserProcessor {
public:
	bool init(uint16_t threadCnt_, std::shared_ptr<sw::redis::RedisCluster> redis_) {

        redis = redis_;



        return true;
	}
private:
    uint16_t threadCnt = 0;

	SOCKET toUserSkt;
    HANDLE IOCPHandle;
    
    std::shared_ptr<sw::redis::RedisCluster> redis;

    std::string webToken;
    char recvBuffer[PACKET_SIZE];
};