#pragma once

#include <curl/curl.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct Response {
    int status_code = 0;
    std::string body;
};

class RestClient {
public:
    explicit RestClient(std::string base_url);

    Response Get(std::string_view path, const std::vector<std::string>& headers = {});
    Response Post(std::string_view path, std::string_view body,
                  const std::vector<std::string>& headers = {});

private:
    struct CurlDeleter {
        void operator()(CURL* handle) const;
    };

    Response Perform(std::string_view path, const std::vector<std::string>& headers);

    std::string base_url_;
    std::unique_ptr<CURL, CurlDeleter> handle_;
};
