#include "http_auth.h"
#include "auth_service.h"
#include "rbac_service.h"
#include "../database/sqlite_db.h"
#include <sqlite3.h>

using namespace std;
using json = nlohmann::json;

namespace {
static const string kDbPath = "E:/C++/code/llm_rag_cpp/database/rag.db";

bool prepare_ok(sqlite3* db, const string& sql, sqlite3_stmt** stmt) {
    return sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr) == SQLITE_OK;
}

optional<int> user_id_from_token(const string& token) {
    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql =
        "SELECT user_id FROM user_sessions "
        "WHERE token=? AND expires_at>? "
        "LIMIT 1;";
    if (!prepare_ok(raw, sql, &stmt)) return nullopt;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(nullptr));

    optional<int> uid = nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) uid = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return uid;
}

optional<string> username_from_user_id(int user_id) {
    SQLiteDB db(kDbPath);
    if (!db.open()) return nullopt;
    sqlite3* raw = db.get_raw_db();

    sqlite3_stmt* stmt = nullptr;
    const string sql = "SELECT user_name FROM users WHERE id=? LIMIT 1;";
    if (!prepare_ok(raw, sql, &stmt)) return nullopt;
    sqlite3_bind_int(stmt, 1, user_id);
    optional<string> name = nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        if (t) name = reinterpret_cast<const char*>(t);
    }
    sqlite3_finalize(stmt);
    return name;
}
}

namespace auth {

optional<string> extract_token(const httplib::Request& req) {
    auto authz = req.get_header_value("Authorization");
    const string bearer = "Bearer ";
    if (!authz.empty() && authz.rfind(bearer, 0) == 0) {
        string token = authz.substr(bearer.size());
        if (!token.empty()) return token;
    }
    if (req.has_param("token")) {
        string token = req.get_param_value("token");
        if (!token.empty()) return token;
    }
    return nullopt;
}

optional<json> get_me_from_token(const string& token) {
    auto uid = user_id_from_token(token);
    if (!uid.has_value()) return nullopt;
    auto uname = username_from_user_id(*uid);
    if (!uname.has_value()) return nullopt;

    auto ctx = load_rbac_for_user(*uid, *uname);
    if (!ctx.has_value()) return nullopt;

    json out;
    out["user_name"] = ctx->user_name;
    out["role_codes"] = ctx->role_codes;
    out["permissions"] = ctx->permissions;
    return out;
}

bool require_permission(const httplib::Request& req, httplib::Response& res, const string& permission_code) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");

    auto token = extract_token(req);
    if (!token.has_value()) {
        res.status = 401;
        res.set_content(json({{"status","error"},{"error","missing token"}}).dump(), "application/json");
        return false;
    }

    auto me = get_me_from_token(*token);
    if (!me.has_value()) {
        res.status = 401;
        res.set_content(json({{"status","error"},{"error","invalid session"}}).dump(), "application/json");
        return false;
    }

    const auto& perms = (*me)["permissions"];
    for (const auto& p : perms) {
        if (p.get<string>() == permission_code) return true;
    }

    res.status = 403;
    res.set_content(json({{"status","error"},{"error","forbidden"},{"required",permission_code}}).dump(), "application/json");
    return false;
}

} // namespace auth

