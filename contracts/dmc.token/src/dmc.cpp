/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::bill(name owner, extended_asset asset, double price, time_point_sec expire_on, uint64_t deposit_ratio, string memo) {
    require_auth(owner);
    check(memo.size() <= 256, "memo has more than 256 bytes");
    extended_symbol s_sym = asset.get_extended_symbol();
    check(s_sym == pst_sym, "only proof of service token can be billed");
    check(price >= 0.0001 && (price < (uint64_t(1) << 50)), "invalid price");
    check(asset.quantity.amount > 0, "must bill a positive amount");
    check(deposit_ratio >= 0 && deposit_ratio <= 99, "invalid deposit ratio");

    time_point_sec now_time = time_point_sec(current_time_point());
    check(expire_on >= now_time + get_dmc_config("serverinter"_n, default_service_interval), "invalid service time");

    uint64_t price_t = std::round(price * 10000);

    sub_balance(owner, asset);
    bill_stats sst(get_self(), get_self().value);

    uint64_t bill_id = get_dmc_config("billid"_n, default_id_start);
    bill_record bill_info = {
        .bill_id = bill_id,
        .owner = owner,
        .unmatched = asset,
        .matched = extended_asset(0, pst_sym),
        .price = price_t,
        .created_at = now_time,
        .updated_at = now_time,
        .expire_on = expire_on,
        .deposit_ratio = deposit_ratio};

    set_dmc_config("billid"_n, bill_id + 1);
    sst.emplace(owner, [&](auto& r) {
        r = bill_info;
    });
    SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {bill_info});
}

void token::unbill(name owner, uint64_t bill_id, string memo) {
    require_auth(owner);
    check(memo.size() <= 256, "memo has more than 256 bytes");

    // TODO: delete it after
    if (bill_id < get_dmc_config("olderbillid"_n, default_id_start)) {
        bill_stats sst(get_self(), owner.value);
        auto ust = sst.find(bill_id);
        check(ust != sst.end(), "no such record");
        extended_asset unmatched_asseet = ust->unmatched;
        calbonus(owner, bill_id, owner);
        sst.erase(ust);
        add_balance(owner, unmatched_asseet, owner);
        bill_record bill_info = {
            .bill_id = bill_id,
            .owner = owner,
            .unmatched = unmatched_asseet};

        SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {bill_info});
    } else {
        bill_stats sst(get_self(), get_self().value);
        auto ust = sst.find(bill_id);
        check(ust != sst.end(), "no such record");
        check(ust->owner == owner, "only owner can unbill");
        extended_asset unmatched_asseet = ust->unmatched;
        calbonus(owner, bill_id, owner);
        sst.erase(ust);
        add_balance(owner, unmatched_asseet, owner);
        bill_record bill_info = {
            .bill_id = bill_id,
            .owner = owner,
            .unmatched = unmatched_asseet};

        SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {bill_info});
    }
}

