/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

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