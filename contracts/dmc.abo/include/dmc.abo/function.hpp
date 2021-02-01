#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/types.hpp>

#include <string>
#include <cmath>

namespace eosio {

template <typename T>
checksum256 sha256(const T& value)
{
    auto digest = pack(value);
    checksum256 hash;
    ::sha256(digest.data(), uint32_t(digest.size()), &hash);
    return hash;
}

std::string uint64_to_string(uint64_t value)
{
    std::string result;
    do {
        result += "0123456789"[value % 10];
        value /= 10;
    } while (value);
    std::reverse(result.begin(), result.end());
    return result;
}

uint64_t string_to_uint64(string value)
{
    char* end;
    uint64_t n = strtoull(value.c_str(), &end, 10);
    return n;
}

std::tuple<std::string, std::string> find_keyword(std::string value, const char* mathcher)
{
    eosio_assert(!value.empty(), "Invalid memo");

    auto pos = value.find(mathcher);
    eosio_assert(pos != std::string::npos, "Invalid memo");

    std::string find_string = value.substr(0, pos);
    eosio_assert(!find_string.empty(), "Invalid memo");

    return std::make_tuple(find_string, value.substr(pos + 1));
}
}
