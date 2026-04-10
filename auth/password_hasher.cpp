#include "password_hasher.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

using namespace std;

namespace {
const int kIterations = 100000;
const int kSaltBytes = 16;
const int kHashBytes = 32; // SHA256

string to_hex(const vector<unsigned char>& data) {
    stringstream ss;
    ss << hex << setfill('0');
    for (unsigned char b : data) ss << setw(2) << static_cast<int>(b);
    return ss.str();
}

bool from_hex(const string& hex_str, vector<unsigned char>& out) {
    if (hex_str.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex_str.size() / 2);
    for (size_t i = 0; i < hex_str.size(); i += 2) {
        unsigned int x = 0;
        string byte_str = hex_str.substr(i, 2);
        stringstream ss;
        ss << hex << byte_str;
        if (!(ss >> x)) return false;
        out.push_back(static_cast<unsigned char>(x));
    }
    return true;
}

bool pbkdf2_sha256(
    const string& password,
    const vector<unsigned char>& salt,
    int iterations,
    vector<unsigned char>& derived
) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0) return false;

    derived.assign(kHashBytes, 0);
    status = BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        static_cast<ULONGLONG>(iterations),
        derived.data(),
        static_cast<ULONG>(derived.size()),
        0
    );

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return status >= 0;
}
}

namespace auth {

string hash_password(const string& plain_password) {
    vector<unsigned char> salt(kSaltBytes, 0);
    if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return "";
    }

    vector<unsigned char> derived;
    if (!pbkdf2_sha256(plain_password, salt, kIterations, derived)) return "";

    return string("pbkdf2$") + to_string(kIterations) + "$" + to_hex(salt) + "$" + to_hex(derived);
}

bool verify_password(const string& plain_password, const string& stored_hash) {
    const string prefix = "pbkdf2$";
    if (stored_hash.rfind(prefix, 0) != 0) return false;

    size_t p1 = stored_hash.find('$', prefix.size());
    if (p1 == string::npos) return false;
    size_t p2 = stored_hash.find('$', p1 + 1);
    if (p2 == string::npos) return false;

    int iterations = 0;
    try {
        iterations = stoi(stored_hash.substr(prefix.size(), p1 - prefix.size()));
    } catch (...) {
        return false;
    }
    if (iterations <= 0) return false;

    vector<unsigned char> salt;
    vector<unsigned char> expected;
    if (!from_hex(stored_hash.substr(p1 + 1, p2 - p1 - 1), salt)) return false;
    if (!from_hex(stored_hash.substr(p2 + 1), expected)) return false;

    vector<unsigned char> actual;
    if (!pbkdf2_sha256(plain_password, salt, iterations, actual)) return false;
    if (actual.size() != expected.size()) return false;

    unsigned char diff = 0;
    for (size_t i = 0; i < actual.size(); ++i) diff |= (actual[i] ^ expected[i]);
    return diff == 0;
}

} // namespace auth

