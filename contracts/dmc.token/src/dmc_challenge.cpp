#include <dmc.token/dmc.token.hpp>
#include <eosio/transaction.hpp>
#include <string.h>

namespace eosio {

void token::phishing_challenge() {
    dmc_global dmc_global_tbl(get_self(), get_self().value);
    auto phishing_date_iter = dmc_global_tbl.find("phishdate"_n.value);
    time_point_sec phishing_date;
    if (phishing_date_iter != dmc_global_tbl.end())
        phishing_date = time_point_sec(phishing_date_iter->value);
    else {
        phishing_date = time_point_sec(current_time_point());
        dmc_global_tbl.emplace(_self, [&](auto& conf) {
            conf.key = "phishdate"_n;
            conf.value = current_time_point().sec_since_epoch();
        });
    }
    
    auto phishing_interval_iter = dmc_global_tbl.find("phishinter"_n.value);
    uint64_t phishing_interval = default_phishing_interval;
    if (phishing_interval_iter != dmc_global_tbl.end()) {
        phishing_interval = phishing_interval_iter->value;
    }
    if (phishing_date + phishing_interval > time_point_sec(current_time_point())) {
        return;
    }

    dmc_orders order_tbl(get_self(), get_self().value);
    if (order_tbl.begin() == order_tbl.end()) {
        return;
    }
    uint64_t order_id_begin = order_tbl.begin()->order_id;
    uint64_t order_id_end = get_dmc_config("orderid"_n, default_id_start);
    uint64_t tpos_mult = uint64_t(tapos_block_prefix()) * tapos_block_num();
    uint64_t rand = tpos_mult % (order_id_end - order_id_begin);
    uint64_t order_id = order_id_begin + rand;

    auto state_id_idx = order_tbl.get_index<"stateid"_n>();
    auto state_id_iter = state_id_idx.lower_bound(dmc_order::get_state_id(OrderStateDeliver, order_id));
    if (state_id_iter != state_id_idx.end() && state_id_iter->state == OrderStateDeliver) {
        dmc_challenges challenge_tbl(get_self(), get_self().value);
        auto challenge_iter = challenge_tbl.find(state_id_iter->order_id);
        if (is_challenge_end(challenge_iter->state) && challenge_iter->data_block_count) {
            auto challenge_hash = sha256((char*)&tpos_mult, sizeof(uint64_t));
            uint64_t data_id = uint64_t(*reinterpret_cast<const uint64_t*>(&challenge_hash)) % challenge_iter->data_block_count;
            SEND_INLINE_ACTION(*this, reqchallenge, { _self, "active"_n }, { _self, state_id_iter->order_id, data_id, challenge_hash, std::string("phishing")});
            dmc_global_tbl.modify(phishing_date_iter, get_self(), [&](auto& d) {
                d.value = current_time_point().sec_since_epoch();
            });
        }
    }
}

ChallengeState token::get_challenge_state(uint64_t order_id)
{
    uint64_t challenge_interval = get_dmc_config("challinter"_n, default_dmc_challenge_interval);
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    if (challenge_iter->state == ChallengeRequest && challenge_iter->challenge_date + challenge_interval <= time_point_sec(current_time_point())) {
        return ChallengeTimeout;
    }
    return challenge_iter->state;
}

bool token::is_challenge_end(ChallengeState state)
{
    return (state == ChallengeConsistent || state == ChallengeAnswer
        || state == ChallengeArbitrationMinerPay || state == ChallengeArbitrationUserPay);
}

void token::addmerkle(name sender, uint64_t order_id, checksum256 merkle_root, uint64_t data_block_count)
{
    require_auth(sender);

    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    check(sender == order_iter->user || sender == order_iter->miner, "order doesn't belong to sender");

    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge!");

    check(challenge_iter->state == ChallengePrepare || is_challenge_end(challenge_iter->state), "invalid state");
    if (challenge_iter->merkle_submitter == sender || challenge_iter->merkle_submitter == _self) {
        challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
            c.pre_merkle_root = merkle_root;
            c.pre_data_block_count = data_block_count;
            c.merkle_submitter = sender;
        });
    } else {
        check(merkle_root == challenge_iter->pre_merkle_root, "merkle root mismatch");
        check(challenge_iter->pre_data_block_count == data_block_count, "block count mismatch");
        if (challenge_iter->state == ChallengePrepare) {
            challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
                c.state = ChallengeConsistent;
                c.merkle_submitter = name { _self };
                c.merkle_root = c.pre_merkle_root;
                c.data_block_count = c.pre_data_block_count;
                c.pre_data_block_count = 0;
                c.pre_merkle_root = checksum256();
            });

            dmc_order order = *order_iter;
            update_order(order, *challenge_iter, sender);
            order_tbl.modify(order_iter, sender, [&](auto& o) {
                o = order;
            });
            SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2});
        } else {
            challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
                c.merkle_submitter = name { _self };
                c.merkle_root = c.pre_merkle_root;
                c.data_block_count = c.pre_data_block_count;
                c.pre_data_block_count = 0;
                c.pre_merkle_root = checksum256();
            });
        }
    }
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { *challenge_iter });
}

