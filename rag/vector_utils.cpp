#include "vector_utils.h"
#include <cmath>

using namespace std;

float cosine_similarity(
	const vector<float>& a,
	const vector<float>& b) {
	
	if (a.size() != b.size() || a.empty()) {
		return 0.0f; // 维度不匹配或空向量相似度定义为0
	}
	float dot_product = 0.0f;
	float norm_a = 0.0f;
	float norm_b = 0.0f;
	for (size_t i = 0; i < a.size(); ++i) {
		dot_product += a[i] * b[i];
		norm_a += a[i] * a[i];
		norm_b += b[i] * b[i];
	}
	if (norm_a == 0.0f || norm_b == 0.0f) {
		return 0.0f; // 避免除以零，定义零向量相似度为0
	}
	return dot_product / (sqrt(norm_a) * sqrt(norm_b));
}