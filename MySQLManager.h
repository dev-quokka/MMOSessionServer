#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <mysql.h>

#pragma comment (lib, "libmysql.lib") // mysql 연동

struct UserInfo {
	uint16_t userLevel;
	uint32_t userPk;
	unsigned int userExp;
	std::string lastLogin;
};

struct UserEquipment {
	uint16_t itemCode;
	uint16_t position;
	uint16_t enhance;
};

struct UserInventory {
	uint16_t itemType;
	uint16_t itemCode;
	uint16_t position;
	uint16_t count;
};

class MySQLManager {
public:
	~MySQLManager(){
		std::cout << "MySQL End" << std::endl;
		mysql_close(ConnPtr);
	}

	bool Run(std::shared_ptr<sw::redis::RedisCluster> redis_) {
		redis = redis_;

		mysql_init(&Conn);
		ConnPtr = mysql_real_connect(&Conn, "127.0.0.1", "quokka", "1234", "iocp", 3306, (char*)NULL, 0);

		if (ConnPtr == NULL) {
			std::cout << mysql_error(&Conn) << std::endl;
			std::cout << "Mysql Connect Fail" << std::endl;
			return false;
		}

		std::cout << "Mysql Connect Success" << std::endl;
		return true;
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

	bool SyncUserInfo(uint16_t userPk) {
	
		return true;
	}

	uint32_t GetUserInfoById(std::string userId_) {
		std::string query_s = "SELECT id, level, exp, last_login WHERE Users WHERE name = " + userId_;

		const char* Query = &*query_s.begin();

		MysqlResult = mysql_query(ConnPtr, Query);
		UserInfo userInfo;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				userInfo.userPk = (uint32_t)std::stoi(Row[0]);
				userInfo.userLevel = (uint16_t)std::stoi(Row[1]);
				userInfo.userExp = (unsigned int)std::stoi(Row[2]);
				userInfo.lastLogin = std::stoi(Row[3]);
			}
			mysql_free_result(Result);
		}

		std::string tag = "{" + std::to_string(userInfo.userPk) + "}";
		std::string key = "userinfo:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		pipe.hset(key, "level", std::to_string(userInfo.userLevel))
			.hset(key, "exp", std::to_string(userInfo.userExp))
			.hset(key, "userId", userId_)
			.hset(key, "lastlogin", userInfo.lastLogin)
			.expire(key, 3600);

		pipe.exec();

		return userInfo.userPk;
	}

	bool GetUserEquipByPk(uint32_t userPk_) {

		std::string query_s = "SELECT item_code, position, enhance FROM Equipment WHERE user_pk = " + std::to_string(userPk_);

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::string tag = "{" + std::to_string(userPk_) + "}";
		std::string key = "equipment:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.hset(key, "itemcode", Row[0])
				.hset(key, "position", Row[1])
				.hset(key, "enhance", Row[2]);
			}
			mysql_free_result(Result);
		}

		pipe.exec();

		return true;
	}

	bool GetUserInvenByPk(uint32_t userPk_) {

		std::string query_s = "SELECT item_type, item_code, position, enhance FROM Inventory WHERE user_pk = " + std::to_string(userPk_);

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::string tag = "{" + std::to_string(userPk_) + "}";
		std::string key = "equipment:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.hset(key, "item_type", Row[0])
					.hset(key, "item_code", Row[1])
					.hset(key, "position", Row[2])
					.hset(key, "enhance", Row[2]);
			}
			mysql_free_result(Result);
		}

		pipe.exec();

		return true;
	}

private:
	MYSQL Conn;
	MYSQL* ConnPtr = NULL;
	MYSQL_RES* Result;
	MYSQL_ROW Row;
	int MysqlResult;
	std::shared_ptr<sw::redis::RedisCluster> redis;
};