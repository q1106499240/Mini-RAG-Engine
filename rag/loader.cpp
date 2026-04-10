#include "loader.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "../database/sqlite_db.h"

using namespace std;

vector<string> split_into_chunks(const string& text, size_t chunk_size);
void trim(string& s);

vector<string> load_documents(const string& path) {
    const string db_file_path = "E:/C++/code/llm_rag_cpp/database/rag.db";
    SQLiteDB db(db_file_path);

    if (!db.open()) {
        cerr << "[ERROR] load_documents: Failed to open DB at: " << db_file_path << endl;
        return {};
    }

    db.execute("DROP TABLE IF EXISTS documents;");
    db.execute("DROP TABLE IF EXISTS documents_fts;");

    if (!db.execute("CREATE TABLE documents (id INTEGER PRIMARY KEY AUTOINCREMENT, content TEXT);")) {
        cerr << "[ERROR] Failed to create table: " << sqlite3_errmsg(db.get_raw_db()) << endl;
        return {};
    }

    // Try enabling FTS only when SQLite supports it.
    bool fts_enabled = false;
    sqlite3_stmt* opt_stmt = nullptr;
    const char* opt_sql = "SELECT sqlite_compileoption_used('ENABLE_FTS5');";
    if (sqlite3_prepare_v2(db.get_raw_db(), opt_sql, -1, &opt_stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(opt_stmt) == SQLITE_ROW) {
        fts_enabled = sqlite3_column_int(opt_stmt, 0) == 1;
    }
    if (opt_stmt) sqlite3_finalize(opt_stmt);

    if (fts_enabled) {
        db.execute("CREATE VIRTUAL TABLE documents_fts USING fts5(content);");
    }

    if (!filesystem::exists(path)) {
        cerr << "[ERROR] load_documents: TXT file not found: " << path
             << " (Current Working Directory: " << filesystem::current_path() << ")\n";
        return {};
    }

    ifstream file(path, ios::in | ios::binary);
    if (!file.is_open()) {
        char errbuf[128] = { 0 };
        strerror_s(errbuf, sizeof(errbuf), errno);
        cerr << "[ERROR] load_documents: failed to open file: " << path << " error: " << errbuf << endl;
        return {};
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string text = buffer.str();
    file.close();

    auto chunks = split_into_chunks(text, 512);
    if (chunks.empty()) {
        cerr << "[WARN] No chunks generated from file." << endl;
        return {};
    }

    db.execute("BEGIN TRANSACTION;");

    sqlite3_stmt* stmt_doc = nullptr;
    sqlite3_stmt* stmt_fts = nullptr;

    const string sql_doc = "INSERT INTO documents (content) VALUES (?);";
    if (sqlite3_prepare_v2(db.get_raw_db(), sql_doc.c_str(), -1, &stmt_doc, nullptr) != SQLITE_OK) {
        cerr << "[ERROR] SQL prepare error: " << sqlite3_errmsg(db.get_raw_db()) << endl;
        db.execute("ROLLBACK;");
        return {};
    }

    if (fts_enabled) {
        const string sql_fts = "INSERT INTO documents_fts (content) VALUES (?);";
        if (sqlite3_prepare_v2(db.get_raw_db(), sql_fts.c_str(), -1, &stmt_fts, nullptr) != SQLITE_OK) {
            fts_enabled = false;
        }
    }

    for (const auto& chunk : chunks) {
        sqlite3_bind_text(stmt_doc, 1, chunk.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt_doc) != SQLITE_DONE) {
            cerr << "[ERROR] SQL insert documents error: " << sqlite3_errmsg(db.get_raw_db()) << endl;
        }
        sqlite3_reset(stmt_doc);

        if (fts_enabled && stmt_fts) {
            sqlite3_bind_text(stmt_fts, 1, chunk.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt_fts);
            sqlite3_reset(stmt_fts);
        }
    }

    sqlite3_finalize(stmt_doc);
    if (stmt_fts) sqlite3_finalize(stmt_fts);

    db.execute("COMMIT;");

    cout << "[SUCCESS] Database updated: " << chunks.size() << " chunks stored in " << db_file_path << endl;
    return chunks;
}

vector<string> split_into_chunks(const string& text, size_t chunk_size) {
    vector<string> chunks;

    string normalized_text;
    normalized_text.reserve(text.size());
    bool last_was_space = false;

    for (char c : text) {
        if (isspace(static_cast<unsigned char>(c))) {
            if (!last_was_space) {
                normalized_text += ' ';
                last_was_space = true;
            }
        }
        else {
            normalized_text += c;
            last_was_space = false;
        }
    }

    const size_t overlap = 50;
    const size_t step = (chunk_size > overlap) ? (chunk_size - overlap) : chunk_size;

    for (size_t start = 0; start < normalized_text.size(); start += step) {
        // Avoid cutting words in the middle: adjust chunk boundaries to nearest spaces.
        size_t real_start = start;
        if (real_start > 0) {
            const size_t left_limit = (real_start > 30) ? (real_start - 30) : 0;
            for (size_t p = real_start; p > left_limit; --p) {
                if (isspace(static_cast<unsigned char>(normalized_text[p - 1]))) {
                    real_start = p;
                    break;
                }
            }
        }

        size_t real_end = min(real_start + chunk_size, normalized_text.size());
        if (real_end < normalized_text.size()) {
            const size_t right_limit = min(real_end + 30, normalized_text.size());
            for (size_t p = real_end; p < right_limit; ++p) {
                if (isspace(static_cast<unsigned char>(normalized_text[p]))) {
                    real_end = p;
                    break;
                }
            }
        }

        if (real_end <= real_start) continue;
        size_t len = real_end - real_start;
        string chunk = normalized_text.substr(real_start, len);

        trim(chunk);
        if (chunk.size() > 10) {
            chunks.push_back(chunk);
        }

        if (start + chunk_size >= normalized_text.size()) break;
    }

    return chunks;
}

void trim(string& s) {
    if (s.empty()) return;
    const char* whitespace = " \t\r\n";
    size_t first = s.find_first_not_of(whitespace);
    if (first == string::npos) {
        s.clear();
        return;
    }
    size_t last = s.find_last_not_of(whitespace);
    s = s.substr(first, last - first + 1);
}
