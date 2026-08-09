#include "rest_client.hpp"

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void EnsureCurlGlobalInit() {
    // curl_global_init is not thread-safe and should only be called once
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Override default std::unique_ptr behaviour to free curl_slist* the curl way
struct SlistDeleter {
    void operator()(curl_slist* list) const {
        curl_slist_free_all(list);
    }
};

constexpr long kTimeoutMs = 5000;
constexpr long kConnectTimeoutMs = 3000;

// libcurl returns the response in chunks, so we need to build it up
size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}
}  // namespace

// Override default std::unique_ptr behaviour to free CURL* the curl way
void RestClient::CurlDeleter::operator()(CURL* handle) const {
    curl_easy_cleanup(handle);
}

RestClient::RestClient(std::string base_url) : base_url_(std::move(base_url)) {
    EnsureCurlGlobalInit();
    handle_.reset(curl_easy_init());
    if (handle_ == nullptr) {
        throw std::runtime_error("failed to initialize curl handle");
    }
}

Response RestClient::Get(const std::string_view path, const std::vector<std::string>& headers) {
    curl_easy_reset(handle_.get());  // Clear out options set in the previous HTTP call
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPGET, 1L);
    return Perform(path, headers);
}

Response RestClient::Post(const std::string_view path, const std::string_view body,
                          const std::vector<std::string>& headers) {
    curl_easy_reset(handle_.get());
    curl_easy_setopt(handle_.get(), CURLOPT_POST, 1L);
    // std::string_view::data can be the window of the middle of a string, which wouldn't
    // have \0. We thus need to pass the size of the body
    curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDS,
                     body.data());  // NOLINT(bugprone-suspicious-stringview-data-usage)
    curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    return Perform(path, headers);
}

Response RestClient::Perform(const std::string_view path, const std::vector<std::string>& headers) {
    const std::string url = base_url_ + std::string(path);
    curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT_MS, kTimeoutMs);
    curl_easy_setopt(handle_.get(), CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);

    // response.body is what WriteCallback appends into as data streams in
    Response response;
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, &response.body);

    // Build libcurl's linked list of HTTP headers by relinquishing the list pointer, appending
    // to it and reclaiming the pointer
    std::unique_ptr<curl_slist, SlistDeleter> header_list;
    for (const auto& header : headers) {
        header_list.reset(curl_slist_append(header_list.release(), header.c_str()));
    }
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, header_list.get());

    if (const CURLcode result = curl_easy_perform(handle_.get()); result != CURLE_OK) {
        // The error is on the transport itself, e.g. DNS failure or connection refused,
        // it has nothing to do with the HTTP response
        throw std::runtime_error(std::string("curl request failed: ") + curl_easy_strerror(result));
    }

    long status_code = 0;  // libcurl requires a long for response code
    curl_easy_getinfo(handle_.get(), CURLINFO_RESPONSE_CODE, &status_code);
    response.status_code = static_cast<int>(status_code);
    return response;
}
