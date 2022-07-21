/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */

#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::bill(account_name owner, extended_asset asset, double price, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    extended_symbol s_sym = asset.get_extended_symbol();
    eosio_assert(s_sym == pst_sym, "only proof of service token can be billed");
    eosio_assert(price >= 0.0001 && price < std::pow(2, 32), "invaild price");
    eosio_assert(asset.amount > 0, "must bill a positive amount");

    uint64_t price_t = price * std::pow(2, 32);
    sub_balance(owner, asset);
    bill_stats sst(_self, owner);

    auto hash = sha256<stake_id_args>({ owner, asset, price_t, now(), memo });
    uint64_t bill_id = uint64_t(*reinterpret_cast<const uint64_t*>(&hash));

    sst.emplace(_self, [&](auto& r) {
        r.primary = sst.available_primary_key();
        r.bill_id = bill_id;
        r.owner = owner;
        r.unmatched = asset;
        r.matched = extended_asset(0, pst_sym);
        r.price = price_t;
        r.created_at = time_point_sec(now());
        r.updated_at = time_point_sec(now());
    });
    SEND_INLINE_ACTION(*this, billrec, { _self, N(active) }, { owner, asset, bill_id, BILL });
}

void token::unbill(account_name owner, uint64_t bill_id, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    bill_stats sst(_self, owner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(bill_id);
    eosio_assert(ust != ust_idx.end(), "no such record");
    extended_asset unmatched_asseet = ust->unmatched;
    calbonus(owner, bill_id, owner);
    ust_idx.erase(ust);
    add_balance(owner, unmatched_asseet, owner);

    SEND_INLINE_ACTION(*this, billrec, { _self, N(active) }, { owner, unmatched_asseet, bill_id, UNBILL });
}

void token::order(account_name owner, account_name miner, uint64_t bill_id, extended_asset asset, extended_asset reserve, string memo)
{
    require_auth(owner);
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");
    eosio_assert(owner != miner, "owner and user are same person");
    eosio_assert(asset.get_extended_symbol() == pst_sym, "only proof of service token can be ordered");
    eosio_assert(asset.amount > 0, "must order a positive amount");
    eosio_assert(reserve.amount >= 0, "reserve amount must >= 0");
    require_recipient(owner);
    require_recipient(miner);

    bill_stats sst(_self, miner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(bill_id);
    eosio_assert(ust != ust_idx.end(), "no such record");
    eosio_assert(ust->unmatched >= asset, "overdrawn balance");

    double price = (double)ust->price / std::pow(2, 32);
    double dmc_amount = price * asset.amount;
    extended_asset user_to_pay = get_asset_by_amount<double, std::ceil>(dmc_amount, dmc_sym);
    sub_balance(owner, user_to_pay + reserve);

    uint64_t now_time_t = calbonus(miner, bill_id, owner);

    ust_idx.modify(ust, 0, [&](auto& s) {
        s.unmatched -= asset;
        s.matched += asset;
        s.updated_at = time_point_sec(now_time_t);
    });

    uint64_t claims_interval = get_dmc_config(name { N(claiminter) }, default_dmc_claims_interval);

    dmc_orders order_tbl(_self, _self);
    auto hash = sha256<order_id_args>({ owner, miner, bill_id, asset, reserve, memo, time_point_sec(now()) });
    uint64_t order_id = uint64_t(*reinterpret_cast<const uint64_t*>(&hash));
    while (order_tbl.find(order_id) != order_tbl.end()) {
        order_id += 1;
    }
    order_tbl.emplace(owner, [&](auto& o) {
        o.order_id = order_id;
        o.user = owner;
        o.miner = miner;
        o.bill_id = bill_id;
        o.user_pledge = reserve;
        o.miner_pledge = asset;
        o.settlement_pledge = extended_asset(0, user_to_pay.get_extended_symbol());
        o.lock_pledge = user_to_pay;
        o.price = user_to_pay;
        o.state = OrderStateWaiting;
        o.deliver_start_date = time_point_sec();
        o.latest_settlement_date = time_point_sec();
    });

    dmc_challenges challenge_tbl(_self, _self);
    challenge_tbl.emplace(owner, [&](auto& c) {
        c.order_id = order_id;
        c.pre_merkle_root = checksum256();
        c.pre_data_block_count = 0;
        c.merkle_submitter = name { _self };
        c.challenge_times = 0;
        c.state = ChallengePrepare;
        c.user_lock = extended_asset(0, dmc_sym);
        c.miner_pay = extended_asset(0, dmc_sym);
    });

    if (reserve.amount > 0) {
        extended_asset zero_dmc = extended_asset(0, dmc_sym);
        SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
            { order_id, reserve, zero_dmc, zero_dmc, zero_dmc, time_point_sec(now()), OrderReceiptUser });
    }

    trace_price_history(price, bill_id);
    SEND_INLINE_ACTION(*this, orderrec, { _self, N(active) }, { owner, miner, user_to_pay, asset, reserve, bill_id, order_id });
}

void token::increase(account_name owner, extended_asset asset, account_name miner)
{
    require_auth(owner);
    eosio_assert(asset.get_extended_symbol() == dmc_sym, "only DMC can be staked");
    eosio_assert(asset.amount > 0, "must increase a positive amount");

    sub_balance(owner, asset);

    dmc_makers maker_tbl(_self, _self);
    auto iter = maker_tbl.find(miner);
    dmc_maker_pool dmc_pool(_self, miner);
    auto p_iter = dmc_pool.find(owner);
    if (iter == maker_tbl.end()) {
        if (owner == miner) {
            maker_tbl.emplace(miner, [&](auto& m) {
                m.miner = owner;
                m.current_rate = cal_current_rate(asset, miner);
                m.miner_rate = 1;
                m.total_weight = static_weights;
                m.total_staked = asset;
            });

            SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { owner, miner, asset, MakerReceiptIncrease });
            dmc_pool.emplace(owner, [&](auto& p) {
                p.owner = owner;
                p.weight = static_weights;
            });
        } else {
            eosio_assert(false, "no such record");
        }
    } else {
        extended_asset new_total = iter->total_staked + asset;
        double new_weight = (double)asset.amount / iter->total_staked.amount * iter->total_weight;
        double total_weight = iter->total_weight + new_weight;
        eosio_assert(new_weight > 0, "invalid new weight");
        eosio_assert(new_weight / total_weight > 0.0001, "increase too lower");

        double r = cal_current_rate(new_total, miner);
        maker_tbl.modify(iter, 0, [&](auto& m) {
            m.total_weight = total_weight;
            m.total_staked = new_total;
            m.current_rate = r;
        });

        SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { owner, miner, asset, MakerReceiptIncrease });

        if (p_iter != dmc_pool.end()) {
            dmc_pool.modify(p_iter, 0, [&](auto& s) {
                s.weight += new_weight;
            });
        } else {
            dmc_pool.emplace(owner, [&](auto& p) {
                p.owner = owner;
                p.weight = new_weight;
            });
        }

        auto miner_iter = dmc_pool.find(miner);
        eosio_assert(miner_iter != dmc_pool.end(), ""); // never happened
        eosio_assert(miner_iter->weight / total_weight >= iter->miner_rate, "exceeding the maximum rate");
    }
}

