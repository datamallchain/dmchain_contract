/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {
void token::exlock(name owner, extended_asset quantity, time_point_sec expiration, string memo)
{
    require_auth(owner);

    check(quantity.quantity.is_valid(), "invalid quantity");
    check(quantity.quantity.amount > 0, "must lock positive amount");

    check(memo.size() <= 256, "memo has more than 256 bytes");

    require_recipient(quantity.contract);

    extended_symbol quantity_sym = quantity.get_extended_symbol();
    check(quantity_sym != pst_sym && quantity_sym != rsi_sym, "pst and rsi are not allowed to be locked");

    sub_balance(owner, quantity);
    exchange_balance_to_lockbalance(owner, quantity, expiration,owner);
}

void token::exlocktrans(name from, name to, extended_asset quantity, time_point_sec expiration, time_point_sec expiration_to, string memo)
{
    check(from != to, "cannot transfer lock tokens to self");
    require_auth(from);

    check(is_account(to), "cannot transfer lock tokens to nonexist account");

    require_recipient(from);
    require_recipient(to);

    check(quantity.quantity.is_valid(), "invalid quantity when transfer lock tokens");
    check(quantity.quantity.amount > 0, "must transfer lock tokens in positive quantity");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    extended_symbol quantity_sym = quantity.get_extended_symbol();
    check(quantity_sym != pst_sym && quantity_sym != rsi_sym, "pst and rsi are not allowed to locktrans");

    if (time_point_sec(current_time_point()) < expiration) {
        check(expiration_to >= expiration, "expiration_to must longer than expiration");
    }

    if (expiration_to == time_point_sec())
        lock_sub_balance(from, quantity, true);
    else
        lock_sub_balance(from, quantity, expiration);

    lock_add_balance(to, quantity, expiration_to, from);
}

void token::exunlock(name owner, extended_asset quantity, time_point_sec expiration, string memo)
{
    require_auth(owner);

    check(quantity.quantity.is_valid(), "invalid quantity");
    check(quantity.quantity.amount > 0, "must unlock positive amount");

    check(time_point_sec(current_time_point()) >= expiration, "under expiration time");

    check(memo.size() <= 256, "memo has more than 256 bytes");

    require_recipient(quantity.contract);

    stats statstable(_self, quantity.contract.value);
    const auto& st = statstable.get(quantity.quantity.symbol.code().raw(), "token with symbol does not exist");

    lock_sub_balance(owner, quantity, expiration);
    add_balance(owner, quantity, owner);

    statstable.modify(st, get_self(), [&](auto& s) {
        s.reserve_supply -= quantity.quantity;
        s.supply += quantity.quantity;
    });
}

void token::lock_sub_balance(name owner, extended_asset value, time_point_sec expiration)
{
    lock_accounts from_acnts(_self, owner.value);
    auto from_iter = from_acnts.get_index<"byextendedas"_n>();
    auto from = from_iter.find(lock_account::key(value.get_extended_symbol(), expiration));

    check(from != from_iter.end(), "no such lock tokens");
    check(from->balance.quantity.amount >= value.quantity.amount, "overdrawn balance when sub lock balance");
    check(from->balance.get_extended_symbol() == value.get_extended_symbol(), "symbol precision mismatch");

    if (from->balance.quantity.amount == value.quantity.amount) {
        from_iter.erase(from);
    } else {
        from_iter.modify(from, get_self(), [&](auto& a) {
            a.balance -= value;
        });
    }
}

void token::lock_add_balance(name owner, extended_asset value, time_point_sec expiration, name ram_payer)
{
    lock_accounts to_acnts(_self, owner.value);
    auto to_iter = to_acnts.get_index<"byextendedas"_n>();
    auto to = to_iter.find(lock_account::key(value.get_extended_symbol(), expiration));

    if (to == to_iter.end()) {
        to_acnts.emplace(ram_payer, [&](auto& a) {
            a.primary = to_acnts.available_primary_key();
            a.balance = value;
            a.lock_timestamp = expiration;
        });
    } else {
        to_iter.modify(to, get_self(), [&](auto& a) {
            a.balance += value;
        });
    }
}

void token::lock_sub_balance(name foundation, extended_asset quantity, bool recur)
{
    lock_accounts from_acnts(_self, foundation.value);

    auto from_iter = from_acnts.get_index<"byextendedas"_n>();
    auto from = from_iter.lower_bound(lock_account::key(quantity.get_extended_symbol(), time_point_sec()));

    check(quantity.get_extended_symbol() == from->balance.get_extended_symbol(), "symbol precision mismatch");
    while (quantity.quantity.amount > 0) {
        check(from != from_iter.end(), "overdrawn balance when lock_sub");

        if (recur)
            check(time_point_sec(current_time_point()) >= from->lock_timestamp, "under expiration time");

        quantity -= from->balance;
        if (quantity.quantity.amount >= 0) {
            from = from_iter.erase(from);
        } else {
            from_iter.modify(from, get_self(), [&](auto& a) {
                a.balance.quantity.amount = std::abs(quantity.quantity.amount);
            });
        }
    }
}

void token::exchange_balance_to_lockbalance(name owner, extended_asset value, time_point_sec lock_timestamp, name ram_payer)
{
    stats statstable(_self, value.contract.value);
    auto st = statstable.find(value.get_extended_symbol().get_symbol().code().raw());
    check(st != statstable.end(), "token with symbol does not exist");

    lock_add_balance(owner, value, lock_timestamp, ram_payer);
    statstable.modify(st, get_self(), [&](auto& s) {
        if (s.reserve_supply.symbol != s.supply.symbol)
            s.reserve_supply = value.quantity;
        else
            s.reserve_supply += value.quantity;
        s.supply -= value.quantity;
    });
}

extended_asset token::get_balance(extended_asset quantity, name name)
{
    accounts acnts(_self, name.value);

    auto iter = acnts.get_index<"byextendedas"_n>();
    auto it = iter.find(account::key(quantity.get_extended_symbol()));

    if (it == iter.end())
        return extended_asset(0, quantity.get_extended_symbol());

    check(it->balance.quantity.symbol == quantity.quantity.symbol, "symbol precision mismatch");

    return it->balance;
}
}