void token::order(name owner, uint64_t bill_id, uint64_t benchmark_price, PriceRangeType price_range, uint64_t epoch, extended_asset asset, extended_asset reserve, string memo) {
    require_auth(owner);
    check(memo.size() <= 256, "memo has more than 256 bytes");
    check(price_range > RangeTypeBegin && price_range < RangeTypeEnd, "invalid price range");
    check(benchmark_price > 0, "invalid benchmark price");
    check(epoch > 0, "invalid epoch");
    check(asset.get_extended_symbol() == pst_sym, "only PST can be ordered");
    check(asset.quantity.amount > 0, "must order a positive amount");
    check(reserve.get_extended_symbol() == dmc_sym, "only DMC can be reserved");
    check(reserve.quantity.amount >= 0, "reserve amount must >= 0");

    double current_price = get_benchmark_price();
    check(current_price <= (double)benchmark_price / 10000.0 * 1.05 && current_price >= (double)benchmark_price / 10000.0 * 0.95, "current price is not in benchmark price range");

    uint64_t range = 0;
    switch (price_range) {
        case TwentyPercent:
            range = 20;
            break;
        case ThirtyPercent:
            range = 30;
            break;
        case NoLimit:
            range = 100;
            break;
        default:
            break;
    }
    uint64_t lower_bound_begin = benchmark_price * (100 - range) / 100;
    uint64_t upper_bound_end = range == 100 ? uint64_max : benchmark_price * (100 + range) / 100;

    bill_stats sst(get_self(), get_self().value);
    auto price_idx = sst.get_index<"bylowerprice"_n>();
    auto lower_bound_iter = price_idx.lower_bound(lower_bound_begin);
    bool is_match = false;
    uint64_t bill_number_limit = get_dmc_config("billnumlimit"_n, default_bill_num_limit);

    // search ${bill_number_limit} records,
    // NOTE: the actual situation may be more than ${bill_number_limit} entries, as entries with the same price count as one.
    auto start_price = lower_bound_iter->price;
    for (uint64_t count = 0; lower_bound_iter != price_idx.end() && count < bill_number_limit; lower_bound_iter++) {
        if (lower_bound_iter->unmatched < asset) {
            continue;
        }
        if (lower_bound_iter->price > upper_bound_end) {
            break;
        }
        // only count the first record with the same price
        if (lower_bound_iter->price != start_price) {
            count++;
            start_price = lower_bound_iter->price;
        }
        if (lower_bound_iter->bill_id == bill_id) {
            is_match = true;
            break;
        }
    }
    check(is_match, "no matched bill");

    name miner = lower_bound_iter->owner;
    check(miner != owner, "can not order with self");
    require_recipient(miner);
    require_recipient(owner);

    uint64_t order_serivce_epoch = get_dmc_config("ordsrvepoch"_n, default_order_service_epoch);
    uint64_t claims_interval = get_dmc_config("claiminter"_n, default_dmc_claims_interval);

    check(time_point_sec(current_time_point() + eosio::seconds(claims_interval * epoch)) <= lower_bound_iter->expire_on, "service has expired");
    check((claims_interval * epoch) >= order_serivce_epoch, "service not reach minimum deposit expire time");

    double price = (double)lower_bound_iter->price / 10000;
    double dmc_amount = price * asset.quantity.amount;
    extended_asset user_to_pay = get_asset_by_amount<double, std::round>(dmc_amount, dmc_sym);

    // deposit
    extended_asset user_to_deposit = extended_asset(std::floor(user_to_pay.quantity.amount * lower_bound_iter->deposit_ratio), dmc_sym);
    check(reserve >= user_to_pay + user_to_deposit, "reserve can't pay first time");
    sub_balance(owner, reserve);

    uint64_t now_time_t = calbonus(miner, bill_id, owner);

    price_idx.modify(lower_bound_iter, get_self(), [&](auto& s) {
        s.unmatched -= asset;
        s.matched += asset;
        s.updated_at = time_point_sec(now_time_t);
    });

    if (lower_bound_iter->unmatched.quantity.amount == 0) {
        price_idx.erase(lower_bound_iter);
    }

    uint64_t order_id = get_dmc_config("orderid"_n, default_id_start);

    dmc_makers maker_tbl(get_self(), get_self().value);
    auto maker_iter = maker_tbl.find(miner.value);
    check(maker_iter != maker_tbl.end(), "can't find maker pool");

    dmc_orders order_tbl(get_self(), get_self().value);
    uint64_t r = std::floor(maker_iter->current_rate * 100.0 / get_benchmark_price());
    // r = 5m' if r > 5m'
    if (r > maker_iter->benchmark_stake_rate * 5) {
        r = maker_iter->benchmark_stake_rate * 5;
    }
    auto miner_lock_dmc = extended_asset(user_to_pay.quantity.amount * (r / 100.0), user_to_pay.get_extended_symbol());
    check(maker_iter->total_staked >= miner_lock_dmc, "not enough stake quantity");
    dmc_order order_info = {
        .order_id = order_id,
        .user = owner,
        .miner = miner,
        .bill_id = bill_id,
        .user_pledge = reserve - user_to_pay - user_to_deposit,
        .miner_lock_pst = asset,
        .miner_lock_dmc = miner_lock_dmc,
        .settlement_pledge = extended_asset(0, user_to_pay.get_extended_symbol()),
        .lock_pledge = user_to_pay,
        .price = user_to_pay,
        .state = OrderStateWaiting,
        .deliver_start_date = time_point_sec(),
        .latest_settlement_date = time_point_sec(),
        .miner_lock_rsi = extended_asset(0, rsi_sym),
        .miner_rsi = extended_asset(0, rsi_sym),
        .user_rsi = extended_asset(0, rsi_sym),
        .deposit = user_to_deposit,
        .epoch = epoch,
        .deposit_valid = time_point_sec(),
        .cancel_date = time_point_sec(),
    };
    order_tbl.emplace(owner, [&](auto& o) {
        o = order_info;
    });

    dmc_challenge challenge_info = {
        .order_id = order_id,
        .pre_merkle_root = checksum256(),
        .pre_data_block_count = 0,
        .merkle_submitter = get_self(),
        .challenge_times = 0,
        .state = ChallengePrepare,
        .user_lock = extended_asset(0, dmc_sym),
        .miner_pay = extended_asset(0, dmc_sym),
    };
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    challenge_tbl.emplace(owner, [&](auto& c) {
        c = challenge_info;
    });

    change_pst(miner, -asset, true);
    maker_tbl.modify(maker_iter, owner, [&](auto& m) {
        m.current_rate = cal_current_rate(m.total_staked - miner_lock_dmc, miner, m.get_real_m());
        m.total_staked -= miner_lock_dmc;
    });

    generate_maker_snapshot(order_info.order_id, bill_id, order_info.miner, owner, r, maker_iter->total_staked.quantity.amount == 0);
    trace_price_history(price, bill_id, order_info.order_id);
    set_dmc_config("orderid"_n, order_id + 1);
    SEND_INLINE_ACTION(*this, orderrec, {_self, "active"_n}, {order_info, 1});
    SEND_INLINE_ACTION(*this, challengerec, {_self, "active"_n}, {challenge_info});
    SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {*lower_bound_iter});
    SEND_INLINE_ACTION(*this, assetrec, {_self, "active"_n}, {order_info.order_id, {reserve}, order_info.user, AssetReceiptAddReserve});
    SEND_INLINE_ACTION(*this, orderassrec, {_self, "active"_n}, {order_info.order_id, {{reserve, OrderReceiptAddReserve}, {-user_to_deposit, OrderReceiptDeposit}, {-user_to_pay, OrderReceiptRenew}}, order_info.user, ACC_TYPE_USER, time_point_sec(current_time_point())});
}

