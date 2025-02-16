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
        WSADATA wsadata;
        int check = 0;
        threadCnt = threadCnt_;

        check = WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (check) {
            std::cout << "WSAStartup() Fail" << std::endl;
            return false;
        }

        toUserSkt = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
        if (toUserSkt == INVALID_SOCKET) {
            std::cout << "Server Socket 생성 실패" << std::endl;
            return false;
        }

        SOCKADDR_IN addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

        std::cout << "서버 연결중" << std::endl;

        if (!connect(toUserSkt, (SOCKADDR*)&addr, sizeof(addr))) {
            std::cout << "서버와 연결완료" << std::endl << std::endl;
        }

        redis = redis_;

        IM_WEB_REQUEST iwReq;
        iwReq.PacketId = (UINT16)PACKET_ID::IM_WEB_REQUEST;
        iwReq.PacketLength = sizeof(IM_WEB_REQUEST);

        webToken = jwt::create()
            .set_issuer("web_server")
            .set_subject("WebServer")
            .sign(jwt::algorithm::hs256{ JWT_SECRET });

        redis->hset("JWT_CHECK", webToken, );

        send(toUserSkt, webToken.c_str(), webToken.size(), 0);
        recv(toUserSkt, recvBuffer, PACKET_SIZE, 0);

        IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, threadCnt);
        if (IOCPHandle == NULL) {
            std::cout << "iocp 핸들 생성 실패" << std::endl;
            return false;
        }

        auto bIOCPHandle = CreateIoCompletionPort((HANDLE)toUserSkt, IOCPHandle, (uint32_t)0, 0);
        if (bIOCPHandle == nullptr) {
            std::cout << "iocp 핸들 바인드 실패" << std::endl;
            return false;
        }
	}
private:
    uint16_t threadCnt = 0;

	SOCKET toUserSkt;
    HANDLE IOCPHandle;
    
    std::shared_ptr<sw::redis::RedisCluster> redis;

    std::string webToken;
    char recvBuffer[PACKET_SIZE];
};