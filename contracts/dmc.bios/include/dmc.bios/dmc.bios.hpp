/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */
#pragma once

#include <eosiolib/eosio.hpp>

#define private public
#include "../../../dmc.contracts/dmc.system/include/dmc.system/dmc.system.hpp"
#undef private

#define apply apply_old
#include "../../../dmc.contracts/dmc.system/src/dmc.system.cpp"
#undef apply

namespace eosiosystem {

class boot_contract : public system_contract {

public:
    boot_contract(account_name s)
        : system_contract(s){};

public:
    void setstake(uint64_t stake);
};

} /// eosiosystem
