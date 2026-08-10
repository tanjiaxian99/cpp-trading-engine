#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <string>

namespace asio = boost::asio;

class Transport {
public:
    Transport(std::string host, std::string port);
    void Connect();
    std::size_t ReadSome(asio::mutable_buffer buffer);

private:
    std::string host_;
    std::string port_;
    asio::io_context io_context_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> stream_;
};
