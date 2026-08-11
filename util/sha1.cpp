#include "util/sha1.hpp"

#include <openssl/evp.h>

#include <stdexcept>

std::vector<unsigned char> Sha1(std::string_view message) {
    std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
    unsigned int digest_len = 0;

    const auto* data = reinterpret_cast<const unsigned char*>(message.data());
    const bool ok =
        EVP_Digest(data, message.size(), digest.data(), &digest_len, EVP_sha1(), nullptr) == 1;
    if (!ok) {
        throw std::runtime_error("SHA-1 computation failed");
    }

    digest.resize(digest_len);
    return digest;
}
