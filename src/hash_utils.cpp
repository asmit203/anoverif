#include "hash_utils.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <cstring>
#include <memory>

namespace anoverif
{

    const std::array<char, 16> HashUtils::hex_chars = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    std::string HashUtils::sha256_hash(const std::string &input)
    {
        // Use OpenSSL's EVP interface for better performance
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(
            EVP_MD_CTX_new(), EVP_MD_CTX_free);

        if (!ctx)
        {
            throw std::runtime_error("Failed to create EVP context");
        }

        if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        {
            throw std::runtime_error("Failed to initialize SHA256");
        }

        if (EVP_DigestUpdate(ctx.get(), input.c_str(), input.length()) != 1)
        {
            throw std::runtime_error("Failed to update hash");
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        unsigned int hash_len;

        if (EVP_DigestFinal_ex(ctx.get(), hash, &hash_len) != 1)
        {
            throw std::runtime_error("Failed to finalize hash");
        }

        return to_hex(hash, hash_len);
    }

    std::string HashUtils::to_hex(const unsigned char *data, size_t length)
    {
        std::string result;
        result.reserve(length * 2);

        for (size_t i = 0; i < length; ++i)
        {
            result += hex_chars[data[i] >> 4];
            result += hex_chars[data[i] & 0x0F];
        }

        return result;
    }

} // namespace anoverif
