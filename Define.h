#pragma once
#define WIN32_LEAN_AND_MEAN 
#define JWT_SECRET "quokka_lover"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cstdint>

const uint32_t MAX_SOCK = 1024; // Set Max Socket Buf
const uint32_t MAX_RECV_DATA = 8096;

enum class TaskType {
	ACCEPT,
	RECV,
	SEND
};

struct OverlappedEx {
	// 4 bytes
	TaskType taskType; // ACCPET, RECV, SEND INFO

	WSAOVERLAPPED wsaOverlapped;
};

struct OverlappedTCP : OverlappedEx {
	// 2 bytes
	short retryCnt = 0; // Retry Count For Send Proc

	// 8 bytes
	SOCKET userSkt;

	// 16 bytes
	WSABUF wsaBuf; // TCP Buffer
};
