#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::nftcreatesym(extended_symbol nft_symbol, std::string symbol_uri, nft_type type)
{
    require_auth(nft_symbol.contract);
    eosio_assert(type > NftTypeBegin && type < NftTypeEnd, "invalid nft type");
    eosio_assert(nft_symbol.precision() == 0, "invalid precision");

    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(nft_symbol));
    eosio_assert(symbol_iter == symbol_idx.end(), "symbol already exists");

    auto symbol_id = nft_symbol_tbl.available_primary_key();
    nft_symbol_tbl.emplace(nft_symbol.contract, [&](auto& n) {
        n.symbol_id = symbol_id;
        n.nft_symbol = nft_symbol;
        n.symbol_uri = symbol_uri;
        n.type = type;
    });
    SEND_INLINE_ACTION(*this, nftsymrec, { _self, N(active) }, { symbol_id, nft_symbol, symbol_uri, type });
}

void token::nftcreate(name to, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity)
{
    require_auth(quantity.contract);
    eosio_assert(is_account(to), "to account no exists!");
    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(quantity.get_extended_symbol()));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    if (symbol_iter->type == ERC721) {
        eosio_assert(quantity.amount == 1, "721 can only issue 1");
    }

    nft_infos nft_info_tbl(_self, symbol_iter->symbol_id);
    auto nft_id = nft_info_tbl.available_primary_key();
    nft_info_tbl.emplace(quantity.contract, [&](auto& n) {
        n.nft_id = nft_id;
        n.nft_uri = nft_uri;
        n.nft_name = nft_name;
        n.extra_data = extra_data;
        n.supply = quantity;
    });

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    nft_balance_tbl.emplace(quantity.contract, [&](auto& n) {
        n.primary = nft_balance_tbl.available_primary_key();
        n.owner = to;
        n.nft_id = nft_id;
        n.quantity = quantity;
    });
    SEND_INLINE_ACTION(*this, nftrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, nft_uri, nft_name, extra_data, quantity });
    SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, to, quantity });
}

void token::nftissue(name to, uint64_t nft_id, extended_asset quantity)
{
    require_auth(quantity.contract);
    eosio_assert(is_account(to), "to account no exists!");
    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(quantity.get_extended_symbol()));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    nft_infos nft_info_tbl(_self, symbol_iter->symbol_id);
    auto nft_iter = nft_info_tbl.find(nft_id);
    eosio_assert(nft_iter != nft_info_tbl.end(), "nft not exists");
    if (symbol_iter->type == ERC721) {
        eosio_assert(nft_iter->supply.amount == 0, "invalid issue amount");
        eosio_assert(quantity.amount == 1, "invalid issue amount");
    }
    nft_info_tbl.modify(nft_iter, quantity.contract, [&](auto& n) {
        n.supply += quantity;
    });

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    auto owner_id_idx = nft_balance_tbl.get_index<N(ownerid)>();
    auto owner_id_iter = owner_id_idx.find(nft_balance::get_owner_id(to, nft_id));
    auto user_quant = quantity;
    if (owner_id_iter == owner_id_idx.end()) {
        nft_balance_tbl.emplace(quantity.contract, [&](auto& n) {
            n.primary = nft_balance_tbl.available_primary_key();
            n.owner = to;
            n.nft_id = nft_id;
            n.quantity = quantity;
        });
    } else {
        user_quant += owner_id_iter->quantity;
        owner_id_idx.modify(owner_id_iter, quantity.contract, [&](auto& n) {
            n.quantity += quantity;
        });
    }
    SEND_INLINE_ACTION(*this, nftrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, nft_iter->nft_uri, nft_iter->nft_name, nft_iter->extra_data, nft_iter->supply });
    SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, to, user_quant });
}

void token::nfttransfer(name from, name to, uint64_t nft_id, extended_asset quantity, std::string memo)
{
    require_auth(from);
    eosio_assert(memo.size() <= 512, "memo has more than 512 bytes");
    eosio_assert(is_account(to), "to account no exists!");

    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(quantity.get_extended_symbol()));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    auto owner_id_idx = nft_balance_tbl.get_index<N(ownerid)>();
    auto from_iter = owner_id_idx.find(nft_balance::get_owner_id(from, nft_id));
    eosio_assert(from_iter != owner_id_idx.end(), "not enough asset");
    eosio_assert(from_iter->quantity >= quantity, "not enough asset");
    owner_id_idx.modify(from_iter, from, [&](auto& n) {
        n.quantity -= quantity;
    });

    auto to_iter = owner_id_idx.find(nft_balance::get_owner_id(to, nft_id));
    auto to_quant = quantity;
    if (to_iter == owner_id_idx.end()) {
        nft_balance_tbl.emplace(from, [&](auto& n) {
            n.primary = nft_balance_tbl.available_primary_key();
            n.owner = to;
            n.nft_id = nft_id;
            n.quantity = quantity;
        });
    } else {
        to_quant += to_iter->quantity;
        owner_id_idx.modify(to_iter, from, [&](auto& n) {
            n.quantity += quantity;
        });
    }
    SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, from, from_iter->quantity });
    SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, to, to_quant });
}

