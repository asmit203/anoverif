#include <microhttpd.h>
#include <json/json.h>
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <cstring>
#include <fstream>
#include <queue>
#include <condition_variable>
#include <future>
#include <random>

#include "hash_utils.h"
#include "http_client.h"
#include "config.h"

namespace anoverif
{
    // Memory pool for efficient string allocation
    class StringPool
    {
    private:
        std::vector<std::unique_ptr<std::string>> pool_;
        std::mutex pool_mutex_;
        size_t pool_size_ = 0;
        static constexpr size_t MAX_POOL_SIZE = 1000;

    public:
        std::unique_ptr<std::string> get()
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (!pool_.empty())
            {
                auto str = std::move(pool_.back());
                pool_.pop_back();
                pool_size_--;
                str->clear();
                return str;
            }
            return std::make_unique<std::string>();
        }

        void release(std::unique_ptr<std::string> str)
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (pool_size_ < MAX_POOL_SIZE)
            {
                str->clear();
                str->shrink_to_fit();
                pool_.push_back(std::move(str));
                pool_size_++;
            }
        }
    };

    // Struct to track pending requests
    struct PendingRequest
    {
        std::string request_hash;
        std::string original_idval;
        std::chrono::steady_clock::time_point created_at;
        std::promise<Json::Value> response_promise;

        PendingRequest(const std::string &hash, const std::string &idval)
            : request_hash(hash), original_idval(idval),
              created_at(std::chrono::steady_clock::now()) {}
    };

    class AnonymizationServer
    {
    private:
        struct MHD_Daemon *daemon_;
        struct MHD_Daemon *ssl_daemon_;
        Config config_;
        std::unique_ptr<HttpClient> http_client_;
        std::atomic<bool> running_;

        // Async request processing
        std::queue<std::shared_ptr<PendingRequest>> request_queue_;
        std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
        std::mutex queue_mutex_;
        std::mutex pending_mutex_;
        std::condition_variable queue_cv_;
        std::thread processing_thread_;

        // Request mixing parameters
        std::random_device rd_;
        std::mt19937 gen_;
        std::uniform_int_distribution<> delay_dist_;

        // Performance monitoring
        std::atomic<uint64_t> request_count_;
        std::atomic<uint64_t> success_count_;
        std::atomic<uint64_t> error_count_;
        std::atomic<uint64_t> pending_count_;

        // Hash cache for improved performance (optional)
        std::unordered_map<std::string, std::string> hash_cache_;
        std::mutex cache_mutex_;

        // Memory pool for efficient allocations
        StringPool string_pool_;

        // Pre-allocated JSON builders (thread-local storage)
        thread_local static Json::StreamWriterBuilder json_builder_;
        thread_local static Json::Reader json_reader_;

    public:
        AnonymizationServer(const Config &config)
            : daemon_(nullptr), ssl_daemon_(nullptr), config_(config), running_(false),
              gen_(rd_()), delay_dist_(100, 2000), // 100ms to 2s random delay
              request_count_(0), success_count_(0), error_count_(0), pending_count_(0)
        {
            http_client_ = std::make_unique<HttpClient>();
            http_client_->set_timeout(config_.backend_timeout_ms);
        }

        ~AnonymizationServer()
        {
            stop();
        }

        bool start()
        {
            running_ = true;

            // Start async processing thread
            processing_thread_ = std::thread(&AnonymizationServer::process_requests, this);

            // Start HTTP server
            daemon_ = MHD_start_daemon(
                MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
                config_.port,
                nullptr, nullptr,
                &request_handler, this,
                MHD_OPTION_CONNECTION_LIMIT, config_.max_connections,
                MHD_OPTION_CONNECTION_TIMEOUT, config_.connection_timeout,
                MHD_OPTION_END);

            if (!daemon_)
            {
                std::cerr << "Failed to start HTTP server on port " << config_.port << std::endl;
                return false;
            }

            std::cout << "HTTP server started on " << config_.bind_address << ":" << config_.port << std::endl;

            // Start HTTPS server if SSL is enabled
            if (config_.enable_ssl)
            {
                ssl_daemon_ = MHD_start_daemon(
                    MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_TLS,
                    config_.ssl_port,
                    nullptr, nullptr,
                    &request_handler, this,
                    MHD_OPTION_HTTPS_MEM_KEY, load_file(config_.ssl_key_file).c_str(),
                    MHD_OPTION_HTTPS_MEM_CERT, load_file(config_.ssl_cert_file).c_str(),
                    MHD_OPTION_CONNECTION_LIMIT, config_.max_connections,
                    MHD_OPTION_CONNECTION_TIMEOUT, config_.connection_timeout,
                    MHD_OPTION_END);

                if (!ssl_daemon_)
                {
                    std::cerr << "Failed to start HTTPS server on port " << config_.ssl_port << std::endl;
                    return false;
                }

                std::cout << "HTTPS server started on " << config_.bind_address << ":" << config_.ssl_port << std::endl;
            }

            return true;
        }

        void stop()
        {
            running_ = false;

            // Wake up processing thread
            queue_cv_.notify_all();

            // Wait for processing thread to finish
            if (processing_thread_.joinable())
            {
                processing_thread_.join();
            }

            if (daemon_)
            {
                MHD_stop_daemon(daemon_);
                daemon_ = nullptr;
            }

            if (ssl_daemon_)
            {
                MHD_stop_daemon(ssl_daemon_);
                ssl_daemon_ = nullptr;
            }
        }

        void print_stats() const
        {
            std::cout << "Server Statistics:" << std::endl;
            std::cout << "  Total Requests: " << request_count_.load() << std::endl;
            std::cout << "  Successful: " << success_count_.load() << std::endl;
            std::cout << "  Errors: " << error_count_.load() << std::endl;
            std::cout << "  Pending Requests: " << pending_count_.load() << std::endl;
            std::cout << "  Cache Size: " << hash_cache_.size() << std::endl;
        }

    private:
        // Async request processing thread
        void process_requests()
        {
            std::cout << "Started async request processing thread" << std::endl;

            while (running_)
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                // Wait for requests or shutdown signal
                queue_cv_.wait(lock, [this]
                               { return !request_queue_.empty() || !running_; });

                if (!running_)
                    break;

                // Get a batch of requests to process (optimized batch size)
                std::vector<std::shared_ptr<PendingRequest>> batch;
                batch.reserve(20);                                   // Reserve space to avoid reallocations
                while (!request_queue_.empty() && batch.size() < 20) // Increased batch size
                {
                    batch.push_back(std::move(request_queue_.front()));
                    request_queue_.pop();
                }

                lock.unlock();

                // Process requests with random delays for mixing
                for (auto &request : batch)
                {
                    std::thread([this, request = std::move(request)]()
                                {
                                    // Random delay to mix up request order
                                    int delay_ms = delay_dist_(gen_);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

                                    // Process the request
                                    Json::Value result = process_backend_request(request->original_idval);

                                    // Set the response
                                    request->response_promise.set_value(std::move(result));

                                    // Remove from pending requests
                                    {
                                        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
                                        pending_requests_.erase(request->request_hash);
                                        pending_count_--;
                                    } })
                        .detach();
                }
            }

            std::cout << "Async request processing thread stopped" << std::endl;
        }

        // Process request with backend API (original idval, not hash)
        Json::Value process_backend_request(const std::string &original_idval)
        {
            try
            {
                // Forward original idval to backend API (not hash!)
                Json::Value backend_request;
                backend_request["idval"] = original_idval; // Send original value

                // Use thread-local builder for efficiency
                std::string backend_data = Json::writeString(json_builder_, backend_request);

                auto response = http_client_->post(config_.backend_api_url, backend_data, "application/json");

                if (!response.success)
                {
                    error_count_++;
                    Json::Value error_result;
                    error_result["success"] = false;
                    error_result["error"] = "Backend API unavailable";
                    return error_result;
                }

                // Parse backend response using thread-local reader
                Json::Value backend_result;
                if (!json_reader_.parse(response.body, backend_result))
                {
                    error_count_++;
                    Json::Value error_result;
                    error_result["success"] = false;
                    error_result["error"] = "Invalid backend response";
                    return error_result;
                }

                success_count_++;

                // Return successful result
                Json::Value client_response;
                client_response["success"] = true;
                client_response["result"] = backend_result.get("result", false).asBool();
                client_response["timestamp"] = static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());

                return client_response;
            }
            catch (const std::exception &e)
            {
                error_count_++;
                Json::Value error_result;
                error_result["success"] = false;
                error_result["error"] = std::string("Processing error: ") + e.what();
                return error_result;
            }
        }

        static MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                                          const char *url, const char *method,
                                          const char *version, const char *upload_data,
                                          size_t *upload_data_size, void **con_cls)
        {

            auto *server = static_cast<AnonymizationServer *>(cls);
            return server->handle_request(connection, url, method, version,
                                          upload_data, upload_data_size, con_cls);
        }

        MHD_Result handle_request(struct MHD_Connection *connection, const char *url,
                                  const char *method, const char *version,
                                  const char *upload_data, size_t *upload_data_size,
                                  void **con_cls)
        {

            request_count_++;

            // Handle CORS preflight
            if (strcmp(method, "OPTIONS") == 0)
            {
                return send_cors_response(connection);
            }

            // Only handle POST requests to /verify endpoint
            if (strcmp(method, "POST") != 0 || strcmp(url, "/verify") != 0)
            {
                return send_error_response(connection, 404, "Not Found");
            }

            // Handle POST data
            if (*con_cls == nullptr)
            {
                *con_cls = string_pool_.get().release(); // Use memory pool
                return MHD_YES;
            }

            auto *post_data = static_cast<std::string *>(*con_cls);

            if (*upload_data_size != 0)
            {
                post_data->append(upload_data, *upload_data_size);
                *upload_data_size = 0;
                return MHD_YES;
            }

            // Process the request
            std::string response = process_verification_request(*post_data);

            // Return to memory pool instead of delete
            auto post_data_ptr = std::unique_ptr<std::string>(post_data);
            string_pool_.release(std::move(post_data_ptr));
            *con_cls = nullptr;

            return send_json_response(connection, response);
        }

        std::string process_verification_request(const std::string &request_data)
        {
            try
            {
                // Parse JSON request using thread-local reader
                Json::Value root;
                if (!json_reader_.parse(request_data, root))
                {
                    error_count_++;
                    return create_error_response("Invalid JSON");
                }

                if (!root.isMember("idval") || !root["idval"].isString())
                {
                    error_count_++;
                    return create_error_response("Missing or invalid 'idval' parameter");
                }

                const std::string &idval = root["idval"].asString(); // Use const reference

                if (idval.empty())
                {
                    error_count_++;
                    return create_error_response("Empty 'idval' parameter");
                }

                // Create hash for request tracking (but send original idval to backend)
                std::string request_hash = hash_with_cache(idval);

                // Create pending request
                auto pending_request = std::make_shared<PendingRequest>(request_hash, idval);
                auto future = pending_request->response_promise.get_future();

                // Add to pending requests and queue
                {
                    std::lock_guard<std::mutex> pending_lock(pending_mutex_);
                    pending_requests_[request_hash] = pending_request;
                    pending_count_++;
                }

                {
                    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
                    request_queue_.push(pending_request);
                }

                // Notify processing thread
                queue_cv_.notify_one();

                // Wait for async response (with timeout)
                auto status = future.wait_for(std::chrono::milliseconds(config_.backend_timeout_ms + 3000));

                if (status == std::future_status::timeout)
                {
                    // Cleanup on timeout
                    {
                        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
                        pending_requests_.erase(request_hash);
                        pending_count_--;
                    }

                    error_count_++;
                    return create_error_response("Request timeout");
                }

                // Get the result
                Json::Value result = future.get();

                // Convert to string response using thread-local builder
                return Json::writeString(json_builder_, result);
            }
            catch (const std::exception &e)
            {
                error_count_++;
                return create_error_response(std::string("Processing error: ") + e.what());
            }
        }

        std::string hash_with_cache(const std::string &input)
        {
            // Check cache first (optional optimization)
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it = hash_cache_.find(input);
                if (it != hash_cache_.end())
                {
                    return it->second;
                }
            }

            // Add salt for additional security
            std::string salted_input = config_.hash_salt + input + config_.hash_salt;
            std::string hash = HashUtils::sha256_hash(salted_input);

            // Cache the result with LRU-style eviction
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                if (hash_cache_.size() >= 10000)
                {
                    // Simple eviction: remove oldest entries (first 1000)
                    auto it = hash_cache_.begin();
                    for (int i = 0; i < 1000 && it != hash_cache_.end(); ++i)
                    {
                        it = hash_cache_.erase(it);
                    }
                }
                hash_cache_[input] = hash;
            }

            return hash;
        }

        std::string create_error_response(const std::string &message)
        {
            Json::Value response;
            response["success"] = false;
            response["error"] = message;
            response["timestamp"] = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());

            // Use thread-local builder for efficiency
            return Json::writeString(json_builder_, response);
        }

        MHD_Result send_json_response(struct MHD_Connection *connection, const std::string &json)
        {
            auto *response = MHD_create_response_from_buffer(
                json.length(), (void *)json.c_str(), MHD_RESPMEM_MUST_COPY);

            MHD_add_response_header(response, "Content-Type", "application/json");
            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
            MHD_add_response_header(response, "Access-Control-Allow-Methods", "POST, OPTIONS");
            MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");

            MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return ret;
        }

        MHD_Result send_error_response(struct MHD_Connection *connection, int status_code, const std::string &message)
        {
            std::string error_json = create_error_response(message);

            auto *response = MHD_create_response_from_buffer(
                error_json.length(), (void *)error_json.c_str(), MHD_RESPMEM_MUST_COPY);

            MHD_add_response_header(response, "Content-Type", "application/json");
            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

            MHD_Result ret = MHD_queue_response(connection, status_code, response);
            MHD_destroy_response(response);

            return ret;
        }

        MHD_Result send_cors_response(struct MHD_Connection *connection)
        {
            auto *response = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);

            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
            MHD_add_response_header(response, "Access-Control-Allow-Methods", "POST, OPTIONS");
            MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
            MHD_add_response_header(response, "Access-Control-Max-Age", "86400");

            MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return ret;
        }

        std::string load_file(const std::string &filename)
        {
            std::ifstream file(filename, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Cannot load file: " + filename);
            }

            return std::string(std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>());
        }
    };

} // namespace anoverif

