#include "okx/auth.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <array>
#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

#include "util/base64.hpp"

constexpr std::string_view kIsoTimestampFormat = "{:%Y-%m-%dT%H:%M:%S}Z";

std::string IsoTimestampNow() {
    // floor to millisecond precision first — %S then prints the truncated
    // sub-second remainder (".123") as part of the formatted string,
    // giving exactly the millisecond precision OKX's signing requires.
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return std::format(kIsoTimestampFormat, now);
}

namespace {
constexpr const char* kHmacAlgorithm = "HMAC";

// OSSL_PARAM_construct_utf8_string wants a non-const buffer
char sha256_digest_name[] = "SHA256";  // NOLINT(modernize-avoid-c-arrays)

std::vector<unsigned char> HmacSha256(std::string_view message, std::string_view secret) {
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, kHmacAlgorithm, nullptr);
    if (mac == nullptr) {
        throw std::runtime_error("failed to fetch HMAC implementation");
    }

    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);  // ctx has its own reference to mac so we can release mac
    if (ctx == nullptr) {
        throw std::runtime_error("failed to create HMAC context");
    }

    std::array params = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, sha256_digest_name, 0),
        OSSL_PARAM_construct_end(),
    };

    std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
    std::size_t digest_len = 0;

    const auto* key = reinterpret_cast<const unsigned char*>(secret.data());
    const auto* data = reinterpret_cast<const unsigned char*>(message.data());
    const bool ok = EVP_MAC_init(ctx, key, secret.size(), params.data()) == 1 &&
                    EVP_MAC_update(ctx, data, message.size()) == 1 &&
                    EVP_MAC_final(ctx, digest.data(), &digest_len, digest.size()) == 1;

    EVP_MAC_CTX_free(ctx);

    if (!ok) {
        throw std::runtime_error("HMAC-SHA256 computation failed");
    }
    digest.resize(digest_len);
    return digest;
}

constexpr std::array<std::string_view, 2> kHttpMethodNames = {"GET", "POST"};

std::string_view ToString(HttpMethod method) {
    return kHttpMethodNames.at(static_cast<std::size_t>(method));
}
}  // namespace

std::string HmacSha256Base64(std::string_view message, std::string_view secret) {
    return Base64Encode(HmacSha256(message, secret));
}

OkxAuth::OkxAuth(std::string api_key, std::string api_secret, std::string passphrase)
    : api_key_(std::move(api_key)),
      api_secret_(std::move(api_secret)),
      passphrase_(std::move(passphrase)) {}

std::vector<std::string> OkxAuth::SignHeaders(HttpMethod method, std::string_view request_path,
                                              std::string_view body) const {
    const std::string timestamp = IsoTimestampNow();

    const std::string prehash =
        timestamp + std::string(ToString(method)) + std::string(request_path) + std::string(body);
    const std::string signature = HmacSha256Base64(prehash, api_secret_);

    return {
        "OK-ACCESS-KEY: " + api_key_,        "OK-ACCESS-SIGN: " + signature,
        "OK-ACCESS-TIMESTAMP: " + timestamp, "OK-ACCESS-PASSPHRASE: " + passphrase_,
        "Content-Type: application/json",    "x-simulated-trading: 1",
    };
}