void token::reqchallenge(name sender, uint64_t order_id, uint64_t data_id, checksum256 hash_data, std::string nonce)
{
    require_auth(sender);
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    check(sender == order_iter->user || sender == get_self(), "only user can reqchallenge");
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");
    auto state = get_challenge_state(order_id);
    check(is_challenge_end(state), "invalid challenge state, cannot reqchallenge");
    check(data_id < challenge_iter->data_block_count, "invalid data number");

    auto order = *order_iter;
    update_order(order, *challenge_iter, sender);
    check(order.state == OrderStateDeliver || order.state == OrderStatePreEnd || order.state == OrderStatePreCont, "order state is invalid, can't reqchallenge");

    auto per_price_amount = double(order_iter->price.quantity.amount) * 0.1 / (order_iter->miner_lock_pst.quantity.amount / pow(10, pst_sym.get_symbol().precision()));
    auto user_lock = extended_asset(per_price_amount * 100, order_iter->price.get_extended_symbol());
    if (sender == get_self()) {
        user_lock = extended_asset(0, user_lock.get_extended_symbol());
    }

    check(order.user_pledge >= user_lock, "not enough dmc to challenge");
    order.user_pledge -= user_lock;
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order;
    });

    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c.data_id = data_id;
        c.hash_data = hash_data;
        c.nonce = nonce;
        c.challenge_times = c.challenge_times + 1;
        c.state = ChallengeRequest;
        c.challenge_date = time_point_sec(current_time_point());
        c.user_lock += user_lock;
        c.challenger = sender;
    });
    if (user_lock.quantity.amount > 0) {
        SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order_id, { {-user_lock, OrderReceiptChallengeReq}}, order.user,  ACC_TYPE_USER, challenge_iter->challenge_date});
    }
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { *challenge_iter });
}

void token::anschallenge(name sender, uint64_t order_id, checksum256 reply_hash)
{
    require_auth(sender);

    check(get_challenge_state(order_id) == ChallengeRequest, "invalid state, cannot reply");
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    check(sender == order_iter->miner, "only miner can reply proof");

    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    auto reply_bytes = reply_hash.extract_as_byte_array();
    checksum256 checksum_data = sha256((char*)&reply_bytes[0], reply_bytes.size());

    check(checksum_data == challenge_iter->hash_data, "invalid reply hash data");

    auto per_price_amount = double(order_iter->price.quantity.amount) * 0.1 / (order_iter->miner_lock_pst.quantity.amount / pow(10, pst_sym.get_symbol().precision()));
    auto user_pay = extended_asset(per_price_amount, order_iter->price.get_extended_symbol());
    if (challenge_iter->challenger == get_self()){
        user_pay = extended_asset(0,  order_iter->price.get_extended_symbol());
    }

    auto order = *order_iter;
    order.user_pledge += challenge_iter->user_lock - user_pay;
    if ((challenge_iter->user_lock - user_pay).quantity.amount != 0) {
        SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order_id, {{challenge_iter->user_lock - user_pay, OrderReceiptChallengeAns}}, order.user,  ACC_TYPE_USER, time_point_sec(current_time_point())});
    }

    increase_penalty(user_pay);

    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c.state = ChallengeAnswer;
        c.user_lock = extended_asset(0, dmc_sym);
        c.miner_pay += user_pay;
    });

    update_order(order, *challenge_iter, sender);
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order;
    });
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { *challenge_iter });
}

