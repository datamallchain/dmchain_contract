#pragma once
#include <string>

uint8_t from_hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    eosio_assert(false, "Invalid hex character");
    return 0;
}

size_t from_hex(std::string& hex_str, char* out_data, size_t out_data_len)
{
    if (hex_str.length() % 2)
        hex_str.insert(0, "0");
    auto i = hex_str.begin();
    uint8_t* out_pos = (uint8_t*)out_data;
    uint8_t* out_end = out_pos + out_data_len;
    while (i != hex_str.end() && out_end != out_pos) {
        *out_pos = from_hex((char)(*i)) << 4;
        ++i;
        if (i != hex_str.end()) {
            *out_pos |= from_hex((char)(*i));
            ++i;
        }
        ++out_pos;
    }
    return out_pos - (uint8_t*)out_data;
}

checksum256 hex_to_sha256(std::string hex_str)
{
    eosio_assert(hex_str.length() <= 64, "invalid sha256");
    auto diff = 64 - hex_str.length();
    if (diff > 0) {
        hex_str.insert(0, diff, '0');
    }
    checksum256 checksum;
    from_hex(hex_str, (char*)checksum.hash, sizeof(checksum.hash));
    return checksum;
}

std::string to_hex(const char* d, uint32_t s)
{
    std::string r;
    const char* to_hex = "0123456789abcdef";
    uint8_t* c = (uint8_t*)d;
    for (uint32_t i = 0; i < s; ++i)
        (r += to_hex[(c[i] >> 4)]) += to_hex[(c[i] & 0x0f)];
    return r;
}

std::string sha256_to_hex(const checksum256& sha256)
{
    return to_hex((char*)sha256.hash, sizeof(sha256.hash));
}

uint64_t hexstring_to_int(std::string hexstring)
{
    char* p;
    uint64_t n = strtoull(hexstring.c_str(), &p, 16);
    return n;
}

uint128_t hexstring_to_int128(std::string hexstring)
{
    eosio_assert(hexstring.length() <= 64, "invalid uint128");
    unsigned char buf[32];
    auto size = from_hex(hexstring, (char*)buf, sizeof(buf));
    uint128_t value = 0;
    for (int i = 0; i < size; i++) {
        value = (value << 8) + buf[i];
    }
    return value;
}

std::string parse_checksum256_to_string(checksum256 value)
{
    std::string result;
    for (size_t i = 0; i < 32; i++) {
        result.append(1, value.hash[i] & 0xFF);
    }
    return result;
}

template <typename T>
std::string parse_int256_to_string(T value)
{
    const size_t byte_length = sizeof(T) / sizeof(char);
    std::string result;
    for (size_t i = 0; i < 32; i++) {
        result.append(1, value & 0xFF);
        value = value >> 8;
    }
    std::string reserve_result;
    reserve_result.insert(reserve_result.end(), result.rbegin(), result.rend());
    return reserve_result;
}

template <typename T>
std::string parse_number_to_string(T value)
{
    const size_t byte_length = sizeof(T) / sizeof(char);
    std::string result;
    for (size_t i = 0; i < byte_length; i++) {
        result.append(1, value & 0xFF);
        value = value >> 8;
    }
    std::string reserve_result;
    reserve_result.insert(reserve_result.end(), result.rbegin(), result.rend());
    return reserve_result;
}

void assign_checksum256(checksum256& out, const checksum256& value)
{
    for (int i = 0; i < 32; i++) {
        out.hash[i] = value.hash[i];
    }
}

void assign_signature(signature& out, const signature& value)
{
    for (int i = 0; i < 66; i++) {
        out.data[i] = value.data[i];
    }
}

bool is_equal_checksum256(const checksum256& a, const checksum256& b)
{
    for (int i = 0; i < 32; i++) {
        if (a.hash[i] != b.hash[i])
            return false;
    }
    return true;
}

bool is_equal_public_key(const public_key& a, const public_key& b)
{
    for (int i = 0; i < sizeof(a); i++) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

eosio::bytes sha256_to_bytes(const checksum256& sha256)
{
    return std::vector<char>(&sha256.hash[0], &sha256.hash[0] + 32);
}

bool is_hex(const char& ch)
{
    const char* hex_base = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
        if (hex_base[i] == ch)
            return true;
    return false;
}

template <typename T>
checksum256 sha256(const T& value)
{
    auto digest = pack(value);
    checksum256 hash;
    ::sha256(digest.data(), uint32_t(digest.size()), &hash);
    return hash;
}