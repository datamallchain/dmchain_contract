#include <eosio.token/eosio.token.hpp>
#include <string.h>

namespace eosio {

ChallengeState token::get_challenge_state(uint64_t order_id)
{
    uint64_t challenge_interval = get_dmc_config(name { N(challinter) }, default_dmc_challenge_interval);
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    if (challenge_iter->state == ChallengeRequest && challenge_iter->challenge_date + challenge_interval <= time_point_sec(now())) {
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

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->user || sender == order_iter->miner, "order doesn't belong to sender");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge!");

    eosio_assert(challenge_iter->state == ChallengePrepare || is_challenge_end(challenge_iter->state), "invalid state");
    if (challenge_iter->merkle_submitter == sender || challenge_iter->merkle_submitter == _self) {
        challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
            c.pre_merkle_root = merkle_root;
            c.pre_data_block_count = data_block_count;
            c.merkle_submitter = sender;
        });
    } else {
        eosio_assert(is_equal_checksum256(merkle_root, challenge_iter->pre_merkle_root), "merkle root mismatch");
        eosio_assert(challenge_iter->pre_data_block_count == data_block_count, "block count mismatch");
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
}

void token::reqchallenge(name sender, uint64_t order_id, uint64_t data_id, checksum256 hash_data, std::string nonce)
{
    require_auth(sender);
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->user, "only user can reqchallenge");
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");
    auto state = get_challenge_state(order_id);
    eosio_assert(is_challenge_end(state), "invalid challenge state, cannot reqchallenge");
    eosio_assert(data_id < challenge_iter->data_block_count, "invalid data number");

    auto order = *order_iter;
    update_order(order, *challenge_iter, sender);
    eosio_assert(order.state == OrderStateDeliver || order.state == OrderStatePreEnd || order.state == OrderStatePreCont, "order state is invalid, can't reqchallenge");

    // 计算订单 pst 单价的 10%
    auto per_price_amount = double(order_iter->price.amount) * 0.1 / (order_iter->miner_pledge.amount / pow(10, pst_sym.precision()));
    auto user_lock = extended_asset(per_price_amount * 100, order_iter->price.get_extended_symbol());
    //预扣除挑战需要的 dmc
    eosio_assert(order.user_pledge >= user_lock, "not enough dmc to challenge");
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
        c.challenge_date = time_point_sec(now());
        c.user_lock += user_lock;
    });
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, -user_lock, extended_asset(0, dmc_sym), extended_asset(0, dmc_sym), user_lock, time_point_sec(now()), OrderReceiptChallengeReq });
}

void token::anschallenge(name sender, uint64_t order_id, checksum256 reply_hash)
{
    require_auth(sender);

    eosio_assert(get_challenge_state(order_id) == ChallengeRequest, "invalid state, cannot reply");
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->miner, "only miner can reply proof");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    checksum256 checksum_data;
    ::sha256((char*)&reply_hash.hash[0], sizeof(reply_hash.hash), &checksum_data);

    eosio_assert(is_equal_checksum256(checksum_data, challenge_iter->hash_data), "invalid reply hash data");

    // 计算订单 pst 单价的 10%
    auto per_price_amount = double(order_iter->price.amount) * 0.1 / (order_iter->miner_pledge.amount / pow(10, pst_sym.precision()));
    auto user_pay = extended_asset(per_price_amount, order_iter->price.get_extended_symbol());
    // 归还多锁定的dmc
    auto order = *order_iter;
    order.user_pledge += challenge_iter->user_lock - user_pay;
    // 手续费交给系统账户
    add_balance(abo_account, user_pay, sender);
    SEND_INLINE_ACTION(*this, assetcharec, { _self, N(active) }, { abo_account, user_pay, 0, order_id });
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, challenge_iter->user_lock - user_pay, extended_asset(0, dmc_sym),
            extended_asset(0, dmc_sym), user_pay - challenge_iter->user_lock, time_point_sec(now()), OrderReceiptChallengeAns });

    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c.state = ChallengeAnswer;
        c.user_lock = extended_asset(0, dmc_sym);
        c.miner_pay += user_pay;
    });

    update_order(order, *challenge_iter, sender);
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o = order;
    });
}