void token::arbitration(name sender, uint64_t order_id, const std::vector<char>& data, std::vector<checksum256> cut_merkle)
{
    require_auth(sender);

    check(get_challenge_state(order_id) == ChallengeRequest, "invalid state, cannot arbitration");
    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");

    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    std::vector<char> copy_data = data;
    checksum256 checksum_data = sha256((char*)&copy_data[0], copy_data.size());
    copy_data.insert(copy_data.end(), challenge_iter->nonce.begin(), challenge_iter->nonce.end());
    checksum256 pre_hash_data = sha256((char*)&copy_data[0], copy_data.size());
    auto pre_hash_bytes = pre_hash_data.extract_as_byte_array();
    checksum256 hash_data = sha256((char*)&pre_hash_bytes[0], pre_hash_bytes.size());
    auto hash_bytes = checksum_data.extract_as_byte_array();
    uint64_t id_tmp = challenge_iter->data_id;

    for (auto iter = cut_merkle.begin(); iter != cut_merkle.end(); iter++) {
        std::vector<char> mixed_hash;
        auto iter_hash_bytes = (*iter).extract_as_byte_array();
        auto pos = id_tmp % 2;
        if (!pos) {
            mixed_hash.insert(mixed_hash.begin(), &hash_bytes[0], (&hash_bytes[0]) + hash_bytes.size());
            mixed_hash.insert(mixed_hash.begin() + hash_bytes.size(), &(iter_hash_bytes[0]), (&(iter_hash_bytes[0])) + iter_hash_bytes.size());
        } else {
            mixed_hash.insert(mixed_hash.begin(), &(iter_hash_bytes[0]), (&(iter_hash_bytes[0])) + iter_hash_bytes.size());
            mixed_hash.insert(mixed_hash.begin() + hash_bytes.size(), &(hash_bytes[0]), (&(hash_bytes[0])) + hash_bytes.size());
        }
        checksum_data = sha256((char*)&mixed_hash[0], mixed_hash.size());
        hash_bytes = checksum_data.extract_as_byte_array();
        id_tmp /= 2;
    }
    check(checksum_data == challenge_iter->merkle_root, "merkle root mismatch!");

    auto per_price_amount = double(order_iter->price.quantity.amount) * 0.1 / (order_iter->miner_lock_pst.quantity.amount / pow(10, pst_sym.get_symbol().precision()));
    auto miner_pay = extended_asset(per_price_amount, order_iter->price.get_extended_symbol());
    auto user_pay = extended_asset(per_price_amount * 100, order_iter->price.get_extended_symbol());
    if (challenge_iter->challenger == get_self()){
        user_pay = extended_asset(0, order_iter->price.get_extended_symbol());
    }

    ChallengeState state = ChallengeArbitrationUserPay;
    if (hash_data == challenge_iter->hash_data) {
        state = ChallengeArbitrationMinerPay;
        auto tmp = miner_pay;
        miner_pay = user_pay;
        user_pay = tmp;
    }

    auto order = *order_iter;
    order.user_pledge += challenge_iter->user_lock - user_pay;

    increase_penalty(user_pay);
    if ((challenge_iter->user_lock - user_pay).quantity.amount != 0) {
        SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order_id, { {challenge_iter->user_lock - user_pay, OrderReceiptChallengeArb} }, order.user,  ACC_TYPE_USER, time_point_sec(current_time_point())});
    }
    
    challenge_tbl.modify(challenge_iter, sender, [&](auto& o) {
        o.state = state;
        o.user_lock = extended_asset(0, dmc_sym);
        o.miner_pay += miner_pay;
    });

    update_order(order, *challenge_iter, sender);

    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order;
    });
    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { *order_iter, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { *challenge_iter });
}