void token::increase(name owner, extended_asset asset, name miner) {
    require_auth(owner);
    check(asset.get_extended_symbol() == dmc_sym, "only DMC can be staked");
    check(asset.quantity.amount > 0, "must increase a positive amount");

    sub_balance(owner, asset);

    dmc_makers maker_tbl(get_self(), get_self().value);
    auto iter = maker_tbl.find(miner.value);
    dmc_maker_pool dmc_pool(get_self(), miner.value);
    auto p_iter = dmc_pool.find(owner.value);
    uint64_t benchmark_stake_rate = get_dmc_config("bmrate"_n, default_benchmark_stake_rate);
    if (iter == maker_tbl.end()) {
        if (owner == miner) {
            iter = maker_tbl.emplace(miner, [&](auto& m) {
                m.miner = owner;
                m.current_rate = cal_current_rate(asset, miner, benchmark_stake_rate / 100.0);
                m.miner_rate = 1;
                m.total_weight = static_weights;
                m.total_staked = asset;
                m.benchmark_stake_rate = benchmark_stake_rate;
            });

            p_iter = dmc_pool.emplace(owner, [&](auto& p) {
                p.owner = owner;
                p.weight = static_weights;
            });
        } else {
            check(false, "no such record");
        }
    } else if (iter->total_staked.quantity.amount == 0) {
        check(owner == miner, "only miner can stake now");
        maker_tbl.modify(iter, owner, [&](auto& m) {
            m.current_rate = cal_current_rate(asset, miner, m.get_real_m());
            m.total_weight = static_weights;
            m.total_staked = asset;
        });
        dmc_pool.modify(p_iter, owner, [&](auto& p) {
            p.weight = static_weights;
        });
    } else {
        extended_asset new_total = iter->total_staked + asset;
        double new_weight = (double)asset.quantity.amount / iter->total_staked.quantity.amount * iter->total_weight;
        double total_weight = iter->total_weight + new_weight;
        check(new_weight > 0, "invalid new weight");
        // check(new_weight / total_weight > 0.0001, "the quantity of increase is insufficient.");

        double r = cal_current_rate(new_total, miner, iter->get_real_m());
        maker_tbl.modify(iter, owner, [&](auto& m) {
            m.total_weight = total_weight;
            m.total_staked = new_total;
            m.current_rate = r;
        });

        if (p_iter != dmc_pool.end()) {
            dmc_pool.modify(p_iter, owner, [&](auto& s) {
                s.weight += new_weight;
            });
        } else {
            p_iter = dmc_pool.emplace(owner, [&](auto& p) {
                p.owner = owner;
                p.weight = new_weight;
            });
        }

        auto miner_iter = dmc_pool.find(miner.value);
        check(miner_iter != dmc_pool.end(), "miner can not destroy maker");  // never happened
        check(miner_iter->weight / total_weight >= iter->miner_rate, "exceeding the maximum rate");

        // The total amount staked by the lp must be greater than one percent of the stakable amount
        if (miner != owner) {
            check(p_iter->weight / (total_weight * (1 - iter->miner_rate)) >= 0.01, "the quantity of increase is insufficient.");
        }
    }
    SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {*iter});
    SEND_INLINE_ACTION(*this, makerpoolrec, {_self, "active"_n}, {miner, {*p_iter}});
}

