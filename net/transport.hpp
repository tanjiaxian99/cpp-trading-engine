#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace asio = boost::asio;

class Transport {
public:
    Transport(asio::io_context& io_context, std::string host, std::string port);
    void Connect();
    std::size_t ReadSome(asio::mutable_buffer buffer);
    void Write(std::string_view data);
    void AsyncReadSome(asio::mutable_buffer buffer,
                       std::function<void(const boost::system::error_code&, std::size_t)> handler);
    void AsyncWrite(asio::const_buffer buffer,
                    std::function<void(const boost::system::error_code&, std::size_t)> handler);

private:
    asio::io_context& io_context_;
    std::string host_;
    std::string port_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> stream_;
};
