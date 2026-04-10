#pragma once
#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "../include/httplib.h"

namespace auth {

// Extract token from Authorization: Bearer <token> or query param ?token=...
std::optional<std::string> extract_token(const httplib::Request& req);

// Return { user_name, role_codes, permissions } for the session token.
std::optional<nlohmann::json> get_me_from_token(const std::string& token);

// Permission guard helper. On failure, writes response and returns false.
bool require_permission(
    const httplib::Request& req,
    httplib::Response& res,
    const std::string& permission_code
);

} // namespace auth

