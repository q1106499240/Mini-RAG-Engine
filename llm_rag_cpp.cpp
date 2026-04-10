#include "llm_rag_cpp.h"
#include "include/httplib.h"           
#include <nlohmann/json.hpp>   
#include "rag/vector_retriever.h"
#include "rag/loader.h"
#include "rag/retriever.h"
#include "rag/prompt.h"
#include "llm/llm_client.h"
#include "rag/sqlite_retriever.h"  
#include "auth/auth_service.h"
#include "auth/rbac_service.h"
#include "auth/http_auth.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>            
#include <numeric>
#include <filesystem>
#include <set>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <cctype>
#include <exception>
#include "database/sqlite_db.h"

using namespace std;
using json = nlohmann::json;
using namespace httplib;

// --- Helper Functions: text normalization + evaluation match ---
static string normalize_text(const string& s) {
    string out;
    out.reserve(s.size());

    for (unsigned char ch : s) {
        if (std::isspace(ch) || std::ispunct(ch)) continue;
        out.push_back((char)std::tolower(ch));
    }
    return out;
}

static vector<string> tokenize_ascii_words(const string& s) {
    string cleaned;
    cleaned.reserve(s.size());

    for (unsigned char ch : s) {
        if (std::isalnum(ch)) cleaned.push_back((char)std::tolower(ch));
        else cleaned.push_back(' ');
    }

    vector<string> tokens;
    string w;
    stringstream ss(cleaned);
    while (ss >> w) {
        if (w.size() >= 3) tokens.push_back(w);
    }
    return tokens;
}

static bool english_token_match(
    const string& retrieved,
    const string& expected,
    int& overlap_out,
    double& ratio_out
) {
    static const unordered_set<string> stopwords = {
        "a","an","the","and","or","but","if","then","else",
        "is","are","was","were","be","been","being",
        "what","which","who","whom","whose","why","how",
        "to","of","in","on","at","by","for","with","from","into","up","down","out","over","under",
        "this","that","these","those",
        "it","its","as","we","you","they","i",
        "via"
    };

    auto stem_token = [](string t) {
        // Minimal stemming: messages -> message
        if (t.size() > 4 && t.back() == 's') t.pop_back();
        return t;
    };

    auto exp_tokens = tokenize_ascii_words(expected);
    auto ret_tokens = tokenize_ascii_words(retrieved);

    unordered_set<string> exp_set;
    unordered_set<string> ret_set;
    for (const auto& t : exp_tokens) {
        if (stopwords.find(t) == stopwords.end()) exp_set.insert(stem_token(t));
    }
    for (const auto& t : ret_tokens) {
        if (stopwords.find(t) == stopwords.end()) ret_set.insert(stem_token(t));
    }

    if (exp_set.empty() || ret_set.empty()) {
        overlap_out = 0;
        ratio_out = 0.0;
        return false;
    }

    int overlap = 0;
    for (const auto& t : exp_set) {
        if (ret_set.find(t) != ret_set.end()) ++overlap;
    }

    double ratio = static_cast<double>(overlap) / static_cast<double>(exp_set.size());
    overlap_out = overlap;
    ratio_out = ratio;

    // Threshold: avoid "always true" and "always false". Will be calibrated from debug output.
    return overlap >= 4 && ratio >= 0.30;
}

bool fuzzy_match(const string& retrieved, const string& expected) {
    if (expected.empty()) return false;

    string expected_norm = normalize_text(expected);
    string retrieved_norm = normalize_text(retrieved);
    if (expected_norm.empty() || retrieved_norm.empty()) return false;

    // 1) Substring match after normalization.
    if (retrieved_norm.find(expected_norm) != string::npos) return true;

    // Decide whether we should allow a char-level fallback.
    bool expected_has_non_ascii = false;
    for (unsigned char ch : expected) {
        if (ch >= 128) {
            expected_has_non_ascii = true;
            break;
        }
    }

    // 2) ASCII/English: use stricter token overlap (avoid always-true char coverage).
    if (!expected_has_non_ascii) {
        int overlap = 0;
        double ratio = 0.0;
        return english_token_match(retrieved, expected, overlap, ratio);
    }

    // 3) Non-ASCII (e.g., Chinese): use char overlap fallback.
    unordered_set<char> e_chars;
    for (char c : expected_norm) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (isalnum(uc)) e_chars.insert(c);
    }
    if (e_chars.empty()) return false;

    int match_chars = 0;
    for (char c : e_chars) {
        if (retrieved_norm.find(c) != string::npos) ++match_chars;
    }
    double score = static_cast<double>(match_chars) / e_chars.size();
    return score >= 0.8;
}