void token::redemption(name owner, double rate, name miner) {
    require_auth(owner);

    check(rate > 0 && rate <= 1, "invalid rate");
    dmc_makers maker_tbl(get_self(), get_self().value);
    auto iter = maker_tbl.find(miner.value);
    check(iter != maker_tbl.end(), "no such record");

    dmc_maker_pool dmc_pool(get_self(), miner.value);
    auto p_iter = dmc_pool.find(owner.value);
    check(p_iter != dmc_pool.end(), "no such limit partnership");

    double owner_weight = p_iter->weight * rate;
    double rede_rate = owner_weight / iter->total_weight;
    extended_asset rede_quantity = extended_asset(std::floor(iter->total_staked.quantity.amount * rede_rate), dmc_sym);

    bool last_one = false;
    if (rate == 1) {
        // for tracker
        dmc_pool.modify(p_iter, owner, [&](auto& s) {
            s.weight = 0;
        });

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
        dmc_pool.modify(p_iter, owner, [&](auto& s) {
            s.weight -= owner_weight;
        });
        check(p_iter->weight > 0, "negative pool weight amount");
    }

    double total_weight = iter->total_weight - owner_weight;
    extended_asset total_staked = iter->total_staked - rede_quantity;
    double benchmark_stake_rate = get_dmc_rate(iter->benchmark_stake_rate);
    double r = cal_current_rate(total_staked, miner, iter->get_real_m());

    if (miner == owner) {
        check(r >= benchmark_stake_rate, "current stake rate less than benchmark stake rate, redemption fails");

        double miner_rate = 0.0;
        if (total_staked.quantity.amount == 0) {
            miner_rate = uint64_max;
        } else {
            auto miner_iter = dmc_pool.find(miner.value);
            check(miner_iter != dmc_pool.end(), "miner can only redeem all last");
            miner_rate = miner_iter->weight / total_weight;
        }
        check(miner_rate >= iter->miner_rate, "below the minimum rate");
    }
    check(rede_quantity.quantity.amount > 0, "dust attack detected");
    exchange_balance_to_lockbalance(owner, rede_quantity, time_point_sec(current_time_point() + eosio::days(3)), owner);
    SEND_INLINE_ACTION(*this, redeemrec, {get_self(), "active"_n}, {owner, miner, rede_quantity});
    if (total_staked.quantity.amount == 0) {
        // for tracker
        maker_tbl.modify(iter, get_self(), [&](auto& m) {
            m.total_weight = 0;
        });

        dmc_orders order_tbl(get_self(), get_self().value);
        auto order_idx = order_tbl.get_index<"miner"_n>();
        auto order_iter = order_idx.find(miner.value);
        check(order_iter == order_idx.end(), "maker has orders");

        maker_tbl.erase(iter);
    } else {
        maker_tbl.modify(iter, get_self(), [&](auto& m) {
            m.total_weight = total_weight;
            m.total_staked = total_staked;
            m.current_rate = r;
            if (last_one)
                m.total_weight = owner_weight;
        });
        check(iter->total_staked.quantity.amount >= 0, "negative total_staked amount");
        check(iter->total_weight >= 0, "negative total weight amount");
    }

    if (rate != 1) {
        check(p_iter->weight / (iter->total_weight * (1 - iter->miner_rate)) >= 0.01, "The remaining weight is too low");
    }

    SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {*iter});
    SEND_INLINE_ACTION(*this, makerpoolrec, {_self, "active"_n}, {miner, {*p_iter}});
}

void token::mint(name owner, extended_asset asset) {
    require_auth(owner);
    check(asset.quantity.amount > 0, "must mint a positive amount");
    check(asset.get_extended_symbol() == pst_sym, "only PST can be minted");

    dmc_makers maker_tbl(get_self(), get_self().value);
    const auto& iter = maker_tbl.get(owner.value, "no such pst maker");

    double benchmark_stake_rate = get_dmc_rate(iter.benchmark_stake_rate);
    double makerd_pst = (double)get_real_asset(iter.total_staked) / benchmark_stake_rate;
    extended_asset added_asset = asset;
    pststats pst_acnts(get_self(), get_self().value);

    auto st = pst_acnts.find(owner.value);
    if (st != pst_acnts.end())
        added_asset += st->amount;

    check(std::floor(makerd_pst) >= added_asset.quantity.amount, "insufficient funds to mint");

    add_balance(owner, asset, owner);
    change_pst(owner, asset);
    double r = cal_current_rate(iter.total_staked, owner, iter.get_real_m());
    check(r >= benchmark_stake_rate, "current stake rate less than benchmark stake rate, mint fails");

    maker_tbl.modify(iter, get_self(), [&](auto& m) {
        m.current_rate = r;
    });

    SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {iter});
}

void token::setmakerrate(name owner, double rate) {
    require_auth(owner);
    check(rate >= 0.2 && rate <= 1, "invalid rate");
    dmc_makers maker_tbl(get_self(), get_self().value);
    const auto& iter = maker_tbl.get(owner.value, "no such record");

    dmc_maker_pool dmc_pool(get_self(), owner.value);
    auto miner_iter = dmc_pool.find(owner.value);
    check(miner_iter != dmc_pool.end(), "miner can not destroy maker");  // never happened
    check(miner_iter->weight / iter.total_weight >= rate, "rate does not meet limits");

    maker_tbl.modify(iter, get_self(), [&](auto& s) {
        s.miner_rate = rate;
    });

    SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {iter});
}

