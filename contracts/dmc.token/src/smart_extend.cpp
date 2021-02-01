/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::exdestroy(extended_symbol sym)
{
    check(sym.get_symbol().is_valid(), "invalid symbol name");

    require_auth(sym.get_contract());

    stats statstable(_self, sym.get_contract().value);
    const auto& st = statstable.get(sym.get_symbol().code().raw(), "token with symbol does not exist");

    if (st.supply.amount > 0) {
        sub_balance(st.issuer, extended_asset(st.supply, st.issuer));
    }

    if (st.reserve_supply.amount > 0) {
        lock_accounts from_acnts(_self, sym.get_contract().value);

        uint64_t balances = 0;
        for (auto it = from_acnts.begin(); it != from_acnts.end();) {
            if (it->balance.get_extended_symbol() == sym) {
                balances += it->balance.quantity.amount;
                it = from_acnts.erase(it);
            } else {
                it++;
            }
        }

        check(st.reserve_supply.amount == balances, "reserve_supply must all in issuer");
    }

    statstable.erase(st);
}

void token::exchange(name owner, extended_asset quantity, extended_asset to, double price, name id, string memo)
{
    require_auth(owner);
    check(quantity.quantity.is_valid() && to.quantity.is_valid(), "invalid exchange currency");
    check(memo.size() <= 256, "memo has more than 256 bytes");
    price = 0;
    auto from_sym = quantity.get_extended_symbol();
    auto to_sym = to.get_extended_symbol();

    uniswaporder(owner, quantity, to, price, id, owner);
}

} /// namespace eosio
