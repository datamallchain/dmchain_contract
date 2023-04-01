/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */

#include <dmc.token/dmc.token.hpp>

namespace eosio {

void token::addreserves(name owner, extended_asset x, extended_asset y)
{
    require_auth(owner);

    check(x.quantity.is_valid() && y.quantity.is_valid(), "invalid currency");
    check(x.quantity.amount > 0 && y.quantity.amount > 0, "must addreserves positive amount");
    check(x.get_extended_symbol() != y.get_extended_symbol(), "can't create market with same token");

    if (x.get_extended_symbol() < y.get_extended_symbol()) {
        auto swap = x;
        x = y;
        y = swap;
    }
    auto sym_x = x.get_extended_symbol();
    auto sym_y = y.get_extended_symbol();

    if (sym_x == rsi_sym && sym_y == dmc_sym)
        check(owner == system_account, "this market only support datamall to addreserves");

    if (owner == system_account) {
        add_stats(x);
        add_stats(y);
    } else {
        sub_balance(owner, x);
        sub_balance(owner, y);
    }
    swap_market market(get_self(), get_self().value);
    auto m_index = market.get_index<"bysymbol"_n>();
    auto m_iter = m_index.find(uniswap_market::key(sym_x, sym_y));

    double new_weights = static_weights;
    uint64_t primary;
    if (m_iter == m_index.end()) {
        primary = market.available_primary_key();
        market.emplace(owner, [&](auto& r) {
            r.primary = primary;
            r.tokenx = x;
            r.tokeny = y;
            r.total_weights = new_weights;
        });
    } else {
        primary = m_iter->primary;

        extended_asset new_x = m_iter->tokenx + x;
        extended_asset new_y = m_iter->tokeny + y;
        double real_old_x = get_real_asset(m_iter->tokenx);
        double real_old_y = get_real_asset(m_iter->tokeny);
        double real_new_x = get_real_asset(new_x);
        double real_new_y = get_real_asset(new_y);
        uint64_t price = real_old_x / real_old_y * std::pow(2, 32);
        uint64_t new_price = real_new_x / real_new_y * std::pow(2, 32);
        double div = (double)new_price / (double)price;
        check(div <= 1.01 && div >= 0.99, "Excessive price volatility");

        SEND_INLINE_ACTION(*this, pricerec, { _self, "active"_n }, { price, new_price });
        double total = std::sqrt(real_old_x) * std::sqrt(real_old_y);
        double new_total = std::sqrt(real_new_x) * std::sqrt(real_new_y);

        new_weights = (new_total - total) / total * m_iter->total_weights;
        check(new_weights > 0, "Invalid new weights");
        check(new_weights / new_weights > 0, "Invalid new weights");
        auto total_weights = m_iter->total_weights + new_weights;
        check(new_weights / total_weights > 0.0001, "Add reserves too lower");
        m_index.modify(m_iter, get_self(), [&](auto& s) {
            s.tokenx = new_x;
            s.tokeny = new_y;
            s.total_weights = total_weights;
        });
    }

    swap_pool pool(_self, primary);
    auto pool_iter = pool.find(owner.value);
    if (pool_iter != pool.end()) {
        pool.modify(pool_iter, get_self(), [&](auto& s) {
            s.weights += new_weights;
        });
    } else {
        pool.emplace(owner, [&](auto& s) {
            s.owner = owner;
            s.weights = new_weights;
        });
    }
}

void token::outreserves(name owner, extended_symbol x, extended_symbol y, double rate)
{
    require_auth(owner);

    if (x < y) {
        auto swap = x;
        x = y;
        y = swap;
    }

    check(x.get_symbol().is_valid() && y.get_symbol().is_valid(), "invalid symbol name");
    check(rate > 0 && rate <= 1, "invalid rate");

    swap_market market(get_self(), get_self().value);
    auto m_index = market.get_index<"bysymbol"_n>();
    auto m_iter = m_index.find(uniswap_market::key(x, y));
    check(m_iter != m_index.end(), "no matched market record");

    auto primary = m_iter->primary;

    swap_pool pool(_self, m_iter->primary);
    auto pool_iter = pool.find(owner.value);
    check(pool_iter != pool.end(), "no matched pool record");

    auto owner_weights = pool_iter->weights * rate;
    auto total_weights = m_iter->total_weights;

    double out_rate = owner_weights / total_weights;

    extended_asset x_quantity = extended_asset(std::floor(m_iter->tokenx.quantity.amount * out_rate), x);
    extended_asset y_quantity = extended_asset(std::floor(m_iter->tokeny.quantity.amount * out_rate), y);

    check(x_quantity.quantity.amount > 0 && y_quantity.quantity.amount > 0, "dust attack detected");
    bool last_one = false;
    if (rate == 1) {
        owner_weights = pool_iter->weights;
        pool.erase(pool_iter);
        auto pool_begin = pool.begin();
        if (pool_begin == pool.end()) {
            x_quantity = m_iter->tokenx;
            y_quantity = m_iter->tokeny;
        } else if (++pool_begin == pool.end()) {
            last_one = true;
            pool_begin--;
            owner_weights = pool_begin->weights;
        }
    } else {
        pool.modify(pool_iter, get_self(), [&](auto& s) {
            s.weights -= owner_weights;
        });
        check(pool_iter->weights > 0, "negative pool weights amount");
    }
    m_index.modify(m_iter, get_self(), [&](auto& s) {
        s.tokenx -= x_quantity;
        s.tokeny -= y_quantity;
        s.total_weights -= owner_weights;
        if (last_one)
            s.total_weights = owner_weights;
    });
    check(m_iter->tokenx.quantity.amount >= 0, "negative tokenx amount");
    check(m_iter->tokeny.quantity.amount >= 0, "negative tokeny amount");
    check(m_iter->total_weights >= 0, "negative total weights amount");
    if (m_iter->total_weights == 0)
        m_index.erase(m_iter);

    if (rate != 1)
        check(pool_iter->weights / m_iter->total_weights > 0.0001, "The remaining weight is too low");

    SEND_INLINE_ACTION(*this, outreceipt, { _self, "active"_n }, { owner, x_quantity, y_quantity });
    if (owner == system_account) {
        sub_stats(x_quantity);
        sub_stats(y_quantity);
    } else {
        add_balance(owner, x_quantity, owner);
        add_balance(owner, y_quantity, owner);
    }
}

void token::uniswaporder(name owner, extended_asset quantity, extended_asset to, double price, name id, name rampay)
{
    auto from_sym = quantity.get_extended_symbol();
    auto to_sym = to.get_extended_symbol();
    swap_market market(get_self(), get_self().value);
    auto m_index = market.get_index<"bysymbol"_n>();
    auto m_iter = m_index.find(uniswap_market::key(from_sym, to_sym));
    check(m_iter != m_index.end(), "this uniswap pair dose not exist");

    auto marketx = m_iter->tokenx;
    auto markety = m_iter->tokeny;
    auto marketx_sym = marketx.get_extended_symbol();
    auto markety_sym = markety.get_extended_symbol();
    uint64_t primary = m_iter->primary;

    if (from_sym == marketx_sym && to_sym == markety_sym) {
        uniswapdeal(owner, marketx, markety, quantity, to, primary, price, rampay);
    } else if (from_sym == markety_sym && to_sym == marketx_sym) {
        uniswapdeal(owner, markety, marketx, quantity, to, primary, price, rampay);
    } else {
        check(false, "symbol precision mismatch");
    }

    m_index.modify(m_iter, get_self(), [&](auto& s) {
        s.tokenx = marketx;
        s.tokeny = markety;
    });
}

void token::uniswapdeal(name owner, extended_asset& market_from, extended_asset& market_to, extended_asset from, extended_asset to, uint64_t primary, double price, name rampay)
{
    auto from_sym = from.get_extended_symbol();
    auto to_sym = to.get_extended_symbol();
    double real_market_from = get_real_asset(market_from);
    double real_market_to = get_real_asset(market_to);
    double real_market_total = real_market_from * real_market_to;
    uint64_t min_price = real_market_from / real_market_to * std::pow(2, 32);
    uint64_t price_t = price * std::pow(2, 32);
    bool buy = (from.quantity.amount == 0) ? true : false;
    bool limit = (price_t == 0) ? false : true;
    bool first = true;

    extended_asset sub_asset = extended_asset(0, from_sym);
    extended_asset add_asset = extended_asset(0, to_sym);
    extended_asset from_scrap = extended_asset(0, from_sym);
    extended_asset to_scrap = extended_asset(0, to_sym);
    uint64_t new_price_t = uint64_max;
    double new_price;
    if (buy) {
        extended_asset new_to = market_to - to;
        if (new_to.quantity.amount > 0) {
            double real_new_to = get_real_asset(new_to);
            new_price = real_market_total / (real_new_to * real_new_to);
            new_price_t = new_price * std::pow(2, 32);
        }
    } else {
        extended_asset new_from = market_from + from;
        double real_new_from = get_real_asset(new_from);
        new_price = (real_new_from * real_new_from) / real_market_total;
        new_price_t = new_price * std::pow(2, 32);
    }

    auto old_price = min_price;
    price_t = limit ? price_t : new_price_t;
    min_price = price_t;
    min_price = std::min(min_price, new_price_t);

    double real_new_from;
    extended_asset spread_from;
    double cal_price = std::sqrt(min_price / std::pow(2, 32));
    if (min_price == new_price_t && min_price != uint64_max)
        cal_price = std::sqrt(new_price);
    real_new_from = cal_price * std::sqrt(real_market_total);
    extended_asset new_market_from = get_asset_by_amount<double, std::round>(real_new_from, from_sym);
    spread_from = new_market_from - market_from;
    market_from = new_market_from;

    auto real_new_to = real_market_total / real_new_from;
    auto new_market_to = get_asset_by_amount<double, std::round>(real_new_to, to_sym);
    auto spread_ex_to = market_to - new_market_to;
    market_to = new_market_to;
    auto to_fee = extended_asset(std::ceil(spread_ex_to.quantity.amount * uniswap_fee), spread_ex_to.get_extended_symbol());
    extended_asset spread_to = spread_ex_to - to_fee;
    to_scrap += to_fee;
    check(spread_from.quantity.amount > 0 && spread_to.quantity.amount > 0, "dust attack detected in uniswap");

    from -= spread_from;
    to -= spread_ex_to;
    check(from.quantity.amount >= 0 || to.quantity.amount >= 0, "can't sub/add negative asset"); // never happened

    sub_asset += spread_from;
    add_asset += spread_to;

    SEND_INLINE_ACTION(*this, pricerec, { _self, "active"_n }, { old_price, min_price });
    SEND_INLINE_ACTION(*this, traderecord, { _self, "active"_n },
        { owner, dmc_account, spread_from, spread_to, to_fee, 0 });

    add_balance(owner, add_asset, rampay);
    sub_balance(owner, sub_asset);

    market_from += from_scrap;
    market_to += to_scrap;

    SEND_INLINE_ACTION(*this, uniswapsnap, { _self, "active"_n },
        { owner, add_asset });
}

double token::get_real_asset(extended_asset quantity)
{
    return (double)quantity.quantity.amount / std::pow(10, quantity.get_extended_symbol().get_symbol().precision());
}

void token::setreserve(name owner, extended_asset dmc_quantity, extended_asset rsi_quantity)
{
    require_auth(system_account);
    check(dmc_quantity.get_extended_symbol() == dmc_sym && rsi_quantity.get_extended_symbol() == rsi_sym, "only DMC && RSI can be set as reserve");
    check(dmc_quantity.quantity.amount > 0 && rsi_quantity.quantity.amount > 0, "reserve can't be zero");

    inner_market market(get_self(), get_self().value);
    auto m_index = market.get_index<"bysymbol"_n>();
    auto m_iter = m_index.find(inner_uniswap_market::key(dmc_quantity.get_extended_symbol(), rsi_quantity.get_extended_symbol()));
    if (m_iter == m_index.end()) {
        market.emplace(owner, [&](auto& r) {
            r.primary = market.available_primary_key();
            r.tokenx = rsi_quantity;
            r.tokeny = dmc_quantity;
        });
    } else {
        m_index.modify(m_iter, owner, [&](auto& r) {
            r.tokenx = rsi_quantity;
            r.tokeny = dmc_quantity;
        });
    }
}

extended_asset token::allocation_abo(time_point_sec now_time) {
    abostats ast(get_self(), get_self().value);
    extended_asset to_user(0, dmc_sym);

    for (auto it = ast.begin(); it != ast.end();) {
        if (now_time > it->end_at) {
            if (it->remaining_release.quantity.amount != 0) {
                extended_asset curr(it->remaining_release.quantity.amount * it->user_rate, dmc_sym);
                to_user += curr;
                ast.modify(it, get_self(), [&](auto& a) {
                    a.last_user_released_at = it->end_at;
                    a.remaining_release -= curr;
                });
            }
            it++;
        } else if (now_time < it->start_at) {
            break;
        } else {
            auto duration_time = now_time.sec_since_epoch() - it->last_user_released_at.sec_since_epoch();
            auto remaining_time = it->end_at.sec_since_epoch() - it->last_user_released_at.sec_since_epoch();
            double per = (double)duration_time / (double)remaining_time;
            extended_asset curr(per * it->remaining_release.quantity.amount * it->user_rate, dmc_sym);
            to_user += curr;
            ast.modify(it, get_self(), [&](auto& a) {
                a.last_user_released_at = now_time;
                a.remaining_release -= curr;
            });
            break;
        }
    }

    if (to_user.quantity.amount > 0) {
        add_stats(to_user);
        SEND_INLINE_ACTION(*this, allocrec, {_self, "active"_n}, {to_user, AllocationAbo});
    }
    return to_user;
}

extended_asset token::allocation_penalty(time_point_sec now_time)
{
    penaltystats penst(get_self(), get_self().value);

    extended_asset to_penalty(0, dmc_sym);

    for (auto it = penst.begin(); it != penst.end();){
        if (now_time >= it->end_at) {
            to_penalty += it->remaining_release;
            it = penst.erase(it);
        } else {
            auto duration_time = now_time.sec_since_epoch() - it->start_at.sec_since_epoch();
            auto remaining_time = it->end_at.sec_since_epoch() - it->start_at.sec_since_epoch();
            double per = (double)duration_time / (double)remaining_time;
            uint64_t total_asset_amount = per * it->remaining_release.quantity.amount;
            to_penalty.quantity.amount += total_asset_amount;

            penst.modify(it, get_self(), [&](auto& p) {
                p.start_at = now_time;
                p.remaining_release.quantity.amount -= total_asset_amount;
            });
            break;
        }
    }
    SEND_INLINE_ACTION(*this, allocrec, { _self, "active"_n }, { to_penalty, AllocationPenalty });
    return to_penalty;
}

void token::increase_penalty(extended_asset quantity) 
{
    check(quantity.get_extended_symbol() == dmc_sym, "only DMC can be penalty");
    time_point_sec now_time = time_point_sec(current_time_point());
    uint64_t now_time_t = now_time.sec_since_epoch();
    uint64_t nearest_hour_time = now_time.sec_since_epoch() + 3600 - now_time.sec_since_epoch() % 3600;
    uint64_t sec_to_nearest_hour = nearest_hour_time - now_time_t;
    uint64_t copies = 11;

    auto final_end_time = time_point_sec(nearest_hour_time) + eosio::hours(copies);
    uint64_t sec_to_final_end_time = final_end_time.sec_since_epoch() - now_time_t;
    double per = (double)sec_to_nearest_hour / (double)sec_to_final_end_time;

    extended_asset first_period = extended_asset(quantity.quantity.amount * per, dmc_sym);
    extended_asset other_period_total = quantity - first_period;
    extended_asset other_period = extended_asset(other_period_total.quantity.amount / copies, dmc_sym);
    // for rounding error
    extended_asset real_other_period_total = extended_asset(other_period.quantity.amount * copies, dmc_sym);
    extended_asset real_first_period = first_period + (other_period_total - real_other_period_total);

    extended_asset dmc_quantity = allocation_penalty(now_time);
    exchange_from_uniswap(dmc_quantity);

    penaltystats penst(get_self(), get_self().value);

    // the first period
    {
        auto it = penst.begin();
        if (it != penst.end()) {
            penst.modify(it, get_self(), [&](auto& p) {
                p.remaining_release += real_first_period;
            });
        } else {
            penst.emplace(get_self(), [&](auto& p) {
                p.start_at = now_time;
                p.end_at = time_point_sec(nearest_hour_time);
                p.remaining_release = real_first_period;
            });
        }
    }

    // the other periods
    for (int i = 0; i < copies; i++) {
        auto end_time = time_point_sec(nearest_hour_time) + eosio::hours(i + 1);
        auto start_time = time_point_sec(nearest_hour_time) + eosio::hours(i);
        auto it = penst.find(end_time.sec_since_epoch());
        if (it != penst.end()) {
            penst.modify(it, get_self(), [&](auto& p) {
                p.remaining_release += other_period;
            });
        } else {
            penst.emplace(get_self(), [&](auto& p) {
                p.start_at = start_time;
                p.end_at = end_time;
                p.remaining_release = other_period;
            });
        }
    }
}

extended_asset token::exchange_from_uniswap(extended_asset add_balance) 
{
    check(add_balance.quantity.is_valid(), "invalid add_balance");
    check(add_balance.get_extended_symbol() == rsi_sym || add_balance.get_extended_symbol() == dmc_sym, "only RSI and DMC can be added to uniswap market");
    check(add_balance.quantity.amount >= 0, "add_balance must be positive");

    if(add_balance.quantity.amount == 0) return extended_asset(0, dmc_sym);

    inner_market market(get_self(), get_self().value);
    auto m_index = market.get_index<"bysymbol"_n>();
    auto m_iter = m_index.find(inner_uniswap_market::key(rsi_sym, dmc_sym));
    check(m_iter != m_index.end(), "this uniswap pair dose not exist");

    // tokenx = RSI
    // tokeny = DMC
    auto rsi_quantity = m_iter->tokenx;
    auto dmc_quantity = m_iter->tokeny;

    // S = rsi x dmc
    double real_rsi_quantity = get_real_asset(rsi_quantity);
    double real_dmc_quantity = get_real_asset(dmc_quantity);
    double real_market_total = real_rsi_quantity * real_dmc_quantity;

    extended_asset to_user(0, dmc_sym);
    if (add_balance.get_extended_symbol() == rsi_sym) {
        rsi_quantity += add_balance;
        real_rsi_quantity = get_real_asset(rsi_quantity);
        double real_dmc_quantity = real_market_total / real_rsi_quantity;
        extended_asset new_dmc_quantity = get_asset_by_amount<double, std::round>(real_dmc_quantity, dmc_sym);
        
        // DMC need return to user
        to_user = dmc_quantity - new_dmc_quantity;
        dmc_quantity = new_dmc_quantity;
    } else if (add_balance.get_extended_symbol() == dmc_sym) {
        dmc_quantity += add_balance;
        real_dmc_quantity = get_real_asset(dmc_quantity);
        double real_rsi_quantity = real_market_total / real_dmc_quantity;
        extended_asset new_rsi_quantity = get_asset_by_amount<double, std::round>(real_rsi_quantity, rsi_sym);

        rsi_quantity = new_rsi_quantity;
    } else {
        check(false, "only RSI and DMC can be added to uniswap market");
    }

    m_index.modify(m_iter, get_self(), [&](auto& m) {
        m.tokenx = rsi_quantity;
        m.tokeny = dmc_quantity;
    });
    SEND_INLINE_ACTION(*this, innerswaprec, { {get_self(), "active"_n} }, { add_balance, to_user });
    return to_user;
}

/**
 * 1. release the DMC from abo_stats, exchange a part of DMC to RSI, then exretire RSI.
 * 2. release the DMC from DMC penalty pool, and ditto.
 * 3. exchange RSI to DMC, and return the amount of DMC.
 */
extended_asset token::get_dmc_by_vrsi(extended_asset add_rsi_quantity)
{
    time_point_sec now_time = time_point_sec(current_time_point());
    // 1. exchange DMC to RSI 
    extended_asset dmc_add = allocation_abo(now_time) + allocation_penalty(now_time);
    exchange_from_uniswap(dmc_add);
    
    // 2. exchange RSI to DMC
    extended_asset to_user = exchange_from_uniswap(add_rsi_quantity);
    return to_user;
}

} /// namespace eosio