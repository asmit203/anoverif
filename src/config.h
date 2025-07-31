#pragma once

#include <string>

namespace anoverif
{

    struct Config
    {
        // Server configuration
        int port = 8080;
        int ssl_port = 8443;
        std::string bind_address = "0.0.0.0";

        // SSL/TLS configuration
        std::string ssl_cert_file = "server.crt";
        std::string ssl_key_file = "server.key";
        bool enable_ssl = false;

        // Backend API configuration
        std::string backend_api_url = "http://localhost:9090/verify";
        int backend_timeout_ms = 5000;

        // Performance settings
        int max_connections = 1000;
        int thread_pool_size = 0; // 0 = auto-detect based on CPU cores
        int connection_timeout = 30;

        // Hash salt for additional security
        std::string hash_salt = "";

        // Load configuration from file or environment
        static Config load();
        static Config load_from_file(const std::string &filename);
        void save_to_file(const std::string &filename) const;
    };

} // namespace anoverif
