#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <mysql.h>

#pragma comment (lib, "libmysql.lib") // mysql 연동

class MySQLManager {
public:
	~MySQLManager(){
				std::cout << "MySQL End" << std::endl;
		mysql_close(ConnPtr);
	}

	void Run() {
		mysql_init(&Conn);
		ConnPtr = mysql_real_connect(&Conn, "127.0.0.1", "quokka", "1234", "Quokka", 3306, (char*)NULL, 0);

		if (ConnPtr == NULL) {
			std::cout << "Mysql Connect Fail" << std::endl;
		}

		std::cout << "Mysql Connect Success" << std::endl;

	}

	bool SyncLevel(uint16_t userPk, uint16_t level, unsigned int currentExp) {
		std::string query_s = "UPDATE level, exp SET Users WHERE id = " + userPk;

		const char* Query = &*query_s.begin();

		MysqlResult = mysql_query(ConnPtr, Query);

		if (MysqlResult == 0) {
			// 변경된 행의 수 확인 (성공 여부 체크)
			if (mysql_affected_rows(ConnPtr) > 0) {
				return true; 
			}
		}

		return false; 
	}


private:
	MYSQL Conn;
	MYSQL* ConnPtr = NULL;
	MYSQL_RES* Result;
	MYSQL_ROW Row;
	int MysqlResult;
};