#pragma once

#include <string_view>

#include "okx/auth.hpp"
#include "okx/order_store.hpp"
#include "rest/rest_client.hpp"

class KillSwitch {
public:
    KillSwitch(RestClient& rest_client, const OkxAuth& auth, OrderStore& order_store);

    void Trigger(std::string_view reason);

    [[nodiscard]] bool IsTriggered() const {
        return triggered_;
    }

private:
    RestClient& rest_client_;
    const OkxAuth& auth_;
    OrderStore& order_store_;
    bool triggered_ = false;
};
