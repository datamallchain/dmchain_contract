/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */

#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::receipt(extended_asset in, extended_asset out, extended_asset fee)
{
    require_auth(_self);
}

void token::outreceipt(account_name owner, extended_asset x, extended_asset y)
{
    require_auth(_self);
}

void token::traderecord(account_name owner, account_name oppo, extended_asset from, extended_asset to, extended_asset fee, uint64_t bid_id)
{
    require_auth(_self);
}

void token::orderchange(uint64_t bid_id, uint8_t state)
{
    require_auth(_self);
}

void token::bidrec(uint64_t price, extended_asset quantity, extended_asset filled, uint64_t bid_id)
{
    require_auth(_self);
}

void token::pricerec(uint64_t old_price, uint64_t new_price)
{
    require_auth(_self);
}

void token::uniswapsnap(account_name owner, extended_asset quantity)
{
    require_auth(_self);
    require_recipient(owner);
}

void token::billrec(account_name owner, extended_asset asset, uint64_t bill_id, uint8_t state)
{
    require_auth(_self);
}

void token::orderrec(account_name owner, account_name oppo, extended_asset sell, extended_asset buy, extended_asset reserve, uint64_t bill_id, uint64_t order_id)
{
    require_auth(_self);
}

void token::incentiverec(account_name owner, extended_asset inc, uint64_t bill_id, uint64_t order_id, uint8_t type)
{
    require_auth(_self);
}

void token::orderclarec(account_name owner, extended_asset quantity, uint64_t bill_id, uint64_t order_id)
{
    require_auth(_self);
}

void token::redeemrec(account_name owner, account_name miner, extended_asset asset)
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

void token::liqrec(account_name miner, extended_asset pst_asset, extended_asset dmc_asset)
{
    require_auth(_self);
}

void token::makerliqrec(account_name miner, uint64_t bill_id, extended_asset sub_pst)
{
    require_auth(_self);
}

void token::makercharec(account_name sender, account_name miner, extended_asset changed, MakerReceiptType type)
{
    require_auth(_self);
}

void token::ordercharec(uint64_t order_id, extended_asset storage, extended_asset lock, extended_asset settlement, extended_asset challenge, time_point_sec exec_date, OrderReceiptType type)
{
    require_auth(_self);
}

void token::assetcharec(account_name owner, extended_asset changed, uint8_t type, uint64_t order_id)
{
    require_auth(_self);
}
} /// namespace eosio
