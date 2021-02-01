#include <dmc.bios/dmc.bios.hpp>

namespace eosiosystem {

void boot_contract::setstake(uint64_t stake)
{
    _gstate.total_activated_stake = stake;
}

} /// eosio.bios

EOSIO_ABI(eosiosystem::boot_contract,
    // native.hpp (newaccount definition is actually in eosio.system.cpp)
    (newaccount)(updateauth)(deleteauth)(linkauth)(unlinkauth)(canceldelay)(onerror)
    // eosio.system.cpp
    (setram)(setramrate)(setparams)(setpriv)(rmvproducer)(bidname)
    // delegate_bandwidth.cpp
    (buyrambytes)(buyram)(sellram)(delegatebw)(undelegatebw)(refund)
    // voting.cpp
    (regproducer)(unregprod)(voteproducer)(regproxy)
    // producer_pay.cpp
    (onblock)(claimrewards)
    // setstake
    (setstake)

)
