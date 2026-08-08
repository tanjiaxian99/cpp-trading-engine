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
        config.api_key = ReadRequired("OKX_API_KEY");
        config.api_secret = ReadRequired("OKX_API_SECRET");
        config.passphrase = ReadRequired("OKX_PASSPHRASE");
        return config;
    }

private:
    static std::string ReadRequired(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            std::cerr << "missing required environment variable: " << name << "\n";
            std::exit(1);
        }
        return {value};
    }
};
