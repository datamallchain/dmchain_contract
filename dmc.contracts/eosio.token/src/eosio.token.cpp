/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */

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
#include "./nft.cpp"

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
    (bill)(unbill)(getincentive)(setabostats)(allocation)(order)
    //
    (billrec)(orderrec)(incentiverec)(orderclarec)
    //
    (increase)(redemption)(mint)(setmakerrate)
    //
    (addmerkle)(reqchallenge)(anschallenge)(arbitration)(paychallenge)
    //
    (liquidation)(liqrec)(makerliqrec)
    //
    (setdmcconfig)
    //
    (claimorder)(addordasset)(subordasset)(updateorder)
    //
    (makercharec)(ordercharec)(assetcharec)(ordermig)
    //
    (nftcreatesym)(nftcreate)(nftissue)(nfttransfer)(nfttransferb)(nftburn)(burnbatch)
    //
    (nftsymrec)(nftrec)(nftaccrec)
    //
    (cleanpst))