void token::redemption(account_name owner, double rate, account_name miner)
{
    require_auth(owner);

    eosio_assert(rate > 0 && rate <= 1, "invaild rate");
    dmc_makers maker_tbl(_self, _self);
    auto iter = maker_tbl.find(miner);
    eosio_assert(iter != maker_tbl.end(), "no such record");

    dmc_maker_pool dmc_pool(_self, miner);
    auto p_iter = dmc_pool.find(owner);
    eosio_assert(p_iter != dmc_pool.end(), "no such limit partnership");

    double owner_weight = p_iter->weight * rate;
    double rede_rate = owner_weight / iter->total_weight;
    extended_asset rede_quantity = extended_asset(std::floor(iter->total_staked.amount * rede_rate), dmc_sym);
    eosio_assert(rede_quantity.amount > 0, "dust attack detected");

    bool last_one = false;
    if (rate == 1) {
        owner_weight = p_iter->weight;
        dmc_pool.erase(p_iter);
        auto pool_begin = dmc_pool.begin();
        if (pool_begin == dmc_pool.end()) {
            rede_quantity = iter->total_staked;
        } else if (++pool_begin == dmc_pool.end()) {
            last_one = true;
            pool_begin--;
            owner_weight = pool_begin->weight;
        }
    } else {
        dmc_pool.modify(p_iter, 0, [&](auto& s) {
            s.weight -= owner_weight;
        });
        eosio_assert(p_iter->weight > 0, "negative pool weight amount");
    }

    double total_weight = iter->total_weight - owner_weight;
    extended_asset total_staked = iter->total_staked - rede_quantity;
    double benchmark_stake_rate = get_dmc_rate(name { N(bmrate) }, default_benchmark_stake_rate);
    double r = cal_current_rate(total_staked, miner);
    if (miner == owner) {
        eosio_assert(r >= benchmark_stake_rate, "current stake rate less than benchmark stake rate, redemption fails");
        auto miner_iter = dmc_pool.find(miner);
        eosio_assert(miner_iter != dmc_pool.end(), "miner can not destroy maker");
        eosio_assert(miner_iter->weight / total_weight >= iter->miner_rate, "below the minimum rate");
    }

    lock_add_balance(owner, rede_quantity, time_point_sec(now() + seconds_three_days), owner);
    SEND_INLINE_ACTION(*this, redeemrec, { _self, N(active) }, { owner, miner, rede_quantity });

    maker_tbl.modify(iter, 0, [&](auto& m) {
        m.total_weight = total_weight;
        m.total_staked = total_staked;
        m.current_rate = r;
        if (last_one)
            m.total_weight = owner_weight;
    });
    SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { owner, miner, -rede_quantity, MakerReceiptRedemption });

    eosio_assert(iter->total_staked.amount >= 0, "negative total_staked amount");
    eosio_assert(iter->total_weight >= 0, "negative total weight amount");

    if (rate != 1)
        eosio_assert(p_iter->weight / iter->total_weight > 0.0001, "The remaining weight is too low");
}

