#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include "bm25_retriever.h"

using namespace std;

class HybridRetriever {
public:
    // Default: builds BM25 index from docs
    HybridRetriever(const vector<string>& docs, const vector<vector<float>>& embeddings);

    // Fast: used when restoring from persisted data
    HybridRetriever(const vector<string>& docs, const vector<vector<float>>& embeddings, bool skip_bm25_build);

    // Hybrid search
    vector<string> search(const string& query, int top_k, double alpha = 0.5);

    // Expose BM25 engine for serialize/deserialize
    BM25Retriever& get_bm25_engine() { return bm25_engine; }

private:
    const vector<string>& documents;
    const vector<vector<float>>& doc_embeddings;
    BM25Retriever bm25_engine;

    double fast_cosine_similarity(const vector<float>& v1, const vector<float>& v2);
};