#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <mysql.h>

#pragma comment (lib, "libmysql.lib") // mysql 연동

class MySQLManager {
public:
	~MySQLManager(){
		mysql_close(ConnPtr);
		std::cout << "MySQL End" << std::endl;
	}

	bool SetRankingInRedis() {
		std::string query_s = "SELECT score,name From Ranking";

		const char* Query = &*query_s.begin();

		MysqlResult = mysql_query(ConnPtr, Query);

		auto pipe = redis->pipeline("ranking");

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.zadd("ranking", std::string(Row[1]), std::stod(Row[0]));
				pipe.exec();
			}
			mysql_free_result(Result);
		}

		pipe.exec();

		return true;
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

	bool SyncUserInfo(uint16_t userPk_) {
		std::string userInfokey = "userinfo:{" + std::to_string(userPk_) + "}";
		std::unordered_map<std::string, std::string> userData;
		redis->hgetall(userInfokey, std::inserter(userData, userData.begin()));

			std::string query_s = "UPDATE USERS left join Ranking r on USERS.name = r.name SET USERS.name = '"+ userData["userId"] +"', USERS.exp = " + userData["exp"] +
			", USERS.level = " + userData["level"] +
			", USERS.last_login = current_timestamp, r.score =" + userData["raidScore"] +
			" WHERE USERS.id = " + std::to_string(userPk_);

		const char* Query = query_s.c_str(); // c_str()을 사용하여 C 문자열 변환

		MysqlResult = mysql_query(ConnPtr, Query);

		if (MysqlResult != 0) {
			std::cerr << "MySQL UPDATE Error: " << mysql_error(ConnPtr) << std::endl;
			return false;
		}

		return true; 
	}

	std::pair<uint32_t, USERINFO> GetUserInfoById(std::string userId_) {
		std::string query_s = "SELECT u.id, u.level, u.exp, u.last_login, r.score From Users u left join Ranking r on u.name = r.name WHERE u.name = '" + userId_+"'";

		const char* Query = &*query_s.begin();

		MysqlResult = mysql_query(ConnPtr, Query);

		USERINFO userInfo;
		uint32_t pk_;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pk_ = (uint32_t)std::stoi(Row[0]);
				userInfo.level = (uint16_t)std::stoi(Row[1]);
				userInfo.exp = (unsigned int)std::stoi(Row[2]);
				userInfo.raidScore = (unsigned int)std::stoi(Row[4]);

				std::string tag = "{" + std::to_string(pk_) + "}";
				std::string key = "userinfo:" + tag; // user:{pk}

				auto pipe = redis->pipeline(tag);

				pipe.hset(key, "level", Row[1])
					.hset(key, "exp", Row[2])
					.hset(key, "userId", userId_)
					.hset(key, "lastlogin", Row[3])
					.hset(key, "raidScore", Row[4])
					.expire(key, 3600);

				pipe.exec();
			}
			mysql_free_result(Result);
		}

		return { pk_, userInfo };
	}
	
	std::pair<uint16_t, char*> GetUserEquipByPk(std::string userPk_) {

		std::string query_s = "SELECT item_code, position, enhance FROM Equipment WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		// std::vector<EQUIPMENT> Equipments;

		std::string tag = "{" + userPk_ + "}";
		std::string key = "equipment:" + tag; // user:{pk}

		char* tempC = new char[MAX_INVEN_SIZE+1];
		char* tc = tempC;
		uint16_t cnt = 0;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				redis->hset(key, Row[1], std::string(Row[0]) + "," + std::string(Row[2]));

				EQUIPMENT equipment;
				equipment.itemCode = (uint16_t)std::stoi(Row[0]);
				equipment.position = (uint16_t)std::stoi(Row[1]);
				equipment.enhance = (uint16_t)std::stoi(Row[2]);

				memcpy(tc,(char*)&equipment,sizeof(EQUIPMENT));
				tc += sizeof(EQUIPMENT);
				cnt++;
				//Equipments.emplace_back(equipment);
			}

			mysql_free_result(Result);
		}

		//return Equipments;

		return { cnt, tempC };
	}

	std::pair<uint16_t, char*> GetUserConsumablesByPk(std::string userPk_) {
		std::string query_s = "SELECT item_code, position, count FROM Consumables WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::string tag = "{" + userPk_ + "}";
		std::string key = "inventory:" + tag; // user:{pk}

		char* tempC = new char[MAX_INVEN_SIZE + 1];
		char* tc = tempC;
		uint16_t cnt = 0;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				redis->hset(key, Row[1], std::string(Row[0]) + "," + std::string(Row[2]));

				CONSUMABLES consumable;
				consumable.itemCode = (uint16_t)std::stoi(Row[0]);
				consumable.position = (uint16_t)std::stoi(Row[1]);
				consumable.count = (uint16_t)std::stoi(Row[2]);
				memcpy(tc, (char*)&consumable, sizeof(CONSUMABLES));
				tc += sizeof(CONSUMABLES);
				cnt++;
			}
			mysql_free_result(Result);
		}

		return { cnt, tempC };
	}


	std::pair<uint16_t, char*> GetUserMaterialsByPk(std::string userPk_) {

		std::string query_s = "SELECT item_code, position, count FROM Materials WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::string tag = "{" + userPk_ + "}";
		std::string key = "inventory:" + tag; // user:{pk}


		char* tempC = new char[MAX_INVEN_SIZE + 1];
		char* tc = tempC;
		uint16_t cnt = 0;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				redis->hset(key, Row[1], std::string(Row[0]) + "," + std::string(Row[2]));

				MATERIALS Material;
				Material.itemCode = (uint16_t)std::stoi(Row[0]);
				Material.position = (uint16_t)std::stoi(Row[1]);
				Material.count = (uint16_t)std::stoi(Row[2]);

				memcpy(tc, (char*)&Material, sizeof(MATERIALS));
				tc += sizeof(MATERIALS);
				cnt++;
			}

			mysql_free_result(Result);
		}

		return { cnt, tempC };
	}

private:
	MYSQL Conn;
	MYSQL* ConnPtr = NULL;
	MYSQL_RES* Result;
	MYSQL_ROW Row;
	int MysqlResult;
	std::shared_ptr<sw::redis::RedisCluster> redis;
};