void token::mint(account_name owner, extended_asset asset)
{
    require_auth(owner);
    eosio_assert(asset.amount > 0, "must mint a positive amount");
    eosio_assert(asset.get_extended_symbol() == pst_sym, "only PST can be minted");

    dmc_makers maker_tbl(_self, _self);
    const auto& iter = maker_tbl.get(owner, "no such pst maker");

    //! refactor
    double makerd_pst = cal_makerd_pst(iter.total_staked);
    extended_asset added_asset = asset;
    pststats pst_acnts(_self, _self);

    auto st = pst_acnts.find(owner);
    if (st != pst_acnts.end())
        added_asset += st->amount;

    eosio_assert(std::floor(makerd_pst) >= added_asset.amount, "insufficient funds to mint");

    add_stats(asset);
    add_balance(owner, asset, owner);
    change_pst(owner, asset);
    double r = cal_current_rate(iter.total_staked, owner);
    double benchmark_stake_rate = get_dmc_rate(name { N(bmrate) }, default_benchmark_stake_rate);
    eosio_assert(r >= benchmark_stake_rate, "current stake rate less than benchmark stake rate, mint fails");

    maker_tbl.modify(iter, 0, [&](auto& m) {
        m.current_rate = r;
    });
}

void token::setmakerrate(account_name owner, double rate)
{
    require_auth(owner);
    eosio_assert(rate >= 0.2 && rate <= 1, "invaild rate");
    dmc_makers maker_tbl(_self, _self);
    const auto& iter = maker_tbl.get(owner, "no such record");

    dmc_maker_pool dmc_pool(_self, owner);
    auto miner_iter = dmc_pool.find(owner);
    eosio_assert(miner_iter != dmc_pool.end(), "miner can not destroy maker"); // never happened
    eosio_assert(miner_iter->weight / iter.total_weight >= rate, "rate does not meet limits");

    maker_tbl.modify(iter, 0, [&](auto& s) {
        s.miner_rate = rate;
    });
}

double token::cal_makerd_pst(extended_asset dmc_asset)
{
    double benchmark_stake_rate = get_dmc_rate(name { N(bmrate) }, default_benchmark_stake_rate);
    double pst_amount = (double)get_real_asset(dmc_asset) / benchmark_stake_rate;
    return pst_amount;
}

double token::cal_current_rate(extended_asset dmc_asset, account_name owner)
{
    pststats pst_acnts(_self, _self);
    double r = 0.0;
    auto st = pst_acnts.find(owner);
    if (st != pst_acnts.end() && st->amount.amount != 0) {
        r = (double)get_real_asset(dmc_asset) / st->amount.amount;
    } else {
        r = uint64_max;
    }
    return r;
}

