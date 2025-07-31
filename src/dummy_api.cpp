#include <microhttpd.h>
#include <json/json.h>
#include <iostream>
#include <random>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstring>

namespace anoverif
{

    class DummyApiServer
    {
    private:
        struct MHD_Daemon *daemon_;
        int port_;
        std::atomic<bool> running_;

        // Simulate a database of "valid" idvals (not hashes!)
        std::unordered_set<std::string> valid_hashes_; // TODO: rename to valid_idvals_
        std::mutex hashes_mutex_;

        // Random number generator for simulation
        std::random_device rd_;
        std::mt19937 gen_;
        std::uniform_int_distribution<> dis_;

        // Statistics
        std::atomic<uint64_t> request_count_;
        std::atomic<uint64_t> true_responses_;
        std::atomic<uint64_t> false_responses_;

    public:
        DummyApiServer(int port = 9090)
            : daemon_(nullptr), port_(port), running_(false),
              gen_(rd_()), dis_(0, 100),
              request_count_(0), true_responses_(0), false_responses_(0)
        {

            // Pre-populate some "valid" hashes for testing
            seed_valid_hashes();
        }

        ~DummyApiServer()
        {
            stop();
        }

        bool start()
        {
            running_ = true;

            daemon_ = MHD_start_daemon(
                MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
                port_,
                nullptr, nullptr,
                &request_handler, this,
                MHD_OPTION_CONNECTION_LIMIT, 500,
                MHD_OPTION_CONNECTION_TIMEOUT, 30,
                MHD_OPTION_END);

            if (!daemon_)
            {
                std::cerr << "Failed to start dummy API server on port " << port_ << std::endl;
                return false;
            }

            std::cout << "Dummy API server started on port " << port_ << std::endl;
            std::cout << "Endpoints:" << std::endl;
            std::cout << "  POST /verify - Verify a hash" << std::endl;
            std::cout << "  GET /stats - Show server statistics" << std::endl;
            std::cout << "  GET /health - Health check" << std::endl;

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
        }

        void print_stats() const
        {
            std::cout << "Dummy API Statistics:" << std::endl;
            std::cout << "  Total Requests: " << request_count_.load() << std::endl;
            std::cout << "  True Responses: " << true_responses_.load() << std::endl;
            std::cout << "  False Responses: " << false_responses_.load() << std::endl;
            std::cout << "  Valid IDVals in DB: " << valid_hashes_.size() << std::endl;
        }

    private:
        void seed_valid_hashes()
        {
            // Add some predefined valid idvals for testing
            std::lock_guard<std::mutex> lock(hashes_mutex_);

            // These represent "valid" identifiers that would be verified as true
            valid_hashes_.insert("user123");
            valid_hashes_.insert("admin456");
            valid_hashes_.insert("test_user");
            valid_hashes_.insert("valid_id_001");
            valid_hashes_.insert("authorized_user");
            valid_hashes_.insert("premium_member");
            valid_hashes_.insert("verified_account");

            // Add some random valid idvals
            for (int i = 0; i < 50; i++)
            {
                std::string idval = generate_random_idval();
                valid_hashes_.insert(idval);
            }
        }

        std::string generate_random_idval()
        {
            const char *chars = "abcdefghijklmnopqrstuvwxyz0123456789";
            std::string idval = "user_";

            for (int i = 0; i < 8; i++)
            {
                idval += chars[dis_(gen_) % 36];
            }

            return idval;
        }

        static MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                                          const char *url, const char *method,
                                          const char *version, const char *upload_data,
                                          size_t *upload_data_size, void **con_cls)
        {

            auto *server = static_cast<DummyApiServer *>(cls);
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

            // Handle different endpoints
            if (strcmp(url, "/health") == 0 && strcmp(method, "GET") == 0)
            {
                return handle_health_check(connection);
            }

            if (strcmp(url, "/stats") == 0 && strcmp(method, "GET") == 0)
            {
                return handle_stats(connection);
            }

            if (strcmp(url, "/verify") == 0 && strcmp(method, "POST") == 0)
            {
                return handle_verify_request(connection, upload_data, upload_data_size, con_cls);
            }

            return send_error_response(connection, 404, "Not Found");
        }

