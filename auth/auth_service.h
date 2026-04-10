#pragma once
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace auth {

bool init_auth_tables();

// Register user into DB (password will be stored as hash).
// Returns {username} on success.
std::optional<nlohmann::json> register_user(
    const std::string& username,
    const std::string& password,
    const std::string& email
);

// Returns {username, token} on success; nullopt on auth failure.
std::optional<nlohmann::json> login(const std::string& username, const std::string& password);

// Returns username if token is valid.
std::optional<std::string> validate_session(const std::string& token);

bool logout(const std::string& token);

} // namespace auth

