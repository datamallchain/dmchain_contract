#include <eosio.token/eosio.token.hpp>

namespace eosio {

void token::addreserves(account_name owner, extended_asset x, extended_asset y)
{
    require_auth(owner);

    eosio_assert(x.is_valid() && y.is_valid(), "invalid currency");
    eosio_assert(x.amount > 0 && y.amount > 0, "must addreserves positive amount");
    eosio_assert(x.get_extended_symbol() != y.get_extended_symbol(), "can't create market with same token");

    if (x.get_extended_symbol() < y.get_extended_symbol()) {
        auto swap = x;
        x = y;
        y = swap;
    }
    auto sym_x = x.get_extended_symbol();
    auto sym_y = y.get_extended_symbol();

    if (sym_x == rsi_sym && sym_y == dmc_sym)
        eosio_assert(owner == system_account, "this market only support datamall to addreserves");

    if (owner == system_account) {
        add_stats(owner, x);
        add_stats(owner, y);
    } else {
        sub_balance(owner, x);
        sub_balance(owner, y);
    }
    swap_market market(_self, _self);
    auto m_index = market.get_index<N(bysymbol)>();
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
        eosio_assert(div <= 1.01 && div >= 0.99, "Excessive price volatility");

        SEND_INLINE_ACTION(*this, pricerec, { _self, N(active) }, { price, new_price });
        double total = std::sqrt(real_old_x) * std::sqrt(real_old_y);
        double new_total = std::sqrt(real_new_x) * std::sqrt(real_new_y);

        new_weights = (new_total - total) / total * m_iter->total_weights;
        eosio_assert(new_weights > 0, "Invalid new weights");
        eosio_assert(new_weights / new_weights > 0, "Invalid new weights");
        auto total_weights = m_iter->total_weights + new_weights;
        eosio_assert(new_weights / total_weights > 0.0001, "Add reserves too lower");
        m_index.modify(m_iter, 0, [&](auto& s) {
            s.tokenx = new_x;
            s.tokeny = new_y;
            s.total_weights = total_weights;
        });
    }

    swap_pool pool(_self, primary);
    auto pool_iter = pool.find(owner);
    if (pool_iter != pool.end()) {
        pool.modify(pool_iter, 0, [&](auto& s) {
            s.weights += new_weights;
        });
    } else {
        pool.emplace(owner, [&](auto& s) {
            s.owner = owner;
            s.weights = new_weights;
        });
    }
}

void token::outreserves(account_name owner, extended_symbol x, extended_symbol y, double rate)
{
    require_auth(owner);

    if (x < y) {
        auto swap = x;
        x = y;
        y = swap;
    }

    eosio_assert(x.is_valid() && y.is_valid(), "invalid symbol name");
    eosio_assert(rate > 0 && rate <= 1, "invaild rate");

    swap_market market(_self, _self);
    auto m_index = market.get_index<N(bysymbol)>();
    auto m_iter = m_index.find(uniswap_market::key(x, y));
    eosio_assert(m_iter != m_index.end(), "no matched market record");

    auto primary = m_iter->primary;

    swap_pool pool(_self, m_iter->primary);
    auto pool_iter = pool.find(owner);
    eosio_assert(pool_iter != pool.end(), "no matched pool record");

    auto owner_weights = pool_iter->weights * rate;
    auto total_weights = m_iter->total_weights;

    double out_rate = owner_weights / total_weights;

    extended_asset x_quantity = extended_asset(std::floor(m_iter->tokenx.amount * out_rate), x);
    extended_asset y_quantity = extended_asset(std::floor(m_iter->tokeny.amount * out_rate), y);

    eosio_assert(x_quantity.amount > 0 && y_quantity.amount > 0, "dust attack detected");
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
        pool.modify(pool_iter, 0, [&](auto& s) {
            s.weights -= owner_weights;
        });
        eosio_assert(pool_iter->weights > 0, "negative pool weights amount");
    }
    m_index.modify(m_iter, 0, [&](auto& s) {
        s.tokenx -= x_quantity;
        s.tokeny -= y_quantity;
        s.total_weights -= owner_weights;
        if (last_one)
            s.total_weights = owner_weights;
    });
    eosio_assert(m_iter->tokenx.amount >= 0, "negative tokenx amount");
    eosio_assert(m_iter->tokeny.amount >= 0, "negative tokeny amount");
    eosio_assert(m_iter->total_weights >= 0, "negative total weights amount");
    if (m_iter->total_weights == 0)
        m_index.erase(m_iter);

    if (rate != 1)
        eosio_assert(pool_iter->weights / m_iter->total_weights > 0.0001, "The remaining weight is too low");

    SEND_INLINE_ACTION(*this, outreceipt, { _self, N(active) }, { owner, x_quantity, y_quantity });
    if (owner == system_account) {
        sub_stats(owner, x_quantity);
        sub_stats(owner, y_quantity);
    } else {
        add_balance(owner, x_quantity, owner);
        add_balance(owner, y_quantity, owner);
    }
}

