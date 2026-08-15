#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

class WsResponseDemux {
public:
    using ResponseHandler = std::function<void(std::string_view response)>;

    void Track(std::string id, ResponseHandler handler);
    bool Dispatch(std::string_view message);

private:
    std::unordered_map<std::string, ResponseHandler> pending_;
};
