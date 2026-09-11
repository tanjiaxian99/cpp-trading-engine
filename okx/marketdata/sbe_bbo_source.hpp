#pragma once

#include <optional>
#include <string>

#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/marketdata/market_data_source.hpp"
#include "okx/marketdata/order_book.hpp"

class SbeBboSource : public MarketDataSource {
public:
    SbeBboSource(asio::io_context& io_context, std::string host, std::string port, std::string path,
                 long long inst_id_code, std::optional<OkxAuth> auth);

    void SetOnBookUpdate(BookHandler handler) override;
    void Start() override;
    [[nodiscard]] std::string_view Name() const override;

private:
    void OnMessage(std::string_view message);

    OkxWsClient ws_client_;
    OrderBook book_;
    BookHandler on_book_update_;
    std::string subscribe_msg_;
    std::int64_t last_seq_id_ = -1;
};
