#pragma once
#include <vector>
#include <string>

std::vector<std::string> load_documents(const std::string& path);
std::vector<std::string> split_into_chunks(const std::string& text, size_t chunk_size);