void token::liquidation(string memo)
{
    require_auth(eos_account);
    dmc_makers maker_tbl(_self, _self);
    auto maker_idx = maker_tbl.get_index<N(byrate)>();

    double n = get_dmc_rate(name { N(liqrate) }, default_liquidation_stake_rate);
    double m = get_dmc_rate(name { N(bmrate) }, default_benchmark_stake_rate);
    pststats pst_acnts(_self, _self);
    constexpr uint64_t required_size = 20;
    std::vector<std::tuple<account_name /* miner */, extended_asset /* pst_asset */, extended_asset /* dmc_asset */>> liquidation_required;
    liquidation_required.reserve(required_size);
    for (auto maker_it = maker_idx.cbegin(); maker_it != maker_idx.cend() && maker_it->current_rate < n && liquidation_required.size() < required_size; maker_it++) {
        account_name owner = maker_it->miner;
        double r1 = maker_it->current_rate;
        auto pst_it = pst_acnts.find(owner);

        double sub_pst = (double)(1 - r1 / m) * get_real_asset(pst_it->amount);
        extended_asset liq_pst_asset_leftover = get_asset_by_amount<double, std::ceil>(sub_pst, pst_sym);
        auto origin_liq_pst_asset = liq_pst_asset_leftover;

        accounts acnts(_self, owner);
        auto account_idx = acnts.get_index<N(byextendedasset)>();
        auto account_it = account_idx.find(account::key(pst_sym));
        if (account_it != account_idx.end()) {
            extended_asset pst_sub = extended_asset(std::min(liq_pst_asset_leftover.amount, account_it->balance.amount), pst_sym);

            sub_balance(owner, pst_sub);
            liq_pst_asset_leftover.amount = std::max((liq_pst_asset_leftover - pst_sub).amount, 0ll);
        }

        bill_stats sst(_self, owner);
        for (auto bit = sst.begin(); bit != sst.end() && liq_pst_asset_leftover.amount > 0;) {
            extended_asset sub_pst;
            if (bit->unmatched <= liq_pst_asset_leftover) {
                sub_pst = bit->unmatched;
                liq_pst_asset_leftover -= bit->unmatched;
            } else {
                sub_pst = liq_pst_asset_leftover;
                liq_pst_asset_leftover.amount = 0;
            }

            account_name miner = bit->owner;
            uint64_t bill_id = bit->bill_id;
            uint64_t now_time_t = calbonus(miner, bill_id, _self);

            sst.modify(bit, 0, [&](auto& r) {
                r.unmatched -= sub_pst;
                r.updated_at = time_point_sec(now_time_t);
            });

            if (bit->unmatched.amount == 0)
                bit = sst.erase(bit);
            else
                bit++;

            SEND_INLINE_ACTION(*this, makerliqrec, { _self, N(active) }, { miner, bill_id, sub_pst });
        }
        extended_asset sub_pst_asset = origin_liq_pst_asset - liq_pst_asset_leftover;
        double penalty_dmc = (double)(1 - r1 / m) * get_real_asset(maker_it->total_staked) * get_dmc_config(name { N(penaltyrate) }, default_penalty_rate) / 100.0;
        extended_asset penalty_dmc_asset = get_asset_by_amount<double, std::ceil>(penalty_dmc, dmc_sym);
        if (sub_pst_asset.amount != 0 && penalty_dmc_asset.amount != 0) {
            liquidation_required.emplace_back(std::make_tuple(owner, sub_pst_asset, penalty_dmc_asset));
        }
    }

    for (const auto liq : liquidation_required) {
        account_name miner;
        extended_asset pst;
        extended_asset dmc;
        std::tie(miner, pst, dmc) = liq;
        auto pst_it = pst_acnts.find(miner);
        change_pst(miner, -pst);
        sub_stats(pst);
        auto iter = maker_tbl.find(miner);
        extended_asset new_staked = iter->total_staked - dmc;
        double new_rate = cal_current_rate(new_staked, miner);
        maker_tbl.modify(iter, 0, [&](auto& s) {
            s.total_staked = new_staked;
            s.current_rate = new_rate;
        });
        SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { _self, miner, -dmc, MakerReceiptLiquidation });
        add_balance(system_account, dmc, eos_account);
        SEND_INLINE_ACTION(*this, liqrec, { _self, N(active) }, { miner, pst, dmc });
    }
}