void token::paychallenge(name sender, uint64_t order_id)
{
    require_auth(sender);

    dmc_orders order_tbl(get_self(), get_self().value);
    auto order_iter = order_tbl.find(order_id);
    check(order_iter != order_tbl.end(), "can't find order");
    uint64_t challenge_interval = get_dmc_config("challinter"_n, default_dmc_challenge_interval);
    dmc_challenges challenge_tbl(get_self(), get_self().value);
    auto challenge_iter = challenge_tbl.find(order_id);
    check(challenge_iter != challenge_tbl.end(), "can't find challenge");

    check(challenge_iter->state == ChallengeRequest, "invalid state, can't pay challenge!");
    check(challenge_iter->challenge_date + challenge_interval <= time_point_sec(current_time_point()), "challange doesn't reach expire time!");

    dmc_order order_info = *order_iter;
    auto miner_arbitration = order_info.miner_lock_dmc;

    auto system_reward = extended_asset(miner_arbitration.quantity.amount * 0.5, miner_arbitration.get_extended_symbol());
    increase_penalty(system_reward);
    
    add_balance(order_info.user,  (miner_arbitration - system_reward) + order_info.deposit, sender);
    if (order_info.deposit.quantity.amount > 0) {
         SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.deposit }, order_info.user, AssetReceiptDeposit});
         order_info.deposit = extended_asset(0, order_info.deposit.get_extended_symbol());
    }
    SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { miner_arbitration - system_reward }, order_info.user, AssetReceiptPayChallenge});
    delete_order_pst(order_info);
    order_info.miner_lock_dmc = extended_asset(0, dmc_sym);
    order_info.lock_pledge -= order_info.price;
    order_info.user_pledge += challenge_iter->user_lock + order_info.price;
    order_info.state = OrderStateEnd;
    SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order_id, { {order_info.price, OrderReceiptLockRet} }, order_info.user, ACC_TYPE_USER, time_point_sec(current_time_point())});
    if (challenge_iter->user_lock.quantity.amount != 0) {
        SEND_INLINE_ACTION(*this, orderassrec, { _self, "active"_n }, { order_id, { {challenge_iter->user_lock, OrderReceiptPayChallengeRet} }, order_info.user,  ACC_TYPE_USER, time_point_sec(current_time_point())});
    }

    bool deleted = false;
    if ((!order_info.lock_pledge.quantity.amount) && (!order_info.miner_lock_rsi.quantity.amount) && 
        (!order_info.miner_lock_dmc.quantity.amount) && (!order_info.settlement_pledge.quantity.amount)) {
            if (order_info.user_pledge.quantity.amount) {
                add_balance(order_info.user, order_info.user_pledge, sender);
                SEND_INLINE_ACTION(*this, assetrec, { _self, "active"_n }, { order_id, { order_info.user_pledge }, order_info.user, AssetReceiptSubReserve});
                order_info.user_pledge = extended_asset(0, order_info.user_pledge.get_extended_symbol());
            }
            deleted = true;
    }

    auto challenge = *challenge_iter;
    challenge.state = ChallengeTimeout;
    challenge.user_lock = extended_asset(0, challenge.user_lock.get_extended_symbol());
    if (deleted) {
        order_tbl.erase(order_iter);
        challenge_tbl.erase(challenge_iter);
        delete_maker_snapshot(order_id);
    } else {
        order_tbl.modify(order_iter, sender, [&](auto& o) {
            o = order_info;
        });
        challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
            c = challenge;
        });
    }

    SEND_INLINE_ACTION(*this, orderrec, { _self, "active"_n }, { order_info, 2 });
    SEND_INLINE_ACTION(*this, challengerec, { _self, "active"_n }, { challenge });
}
}