void token::setmakerbstr(name owner, uint64_t self_benchmark_stake_rate) {
    require_auth(owner);
    dmc_makers maker_tbl(get_self(), get_self().value);
    const auto& iter = maker_tbl.get(owner.value, "no such record");
    dmc_maker_pool dmc_pool(get_self(), owner.value);
    auto miner_iter = dmc_pool.find(owner.value);
    check(miner_iter != dmc_pool.end(), "miner can not destroy maker");  // never happened
    time_point_sec now = time_point_sec(current_time_point());

    check(now >= iter.rate_updated_at + get_dmc_config("rateinter"_n, default_maker_change_rate_interval), "change rate interval too short");

    // no limit if set up rate first time, only >= m
    if (iter.rate_updated_at == time_point_sec(0)) {
        check(self_benchmark_stake_rate >= get_dmc_config("bmrate"_n, default_benchmark_stake_rate), "invalid benchmark_stake_rate");
    } else {
        check(self_benchmark_stake_rate <= iter.benchmark_stake_rate * 1.1 && self_benchmark_stake_rate >= iter.benchmark_stake_rate * 0.9 && self_benchmark_stake_rate >= get_dmc_config("bmrate"_n, default_benchmark_stake_rate),
              "invalid benchmark_stake_rate");
    }

    maker_tbl.modify(iter, get_self(), [&](auto& s) {
        s.benchmark_stake_rate = self_benchmark_stake_rate;
        s.rate_updated_at = now;
    });

    SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {iter});
}

double token::cal_current_rate(extended_asset dmc_asset, name owner, double real_m) {
    pststats pst_acnts(get_self(), get_self().value);
    double r = uint32_max;
    auto st = pst_acnts.find(owner.value);
    if (st != pst_acnts.end() && st->amount.quantity.amount != 0) {
        r = (double)get_real_asset(dmc_asset) / st->amount.quantity.amount;
    }
    return r;
}

void token::liquidation(string memo) {
    require_auth(dmc_account);
    check(memo.size() <= 256, "memo has more than 256 bytes");
    dmc_makers maker_tbl(get_self(), get_self().value);
    auto maker_idx = maker_tbl.get_index<"byrate"_n>();

    pststats pst_acnts(get_self(), get_self().value);
    constexpr uint64_t required_size = 20;
    std::vector<std::tuple<name /* miner */, extended_asset /* pst_asset */, extended_asset /* dmc_asset */>> liquidation_required;
    liquidation_required.reserve(required_size);

    for (auto maker_it = maker_idx.cbegin(); maker_it != maker_idx.cend() && maker_it->current_rate < get_dmc_rate(maker_it->get_n()) && liquidation_required.size() < required_size; maker_it++) {
        name owner = maker_it->miner;
        double r1 = maker_it->current_rate;
        auto pst_it = pst_acnts.find(owner.value);

        double m = get_dmc_rate(maker_it->benchmark_stake_rate);
        double sub_pst = (double)(1 - r1 / m) * get_real_asset(pst_it->amount);
        extended_asset liq_pst_asset_leftover = get_asset_by_amount<double, std::ceil>(sub_pst, pst_sym);
        auto origin_liq_pst_asset = liq_pst_asset_leftover;

        accounts acnts(get_self(), owner.value);
        auto account_idx = acnts.get_index<"byextendedas"_n>();
        auto account_it = account_idx.find(account::key(pst_sym));
        if (account_it != account_idx.end() && account_it->balance.quantity.amount > 0) {
            extended_asset pst_sub = extended_asset(std::min(liq_pst_asset_leftover.quantity.amount, account_it->balance.quantity.amount), pst_sym);

            sub_balance(owner, pst_sub);
            SEND_INLINE_ACTION(*this, currliqrec, {_self, "active"_n}, {owner, pst_sub});
            liq_pst_asset_leftover.quantity.amount = std::max((liq_pst_asset_leftover - pst_sub).quantity.amount, 0ll);
        }

        bill_stats sst(get_self(), get_self().value);
        auto bill_idx = sst.get_index<"byowner"_n>();
        auto bill_it = bill_idx.lower_bound(owner.value);

        for (; bill_it != bill_idx.end() && liq_pst_asset_leftover.quantity.amount > 0 && bill_it->owner == owner;) {
            extended_asset sub_pst;
            if (bill_it->unmatched <= liq_pst_asset_leftover) {
                sub_pst = bill_it->unmatched;
                liq_pst_asset_leftover -= bill_it->unmatched;
            } else {
                sub_pst = liq_pst_asset_leftover;
                liq_pst_asset_leftover.quantity.amount = 0;
            }

            uint64_t bill_id = bill_it->bill_id;
            uint64_t now_time_t = calbonus(owner, bill_id, _self);

            bill_idx.modify(bill_it, get_self(), [&](auto& r) {
                r.unmatched -= sub_pst;
                r.updated_at = time_point_sec(now_time_t);
                // for tracker
                if (r.unmatched.quantity.amount == 0)
                    r.price = 0;
            });

            SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {*bill_it});
            if (bill_it->unmatched.quantity.amount == 0)
                bill_it = bill_idx.erase(bill_it);
            else
                bill_it++;

            SEND_INLINE_ACTION(*this, billliqrec, {_self, "active"_n}, {owner, bill_id, sub_pst});
        }
        extended_asset sub_pst_asset = origin_liq_pst_asset - liq_pst_asset_leftover;
        double penalty_dmc = (double)(1 - r1 / m) * get_real_asset(maker_it->total_staked) * get_dmc_config("penaltyrate"_n, default_penalty_rate) / 100.0;
        extended_asset penalty_dmc_asset = get_asset_by_amount<double, std::ceil>(penalty_dmc, dmc_sym);
        if (sub_pst_asset.quantity.amount != 0 && penalty_dmc_asset.quantity.amount != 0) {
            liquidation_required.emplace_back(std::make_tuple(owner, sub_pst_asset, penalty_dmc_asset));
        }
    }

    for (const auto liq : liquidation_required) {
        name miner;
        extended_asset pst;
        extended_asset dmc;
        std::tie(miner, pst, dmc) = liq;
        auto pst_it = pst_acnts.find(miner.value);
        change_pst(miner, -pst);
        auto iter = maker_tbl.find(miner.value);
        extended_asset new_staked = iter->total_staked - dmc;
        double new_rate = cal_current_rate(new_staked, miner, iter->get_real_m());
        maker_tbl.modify(iter, get_self(), [&](auto& s) {
            s.total_staked = new_staked;
            s.current_rate = new_rate;
        });
        add_balance(system_account, dmc, dmc_account);
        SEND_INLINE_ACTION(*this, makerecord, {_self, "active"_n}, {*iter});
        SEND_INLINE_ACTION(*this, liqrec, {_self, "active"_n}, {miner, pst, dmc});
    }
}

