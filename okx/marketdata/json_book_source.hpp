#pragma once

#include <string>

#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/marketdata/market_data_source.hpp"
#include "okx/marketdata/order_book.hpp"

class JsonBookSource : public MarketDataSource {
public:
    JsonBookSource(asio::io_context& io_context, std::string host, std::string port,
                   std::string path, std::string inst_id);

    void SetOnBookUpdate(BookHandler handler) override;
    void Start() override;
    [[nodiscard]] std::string_view Name() const override;

private:
    void OnMessage(std::string_view message);

    OkxWsClient ws_client_;
    OrderBook book_;
    BookHandler on_book_update_;
    std::string subscribe_msg_;
    std::string unsubscribe_msg_;
    std::string resubscribe_msg_;
};
