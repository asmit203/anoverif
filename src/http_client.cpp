#include "http_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <memory>

namespace anoverif
{

    HttpClient::HttpClient() : timeout_ms_(5000)
    {
        curl_handle = curl_easy_init();
        if (!curl_handle)
        {
            throw std::runtime_error("Failed to initialize CURL");
        }

        // Set common options for performance
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, timeout_ms_);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, 3000L);

        // Performance optimizations
        curl_easy_setopt(curl_handle, CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_FORBID_REUSE, 0L);
    }

    HttpClient::~HttpClient()
    {
        if (curl_handle)
        {
            curl_easy_cleanup(curl_handle);
        }
    }

    void HttpClient::set_timeout(long timeout_ms)
    {
        timeout_ms_ = timeout_ms;
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, timeout_ms_);
    }

    HttpClient::Response HttpClient::post(const std::string &url, const std::string &data,
                                          const std::string &content_type)
    {
        Response response{0, "", false};

        curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, data.length());
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response.body);

        // Set headers
        struct curl_slist *headers = nullptr;
        std::string content_type_header = "Content-Type: " + content_type;
        headers = curl_slist_append(headers, content_type_header.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response.status_code);
            response.success = (response.status_code >= 200 && response.status_code < 300);
        }

        curl_slist_free_all(headers);
        return response;
    }

    HttpClient::Response HttpClient::get(const std::string &url)
    {
        Response response{0, "", false};

        curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response.body);

        CURLcode res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response.status_code);
            response.success = (response.status_code >= 200 && response.status_code < 300);
        }

        return response;
    }

    size_t HttpClient::write_callback(void *contents, size_t size, size_t nmemb, std::string *userp)
    {
        size_t total_size = size * nmemb;
        userp->append(static_cast<char *>(contents), total_size);
        return total_size;
    }

} // namespace anoverif
