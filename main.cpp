#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "auth.hpp"
#include "config.hpp"
#include "rest_client.hpp"
#include "transport.hpp"

namespace {

// Extracts just the "ts" field from /public/time's known response shape:
// {"code":"0","data":[{"ts":"1786204129995"}],"msg":""}. Deliberately not
// a general JSON parser — A4 introduces the real targeted field scanner
// for arbitrary responses; this is a one-off stopgap scoped to exactly
// this endpoint, not something meant to be reused.
long long ExtractTimestampMs(const std::string& public_time_body) {
    const std::string key = R"("ts":")";
    const auto value_start = public_time_body.find(key);
    if (value_start == std::string::npos) {
        throw std::runtime_error("could not find ts field in /public/time response");
    }
    const auto digits_start = value_start + key.size();
    const auto digits_end = public_time_body.find('"', digits_start);
    if (digits_end == std::string::npos) {
        throw std::runtime_error("malformed ts field in /public/time response");
    }
    return std::stoll(public_time_body.substr(digits_start, digits_end - digits_start));
}

}  // namespace

int main() {
    try {
        const Config config = Config::FromEnv();
        std::cout << "loaded config for key " << config.api_key << "\n";

        RestClient rest_client("https://www.okx.com");

        // Public, unauthenticated endpoint — also used below for the clock
        // drift check A3 requires before signed requests can be trusted.
        const Response time_response = rest_client.Get("/api/v5/public/time");
        std::cout << "REST status " << time_response.status_code << ": " << time_response.body
                  << "\n";

        const long long server_time_ms = ExtractTimestampMs(time_response.body);
        const auto local_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
        std::cout << "clock drift: " << (local_time_ms - server_time_ms) << " ms\n";

        // Signed request — proves the A3 auth/signing path end-to-end.
        const OkxAuth auth(config.api_key, config.api_secret, config.passphrase);
        const std::string balance_path = "/api/v5/account/balance";
        const Response balance_response =
            rest_client.Get(balance_path, auth.SignHeaders("GET", balance_path));
        std::cout << "balance status " << balance_response.status_code << ": "
                  << balance_response.body << "\n";

        Transport transport("wspap.okx.com", "8443");
        transport.Connect();
        std::cout << "TLS connected to wspap.okx.com:8443\n";

        // No WebSocket upgrade request has been sent yet (that's B1), so
        // OKX has nothing to proactively respond to. This read only proves
        // the transport layer works — it may block for a while before the
        // far end drops the connection. That's expected here, not a bug.
        std::array<char, 4096> buf{};
        const std::size_t n = transport.ReadSome(asio::buffer(buf));
        std::cout << "read " << n << " bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