void token::getincentive(name owner, uint64_t bill_id) {
    require_auth(owner);
    // TODO: delete it after
    check(get_dmc_config("olderbillid"_n, default_id_start) <= bill_id, "this bill can only be unbilled");
    uint64_t now_time_t = calbonus(owner, bill_id, owner);
    bill_stats sst(get_self(), get_self().value);
    // check bill_id in calbouns, so no need to check here
    auto ust = sst.find(bill_id);
    check(ust != sst.end(), "no such record");
    sst.modify(ust, get_self(), [&](auto& s) {
        s.updated_at = time_point_sec(now_time_t);
    });
    SEND_INLINE_ACTION(*this, billsnap, {_self, "active"_n}, {*ust});
}

uint64_t token::calbonus(name owner, uint64_t bill_id, name ram_payer) {
    // TODO: delete it after
    if (bill_id < get_dmc_config("olderbillid"_n, default_id_start)) {
        bill_stats sst(get_self(), owner.value);
        auto ust = sst.find(bill_id);
        check(ust != sst.end(), "no such record");
        dmc_makers maker_tbl(get_self(), get_self().value);
        const auto& iter = maker_tbl.get(owner.value, "no such pst maker");

        auto now_time = time_point_sec(current_time_point());
        uint64_t now_time_t = now_time.sec_since_epoch();
        uint64_t updated_at_t = ust->updated_at.sec_since_epoch();
        uint64_t bill_dmc_claims_interval = get_dmc_config("billinter"_n, default_bill_dmc_claims_interval);
        uint64_t max_dmc_claims_interval = ust->created_at.sec_since_epoch() + bill_dmc_claims_interval;

        now_time_t = now_time_t >= max_dmc_claims_interval ? max_dmc_claims_interval : now_time_t;

        if (updated_at_t <= max_dmc_claims_interval) {
            uint64_t duration = now_time_t - updated_at_t;
            check(duration <= now_time_t, "subtractive overflow");  // never happened

            extended_asset quantity = get_asset_by_amount<double, std::floor>(
                incentive_rate * duration * ust->unmatched.quantity.amount * get_dmc_config("bmrate"_n, default_benchmark_stake_rate) / 100.0 / bill_dmc_claims_interval,
                rsi_sym);

            if (quantity.quantity.amount > 0) {
                extended_asset dmc_quantity = get_dmc_by_vrsi(quantity);

                if (dmc_quantity.quantity.amount > 0) {
                    maker_tbl.modify(iter, ram_payer, [&](auto& s) {
                        s.total_staked += dmc_quantity;
                        s.current_rate = cal_current_rate(s.total_staked, owner, s.get_real_m());
                    });
                    SEND_INLINE_ACTION(*this, incentiverec, {_self, "active"_n}, {owner, dmc_quantity, bill_id});
                }
            } else {
                // if quantity is 0, don't update updated_at
                now_time_t = updated_at_t;
            }
        }
        return now_time_t;
    } else {
        bill_stats sst(get_self(), get_self().value);
        auto ust = sst.find(bill_id);
        check(ust != sst.end(), "no such record");
        check(ust->owner == owner, "bill_id not belong to you");
        dmc_makers maker_tbl(get_self(), get_self().value);
        const auto& iter = maker_tbl.get(owner.value, "no such pst maker");

        auto now_time = time_point_sec(current_time_point());
        uint64_t now_time_t = now_time.sec_since_epoch();
        uint64_t updated_at_t = ust->updated_at.sec_since_epoch();
        uint64_t bill_dmc_claims_interval = get_dmc_config("billinter"_n, default_bill_dmc_claims_interval);
        uint64_t max_dmc_claims_interval = ust->created_at.sec_since_epoch() + bill_dmc_claims_interval;

        now_time_t = now_time_t >= max_dmc_claims_interval ? max_dmc_claims_interval : now_time_t;

        if (updated_at_t <= max_dmc_claims_interval) {
            uint64_t duration = now_time_t - updated_at_t;
            check(duration <= now_time_t, "subtractive overflow");  // never happened

            extended_asset quantity = get_asset_by_amount<double, std::floor>(
                incentive_rate * duration * ust->unmatched.quantity.amount * get_dmc_config("bmrate"_n, default_benchmark_stake_rate) / 100.0 / bill_dmc_claims_interval,
                rsi_sym);

            if (quantity.quantity.amount > 0) {
                extended_asset dmc_quantity = get_dmc_by_vrsi(quantity);

                if (dmc_quantity.quantity.amount > 0) {
                    maker_tbl.modify(iter, ram_payer, [&](auto& s) {
                        s.total_staked += dmc_quantity;
                        s.current_rate = cal_current_rate(s.total_staked, owner, s.get_real_m());
                    });
                    SEND_INLINE_ACTION(*this, incentiverec, {_self, "active"_n}, {owner, dmc_quantity, bill_id});
                }
            } else {
                // if quantity is 0, don't update updated_at
                now_time_t = updated_at_t;
            }
        }
        return now_time_t;
    }
}

