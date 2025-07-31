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

#include "hash_utils.h"
#include "http_client.h"
#include "config.h"

namespace anoverif
{

    class AnonymizationServer
    {
    private:
        struct MHD_Daemon *daemon_;
        struct MHD_Daemon *ssl_daemon_;
        Config config_;
        std::unique_ptr<HttpClient> http_client_;
        std::atomic<bool> running_;

        // Performance monitoring
        std::atomic<uint64_t> request_count_;
        std::atomic<uint64_t> success_count_;
        std::atomic<uint64_t> error_count_;

        // Hash cache for improved performance (optional)
        std::unordered_map<std::string, std::string> hash_cache_;
        std::mutex cache_mutex_;

    public:
        AnonymizationServer(const Config &config)
            : daemon_(nullptr), ssl_daemon_(nullptr), config_(config), running_(false),
              request_count_(0), success_count_(0), error_count_(0)
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
            std::cout << "  Cache Size: " << hash_cache_.size() << std::endl;
        }

    private:
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
                *con_cls = new std::string();
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

            delete post_data;
            *con_cls = nullptr;

            return send_json_response(connection, response);
        }

        std::string process_verification_request(const std::string &request_data)
        {
            try
            {
                // Parse JSON request
                Json::Value root;
                Json::Reader reader;

                if (!reader.parse(request_data, root))
                {
                    error_count_++;
                    return create_error_response("Invalid JSON");
                }

                if (!root.isMember("idval") || !root["idval"].isString())
                {
                    error_count_++;
                    return create_error_response("Missing or invalid 'idval' parameter");
                }

                std::string idval = root["idval"].asString();

                if (idval.empty())
                {
                    error_count_++;
                    return create_error_response("Empty 'idval' parameter");
                }

                // Hash the sensitive data
                std::string hashed_value = hash_with_cache(idval);

                // Forward to backend API
                Json::Value backend_request;
                backend_request["hash"] = hashed_value;

                Json::StreamWriterBuilder builder;
                std::string backend_data = Json::writeString(builder, backend_request);

                auto response = http_client_->post(config_.backend_api_url, backend_data, "application/json");

                if (!response.success)
                {
                    error_count_++;
                    return create_error_response("Backend API unavailable");
                }

                // Parse backend response
                Json::Value backend_result;
                if (!reader.parse(response.body, backend_result))
                {
                    error_count_++;
                    return create_error_response("Invalid backend response");
                }

                success_count_++;

                // Return the result to client
                Json::Value client_response;
                client_response["success"] = true;
                client_response["result"] = backend_result.get("result", false).asBool();
                client_response["timestamp"] = static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());

                return Json::writeString(builder, client_response);
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

            // Cache the result (with size limit)
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                if (hash_cache_.size() < 10000)
                { // Limit cache size
                    hash_cache_[input] = hash;
                }
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

            Json::StreamWriterBuilder builder;
            return Json::writeString(builder, response);
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
