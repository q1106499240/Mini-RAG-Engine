#include "hybrid_retriever.h"
#include "embedder.h"
#include <cmath>
#include <algorithm>
#include <omp.h>
#include <unordered_map>
#include <set>
#include <sstream>

using namespace std;

// Simple query cleaner for Q/A style prompts
static string clean_query(const string& query) {
    // High-frequency stop words in typical questions
    static const set<string> stop_words = {
        "what", "is", "and", "the", "of", "who", "why", "are", "it", "its", "to", "in", "a", "an"
    };

    stringstream ss(query);
    string word, result;
    while (ss >> word) {
        // Lowercase and strip punctuation
        string cleaned;
        for (char c : word) if (!ispunct(c)) cleaned += tolower(c);

        if (!cleaned.empty() && stop_words.find(cleaned) == stop_words.end()) {
            result += cleaned + " ";
        }
    }
    return result.empty() ? query : result;
}


HybridRetriever::HybridRetriever(const vector<string>& docs, const vector<vector<float>>& embeddings)
    : documents(docs), doc_embeddings(embeddings), bm25_engine(docs) {
}

// Fast constructor for loading persisted data
HybridRetriever::HybridRetriever(const vector<string>& docs, const vector<vector<float>>& embeddings, bool skip_bm25_build)
    : documents(docs), doc_embeddings(embeddings), bm25_engine() {
    (void)skip_bm25_build;
}


double HybridRetriever::fast_cosine_similarity(const vector<float>& v1, const vector<float>& v2) {
    double dot = 0.0, n1 = 0.0, n2 = 0.0;
    // Assume same dimension; caller must ensure embeddings are aligned
    size_t dim = v1.size();
    for (size_t i = 0; i < dim; ++i) {
        dot += v1[i] * v2[i];
        n1 += v1[i] * v1[i];
        n2 += v2[i] * v2[i];
    }
    return dot / (sqrt(n1) * sqrt(n2) + 1e-9);
}

vector<string> HybridRetriever::search(const string& query, int top_k, double alpha) {
    if (documents.empty()) return {};

    int num_docs = static_cast<int>(documents.size());

    // --- 1) Vector search (semantic) ---
    vector<float> q_vec = embed_text(query);
    vector<pair<int, float>> vec_scored(num_docs);

#pragma omp parallel for
    for (int i = 0; i < num_docs; ++i) {
        float sim = (float)fast_cosine_similarity(q_vec, doc_embeddings[i]);
        vec_scored[i] = { i, sim };
    }

    // Pool depth: keep more candidates for RRF merge
    int vec_pool_depth = min(num_docs, 150);
    partial_sort(vec_scored.begin(), vec_scored.begin() + vec_pool_depth, vec_scored.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    // --- 2) BM25 search (keywords) ---
    string semantic_query = clean_query(query);
    auto bm25_results = bm25_engine.search(semantic_query, 100);

    // --- 3) RRF merge ---
    unordered_map<int, double> rrf_scores;
    const int k_rrf = 60;

    // Vector path (semantic weight)
    for (int rank = 0; rank < vec_pool_depth; ++rank) {
        int doc_id = vec_scored[rank].first;
        rrf_scores[doc_id] += alpha * (1.0 / (k_rrf + rank + 1));
    }

    // BM25 path (keyword weight)
    for (int rank = 0; rank < (int)bm25_results.size(); ++rank) {
        int doc_id = bm25_results[rank].first;
        rrf_scores[doc_id] += (1.0 - alpha) * (1.0 / (k_rrf + rank + 1));
    }

    // --- 4) Final ranking ---
    vector<pair<int, double>> final_ranking;
    final_ranking.reserve(rrf_scores.size());
    for (auto const& [id, score] : rrf_scores) {
        final_ranking.push_back({ id, score });
    }

    int final_count = min(top_k, (int)final_ranking.size());
    partial_sort(final_ranking.begin(), final_ranking.begin() + final_count, final_ranking.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    vector<string> results;
    for (int i = 0; i < final_count; ++i) {
        int idx = final_ranking[i].first;
        if (idx >= 0 && idx < (int)documents.size()) {
            results.push_back(documents[idx]);
        }
    }

    return results;
}