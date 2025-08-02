#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <random>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <curl/curl.h>
#include <json/json.h>
#include <atomic>

namespace anoverif
{
    struct SimpleResult
    {
        int request_id;
        double response_time_ms;
        bool success;
        std::string error;
    };

    class SimpleBenchmark
    {
    private:
        std::string server_url_;

        static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
        {
            size_t total_size = size * nmemb;
            userp->append(static_cast<char *>(contents), total_size);
            return total_size;
        }

        SimpleResult send_request(int request_id, const std::string &idval)
        {
            SimpleResult result;
            result.request_id = request_id;

            auto start = std::chrono::high_resolution_clock::now();

            CURL *curl = curl_easy_init();
            std::string response_data;

            if (curl)
            {
                // Create JSON payload
                Json::Value payload;
                payload["idval"] = idval;
                Json::StreamWriterBuilder builder;
                std::string json_data = Json::writeString(builder, payload);

                // Setup headers
                struct curl_slist *headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");

                // Configure CURL with timeout
                curl_easy_setopt(curl, CURLOPT_URL, server_url_.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);        // 30 second timeout
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // 10 second connect timeout

                // Execute request
                CURLcode res = curl_easy_perform(curl);
                auto end = std::chrono::high_resolution_clock::now();
                result.response_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

                if (res == CURLE_OK)
                {
                    long response_code;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                    if (response_code == 200)
                    {
                        result.success = true;
                    }
                    else
                    {
                        result.success = false;
                        result.error = "HTTP " + std::to_string(response_code);
                    }
                }
                else
                {
                    result.success = false;
                    result.error = curl_easy_strerror(res);
                }

                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
            }
            else
            {
                auto end = std::chrono::high_resolution_clock::now();
                result.response_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
                result.success = false;
                result.error = "Failed to initialize CURL";
            }

            return result;
        }

    public:
        SimpleBenchmark(const std::string &server_url) : server_url_(server_url)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        ~SimpleBenchmark()
        {
            curl_global_cleanup();
        }

        void run_detailed_timing_analysis(int num_requests, double delay_seconds)
        {
            std::cout << "🔍 Running Detailed Timing Analysis: " << num_requests
                      << " requests with " << delay_seconds << "s delays" << std::endl;

            std::vector<SimpleResult> results;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            for (int i = 0; i < num_requests; ++i)
            {
                std::string idval = "analysis_req_" + std::to_string(i) + "_" + std::to_string(dis(gen));

                std::cout << "  Request " << (i + 1) << "/" << num_requests << " - ";
                std::cout.flush();

                SimpleResult result = send_request(i, idval);
                results.push_back(result);

                if (result.success)
                {
                    std::cout << "✓ " << std::fixed << std::setprecision(1)
                              << result.response_time_ms << "ms" << std::endl;
                }
                else
                {
                    std::cout << "✗ " << result.error << " ("
                              << std::fixed << std::setprecision(1)
                              << result.response_time_ms << "ms)" << std::endl;
                }

                // Wait between requests
                if (i < num_requests - 1)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(delay_seconds));
                }
            }

