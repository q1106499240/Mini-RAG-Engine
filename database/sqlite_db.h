#pragma once
#include <string>
#include "sqlite3.h"

class SQLiteDB {
public:
    SQLiteDB(const std::string& db_path);
    ~SQLiteDB();

    bool open();
    void close();

    bool execute(const std::string& sql);

    sqlite3* get_raw_db();

private:
    std::string db_path_;
    sqlite3* db_ = nullptr;
};