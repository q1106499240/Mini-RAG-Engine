#include "embedder.h"
#include <vector>
#include <string>

using namespace std;

// Simple placeholder embedding function. Replace this with a real
// embedding call to an LLM or embedding service in production.
vector<float> embed_text(const string& text) {
	vector<float> vec(16, 0.0f);
	for (size_t i = 0; i < text.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		vec[i % vec.size()] += static_cast<float>(c) / 255.0f;
	}
	return vec;
}
