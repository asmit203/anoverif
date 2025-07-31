#include "config.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <iostream>

namespace anoverif
{

    Config Config::load()
    {
        Config config;

        // Try to load from environment variables first
        const char *port_env = std::getenv("ANON_PORT");
        if (port_env)
        {
            config.port = std::stoi(port_env);
        }

        const char *ssl_port_env = std::getenv("ANON_SSL_PORT");
        if (ssl_port_env)
        {
            config.ssl_port = std::stoi(ssl_port_env);
        }

        const char *bind_env = std::getenv("ANON_BIND_ADDRESS");
        if (bind_env)
        {
            config.bind_address = bind_env;
        }

        const char *backend_env = std::getenv("ANON_BACKEND_URL");
        if (backend_env)
        {
            config.backend_api_url = backend_env;
        }

        const char *ssl_cert_env = std::getenv("ANON_SSL_CERT");
        if (ssl_cert_env)
        {
            config.ssl_cert_file = ssl_cert_env;
        }

        const char *ssl_key_env = std::getenv("ANON_SSL_KEY");
        if (ssl_key_env)
        {
            config.ssl_key_file = ssl_key_env;
        }

        const char *enable_ssl_env = std::getenv("ANON_ENABLE_SSL");
        if (enable_ssl_env)
        {
            config.enable_ssl = (std::string(enable_ssl_env) == "true" || std::string(enable_ssl_env) == "1");
        }

        const char *salt_env = std::getenv("ANON_HASH_SALT");
        if (salt_env)
        {
            config.hash_salt = salt_env;
        }

        // Auto-detect thread pool size based on CPU cores
        if (config.thread_pool_size == 0)
        {
            config.thread_pool_size = std::max(2u, std::thread::hardware_concurrency());
        }

        // Try to load from config file if it exists
        try
        {
            Config file_config = load_from_file("config.txt");
            // Merge configurations (environment takes precedence)
            if (!port_env)
                config.port = file_config.port;
            if (!ssl_port_env)
                config.ssl_port = file_config.ssl_port;
            if (!bind_env)
                config.bind_address = file_config.bind_address;
            if (!backend_env)
                config.backend_api_url = file_config.backend_api_url;
            if (!ssl_cert_env)
                config.ssl_cert_file = file_config.ssl_cert_file;
            if (!ssl_key_env)
                config.ssl_key_file = file_config.ssl_key_file;
            if (!enable_ssl_env)
                config.enable_ssl = file_config.enable_ssl;
            if (!salt_env)
                config.hash_salt = file_config.hash_salt;
        }
        catch (const std::exception &)
        {
            // Config file doesn't exist or has issues, use defaults
        }

        return config;
    }

    Config Config::load_from_file(const std::string &filename)
    {
        Config config;
        std::ifstream file(filename);

        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open config file: " + filename);
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream iss(line);
            std::string key, value;

            if (std::getline(iss, key, '=') && std::getline(iss, value))
            {
                if (key == "port")
                    config.port = std::stoi(value);
                else if (key == "ssl_port")
                    config.ssl_port = std::stoi(value);
                else if (key == "bind_address")
                    config.bind_address = value;
                else if (key == "backend_api_url")
                    config.backend_api_url = value;
                else if (key == "ssl_cert_file")
                    config.ssl_cert_file = value;
                else if (key == "ssl_key_file")
                    config.ssl_key_file = value;
                else if (key == "enable_ssl")
                    config.enable_ssl = (value == "true" || value == "1");
                else if (key == "max_connections")
                    config.max_connections = std::stoi(value);
                else if (key == "thread_pool_size")
                    config.thread_pool_size = std::stoi(value);
                else if (key == "connection_timeout")
                    config.connection_timeout = std::stoi(value);
                else if (key == "backend_timeout_ms")
                    config.backend_timeout_ms = std::stoi(value);
                else if (key == "hash_salt")
                    config.hash_salt = value;
            }
        }

        return config;
    }

    void Config::save_to_file(const std::string &filename) const
    {
        std::ofstream file(filename);

        if (!file.is_open())
        {
            throw std::runtime_error("Cannot create config file: " + filename);
        }

        file << "# Anoverif Configuration File\n";
        file << "port=" << port << "\n";
        file << "ssl_port=" << ssl_port << "\n";
        file << "bind_address=" << bind_address << "\n";
        file << "backend_api_url=" << backend_api_url << "\n";
        file << "ssl_cert_file=" << ssl_cert_file << "\n";
        file << "ssl_key_file=" << ssl_key_file << "\n";
        file << "enable_ssl=" << (enable_ssl ? "true" : "false") << "\n";
        file << "max_connections=" << max_connections << "\n";
        file << "thread_pool_size=" << thread_pool_size << "\n";
        file << "connection_timeout=" << connection_timeout << "\n";
        file << "backend_timeout_ms=" << backend_timeout_ms << "\n";
        file << "hash_salt=" << hash_salt << "\n";
    }

} // namespace anoverif