// Thread-local storage definitions
thread_local Json::StreamWriterBuilder anoverif::AnonymizationServer::json_builder_;
thread_local Json::Reader anoverif::AnonymizationServer::json_reader_;

// Global server instance for signal handling
std::unique_ptr<anoverif::AnonymizationServer> g_server;

void signal_handler(int sig)
{
    std::cout << "\nReceived signal " << sig << ", shutting down gracefully..." << std::endl;
    if (g_server)
    {
        g_server->print_stats();
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    try
    {
        // Load configuration
        anoverif::Config config = anoverif::Config::load();

        // Override with command line arguments
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            {
                config.port = std::stoi(argv[++i]);
            }
            else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
            {
                config.backend_api_url = argv[++i];
            }
            else if (strcmp(argv[i], "--ssl") == 0)
            {
                config.enable_ssl = true;
            }
            else if (strcmp(argv[i], "--help") == 0)
            {
                std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
                std::cout << "Options:" << std::endl;
                std::cout << "  --port PORT        HTTP port (default: 8080)" << std::endl;
                std::cout << "  --backend URL      Backend API URL" << std::endl;
                std::cout << "  --ssl              Enable HTTPS" << std::endl;
                std::cout << "  --help             Show this help" << std::endl;
                return 0;
            }
        }

        std::cout << "Anoverif - Anonymous Verification Server" << std::endl;
        std::cout << "Configuration:" << std::endl;
        std::cout << "  HTTP Port: " << config.port << std::endl;
        std::cout << "  HTTPS Port: " << config.ssl_port << std::endl;
        std::cout << "  Backend API: " << config.backend_api_url << std::endl;
        std::cout << "  SSL Enabled: " << (config.enable_ssl ? "Yes" : "No") << std::endl;
        std::cout << "  Max Connections: " << config.max_connections << std::endl;
        std::cout << "  Thread Pool Size: " << config.thread_pool_size << std::endl;

        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // Create and start server
        g_server = std::make_unique<anoverif::AnonymizationServer>(config);

        if (!g_server->start())
        {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }

        std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

        // Keep the server running
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
