#include "instrument.hpp"

#include <format>
#include <stdexcept>

#include "json.hpp"

constexpr std::string_view kInstrumentsPathFormat =
    "/api/v5/public/instruments?instType=SPOT&instId={}";

constexpr std::string_view kData = "data";
constexpr std::string_view kTickSz = "tickSz";
constexpr std::string_view kLotSz = "lotSz";
constexpr std::string_view kMinSz = "minSz";

InstrumentSpec FetchInstrumentSpec(RestClient& rest_client, std::string_view inst_id) {
    const std::string path = std::format(kInstrumentsPathFormat, inst_id);
    const HttpResponse response = rest_client.Get(path);

    const auto maybe_data = json::FindArrayElement(response.body, kData, 0);
    if (!maybe_data) {
        throw std::runtime_error(std::format("instrument not found: {}", inst_id));
    }
    const std::string_view data = *maybe_data;

    const auto tick_sz = json::FindString(data, kTickSz);
    const auto lot_sz = json::FindString(data, kLotSz);
    const auto min_sz = json::FindString(data, kMinSz);
    if (!tick_sz || !lot_sz || !min_sz) {
        throw std::runtime_error(std::format("malformed instrument spec response for {}", inst_id));
    }

    return InstrumentSpec{
        .inst_id = std::string(inst_id),
        .tick_sz = std::string(*tick_sz),
        .lot_sz = std::string(*lot_sz),
        .min_sz = std::string(*min_sz),
    };
}
