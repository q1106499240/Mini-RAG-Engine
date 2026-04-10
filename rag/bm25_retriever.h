#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace std;

struct Posting {
    int doc_id;
    int tf;
};

class BM25Retriever {
public:
    // Two constructors: build or empty (for deserialize)
    BM25Retriever(const vector<string>& docs);
    BM25Retriever() : num_docs(0), avg_doc_len(0.0) {} // used for loading

    // Persistence
    void serialize(ostream& os) const;
    void deserialize(istream& is);

    vector<pair<int, double>> search(const string& query, int top_k);

private:
    int num_docs;
    double avg_doc_len;
    const double k1 = 1.5;
    const double b = 0.75;

    unordered_map<string, vector<Posting>> inverted_index;
    unordered_map<string, double> idf_table;
    vector<int> doc_lens;

    vector<string> tokenize(const string& text);
};