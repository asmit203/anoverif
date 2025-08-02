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
#include <mutex>

namespace anoverif
{
    struct BenchmarkResult
    {
        int request_id;
        double start_time_ms;
        double end_time_ms;
        double response_time_ms;
        bool success;
        bool result_value;
        std::string error;
    };

    struct BenchmarkStats
    {
        int total_requests = 0;
        int successful_requests = 0;
        int failed_requests = 0;
        double success_rate = 0.0;
        double min_response_time = 0.0;
        double max_response_time = 0.0;
        double avg_response_time = 0.0;
        double median_response_time = 0.0;
        double p95_response_time = 0.0;
        double p99_response_time = 0.0;
        double std_deviation = 0.0;
        double max_variation = 0.0;
        double avg_variation = 0.0;
        std::vector<double> response_times;
    };

    class AdvancedBenchmark
    {
    private:
        std::string server_url_;
        std::vector<BenchmarkResult> results_;
        std::mutex results_mutex_;
        std::atomic<int> request_counter_{0};

        static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
        {
            size_t total_size = size * nmemb;
            userp->append(static_cast<char *>(contents), total_size);
            return total_size;
        }

        BenchmarkResult send_request(int request_id, const std::string &idval)
        {
            BenchmarkResult result;
            result.request_id = request_id;
            result.start_time_ms = get_current_time_ms();

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

                // Configure CURL
                curl_easy_setopt(curl, CURLOPT_URL, server_url_.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

                // Execute request
                CURLcode res = curl_easy_perform(curl);
                result.end_time_ms = get_current_time_ms();
                result.response_time_ms = result.end_time_ms - result.start_time_ms;

                if (res == CURLE_OK)
                {
                    // Parse response
                    Json::Value response_json;
                    Json::Reader reader;
                    if (reader.parse(response_data, response_json))
                    {
                        result.success = response_json.get("success", false).asBool();
                        result.result_value = response_json.get("result", false).asBool();
                    }
                    else
                    {
                        result.success = false;
                        result.error = "Failed to parse JSON response";
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
                result.end_time_ms = get_current_time_ms();
                result.response_time_ms = result.end_time_ms - result.start_time_ms;
                result.success = false;
                result.error = "Failed to initialize CURL";
            }

            return result;
        }

        double get_current_time_ms()
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration<double, std::milli>(duration).count();
        }

    public:
        AdvancedBenchmark(const std::string &server_url) : server_url_(server_url)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        ~AdvancedBenchmark()
        {
            curl_global_cleanup();
        }

        // Burst test: Send batches of requests with intervals
        void run_burst_test(int burst_size, int num_bursts, double interval_seconds)
        {
            std::cout << "🚀 Running Burst Test: " << burst_size << " requests x " << num_bursts
                      << " bursts, " << interval_seconds << "s intervals" << std::endl;

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            for (int burst = 0; burst < num_bursts; ++burst)
            {
                std::cout << "  Burst " << (burst + 1) << "/" << num_bursts << "..." << std::endl;

                std::vector<std::future<BenchmarkResult>> futures;

                // Launch burst
                for (int i = 0; i < burst_size; ++i)
                {
                    int req_id = request_counter_++;
                    std::string idval = "burst_" + std::to_string(burst) + "_req_" +
                                        std::to_string(i) + "_" + std::to_string(dis(gen));

                    futures.push_back(std::async(std::launch::async,
                                                 [this, req_id, idval]()
                                                 {
                                                     return this->send_request(req_id, idval);
                                                 }));
                }

                // Collect results
                for (auto &future : futures)
                {
                    BenchmarkResult result = future.get();
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_.push_back(result);
                }

                // Wait before next burst (except for last one)
                if (burst < num_bursts - 1)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(interval_seconds));
                }
            }
        }

