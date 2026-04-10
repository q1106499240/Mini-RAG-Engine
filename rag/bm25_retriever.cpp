#include "bm25_retriever.h"
#include <sstream>
#include <cctype>
#include <iostream>

BM25Retriever::BM25Retriever(const vector<string>& docs) {
    num_docs = (int)docs.size();
    if (num_docs == 0) return;

    cout << "[BM25] Building inverted index for " << num_docs << " docs..." << endl;

    doc_lens.resize(num_docs);
    long long total_len = 0;

    // Build an inverted index (single-threaded)
    for (int i = 0; i < num_docs; ++i) {
        auto tokens = tokenize(docs[i]);
        doc_lens[i] = (int)tokens.size();
        total_len += doc_lens[i];

        unordered_map<string, int> temp_tf;
        for (const auto& token : tokens) {
            temp_tf[token]++;
        }
        for (const auto& kv : temp_tf) {
            inverted_index[kv.first].push_back({ i, kv.second });
        }

        // Progress log for large corpora
        if (i % 50000 == 0) cout << "  Processed " << i << " docs..." << endl;
    }

    avg_doc_len = (double)total_len / num_docs;

    // Precompute IDF
    for (auto& kv : inverted_index) {
        int df = (int)kv.second.size();
        double idf = log((num_docs - df + 0.5) / (df + 0.5) + 1.0);
        idf_table[kv.first] = max(0.0, idf);
    }
    cout << "[BM25] Inverted index ready." << endl;
}

vector<string> BM25Retriever::tokenize(const string& text) {
    vector<string> tokens;
    string normalized;

    // Lowercase and strip punctuation
    for (char c : text) {
        if (ispunct(static_cast<unsigned char>(c))) {
            normalized += ' ';
        }
        else {
            normalized += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
    }

    stringstream ss(normalized);
    string word;
    while (ss >> word) {
        tokens.push_back(word);
    }
    return tokens;
}

vector<pair<int, double>> BM25Retriever::search(const string& query, int top_k) {
    auto q_tokens = tokenize(query);

    // Accumulate scores only for docs that contain query terms
    unordered_map<int, double> doc_scores;

    for (const auto& term : q_tokens) {
        if (inverted_index.find(term) == inverted_index.end()) continue;

        double idf = idf_table[term];
        const auto& postings = inverted_index[term];

        // Iterate only postings for this term
        for (const auto& p : postings) {
            double tf = p.tf;
            double len_norm = 1.0 - b + b * (static_cast<double>(doc_lens[p.doc_id]) / avg_doc_len);
            double score = idf * (tf * (k1 + 1.0)) / (tf + k1 * len_norm);

            doc_scores[p.doc_id] += score;
        }
    }

    // Convert to vector for sorting
    vector<pair<int, double>> results(doc_scores.begin(), doc_scores.end());

    // partial_sort is faster when top_k << results.size()
    int actual_k = min(static_cast<int>(results.size()), top_k);
    partial_sort(results.begin(), results.begin() + actual_k, results.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    if (results.size() > (size_t)actual_k) {
        results.resize(actual_k);
    }

    return results;
}

void BM25Retriever::serialize(ostream& os) const {
    // 1) metadata
    os.write(reinterpret_cast<const char*>(&num_docs), sizeof(num_docs));
    os.write(reinterpret_cast<const char*>(&avg_doc_len), sizeof(avg_doc_len));

    // 2) doc_lens
    size_t dl_size = doc_lens.size();
    os.write(reinterpret_cast<const char*>(&dl_size), sizeof(dl_size));
    os.write(reinterpret_cast<const char*>(doc_lens.data()), dl_size * sizeof(int));

    // 3) idf_table
    size_t idf_size = idf_table.size();
    os.write(reinterpret_cast<const char*>(&idf_size), sizeof(idf_size));
    for (const auto& kv : idf_table) {
        size_t key_len = kv.first.size();
        os.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        os.write(kv.first.data(), key_len);
        os.write(reinterpret_cast<const char*>(&kv.second), sizeof(kv.second));
    }

    // 4. inverted_index
    size_t ii_size = inverted_index.size();
    os.write(reinterpret_cast<const char*>(&ii_size), sizeof(ii_size));
    for (const auto& kv : inverted_index) {
        size_t key_len = kv.first.size();
        os.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        os.write(kv.first.data(), key_len);

        size_t post_size = kv.second.size();
        os.write(reinterpret_cast<const char*>(&post_size), sizeof(post_size));
        os.write(reinterpret_cast<const char*>(kv.second.data()), post_size * sizeof(Posting));
    }
}

void BM25Retriever::deserialize(istream& is) {
    // 1) metadata
    is.read(reinterpret_cast<char*>(&num_docs), sizeof(num_docs));
    is.read(reinterpret_cast<char*>(&avg_doc_len), sizeof(avg_doc_len));

    // 2. doc_lens
    size_t dl_size;
    is.read(reinterpret_cast<char*>(&dl_size), sizeof(dl_size));
    doc_lens.resize(dl_size);
    is.read(reinterpret_cast<char*>(doc_lens.data()), dl_size * sizeof(int));

    // 3. idf_table
    size_t idf_size;
    is.read(reinterpret_cast<char*>(&idf_size), sizeof(idf_size));
    idf_table.clear();
    for (size_t i = 0; i < idf_size; ++i) {
        size_t key_len;
        is.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        string key(key_len, ' ');
        is.read(&key[0], key_len);
        double val;
        is.read(reinterpret_cast<char*>(&val), sizeof(val));
        idf_table[key] = val;
    }

    // 4. inverted_index
    size_t ii_size;
    is.read(reinterpret_cast<char*>(&ii_size), sizeof(ii_size));
    inverted_index.clear();
    for (size_t i = 0; i < ii_size; ++i) {
        size_t key_len;
        is.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        string key(key_len, ' ');
        is.read(&key[0], key_len);

        size_t post_size;
        is.read(reinterpret_cast<char*>(&post_size), sizeof(post_size));
        vector<Posting> posts(post_size);
        is.read(reinterpret_cast<char*>(posts.data()), post_size * sizeof(Posting));
        inverted_index[key] = move(posts);
    }
}