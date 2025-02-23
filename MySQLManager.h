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

	USERINFOPK GetUserInfoById(std::string userId_) {
		std::string query_s = "SELECT id, level, exp, last_login From Users WHERE name = '" + userId_+"'";

		const char* Query = &*query_s.begin();

		MysqlResult = mysql_query(ConnPtr, Query);

		USERINFOPK userInfo;

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				userInfo.pk = (uint32_t)std::stoi(Row[0]);
				userInfo.level = (uint16_t)std::stoi(Row[1]);
				userInfo.exp = (unsigned int)std::stoi(Row[2]);

				std::string tag = "{" + std::to_string(userInfo.pk) + "}";
				std::string key = "userinfo:" + tag; // user:{pk}

				auto pipe = redis->pipeline(tag);

				pipe.hset(key, "level", Row[1])
					.hset(key, "exp", Row[2])
					.hset(key, "userId", userId_)
					.hset(key, "lastlogin", Row[3])
					.expire(key, 3600);

				pipe.exec();
			}
			mysql_free_result(Result);
		}

		return userInfo;
	}

	std::vector<EQUIPMENT> GetUserEquipByPk(std::string userPk_) {

		std::string query_s = "SELECT item_code, position, enhance FROM Equipment WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::vector<EQUIPMENT> Equipments;

		std::string tag = "{" + userPk_ + "}";
		std::string key = "equipment:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.hset(key, "itemcode", Row[0])
				.hset(key, "position", Row[1])
				.hset(key, "enhance", Row[2]);

				EQUIPMENT equipment;
				equipment.itemCode = (uint16_t)std::stoi(Row[0]);
				equipment.position = (uint16_t)std::stoi(Row[1]);
				equipment.enhance = (uint16_t)std::stoi(Row[2]);
				Equipments.emplace_back(equipment);
			}
			pipe.exec();
			mysql_free_result(Result);
		}

		return Equipments;
	}

	std::vector<CONSUMABLES> GetUserConsumablesByPk(std::string userPk_) {
		std::string query_s = "SELECT item_code, position, count FROM Consumables WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::vector<CONSUMABLES> Consumables;

		std::string tag = "{" + userPk_ + "}";
		std::string key = "inventory:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.hset(key, "item_code", Row[0])
					.hset(key, "position", Row[1])
					.hset(key, "count", Row[2]);

				CONSUMABLES consumable;
				consumable.itemCode = (uint16_t)std::stoi(Row[0]);
				consumable.position = (uint16_t)std::stoi(Row[1]);
				consumable.count = (uint16_t)std::stoi(Row[2]);
				Consumables.emplace_back(consumable);
			}
			pipe.exec();
			mysql_free_result(Result);
		}

		return Consumables;
	}


	std::vector<MATERIALS> GetUserMaterialsByPk(std::string userPk_) {

		std::string query_s = "SELECT item_code, position, count FROM Materials WHERE user_pk = " + userPk_;

		const char* Query = &*query_s.begin();
		MysqlResult = mysql_query(ConnPtr, Query);

		std::vector<MATERIALS> Materials;

		std::string tag = "{" + userPk_ + "}";
		std::string key = "inventory:" + tag; // user:{pk}

		auto pipe = redis->pipeline(tag);

		if (MysqlResult == 0) {
			Result = mysql_store_result(ConnPtr);
			while ((Row = mysql_fetch_row(Result)) != NULL) {
				pipe.hset(key, "item_code", Row[0])
					.hset(key, "position", Row[1])
					.hset(key, "count", Row[2]);

				MATERIALS Material;
				Material.itemCode = (uint16_t)std::stoi(Row[0]);
				Material.position = (uint16_t)std::stoi(Row[1]);
				Material.count = (uint16_t)std::stoi(Row[2]);
				Materials.emplace_back(Material);
			}
			pipe.exec();
			mysql_free_result(Result);
		}

		return Materials;
	}

private:
	MYSQL Conn;
	MYSQL* ConnPtr = NULL;
	MYSQL_RES* Result;
	MYSQL_ROW Row;
	int MysqlResult;
	std::shared_ptr<sw::redis::RedisCluster> redis;
};