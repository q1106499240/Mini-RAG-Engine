#include "embedder.h"
#include <curl/curl.h> // requires libcurl
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>

using json = nlohmann::json;
using namespace std;

// libcurl callback: collect response body
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

vector<float> embed_text(const string& text) {
    CURL* curl = curl_easy_init();
    string readBuffer;
    vector<float> embedding;

    if (curl) {
        // Build JSON request
        json j;
        j["text"] = text;
        string json_data = j.dump();

        // Request config
        curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:5000/embed");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

        // Headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Response callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Execute
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            auto res_j = json::parse(readBuffer);
            embedding = res_j["embedding"].get<vector<float>>();
        }
        else {
            cerr << "[ERROR] GPU Server connection failed: " << curl_easy_strerror(res) << endl;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return embedding;
}