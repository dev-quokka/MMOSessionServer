#pragma once

#define SERVER_IP "127.0.0.1"
#define TO_USER_PORT 9001

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <string>
#include <thread>

#include <jwt-cpp/jwt.h>
#include <sw/redis++/redis++.h>

#include "Packet.h"
#include "Define.h"
#include "MySQLManager.h"

#pragma comment(lib, "ws2_32.lib") // 비주얼에서 소켓프로그래밍 하기 위한 것

class ServerProcessor {
public:
	bool init(int16_t threadCnt_, std::shared_ptr<sw::redis::RedisCluster> redis_, MySQLManager* mysqlManager_) {
        WSADATA wsadata;
        int check = 0;
        threadCnt = threadCnt_;

        check = WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (check) {
            std::cout << "WSAStartup() Fail" << std::endl;
            return false;
        }

        serverIOSkt = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
        if (serverIOSkt == INVALID_SOCKET) {
            std::cout << "Server Socket 생성 실패" << std::endl;
            return false;
        }

        SOCKADDR_IN addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

        std::cout << "서버 연결중" << std::endl;

        if (!connect(serverIOSkt, (SOCKADDR*)&addr, sizeof(addr))) {
            std::cout << "서버와 연결완료" << std::endl << std::endl;
        }

        IM_WEB_REQUEST iwReq;
        iwReq.PacketId = (UINT16)PACKET_ID::IM_WEB_REQUEST;
        iwReq.PacketLength = sizeof(IM_WEB_REQUEST);

        webToken = jwt::create()
            .set_issuer("web_server")
            .set_subject("WebServer")
            .sign(jwt::algorithm::hs256{ JWT_SECRET });

        if (!redis->hset("JWT_CHECK", webToken, "0")) {
            std::cout << "Fail to Insert WebToken in Redis" << std::endl;
            return false;
        }

        send(serverIOSkt, webToken.c_str(), webToken.size(), 0);
        recv(serverIOSkt, recvBuf, PACKET_SIZE, 0);
    
        auto iwRes = reinterpret_cast<IM_WEB_RESPONSE*>(recvBuf);

        if (!iwRes->isSuccess) {
            std::cout << "Fail to Check Token in Server" << std::endl;
            return false;
        }

        IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, threadCnt);
        
        if (IOCPHandle == NULL) {
            std::cout << "Iocp Handle Make Fail" << std::endl;
            return false;
        }

        auto bIOCPHandle = CreateIoCompletionPort((HANDLE)serverIOSkt, IOCPHandle, (uint32_t)0, 0);
        if (bIOCPHandle == nullptr) {
            std::cout << "Iocp Handle Bind Fail" << std::endl;
            return false;
        }

        CreateServerProcThread(threadCnt_);

        mysqlManager = mysqlManager_;
	}

    bool ConnUserRecv() {
        DWORD dwFlag = 0;
        DWORD dwRecvBytes = 0;

        OverlappedTCP* tempOvLap = new OverlappedTCP;
        ZeroMemory(tempOvLap, sizeof(OverlappedTCP));
        tempOvLap->wsaBuf.len = MAX_SOCK;
        tempOvLap->wsaBuf.buf = recvBuf;
        tempOvLap->userSkt = serverIOSkt;
        tempOvLap->taskType = TaskType::RECV;

        int tempR = WSARecv(serverIOSkt, &(tempOvLap->wsaBuf), 1, &dwRecvBytes, &dwFlag, (LPWSAOVERLAPPED) & (tempOvLap), NULL);

        if (tempR == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
        {
            std::cout << serverIOSkt << " WSARecv() Fail : " << WSAGetLastError() << std::endl;
            return false;
        }

        return true;
    }

    void CreateServerProcThread(int16_t threadCnt_) {
        serverProcRun = true;
        std::thread([this]() {WorkRun();});
    }

    void WorkRun() {
        LPOVERLAPPED lpOverlapped = NULL;
        DWORD dwIoSize = 0;
        bool gqSucces = TRUE;

        while (WorkRun) {
            gqSucces = GetQueuedCompletionStatus(
                IOCPHandle,
                &dwIoSize,
                (PULONG_PTR)&serverIOSkt,
                &lpOverlapped,
                INFINITE
            );

            auto overlappedTCP = (OverlappedTCP*)lpOverlapped;

            if (overlappedTCP->taskType == TaskType::RECV) { // 서버로 부터 요청만 받음
                auto k = reinterpret_cast<PACKET_HEADER*>(overlappedTCP->wsaBuf.buf);
                if (k->PacketId == (uint16_t)PACKET_ID::SYNCRONIZE_LEVEL_REQUEST) {
                    auto pakcet = reinterpret_cast<SYNCRONIZE_LEVEL_REQUEST*>(overlappedTCP->wsaBuf.buf);
                    
                    if (mysqlManager->SyncLevel(pakcet->userPk, pakcet->level, pakcet->currentExp)) { 
                        std::cout << "Level Sync Success" << std::endl;
                    }

                }
                else if (k->PacketId == (uint16_t)PACKET_ID::SYNCRONIZE_LOGOUT_REQUEST) {
                    auto pakcet = reinterpret_cast<SYNCRONIZE_LOGOUT_REQUEST*>(overlappedTCP->wsaBuf.buf);



                }
                else if (k->PacketId == (uint16_t)PACKET_ID::SYNCRONIZE_DISCONNECT_REQUEST) {
                    auto pakcet = reinterpret_cast<SYNCRONIZE_DISCONNECT_REQUEST*>(overlappedTCP->wsaBuf.buf);



                }
            }

        }
    }

private:
    bool serverProcRun = false;

    uint16_t threadCnt = 0;

    SOCKET serverIOSkt;
    HANDLE IOCPHandle;
    MySQLManager* mysqlManager;

    std::shared_ptr<sw::redis::RedisCluster> redis;

    std::string webToken;

    char recvBuf[PACKET_SIZE];
};