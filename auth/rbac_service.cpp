#include "rbac_service.h"
#include "../database/sqlite_db.h"
#include <sqlite3.h>
#include <unordered_set>
#include <sstream>

using namespace std;
using json = nlohmann::json;

namespace {
static const string kDbPath = "E:/C++/code/llm_rag_cpp/database/rag.db";

bool prepare_ok(sqlite3* db, const string& sql, sqlite3_stmt** stmt) {
    return sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr) == SQLITE_OK;
}
}

namespace auth {

bool init_rbac_tables() {
    SQLiteDB db(kDbPath);
    if (!db.open()) return false;

    // These are compatible with the SQL you already executed.
    const string roles_sql =
        "CREATE TABLE IF NOT EXISTS roles ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "role_name TEXT NOT NULL,"
        "role_code TEXT NOT NULL UNIQUE,"
        "description TEXT"
        ");";

    const string perms_sql =
        "CREATE TABLE IF NOT EXISTS permissions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "permission_name TEXT NOT NULL,"
        "permission_code TEXT NOT NULL UNIQUE,"
        "resource TEXT,"
        "action TEXT"
        ");";

    const string role_perms_sql =
        "CREATE TABLE IF NOT EXISTS role_permissions ("
        "role_id INTEGER NOT NULL,"
        "permission_id INTEGER NOT NULL,"
        "PRIMARY KEY(role_id, permission_id)"
        ");";

    const string user_roles_sql =
        "CREATE TABLE IF NOT EXISTS user_roles ("
        "user_id INTEGER NOT NULL,"
        "role_id INTEGER NOT NULL,"
        "PRIMARY KEY(user_id, role_id)"
        ");";

    bool ok = db.execute(roles_sql) &&
              db.execute(perms_sql) &&
              db.execute(role_perms_sql) &&
              db.execute(user_roles_sql);
    if (!ok) return false;
    return ensure_rbac_seed_data();
}

bool ensure_rbac_seed_data() {
    SQLiteDB db(kDbPath);
    if (!db.open()) return false;

    const string seed_roles_sql =
        "INSERT OR IGNORE INTO roles(role_name, role_code, description) VALUES "
        "('Super Admin','SUPER_ADMIN','all permissions including system settings'),"
        "('Normal Admin','NORMAL_ADMIN','knowledge base and document operations'),"
        "('Normal User','NORMAL_USER','chat and basic read permissions');";

    const string seed_perms_sql =
        "INSERT OR IGNORE INTO permissions(permission_name, permission_code, resource, action) VALUES "
        "('Read knowledge base','kb:read','knowledge_base','read'),"
        "('Create knowledge base','kb:create','knowledge_base','create'),"
        "('Delete knowledge base','kb:delete','knowledge_base','delete'),"
        "('Update knowledge base','kb:update','knowledge_base','update'),"
        "('Upload document','doc:upload','document','create'),"
        "('Delete document','doc:delete','document','delete'),"
        "('Vectorize document','doc:vectorize','document','update'),"
        "('Read chunks','doc:chunk_read','document','read'),"
        "('Chat query','chat:query','retrieval','read'),"
        "('Read chat history','chat:history','retrieval','read'),"
        "('Clear chat context','chat:clear','retrieval','delete'),"
        "('Manage users','user:manage','system','update'),"
        "('Read logs','log:read','system','read');";

    const string super_admin_sql =
        "INSERT OR IGNORE INTO role_permissions(role_id, permission_id) "
        "SELECT r.id, p.id FROM roles r, permissions p WHERE r.role_code='SUPER_ADMIN';";

    const string normal_admin_sql =
        "INSERT OR IGNORE INTO role_permissions(role_id, permission_id) "
        "SELECT r.id, p.id FROM roles r, permissions p "
        "WHERE r.role_code='NORMAL_ADMIN' AND p.permission_code IN "
        "('kb:read','kb:create','kb:update','kb:delete','doc:upload','doc:delete','doc:vectorize','doc:chunk_read','log:read');";

    const string normal_user_sql =
        "INSERT OR IGNORE INTO role_permissions(role_id, permission_id) "
        "SELECT r.id, p.id FROM roles r, permissions p "
        "WHERE r.role_code='NORMAL_USER' AND p.permission_code IN "
        "('kb:read','chat:query','chat:history','chat:clear');";

    return db.execute(seed_roles_sql) &&
           db.execute(seed_perms_sql) &&
           db.execute(super_admin_sql) &&
           db.execute(normal_admin_sql) &&
           db.execute(normal_user_sql);
}

bool assign_default_role_for_user(int user_id, const string& role_code) {
    if (user_id <= 0 || role_code.empty()) return false;
    SQLiteDB db(kDbPath);
    if (!db.open()) return false;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* role_stmt = nullptr;
    if (!prepare_ok(raw, "SELECT id FROM roles WHERE role_code=? LIMIT 1;", &role_stmt)) return false;
    sqlite3_bind_text(role_stmt, 1, role_code.c_str(), -1, SQLITE_TRANSIENT);
    int role_id = -1;
    if (sqlite3_step(role_stmt) == SQLITE_ROW) role_id = sqlite3_column_int(role_stmt, 0);
    sqlite3_finalize(role_stmt);
    if (role_id <= 0) return false;

    sqlite3_stmt* bind_stmt = nullptr;
    if (!prepare_ok(raw, "INSERT OR IGNORE INTO user_roles(user_id, role_id) VALUES(?, ?);", &bind_stmt)) return false;
    sqlite3_bind_int(bind_stmt, 1, user_id);
    sqlite3_bind_int(bind_stmt, 2, role_id);
    bool ok = sqlite3_step(bind_stmt) == SQLITE_DONE;
    sqlite3_finalize(bind_stmt);
    return ok;
}

optional<RbacContext> load_rbac_for_user(int user_id, const string& user_name) {
    if (user_id < 0) return nullopt;

    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    RbacContext ctx;
    ctx.user_id = user_id;
    ctx.user_name = user_name;

    // Roles
    {
        sqlite3_stmt* stmt = nullptr;
        const string sql =
            "SELECT r.role_code "
            "FROM user_roles ur JOIN roles r ON r.id = ur.role_id "
            "WHERE ur.user_id=?;";
        if (!prepare_ok(raw, sql, &stmt)) return nullopt;
        sqlite3_bind_int(stmt, 1, user_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(stmt, 0);
            if (t) ctx.role_codes.emplace_back(reinterpret_cast<const char*>(t));
        }
        sqlite3_finalize(stmt);
    }

    // Permissions (dedup)
    {
        unordered_set<string> uniq;
        sqlite3_stmt* stmt = nullptr;
        const string sql =
            "SELECT p.permission_code "
            "FROM user_roles ur "
            "JOIN role_permissions rp ON rp.role_id = ur.role_id "
            "JOIN permissions p ON p.id = rp.permission_id "
            "WHERE ur.user_id=?;";
        if (!prepare_ok(raw, sql, &stmt)) return nullopt;
        sqlite3_bind_int(stmt, 1, user_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(stmt, 0);
            if (!t) continue;
            string code = reinterpret_cast<const char*>(t);
            if (uniq.insert(code).second) ctx.permissions.push_back(code);
        }
        sqlite3_finalize(stmt);
    }

    return ctx;
}

vector<json> list_roles() {
    SQLiteDB db(kDbPath);
    if (!db.open()) return {};
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql = "SELECT id, role_name, role_code, COALESCE(description,'') FROM roles ORDER BY id ASC;";
    if (!prepare_ok(raw, sql, &stmt)) return {};

    vector<json> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json row;
        row["id"] = sqlite3_column_int(stmt, 0);
        row["role_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        row["role_code"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        row["description"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        out.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return out;
}

vector<json> list_users_with_roles() {
    SQLiteDB db(kDbPath);
    if (!db.open()) return {};
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql =
        "SELECT u.id, u.user_name, u.status, COALESCE(u.email,''), COALESCE(GROUP_CONCAT(r.role_code, ','),'') "
        "FROM users u "
        "LEFT JOIN user_roles ur ON ur.user_id = u.id "
        "LEFT JOIN roles r ON r.id = ur.role_id "
        "GROUP BY u.id, u.user_name, u.status, u.email "
        "ORDER BY u.id ASC;";
    if (!prepare_ok(raw, sql, &stmt)) return {};

    vector<json> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json row;
        row["id"] = sqlite3_column_int(stmt, 0);
        row["user_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        row["status"] = sqlite3_column_int(stmt, 2);
        row["email"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

        vector<string> roles;
        const unsigned char* role_text = sqlite3_column_text(stmt, 4);
        string role_csv = role_text ? reinterpret_cast<const char*>(role_text) : "";
        stringstream ss(role_csv);
        string part;
        while (getline(ss, part, ',')) {
            if (!part.empty()) roles.push_back(part);
        }
        row["role_codes"] = roles;
        out.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool set_user_role(int user_id, const string& role_code) {
    if (user_id <= 0 || role_code.empty()) return false;
    SQLiteDB db(kDbPath);
    if (!db.open()) return false;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* role_stmt = nullptr;
    if (!prepare_ok(raw, "SELECT id FROM roles WHERE role_code=? LIMIT 1;", &role_stmt)) return false;
    sqlite3_bind_text(role_stmt, 1, role_code.c_str(), -1, SQLITE_TRANSIENT);
    int role_id = -1;
    if (sqlite3_step(role_stmt) == SQLITE_ROW) role_id = sqlite3_column_int(role_stmt, 0);
    sqlite3_finalize(role_stmt);
    if (role_id <= 0) return false;

    sqlite3_stmt* del_stmt = nullptr;
    if (!prepare_ok(raw, "DELETE FROM user_roles WHERE user_id=?;", &del_stmt)) return false;
    sqlite3_bind_int(del_stmt, 1, user_id);
    bool del_ok = sqlite3_step(del_stmt) == SQLITE_DONE;
    sqlite3_finalize(del_stmt);
    if (!del_ok) return false;

    sqlite3_stmt* ins_stmt = nullptr;
    if (!prepare_ok(raw, "INSERT INTO user_roles(user_id, role_id) VALUES(?, ?);", &ins_stmt)) return false;
    sqlite3_bind_int(ins_stmt, 1, user_id);
    sqlite3_bind_int(ins_stmt, 2, role_id);
    bool ins_ok = sqlite3_step(ins_stmt) == SQLITE_DONE;
    sqlite3_finalize(ins_stmt);
    return ins_ok;
}

} // namespace auth

