#include "auth_service.h"
#include "password_hasher.h"
#include "rbac_service.h"
#include "../database/sqlite_db.h"
#include <sqlite3.h>
#include <random>
#include <sstream>
#include <chrono>
#include <unordered_set>

using namespace std;
using json = nlohmann::json;

namespace {
static const string kDbPath = "E:/C++/code/llm_rag_cpp/database/rag.db";

string make_token() {
    static random_device rd;
    static mt19937_64 gen(rd());
    uniform_int_distribution<unsigned long long> dis;
    stringstream ss;
    ss << hex << dis(gen) << dis(gen);
    return ss.str();
}

long long now_epoch() {
    return chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

bool prepare_ok(sqlite3* db, const string& sql, sqlite3_stmt** stmt) {
    return sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr) == SQLITE_OK;
}
}

namespace auth {

bool init_auth_tables() {
    SQLiteDB db(kDbPath);
    if (!db.open()) return false;

    const string users_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_name TEXT NOT NULL UNIQUE,"
        "password_hash TEXT NOT NULL,"
        "status INTEGER NOT NULL DEFAULT 1,"
        "email TEXT,"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL,"
        "last_login_at INTEGER"
        ");";

    const string sessions_sql =
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "token TEXT PRIMARY KEY,"
        "user_id INTEGER NOT NULL,"
        "created_at INTEGER NOT NULL,"
        "expires_at INTEGER NOT NULL,"
        "FOREIGN KEY(user_id) REFERENCES users(id)"
        ");";

    if (!(db.execute(users_sql) && db.execute(sessions_sql))) return false;

    // Ensure enterprise schema columns exist even if table was created by old code.
    sqlite3* raw = db.get_raw_db();
    unordered_set<string> cols;
    sqlite3_stmt* info_stmt = nullptr;
    if (prepare_ok(raw, "PRAGMA table_info(users);", &info_stmt)) {
        while (sqlite3_step(info_stmt) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(info_stmt, 1);
            if (c) cols.insert(reinterpret_cast<const char*>(c));
        }
    }
    if (info_stmt) sqlite3_finalize(info_stmt);

    auto ensure_col = [&](const string& name, const string& ddl) {
        if (cols.find(name) == cols.end()) db.execute("ALTER TABLE users ADD COLUMN " + ddl + ";");
    };
    ensure_col("user_name", "user_name TEXT");
    ensure_col("password_hash", "password_hash TEXT");
    ensure_col("status", "status INTEGER NOT NULL DEFAULT 1");
    ensure_col("email", "email TEXT");
    ensure_col("created_at", "created_at INTEGER");
    ensure_col("updated_at", "updated_at INTEGER");
    ensure_col("last_login_at", "last_login_at INTEGER");

    return true;
}

optional<json> register_user(const string& username, const string& password, const string& email) {
    if (username.empty() || password.empty()) return nullopt;
    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    string password_hash = hash_password(password);
    if (password_hash.empty()) return nullopt;
    long long now = now_epoch();

    sqlite3_stmt* stmt = nullptr;
    const string sql =
        "INSERT INTO users(user_name, password_hash, status, email, created_at, updated_at, last_login_at) "
        "VALUES(?, ?, 1, ?, ?, ?, NULL);";
    if (!prepare_ok(raw, sql, &stmt)) return nullopt;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int64(stmt, 5, now);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) return nullopt;
    int user_id = static_cast<int>(sqlite3_last_insert_rowid(raw));
    if (user_id <= 0) return nullopt;
    if (!assign_default_role_for_user(user_id, "NORMAL_USER")) return nullopt;

    json out;
    out["username"] = username;
    return out;
}

optional<json> login(const string& username, const string& password) {
    if (username.empty() || password.empty()) return nullopt;

    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    long long now = now_epoch();

    int user_id = -1;
    int status = 0;
    string password_hash;
    {
        sqlite3_stmt* query_stmt = nullptr;
        const string sql = "SELECT id, password_hash, status FROM users WHERE user_name=? LIMIT 1;";
        if (!prepare_ok(raw, sql, &query_stmt)) return nullopt;
        sqlite3_bind_text(query_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(query_stmt) == SQLITE_ROW) {
            user_id = sqlite3_column_int(query_stmt, 0);
            const unsigned char* hash_text = sqlite3_column_text(query_stmt, 1);
            if (hash_text) password_hash = reinterpret_cast<const char*>(hash_text);
            status = sqlite3_column_int(query_stmt, 2);
        }
        sqlite3_finalize(query_stmt);
    }
    if (user_id < 0 || status != 1) return nullopt;
    if (!verify_password(password, password_hash)) return nullopt;

    // Keep only one latest session per user.
    {
        sqlite3_stmt* del_stmt = nullptr;
        const string sql = "DELETE FROM user_sessions WHERE user_id=?;";
        if (prepare_ok(raw, sql, &del_stmt)) {
            sqlite3_bind_int(del_stmt, 1, user_id);
            sqlite3_step(del_stmt);
        }
        if (del_stmt) sqlite3_finalize(del_stmt);
    }

    string token = make_token();
    long long expires = now + 7LL * 24 * 60 * 60; // 7 days
    {
        sqlite3_stmt* ins_stmt = nullptr;
        const string sql =
            "INSERT INTO user_sessions(token, user_id, created_at, expires_at) VALUES(?, ?, ?, ?);";
        if (!prepare_ok(raw, sql, &ins_stmt)) return nullopt;
        sqlite3_bind_text(ins_stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins_stmt, 2, user_id);
        sqlite3_bind_int64(ins_stmt, 3, now);
        sqlite3_bind_int64(ins_stmt, 4, expires);
        if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
            sqlite3_finalize(ins_stmt);
            return nullopt;
        }
        sqlite3_finalize(ins_stmt);
    }

    {
        sqlite3_stmt* upd_stmt = nullptr;
        const string sql = "UPDATE users SET last_login_at=?, updated_at=? WHERE id=?;";
        if (prepare_ok(raw, sql, &upd_stmt)) {
            sqlite3_bind_int64(upd_stmt, 1, now);
            sqlite3_bind_int64(upd_stmt, 2, now);
            sqlite3_bind_int(upd_stmt, 3, user_id);
            sqlite3_step(upd_stmt);
        }
        if (upd_stmt) sqlite3_finalize(upd_stmt);
    }

    json out;
    out["username"] = username;
    out["token"] = token;
    return out;
}

optional<string> validate_session(const string& token) {
    if (token.empty()) return nullopt;

    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql =
        "SELECT u.user_name "
        "FROM user_sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token=? AND s.expires_at>? "
        "LIMIT 1;";
    if (!prepare_ok(raw, sql, &stmt)) return nullopt;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now_epoch());

    optional<string> username = nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) username = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return username;
}

bool logout(const string& token) {
    if (token.empty()) return false;

    SQLiteDB db(kDbPath);
    if (!db.open()) return false;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql = "DELETE FROM user_sessions WHERE token=?;";
    if (!prepare_ok(raw, sql, &stmt)) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace auth

