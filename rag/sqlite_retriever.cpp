#include "sqlite_retriever.h"
#include "../database/sqlite_db.h"
#include "loader.h"
#include "vector_retriever.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cctype>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <algorithm>

using namespace std;

// Basic tokenization for building an FTS5 query
static vector<string> tokenize_for_match(const string& s) {
    string cleaned;
    for (unsigned char ch : s) {
        if (isalnum(ch)) cleaned.push_back((char)tolower(ch));
        else cleaned.push_back(' ');
    }

    static const unordered_set<string> stopwords = {
        "what","is","are","the","and","why","for","how", "to", "of", "in", "on", "with", "an", "by", "a", "can", "be"
    };

    vector<string> words;
    stringstream ss(cleaned);
    string w;
    while (ss >> w) {
        if (w.size() >= 2 && !stopwords.count(w)) {
            words.push_back(std::move(w));
        }
    }
    return words;
}

vector<string> sqlite_retrieve(const string& query, int top_k) {
    static const string db_path = "E:/C++/code/llm_rag_cpp/database/rag.db";
    static SQLiteDB db(db_path);
    static bool is_opened = false;
    static bool fts_available = false;
    static bool fts_checked = false;
    static bool fts_initialized = false;
    static mutex db_mutex;

    lock_guard<mutex> lock(db_mutex);

    if (!is_opened) {
        if (!db.open()) {
            cerr << "[SQLITE] Error: Failed to open DB" << endl;
            return {};
        }
        is_opened = true;
    }

    if (!fts_checked) {
        const char* check_sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name='documents';";
        sqlite3_stmt* check_stmt = nullptr;
        bool has_documents = false;
        if (sqlite3_prepare_v2(db.get_raw_db(), check_sql, -1, &check_stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(check_stmt) == SQLITE_ROW) has_documents = true;
        }
        if (check_stmt) sqlite3_finalize(check_stmt);

        if (!has_documents) {
            const string txt_path = "E:/C++/code/llm_rag_cpp/rag/language.txt";
            auto docs = load_documents(txt_path);
            (void)docs;
        }

        fts_available = db.execute("CREATE VIRTUAL TABLE IF NOT EXISTS documents_fts USING fts5(content);");
        if (fts_available) {
            db.execute("INSERT INTO documents_fts(content) SELECT content FROM documents "
                "WHERE id NOT IN (SELECT rowid FROM documents_fts);");
            fts_initialized = true;
        }
        fts_checked = true;
    }

    if (!fts_available) {
        cerr << "[SQLITE] Error: FTS5 not available" << endl;
        return {};
    }

    // --- 1) FTS5 recall (wider pool for RRF merge) ---
    int recall_k = top_k * 3;
    auto query_terms = tokenize_for_match(query);
    if (query_terms.empty()) {
        return {};
    }

    string match_query;
    for (size_t i = 0; i < query_terms.size(); ++i) {
        if (i > 0) match_query += " OR ";
        match_query += query_terms[i];
    }

    const string fts5_sql = "SELECT content FROM documents_fts WHERE documents_fts MATCH ? ORDER BY bm25(documents_fts) LIMIT ?;";
    sqlite3_stmt* fts5_stmt = nullptr;
    vector<string> fts5_results;

    if (sqlite3_prepare_v2(db.get_raw_db(), fts5_sql.c_str(), -1, &fts5_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(fts5_stmt, 1, match_query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(fts5_stmt, 2, recall_k);

        while (sqlite3_step(fts5_stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(fts5_stmt, 0);
            if (text) {
                string content = reinterpret_cast<const char*>(text);
                if (!content.empty()) {
                    fts5_results.push_back(std::move(content));
                }
            }
        }
        sqlite3_finalize(fts5_stmt);
    }

    // --- 2) Vector recall ---
    vector<string> vector_results = vector_retrieve_list(query, recall_k);

    // --- 3) RRF merge (k controls rank decay) ---
    unordered_map<string, double> rrf_scores;

    // RRF: score = 1 / (k + rank)
    // k=50: slower decay so lower-ranked results still contribute a bit
    const double k_rrf = 50.0;

    for (size_t i = 0; i < fts5_results.size(); ++i) {
        const string& doc = fts5_results[i];
        double score = 1.0 / (k_rrf + static_cast<double>(i));
        rrf_scores[doc] += score;
    }

    for (size_t i = 0; i < vector_results.size(); ++i) {
        const string& doc = vector_results[i];
        double score = 1.0 / (k_rrf + static_cast<double>(i));
        rrf_scores[doc] += score;
    }

    // --- 4) Sort and return Top-K ---
    vector<pair<string, double>> scored_docs(rrf_scores.begin(), rrf_scores.end());
    sort(scored_docs.begin(), scored_docs.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
        });

    vector<string> final_results;
    final_results.reserve(top_k);
    for (size_t i = 0; i < scored_docs.size() && final_results.size() < static_cast<size_t>(top_k); ++i) {
        final_results.push_back(std::move(scored_docs[i].first));
    }

    return final_results;
}