void token::setabostats(uint64_t stage, double user_rate, double foundation_rate, extended_asset total_release, extended_asset remaining_release, time_point_sec start_at, time_point_sec end_at, time_point_sec last_released_at) {
    require_auth(dmc_account);
    check(stage >= 1 && stage <= 11, "invalid stage");
    check(user_rate <= 1 && user_rate >= 0, "invalid user_rate");
    check(foundation_rate + user_rate == 1, "invalid foundation_rate");
    check(start_at < end_at, "invalid time");
    check(total_release.get_extended_symbol() == dmc_sym, "invalid symbol");

    abostats ast(get_self(), get_self().value);
    const auto& st = ast.find(stage);
    if (st != ast.end()) {
        ast.modify(st, get_self(), [&](auto& a) {
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
            a.remaining_release = remaining_release;
            a.start_at = start_at;
            a.end_at = end_at;
            a.last_user_released_at = last_released_at;
            a.last_foundation_released_at = last_released_at;
        });
    }
}

void token::setdmcconfig(name key, uint64_t value) {
    require_auth(config_account);
    dmc_global dmc_global_tbl(get_self(), get_self().value);
    switch (key.value) {
        case ("claiminter"_n).value:
            check(value > 0, "invalid claims interval");
            break;
        default:
            break;
    }
    auto config_itr = dmc_global_tbl.find(key.value);
    if (config_itr == dmc_global_tbl.end()) {
        dmc_global_tbl.emplace(_self, [&](auto& conf) {
            conf.key = key;
            conf.value = value;
        });
    } else {
        dmc_global_tbl.modify(config_itr, get_self(), [&](auto& conf) {
            conf.value = value;
        });
    }
}

uint64_t token::get_dmc_config(name key, uint64_t default_value) {
    dmc_global dmc_global_tbl(get_self(), get_self().value);
    auto dmc_global_iter = dmc_global_tbl.find(key.value);
    if (dmc_global_iter != dmc_global_tbl.end())
        return dmc_global_iter->value;
    return default_value;
}

void token::set_dmc_config(name key, uint64_t value) {
    dmc_global dmc_global_tbl(get_self(), get_self().value);
    auto config_itr = dmc_global_tbl.find(key.value);

    if (config_itr == dmc_global_tbl.end()) {
        dmc_global_tbl.emplace(_self, [&](auto& conf) {
            conf.key = key;
            conf.value = value;
        });
    } else {
        dmc_global_tbl.modify(config_itr, get_self(), [&](auto& conf) {
            conf.value = value;
        });
    }
}

