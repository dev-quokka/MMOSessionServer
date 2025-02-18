#pragma once

#define SERVER_IP "127.0.0.1"
#define TO_USER_PORT 9001
#define PACKET_SIZE 1024
#define SEND_QUEUE_CNT 5
#define MAX_OVERLAP_CNT 10

#include <jwt-cpp/jwt.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <string>
#include <thread>
#include <atomic>
#include <sw/redis++/redis++.h>
#include <boost/lockfree/queue.hpp>

#include "Packet.h"
#include "Define.h"
#include "MySQLManager.h"

#pragma comment(lib, "ws2_32.lib") // 비주얼에서 소켓프로그래밍 하기 위한 것

class UserProcessor {
public:
    ~UserProcessor() {
        if (userProcThread.joinable()) {
            userProcThread.join();
        }
        CloseHandle(IOCPHandle);
        closesocket(userIOSkt);
        WSACleanup();
        std::cout << "userProcThread End" << std::endl;
    }

	bool init(uint16_t threadCnt_, std::shared_ptr<sw::redis::RedisCluster> redis_, MySQLManager* mysqlManager_) {
        
        WSADATA wsadata;
        int check = 0;
        threadCnt = threadCnt_;

        check = WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (check) {
            std::cout << "WSAStartup Fail" << std::endl;
            return false;
        }

        userIOSkt = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
        if (userIOSkt == INVALID_SOCKET) {
            std::cout << "Make Server Socket Fail" << std::endl;
            return false;
        }

        SOCKADDR_IN addr;
        addr.sin_port = htons(TO_USER_PORT);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        check = bind(userIOSkt, (SOCKADDR*)&addr, sizeof(addr));
        if (check) {
            std::cout << "bind fail" << std::endl;
            return false;
        }

        check = listen(userIOSkt, SOMAXCONN);
        if (check) {
            std::cout << "listen fail" << std::endl;
            return false;
        }

        IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, threadCnt_);
        if (IOCPHandle == NULL) {
            std::cout << "Make Iocp Hande Fail" << std::endl;
            return false;
        }

        auto bIOCPHandle = CreateIoCompletionPort((HANDLE)userIOSkt, IOCPHandle, (uint32_t)0, 0);
        if (bIOCPHandle == nullptr) {
            std::cout << "Bind Iocp Hande Fail" << std::endl;
            return false;
        }
        
        for (int i = 0; i < MAX_OVERLAP_CNT; i++) {
            OverlappedTCP* temp = new OverlappedTCP;
            overLapPool.push(temp);
        }

        redis = redis_;
        mysqlManager = mysqlManager_;

        return true;
	}

    bool UserRecv() {
        OverlappedTCP* tempOvLap;

        if (overLapPool.pop(tempOvLap)) {
            sendQueueSize.fetch_sub(1);

            DWORD dwFlag = 0;
            DWORD dwRecvBytes = 0;

            ZeroMemory(tempOvLap, sizeof(OverlappedTCP));
            tempOvLap->wsaBuf.len = MAX_SOCK;
            tempOvLap->wsaBuf.buf = recvBuf;
            tempOvLap->userSkt = userIOSkt;
            tempOvLap->taskType = TaskType::RECV;

            int tempR = WSARecv(userIOSkt, &(tempOvLap->wsaBuf), 1, &dwRecvBytes, &dwFlag, (LPWSAOVERLAPPED) & (tempOvLap), NULL);

            if (tempR == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
            {
                std::cout << userIOSkt << " WSARecv() Fail : " << WSAGetLastError() << std::endl;
                return false;
            }

            return true;
        }

        else {
            std::cout << "Recv Fail, OverLapped Pool Full" << std::endl;
            return false;
        }
    }

    void UserSend(){
        OverlappedTCP* overlappedTCP;

        if (SendQueue.pop(overlappedTCP)) {
            sendQueueSize.fetch_sub(1);

            DWORD dwSendBytes = 0;
            int sCheck = WSASend(userIOSkt,
                &(overlappedTCP->wsaBuf),
                1,
                &dwSendBytes,
                0,
                (LPWSAOVERLAPPED)overlappedTCP,
                NULL);
        }

        else std::cout << "Send Fail, OverLappend Pool Full" << std::endl;
    }

    void SendComplete() {
        if (sendQueueSize.load() == 1) {
            UserSend();
        }
    }

    void CreateServerProcThread(int16_t threadCnt_) {
        userProcRun = true;
        userProcThread = std::thread([this]() {WorkRun(); });
    }

    void WorkRun() {
        LPOVERLAPPED lpOverlapped = NULL;
        DWORD dwIoSize = 0;
        bool gqSucces = TRUE;

        while (userProcRun) {
            gqSucces = GetQueuedCompletionStatus(
                IOCPHandle,
                &dwIoSize,
                (PULONG_PTR)&userIOSkt,
                &lpOverlapped,
                INFINITE
            );

            auto overlappedTCP = (OverlappedTCP*)lpOverlapped;

            if (overlappedTCP->taskType == TaskType::RECV) {
                UserRecv();
                auto k = reinterpret_cast<PACKET_HEADER*>(overlappedTCP->wsaBuf.buf);
                
                if (k->PacketId == (uint16_t)WEBPACKET_ID::USER_GAMESTART_REQUEST) {
                    auto ugReq = reinterpret_cast<USER_GAMESTART_REQUEST*>(overlappedTCP->wsaBuf.buf);
                    GameStart(ugReq);
                    std::cout << ugReq->userId << " Game Start Request" << std::endl;
                }

                ZeroMemory(overlappedTCP,sizeof(OverlappedTCP)); // Push After Init
                overLapPool.push(overlappedTCP); // Push OverLappedTcp
                sendQueueSize.fetch_add(1);
            }

            else if (overlappedTCP->taskType == TaskType::SEND) {


                ZeroMemory(overlappedTCP, sizeof(OverlappedTCP)); // Push After Init
                overLapPool.push(overlappedTCP); // Push OverLappedTcp
                sendQueueSize.fetch_add(1);

                SendComplete();
            }

        }
    }

    void GameStart(USER_GAMESTART_REQUEST* ugReq) {
        uint16_t pk = mysqlManager->GetUserInfoById(ugReq->userId);

        if (mysqlManager->GetUserEquipByPk(pk)) {
            std::cout << "Insert Equipment Success" << std::endl;
        }

        if (mysqlManager->GetUserInvenByPk(pk)) {
            std::cout << "Insert Inventory Success" << std::endl;
        }

        std::string token = jwt::create()
            .set_issuer("web_server")
            .set_subject("Login_check")
            .sign(jwt::algorithm::hs256{ JWT_SECRET });

        std::string tag = "{" + std::to_string(pk) + "}";
        std::string key = "jwtcheck:" + tag; // user:{pk}

        auto pipe = redis->pipeline(tag);

        pipe.hset(key, token, std::to_string(pk))
            .expire(key, 3600); // set ttl 1 hour

        pipe.exec();

        UserSend();
    }

private:
    bool userProcRun = false;
    std::atomic<uint16_t> sendQueueSize{ 0 };

    uint16_t threadCnt = 0;

	SOCKET userIOSkt;
    HANDLE IOCPHandle;
    std::thread userProcThread;
    MySQLManager* mysqlManager;

    std::shared_ptr<sw::redis::RedisCluster> redis;

    boost::lockfree::queue<OverlappedTCP*> SendQueue{ SEND_QUEUE_CNT };
    boost::lockfree::queue<OverlappedTCP*> overLapPool{ MAX_OVERLAP_CNT };

    std::string webToken;
    char recvBuf[PACKET_SIZE];
};