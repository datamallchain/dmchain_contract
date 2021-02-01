/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */
#pragma once
#include <string>

std::string uint32_to_string(uint32_t value)
{
    std::string result;
    do {
        result += "0123456789"[value % 10];
        value /= 10;
    } while (value);
    std::reverse(result.begin(), result.end());
    return result;
}