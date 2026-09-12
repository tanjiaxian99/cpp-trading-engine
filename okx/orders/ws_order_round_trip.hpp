#pragma once

#include <string>

#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/connectivity/ws_response_demux.hpp"
#include "okx/orders/order_events.hpp"
#include "okx/orders/order_store.hpp"
#include "okx/orders/order_types.hpp"
#include "rest/rest_client.hpp"

class WsOrderRoundTrip {
public:
    WsOrderRoundTrip(OkxWsClient& ws_client, WsResponseDemux& demux, RestClient& rest_client,
                     const OkxAuth& auth, const OrderRequest& order_request,
                     OrderStore& order_store);

    void Start();
    void ApplyOrderEvent(const OrderEvent& event);

private:
    Order& GetOrder();
    void OnOrderResponse(std::string_view response);
    void OnAmendResponse(std::string_view response);
    void OnCancelResponse(std::string_view response);
    void FallBackToRestCancel();

    OkxWsClient& ws_client_;
    WsResponseDemux& demux_;
    RestClient& rest_client_;
    const OkxAuth& auth_;
    const OrderRequest& order_request_;
    std::string ord_id_;
    std::string cl_ord_id_;
    OrderStore& order_store_;
};
