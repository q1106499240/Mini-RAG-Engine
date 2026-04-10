#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>
#include <functional>

// Helpers
std::string mbcs_to_utf8(const std::string& input);
std::string load_api_key();

// LLM APIs
std::string call_llm(const std::string& prompt);
void call_llm_stream(const std::string& prompt, std::function<bool(const std::string&)> on_token);

#endif