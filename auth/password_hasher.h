#pragma once
#include <string>

namespace auth {

// Hash format: pbkdf2$<iterations>$<salt_hex>$<hash_hex>
std::string hash_password(const std::string& plain_password);
bool verify_password(const std::string& plain_password, const std::string& stored_hash);

} // namespace auth

