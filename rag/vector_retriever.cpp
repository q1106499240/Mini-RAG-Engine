#include "vector_retriever.h"
#include "hybrid_retriever.h"
#include "embedder.h"
#include "vector_utils.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <omp.h>
#include <memory>
#include <thread> // used for retry backoff

using namespace std;

vector<VecChunk> global_chunks;
unique_ptr<HybridRetriever> g_hybrid_engine = nullptr;

// Sync hybrid engine (optionally load persisted BM25 index)
void sync_hybrid_engine(bool try_load_persisted = false) {
    if (global_chunks.empty()) {
        cerr << "[WARN] global_chunks is empty, nothing to sync." << endl;
        return;
    }

    static vector<string> texts;
    static vector<vector<float>> embs;
    texts.clear();
    embs.clear();

    // ???????????งน??
    texts.reserve(global_chunks.size());
    embs.reserve(global_chunks.size());

    for (const auto& chunk : global_chunks) {
        texts.push_back(chunk.text);
        embs.push_back(chunk.embedding);
    }

    if (try_load_persisted) {
        ifstream bifs("bm25.bin", ios::binary);
        if (bifs) {
            cout << "[INFO] Found bm25.bin, loading persisted index..." << endl;
            g_hybrid_engine = make_unique<HybridRetriever>(texts, embs, true);
            g_hybrid_engine->get_bm25_engine().deserialize(bifs);
            cout << "[INFO] Hybrid engine (Persisted) loaded." << endl;
            return;
        }
    }

    cout << "[INFO] Rebuilding BM25 engine from scratch..." << endl;
    g_hybrid_engine = make_unique<HybridRetriever>(texts, embs);
}

void save_index(const string& path) {
    ofstream ofs(path, ios::binary);
    if (!ofs) {
        cerr << "[ERROR] Failed to open " << path << " for writing!" << endl;
        return;
    }
    size_t count = global_chunks.size();
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& chunk : global_chunks) {
        size_t text_len = chunk.text.size();
        ofs.write(reinterpret_cast<const char*>(&text_len), sizeof(text_len));
        ofs.write(chunk.text.data(), text_len);
        size_t vec_size = chunk.embedding.size();
        ofs.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
        ofs.write(reinterpret_cast<const char*>(chunk.embedding.data()), vec_size * sizeof(float));
    }
    ofs.close();

    if (g_hybrid_engine) {
        ofstream bofs("bm25.bin", ios::binary);
        if (bofs) {
            g_hybrid_engine->get_bm25_engine().serialize(bofs);
            cout << "[INFO] BM25 Index persisted to bm25.bin" << endl;
        }
    }
    cout << "All indexes saved successfully. (Total: " << count << " chunks)" << endl;
}

bool load_index(const string& path) {
    ifstream ifs(path, ios::binary);
    if (!ifs) return false;

    size_t count;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    global_chunks.clear();
    global_chunks.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        VecChunk chunk;
        size_t text_len;
        ifs.read(reinterpret_cast<char*>(&text_len), sizeof(text_len));
        chunk.text.resize(text_len);
        ifs.read(&chunk.text[0], text_len);
        size_t vec_size;
        ifs.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
        chunk.embedding.resize(vec_size);
        ifs.read(reinterpret_cast<char*>(chunk.embedding.data()), vec_size * sizeof(float));
        global_chunks.push_back(chunk);
    }

    sync_hybrid_engine(true);
    return true;
}

void build_index(const vector<string>& docs) {
    global_chunks.clear();
    global_chunks.reserve(docs.size());

    int fail_count = 0;
    const int max_allowed_fails = 50;

    cout << "[BUILD] Starting build_index for " << docs.size() << " documents..." << endl;

    for (size_t i = 0; i < docs.size(); ++i) {
        if (docs[i].empty()) continue;

        bool success = false;
        vector<float> emb;

        // Retry embedding up to 3 times per document
        for (int retry = 0; retry < 3; ++retry) {
            try {
                emb = embed_text(docs[i]);
                if (!emb.empty()) {
                    success = true;
                    break;
                }
            }
            catch (const std::exception& e) {
                // Backoff a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (success) {
            VecChunk chunk;
            chunk.text = docs[i];
            chunk.embedding = emb;
            global_chunks.push_back(chunk);
            fail_count = 0; // reset consecutive-failure counter
        }
        else {
            fail_count++;
            cerr << "\n[WARN] Failed to embed doc " << i << " after retries." << endl;
            if (fail_count > max_allowed_fails) {
                cerr << "[FATAL] Connection to GPU Server lost. Stopping build." << endl;
                break;
            }
        }

        // ?????
        if (i % 1000 == 0 || i == docs.size() - 1) {
            printf("\r[REBUILD] Progress: %6zu / %6zu (%.2f%%)",
                i, docs.size(), (double)i * 100.0 / docs.size());
            fflush(stdout);
        }
    }
    cout << "\n[BUILD] Finished. Processed " << global_chunks.size() << " successful chunks." << endl;

    sync_hybrid_engine(false);
}

vector<string> vector_retrieve_list(const string& query, int topk) {
    if (!g_hybrid_engine) {
        cerr << "[ERROR] Hybrid engine not initialized!" << endl;
        return {};
    }
    return g_hybrid_engine->search(query, topk, 0.5);
}

string vector_retrieve(const string& query, int topk) {
    vector<string> chunks = vector_retrieve_list(query, topk);
    string result;
    for (const auto& s : chunks) result += s + "\n";
    return result;
}