double token::get_benchmark_price() {
    bc_price_table bptb(get_self(), get_self().value);
    auto bptb_iter = bptb.begin();
    if (bptb_iter == bptb.end()) {
        auto avg_price = get_dmc_config("initalprice"_n, default_initial_price) / 100.0;
        return avg_price;
    }
    return bptb_iter->benchmark_price;
}

double token::get_dmc_rate(uint64_t rate_value) {
    double value = rate_value / 100.0;
    return value * get_benchmark_price();
}

void token::trace_price_history(double price, uint64_t bill_id, uint64_t order_id) {
    price_table ptb(get_self(), get_self().value);
    auto iter = ptb.get_index<"bytime"_n>();
    auto now_time = time_point_sec(current_time_point());
    const uint64_t max_price_distance = get_dmc_config("pricedist"_n, default_max_price_distance);

    std::vector<double> prices;
    prices.push_back(price);

    int times = 0;
    auto rawtime = now_time;
    for (auto it = iter.begin(); it != iter.end();) {
        prices.push_back(it->price);
        if (rawtime.sec_since_epoch() / day_sec != it->created_at.sec_since_epoch() / day_sec) {
            times++;
        }
        if (times >= max_price_distance) {
            // remove the oldest price
            prices.pop_back();
            it = iter.erase(it);
        } else {
            rawtime = it->created_at;
            it++;
        }
    }

    ptb.emplace(_self, [&](auto& p) {
        p.primary = ptb.available_primary_key();
        p.bill_id = bill_id;
        p.order_id = order_id;
        p.price = price;
        p.created_at = now_time;
    });

    // calculate median price
    std::sort(prices.begin(), prices.end());
    auto size = prices.size();
    double bc_price = 0;
    if (size % 2 == 0) {
        bc_price = (prices[size / 2 - 1] + prices[size / 2]) / 2;
    } else {
        bc_price = prices[size / 2];
    }
    // convert to 4 decimal places
    bc_price = std::floor(bc_price * 10000) / 10000.0;
    bc_price_table bptb(get_self(), get_self().value);
    auto bptb_iter = bptb.begin();
    if (bptb_iter == bptb.end()) {
        bptb_iter = bptb.emplace(_self, [&](auto& a) {
            a.prices = prices;
            a.benchmark_price = bc_price;
        });
    } else {
        bptb.modify(bptb_iter, _self, [&](auto& a) {
            a.prices = prices;
            a.benchmark_price = bc_price;
        });
    }
}

void token::adjustprice(string memo) {
    bc_price_table bptb(get_self(), get_self().value);
    auto bptb_iter = bptb.begin();
    if (bptb_iter == bptb.end()) {
        return;
    }
    bptb.modify(bptb_iter, _self, [&](auto& a) {
        a.benchmark_price = std::floor(a.benchmark_price * 10000) / 10000.0;
    });
}

void token::allocation(string memo) {
    require_auth(dmc_account);
    check(memo.size() <= 256, "memo has more than 256 bytes");
    abostats ast(get_self(), get_self().value);
    extended_asset to_foundation(0, dmc_sym);

    auto now_time = time_point_sec(current_time_point());
    for (auto it = ast.begin(); it != ast.end();) {
        if (now_time > it->end_at) {
            if (it->remaining_release.quantity.amount != 0) {
                to_foundation.quantity.amount = it->remaining_release.quantity.amount * it->foundation_rate;
                ast.modify(it, get_self(), [&](auto& a) {
                    a.last_foundation_released_at = it->end_at;
                    a.remaining_release -= to_foundation;
                });
            }
            it++;
        } else if (now_time < it->start_at) {
            break;
        } else {
            auto duration_time = now_time.sec_since_epoch() - it->last_foundation_released_at.sec_since_epoch();
            if (duration_time / 86400 == 0)  // 24 * 60 * 60
                break;
            auto remaining_time = it->end_at.sec_since_epoch() - it->last_foundation_released_at.sec_since_epoch();
            double per = (double)duration_time / (double)remaining_time;
            if (per > 1)
                per = 1;
            uint64_t total_asset_amount = per * it->remaining_release.quantity.amount * it->foundation_rate;
            if (total_asset_amount != 0) {
                extended_asset curr_release(total_asset_amount, dmc_sym);
                to_foundation += curr_release;
                ast.modify(it, get_self(), [&](auto& a) {
                    a.last_foundation_released_at = now_time;
                    a.remaining_release -= curr_release;
                });
            }
            break;
        }
    }

    if (to_foundation.quantity.amount != 0) {
        SEND_INLINE_ACTION(*this, issue, {dmc_account, "active"_n}, {system_account, to_foundation.quantity, "allocation to foundation"});
    }
}
}  // namespace eosio
