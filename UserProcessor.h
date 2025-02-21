#pragma once
#pragma comment(lib, "ws2_32.lib") // 비주얼에서 소켓프로그래밍 하기 위한 것
#pragma comment(lib,"mswsock.lib") //AcceptEx를 사용하기 위한 것

#define SERVER_IP "127.0.0.1"
#define TO_USER_PORT 9091
#define PACKET_SIZE 1024
#define SEND_QUEUE_CNT 5
#define MAX_OVERLAP_CNT 10

#include <jwt-cpp/jwt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <string>
#include <thread>
#include <atomic>
#include <sw/redis++/redis++.h>
//#include <boost/lockfree/queue.hpp>

#include "Packet.h"
#include "Define.h"
#include "MySQLManager.h"

class UserProcessor {
public:
    ~UserProcessor() {
        if (userProcThread.joinable()) {
            userProcThread.join();
        }
        CloseHandle(u_IOCPHandle);
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

        u_IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, threadCnt_);
        if (u_IOCPHandle == NULL) {
            std::cout << "Make Iocp Hande Fail" << std::endl;
            return false;
        }

        auto bIOCPHandle = CreateIoCompletionPort((HANDLE)userIOSkt, u_IOCPHandle, (ULONG_PTR)0, 0);
        if (bIOCPHandle == nullptr) {
            std::cout << "Bind Iocp Hande Fail" << std::endl;
            return false;
        }

