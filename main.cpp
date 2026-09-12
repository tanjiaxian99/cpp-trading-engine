#include <cerrno>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#include "config.hpp"
#include "log/async_logger.hpp"
#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/okx_endpoints.hpp"
#include "okx/engine/bootstrap.hpp"
#include "okx/engine/engine.hpp"
#include "perf/tick_to_trade_stats.hpp"
#include "rest/rest_client.hpp"

int main() {
#ifdef __APPLE__
    if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) != 0) {
        Log.Warn("Failed to set thread QoS class: {}", std::strerror(errno));
    }
#endif

    TickToTradeStats tick_to_trade_stats;
    try {
        const Config config = Config::FromEnv();
        Log.Info("Loaded config for key {}", config.api_key);

        RestClient rest_client{std::string(kOkxRestBaseUrl)};
        const OkxAuth auth(config.api_key, config.api_secret, config.passphrase);

        BootstrapResult bootstrap = RunStartupChecks(rest_client, auth);

        Engine engine(config.market_data_mode, rest_client, auth, std::move(bootstrap),
                      tick_to_trade_stats);
        engine.Run();
        return 0;
    } catch (const std::exception& e) {
        Log.Error("Fatal exception causing main to crash: {}", e.what());
        tick_to_trade_stats.LogSummary();
        return 1;
    }
}
