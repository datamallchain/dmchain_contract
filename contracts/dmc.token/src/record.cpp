/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::outreceipt(name owner, extended_asset x, extended_asset y)
{
    require_auth(_self);
}

void token::traderecord(name owner, name oppo, extended_asset from, extended_asset to, extended_asset fee, uint64_t bid_id)
{
    require_auth(_self);
}

void token::pricerec(uint64_t old_price, uint64_t new_price)
{
    require_auth(_self);
}

void token::uniswapsnap(name owner, extended_asset quantity)
{
    require_auth(_self);
    require_recipient(owner);
}

void token::incentiverec(name owner, extended_asset inc, uint64_t bill_id)
{
    require_auth(_self);
}

void token::redeemrec(name owner, name miner, extended_asset asset)
{
    require_auth(_self);
}

void token::nftsymrec(uint64_t symbol_id, extended_symbol nft_symbol, std::string symbol_uri, nft_type type)
{
    require_auth(_self);
}

void token::nftrec(uint64_t symbol_id, uint64_t nft_id, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity)
{
    require_auth(_self);
}

void token::nftaccrec(uint64_t family_id, uint64_t nft_id, name owner, extended_asset quantity)
{
    require_auth(_self);
}

void token::liqrec(name miner, extended_asset pst_asset, extended_asset dmc_asset)
{
    require_auth(_self);
}

void token::billliqrec(name miner, uint64_t bill_id, extended_asset sub_pst)
{
    require_auth(_self);
}

void token::currliqrec(name miner, extended_asset sub_pst)
{
    require_auth(_self);
}

void token::orderrec(dmc_order order_info, uint8_t type)
{
    require_auth(_self);
}

void token::challengerec(dmc_challenge challenge_info)
{
    require_auth(_self);
}

void token::billsnap(bill_record bill_info)
{
    require_auth(_self);
}

void token::makerecord(dmc_maker maker_info)
{
    require_auth(_self);
}

void token::makerpoolrec(name miner, std::vector<maker_pool> pool_info)
{
    require_auth(_self);
}

void token::assetrec(uint64_t order_id, std::vector<extended_asset> changed, name owner, AssetReceiptType rec_type)
{
    require_auth(_self);
}

void token::orderassrec(uint64_t order_id, std::vector<asset_type_args> changed, name owner, AccountType acc_type, time_point_sec exec_date)
{
    require_auth(_self);
}

void token::makersnaprec(maker_snapshot maker_snapshot)
{
    require_auth(_self);
}

void token::dismakerec(uint64_t order_id, std::vector<asset_type_args> rewards, extended_asset total_sub, std::vector<distribute_maker_snapshot> distribute_info)
{
    require_auth(_self);
}

void token::allocrec(extended_asset quantity, AllocationType type)
{
    require_auth(_self);
}

void token::innerswaprec(extended_asset vrsi, extended_asset dmc)
{
    require_auth(_self);
}
}  // namespace eosio