void token::arbitration(name sender, uint64_t order_id, const std::vector<char>& data, std::vector<checksum256> cut_merkle)
{
    require_auth(sender);

    eosio_assert(get_challenge_state(order_id) == ChallengeRequest, "invalid state, cannot arbitration");
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    std::vector<char> copy_data = data;
    checksum256 checksum_data;
    ::sha256((char*)&copy_data[0], copy_data.size(), &checksum_data);
    copy_data.insert(copy_data.end(), challenge_iter->nonce.begin(), challenge_iter->nonce.end());
    checksum256 pre_hash_data;
    ::sha256((char*)&copy_data[0], copy_data.size(), &pre_hash_data);
    checksum256 hash_data;
    ::sha256((char*)&pre_hash_data.hash[0], sizeof(pre_hash_data), &hash_data);
    uint64_t id_tmp = challenge_iter->data_id;

    for (auto iter = cut_merkle.begin(); iter != cut_merkle.end(); iter++) {
        std::vector<char> mixed_hash;
        auto pos = id_tmp % 2;
        if (!pos) {
            mixed_hash.insert(mixed_hash.begin(), &checksum_data.hash[0], (&checksum_data.hash[0]) + sizeof(checksum_data.hash));
            mixed_hash.insert(mixed_hash.begin() + sizeof(checksum_data.hash), &(iter->hash[0]), (&(iter->hash[0])) + sizeof(iter->hash));
        } else {
            mixed_hash.insert(mixed_hash.begin(), &(iter->hash[0]), (&(iter->hash[0])) + sizeof(iter->hash));
            mixed_hash.insert(mixed_hash.begin() + sizeof(checksum_data.hash), &checksum_data.hash[0], (&checksum_data.hash[0]) + sizeof(checksum_data.hash));
        }
        ::sha256((char*)&mixed_hash[0], mixed_hash.size(), &checksum_data);
        id_tmp /= 2;
    }
    eosio_assert(is_equal_checksum256(checksum_data, challenge_iter->merkle_root), "merkle root mismatch!");

    // 计算订单 pst 单价的 10%
    auto per_price_amount = double(order_iter->price.amount) * 0.1 / (order_iter->miner_pledge.amount / pow(10, pst_sym.precision()));
    auto miner_pay = extended_asset(per_price_amount, order_iter->price.get_extended_symbol());
    auto user_pay = extended_asset(per_price_amount * 100, order_iter->price.get_extended_symbol());

    ChallengeState state = ChallengeArbitrationUserPay;
    if (is_equal_checksum256(hash_data, challenge_iter->hash_data)) {
        state = ChallengeArbitrationMinerPay;
        auto tmp = miner_pay;
        miner_pay = user_pay;
        user_pay = tmp;
    }

    // 归还多锁定的dmc
    auto order = *order_iter;
    order.user_pledge += challenge_iter->user_lock - user_pay;
    // 手续费交给系统账户
    add_balance(abo_account, user_pay, sender);
    SEND_INLINE_ACTION(*this, assetcharec, { _self, N(active) }, { abo_account, user_pay, 0, order_id });
    if ((challenge_iter->user_lock - user_pay).amount > 0) {
        SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
            { order_id, challenge_iter->user_lock - user_pay, extended_asset(0, dmc_sym),
                extended_asset(0, dmc_sym), user_pay - challenge_iter->user_lock, time_point_sec(now()), OrderReceiptChallengeArb });
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
}

void token::paychallenge(name sender, uint64_t order_id)
{
    require_auth(sender);

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    uint64_t challenge_interval = get_dmc_config(name { N(challinter) }, default_dmc_challenge_interval);
    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    eosio_assert(challenge_iter->state == ChallengeRequest, "invalid state, can't pay challenge!");
    eosio_assert(challenge_iter->challenge_date + challenge_interval <= time_point_sec(now()), "challange doesn't reach expire time!");
    destory_pst(*order_iter);

    dmc_makers maker_tbl(_self, _self);
    auto iter = maker_tbl.find(order_iter->miner);
    eosio_assert(iter != maker_tbl.end(), "cannot find miner in dmc maker");
    double m = get_dmc_config(name { N(bmrate) }, default_benchmark_stake_rate) / 100.0;
    auto price_amount = get_real_asset(order_iter->price);
    auto arbitration_cost = get_asset_by_amount<double, std::ceil>(price_amount * m, order_iter->price.get_extended_symbol());

    auto remain_staked = extended_asset(1, arbitration_cost.get_extended_symbol());
    auto miner_arbitration = arbitration_cost - remain_staked;
    if (iter->total_staked > arbitration_cost) {
        remain_staked = iter->total_staked - arbitration_cost;
        miner_arbitration = arbitration_cost;
    }
    maker_tbl.modify(iter, sender, [&](auto& o) {
        o.total_staked = remain_staked;
    });

    SEND_INLINE_ACTION(*this, makercharec, { _self, N(active) }, { _self, order_iter->miner, -miner_arbitration, MakerReceiptChallengePay });

    auto system_reward = extended_asset(miner_arbitration.amount * 0.5, miner_arbitration.get_extended_symbol());
    add_balance(abo_account, system_reward, sender);
    SEND_INLINE_ACTION(*this, assetcharec, { _self, N(active) }, { abo_account, system_reward, 0, order_id });
    extended_asset zero_dmc = extended_asset(0, dmc_sym);
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, challenge_iter->user_lock, zero_dmc, zero_dmc, -challenge_iter->user_lock,
            time_point_sec(now()), OrderReceiptChallengePayRet });
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, order_iter->lock_pledge, -order_iter->lock_pledge,
            zero_dmc, zero_dmc, time_point_sec(now()), OrderReceiptLockRet });
    SEND_INLINE_ACTION(*this, ordercharec, { _self, N(active) },
        { order_id, miner_arbitration - system_reward,
            zero_dmc, zero_dmc, zero_dmc, time_point_sec(now()), OrderReceiptChallengePayReward });
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.user_pledge += challenge_iter->user_lock + o.lock_pledge + (miner_arbitration - system_reward);
        o.lock_pledge = extended_asset(0, o.lock_pledge.get_extended_symbol());
        o.state = OrderStateEnd;
    });

    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c.state = ChallengeTimeout;
        c.user_lock = extended_asset(0, c.user_lock.get_extended_symbol());
    });
}
}