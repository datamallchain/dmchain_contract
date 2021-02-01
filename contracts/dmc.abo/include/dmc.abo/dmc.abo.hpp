/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */
#pragma once
#include <string>
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/currency.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/time.hpp>

namespace eosio {

static constexpr uint64_t retire_dalay = 3600;
static constexpr uint64_t time_window = 1800; // 0.5 * 3600
static const account_name system_account = N(datamall);

struct extransfer {
    account_name from;
    account_name to;
    extended_asset quantity;
    std::string memo;
};

struct uniswapsnap {
    account_name owner;
    extended_asset quantity;
};

class abo : public contract {

public:
    abo(account_name self)
        : contract(self) {};

public:
    void handle_receipt(account_name owner, extended_asset quantity);
    void handle_extransfer(account_name from, account_name to, extended_asset quantity, std::string memo);
    void repurchase(account_name owner, uint64_t seed);

private:
    bool check_uniswap(extended_symbol from, extended_symbol to);

    struct currency_stats {
        account_name issuer;
        asset max_supply;

        asset supply;
        asset reserve_supply;

        uint64_t primary_key() const { return supply.symbol.name(); }
        asset get_supply() const { return supply + reserve_supply; }

        EOSLIB_SERIALIZE(currency_stats, (supply)(max_supply)(issuer)(reserve_supply))
    };

    typedef eosio::multi_index<N(stats), currency_stats> stats;

    struct uniswap_market {
        uint64_t primary;
        extended_asset tokenx;
        extended_asset tokeny;
        double total_weights;

        uint64_t primary_key() const { return primary; }
        static key256 key(extended_symbol symbolx, extended_symbol symboly)
        {
            if (symbolx < symboly) {
                auto swap = symbolx;
                symbolx = symboly;
                symboly = swap;
            }
            return key256::make_from_word_sequence<uint64_t>(symbolx.name(), symbolx.contract, symboly.name(), symboly.contract);
        }
        key256 get_key() const { return key(tokenx.get_extended_symbol(), tokeny.get_extended_symbol()); }
        EOSLIB_SERIALIZE(uniswap_market, (primary)(tokenx)(tokeny)(total_weights))
    };
    typedef eosio::multi_index<N(swapmarket), uniswap_market,
        indexed_by<N(bysymbol), const_mem_fun<uniswap_market, key256, &uniswap_market::get_key>>>
        swap_market;

    struct repostats {
        uint64_t repo_id;
        account_name owner;
        extended_asset balance;
        extended_symbol to_sym;
        time_point_sec expiration;
        time_point_sec update_at;

        uint64_t primary_key() const { return repo_id; }

        uint64_t get_owner() const { return owner; };

        uint64_t get_time() const { return uint64_t(update_at.sec_since_epoch()); };
        EOSLIB_SERIALIZE(repostats, (repo_id)(owner)(balance)(to_sym)(expiration)(update_at))
    };
    typedef eosio::multi_index<N(repospool), repostats,
        indexed_by<N(byowner), const_mem_fun<repostats, uint64_t, &repostats::get_owner>>,
        indexed_by<N(bytime), const_mem_fun<repostats, uint64_t, &repostats::get_time>>>
        repo_stats;

    struct repo_id_args {
        account_name from;
        extended_asset quantity;
        string memo;
        time now;
    };
};

} /// eosio
