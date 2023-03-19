/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::delete_order_pst(const dmc_order& order) {
    if (get_dmc_config("deletepstid"_n, default_id_start) > order.order_id ) {
        return;
    }
    lock_sub_balance(order.miner, order.miner_lock_pst, time_point_sec(uint32_max));
    send_totalvote_to_system(order.miner);
}

void token::update_order_asset(dmc_order& order, OrderState new_state, uint64_t claims_interval) {
    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    auto iter = maker_snapshot_tbl.find(order.order_id);
    check(iter != maker_snapshot_tbl.end(), "cannot find miner in dmc maker");
    auto user_rsi = extended_asset(round(double(order.miner_lock_pst.quantity.amount) * pow(10, rsi_sym.get_symbol().precision() - order.miner_lock_pst.get_extended_symbol().get_symbol().precision())), rsi_sym);
    auto miner_rsi_total = extended_asset(round(user_rsi.quantity.amount * (1 + iter->rate / 100.0)), rsi_sym);
    auto dmc_pledge = extended_asset(order.price.quantity.amount / 2, (order.price.get_extended_symbol()));
    auto miner_rsi_pledge = extended_asset(miner_rsi_total.quantity.amount / 2, (miner_rsi_total.get_extended_symbol()));
    // SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order.order_id, { {miner_rsi_pledge, OrderReceiptReward}, {dmc_pledge, OrderReceiptClaim}}, order.miner, ACC_TYPE_MINER, order.latest_settlement_date});
    // SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order.order_id, { {user_rsi, OrderReceiptReward} }, order.user,  ACC_TYPE_USER, order.latest_settlement_date});

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
    if ((!is_challenge_end(challenge.state)) && (challenge.state != ChallengeTimeout)) {
        return;
    }
    if (order.state == OrderStateWaiting) {
        if (challenge.state == ChallengeConsistent) {
            order.state = OrderStateDeliver;
            order.deliver_start_date = time_point_sec(current_time_point());
            order.deposit_valid = time_point_sec(current_time_point() + eosio::seconds(claims_interval * order.epoch));
            order.latest_settlement_date = time_point_sec(current_time_point());
        }
    } else if (order.state == OrderStateDeliver) {
        uint64_t per_claims_interval = claims_interval * 6 / 7;
        if (order.latest_settlement_date + per_claims_interval > current) {
            return;
        }
        if (order.cancel_date != time_point_sec() && order.cancel_date + claims_interval < current) {
             order.state = OrderStatePreCancel;
             return;
        }
        if (order.user_pledge >= order.price) {
            order.user_pledge -= order.price;
            order.lock_pledge += order.price;
            order.state = OrderStatePreCont;
            SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order.order_id, { {-order.price, OrderReceiptRenew} }, order.user,  ACC_TYPE_USER, order.latest_settlement_date + per_claims_interval});
        } else {
            order.state = OrderStatePreEnd;
        }
    } else if (order.state == OrderStatePreCont) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        update_order_asset(order, OrderStateDeliver, claims_interval);
    } else if (order.state == OrderStatePreEnd) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        update_order_asset(order, OrderStateEnd, claims_interval);
        std::vector<asset_type_args> rewards;
        rewards.push_back({order.miner_lock_dmc, AssetReceiptMinerLock});
        delete_order_pst(order);
        if (order.deposit.quantity.amount > 0) {
            if (order.deposit_valid > order.latest_settlement_date) {
                rewards.push_back({order.deposit, AssetReceiptDeposit});
            } else {
                add_balance(order.user, order.deposit, payer);
                SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order.order_id, { order.deposit }, order.user, AssetReceiptDeposit});
            }
        }
        distribute_lp_pool(order.order_id, rewards,extended_asset(0, dmc_sym), payer);
        order.miner_lock_dmc = extended_asset(0, order.miner_lock_dmc.get_extended_symbol());
        order.deposit = extended_asset(0, order.deposit.get_extended_symbol());
    } else if (order.state == OrderStatePreCancel) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        update_order_asset(order, OrderStateCancel, claims_interval);
        std::vector<asset_type_args> rewards;
        rewards.push_back({order.miner_lock_dmc, AssetReceiptMinerLock});
        delete_order_pst(order);
        if (order.deposit.quantity.amount > 0) {
            if (order.deposit_valid > order.latest_settlement_date) {
                rewards.push_back({order.deposit, AssetReceiptDeposit});
            } else {
                add_balance(order.user, order.deposit, payer);
                SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order.order_id, { order.deposit }, order.user, AssetReceiptDeposit});
            }
        }
        distribute_lp_pool(order.order_id, rewards,extended_asset(0, dmc_sym), payer);
        order.miner_lock_dmc = extended_asset(0, order.miner_lock_dmc.get_extended_symbol());
        order.deposit = extended_asset(0, order.deposit.get_extended_symbol());
    } else if (order.state == OrderStateCancel || order.state == OrderStateEnd) {
        if (order.latest_settlement_date + claims_interval > current) {
            return;
        }
        auto dmc_pledge = extended_asset(order.price.quantity.amount / 2, (order.price.get_extended_symbol()));
        if ( order.lock_pledge.quantity.amount == 0) {
            return;
        } 
        maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
        auto iter = maker_snapshot_tbl.find(order.order_id);
        check(iter != maker_snapshot_tbl.end(), "cannot find miner in dmc maker");
        auto user_rsi = extended_asset(round(double(order.miner_lock_pst.quantity.amount) * pow(10, rsi_sym.get_symbol().precision() - order.miner_lock_pst.get_extended_symbol().get_symbol().precision())), rsi_sym);
        auto miner_rsi_pledge = extended_asset(round(user_rsi.quantity.amount * (1 + iter->rate / 100.0)) / 2, rsi_sym);
        
        if (order.lock_pledge >= dmc_pledge) {
            order.lock_pledge -= dmc_pledge;
            order.settlement_pledge += dmc_pledge;
            order.latest_settlement_date += claims_interval;
        }
        if (order.miner_lock_rsi >= miner_rsi_pledge) {
            order.miner_lock_rsi -= miner_rsi_pledge;
            order.miner_rsi += miner_rsi_pledge;
        }
        if (order.lock_pledge < dmc_pledge) {
            order.settlement_pledge += order.lock_pledge;
            order.lock_pledge = extended_asset(0, order.lock_pledge.get_extended_symbol());
            order.miner_rsi += order.miner_lock_rsi;
            order.miner_lock_rsi = extended_asset(0, order.miner_lock_rsi.get_extended_symbol());
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

void token::generate_maker_snapshot(uint64_t order_id, uint64_t bill_id, name miner, name payer, uint64_t r, bool reset) {
    dmc_makers maker_tbl(get_self(), get_self().value);
    auto maker_iter = maker_tbl.find(miner.value);
    check(maker_iter != maker_tbl.end(), "can't find maker pool");

    dmc_maker_pool dmc_pool(get_self(), miner.value);
    std::vector<maker_lp_pool> lps;
    std::vector<maker_pool> changed;
    for (auto iter = dmc_pool.begin(); iter != dmc_pool.end(); iter++) {
        lps.emplace_back(maker_lp_pool{
            .owner = iter->owner,
            .ratio = iter->weight / maker_iter->total_weight,
        });
        // if maker pool is empty, set all maker pool weight to 0
        if (reset) {
            dmc_pool.modify(iter, payer, [&](auto& m) {
                m.weight = 0;
            });
            changed.push_back(*iter);
        }
    }

    maker_snapshot snapshot_info = {
        .order_id = order_id,
        .miner = maker_iter->miner,
        .bill_id = bill_id,
        .rate = r,
        .lps = lps
    };

    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    check(maker_snapshot_tbl.find(order_id) == maker_snapshot_tbl.end(), "order snapshot already exists");
    maker_snapshot_tbl.emplace(payer, [&](auto& mst) {
        mst = snapshot_info;
    });
    SEND_INLINE_ACTION(*this, makersnaprec, { _self, "active"_n }, { snapshot_info });
    if(reset) {
        SEND_INLINE_ACTION(*this, makerpoolrec, {_self, "active"_n}, {miner, changed});
    }
}

void token::delete_maker_snapshot(uint64_t order_id) {
    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    auto snapshot_iter = maker_snapshot_tbl.find(order_id);
    if (snapshot_iter != maker_snapshot_tbl.end()) {
        maker_snapshot_tbl.erase(snapshot_iter);
    }
}

extended_asset token::distribute_lp_pool(uint64_t order_id, std::vector<asset_type_args> rewards, extended_asset challenge_pledge, name payer) {
    maker_snapshot_table  maker_snapshot_tbl(get_self(), get_self().value);
    auto snapshot_iter = maker_snapshot_tbl.find(order_id);
    check(snapshot_iter != maker_snapshot_tbl.end(), "order snapshot not exists");
    check(rewards.size(), "invalid rewards size");
    
    dmc_makers maker_tbl(get_self(), get_self().value);
    auto miner = snapshot_iter->miner;
    auto maker_iter = maker_tbl.find(miner.value);
    auto remain_pay = extended_asset(0, rewards[0].quant.get_extended_symbol());
    std::vector<asset_type_args> miner_receipt;
    for (uint64_t i = 0; i < rewards.size(); i++) {
        if (rewards[i].type == AssetReceiptClaim || rewards[i].type == AssetReceiptDeposit || rewards[i].type == AssetReceiptReward) {
            double current_r = snapshot_iter->rate / 100.0;
            auto miner_dmc_pledge = extended_asset(round(rewards[i].quant.quantity.amount / (current_r + 1.0)), rewards[i].quant.get_extended_symbol());
            rewards[i].quant -= miner_dmc_pledge;

            auto challenge_pay = miner_dmc_pledge.quantity > challenge_pledge.quantity ? challenge_pledge : miner_dmc_pledge;
            miner_dmc_pledge -= challenge_pay;
            if (miner_dmc_pledge.quantity.amount) {
                add_balance(miner, miner_dmc_pledge, payer);
                miner_receipt.push_back({miner_dmc_pledge, rewards[i].type});
                SEND_INLINE_ACTION(*this, assetrec, {_self, "active"_n}, {order_id, {miner_dmc_pledge}, miner, rewards[i].type});
            }
            if (challenge_pay.quantity.amount > 0) {
                miner_receipt.push_back({-challenge_pay, OrderReceiptChallengeAns});
                increase_penalty(challenge_pay);
            }
            challenge_pledge -= challenge_pay;
        }
    }
    if (miner_receipt.size() > 0) {
        SEND_INLINE_ACTION(*this, orderassrec, {_self, "active"_n}, {order_id, miner_receipt, miner, ACC_TYPE_MINER, time_point_sec(current_time_point())});
    }
    extended_asset pledge = extended_asset(0, rewards[0].quant.get_extended_symbol());
    for (uint64_t i = 0; i < rewards.size(); i++) {
        pledge += rewards[i].quant;
    }

    auto sub_pledge = extended_asset(0, pledge.get_extended_symbol());
    std::vector<maker_pool> pool_info;
    std::vector<distribute_maker_snapshot> distribute_info;
    dmc_maker_pool dmc_pool(get_self(), miner.value);
    auto total_add = extended_asset(0, pledge.get_extended_symbol());

    auto iter_end = snapshot_iter->lps.end();
    for (auto iter = snapshot_iter->lps.begin();  iter != iter_end; iter++) {
        if (iter->owner == miner) {
            continue;
        }
        auto owner_pledge = extended_asset(std::floor(iter->ratio * pledge.quantity.amount), pledge.get_extended_symbol());
        auto pool_iter = dmc_pool.find(iter->owner.value);
        if (pool_iter != dmc_pool.end()) {
            double owner_weight = (double)owner_pledge.quantity.amount / maker_iter->total_staked.quantity.amount * maker_iter->total_weight;
            dmc_pool.modify(pool_iter, payer, [&](auto& p) {
                p.weight = p.weight + owner_weight;
            });
            pool_info.emplace_back(*pool_iter);
            distribute_info.push_back({iter->owner, owner_pledge, MakerDistributePool});
        } else {
            add_balance(iter->owner, owner_pledge, payer);
            sub_pledge += owner_pledge;
            distribute_info.push_back({iter->owner, owner_pledge, MakerDistributeAccount});
        }
        total_add += owner_pledge;
    }
    auto owner_pledge = pledge - total_add;
    auto pool_iter = dmc_pool.find(miner.value);
    if (pool_iter != dmc_pool.end()) {
        double owner_weight = (double)owner_pledge.quantity.amount / maker_iter->total_staked.quantity.amount * maker_iter->total_weight;
        dmc_pool.modify(pool_iter, payer, [&](auto& p) {
            p.weight = p.weight + owner_weight;
        });
        pool_info.emplace_back(*pool_iter);
        distribute_info.push_back({miner, owner_pledge, MakerDistributePool});
    } else {
        add_balance(miner, owner_pledge, payer);
        sub_pledge += owner_pledge;
        distribute_info.push_back({miner, owner_pledge, MakerDistributeAccount});
    }

    extended_asset new_total = maker_iter->total_staked + (pledge - sub_pledge);
    double weight_change = (double)(pledge - sub_pledge).quantity.amount / maker_iter->total_staked.quantity.amount * maker_iter->total_weight;
    double r = cal_current_rate(new_total, miner, maker_iter->get_real_m());
    maker_tbl.modify(maker_iter, get_self(), [&](auto& m) {
        m.total_weight = m.total_weight + weight_change;
        m.total_staked = new_total;
        m.current_rate = r;
    });

    SEND_INLINE_ACTION(*this, makerecord, { _self, "active"_n }, { *maker_iter });
    SEND_INLINE_ACTION(*this, makerpoolrec, { _self, "active"_n }, { miner, pool_info });
    SEND_INLINE_ACTION(*this, dismakerec, { _self, "active"_n }, { order_id, rewards, sub_pledge, distribute_info });
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
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
}

void token::claimdeposit(name payer, uint64_t order_id) {
    require_auth(payer);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");
    check(order_iter->deposit.quantity.amount > 0, "no deposit to claim");

    auto order_info = *order_iter;
    update_order(order_info, *challenge_iter, payer);

    check(payer == order_iter->user, "only order user can claim deposit");
    check(order_info.deposit_valid <= order_info.latest_settlement_date, "order not reach end, can not deposit");
    add_balance(order_info.user, order_info.deposit, payer);
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.deposit }, order_info.user, AssetReceiptDeposit});
    order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o = order_info;
    });
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { order_info, 2 });
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

    auto user_dmc = get_dmc_by_vrsi(order_info.user_rsi);
    add_balance(order_info.user, user_dmc, payer);
    auto challenge = *challenge_iter;
    auto miner_remain_pay = distribute_lp_pool(order_info.order_id, {{order_info.settlement_pledge, AssetReceiptClaim}, {get_dmc_by_vrsi(order_info.miner_rsi), AssetReceiptReward}}, challenge.miner_pay, payer);
    challenge.miner_pay = miner_remain_pay;

    order_info.user_rsi = extended_asset(0, order_info.user_rsi.get_extended_symbol());
    order_info.settlement_pledge = extended_asset(0, order_info.settlement_pledge.get_extended_symbol());
    order_info.miner_rsi = extended_asset(0, order_info.miner_rsi.get_extended_symbol());

    if (order_info.deposit_valid <= order_info.latest_settlement_date && order_info.deposit.quantity.amount > 0) {
        add_balance(order_info.user, order_info.deposit, payer);
        SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.deposit }, order_info.user, AssetReceiptDeposit});
        order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
    }

    
    bool deleted = false;
    if ((order_info.state == OrderStateEnd || order_info.state == OrderStateCancel) &&
        (!order_info.lock_pledge.quantity.amount) && (!order_info.miner_lock_rsi.quantity.amount) &&
        (!order_info.deposit.quantity.amount) && (!order_info.miner_lock_dmc.quantity.amount)) {
            if (order_info.user_pledge.quantity.amount) {
                add_balance(order_info.user, order_info.user_pledge, payer);
                SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.user_pledge }, order_info.user, AssetReceiptSubReserve});
                order_info.user_pledge = extended_asset(0, order_info.user_pledge.get_extended_symbol());
            }
            deleted = true;
    }

    if (deleted) {
        order_tbl.erase(order_iter);
        challenge_tbl.erase(challenge_iter);
        delete_maker_snapshot(order_id);
    } else {
        order_tbl.modify(order_iter, payer, [&](auto& o) {
            o = order_info;
        });
        challenge_tbl.modify(challenge_iter, payer, [&](auto& c) {
            c = challenge;
        });
    }

    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { order_info, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { challenge });
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { user_dmc }, order_info.user, AssetReceiptClaim});
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
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
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

    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
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
    check(is_challenge_end(challenge_info.state) || challenge_info.state == ChallengePrepare, "invalid challenge state");
    check(order_info.cancel_date == time_point_sec(), "can't duplicate cancel order");
    bool deleted = false;
    if (order_info.state == OrderStateWaiting) {
        check(challenge_info.state == ChallengePrepare, "invalid challenge state");
        order_info.state = OrderStateCancel;
        challenge_info.state = ChallengeCancel;
        distribute_lp_pool(order_info.order_id, {{order_info.miner_lock_dmc, AssetReceiptMinerLock}}, extended_asset(0, dmc_sym), get_self());
        delete_order_pst(order_info);
        add_balance(order_info.user, order_info.lock_pledge + order_info.user_pledge + order_info.deposit, sender);
        SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n },
        { order_id, { order_info.lock_pledge, order_info.user_pledge, order_info.deposit }, order_info.user, AssetReceiptCancel});
        order_info.miner_lock_dmc = extended_asset(0, order_info.miner_lock_dmc.get_extended_symbol());
        order_info.lock_pledge = extended_asset(0, order_info.lock_pledge.get_extended_symbol());
        order_info.user_pledge = extended_asset(0, order_info.user_pledge.get_extended_symbol());
        order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
        order_info.cancel_date = time_point_sec(current_time_point());
        deleted = true;
    } else if (order_info.state == OrderStateDeliver || order_info.state == OrderStatePreCont) {
       check(order_info.deposit_valid <= time_point_sec(current_time_point()), "invalid time, can't cancel");
       order_info.cancel_date = time_point_sec(current_time_point());
    } else {
        check(false, "invalid cancel order state");
    }
    
    if (deleted) {
        order_tbl.erase(order_iter);
        challenge_tbl.erase(challenge_iter);
        delete_maker_snapshot(order_id);
    } else {
        order_tbl.modify(order_iter, sender, [&](auto& o) {
            o = order_info;
        });
        challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
            c = challenge_info;
        });
    }
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { order_info, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { challenge_info });
}

}
