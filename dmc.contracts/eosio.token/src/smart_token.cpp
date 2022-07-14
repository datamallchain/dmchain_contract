/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */

#include <eosio.token/eosio.token.hpp>

namespace eosio {
void token::excreate(account_name issuer, asset maximum_supply, asset reserve_supply, time_point_sec expiration)
{
    require_auth(issuer);

    eosio_assert(maximum_supply.is_valid(), "invalid supply");
    eosio_assert(maximum_supply.amount > 0, "maximum_supply must be positive");
    eosio_assert(reserve_supply.is_valid(), "invalid reserve_supply");
    eosio_assert(reserve_supply.symbol == maximum_supply.symbol, "reserve_supply symbol precision mismatch");
    eosio_assert(reserve_supply.symbol.precision() >= minimum_token_precision, "token precision too small");

    if (issuer != eos_account && issuer != system_account)
        eosio_assert(expiration >= time_point_sec(now()), "expiration must longer than now");
    else
        expiration = time_point_sec();

    stats statstable(_self, issuer);
    auto existing = statstable.find(maximum_supply.symbol.name());
    eosio_assert(existing == statstable.end(), "token with symbol already exists");

    eosio_assert(reserve_supply.amount <= maximum_supply.amount, "invalid reserve_supply amount");
    eosio_assert(reserve_supply.amount >= 0, "reserve_supply must be positive");
    lock_add_balance(get_foundation(issuer), extended_asset(reserve_supply, issuer), expiration, issuer);

    statstable.emplace(issuer, [&](auto& s) {
        s.issuer = issuer;
        s.max_supply = maximum_supply;
        s.supply.symbol = maximum_supply.symbol;
        s.reserve_supply = reserve_supply;
    });
}

void token::exissue(account_name to, extended_asset quantity, string memo)
{
    eosio_assert(is_account(to), "to account does not exist");

    auto issuer = quantity.contract;

    auto foundation = issuer;
    if (!has_auth(issuer)) {
        foundation = get_foundation(issuer);
        eosio_assert(issuer != foundation && has_auth(foundation), "only issuer and foundation can exissue");
    }

    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    add_stats(quantity);

    add_balance(foundation, quantity, foundation);

    if (to != foundation) {
        SEND_INLINE_ACTION(*this, extransfer, { foundation, N(active) }, { foundation, to, quantity, memo });
    }

    if (quantity.get_extended_symbol() == pst_sym)
        change_pst(to, quantity);
}

void token::extransfer(account_name from, account_name to, extended_asset quantity, string memo)
{
    eosio_assert(from != to, "cannot transfer to self");
    require_auth(from);

    if (to == N(eosio.ramfee) || to == N(eosio.saving)) {
        INLINE_ACTION_SENDER(eosio::token, exretire)
        (N(eosio.token), { { from, N(active) } }, { from, quantity, std::string("exretire tokens for producer pay and savings") });
        return;
    }

    if (quantity.get_extended_symbol() == pst_sym) {
        eosio_assert(from == system_account || from == eos_account, "pst can not transfer");
    }

    eosio_assert(is_account(to), "to account does not exist");

    require_recipient(from);
    require_recipient(to);

    eosio_assert(quantity.is_valid(), "invalid currency");
    eosio_assert(quantity.amount > 0, "must transfer positive amount");

    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    // producer pay
    auto payer = has_auth(to) ? to : from;

    sub_balance(from, quantity);
    add_balance(to, quantity, payer);
}

void token::exretire(account_name from, extended_asset quantity, string memo)
{
    require_auth(from);

    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must retire positive quantity");

    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    auto issuer = quantity.contract;

    sub_balance(from, quantity);

    sub_stats(quantity);

    if (quantity.get_extended_symbol() == pst_sym) {
        change_pst(from, -quantity);
        dmc_makers maker_tbl(_self, _self);
        auto iter = maker_tbl.find(from);
        if (iter != maker_tbl.end()) {
            maker_tbl.modify(iter, 0, [&](auto& m) {
                m.current_rate = cal_current_rate(iter->total_staked, from);
            });
        }
    }
}

void token::exclose(account_name owner, extended_symbol symbol)
{
    accounts acnts(_self, owner);
    auto it_iter = acnts.get_index<N(byextendedasset)>();
    auto it = it_iter.find(account::key(symbol));
    eosio_assert(it != it_iter.end(), "Balance entry does not exist or already deleted. Action will not have any effects.");
    eosio_assert(it->balance.amount == 0, "balance entry closed should be zero");
    it_iter.erase(it);
}

void token::sub_balance(account_name owner, extended_asset value)
{
    accounts from_acnts(_self, owner);

    auto from_iter = from_acnts.get_index<N(byextendedasset)>();
    auto from = from_iter.find(account::key(value.get_extended_symbol()));

    eosio_assert(from != from_iter.end(), "no balance object found.");
    eosio_assert(from->balance.amount >= value.amount, "overdrawn balance when sub balance");
    eosio_assert(from->balance.symbol == value.symbol, "symbol precision mismatch");

    from_iter.modify(from, 0, [&](auto& a) {
        a.balance -= value;
    });
}

void token::add_balance(account_name owner, extended_asset value, account_name ram_payer)
{
    accounts to_acnts(_self, owner);
    auto to_iter = to_acnts.get_index<N(byextendedasset)>();
    auto to = to_iter.find(account::key(value.get_extended_symbol()));

    if (to == to_iter.end()) {
        to_acnts.emplace(ram_payer, [&](auto& a) {
            a.primary = to_acnts.available_primary_key();
            a.balance = value;
        });
    } else if (to->balance.amount == 0) {
        to_iter.modify(to, 0, [&](auto& a) {
            a.balance = value;
        });
    } else {
        to_iter.modify(to, 0, [&](auto& a) {
            a.balance += value;
        });
    }
}

void token::change_pst(account_name owner, extended_asset value)
{
    pststats pst_acnts(_self, _self);
    auto st = pst_acnts.find(owner);
    if (st != pst_acnts.end()) {
        pst_acnts.modify(st, 0, [&](auto& i) {
            i.amount += value;
        });
    } else {
        st = pst_acnts.emplace(_self, [&](auto& i) {
            i.owner = owner;
            i.amount = value;
        });
    }
    eosio_assert(st->amount.amount >= 0, "overdrawn balance when change PST");

    //  TODO: ADD GLOBAL SET
    action({ _self, N(active) }, N(eosio), N(settotalvote),
        std::make_tuple(owner, st->amount.amount))
        .send();
}

void token::add_stats(extended_asset quantity)
{
    stats statstable(_self, quantity.contract);
    const auto& st = statstable.get(quantity.symbol.name(), "token with symbol does not exist, create token before issue");

    eosio_assert(quantity.is_valid(), "issue invalid currency");
    eosio_assert(quantity.amount > 0, "must issue positive amount");
    eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

    eosio_assert(quantity.amount <= st.max_supply.amount - st.supply.amount - st.reserve_supply.amount, "amount exceeds available supply when issue");
    statstable.modify(st, 0, [&](auto& s) {
        s.supply += quantity;
    });
}

void token::sub_stats(extended_asset quantity)
{
    stats statstable(_self, quantity.contract);
    const auto& st = statstable.get(quantity.symbol.name(), "token with symbol does not exist");

    statstable.modify(st, 0, [&](auto& s) {
        s.supply -= quantity;
    });
}
}