#include "dmc.abo/dmc.abo.hpp"
#include "dmc.abo/function.hpp"

namespace eosio {

void abo::handle_extransfer(account_name from, account_name to, extended_asset quantity, std::string memo)
{
    if (from == _self) {
        return;
    }

    eosio_assert(to == _self, "Invalid require_recipient account_name");

    auto time_tup = find_keyword(memo, ";");
    auto expiration = time_point_sec(string_to_uint64(std::get<0>(time_tup)));
    eosio_assert(expiration >= time_point_sec(now()), "expiration must longer than now");

    auto symbol_tup = find_keyword(std::get<1>(time_tup), "@");
    auto symbol = symbol_type(string_to_symbol(4, std::get<0>(symbol_tup).c_str()));
    auto contract = string_to_name(std::get<1>(symbol_tup).c_str());

    stats statstable(N(eosio.token), contract);
    const auto& st = statstable.get(symbol.name(), "token with symbol does not exist");
    extended_symbol to_sym = extended_symbol(st.supply.symbol, st.issuer);
    eosio_assert(check_uniswap(quantity.get_extended_symbol(), to_sym), "no matched market");

    auto hash = sha256<repo_id_args>({ from, quantity, memo, now() });
    uint64_t repo_id = uint64_t(*reinterpret_cast<const uint64_t*>(&hash));
    repo_stats rst(_self, _self);

    eosio_assert(quantity.amount > 0, "invailed quantity");
    rst.emplace(_self, [&](auto& r) {
        r.repo_id = repo_id;
        r.owner = from;
        r.balance = quantity;
        r.to_sym = to_sym;
        r.expiration = expiration;
        r.update_at = time_point_sec(now());
    });
}

void abo::handle_receipt(account_name owner, extended_asset quantity)
{
    eosio_assert(owner == _self, "Invalid require_recipient account_name");
    action({ _self, N(active) }, N(eosio.token), N(exretire),
        std::make_tuple(_self, quantity, std::string("Repurchase retired.")))
        .send();
}

void abo::repurchase(account_name owner, uint64_t seed)
{
    require_auth(owner);
    repo_stats rst(_self, _self);
    auto iter = rst.get_index<N(bytime)>();

    auto now_time = time_point_sec(now());
    auto rtime = now_time - retire_dalay;
    extended_asset exchange_token;
    bool execute = false;

    for (auto it = iter.begin(); it != iter.end() && it->update_at <= rtime;) {
        auto new_seed = it->repo_id ^ seed;
        auto s_rand = new_seed % time_window;
        auto to_sym = it->to_sym;
        auto repo_id = it->repo_id;
        exchange_token = it->balance;

        if (now_time >= it->expiration) {
            it = iter.erase(it);
        } else if (rtime.sec_since_epoch() - it->update_at.sec_since_epoch() >= s_rand) {
            auto it_fake = it;
            auto floating = new_seed % 41 + 80;
            auto duration = it->expiration.sec_since_epoch() - now_time.sec_since_epoch();
            auto times = std::ceil(duration / 3600.0) + 1;
            exchange_token.amount = uint64_t(exchange_token.amount / times * (floating / 100.0));
            it++;

            iter.modify(it_fake, 0, [&](auto& s) {
                s.balance -= exchange_token;
                s.update_at = now_time;
            });
            eosio_assert(it_fake->balance.amount >= 0, "insufficient balance bandwidth"); // should never happen
        } else {
            it++;
            continue;
        }

        if (exchange_token.amount && check_uniswap(exchange_token.get_extended_symbol(), to_sym)) {
            execute = true;
            action({ _self, N(active) }, N(eosio.token), N(exchange),
                std::make_tuple(_self, exchange_token, extended_asset(0, to_sym), 0.0, _self, uint64_to_string(repo_id)))
                .send();
        }
    }
    eosio_assert(execute, "Not executed.");
}

bool abo::check_uniswap(extended_symbol from, extended_symbol to)
{
    swap_market market(N(eosio.token), N(eosio.token));
    auto m_index = market.get_index<N(bysymbol)>();
    auto m_iter = m_index.find(uniswap_market::key(from, to));
    if (m_iter == m_index.end())
        return false;
    return true;
}

extern "C" {
void apply(uint64_t receiver, uint64_t code, uint64_t action)
{
    auto _self = receiver;
    if (action == N(onerror)) {
        eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account");
    }
    if (code == N(eosio.token)) {
        eosio::abo t(_self);
        if (action == N(transfer)) {
            const currency::transfer& tr = unpack_action_data<currency::transfer>();
            extended_asset quantity = extended_asset(tr.quantity, system_account);
            t.handle_extransfer(tr.from, tr.to, quantity, tr.memo);
        } else if (action == N(extransfer)) {
            const extransfer& tr = unpack_action_data<extransfer>();
            t.handle_extransfer(tr.from, tr.to, tr.quantity, tr.memo);
        } else if (action == N(uniswapsnap)) {
            const uniswapsnap& tr = unpack_action_data<uniswapsnap>();
            t.handle_receipt(tr.owner, tr.quantity);
        }
    } else if (code == _self) {
        eosio::abo thiscontract(_self);
        switch (action) {
            EOSIO_API(eosio::abo,
                (repurchase))
        }
    }
}
}
}