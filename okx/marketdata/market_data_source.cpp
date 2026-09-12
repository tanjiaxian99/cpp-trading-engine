#include "okx/marketdata/market_data_source.hpp"

#include <format>
#include <stdexcept>
#include <string>

#include "okx/common/instruments.hpp"
#include "okx/connectivity/okx_endpoints.hpp"
#include "okx/marketdata/json_book_source.hpp"
#include "okx/marketdata/sbe_bbo_source.hpp"

std::unique_ptr<MarketDataSource> MakeMarketDataSource(MarketDataMode mode,
                                                       asio::io_context& io_context,
                                                       const OkxAuth& auth,
                                                       long long inst_id_code) {
    switch (mode) {
        case MarketDataMode::kJsonBook:
            return std::make_unique<JsonBookSource>(
                io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                std::string(kPublicWsPath), std::string(kEthUsdt));
        case MarketDataMode::kSbeBbo:
            return std::make_unique<SbeBboSource>(io_context, std::string(kOkxWsHost),
                                                  std::string(kOkxWsPort), std::string(kSbeWsPath),
                                                  inst_id_code, auth);
    }
    throw std::runtime_error(std::format("Unrecognized MarketDataMode"));
}