void token::uniswaporder(account_name owner, extended_asset quantity, extended_asset to, double price, account_name id, account_name rampay)
{
    auto from_sym = quantity.get_extended_symbol();
    auto to_sym = to.get_extended_symbol();
    swap_market market(_self, _self);
    auto m_index = market.get_index<N(bysymbol)>();
    auto m_iter = m_index.find(uniswap_market::key(from_sym, to_sym));
    eosio_assert(m_iter != m_index.end(), "this uniswap pair dose not exist");

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
        eosio_assert(false, "symbol precision mismatch");
    }

    m_index.modify(m_iter, 0, [&](auto& s) {
        s.tokenx = marketx;
        s.tokeny = markety;
    });
}

void token::uniswapdeal(account_name owner, extended_asset& market_from, extended_asset& market_to, extended_asset from, extended_asset to, uint64_t primary, double price, account_name rampay)
{
    auto from_sym = from.get_extended_symbol();
    auto to_sym = to.get_extended_symbol();
    double real_market_from = get_real_asset(market_from);
    double real_market_to = get_real_asset(market_to);
    double real_market_total = real_market_from * real_market_to;
    uint64_t min_price = real_market_from / real_market_to * std::pow(2, 32);
    uint64_t price_t = price * std::pow(2, 32);
    bool buy = (from.amount == 0) ? true : false;
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
        if (new_to.amount > 0) {
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
    auto to_fee = extended_asset(std::ceil(spread_ex_to.amount * uniswap_fee), spread_ex_to.get_extended_symbol());
    extended_asset spread_to = spread_ex_to - to_fee;
    to_scrap += to_fee;
    eosio_assert(spread_from.amount > 0 && spread_to.amount > 0, "dust attack detected in uniswap");

    from -= spread_from;
    to -= spread_ex_to;
    eosio_assert(from.amount >= 0 || to.amount >= 0, "can't sub/add negative asset"); // never happened

    sub_asset += spread_from;
    add_asset += spread_to;

    SEND_INLINE_ACTION(*this, pricerec, { _self, N(active) }, { old_price, min_price });
    SEND_INLINE_ACTION(*this, traderecord, { _self, N(active) },
        { owner, eos_account, spread_from, spread_to, to_fee, 0 });

    add_balance(owner, add_asset, rampay);
    sub_balance(owner, sub_asset);

    market_from += from_scrap;
    market_to += to_scrap;

    SEND_INLINE_ACTION(*this, uniswapsnap, { _self, N(active) },
        { owner, add_asset });
}

double token::get_real_asset(extended_asset quantity)
{
    return (double)quantity.amount / std::pow(10, quantity.get_extended_symbol().precision());
}

template <typename T, T (*wipe_function)(T)>
extended_asset token::get_asset_by_amount(T amount, extended_symbol symbol)
{
    uint64_t asset_amount = wipe_function(amount * std::pow(10, symbol.precision()));
    extended_asset asset = extended_asset(asset_amount, symbol);
    eosio_assert(asset.is_valid(), "Invalid asset, please change symbol!");
    return asset;
}

} /// namespace eosio