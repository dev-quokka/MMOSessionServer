#pragma once

#define SERVER_IP "127.0.0.1"
#define TO_USER_PORT 9001

#include <winsock2.h>
#include <windows.h>

#include <sw/redis++/redis++.h>

class ServerProcessor {
public:

private:
	SOCKET toServerSkt;
};