        userSkt = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);

        if (userSkt == INVALID_SOCKET) {
            std::cout << "Client socket Error : " << GetLastError() << std::endl;
        }

        // For Reuse Socket Set
        int optval = 1;
        setsockopt(userSkt, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));

        int recvBufSize = PACKET_SIZE;
        setsockopt(userSkt, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufSize, sizeof(recvBufSize));

        int sendBufSize = PACKET_SIZE;
        setsockopt(userSkt, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufSize, sizeof(sendBufSize));


        auto tIOCPHandle = CreateIoCompletionPort((HANDLE)userSkt, u_IOCPHandle, (ULONG_PTR)0, 0);

        if (tIOCPHandle == INVALID_HANDLE_VALUE)
        {
            std::cout << "reateIoCompletionPort()함수 실패 :" << GetLastError() << std::endl;
        }
        
        //for (int i = 0; i < MAX_OVERLAP_CNT; i++) {
        //    OverlappedTCP* temp = new OverlappedTCP;
        //    overLapPool.push(temp);
        //}

        redis = redis_;
        mysqlManager = mysqlManager_;

        CreateUserProcThread();
        PostAccept();

        return true;
	}

    bool UserRecv() {
        //OverlappedTCP* tempOvLap;

        //if (overLapPool.pop(tempOvLap)) {
        //    sendQueueSize.fetch_sub(1);

            DWORD dwFlag = 0;
            DWORD dwRecvBytes = 0;

            recvOvLap = {};
            memset(recvBuf, 0, sizeof(recvBuf));

            recvOvLap.wsaBuf.len = MAX_SOCK;
            recvOvLap.wsaBuf.buf = recvBuf;
            recvOvLap.userSkt = userIOSkt;
            recvOvLap.taskType = TaskType::RECV;

            int tempR = WSARecv(userSkt, &(recvOvLap.wsaBuf), 1, &dwRecvBytes, &dwFlag, (LPWSAOVERLAPPED)&(recvOvLap), NULL);

            if (tempR == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
            {
                std::cout << userSkt << " WSARecv() Fail : " << WSAGetLastError() << std::endl;
                return false;
            }

            return true;
        //}

        //else {
        //    std::cout << "Recv Fail, OverLapped Pool Full" << std::endl;
        //    return false;
        //}
    }

    void UserSend(USER_GAMESTART_RESPONSE ugRes){
        //OverlappedTCP* overlappedTCP;

        //if (SendQueue.pop(overlappedTCP)) {
            //sendQueueSize.fetch_sub(1);

        DWORD dwSendBytes = 0;

        memset(sendBuf, 0, sizeof(sendBuf));
        sendOvLap = {};

        sendOvLap.wsaBuf.len = MAX_SOCK;
        sendOvLap.wsaBuf.buf = sendBuf;
		CopyMemory(sendBuf, &ugRes, sizeof(USER_GAMESTART_RESPONSE));
        sendOvLap.userSkt = userIOSkt;
        sendOvLap.taskType = TaskType::SEND;

            int sCheck = WSASend(userSkt,
                &(sendOvLap.wsaBuf),
                1,
                &dwSendBytes,
                0,
                (LPWSAOVERLAPPED)&sendOvLap,
                NULL);
        //}

        //else std::cout << "Send Fail, OverLappend Pool Full" << std::endl;
    }

    void CreateUserProcThread() {
        userProcRun = true;
        userProcThread = std::thread([this]() {WorkRun(); });
    }

    bool PostAccept() {
        std::cout << "포스트 억셉트 시작." << std::endl;
        DWORD bytes = 0;
        DWORD flags = 0;

        memset(acceptBuf, 0, sizeof(acceptBuf));
        aceeptOvLap = {};

		aceeptOvLap.taskType = TaskType::ACCEPT;
		aceeptOvLap.userSkt = userSkt;
		aceeptOvLap.wsaBuf.len = sizeof(acceptBuf);
		aceeptOvLap.wsaBuf.buf = acceptBuf;

        if (AcceptEx(userIOSkt, userSkt, acceptBuf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPWSAOVERLAPPED)&aceeptOvLap) == 0) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                std::cout << "AcceptEx Error : " << GetLastError() << std::endl;
                return false;
            }
        }
		std::cout << "AcceptEx 함수 성공" << std::endl;

        return true;
    }

    void WorkRun() {
        LPOVERLAPPED lpOverlapped = NULL;
        DWORD dwIoSize = 0;
        bool gqSucces = TRUE;

        while (userProcRun) {
            gqSucces = GetQueuedCompletionStatus(
                u_IOCPHandle,
                &dwIoSize,
                (PULONG_PTR)&userIOSkt,
                &lpOverlapped,
                INFINITE
            );
            std::cout << "겟 큐드 넘어온 수" << std::endl;

            auto overlappedTCP = (OverlappedTCP*)lpOverlapped;

            if (!gqSucces || (dwIoSize == 0 && overlappedTCP->taskType != TaskType::ACCEPT)) { // User Disconnect
                std::cout << "비정상 종료 감지. 소켓 초기화" << std::endl;
                shutdown(userSkt, SD_BOTH);
                PostAccept();
                continue;
            }

            if (overlappedTCP->taskType == TaskType::ACCEPT) {
                UserRecv();
                std::cout << "유저 한명 정보 요청 했다." << std::endl;
            }
            else if (overlappedTCP->taskType == TaskType::RECV) {
                std::cout << "유저한테 정보달라는 요청 받았다." << std::endl;
                auto k = reinterpret_cast<PACKET_HEADER*>(overlappedTCP->wsaBuf.buf);
                
                if (k->PacketId == (uint16_t)WEBPACKET_ID::USER_GAMESTART_REQUEST) {
                    auto ugReq = reinterpret_cast<USER_GAMESTART_REQUEST*>(overlappedTCP->wsaBuf.buf);
                    GameStart(ugReq);
                    std::cout << ugReq->userId << " Game Start Request" << std::endl;
                }

                // 소켓 종료

                //ZeroMemory(overlappedTCP,sizeof(OverlappedTCP)); // Push After Init
                //overLapPool.push(overlappedTCP); // Push OverLappedTcp
                //sendQueueSize.fetch_add(1);
            }
            else if (overlappedTCP->taskType == TaskType::SEND) {
                std::cout << "유저한테 정보전달 했다." << std::endl;
                // 오버랩, 소켓 초기화 후 acceptex 다시 걸기
                shutdown(userSkt, SD_BOTH);
                PostAccept();

                //ZeroMemory(overlappedTCP, sizeof(OverlappedTCP)); // Push After Init
                //overLapPool.push(overlappedTCP); // Push OverLappedTcp
                //sendQueueSize.fetch_add(1);
            }

        }
    }

    void GameStart(USER_GAMESTART_REQUEST* ugReq) {
        // 레디스 클러스터에 뒤에 {}를 ID로 하면 ID는 한번씩 유저가 바꾸니까 변하지 않는 PK로 설정.
        uint16_t pk = mysqlManager->GetPkById(ugReq->userId);
        std::cout << "유저한테 정보전달 했다. 1" << std::endl;
        std::string pk_s = std::to_string(pk);

        if (mysqlManager->GetUserEquipByPk(pk_s)) {
            std::cout << "Insert Equipment Success" << std::endl;
        }
        std::cout << "유저한테 정보전달 했다. 2" << std::endl;
        if (mysqlManager->GetUserInvenByPk(pk_s)) {
            std::cout << "Insert Inventory Success" << std::endl;
        }
        std::cout << "유저한테 정보전달 했다. 3" << std::endl;
        std::string token = jwt::create()
            .set_issuer("web_server")
            .set_subject("Login_check")
            .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{ 1800 }) // 삼십분으로 세팅
            .sign(jwt::algorithm::hs256{ JWT_SECRET });

        std::string tag = "{" + std::string(ugReq->userId) + "}";
        std::string key = "jwtcheck:" + tag;

        auto pipe = redis->pipeline(tag);

        pipe.hset(key, token, std::to_string(pk))
            .expire(key, 1800); // set ttl 1 hour

        pipe.exec();
        std::cout << "유저한테 정보전달 했다. 4" << std::endl;
		USER_GAMESTART_RESPONSE ugRes; 
		ugRes.PacketId = (UINT16)WEBPACKET_ID::USER_GAMESTART_RESPONSE;
		ugRes.PacketLength = sizeof(USER_GAMESTART_RESPONSE);
		strncpy_s(ugRes.webToken, token.c_str(), 256);
        std::cout << "유저한테 정보전달 했다. 5" << std::endl;
        UserSend(ugRes);
    }

private:
    bool userProcRun = false;
    //std::atomic<uint16_t> sendQueueSize{ 0 };

    uint16_t threadCnt = 0;

	SOCKET userIOSkt;
    SOCKET userSkt;

    HANDLE u_IOCPHandle;
    std::thread userProcThread;
    MySQLManager* mysqlManager;

    std::shared_ptr<sw::redis::RedisCluster> redis;

    OverlappedTCP aceeptOvLap;
    OverlappedTCP sendOvLap;
    OverlappedTCP recvOvLap;

    //boost::lockfree::queue<OverlappedTCP*> SendQueue{ SEND_QUEUE_CNT };
    //boost::lockfree::queue<OverlappedTCP*> overLapPool{ MAX_OVERLAP_CNT };

    std::string userToken;

    char acceptBuf[64] = {0};
    char recvBuf[PACKET_SIZE] = {0};
    char sendBuf[PACKET_SIZE] = { 0 };
};