#include <eosio.token/eosio.token.hpp>
#include <eosio.token/utils.hpp>

#include "./classic_token.cpp"
#include "./smart_token.cpp"
#include "./smart_extend.cpp"
#include "./lock_account.cpp"
#include "./record.cpp"
#include "./uniswap.cpp"
#include "./dmc.cpp"
#include "./dmc_challenge.cpp"
#include "./dmc_deliver.cpp"

namespace eosio {

} /// namespace eosio

EOSIO_ABI(eosio::token,
    // classic tokens
    (create)(issue)(transfer)(close)(retire)
    // smart tokens
    (excreate)(exissue)(extransfer)(exclose)(exretire)(exdestroy)
    //
    (exchange)
    //
    (exunlock)(exlock)(exlocktrans)
    //
    (receipt)(outreceipt)(traderecord)(orderchange)(bidrec)(uniswapsnap)
    //
    (addreserves)(outreserves)
    //
    (stake)(unstake)(getincentive)(setabostats)(allocation)(order)
    //
    (addmerkle)(submitproof)(replyproof)(challenge)
    //
    (setdmcconfig)(claimorder)(reneworder)(cancelorder))