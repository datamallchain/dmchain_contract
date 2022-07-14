/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */

#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::change_order(dmc_order& order, const dmc_challenge& challenge, time_point_sec current, uint64_t claims_interval, name payer)
{
    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    if (!is_challenge_end(challenge.state)) {
        return;
    }
    if (order.state == OrderStateWaiting) {
        if (challenge.state == ChallengeConsistent) {
            order.state = OrderStateDeliver;
            order.deliver_start_date = time_point_sec(now());
            order.latest_settlement_date = time_point_sec(now());
        }
    } else if (order.state == OrderStateDeliver) {
        uint64_t per_claims_interval = claims_interval * 6 / 7;
        if (order.latest_settlement_date + per_claims_interval > current) {
            return;
        }
        if (order.user_pledge >= order.price) {
            order.user_pledge -= order.price;
            order.lock_pledge += order.price;
            order.state = OrderStatePreCont;
            SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
                { order.order_id, -order.price, order.price, zero_dmc, zero_dmc, order.latest_settlement_date + per_claims_interval, OrderReceiptUpdate });
        } else {
            order.state = OrderStatePreEnd;
        }
    } else if (order.state == OrderStatePreCont) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        order.lock_pledge -= order.price;
        order.settlement_pledge += order.price;
        order.state = OrderStateDeliver;
        order.latest_settlement_date += claims_interval;
        SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
            { order.order_id, zero_dmc, -order.price, order.price, zero_dmc, order.latest_settlement_date, OrderReceiptUpdate });
    } else if (order.state == OrderStatePreEnd) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        order.lock_pledge -= order.price;
        order.settlement_pledge += order.price;
        order.state = OrderStateEnd;
        order.latest_settlement_date += claims_interval;
        destory_pst(order);
        SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
            { order.order_id, zero_dmc, -order.price, order.price, zero_dmc, order.latest_settlement_date, OrderReceiptUpdate });
        if (order.lock_pledge.amount > 0) {
            order.user_pledge += order.lock_pledge;
            SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
                { order.order_id, order.lock_pledge, -order.lock_pledge,
                    extended_asset(0, dmc_sym), extended_asset(0, dmc_sym), time_point_sec(now()), OrderReceiptLockRet });
            order.lock_pledge = extended_asset(0, order.lock_pledge.get_extended_symbol());
        }
    }
}

void token::update_order(dmc_order& order, const dmc_challenge& challenge, name payer)
{
    auto current_time = time_point_sec(now());
    uint64_t claims_interval = get_dmc_config(name { N(claiminter) }, default_dmc_claims_interval);
    auto tmp_order = order;
    while (true) {
        change_order(order, challenge, current_time, claims_interval, payer);
        if (tmp_order.state == order.state) {
            break;
        }
        tmp_order = order;
    }
}

void token::destory_pst(const dmc_order& info)
{
    dmc_makers maker_tbl(_self, _self);
    auto iter = maker_tbl.find(info.miner);
    eosio_assert(iter != maker_tbl.end(), "cannot find miner in dmc maker");
    sub_stats(info.miner_pledge);
    change_pst(info.miner, -(info.miner_pledge));
    maker_tbl.modify(iter, 0, [&](auto& m) {
        m.current_rate = cal_current_rate(iter->total_staked, info.miner);
    });
    bill_stats sst(_self, info.miner);
    auto ust_idx = sst.get_index<N(byid)>();
    auto ust = ust_idx.find(info.bill_id);
    if (ust != ust_idx.end()) {
        ust_idx.modify(ust, 0, [&](auto& s) {
            s.matched -= info.miner_pledge;
        });
    }
}

void token::claim_dmc_reward(const dmc_order& info, dmc_challenge& challenge, account_name payer)
{
    double benchmark_stake_rate = get_dmc_config(name { N(bmrate) }, default_benchmark_stake_rate) / 100.0;
    auto miner_pledge_amount = extended_asset(round(info.settlement_pledge.amount * miner_scale), info.settlement_pledge.get_extended_symbol());
    auto lp_pledge_amount = info.settlement_pledge - miner_pledge_amount;
    dmc_makers maker_tbl(_self, _self);
    auto iter = maker_tbl.find(info.miner);
    eosio_assert(iter != maker_tbl.end(), "cannot find miner in dmc maker");
    maker_tbl.modify(iter, payer, [&](auto& o) {
        o.total_staked = o.total_staked + lp_pledge_amount;
    });
    SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { _self, info.miner, lp_pledge_amount, MakerReceiptClaim });

    auto miner_origin_pay = challenge.miner_pay;
    challenge.miner_pay = miner_pledge_amount > miner_origin_pay ? extended_asset(0, miner_origin_pay.get_extended_symbol()) : miner_origin_pay - miner_pledge_amount;
    miner_pledge_amount = miner_pledge_amount > miner_origin_pay ? miner_pledge_amount - miner_origin_pay : extended_asset(0, miner_origin_pay.get_extended_symbol());
    add_balance(info.miner, miner_pledge_amount, payer);
    add_balance(abo_account, miner_origin_pay - challenge.miner_pay, payer);
    SEND_INLINE_ACTION(*this, assetcharec, { _self, N(active) }, { abo_account, miner_origin_pay - challenge.miner_pay, 1, info.order_id });

    SEND_INLINE_ACTION(*this, orderclarec, { _self, N(active) }, { info.miner, miner_pledge_amount, info.bill_id, info.order_id });

    uint64_t epoch = std::round((double)info.settlement_pledge.amount / info.price.amount);
    auto user_reward = extended_asset(round(double(info.miner_pledge.amount * epoch) * pow(10, rsi_sym.precision() - info.miner_pledge.get_extended_symbol().precision())), rsi_sym);
    auto miner_reward = extended_asset(round(user_reward.amount * (1 + benchmark_stake_rate)), rsi_sym);
    add_balance(info.user, user_reward, payer);
    SEND_INLINE_ACTION(*this, incentiverec, { _self, N(active) }, { info.user, user_reward, info.bill_id, info.order_id, 1 });
    add_stats(user_reward);
    add_balance(info.miner, miner_reward, payer);
    SEND_INLINE_ACTION(*this, incentiverec, { _self, N(active) }, { info.miner, miner_reward, info.bill_id, info.order_id, 1 });
    add_stats(miner_reward);
    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { info.order_id, zero_dmc, zero_dmc, -info.settlement_pledge, zero_dmc, time_point_sec(now()), OrderReceiptClaim });
}

