#pragma once

#include <string>
#include <functional>

namespace anoverif
{

    class HttpClient
    {
    public:
        struct Response
        {
            int status_code;
            std::string body;
            bool success;
        };

        HttpClient();
        ~HttpClient();

        // High-performance HTTP client with connection pooling
        Response post(const std::string &url, const std::string &data,
                      const std::string &content_type = "application/json");

        Response get(const std::string &url);

        // Set timeout in milliseconds
        void set_timeout(long timeout_ms);

    private:
        void *curl_handle;
        long timeout_ms_;

        static size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *userp);
    };

} // namespace anoverif
