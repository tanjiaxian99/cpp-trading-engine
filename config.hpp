#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "log/async_logger.hpp"

enum class MarketDataMode : std::uint8_t {
    kJsonBook,
    kSbeBbo,
};

struct Config {
    std::string api_key;
    std::string api_secret;
    std::string passphrase;
    MarketDataMode market_data_mode = MarketDataMode::kJsonBook;

    static Config FromEnv() {
        Config config;
        config.api_key = ReadRequired(kApiKeyEnvVar);
        config.api_secret = ReadRequired(kApiSecretEnvVar);
        config.passphrase = ReadRequired(kPassphraseEnvVar);
        config.market_data_mode = ParseMarketDataMode(ReadRequired(kMarketDataModeEnvVar));
        return config;
    }

private:
    static constexpr auto kApiKeyEnvVar = "OKX_API_KEY";
    static constexpr auto kApiSecretEnvVar = "OKX_API_SECRET";
    static constexpr auto kPassphraseEnvVar = "OKX_PASSPHRASE";
    static constexpr auto kMarketDataModeEnvVar = "OKX_MARKET_DATA_MODE";
    static constexpr std::string_view kJsonMode = "json";
    static constexpr std::string_view kSbeMode = "sbe";

    static MarketDataMode ParseMarketDataMode(std::string_view value) {
        if (value == kJsonMode) {
            return MarketDataMode::kJsonBook;
        }
        if (value == kSbeMode) {
            return MarketDataMode::kSbeBbo;
        }
        Log.Error("{} must be '{}' or '{}' but got '{}'", kMarketDataModeEnvVar, kJsonMode,
                  kSbeMode, value);
        std::exit(1);
    }

    static std::string ReadRequired(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            Log.Error("Missing required environment variable: {}", name);
            std::exit(1);
        }
        return {value};
    }
};