void token::getincentive(account_name owner, uint64_t bill_id)
{
    require_auth(owner);
    uint64_t now_time_t = calbonus(owner, bill_id, owner);
    bill_stats sst(_self, owner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(bill_id);
    ust_idx.modify(ust, 0, [&](auto& s) {
        s.updated_at = time_point_sec(now_time_t);
    });
}

uint64_t token::calbonus(account_name owner, uint64_t bill_id, account_name ram_payer)
{
    bill_stats sst(_self, owner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(bill_id);
    eosio_assert(ust != ust_idx.end(), "no such record");

    auto now_time = time_point_sec(now());
    uint64_t now_time_t = now_time.sec_since_epoch();
    uint64_t updated_at_t = ust->updated_at.sec_since_epoch();
    uint64_t bill_dmc_claims_interval = get_dmc_config(name { N(billinter) }, default_bill_dmc_claims_interval);
    uint64_t max_dmc_claims_interval = ust->created_at.sec_since_epoch() + bill_dmc_claims_interval;

    now_time_t = now_time_t >= max_dmc_claims_interval ? max_dmc_claims_interval : now_time_t;

    if (updated_at_t <= max_dmc_claims_interval) {
        uint64_t duration = now_time_t - updated_at_t;
        eosio_assert(duration <= now_time_t, "subtractive overflow"); // never happened

        extended_asset quantity = get_asset_by_amount<double, std::floor>(get_dmc_config(name { N(bmrate) }, default_benchmark_stake_rate) / 100.0 / default_bill_dmc_claims_interval, rsi_sym);
        quantity.amount *= duration;
        quantity.amount *= ust->unmatched.amount;
        if (quantity.amount != 0) {
            add_stats(quantity);
            add_balance(owner, quantity, ram_payer);
            SEND_INLINE_ACTION(*this, incentiverec, { _self, N(active) }, { owner, quantity, bill_id, 0, 0 });
        }
    }
    return now_time_t;
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
            // 超过时间，但前一阶段仍然有份额没有增发完，一次性全部增发
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
            // 每天只能领取一次
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
    require_auth(system_account);
    dmc_global dmc_global_tbl(_self, _self);
    switch (key) {
    case N(claiminter):
        eosio_assert(value > 0, "invalid claims interval");
        break;
    default:
        break;
    }
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

double token::get_dmc_rate(name key, uint64_t default_value)
{
    dmc_global dmc_global_tbl(_self, _self);
    auto dmc_global_iter = dmc_global_tbl.find(key);
    avg_table atb(_self, _self);
    auto aitr = atb.begin();
    double value;
    if (dmc_global_iter != dmc_global_tbl.end())
        value = dmc_global_iter->value / 100.0;
    value = default_value / 100.0;

    return value * aitr->avg;
}

void token::trace_price_history(double price, uint64_t bill_id)
{
    price_table ptb(_self, _self);
    auto iter = ptb.get_index<N(bytime)>();
    auto now_time = time_point_sec(now());
    auto rtime = now_time - price_fluncuation_interval;

    avg_table atb(_self, _self);
    auto aitr = atb.begin();
    if (aitr == atb.end()) {
        aitr = atb.emplace(_self, [&](auto& a) {
            a.primary = 0;
            a.total = 0;
            a.count = 0;
            a.avg = 0;
        });
    }

    for (auto it = iter.begin(); it != iter.end();) {
        if (it->created_at < rtime) {
            it = iter.erase(it);
            atb.modify(aitr, _self, [&](auto& a) {
                a.total -= it->price;
                a.count -= 1;
            });
        } else {
            break;
        }
    }

    ptb.emplace(_self, [&](auto& p) {
        p.primary = ptb.available_primary_key();
        p.bill_id = bill_id;
        p.price = price;
        p.created_at = now_time;
    });

    atb.modify(aitr, _self, [&](auto& a) {
        a.total += price;
        a.count += 1;
        a.avg = (double)a.total / a.count;
    });
}

void token::cleanpst(string memo)
{
    pststats pst_acnts(_self, _self);
    std::vector<std::tuple<account_name /* owner */, extended_asset /* pst_asset */>> clean_required;

    for (auto it = pst_acnts.begin(); it != pst_acnts.end(); ++it) {
        auto miner = it->owner;
        dmc_makers maker_tbl(_self, _self);
        auto iter = maker_tbl.find(miner);
        if (iter == maker_tbl.end()) {
            extended_asset pst;
            accounts acnts(_self, miner);
            auto account_idx = acnts.get_index<N(byextendedasset)>();
            auto account_it = account_idx.find(account::key(pst_sym));
            if (account_it != account_idx.end()) {
                sub_balance(miner, account_it->balance);
                pst = account_it->balance;
            }
            bill_stats sst(_self, miner);
            for (auto bit = sst.begin(); bit != sst.end();) {
                pst += bit->unmatched;
                bit = sst.erase(bit);
            }
            clean_required.emplace_back(std::make_tuple(it->owner, pst));
        }
    }

    for (const auto cle : clean_required) {
        account_name owner;
        extended_asset pst;
        std::tie(owner, pst) = cle;
        auto pst_it = pst_acnts.find(owner);
        change_pst(owner, -pst);
        sub_stats(pst);
    }
}
} // namespace eosio
