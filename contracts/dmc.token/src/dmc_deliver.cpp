/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::update_order_asset(dmc_order& order, OrderState new_state, uint64_t claims_interval) {
    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    auto iter = maker_snapshot_tbl.find(order.order_id);
    check(iter != maker_snapshot_tbl.end(), "cannot find miner in dmc maker");
    auto user_rsi = extended_asset(round(double(order.miner_lock_pst.quantity.amount) * pow(10, rsi_sym.get_symbol().precision() - order.miner_lock_pst.get_extended_symbol().get_symbol().precision())), rsi_sym);
    auto miner_rsi_total = extended_asset(round(user_rsi.quantity.amount * (1 + iter->rate / 100.0)), rsi_sym);
    auto dmc_pledge = extended_asset(order.price.quantity.amount / 2, (order.price.get_extended_symbol()));
    auto miner_rsi_pledge = extended_asset(miner_rsi_total.quantity.amount / 2, (miner_rsi_total.get_extended_symbol()));

    order.lock_pledge -= dmc_pledge;
    order.settlement_pledge += dmc_pledge;
    order.miner_lock_rsi += miner_rsi_total - miner_rsi_pledge;
    order.miner_rsi += miner_rsi_pledge;
    order.user_rsi += user_rsi;
    order.state = new_state;
    order.latest_settlement_date += claims_interval;
}

void token::change_order(dmc_order& order, const dmc_challenge& challenge, time_point_sec current, uint64_t claims_interval, name payer)
{
    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    if (!is_challenge_end(challenge.state)) {
        return;
    }
    if (order.state == OrderStateWaiting) {
        if (challenge.state == ChallengeConsistent) {
            order.state = OrderStateDeliver;
            order.deliver_start_date = time_point_sec(current_time_point());
            order.latest_settlement_date = time_point_sec(current_time_point());
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
        } else {
            order.state = OrderStatePreEnd;
        }
    } else if (order.state == OrderStatePreCont) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        if (order.cancel_date != time_point_sec(current_time_point()) && order.cancel_date + claims_interval < current) {
             order.state = OrderStatePreCancel;
        }
        update_order_asset(order, OrderStateDeliver, claims_interval);
    } else if (order.state == OrderStatePreEnd) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        update_order_asset(order, OrderStateEnd, claims_interval);
        distribute_lp_pool(order.order_id, order.miner_lock_dmc, extended_asset(0, dmc_sym), get_self(), false);
        order.miner_lock_dmc = extended_asset(0, order.miner_lock_dmc.get_extended_symbol());
    } else if (order.state == OrderStatePreCancel) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        update_order_asset(order, OrderStateCancel, claims_interval);
        distribute_lp_pool(order.order_id, order.miner_lock_dmc, extended_asset(0, dmc_sym), get_self(), false);
        order.miner_lock_dmc = extended_asset(0, order.miner_lock_dmc.get_extended_symbol());
        if (order.deposit_valid >= order.latest_settlement_date) {
            distribute_lp_pool(order.order_id,  order.deposit, challenge.miner_pay, payer, true);
        } else {
            add_balance(order.user, order.deposit, payer);
            SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order.order_id, { order.deposit }, order.user,  ACC_TYPE_USER, OrderReceiptDeposit});
        }
        order.deposit = extended_asset(0, order.deposit.get_extended_symbol());
    } else if (order.state == OrderStateCancel || order.state == OrderStateEnd) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
        auto iter = maker_snapshot_tbl.find(order.order_id);
        check(iter != maker_snapshot_tbl.end(), "cannot find miner in dmc maker");
        auto user_rsi = extended_asset(round(double(order.price.quantity.amount) * pow(10, rsi_sym.get_symbol().precision() - order.price.get_extended_symbol().get_symbol().precision())), rsi_sym);
        auto miner_rsi_pledge = extended_asset(round(user_rsi.quantity.amount * (1 + iter->rate / 100.0)) / 2, rsi_sym);
        auto dmc_pledge = extended_asset(order.price.quantity.amount / 2, (order.price.get_extended_symbol()));
        
        if (order.lock_pledge >= dmc_pledge) {
            order.lock_pledge -= dmc_pledge;
            order.settlement_pledge += dmc_pledge;
            order.latest_settlement_date += claims_interval;
        }
        if (order.miner_lock_rsi >= miner_rsi_pledge) {
            order.miner_lock_rsi -= miner_rsi_pledge;
            order.miner_rsi += miner_rsi_pledge;
        }
    }
}