        MHD_Result handle_verify_request(struct MHD_Connection *connection,
                                         const char *upload_data, size_t *upload_data_size,
                                         void **con_cls)
        {

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

            // Process the verification request
            std::string response = process_verify_request(*post_data);

            delete post_data;
            *con_cls = nullptr;

            return send_json_response(connection, response);
        }

        std::string process_verify_request(const std::string &request_data)
        {
            try
            {
                // Parse JSON request
                Json::Value root;
                Json::Reader reader;

                if (!reader.parse(request_data, root))
                {
                    return create_error_response("Invalid JSON");
                }

                if (!root.isMember("idval") || !root["idval"].isString())
                {
                    return create_error_response("Missing or invalid 'idval' parameter");
                }

                std::string idval = root["idval"].asString();

                // Simulate processing delay (1-10ms)
                std::this_thread::sleep_for(std::chrono::milliseconds(dis_(gen_) % 10 + 1));

                // Check if idval is in our "database" or use probabilistic response
                bool result = is_idval_valid(idval);

                if (result)
                {
                    true_responses_++;
                }
                else
                {
                    false_responses_++;
                }

                // Create response (note: we return result but don't expose the idval for privacy)
                Json::Value response;
                response["result"] = result;
                response["verified"] = result; // Additional field for clarity
                response["timestamp"] = static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
                response["processing_time_ms"] = dis_(gen_) % 10 + 1;

                Json::StreamWriterBuilder builder;
                return Json::writeString(builder, response);
            }
            catch (const std::exception &e)
            {
                return create_error_response(std::string("Processing error: ") + e.what());
            }
        }

        bool is_idval_valid(const std::string &idval)
        {
            std::lock_guard<std::mutex> lock(hashes_mutex_);

            // Check if idval is in our predefined valid set
            if (valid_hashes_.find(idval) != valid_hashes_.end())
            {
                return true;
            }

            // Otherwise, use probabilistic response (30% chance of true)
            return dis_(gen_) < 30;
        }

        MHD_Result handle_health_check(struct MHD_Connection *connection)
        {
            Json::Value response;
            response["status"] = "healthy";
            response["uptime_seconds"] = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            response["requests_processed"] = static_cast<int64_t>(request_count_.load());

            Json::StreamWriterBuilder builder;
            std::string json = Json::writeString(builder, response);

            return send_json_response(connection, json);
        }

        MHD_Result handle_stats(struct MHD_Connection *connection)
        {
            Json::Value response;
            response["total_requests"] = static_cast<int64_t>(request_count_.load());
            response["true_responses"] = static_cast<int64_t>(true_responses_.load());
            response["false_responses"] = static_cast<int64_t>(false_responses_.load());
            response["valid_hashes_count"] = static_cast<int64_t>(valid_hashes_.size());

            if (request_count_.load() > 0)
            {
                response["true_percentage"] = static_cast<double>(true_responses_.load()) / request_count_.load() * 100.0;
            }
            else
            {
                response["true_percentage"] = 0.0;
            }

            Json::StreamWriterBuilder builder;
            std::string json = Json::writeString(builder, response);

            return send_json_response(connection, json);
        }

        std::string create_error_response(const std::string &message)
        {
            Json::Value response;
            response["error"] = message;
            response["result"] = false;
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
            MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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
            MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
            MHD_add_response_header(response, "Access-Control-Max-Age", "86400");

            MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);

            return ret;
        }
    };

} // namespace anoverif

// Global server instance for signal handling
std::unique_ptr<anoverif::DummyApiServer> g_server;

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
    int port = 9090;

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --port PORT        Server port (default: 9090)" << std::endl;
            std::cout << "  --help             Show this help" << std::endl;
            return 0;
        }
    }

    try
    {
        std::cout << "Dummy API Server for Anoverif Testing" << std::endl;
        std::cout << "Port: " << port << std::endl;

        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // Create and start server
        g_server = std::make_unique<anoverif::DummyApiServer>(port);

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
