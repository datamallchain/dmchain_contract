#include <eosio.token/eosio.token.hpp>

namespace eosio {
void token::claimorder(name payer, uint64_t order_id)
{
    require_auth(payer);
    // TODO: do order status check
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    uint64_t claims_interval = get_dmc_config(name { N(claiminter) }, default_dmc_claims_interval);
    double stake_rate = get_dmc_config(name { N(stakerate) }, default_dmc_stake_rate) / 100.0;
    eosio_assert(stake_rate > 0 && stake_rate < 1, "invalid stake rate");
    time_point_sec claim_end_date = time_point_sec(now()) > order_iter->end_date
        ? order_iter->end_date
        : time_point_sec(now());
    auto epoch = (claim_end_date - order_iter->claim_date).to_seconds() / claims_interval;
    eosio_assert(epoch >= 1, "no reward to claim");

    // user will get lock_dmc_amount * 1 RSI every 7 day
    // miner will get lock_dmc_amount * (1 + 1/m) RSI every 7 day
    auto user_reward = extended_asset(round(double(order_iter->user_pledge.amount) / pow(10, order_iter->user_pledge.get_extended_symbol().precision()) * rsi_sym.precision() * epoch), rsi_sym);
    auto miner_reward = extended_asset(round(user_reward.amount * (1 + 1 / stake_rate) * epoch), rsi_sym);
    SEND_INLINE_ACTION(*this, exissue, { eos_account, N(active) }, { order_iter->user, user_reward, std::string("user claim order reward") });
    SEND_INLINE_ACTION(*this, exissue, { eos_account, N(active) }, { order_iter->miner, miner_reward, std::string("miner claim order reward") });

    order_tbl.modify(order_iter, payer, [&](auto& o) {
        o.claim_date = o.claim_date + claims_interval * epoch;
    });
}

void token::reneworder(name sender, uint64_t order_id)
{
    require_auth(sender);
    // TODO: do order status check
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->user, "only user can renew order");
    eosio_assert(order_iter->state != OrderCancel, "order cancel, can't renew");
    uint64_t claims_interval = get_dmc_config(name { N(claiminter) }, default_dmc_claims_interval);
    eosio_assert(order_iter->end_date > time_point_sec(now())
            && order_iter->end_date < time_point_sec(now()) + claims_interval,
        "user can only renew one epoch");

    sub_balance(sender, order_iter->user_pledge);
    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.end_date = o.end_date + claims_interval;
    });
}

void token::cancelorder(name sender, uint64_t order_id)
{
    require_auth(sender);
    // TODO: do order status check
    dmc_orders order_tbl(_self, _self);
    auto order_iter = order_tbl.find(order_id);
    eosio_assert(order_iter != order_tbl.end(), "can't find order");
    eosio_assert(sender == order_iter->user || sender == order_iter->miner, "order must belong to user");

    order_tbl.modify(order_iter, sender, [&](auto& o) {
        o.state = OrderCancel;
    });
}
}

// void token::pay_arbitration(uint64_t order_id, name payer)
// {
// }