// --- Evaluation logic ---
json perform_evaluation(const string& dataset_path) {
    string actual_path = dataset_path;
    if (!filesystem::exists(actual_path)) actual_path = "dataset.json";

    ifstream f(actual_path);
    if (!f.is_open()) return { {"error", "dataset file not found: " + actual_path} };

    json dataset;
    try { dataset = json::parse(f); }
    catch (...) { return { {"error", "JSON Parse Error"} }; }

    int hits_at_1 = 0, hits_at_3 = 0;
    int empty_retrieval_count = 0;
    vector<double> latencies;
    double total_mrr = 0.0;
    int total_valid = 0;

    for (int idx = 0; idx < (int)dataset.size(); ++idx) {
        const auto& item = dataset[idx];
        if (!item.contains("question")) continue;

        string query = item.value("question", "");
        string expected = item.value("answer", "");
        if (query.empty() || expected.empty()) continue;

        ++total_valid;

        auto start = chrono::steady_clock::now();
        vector<string> retrieved_chunks = sqlite_retrieve(query, 3);
        auto end = chrono::steady_clock::now();

        latencies.push_back(chrono::duration<double, milli>(end - start).count());
        if (retrieved_chunks.empty()) ++empty_retrieval_count;

        // Minimal debug: print first 2 samples so we can verify retrieval vs matching.
        if (idx < 2) {
            auto clip = [](string s, size_t n) {
                for (char& c : s) {
                    if (c == '\n' || c == '\r') c = ' ';
                }
                if (s.size() > n) s = s.substr(0, n);
                return s;
            };
            cerr << "[EVAL] sample=" << idx
                 << " query=\"" << clip(query, 60) << "\""
                 << " expected=\"" << clip(expected, 60) << "\"" << endl;
            bool expected_has_non_ascii = false;
            for (unsigned char ch : expected) {
                if (ch >= 128) { expected_has_non_ascii = true; break; }
            }
            for (size_t i = 0; i < retrieved_chunks.size(); ++i) {
                cerr << "  retrieved[" << i << "]=\"" << clip(retrieved_chunks[i], 90) << "\"" << endl;
                int overlap = 0;
                double ratio = 0.0;
                bool ok = fuzzy_match(retrieved_chunks[i], expected);
                if (!expected_has_non_ascii) {
                    english_token_match(retrieved_chunks[i], expected, overlap, ratio);
                    cerr << "    match=" << (ok ? "true" : "false")
                         << " overlap=" << overlap
                         << " ratio=" << ratio << endl;
                } else {
                    cerr << "    match=" << (ok ? "true" : "false") << endl;
                }
            }
        }

        for (int i = 0; i < (int)retrieved_chunks.size(); ++i) {
            if (fuzzy_match(retrieved_chunks[i], expected)) {
                if (i == 0) ++hits_at_1;
                ++hits_at_3;
                total_mrr += 1.0 / (i + 1);
                break;
            }
        }
    }

    double avg_latency = latencies.empty() ? 0 : accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

    json report;
    report["status"] = "success";
    report["total_samples"] = total_valid;
    report["hit_at_1"] = total_valid > 0 ? (double)hits_at_1 / total_valid : 0;
    report["hit_at_3"] = total_valid > 0 ? (double)hits_at_3 / total_valid : 0;
    report["mrr"] = total_valid > 0 ? total_mrr / total_valid : 0;
    report["avg_latency_ms"] = avg_latency;
    report["empty_retrieval_count"] = empty_retrieval_count;
    return report;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    string txt_path = "E:/C++/code/llm_rag_cpp/rag/language.txt";

    if (argc > 1 && string(argv[1]) == "build_db") {
        auto docs = load_documents(txt_path);
        return 0;
    }

    if (argc > 1 && string(argv[1]) == "rebuild") {
        auto docs = load_documents(txt_path);
        if (docs.empty()) return -1;
        build_index(docs);
        save_index("index.dat");
        return 0;
    }

    cerr << "[SYSTEM] Starting RAG Engine in Server Mode..." << endl;
    load_index("index.dat");
    if (!auth::init_auth_tables()) {
        cerr << "[AUTH] Failed to initialize users/session tables." << endl;
    } else {
        cerr << "[AUTH] Auth tables ready." << endl;
    }
    if (!auth::init_rbac_tables()) {
        cerr << "[RBAC] Failed to initialize roles/permissions tables." << endl;
    } else {
        cerr << "[RBAC] RBAC tables ready." << endl;
    }

    Server svr;

    // --- 0. Auth endpoints ---
    svr.Post("/auth/login", [&](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            string username = body.value("username", "");
            string password = body.value("password", "");
            auto session = auth::login(username, password);
            if (!session.has_value()) {
                res.status = 401;
                res.set_content(json({{"status", "error"}, {"error", "invalid username or password"}}).dump(), "application/json");
                return;
            }
            res.set_content(json({{"status", "success"}, {"data", *session}}).dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(json({{"status", "error"}, {"error", "bad request"}}).dump(), "application/json");
        }
    });

    svr.Post("/auth/register", [&](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            string username = body.value("username", "");
            string password = body.value("password", "");
            string email = body.value("email", "");
            auto reg = auth::register_user(username, password, email);
            if (!reg.has_value()) {
                res.status = 400;
                res.set_content(json({{"status", "error"}, {"error", "register failed"}}).dump(), "application/json");
                return;
            }
            res.set_content(json({{"status", "success"}, {"data", *reg}}).dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(json({{"status", "error"}, {"error", "bad request"}}).dump(), "application/json");
        }
    });

    svr.Get("/auth/session", [&](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        auto tokenOpt = auth::extract_token(req);
        if (!tokenOpt.has_value()) {
            res.status = 400;
            res.set_content(json({{"status", "error"}, {"error", "missing token"}}).dump(), "application/json");
            return;
        }
        auto username = auth::validate_session(*tokenOpt);
        if (!username.has_value()) {
            res.status = 401;
            res.set_content(json({{"status", "error"}, {"error", "invalid session"}}).dump(), "application/json");
            return;
        }
        res.set_content(json({{"status", "success"}, {"data", {{"username", *username}}}}).dump(), "application/json");
    });

    svr.Post("/auth/logout", [&](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            string token = body.value("token", "");
            auth::logout(token);
            res.set_content(json({{"status", "success"}}).dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(json({{"status", "error"}, {"error", "bad request"}}).dump(), "application/json");
        }
    });

    svr.Get("/auth/me", [&](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        auto token = auth::extract_token(req);
        if (!token.has_value()) {
            res.status = 401;
            res.set_content(json({{"status","error"},{"error","missing token"}}).dump(), "application/json");
            return;
        }
        auto me = auth::get_me_from_token(*token);
        if (!me.has_value()) {
            res.status = 401;
            res.set_content(json({{"status","error"},{"error","invalid session"}}).dump(), "application/json");
            return;
        }
        res.set_content(json({{"status","success"},{"data",*me}}).dump(), "application/json");
    });

    // --- 0.1 Admin user-role management ---
    svr.Get("/admin/roles", [&](const Request& req, Response& res) {
        if (!auth::require_permission(req, res, "user:manage")) return;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        auto roles = auth::list_roles();
        res.set_content(json({{"status","success"},{"data",roles}}).dump(), "application/json");
    });

    svr.Get("/admin/users", [&](const Request& req, Response& res) {
        if (!auth::require_permission(req, res, "user:manage")) return;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        auto users = auth::list_users_with_roles();
        res.set_content(json({{"status","success"},{"data",users}}).dump(), "application/json");
    });

    svr.Post("/admin/users/role", [&](const Request& req, Response& res) {
        if (!auth::require_permission(req, res, "user:manage")) return;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            int user_id = body.value("user_id", -1);
            string role_code = body.value("role_code", "");
            bool ok = auth::set_user_role(user_id, role_code);
            if (!ok) {
                res.status = 400;
                res.set_content(json({{"status","error"},{"error","set role failed"}}).dump(), "application/json");
                return;
            }
            res.set_content(json({{"status","success"}}).dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(json({{"status","error"},{"error","bad request"}}).dump(), "application/json");
        }
    });

    // --- 1. Q&A endpoint ---
    svr.Post("/ask", [&](const Request& req, Response& res) {
        if (!auth::require_permission(req, res, "chat:query")) return;
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto start_time = chrono::steady_clock::now();
            auto j_input = json::parse(req.body);
            string question = j_input.value("query", "");
            auto chunks = sqlite_retrieve(question, 3);
            string context;
            for (size_t i = 0; i < chunks.size(); ++i) {
                context += "[Source " + to_string(i + 1) + "]: " + chunks[i] + "\n\n";
            }
            string prompt = build_prompt(context, question);

            res.set_chunked_content_provider(
                "text/event-stream",
                [prompt, chunks, start_time](size_t, DataSink& sink) {
                    if (!sink.is_writable()) return false;
                    call_llm_stream(prompt, [&](const string& token) {
                        if (!sink.is_writable()) return false;
                        json chunk_json;
                        chunk_json["content"] = token;
                        string sse_data = "data: " + chunk_json.dump() + "\n\n";
                        sink.write(sse_data.c_str(), sse_data.size());
                        return true;
                    });
                    auto end_time = chrono::steady_clock::now();
                    double duration = chrono::duration<double, milli>(end_time - start_time).count();
                    json meta;
                    meta["type"] = "metadata";
                    meta["latency"] = duration;
                    meta["sources"] = chunks;
                    string meta_line = "data: " + meta.dump() + "\n\n";
                    sink.write(meta_line.c_str(), meta_line.size());
                    sink.write("data: [DONE]\n\n", 15);
                    sink.done();
                    return true;
                }
            );
        }
        catch (...) {
            res.status = 500;
            res.set_content("{\"error\":\"ask failed\"}", "application/json");
        }
    });

    // --- 2. Evaluation endpoint ---
    svr.Get("/eval", [&](const Request& req, Response& res) {
        // Only allow admins to run evaluation (example permission).
        // If you want NORMAL_USER to see eval, change permission code here.
        if (!auth::require_permission(req, res, "log:read")) return;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");

        string absolute_path = R"(E:\C++\code\llm_rag_cpp\api\dataset.json)";

        try {
            auto start_all = chrono::steady_clock::now();
            json result = perform_evaluation(absolute_path);
            auto end_all = chrono::steady_clock::now();

            double total_time = chrono::duration<double>(end_all - start_all).count();
            cerr << "[EVAL] Finished in " << total_time << " seconds" << endl;

            res.set_content(result.dump(), "application/json");
        }
        catch (const exception& e) {
            res.status = 500;
            res.set_content(json({{"status", "error"}, {"error", e.what()}}).dump(), "application/json");
        }
        catch (...) {
            res.status = 500;
            res.set_content(json({{"status", "error"}, {"error", "unknown error"}}).dump(), "application/json");
        }
    });

    // --- 3. CORS preflight OPTIONS handling ---
    svr.Options(R"(/.*)", [](const Request&, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 200;
    });

    cerr << "[SERVER] RAG API is running on http://localhost:8080" << endl;
    svr.listen("0.0.0.0", 8080);
    return 0;
}
