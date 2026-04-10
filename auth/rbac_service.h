#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace auth {

struct RbacContext {
    int user_id = -1;
    std::string user_name;
    std::vector<std::string> role_codes;
    std::vector<std::string> permissions;
};

// Ensure RBAC tables exist (non-destructive).
bool init_rbac_tables();
bool ensure_rbac_seed_data();
bool assign_default_role_for_user(int user_id, const std::string& role_code = "NORMAL_USER");

// Load roles + permissions for a user_id.
std::optional<RbacContext> load_rbac_for_user(int user_id, const std::string& user_name);
std::vector<nlohmann::json> list_roles();
std::vector<nlohmann::json> list_users_with_roles();
bool set_user_role(int user_id, const std::string& role_code);

} // namespace auth

