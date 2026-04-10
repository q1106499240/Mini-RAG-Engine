#pragma once
#include <vector>
#include <string>

std::string retrieve(
    const std::vector<std::string>& docs,
    const std::string& query,
    int topk = 3);
