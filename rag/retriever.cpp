// Legacy keyword "contains" retriever (kept for fallback/tests).


#include "retriever.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cmath>



using namespace std;



static string to_lower(const string& s) {
	string out = s;
	transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return tolower(c); });
	return out;
}

// Split query into tokens

vector<string> split_query(const string& query) {
	vector<string> tokens;
	stringstream ss(query);
	string word;
	string current;
//	for (unsigned char c : query) {
//		if (isspace(c)) {
//			if (!current.empty()) {
//				tokens.push_back(current);
//				current.clear();
//			}
//		} else {
//			current += c;
//		}
//	}
//	if (!current.empty()) {
//		tokens.push_back(current);
//	}

	while (ss >> word)
	{
		tokens.push_back(word);
	}
	return tokens;
}

int count_occurrences(const string& text, const string& query) {

	string text_lc = to_lower(text);
	string query_lc = to_lower(query);

	int count = 0;
	size_t pos = 0;
	while ((pos = text_lc.find(query_lc, pos)) != string::npos) {
		++count;
		pos += query_lc.length();
	}
	return count;
}

// Document frequency (DF)
unordered_map<string, int> build_doc_freq(const vector<string>& docs) {
	unordered_map<string, int> df;
	for (const auto& doc : docs) {
		unordered_map<string, bool> seen; // per-document "seen" set
		stringstream ss(doc);
		string word;
		while (ss >> word) {
			word = to_lower(word);
			if (!seen[word]) {
				df[word]++;
				seen[word] = true;
			}
		}
	}
	return df;
}

// ???? IDF
double compute_idf(const string& term, const unordered_map<string, int>& df, int total_docs) {
	auto it = df.find(term);
	int doc_count = (it != df.end()) ? it->second : 0;
	return log((total_docs + 1.0) / (doc_count + 1.0)) + 1.0; // ??1???
}

string retrieve(
	const vector<string>& docs,
	const string& query, int topk) {

	vector<pair<string, int>> scored_docs;

	auto df = build_doc_freq(docs);
	int total_docs = docs.size();

	// ????????????????
	vector<string> keywords = split_query(query);

	for (const auto& doc : docs) {
		double score = 0.0;
		for (const auto& kw : keywords) {
			int tf = count_occurrences(to_lower(doc), to_lower(kw));
			score += tf * compute_idf(kw,df,total_docs);
		}
		scored_docs.emplace_back(doc, score);
	}

	sort(scored_docs.begin(), scored_docs.end(),
		[](const pair<string, int>& a, const pair<string, int>& b) {
			return b.second < a.second; // ????????
		});

	cout << "\n--- TF-IDF Ranking ---" << endl;

	for (const auto& item : scored_docs) {
		cout << "Doc: " << item.first << " Score: " << item.second << endl;
	}

	cout << "--------------------------\n" << endl;


	int k = min(topk, static_cast<int>(scored_docs.size()));
	string q = to_lower(query);
	string result = "";

	for (int i = 0; i < k; ++i) {
		if (scored_docs[i].second > 0) {
			//cout << "Doc: " << scored_docs[i].first << " Score: " << scored_docs[i].second << endl;
			result += scored_docs[i].first + "\n";
		}
	}

	
		
//	for (const auto& doc : docs) {
//
//		string d = to_lower(doc);
//
//		if (doc.find(query) != string::npos) {
//			result += doc + "\n";
//		}
//	}
	if (result.empty()) {
		return "No relevant document found.";
	}
	return result;
}