void token::update_order(dmc_order& order, const dmc_challenge& challenge, name payer)
{
    phishing_challenge();
    auto current_time = time_point_sec(current_time_point());
    uint64_t claims_interval = get_dmc_config("claiminter"_n, default_dmc_claims_interval);
    auto tmp_order = order;
    while (true) {
        change_order(order, challenge, current_time, claims_interval, payer);
        if (tmp_order.state == order.state && tmp_order.latest_settlement_date == order.latest_settlement_date) {
            break;
        }
        tmp_order = order;
    }
}

void token::generate_maker_snapshot(uint64_t order_id, uint64_t bill_id, name miner, name payer) {
    dmc_makers maker_tbl(get_self(), get_self().value);
    auto maker_iter = maker_tbl.find(miner.value);
    check(maker_iter != maker_tbl.end(), "can't find maker pool");
    double r = std::round(maker_iter->current_rate * 100 / get_avg_price());

    dmc_maker_pool dmc_pool(get_self(), miner.value);
    std::vector<maker_lp_pool> lps;
    for (auto iter = dmc_pool.begin(); iter != dmc_pool.end(); iter++) {
        lps.emplace_back(maker_lp_pool{
            .owner = iter->owner,
            .ratio = iter->weight / maker_iter->total_weight,
        });
    }

    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    check(maker_snapshot_tbl.find(order_id) == maker_snapshot_tbl.end(), "order snapshot already exists");
    maker_snapshot_tbl.emplace(payer, [&](auto& mst) {
        mst.order_id = order_id;
        mst.miner = maker_iter->miner;
        mst.bill_id = bill_id;
        mst.rate = r;
        mst.lps = lps;
    });
}

extended_asset token::distribute_lp_pool(uint64_t order_id, extended_asset pledge, extended_asset challenge_pledge, name payer, OrderReceiptType rec_type) {
    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    auto snapshot_iter = maker_snapshot_tbl.find(order_id);
    check(snapshot_iter != maker_snapshot_tbl.end(), "order snapshot not exists");

    dmc_makers maker_tbl(get_self(), get_self().value);
    auto miner = snapshot_iter->miner;
    auto maker_iter = maker_tbl.find(miner.value);
    auto remain_pay = extended_asset(0, pledge.get_extended_symbol());
    if (rec_type == OrderReceiptClaim) {
        double current_r = snapshot_iter->rate / 100.0;
        auto miner_dmc_pledge = extended_asset(round(pledge.quantity.amount / (current_r + 1.0)), pledge.get_extended_symbol());
        auto remain_pay = miner_dmc_pledge.quantity > challenge_pledge.quantity ? extended_asset(0, challenge_pledge.get_extended_symbol()) : challenge_pledge - miner_dmc_pledge;
        miner_dmc_pledge = miner_dmc_pledge.quantity > challenge_pledge.quantity ? miner_dmc_pledge - challenge_pledge : extended_asset(0, miner_dmc_pledge.get_extended_symbol());
        increase_penalty(challenge_pledge - remain_pay);
        if (miner_dmc_pledge.quantity.amount) {
            add_balance(miner, miner_dmc_pledge, payer);
            SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { miner_dmc_pledge }, miner,  ACC_TYPE_MINER, OrderReceiptClaim});
        }
        pledge -= miner_dmc_pledge;
    }

    auto sub_pledge = extended_asset(0, pledge.get_extended_symbol());

    dmc_maker_pool dmc_pool(get_self(), miner.value);
    for (auto iter = snapshot_iter->lps.begin(); iter != snapshot_iter->lps.end(); iter++) {
        auto owner_pledge = extended_asset(std::floor(iter->ratio * pledge.quantity.amount), pledge.get_extended_symbol());
        auto pool_iter = dmc_pool.find(iter->owner.value);
        if (pool_iter != dmc_pool.end()) {
            double owner_weight = (double)owner_pledge.quantity.amount / maker_iter->total_staked.quantity.amount * maker_iter->total_weight;
            dmc_pool.modify(pool_iter, payer, [&](auto& p) {
                p.weight = p.weight + owner_weight;
            });
        } else {
            add_balance(iter->owner, owner_pledge, payer);
            sub_pledge += owner_pledge;
        }
    }

    extended_asset new_total = maker_iter->total_staked + (pledge - sub_pledge);
    double weight_change = (double)(pledge - sub_pledge).quantity.amount / maker_iter->total_staked.quantity.amount * maker_iter->total_weight;
    double r = cal_current_rate(new_total, miner);
    maker_tbl.modify(maker_iter, get_self(), [&](auto& m) {
        m.total_weight = m.total_weight + weight_change;
        m.total_staked = new_total;
        m.current_rate = r;
    });
    SEND_INLINE_ACTION(*this, makercharec, { _self, "active"_n }, { _self, miner, pledge - sub_pledge, MakerReceiptClaim });
    return remain_pay;
}

