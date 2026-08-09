#pragma once

#include <string>
#include <string_view>
#include <vector>

// ISO 8601 UTC timestamp with millisecond precision and a "Z" suffix, e.g.
// "2026-08-05T09:08:57.715Z"
std::string IsoTimestampNow();
std::string HmacSha256Base64(std::string_view message, std::string_view secret);

class OkxAuth {
public:
    OkxAuth(std::string api_key, std::string api_secret, std::string passphrase);

    [[nodiscard]] std::vector<std::string> SignHeaders(std::string_view method,
                                                       std::string_view request_path,
                                                       std::string_view body = "") const;

private:
    std::string api_key_;
    std::string api_secret_;
    std::string passphrase_;
};
