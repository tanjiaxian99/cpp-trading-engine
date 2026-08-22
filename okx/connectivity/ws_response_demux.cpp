#include "okx/connectivity/ws_response_demux.hpp"

#include <utility>

#include "okx/common/okx_constants.hpp"
#include "util/json.hpp"

void WsResponseDemux::Track(std::string id, ResponseHandler handler) {
    pending_.emplace(std::move(id), std::move(handler));
}

bool WsResponseDemux::Dispatch(std::string_view message) {
    const auto id = json::FindString(message, kId);
    if (!id) {
        return false;
    }

    const auto it = pending_.find(std::string(*id));
    if (it == pending_.end()) {
        return false;
    }

    const ResponseHandler handler = std::move(it->second);
    pending_.erase(it);
    handler(message);
    return true;
}
