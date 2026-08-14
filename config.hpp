#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

struct Config {
    std::string api_key;
    std::string api_secret;
    std::string passphrase;

    static Config FromEnv() {
        Config config;
        config.api_key = ReadRequired(kApiKeyEnvVar);
        config.api_secret = ReadRequired(kApiSecretEnvVar);
        config.passphrase = ReadRequired(kPassphraseEnvVar);
        return config;
    }

private:
    static constexpr const char* kApiKeyEnvVar = "OKX_API_KEY";
    static constexpr const char* kApiSecretEnvVar = "OKX_API_SECRET";
    static constexpr const char* kPassphraseEnvVar = "OKX_PASSPHRASE";

    static std::string ReadRequired(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            std::cerr << "Missing required environment variable: " << name << "\n";
            std::exit(1);
        }
        return {value};
    }
};