void token::updateorder(name payer, uint64_t order_id)
{
    require_auth(payer);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);

    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter });
}

void token::claimdeposit(name payer, uint64_t order_id) {
    require_auth(payer);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);

    check(payer == order_iter->user, "only order user can claim deposit");
    check(order_info.deposit_valid < order_info.latest_settlement_date, "order not reach end, can not deposit");
    add_balance(order_info.user, order_info.deposit, payer);
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.deposit }, order_info.user,  ACC_TYPE_USER, OrderReceiptDeposit});
    order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });
}

void token::claimorder(name payer, uint64_t order_id)
{
    require_auth(payer);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);
    check(order_info.settlement_pledge.quantity.amount > 0, "no settlement pledge to claim");

    auto challenge = *challenge_iter;
    auto user_dmc = get_dmc_by_vrsi(order_info.user_rsi);
    add_balance(order_info.user, user_dmc, payer);
    auto miner_total_dmc = get_dmc_by_vrsi(order_info.miner_rsi) + order_info.settlement_pledge;
    auto miner_remain_pay = distribute_lp_pool(order_info.order_id, miner_total_dmc, challenge.miner_pay, payer, true);

    order_info.user_rsi = extended_asset(0, order_info.user_rsi.get_extended_symbol());
    order_info.settlement_pledge = extended_asset(0, order_info.settlement_pledge.get_extended_symbol());
    order_info.miner_rsi = extended_asset(0, order_info.miner_rsi.get_extended_symbol());

    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });

    challenge_tbl.modify(challenge_iter, payer, [&](auto& c) {
        c.miner_pay = miner_remain_pay;
    });

    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { *challenge_iter });
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { user_dmc }, order_info.user,  ACC_TYPE_USER, OrderReceiptClaim});
}

void token::addordasset(name sender, uint64_t order_id, extended_asset quantity)
{
    require_auth(sender);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    check(order_iter->user == sender, "only user can add order asset");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, sender);
    sub_balance(sender, quantity);

    order_info.user_pledge += quantity;
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order_info;
    });
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n },
        { order_id, { quantity }, order_info.user,  ACC_TYPE_USER, OrderReceiptUser});
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter });
}

void token::subordasset(name sender, uint64_t order_id, extended_asset quantity)
{
    require_auth(sender);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    check(order_iter->user == sender, "only user can sub order asset");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, sender);
    check(order_info.user_pledge >= quantity, "not enough user pledge");
    add_balance(sender, quantity, sender);

    order_info.user_pledge -= quantity;
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order_info;
    });

   SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n },
        { order_id, { -quantity }, order_info.user,  ACC_TYPE_USER, OrderReceiptUser});
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter });
}

void token::cancelorder(name sender, uint64_t order_id) {
    require_auth(sender);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto order_info = *order_iter;
    auto challenge_info = *challenge_iter;
    update_order(order_info, challenge_info, sender);
    check(is_challenge_end(challenge_info.state) || challenge_info.state == ChallengePrepare, "order in challenge state");
    if (order_info.state == OrderStateWaiting) {
        check(challenge_info.state == ChallengePrepare, "invalid challenge state");
        order_info.state = OrderStateCancel;
        challenge_info.state = ChallengeCancel;
        distribute_lp_pool(order_info.order_id, order_info.miner_lock_dmc, extended_asset(0, dmc_sym), get_self(), false);
        order_info.miner_lock_dmc = extended_asset(0, order_info.miner_lock_dmc.get_extended_symbol());
        order_info.lock_pledge = extended_asset(0, order_info.lock_pledge.get_extended_symbol());
        order_info.user_pledge = extended_asset(0, order_info.user_pledge.get_extended_symbol());
        order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
        add_balance(order_info.user, order_info.lock_pledge + order_info.user_pledge + order_info.deposit, sender);
        SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n },
        { order_id, { order_info.lock_pledge, order_info.user_pledge, order_info.deposit }, order_info.user,  ACC_TYPE_USER, OrderReceiptUser});
    } else if (order_info.state == OrderStateDeliver) {
       check(order_info.deposit_valid <= time_point_sec(current_time_point()), "invalid time, can't cancel");
       order_info.cancel_date = time_point_sec(current_time_point());
    } else {
        check(false, "invalid order state");
    }
   
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order_info;
    });
    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c = challenge_info;
    });
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { order_info });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { challenge_info });
}

}
