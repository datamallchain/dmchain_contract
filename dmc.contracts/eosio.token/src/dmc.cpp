#include <eosio.token/eosio.token.hpp>

namespace eosio {
void token::stake(account_name owner, extended_asset asset, double price, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    extended_symbol s_sym = asset.get_extended_symbol();
    eosio_assert(s_sym == pst_sym, "only proof of service token can be staked");
    eosio_assert(price >= 0 && price < std::pow(2, 32), "invaild price");

    stats statstable(_self, system_account);
    const auto& st = statstable.get(pst_sym.name(), "token with symbol does not exist"); // never happened

    uint64_t price_t = price * std::pow(2, 32);
    sub_balance(owner, asset);
    stake_stats sst(_self, owner);

    auto hash = sha256<stake_id_args>({ owner, asset, price_t, now(), memo });
    uint64_t stake_id = uint64_t(*reinterpret_cast<const uint64_t*>(&hash));

    sst.emplace(_self, [&](auto& r) {
        r.primary = sst.available_primary_key();
        r.stake_id = stake_id;
        r.owner = owner;
        r.unmatched = asset;
        r.matched = extended_asset(0, pst_sym);
        r.price = price_t;
        r.created_at = time_point_sec(now());
        r.updated_at = time_point_sec(now());
    });
}

void token::unstake(account_name owner, uint64_t stake_id, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    stake_stats sst(_self, owner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(stake_id);
    eosio_assert(ust != ust_idx.end(), "no such record");
    eosio_assert(ust->matched == extended_asset(0, pst_sym), "order in progress, can not unstake");
    calbonus(owner, stake_id, true, owner);
}

void token::order(account_name owner, account_name miner, uint64_t stake_id, extended_asset asset, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    eosio_assert(asset.get_extended_symbol() == pst_sym, "only proof of service token can be ordered");

    stake_stats sst(_self, miner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(stake_id);
    eosio_assert(ust != ust_idx.end(), "no such record");
    eosio_assert(ust->unmatched >= asset, "overdrawn balance");

    double price = (double)ust->price / std::pow(2, 32);
    double dmc_amount = price * asset.amount;
    extended_asset user_to_pay = get_asset_by_amount<double, std::ceil>(dmc_amount, dmc_sym);
    sub_balance(owner, user_to_pay);
    ust_idx.modify(ust, 0, [&](auto& s) {
        s.unmatched -= asset;
        s.matched += asset;
    });

    calbonus(miner, stake_id, false, owner);

    uint64_t claims_interval = get_dmc_config(name { N("claiminter") }, default_dmc_claims_interval);

    dmc_orders order_tbl(_self, _self);
    order_tbl.emplace(_self, [&](auto& o) {
        o.order_id = order_tbl.available_primary_key();
        o.user = owner;
        o.miner = miner;
        o.stake_id = stake_id;
        o.user_pledge = user_to_pay;
        o.miner_pledge = asset;
        o.state = OrderStart;
        o.create_date = time_point_sec(now());
        o.claim_date = time_point_sec(now());
        o.end_date = time_point_sec(now()) + claims_interval;
    });
}

void token::getincentive(account_name owner, uint64_t stake_id)
{
    require_auth(owner);
    calbonus(owner, stake_id, false, owner);
}

void token::calbonus(account_name owner, uint64_t stake_id, bool unstake, account_name ram_payer)
{
    stake_stats sst(_self, owner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(stake_id);
    eosio_assert(ust != ust_idx.end(), "no such record");

    stats statstable(_self, system_account);
    const auto& st = statstable.get(rsi_sym.name(), "real storage incentive does not exist"); // never happened

    auto now_time = time_point_sec(now());
    auto duration = now_time.sec_since_epoch() - ust->updated_at.sec_since_epoch();
    eosio_assert(duration <= now_time.sec_since_epoch(), "subtractive overflow"); // never happened
    extended_asset quantity = per_sec_bonus;
    quantity.amount *= duration;
    quantity.amount *= ust->unmatched.amount;
    eosio_assert(quantity.amount > 0, "dust attack detected");

    eosio_assert(quantity.amount <= st.max_supply.amount - st.supply.amount - st.reserve_supply.amount, "amount exceeds available supply when issue");
    statstable.modify(st, 0, [&](auto& s) {
        s.supply += quantity;
    });

    if (unstake) {
        add_balance(owner, ust->unmatched, ram_payer);
        ust_idx.erase(ust);
    } else {
        ust_idx.modify(ust, 0, [&](auto& r) {
            r.updated_at = now_time;
        });
    }
    add_balance(owner, quantity, ram_payer);
}

void token::setabostats(uint64_t stage, double user_rate, double foundation_rate, extended_asset total_release, time_point_sec start_at, time_point_sec end_at)
{
    require_auth(eos_account);
    eosio_assert(stage >= 1 && stage <= 11, "invaild stage");
    eosio_assert(user_rate <= 1 && user_rate >= 0, "invaild user_rate");
    eosio_assert(foundation_rate + user_rate == 1, "invaild foundation_rate");
    eosio_assert(start_at < end_at, "invaild time");
    eosio_assert(total_release.get_extended_symbol() == dmc_sym, "invaild symbol");
    bool set_now = false;
    auto now_time = time_point_sec(now());
    if (now_time > start_at)
        set_now = true;

    abostats ast(_self, _self);
    const auto& st = ast.find(stage);
    if (st != ast.end()) {
        ast.modify(st, 0, [&](auto& a) {
            a.user_rate = user_rate;
            a.foundation_rate = foundation_rate;
            a.total_release = total_release;
            a.start_at = start_at;
            a.end_at = end_at;
        });
    } else {
        ast.emplace(_self, [&](auto& a) {
            a.stage = stage;
            a.user_rate = user_rate;
            a.foundation_rate = foundation_rate;
            a.total_release = total_release;
            a.remaining_release = total_release;
            a.start_at = start_at;
            a.end_at = end_at;
            if (set_now)
                a.last_released_at = time_point_sec(now());
            else
                a.last_released_at = start_at;
        });
    }
}

void token::allocation(string memo)
{
    require_auth(eos_account);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    abostats ast(_self, _self);
    extended_asset to_foundation(0, dmc_sym);
    extended_asset to_user(0, dmc_sym);

    auto now_time = time_point_sec(now());
    for (auto it = ast.begin(); it != ast.end();) {
        if (now_time > it->end_at) {
            if (it->remaining_release.amount != 0) {
                to_foundation.amount = it->remaining_release.amount * it->foundation_rate;
                to_user.amount = it->remaining_release.amount * it->user_rate;
                ast.modify(it, 0, [&](auto& a) {
                    a.last_released_at = it->end_at;
                    a.remaining_release.amount = 0;
                });
            }
            it++;
        } else if (now_time < it->start_at) {
            break;
        } else {
            auto duration_time = now_time.sec_since_epoch() - it->last_released_at.sec_since_epoch();
            if (duration_time / 86400 == 0) // 24 * 60 * 60
                break;
            auto remaining_time = it->end_at.sec_since_epoch() - it->last_released_at.sec_since_epoch();
            double per = (double)duration_time / (double)remaining_time;
            if (per > 1)
                per = 1;
            uint64_t total_asset_amount = per * it->remaining_release.amount;
            if (total_asset_amount != 0) {
                to_foundation.amount = total_asset_amount * it->foundation_rate;
                to_user.amount = total_asset_amount * it->user_rate;
                ast.modify(it, 0, [&](auto& a) {
                    a.last_released_at = now_time;
                    a.remaining_release.amount -= total_asset_amount;
                });
            }
            break;
        }
    }

    if (to_foundation.amount != 0 && to_user.amount != 0) {
        SEND_INLINE_ACTION(*this, issue, { eos_account, N(active) }, { system_account, to_foundation, "allocation to foundation" });
        auto dueto = now_time.sec_since_epoch() + 24 * 60 * 60;
        string memo = uint32_to_string(dueto) + ";RSI@datamall";
        SEND_INLINE_ACTION(*this, issue, { eos_account, N(active) }, { abo_account, to_user, memo });
    }
}

void token::setdmcconfig(account_name key, uint64_t value)
{
    require_auth(_self);
    dmc_global dmc_global_tbl(_self, _self);
    auto config_itr = dmc_global_tbl.find(key);
    if (config_itr == dmc_global_tbl.end()) {
        dmc_global_tbl.emplace(_self, [&](auto& conf) {
            conf.key = key;
            conf.value = value;
        });
    } else {
        dmc_global_tbl.modify(config_itr, 0, [&](auto& conf) {
            conf.value = value;
        });
    }
}

uint64_t token::get_dmc_config(name key, uint64_t default_value)
{
    dmc_global dmc_global_tbl(_self, _self);
    auto dmc_global_iter = dmc_global_tbl.find(key);
    if (dmc_global_iter != dmc_global_tbl.end())
        return dmc_global_iter->value;
    return default_value;
}
} // namespace eosio
