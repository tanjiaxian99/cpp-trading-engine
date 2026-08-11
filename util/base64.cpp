#include "util/base64.hpp"

#include <openssl/evp.h>

std::string Base64Encode(const std::vector<unsigned char>& data) {
    // Base64 encoding takes 3 bytes and outputs 4 characters, i.e. ceil(n/3) * 4
    // ceil(a/b) is calculated using (a + b - 1) / b
    std::string encoded(4 * ((data.size() + 2) / 3), '\0');
    const int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), data.data(),
                                    static_cast<int>(data.size()));
    encoded.resize(static_cast<std::size_t>(len));
    return encoded;
}
