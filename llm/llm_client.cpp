#include "llm_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <windows.h>

using namespace std;
using json = nlohmann::json;

// Helper: MBCS (CP_ACP) -> UTF-8
std::string mbcs_to_utf8(const std::string& input) {
    if (input.empty()) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, input.c_str(), (int)input.size(), nullptr, 0);
    std::wstring wbuf(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), (int)input.size(), &wbuf[0], wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    std::string utf8(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), wlen, &utf8[0], u8len, nullptr, nullptr);
    return utf8;
}

// Helper: read API key from env
std::string load_api_key() {
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, "BIGMODEL_API_KEY") == 0 && buf != nullptr) {
        std::string key(buf);
        free(buf);
        return key;
    }
    return "";
}

// Streaming callback: parse SSE chunks without relying on getline
static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string raw_data((char*)contents, total_size);
    auto* callback_ptr = (std::function<bool(const std::string&)>*)userp;

    // Parse SSE "data: {json}" frames
    size_t pos = 0;
    while ((pos = raw_data.find("data: ", pos)) != std::string::npos) {
        pos += 6; // skip "data: "
        size_t end_pos = raw_data.find("\n", pos);
        std::string json_str = (end_pos == std::string::npos) ?
            raw_data.substr(pos) :
            raw_data.substr(pos, end_pos - pos);

        if (json_str.find("[DONE]") != std::string::npos) break;

        try {
            auto j = json::parse(json_str);
            if (j.contains("choices") && !j["choices"].empty()) {
                auto& delta = j["choices"][0]["delta"];
                if (delta.contains("content")) {
                    std::string token = delta["content"].get<std::string>();
                    // Optional debug: print tokens to console
                    // std::cout << token << std::flush; 

                    if (!(*callback_ptr)(token)) return 0;
                }
            }
        }
        catch (...) {
            // Ignore partial JSON due to chunking
        }

        if (end_pos == std::string::npos) break;
        pos = end_pos + 1;
    }
    return total_size;
}

// API 1: streaming chat
void call_llm_stream(const std::string& prompt, std::function<bool(const std::string&)> on_token) {
    std::string api_key = load_api_key();
    CURL* curl = curl_easy_init();

    if (curl && !api_key.empty()) {
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, (std::string("Authorization: Bearer ") + api_key).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        json body;
        body["model"] = "glm-4";
        body["messages"] = json::array({ {{"role", "user"}, {"content", mbcs_to_utf8(prompt)}} });
        body["stream"] = true;
        body["temperature"] = 0.7;

        std::string s_body = body.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://open.bigmodel.cn/api/paas/v4/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, s_body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);

        // Reduce internal buffering
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
        // 1024 bytes buffer (can hold a full SSE frame in most cases)
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[CURL Error] " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "[Error] CURL init failed or API Key missing!" << std::endl;
    }
}

// API 2: sync chat
std::string call_llm(const std::string& prompt) {
    std::string full_answer = "";
    call_llm_stream(prompt, [&](const std::string& token) {
        full_answer += token;
        return true;
        });
    return full_answer;
}