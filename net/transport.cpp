#include "net/transport.hpp"

#include <openssl/ssl.h>

#include <stdexcept>
#include <utility>

Transport::Transport(std::string host, std::string port)
    : host_(std::move(host)),
      port_(std::move(port)),
      ssl_context_(asio::ssl::context::tlsv12_client),
      // The stream wraps a plain tcp::socket with a TLS layer on top.
      stream_(io_context_, ssl_context_) {
    // Loads the OS's trusted root CA bundle so that certificate verification
    // has something to check the server's cert chain against.
    ssl_context_.set_default_verify_paths();
}

void Transport::Connect() {
    // A single hostname can resolve to several IPs.
    asio::ip::tcp::resolver resolver(io_context_);
    const auto endpoints = resolver.resolve(host_, port_);

    // next_layer() reaches through the TLS wrapper to the raw tcp::socket
    // underneath and tries each endpoint in turn until one succeeds.
    asio::connect(stream_.next_layer(), endpoints);

    // Disable Nagle's algorithm to avoid buffering small writes.
    stream_.next_layer().set_option(asio::ip::tcp::no_delay(true));

    // SNI is used to tell the TLS server which hostname we're asking for.
    if (SSL_set_tlsext_host_name(stream_.native_handle(), host_.c_str()) == 0) {
        throw std::runtime_error("failed to set SNI hostname");
    }

    // Verify certificate-chain up to the trusted CAs loaded by set_default_verify_paths().
    stream_.set_verify_mode(asio::ssl::verify_peer);

    // Verify that the certificate is for the correct hostname.
    stream_.set_verify_callback(asio::ssl::host_name_verification(host_));

    stream_.handshake(asio::ssl::stream_base::client);
}

std::size_t Transport::ReadSome(asio::mutable_buffer buffer) {
    return stream_.read_some(buffer);
}

void Transport::Write(std::string_view data) {
    asio::write(stream_, asio::buffer(data));
}
