#pragma once

#include <string>
#include <string_view>

#include "okx/auth.hpp"
#include "rest/rest_client.hpp"

struct InstrumentSpec {
    std::string inst_id;
    std::string tick_sz;
    std::string lot_sz;
    std::string min_sz;
};

InstrumentSpec FetchInstrumentSpec(RestClient& rest_client, std::string_view inst_id);
long long FetchInstIdCode(RestClient& rest_client, const OkxAuth& auth, std::string_view inst_id);
