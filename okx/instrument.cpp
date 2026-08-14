#include "okx/instrument.hpp"

#include <format>
#include <stdexcept>

#include "okx/okx_constants.hpp"
#include "util/json.hpp"

constexpr std::string_view kInstrumentsPathFormat =
    "/api/v5/public/instruments?instType=SPOT&instId={}";

InstrumentSpec FetchInstrumentSpec(RestClient& rest_client, std::string_view inst_id) {
    const std::string path = std::format(kInstrumentsPathFormat, inst_id);
    const HttpResponse response = rest_client.Get(path);

    const auto data = json::FindArrayElement(response.body, kData, 0);
    if (!data) {
        throw std::runtime_error(std::format("Instrument not found: {}", inst_id));
    }
    const std::string_view element = *data;

    const auto tick_sz = json::FindString(element, kTickSz);
    const auto lot_sz = json::FindString(element, kLotSz);
    const auto min_sz = json::FindString(element, kMinSz);
    if (!tick_sz || !lot_sz || !min_sz) {
        throw std::runtime_error(std::format("Malformed instrument spec response for {}", inst_id));
    }

    return InstrumentSpec{
        .inst_id = std::string(inst_id),
        .tick_sz = std::string(*tick_sz),
        .lot_sz = std::string(*lot_sz),
        .min_sz = std::string(*min_sz),
    };
}
