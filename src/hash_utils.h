#pragma once

#include <string>
#include <array>

namespace anoverif
{

    class HashUtils
    {
    public:
        // High-performance SHA-256 hashing optimized for our use case
        static std::string sha256_hash(const std::string &input);

        // Fast hex conversion using lookup table
        static std::string to_hex(const unsigned char *data, size_t length);

    private:
        // Pre-computed lookup table for hex conversion
        static const std::array<char, 16> hex_chars;
    };

} // namespace anoverif
