#pragma once

#include <string>
#include <string_view>

#include "rest/rest_client.hpp"

struct InstrumentSpec {
    std::string inst_id;
    std::string tick_sz;
    std::string lot_sz;
    std::string min_sz;
};

InstrumentSpec FetchInstrumentSpec(RestClient& rest_client, std::string_view inst_id);
