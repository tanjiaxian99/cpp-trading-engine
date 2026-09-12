#include "okx/orders/ws_order_round_trip.hpp"

#include <stdexcept>

#include "log/async_logger.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/common/response_utils.hpp"
#include "okx/orders/order_lifecycle.hpp"
#include "okx/orders/rest_orders.hpp"
#include "okx/orders/ws_orders.hpp"
#include "util/json.hpp"

namespace {
constexpr std::string_view kSmokeTestAmendPx = "101";
}  // namespace

WsOrderRoundTrip::WsOrderRoundTrip(OkxWsClient& ws_client, WsResponseDemux& demux,
                                   RestClient& rest_client, const OkxAuth& auth,
                                   const OrderRequest& order_request, OrderStore& order_store)
    : ws_client_(ws_client),
      demux_(demux),
      rest_client_(rest_client),
      auth_(auth),
      order_request_(order_request),
      order_store_(order_store) {}

void WsOrderRoundTrip::Start() {
    if (!ws_client_.IsConnected()) {
        Log.Warn("Private WS is down, falling back to REST for order placement");
        const OrderResult result = PlaceOrder(rest_client_, auth_, order_request_);
        Log.Info("REST fallback order placement: accepted={} ordId={} sCode={} sMsg={}",
                 result.accepted, result.ord_id, result.s_code, result.s_msg);
        if (result.accepted) {
            ord_id_ = result.ord_id;
            FallBackToRestCancel();
        }
        return;
    }

    const WsOrderRequest order = BuildWsOrderMessage(order_request_);
    cl_ord_id_ = order.id;
    order_store_.Add(order.id, order_request_.inst_id, order_request_.side, order_request_.px,
                     order_request_.sz);
    ws_client_.Send(order.message);
    Log.Debug("Sent WS order request id={}", order.id);
    demux_.Track(order.id, [this](std::string_view response) { OnOrderResponse(response); });
}

void WsOrderRoundTrip::ApplyOrderEvent(const OrderEvent& event) {
    Order* order = order_store_.FindByClOrdId(event.cl_ord_id);
    if (!order) {
        return;
    }

    order->ApplyEvent(event);
    Log.Debug("New order state={} after applying event", ToString(order->State()));
    if (IsTerminal(order->State())) {
        order_store_.Remove(order->ClOrdId());
    }
}

Order& WsOrderRoundTrip::GetOrder() {
    Order* order = order_store_.FindByClOrdId(cl_ord_id_);
    if (!order) {
        throw std::runtime_error("WsOrderRoundTrip: order not found in order_store_");
    }
    return *order;
}

void WsOrderRoundTrip::OnOrderResponse(std::string_view response) {
    Log.Debug("WS order response: {}", response);
    if (!IsRequestAccepted(response)) {
        Log.Warn("Order placement rejected, cleanup will be done by reconciliation");
        return;
    }

    const auto data = FindData(response);
    const auto ord_id = data ? json::FindString(*data, kOrdId) : std::nullopt;
    if (!ord_id) {
        return;
    }
    ord_id_ = std::string(*ord_id);

    if (!ws_client_.IsConnected()) {
        FallBackToRestCancel();
        return;
    }

    GetOrder().OnAmendRequested();
    const WsOrderRequest amend = BuildWsAmendOrderMessage(order_request_.inst_id_code, ord_id_,
                                                          kSmokeTestAmendPx, order_request_.sz);
    ws_client_.Send(amend.message);
    Log.Debug("Sent WS amend-order request id={}", amend.id);
    demux_.Track(amend.id, [this](std::string_view response) { OnAmendResponse(response); });
}

void WsOrderRoundTrip::OnAmendResponse(std::string_view response) {
    Log.Debug("WS amend-order response: {}", response);
    if (!IsRequestAccepted(response)) {
        GetOrder().OnRequestRejected();
        Log.Warn("New order state={} upon amend request rejection", ToString(GetOrder().State()));
    }

    if (!ws_client_.IsConnected()) {
        FallBackToRestCancel();
        return;
    }

    GetOrder().OnCancelRequested();
    const WsOrderRequest cancel = BuildWsCancelOrderMessage(order_request_.inst_id_code, ord_id_);
    ws_client_.Send(cancel.message);
    Log.Debug("Sent WS cancel-order request id={}", cancel.id);
    demux_.Track(cancel.id, [this](std::string_view response) { OnCancelResponse(response); });
}

void WsOrderRoundTrip::OnCancelResponse(std::string_view response) {
    Log.Debug("WS cancel-order response: {}", response);
    if (!IsRequestAccepted(response)) {
        GetOrder().OnRequestRejected();
        Log.Warn("New order state={} upon cancel request rejection", ToString(GetOrder().State()));
    }
}

void WsOrderRoundTrip::FallBackToRestCancel() {
    Log.Warn("Private WS is down, falling back to REST for order cancellation");
    const OrderResult result = CancelOrder(rest_client_, auth_, order_request_.inst_id, ord_id_);
    Log.Info("REST fallback order cancellation: accepted={} sCode={} sMsg={}", result.accepted,
             result.s_code, result.s_msg);
}