            // Analyze results
            analyze_timing_patterns(results);
        }

        void analyze_timing_patterns(const std::vector<SimpleResult> &results)
        {
            std::cout << "\n📊 Detailed Timing Analysis:" << std::endl;
            std::cout << std::string(50, '=') << std::endl;

            // Basic metrics
            int successful = 0;
            int failed = 0;
            std::vector<double> response_times;

            for (const auto &result : results)
            {
                if (result.success)
                {
                    successful++;
                    response_times.push_back(result.response_time_ms);
                }
                else
                {
                    failed++;
                }
            }

            std::cout << "\n🎯 Success Metrics:" << std::endl;
            std::cout << "  • Total Requests: " << results.size() << std::endl;
            std::cout << "  • Successful: " << successful << std::endl;
            std::cout << "  • Failed: " << failed << std::endl;
            std::cout << "  • Success Rate: " << std::fixed << std::setprecision(1)
                      << (double)successful / results.size() * 100.0 << "%" << std::endl;

            if (!response_times.empty())
            {
                std::sort(response_times.begin(), response_times.end());

                double min_time = response_times.front();
                double max_time = response_times.back();
                double sum = 0;
                for (double time : response_times)
                {
                    sum += time;
                }
                double avg_time = sum / response_times.size();

                // Calculate median
                double median_time;
                size_t mid = response_times.size() / 2;
                if (response_times.size() % 2 == 0)
                {
                    median_time = (response_times[mid - 1] + response_times[mid]) / 2.0;
                }
                else
                {
                    median_time = response_times[mid];
                }

                // Calculate percentiles
                double p95 = response_times[static_cast<size_t>(response_times.size() * 0.95)];
                double p99 = response_times[static_cast<size_t>(response_times.size() * 0.99)];

                // Calculate standard deviation
                double variance = 0;
                for (double time : response_times)
                {
                    variance += (time - avg_time) * (time - avg_time);
                }
                double std_dev = std::sqrt(variance / response_times.size());

                std::cout << "\n⏱️ Response Time Statistics:" << std::endl;
                std::cout << "  • Min: " << std::fixed << std::setprecision(2) << min_time << "ms" << std::endl;
                std::cout << "  • Max: " << std::setprecision(2) << max_time << "ms" << std::endl;
                std::cout << "  • Average: " << std::setprecision(2) << avg_time << "ms" << std::endl;
                std::cout << "  • Median: " << std::setprecision(2) << median_time << "ms" << std::endl;
                std::cout << "  • 95th Percentile: " << std::setprecision(2) << p95 << "ms" << std::endl;
                std::cout << "  • 99th Percentile: " << std::setprecision(2) << p99 << "ms" << std::endl;
                std::cout << "  • Std Deviation: " << std::setprecision(2) << std_dev << "ms" << std::endl;

                // Timing variation analysis
                std::vector<double> variations;
                for (size_t i = 1; i < response_times.size(); ++i)
                {
                    variations.push_back(std::abs(response_times[i] - response_times[i - 1]));
                }

                if (!variations.empty())
                {
                    double max_variation = *std::max_element(variations.begin(), variations.end());
                    double sum_var = 0;
                    for (double var : variations)
                    {
                        sum_var += var;
                    }
                    double avg_variation = sum_var / variations.size();

                    std::cout << "\n📈 Timing Variations:" << std::endl;
                    std::cout << "  • Range: " << std::setprecision(2) << (max_time - min_time) << "ms" << std::endl;
                    std::cout << "  • Max Sequential Variation: " << std::setprecision(2) << max_variation << "ms" << std::endl;
                    std::cout << "  • Avg Sequential Variation: " << std::setprecision(2) << avg_variation << "ms" << std::endl;
                    std::cout << "  • Coefficient of Variation: " << std::setprecision(3) << (std_dev / avg_time) << std::endl;
                }

                // Pattern detection
                std::cout << "\n🔍 Pattern Detection:" << std::endl;
                if (std_dev < avg_time * 0.1)
                {
                    std::cout << "  ✓ Very consistent response times (low variation)" << std::endl;
                }
                else if (std_dev < avg_time * 0.25)
                {
                    std::cout << "  ✓ Moderately consistent response times" << std::endl;
                }
                else
                {
                    std::cout << "  ⚠️ High variation in response times" << std::endl;
                }

                if (max_time > avg_time * 2)
                {
                    std::cout << "  ⚠️ Some requests are significantly slower than average" << std::endl;
                }

                // Save detailed CSV
                save_timing_csv(results, "detailed_timing_analysis.csv");
            }

            // Error analysis
            if (failed > 0)
            {
                std::cout << "\n❌ Error Analysis:" << std::endl;
                std::map<std::string, int> error_counts;
                for (const auto &result : results)
                {
                    if (!result.success)
                    {
                        error_counts[result.error]++;
                    }
                }

                for (const auto &pair : error_counts)
                {
                    std::cout << "  • " << pair.first << ": " << pair.second << " times" << std::endl;
                }
            }
        }

        void save_timing_csv(const std::vector<SimpleResult> &results, const std::string &filename)
        {
            std::ofstream file(filename);
            if (!file.is_open())
            {
                std::cerr << "Error: Could not open " << filename << " for writing" << std::endl;
                return;
            }

            file << "request_id,response_time_ms,success,error\n";
            for (const auto &result : results)
            {
                file << result.request_id << ","
                     << std::fixed << std::setprecision(3) << result.response_time_ms << ","
                     << (result.success ? "true" : "false") << ","
                     << "\"" << result.error << "\"\n";
            }

            std::cout << "💾 Detailed results saved to " << filename << std::endl;
        }
    };
}

int main(int argc, char *argv[])
{
    std::string server_url = "http://localhost:8082/verify";

    if (argc > 1)
    {
        server_url = argv[1];
    }

    std::cout << "🎯 Simple Timing Analysis Benchmark" << std::endl;
    std::cout << "Server URL: " << server_url << std::endl;
    std::cout << std::string(50, '=') << std::endl;

    anoverif::SimpleBenchmark benchmark(server_url);

    try
    {
        // Run detailed timing analysis with proper delays
        benchmark.run_detailed_timing_analysis(20, 3.0); // 20 requests, 3 seconds apart

        std::cout << "\n🎉 Timing analysis completed!" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Benchmark failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
