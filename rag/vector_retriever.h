// 向量检索器

#pragma once
#include <vector>
#include <string>


struct VecChunk
{
    std::string text;
    std::vector<float> embedding;
};

// 构建向量索引（只执行一次）
void build_index(const std::vector<std::string>& docs);

std::string vector_retrieve(
    const std::string& query,
    int topk);

void save_index(const std::string& path);

bool load_index(const std::string& path);

std::vector<std::string> vector_retrieve_list(const std::string& query, int top_k);
