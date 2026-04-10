#pragma once
#include <string>
#include <vector>

std::vector<std::string> sqlite_retrieve(const std::string& query, int top_k);