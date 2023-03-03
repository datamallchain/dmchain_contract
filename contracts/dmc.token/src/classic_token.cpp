/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

token::token(name receiver, name code, datastream<const char*> ds)
    : contract(receiver, code, ds)
{
    dmc_global dmc_global_tbl(get_self(), get_self().value);
    auto destory_iter = dmc_global_tbl.find("olderbillid"_n.value);
    if (destory_iter == dmc_global_tbl.end()) {
        auto current_id_iter = dmc_global_tbl.find("billid"_n.value);
        if (current_id_iter != dmc_global_tbl.end()) {
            dmc_global_tbl.emplace(_self, [&](auto& conf) {
                conf.key = "olderbillid"_n;
                conf.value = current_id_iter->value;
            });
        }
    }
}

void token::create(name issuer,
    asset max_supply)
{
    asset a(0, max_supply.symbol);
    excreate(system_account, max_supply, a, time_point_sec());
}

void token::issue(name to, asset quantity, string memo)
{
    exissue(to, extended_asset(quantity, system_account), memo);
}

void token::retire(asset quantity, string memo)
{
    exretire(system_account, extended_asset(quantity, system_account), memo);
}

void token::transfer(name from, name to, asset quantity, string memo)
{
    extransfer(from, to, extended_asset(quantity, system_account), memo);
}

void token::close(name owner, const symbol& symbol)
{
    exclose(owner, extended_symbol(symbol, system_account));
}
}