void token::updateorder(name payer, uint64_t order_id)
{
    require_auth(payer);
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);

    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });
}

void token::claimorder(name payer, uint64_t order_id)
{
    require_auth(payer);
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);
    eosio_assert(order_info.settlement_pledge.amount > 0, "no settlement pledge to claim");

    auto challenge = *challenge_iter;
    claim_dmc_reward(order_info, challenge, payer);
    order_info.settlement_pledge = extended_asset(0, order_info.settlement_pledge.get_extended_symbol());

    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });

    challenge_tbl.modify(challenge_iter, payer, [&](auto& c) {
        c.miner_pay = challenge.miner_pay;
    });
}

void token::addordasset(name sender, uint64_t order_id, extended_asset quantity)
{
    require_auth(sender);
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(order_iter->user == sender, "only user can add order asset");
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, sender);
    sub_balance(sender, quantity);

    order_info.user_pledge += quantity;
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order_info;
    });
    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, quantity, zero_dmc, zero_dmc, zero_dmc, time_point_sec(now()), OrderReceiptUser });
}

void token::subordasset(name sender, uint64_t order_id, extended_asset quantity)
{
    require_auth(sender);
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(order_iter->user == sender, "only user can sub order asset");
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, sender);
    eosio_assert(order_info.user_pledge >= quantity, "not enough user pledge");
    add_balance(sender, quantity, sender);

    order_info.user_pledge -= quantity;
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order_info;
    });

    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, -quantity, zero_dmc, zero_dmc, zero_dmc, time_point_sec(now()), OrderReceiptUser });
}

void token::ordermig(account_name payer, uint32_t limit)
{
    require_auth(payer);
    order_migration_table order_migration_tbl(_self, _self);
    if (order_migration_tbl.begin() == order_migration_tbl.end()) {
        return;
    }
    dmc_orders order_tbl(_self, _self);
    dmc_challenges challenge_tbl(_self, _self);
    auto order_iter = order_tbl.upper_bound(order_migration_tbl.begin()->current_id);
    uint64_t claims_interval = get_dmc_config(name { N(claiminter) }, default_dmc_claims_interval);
    uint64_t per_claims_interval = claims_interval * 6 / 7;
    if (order_iter == order_tbl.end()) {
        return;
    }
    dmc_order order_info = *order_iter;
    for (size_t i = 0; (order_iter != order_tbl.end()) && (i < limit); order_iter++, i++) {
        order_info = *order_iter;
        if (order_iter->state == 4) {
            continue;
        }
        if (order_iter->deliver_start_date + per_claims_interval > time_point_sec(now())) {
            order_info.state = OrderStateDeliver;
            order_info.lock_pledge = order_info.price;
            order_info.user_pledge = order_info.user_pledge > order_info.price ? order_info.user_pledge - order_info.price : extended_asset(0, dmc_sym);
        } else if (order_iter->deliver_start_date + claims_interval > time_point_sec(now())) {
            order_info.state = OrderStatePreEnd;
            order_info.lock_pledge = order_info.price;
            order_info.user_pledge = order_info.user_pledge > order_info.price ? order_info.user_pledge - order_info.price : extended_asset(0, dmc_sym);
        } else {
            order_info.state = OrderStateEnd;
            order_info.user_pledge = order_info.user_pledge > order_info.price ? order_info.user_pledge - order_info.price : extended_asset(0, dmc_sym);
            order_info.settlement_pledge = order_info.price;
            order_info.latest_settlement_date = order_iter->deliver_start_date + claims_interval;
        }
        auto challenge_iter = challenge_tbl.find(order_info.order_id);
        if (challenge_iter == challenge_tbl.end()) {
            challenge_tbl.emplace(payer, [&](auto& c) {
                c.order_id = order_info.order_id;
                c.merkle_submitter = name { _self };
                c.merkle_root = checksum256();
                c.data_block_count = 0;
                c.pre_merkle_root = checksum256();
                c.pre_data_block_count = 0;
                c.merkle_submitter = name { _self };
                c.challenge_times = 0;
                c.state = ChallengeConsistent;
                c.user_lock = extended_asset(0, dmc_sym);
                c.miner_pay = extended_asset(0, dmc_sym);
            });
        }
        order_tbl.modify(order_iter, payer, [&](auto& c) {
            c = order_info;
        });
    }
    set_order_migration(order_info.order_id, payer);
}

void token::set_order_migration(uint64_t order_id, account_name payer)
{
    order_migration_table order_migration_tbl(_self, _self);
    if (order_migration_tbl.begin() == order_migration_tbl.end()) {
        order_migration_tbl.emplace(payer, [&](auto& o) {
            o.current_id = order_id;
            o.begin_date = time_point_sec(now());
        });
    } else {
        auto order_iter = order_migration_tbl.begin();
        order_migration_tbl.modify(order_iter, payer, [&](auto& o) {
            o.current_id = order_id;
        });
    }
}

}
