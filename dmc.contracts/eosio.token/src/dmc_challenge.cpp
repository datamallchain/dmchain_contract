#include <eosio.token/eosio.token.hpp>

namespace eosio {
void token::addmerkle(name sender, uint64_t order_id, checksum256 merkle_root)
{
    require_auth(sender);

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->user || sender == order_iter->miner, "order doesn't belong to sender");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    OrderState state = OrderStart;
    if (sender == order_iter->user) {
        eosio_assert(order_iter->state == OrderStart, "invalid order state");
        state = OrderSubmitMerkle;
    } else if (sender == order_iter->miner) {
        eosio_assert(order_iter->state == OrderSubmitMerkle, "invalid order state");
        eosio_assert(challenge_iter != challenge_tbl.end(), "user doesn't submit merkle root");
        if (is_equal_checksum256(merkle_root, challenge_iter->merkle_root)) {
            state = OrderConsistent;
        } else {
            state = OrderInconsistent;
        }
    }
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.state = state;
    });

    if (challenge_iter != challenge_tbl.end()) {
        challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
            c.merkle_root = merkle_root;
        });
    } else {
        challenge_tbl.emplace(sender, [&](auto& c) {
            c.order_id = order_id;
            c.challenge_times = 0;
            c.merkle_root = merkle_root;
        });
    }
}

void token::submitproof(name sender, uint64_t order_id, uint64_t data_id, checksum256 hash_data, std::string nonce)
{
    require_auth(sender);

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(order_iter->state == OrderConsistent, "invalid state, cannot submit");
    eosio_assert(sender == order_iter->user, "only user can submit proof");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.state = OrderSubmitProof;
    });

    challenge_tbl.modify(challenge_iter, sender, [&](auto& c) {
        c.data_id = data_id;
        c.hash_data = hash_data;
        c.nonce = nonce;
        c.challenge_times = c.challenge_times + 1;
    });
}

void token::replyproof(name sender, uint64_t order_id, checksum256 reply_hash)
{
    require_auth(sender);

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(order_iter->state == OrderSubmitProof, "invalid state, cannot reply");
    eosio_assert(sender == order_iter->miner, "only miner can reply proof");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");

    checksum256 checksum_data;
    ::sha256((char*)&reply_hash.hash[0], sizeof(reply_hash), &checksum_data);

    OrderState state = OrderSubmitProof;
    if (is_equal_checksum256(checksum_data, challenge_iter->hash_data)) {
        state = OrderVerifyProof;
    } else {
        state = OrderDenyProof;
    }

    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.state = state;
    });
}

void token::challenge(name sender, uint64_t order_id, const std::vector<char>& data, std::vector<std::vector<checksum256>> cut_merkle)
{
    require_auth(sender);
    eosio_assert(cut_merkle.size() >= 1 && cut_merkle[0].size() >= 1, "empty cut_merkle size");

    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(order_iter->state == OrderDenyProof, "invalid state, cannot challenge");

    dmc_challenges challenge_tbl(_self, _self);
    auto challenge_iter = challenge_tbl.find(order_id);
    eosio_assert(challenge_iter != challenge_tbl.end(), "can't find challenge");
    eosio_assert(cut_merkle[cut_merkle.size() - 1][0] == challenge_iter->merkle_root, "merkle root mismatch");

    checksum256 checksum_data;
    ::sha256((char*)&data[0], data.size(), &checksum_data);
    std::string data_hex = sha256_to_hex(checksum_data);
    bool checked = false;
    OrderState state = OrderEnd;

    for (auto iter = cut_merkle.begin(); iter != cut_merkle.end(); iter++) {
        auto current_merkle = *iter;
        checked = false;
        std::string mixed_hash;
        for (auto cur_iter = current_merkle.begin(); cur_iter != current_merkle.end(); cur_iter++) {
            std::string cur_hex = sha256_to_hex(*cur_iter);
            mixed_hash.append(cur_hex);
            if (data_hex == cur_hex) {
                checked = true;
            }
        }
        if (!checked) {
            state = OrderMinerPay;
            break;
        }
        ::sha256(mixed_hash.c_str(), mixed_hash.size(), &checksum_data);
        data_hex = sha256_to_hex(checksum_data);
    }

    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.state = state;
    });
}
}