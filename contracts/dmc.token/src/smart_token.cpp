/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {
void token::excreate(name issuer, asset maximum_supply, asset reserve_supply, time_point_sec expiration)
{
    require_auth(issuer);

    check(maximum_supply.is_valid(), "invalid supply");
    check(maximum_supply.amount > 0, "maximum_supply must be positive");
    check(reserve_supply.is_valid(), "invalid reserve_supply");
    check(reserve_supply.symbol == maximum_supply.symbol, "reserve_supply symbol precision mismatch");
    check(reserve_supply.symbol.precision() >= minimum_token_precision, "token precision too small");

    if (issuer != dmc_account && issuer != system_account)
        check(expiration >= current_time_point(), "expiration must longer than now");
    else
        expiration = time_point_sec();

    stats statstable(_self, issuer.value);
    auto existing = statstable.find(maximum_supply.symbol.code().raw());
    check(existing == statstable.end(), "token with symbol already exists");

    check(reserve_supply.amount <= maximum_supply.amount, "invalid reserve_supply amount");
    check(reserve_supply.amount >= 0, "reserve_supply must be positive");
    lock_add_balance(get_foundation(issuer), extended_asset(reserve_supply, issuer), expiration, issuer);

    statstable.emplace(issuer, [&](auto& s) {
        s.issuer = issuer;
        s.max_supply = maximum_supply;
        s.supply.symbol = maximum_supply.symbol;
        s.reserve_supply = reserve_supply;
    });
}

void token::exissue(name to, extended_asset quantity, string memo)
{
    check(is_account(to), "to account does not exist");

    auto issuer = quantity.contract;

    auto foundation = issuer;
    if (!has_auth(issuer)) {
        foundation = get_foundation(issuer);
        check(issuer != foundation && has_auth(foundation), "only issuer and foundation can exissue");
    }

    check(memo.size() <= 256, "memo has more than 256 bytes");

    add_balance(foundation, quantity, foundation);

    if (to != foundation) {
        SEND_INLINE_ACTION(*this, extransfer, { foundation, "active"_n }, { foundation, to, quantity, memo });
    }

    if (quantity.get_extended_symbol() == pst_sym)
        change_pst(to, quantity);
    else
        add_stats(quantity);
}

void token::extransfer(name from, name to, extended_asset quantity, string memo)
{
    check(from != to, "cannot transfer to self");
    require_auth(from);

    if (to == "dmc.ramfee"_n || to == "dmc.saving"_n) {
        INLINE_ACTION_SENDER(eosio::token, exretire)
        ("dmc.token"_n, { { from, "active"_n } }, { from, quantity, std::string("exretire tokens for producer pay and savings") });
        return;
    }

    if (quantity.get_extended_symbol() == pst_sym) {
        check(from == system_account || from == dmc_account, "PST can not transfer");
    }

    if (quantity.get_extended_symbol() == rsi_sym) {
        check(from == system_account || from == dmc_account, "RSI can not transfer");
    }

    check(is_account(to), "to account does not exist");

    require_recipient(from);
    require_recipient(to);

    check(quantity.quantity.is_valid(), "invalid currency");
    check(quantity.quantity.amount > 0, "must transfer positive amount");

    check(memo.size() <= 256, "memo has more than 256 bytes");

    // producer pay
    auto payer = has_auth(to) ? to : from;

    sub_balance(from, quantity);
    add_balance(to, quantity, payer);
}

void token::exretire(name from, extended_asset quantity, string memo)
{
    require_auth(from);

    check(quantity.quantity.is_valid(), "invalid quantity");
    check(quantity.quantity.amount > 0, "must retire positive quantity");

    check(memo.size() <= 256, "memo has more than 256 bytes");

    auto issuer = quantity.contract;

    sub_balance(from, quantity);

    if (quantity.get_extended_symbol() == pst_sym) {
        change_pst(from, -quantity);
        dmc_makers maker_tbl(get_self(), get_self().value);
        auto iter = maker_tbl.find(from.value);
        if (iter != maker_tbl.end()) {
            maker_tbl.modify(iter, get_self(), [&](auto& m) {
                m.current_rate = cal_current_rate(iter->total_staked, from, iter->get_real_m());
            });
        }
    } else {
        sub_stats(quantity);
    }
}

void token::exclose(name owner, extended_symbol symbol)
{
    accounts acnts(_self, owner.value);
    auto it_iter = acnts.get_index<"byextendedas"_n>();
    auto it = it_iter.find(account::key(symbol));
    check(it != it_iter.end(), "Balance entry does not exist or already deleted. Action will not have any effects.");
    check(it->balance.quantity.amount == 0, "balance entry closed should be zero");
    it_iter.erase(it);
}

void token::sub_balance(name owner, extended_asset value)
{
    accounts from_acnts(_self, owner.value);

    auto from_iter = from_acnts.get_index<"byextendedas"_n>();
    auto from = from_iter.find(account::key(value.get_extended_symbol()));

    check(from != from_iter.end(), "no balance object found.");
    check(from->balance.quantity.amount >= value.quantity.amount, "overdrawn balance when sub balance");
    check(from->balance.quantity.symbol == value.quantity.symbol, "symbol precision mismatch");

    from_iter.modify(from, get_self(), [&](auto& a) {
        a.balance -= value;
    });
}

void token::add_balance(name owner, extended_asset value, name ram_payer)
{
    accounts to_acnts(_self, owner.value);
    auto to_iter = to_acnts.get_index<"byextendedas"_n>();
    auto to = to_iter.find(account::key(value.get_extended_symbol()));

    if (to == to_iter.end()) {
        to_acnts.emplace(ram_payer, [&](auto& a) {
            a.primary = to_acnts.available_primary_key();
            a.balance = value;
        });
    } else if (to->balance.quantity.amount == 0) {
        to_iter.modify(to, get_self(), [&](auto& a) {
            a.balance = value;
        });
    } else {
        to_iter.modify(to, get_self(), [&](auto& a) {
            a.balance += value;
        });
    }
}

// the lock parameter is used to delay notification to the system
void token::change_pst(name owner, extended_asset value, bool lock) 
{
    pststats pst_acnts(get_self(), get_self().value);
    auto st = pst_acnts.find(owner.value);
    if (st != pst_acnts.end()) {
        pst_acnts.modify(st, get_self(), [&](auto& i) {
            i.amount += value;
        });
    } else {
        st = pst_acnts.emplace(_self, [&](auto& i) {
            i.owner = owner;
            i.amount = value;
        });
    }

    check(st->amount.quantity.amount >= 0, "overdrawn balance when change PST");

    if (value.quantity.amount >= 0) {
        add_stats(value);
    } else {
        sub_stats(-value);
        if (lock) {
            lock_add_balance(owner, -value, time_point_sec(uint32_max), _self);
        }
    }

    send_totalvote_to_system(owner);
}

void token::send_totalvote_to_system(name owner) 
{
    pststats pst_acnts(get_self(), get_self().value);
    auto st = pst_acnts.find(owner.value);
    auto balance = st == pst_acnts.end() ? 0 : st->amount.quantity.amount;

    lock_accounts from_acnts(_self, owner.value);
    auto from_iter = from_acnts.get_index<"byextendedas"_n>();
    auto from = from_iter.find(lock_account::key(pst_sym, time_point_sec(uint32_max)));
    auto locked_balance_amount = from == from_iter.end() ? 0 : from->balance.quantity.amount;

    action({_self, "active"_n}, "dmc"_n, "settotalvote"_n,
           std::make_tuple(owner, balance + locked_balance_amount))
        .send();
}

void token::add_stats(extended_asset quantity)
{
    stats statstable(get_self(), quantity.contract.value);
    const auto& st = statstable.get(quantity.quantity.symbol.code().raw(), "token with symbol does not exist, create token before issue");

    check(quantity.quantity.is_valid(), "issue invalid currency");
    check(quantity.quantity.amount > 0, "must issue positive amount");
    check(quantity.quantity.symbol == st.supply.symbol, "symbol precision mismatch");

    check(quantity.quantity.amount <= st.max_supply.amount - st.supply.amount - st.reserve_supply.amount, "amount exceeds available supply when issue");
    statstable.modify(st, get_self(), [&](auto& s) {
        s.supply += quantity.quantity;
    });
}

void token::sub_stats(extended_asset quantity)
{
    stats statstable(get_self(), quantity.contract.value);
    const auto& st = statstable.get(quantity.quantity.symbol.code().raw(), "token with symbol does not exist");

    statstable.modify(st, get_self(), [&](auto& s) {
        s.supply -= quantity.quantity;
    });
}
}