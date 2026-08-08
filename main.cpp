#include <array>
#include <exception>
#include <iostream>

#include "config.hpp"
#include "transport.hpp"

int main() {
    try {
        const Config config = Config::FromEnv();
        std::cout << "loaded config for key " << config.api_key << "\n";

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
