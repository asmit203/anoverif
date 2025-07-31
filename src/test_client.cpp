#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <json/json.h>
#include <curl/curl.h>
#include <random>

namespace anoverif
{

    class TestClient
    {
    private:
        std::string server_url_;
        std::atomic<int> successful_requests_;
        std::atomic<int> failed_requests_;
        std::atomic<int> true_responses_;
        std::atomic<int> false_responses_;

    public:
        TestClient(const std::string &server_url)
            : server_url_(server_url), successful_requests_(0), failed_requests_(0),
              true_responses_(0), false_responses_(0) {}

        struct Response
        {
            int status_code;
            std::string body;
            bool success;
            long response_time_ms;
        };

        Response send_request(const std::string &idval)
        {
            CURL *curl = curl_easy_init();
            Response response{0, "", false, 0};

            if (!curl)
            {
                response.success = false;
                return response;
            }

            // Prepare JSON payload
            Json::Value payload;
            payload["idval"] = idval;

            Json::StreamWriterBuilder builder;
            std::string json_data = Json::writeString(builder, payload);

            // Setup CURL options
            curl_easy_setopt(curl, CURLOPT_URL, server_url_.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.length());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

            // Set headers
            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            // Record start time
            auto start_time = std::chrono::high_resolution_clock::now();

            // Perform request
            CURLcode res = curl_easy_perform(curl);

            // Calculate response time
            auto end_time = std::chrono::high_resolution_clock::now();
            response.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            end_time - start_time)
                                            .count();

            if (res == CURLE_OK)
            {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
                response.success = (response.status_code >= 200 && response.status_code < 300);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            return response;
        }

        void run_single_test(const std::string &idval)
        {
            std::cout << "Testing with idval: '" << idval << "'" << std::endl;

            auto response = send_request(idval);

            if (response.success)
            {
                successful_requests_++;

                // Parse response
                Json::Value root;
                Json::Reader reader;

                if (reader.parse(response.body, root))
                {
                    bool result = root.get("result", false).asBool();
                    if (result)
                    {
                        true_responses_++;
                    }
                    else
                    {
                        false_responses_++;
                    }

                    std::cout << "  Status: " << response.status_code << std::endl;
                    std::cout << "  Result: " << (result ? "TRUE" : "FALSE") << std::endl;
                    std::cout << "  Response Time: " << response.response_time_ms << "ms" << std::endl;
                    std::cout << "  Success: " << root.get("success", false).asBool() << std::endl;

                    if (root.isMember("timestamp"))
                    {
                        std::cout << "  Timestamp: " << root["timestamp"].asInt64() << std::endl;
                    }
                }
                else
                {
                    std::cout << "  Failed to parse response JSON" << std::endl;
                    std::cout << "  Raw response: " << response.body << std::endl;
                }
            }
            else
            {
                failed_requests_++;
                std::cout << "  Failed: Status " << response.status_code << std::endl;
                std::cout << "  Response: " << response.body << std::endl;
            }

            std::cout << "  Response Time: " << response.response_time_ms << "ms" << std::endl;
            std::cout << std::endl;
        }

        void run_load_test(int num_requests, int concurrency)
        {
            std::cout << "Running load test with " << num_requests
                      << " requests and " << concurrency << " concurrent threads" << std::endl;

            successful_requests_ = 0;
            failed_requests_ = 0;
            true_responses_ = 0;
            false_responses_ = 0;

            auto start_time = std::chrono::high_resolution_clock::now();

            std::vector<std::thread> threads;
            std::atomic<int> requests_sent(0);

            for (int i = 0; i < concurrency; i++)
            {
                threads.emplace_back([this, &requests_sent, num_requests]()
                                     {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000000, 9999999);
                
                while (requests_sent++ < num_requests) {
                    std::string test_id = "user_" + std::to_string(dis(gen));
                    auto response = send_request(test_id);
                    
                    if (response.success) {
                        successful_requests_++;
                        
                        // Parse response to count true/false
                        Json::Value root;
                        Json::Reader reader;
                        if (reader.parse(response.body, root)) {
                            bool result = root.get("result", false).asBool();
                            if (result) {
                                true_responses_++;
                            } else {
                                false_responses_++;
                            }
                        }
                    } else {
                        failed_requests_++;
                    }
                } });
            }

            // Wait for all threads to complete
            for (auto &thread : threads)
            {
                thread.join();
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);

            // Print results
            std::cout << "\nLoad Test Results:" << std::endl;
            std::cout << "  Total Time: " << duration.count() << "ms" << std::endl;
            std::cout << "  Successful Requests: " << successful_requests_.load() << std::endl;
            std::cout << "  Failed Requests: " << failed_requests_.load() << std::endl;
            std::cout << "  True Responses: " << true_responses_.load() << std::endl;
            std::cout << "  False Responses: " << false_responses_.load() << std::endl;
            std::cout << "  Requests/Second: " << (double)num_requests / (duration.count() / 1000.0) << std::endl;
            std::cout << "  Average Response Time: " << (double)duration.count() / num_requests << "ms" << std::endl;

            if (successful_requests_.load() > 0)
            {
                double success_rate = (double)successful_requests_.load() /
                                      (successful_requests_.load() + failed_requests_.load()) * 100.0;
                std::cout << "  Success Rate: " << success_rate << "%" << std::endl;

                double true_rate = (double)true_responses_.load() / successful_requests_.load() * 100.0;
                std::cout << "  True Response Rate: " << true_rate << "%" << std::endl;
            }
        }

    private:
        static size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *userp)
        {
            size_t total_size = size * nmemb;
            userp->append(static_cast<char *>(contents), total_size);
            return total_size;
        }
    };

} // namespace anoverif

int main(int argc, char *argv[])
{
    std::string server_url = "http://localhost:8080/verify";
    bool run_load_test = false;
    int load_requests = 1000;
    int load_concurrency = 10;

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc)
        {
            server_url = argv[++i];
        }
        else if (strcmp(argv[i], "--load") == 0)
        {
            run_load_test = true;
        }
        else if (strcmp(argv[i], "--requests") == 0 && i + 1 < argc)
        {
            load_requests = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--concurrency") == 0 && i + 1 < argc)
        {
            load_concurrency = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --url URL             Server URL (default: http://localhost:8080/verify)" << std::endl;
            std::cout << "  --load                Run load test instead of single tests" << std::endl;
            std::cout << "  --requests N          Number of requests for load test (default: 1000)" << std::endl;
            std::cout << "  --concurrency N       Concurrent threads for load test (default: 10)" << std::endl;
            std::cout << "  --help                Show this help" << std::endl;
            return 0;
        }
    }

    try
    {
        // Initialize CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);

        anoverif::TestClient client(server_url);

        std::cout << "Anoverif Test Client" << std::endl;
        std::cout << "Server URL: " << server_url << std::endl;
        std::cout << std::endl;

        if (run_load_test)
        {
            client.run_load_test(load_requests, load_concurrency);
        }
        else
        {
            // Run individual tests
            std::vector<std::string> test_values = {
                "user123",
                "hello",
                "test",
                "sensitive_data_1",
                "user456",
                "another_test",
                "12345",
                "admin",
                "guest",
                "anonymous"};

            for (const auto &value : test_values)
            {
                client.run_single_test(value);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        // Cleanup CURL
        curl_global_cleanup();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