        // Ramp test: Gradually increase request rate
        void run_ramp_test(double start_rps, double end_rps, double duration_seconds)
        {
            std::cout << "📈 Running Ramp Test: " << start_rps << " to " << end_rps
                      << " RPS over " << duration_seconds << " seconds" << std::endl;

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            double start_time = get_current_time_ms();
            double end_time = start_time + (duration_seconds * 1000);

            while (get_current_time_ms() < end_time)
            {
                double elapsed = (get_current_time_ms() - start_time) / 1000.0;
                double progress = elapsed / duration_seconds;
                double current_rps = start_rps + (end_rps - start_rps) * progress;

                if (current_rps > 0)
                {
                    double delay_ms = 1000.0 / current_rps;

                    int req_id = request_counter_++;
                    std::string idval = "ramp_req_" + std::to_string(req_id) + "_" + std::to_string(dis(gen));

                    // Send request asynchronously
                    std::thread([this, req_id, idval]()
                                {
                        BenchmarkResult result = this->send_request(req_id, idval);
                        std::lock_guard<std::mutex> lock(this->results_mutex_);
                        this->results_.push_back(result); })
                        .detach();

                    // Progress indicator
                    if (req_id % 10 == 0)
                    {
                        std::cout << "  Progress: " << std::fixed << std::setprecision(1)
                                  << (progress * 100) << "% - Current RPS: "
                                  << std::setprecision(1) << current_rps << std::endl;
                    }

                    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
                }
            }

            // Wait for remaining requests to complete
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // Sustained test: Constant request rate
        void run_sustained_test(double rps, double duration_seconds)
        {
            std::cout << "⚡ Running Sustained Test: " << rps << " RPS for "
                      << duration_seconds << " seconds" << std::endl;

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            double delay_ms = 1000.0 / rps;
            double start_time = get_current_time_ms();
            double end_time = start_time + (duration_seconds * 1000);

            while (get_current_time_ms() < end_time)
            {
                int req_id = request_counter_++;
                std::string idval = "sustained_req_" + std::to_string(req_id) + "_" + std::to_string(dis(gen));

                // Send request asynchronously
                std::thread([this, req_id, idval]()
                            {
                    BenchmarkResult result = this->send_request(req_id, idval);
                    std::lock_guard<std::mutex> lock(this->results_mutex_);
                    this->results_.push_back(result); })
                    .detach();

                if (req_id % 20 == 0)
                {
                    double elapsed = (get_current_time_ms() - start_time) / 1000.0;
                    std::cout << "  Progress: " << std::fixed << std::setprecision(1)
                              << elapsed << "s - Requests sent: " << req_id << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            }

            // Wait for remaining requests to complete
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // High concurrency test
        void run_high_concurrency_test(int concurrency, int total_requests)
        {
            std::cout << "🔀 Running High Concurrency Test: " << concurrency
                      << " concurrent, " << total_requests << " total requests" << std::endl;

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            std::vector<std::future<BenchmarkResult>> futures;
            std::atomic<int> active_requests{0};

            for (int i = 0; i < total_requests; ++i)
            {
                // Wait if we've reached concurrency limit
                while (active_requests >= concurrency)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                int req_id = request_counter_++;
                std::string idval = "concurrent_req_" + std::to_string(i) + "_" + std::to_string(dis(gen));

                active_requests++;
                futures.push_back(std::async(std::launch::async,
                                             [this, req_id, idval, &active_requests]()
                                             {
                                                 BenchmarkResult result = this->send_request(req_id, idval);
                                                 active_requests--;
                                                 return result;
                                             }));

                if (i % 50 == 0)
                {
                    std::cout << "  Progress: " << i << "/" << total_requests
                              << " requests queued" << std::endl;
                }
            }

            // Collect all results
            for (auto &future : futures)
            {
                BenchmarkResult result = future.get();
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_.push_back(result);
            }
        }

        BenchmarkStats analyze_results()
        {
            BenchmarkStats stats;
            std::lock_guard<std::mutex> lock(results_mutex_);

            if (results_.empty())
            {
                return stats;
            }

            stats.total_requests = results_.size();

            // Separate successful and failed requests
            std::vector<BenchmarkResult> successful;
            std::vector<BenchmarkResult> failed;

            for (const auto &result : results_)
            {
                if (result.success)
                {
                    successful.push_back(result);
                    stats.response_times.push_back(result.response_time_ms);
                }
                else
                {
                    failed.push_back(result);
                }
            }

            stats.successful_requests = successful.size();
            stats.failed_requests = failed.size();
            stats.success_rate = (double)successful.size() / results_.size() * 100.0;

            if (!stats.response_times.empty())
            {
                // Sort for percentile calculations
                std::sort(stats.response_times.begin(), stats.response_times.end());

                stats.min_response_time = stats.response_times.front();
                stats.max_response_time = stats.response_times.back();

                // Calculate average
                double sum = 0;
                for (double time : stats.response_times)
                {
                    sum += time;
                }
                stats.avg_response_time = sum / stats.response_times.size();

                // Calculate median
                size_t mid = stats.response_times.size() / 2;
                if (stats.response_times.size() % 2 == 0)
                {
                    stats.median_response_time = (stats.response_times[mid - 1] + stats.response_times[mid]) / 2.0;
                }
                else
                {
                    stats.median_response_time = stats.response_times[mid];
                }

                // Calculate percentiles
                stats.p95_response_time = stats.response_times[static_cast<size_t>(stats.response_times.size() * 0.95)];
                stats.p99_response_time = stats.response_times[static_cast<size_t>(stats.response_times.size() * 0.99)];

                // Calculate standard deviation
                double variance = 0;
                for (double time : stats.response_times)
                {
                    variance += (time - stats.avg_response_time) * (time - stats.avg_response_time);
                }
                stats.std_deviation = std::sqrt(variance / stats.response_times.size());

                // Calculate variations
                std::vector<double> variations;
                for (size_t i = 1; i < stats.response_times.size(); ++i)
                {
                    variations.push_back(std::abs(stats.response_times[i] - stats.response_times[i - 1]));
                }

                if (!variations.empty())
                {
                    stats.max_variation = *std::max_element(variations.begin(), variations.end());
                    double sum_var = 0;
                    for (double var : variations)
                    {
                        sum_var += var;
                    }
                    stats.avg_variation = sum_var / variations.size();
                }
            }

            return stats;
        }

        void print_detailed_report(const std::string &test_name, const BenchmarkStats &stats)
        {
            std::cout << "\n🎯 " << test_name << " Results:" << std::endl;
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

            std::cout << "\n📊 Basic Metrics:" << std::endl;
            std::cout << "  • Total Requests: " << stats.total_requests << std::endl;
            std::cout << "  • Successful: " << stats.successful_requests << std::endl;
            std::cout << "  • Failed: " << stats.failed_requests << std::endl;
            std::cout << "  • Success Rate: " << std::fixed << std::setprecision(1)
                      << stats.success_rate << "%" << std::endl;

            if (stats.successful_requests > 0)
            {
                std::cout << "\n⏱️ Response Time Analysis:" << std::endl;
                std::cout << "  • Min: " << std::fixed << std::setprecision(2)
                          << stats.min_response_time << "ms" << std::endl;
                std::cout << "  • Max: " << std::setprecision(2)
                          << stats.max_response_time << "ms" << std::endl;
                std::cout << "  • Average: " << std::setprecision(2)
                          << stats.avg_response_time << "ms" << std::endl;
                std::cout << "  • Median: " << std::setprecision(2)
                          << stats.median_response_time << "ms" << std::endl;
                std::cout << "  • 95th Percentile: " << std::setprecision(2)
                          << stats.p95_response_time << "ms" << std::endl;
                std::cout << "  • 99th Percentile: " << std::setprecision(2)
                          << stats.p99_response_time << "ms" << std::endl;
                std::cout << "  • Std Deviation: " << std::setprecision(2)
                          << stats.std_deviation << "ms" << std::endl;

                std::cout << "\n📈 Timing Variations:" << std::endl;
                std::cout << "  • Max Variation: " << std::setprecision(2)
                          << stats.max_variation << "ms" << std::endl;
                std::cout << "  • Avg Variation: " << std::setprecision(2)
                          << stats.avg_variation << "ms" << std::endl;

                double consistency = 1.0 - (stats.std_deviation / stats.avg_response_time);
                std::cout << "  • Consistency Score: " << std::setprecision(3)
                          << consistency << std::endl;
            }
        }

        void save_detailed_results(const std::string &filename)
        {
            std::ofstream file(filename);
            if (!file.is_open())
            {
                std::cerr << "Error: Could not open " << filename << " for writing" << std::endl;
                return;
            }

            file << "request_id,start_time_ms,end_time_ms,response_time_ms,success,result_value,error\n";

            std::lock_guard<std::mutex> lock(results_mutex_);
            for (const auto &result : results_)
            {
                file << result.request_id << ","
                     << std::fixed << std::setprecision(3) << result.start_time_ms << ","
                     << result.end_time_ms << ","
                     << result.response_time_ms << ","
                     << (result.success ? "true" : "false") << ","
                     << (result.result_value ? "true" : "false") << ","
                     << "\"" << result.error << "\"\n";
            }

            std::cout << "💾 Detailed results saved to " << filename << std::endl;
        }

        void clear_results()
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.clear();
            request_counter_ = 0;
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

    std::cout << "🚀 Advanced Anoverif Benchmark Suite" << std::endl;
    std::cout << "Server URL: " << server_url << std::endl;
    std::cout << std::string(50, '=') << std::endl;

    anoverif::AdvancedBenchmark benchmark(server_url);

    try
    {
        // Test 1: Burst Test
        std::cout << "\n"
                  << std::string(50, '=') << std::endl;
        benchmark.run_burst_test(15, 4, 2.0); // 15 requests x 4 bursts, 2s intervals
        auto stats = benchmark.analyze_results();
        benchmark.print_detailed_report("Burst Test", stats);
        benchmark.save_detailed_results("burst_test_results.csv");
        benchmark.clear_results();

        // Test 2: Ramp Test
        std::cout << "\n"
                  << std::string(50, '=') << std::endl;
        benchmark.run_ramp_test(5.0, 20.0, 25.0); // 5 to 20 RPS over 25 seconds
        stats = benchmark.analyze_results();
        benchmark.print_detailed_report("Ramp Test", stats);
        benchmark.save_detailed_results("ramp_test_results.csv");
        benchmark.clear_results();

        // Test 3: Sustained Test
        std::cout << "\n"
                  << std::string(50, '=') << std::endl;
        benchmark.run_sustained_test(12.0, 20.0); // 12 RPS for 20 seconds
        stats = benchmark.analyze_results();
        benchmark.print_detailed_report("Sustained Test", stats);
        benchmark.save_detailed_results("sustained_test_results.csv");
        benchmark.clear_results();

        // Test 4: High Concurrency Test
        std::cout << "\n"
                  << std::string(50, '=') << std::endl;
        benchmark.run_high_concurrency_test(25, 200); // 25 concurrent, 200 total
        stats = benchmark.analyze_results();
        benchmark.print_detailed_report("High Concurrency Test", stats);
        benchmark.save_detailed_results("concurrency_test_results.csv");

        std::cout << "\n🎉 Advanced benchmark suite completed!" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Benchmark failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