void token::nfttransferb(name from, name to, std::vector<nft_batch_args> batch_args, std::string memo)
{
    require_auth(from);
    eosio_assert(memo.size() <= 512, "memo has more than 512 bytes");
    eosio_assert(is_account(to), "to account no exists!");

    eosio_assert(batch_args.size(), "invalid batch_args size");
    extended_symbol ext_sym = batch_args[0].quantity.get_extended_symbol();

    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(ext_sym));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    auto owner_id_idx = nft_balance_tbl.get_index<N(ownerid)>();

    for (auto iter = batch_args.begin(); iter != batch_args.end(); iter++) {
        auto quantity = iter->quantity;
        auto nft_id = iter->nft_id;
        eosio_assert(iter->quantity.get_extended_symbol() == ext_sym, "symbol mismatch");
        auto from_iter = owner_id_idx.find(nft_balance::get_owner_id(from, nft_id));
        eosio_assert(from_iter != owner_id_idx.end(), "not enough asset");
        eosio_assert(from_iter->quantity >= quantity, "not enough asset");
        owner_id_idx.modify(from_iter, from, [&](auto& n) {
            n.quantity -= quantity;
        });
        auto to_iter = owner_id_idx.find(nft_balance::get_owner_id(to, nft_id));
        auto to_quant = quantity;
        if (to_iter == owner_id_idx.end()) {
            nft_balance_tbl.emplace(from, [&](auto& n) {
                n.primary = nft_balance_tbl.available_primary_key();
                n.owner = to;
                n.nft_id = nft_id;
                n.quantity = quantity;
            });
        } else {
            to_quant += to_iter->quantity;
            owner_id_idx.modify(to_iter, from, [&](auto& n) {
                n.quantity += quantity;
            });
        }
        SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, from, from_iter->quantity });
        SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, to, to_quant });
    }
}

void token::nftburn(name from, uint64_t nft_id, extended_asset quantity)
{
    require_auth(from);
    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(quantity.get_extended_symbol()));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    auto owner_id_idx = nft_balance_tbl.get_index<N(ownerid)>();
    auto from_iter = owner_id_idx.find(nft_balance::get_owner_id(from, nft_id));
    eosio_assert(from_iter != owner_id_idx.end(), "not enough asset");
    eosio_assert(from_iter->quantity >= quantity, "not enough asset");
    owner_id_idx.modify(from_iter, from, [&](auto& n) {
        n.quantity -= quantity;
    });

    nft_infos nft_info_tbl(_self, symbol_iter->symbol_id);
    auto nft_iter = nft_info_tbl.find(nft_id);
    eosio_assert(nft_iter != nft_info_tbl.end(), "nft not exists");
    nft_info_tbl.modify(nft_iter, quantity.contract, [&](auto& n) {
        n.supply -= quantity;
    });

    SEND_INLINE_ACTION(*this, nftrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, nft_iter->nft_uri, nft_iter->nft_name, nft_iter->extra_data, nft_iter->supply });
    SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, from, from_iter->quantity });
}

void token::burnbatch(name from, std::vector<nft_batch_args> batch_args)
{
    require_auth(from);
    eosio_assert(batch_args.size(), "invalid batch_args size");
    extended_symbol ext_sym = batch_args[0].quantity.get_extended_symbol();

    nft_symbols nft_symbol_tbl(_self, _self);
    auto symbol_idx = nft_symbol_tbl.get_index<N(extsymbol)>();
    auto symbol_iter = symbol_idx.find(nft_symbol_info::get_extended_symbol(ext_sym));
    eosio_assert(symbol_iter != symbol_idx.end(), "symbol not exists");

    nft_infos nft_info_tbl(_self, symbol_iter->symbol_id);

    nft_balances nft_balance_tbl(_self, symbol_iter->symbol_id);
    auto owner_id_idx = nft_balance_tbl.get_index<N(ownerid)>();

    for (auto iter = batch_args.begin(); iter != batch_args.end(); iter++) {
        auto quantity = iter->quantity;
        auto nft_id = iter->nft_id;
        eosio_assert(iter->quantity.get_extended_symbol() == ext_sym, "symbol mismatch");

        auto from_iter = owner_id_idx.find(nft_balance::get_owner_id(from, nft_id));
        eosio_assert(from_iter != owner_id_idx.end(), "not enough asset");
        eosio_assert(from_iter->quantity >= quantity, "not enough asset");
        owner_id_idx.modify(from_iter, from, [&](auto& n) {
            n.quantity -= quantity;
        });

        auto nft_iter = nft_info_tbl.find(nft_id);
        eosio_assert(nft_iter != nft_info_tbl.end(), "nft not exists");
        nft_info_tbl.modify(nft_iter, quantity.contract, [&](auto& n) {
            n.supply -= quantity;
        });

        SEND_INLINE_ACTION(*this, nftrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, nft_iter->nft_uri, nft_iter->nft_name, nft_iter->extra_data, nft_iter->supply });
        SEND_INLINE_ACTION(*this, nftaccrec, { _self, N(active) }, { symbol_iter->symbol_id, nft_id, from, from_iter->quantity });
    }
}
}