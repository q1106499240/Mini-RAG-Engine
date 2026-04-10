#include "sqlite_db.h"
#include <iostream>

SQLiteDB::SQLiteDB(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {
}

SQLiteDB::~SQLiteDB() {
    close();
}

bool SQLiteDB::open() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    std::cout << "Opened database successfully\n";
    return true;
}

void SQLiteDB::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SQLiteDB::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

sqlite3* SQLiteDB::get_raw_db() {
    return db_;
}