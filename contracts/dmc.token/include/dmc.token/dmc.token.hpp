/**
 *  @file
 *  @copyright defined in dmc/LICENSE.txt
 */
#pragma once

#include <eosio/system.hpp>
#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>
#include <eosio/crypto.hpp>

#include <string>
#include <cmath>

namespace eosio {

using std::string;

static const name dmc_account = "dmc"_n;
constexpr double static_weights = 10000.0;
constexpr double uniswap_fee = 0.003;
constexpr uint64_t uint64_max = ~uint64_t(0);
constexpr uint32_t uint32_max = ~uint32_t(0);
constexpr uint64_t minimum_token_precision = 0;
constexpr double incentive_rate = 0.1;

static const name system_account = "datamall"_n;
static const name config_account = "dmcconfigura"_n;
static const name empty_account = name { 0 };
static constexpr symbol fee_sym = symbol(symbol_code("FEE"), 4);
static const extended_symbol pst_sym = extended_symbol(symbol(symbol_code("PST"), 0), system_account);
static const extended_symbol rsi_sym = extended_symbol(symbol(symbol_code("RSI"), 4), system_account);
static const extended_symbol dmc_sym = extended_symbol(symbol(symbol_code("DMC"), 4), system_account);
constexpr uint64_t day_sec = 24 * 3600;
constexpr uint64_t week_sec = 7 * day_sec;
constexpr uint64_t default_dmc_claims_interval = week_sec;
constexpr uint64_t default_order_service_epoch = 24 * week_sec;
constexpr uint32_t identifying_code_mask = 0x3FFFFFF;
constexpr uint64_t default_dmc_challenge_interval = day_sec;
constexpr uint64_t default_phishing_interval = 365 * day_sec;
constexpr uint64_t default_initial_price = 10;
constexpr uint64_t default_id_start = 1;
constexpr uint64_t default_max_price_distance = 7;
/**
 * the longest service time for bill / order
 * 24 weeks
*/
constexpr uint64_t default_service_interval = 24 * week_sec;
/**
 * the minimum interval to change the benchmark_stake_rate
 * 7 days
*/ 
constexpr uint64_t default_maker_change_rate_interval = week_sec;
/**
 * the longest time for dmc bill claims
 * 7 days
*/
constexpr uint64_t default_bill_dmc_claims_interval = week_sec;
constexpr uint64_t price_fluncuation_interval = week_sec;

// 12
constexpr uint64_t default_benchmark_stake_rate = 1200;
// 7.2
// n = m * 0.6
constexpr uint64_t default_liquidation_stake_rate = 720;
// 0.3
constexpr uint64_t default_penalty_rate = 30;

constexpr uint64_t default_bill_num_limit = 10;

// for abo
static const name abo_account = "dmfoundation"_n;

enum e_order_state {
    OrderStateWaiting = 0,
    OrderStateDeliver = 1,
    OrderStatePreEnd = 2,
    OrderStatePreCont = 3,
    OrderStateEnd = 4,
    OrderStateCancel = 5,
    OrderStatePreCancel = 6,
    OrderChallengeMask = (1 << 5),
};

enum nft_type_struct {
    NftTypeBegin = 0,
    ERC721 = 1,
    ERC1155 = 2,
    NftTypeEnd,
};
enum e_challenge_state {
    ChallengePrepare = 0,
    ChallengeConsistent = 1,
    ChallengeCancel = 2,
    ChallengeRequest = 3,
    ChallengeAnswer = 4,
    ChallengeArbitrationMinerPay = 5,
    ChallengeArbitrationUserPay = 6,
    ChallengeTimeout = 7,
    ChallengeStateEnd,
};

enum bill_state {
    BILL = 0,
    UNBILL = 1,
};

enum e_account_type {
    ACC_TYPE_NONE = 0,
    ACC_TYPE_USER = 1,
    ACC_TYPE_MINER = 2
};

typedef uint8_t AccountType;

enum e_order_receipt_type {
    OrderReceiptAddReserve = 1,
    OrderReceiptSubReserve = 2,
    OrderReceiptDeposit = 3,
    OrderReceiptClaim = 4,
    OrderReceiptReward = 5,
    OrderReceiptRenew = 6,
    OrderReceiptChallengeReq = 7,
    OrderReceiptChallengeAns = 8,
    OrderReceiptChallengeArb = 9,
    OrderReceiptPayChallengeRet = 10,
    OrderReceiptLockRet = 11,
    OrderReceiptEnd = 12,
};

enum e_asset_receipt_type {
    AssetReceiptAddReserve = 1,
    AssetReceiptSubReserve = 2,
    AssetReceiptDeposit = 3,
    AssetReceiptClaim = 4,
    AssetReceiptReward = 5,
    AssetReceiptPayChallenge = 6,
    AssetReceiptCancel = 7,
    AssetReceiptMinerLock = 8,
    AssetReceiptEnd = 9,
};


enum e_maker_receipt_type {
    MakerReceiptClaim = 1,
    MakerReceiptChallengePay = 2,
    MakerReceiptIncrease = 3,
    MakerReceiptRedemption = 4,
    MakerReceiptLiquidation = 5,
    MakerReceiptLock = 6,
};

enum e_maker_distribute_type {
    MakerDistributeAccount = 1,
    MakerDistributePool = 2,
};

enum e_price_range_type {
    RangeTypeBegin = 0,
    TwentyPercent = 1,
    ThirtyPercent = 2,
    NoLimit = 3,
    RangeTypeEnd,
}; 

enum e_allocation_type {
    AllocationAbo = 1,
    AllocationPenalty = 2,
};

typedef uint8_t OrderState;
typedef uint8_t ChallengeState;

typedef uint8_t nft_type;
typedef uint8_t OrderReceiptType;
typedef uint8_t AssetReceiptType;
typedef uint8_t MakerReceiptType;
typedef uint8_t MakerDistributeType;
typedef uint8_t PriceRangeType;
typedef uint8_t AllocationType;

CONTRACT token : public contract {

public:
    token(name receiver, name code, datastream<const char*> ds);

    struct nft_batch_args {
        uint64_t nft_id;
        extended_asset quantity;
    };

    struct asset_type_args {
        extended_asset quant;
        uint8_t type;
    };

public:

    ACTION create(name issuer, asset max_supply);

    ACTION issue(name to, asset quantity, string memo);

    ACTION retire(asset quantity, string memo);

    ACTION transfer(name from, name to, asset quantity, string memo);

    ACTION close(name owner, const symbol& symbol);

public:

    ACTION excreate(name issuer, asset maximum_supply, asset reserve_supply, time_point_sec expiration);

    ACTION exissue(name to, extended_asset quantity, string memo);

    ACTION exretire(name from, extended_asset value, string memo);

    ACTION extransfer(name from, name to, extended_asset quantity, string memo);

    ACTION exclose(name owner, extended_symbol symbol);

public:

    void exchange(name owner, extended_asset quantity, extended_asset to, double price, name id, string memo);

    ACTION exdestroy(extended_symbol sym);

public:

    ACTION exlocktrans(name from, name to, extended_asset quantity, time_point_sec expiration, time_point_sec expiration_to, string memo);

    ACTION exunlock(name owner, extended_asset quantity, time_point_sec expiration, string memo);

    ACTION exlock(name from, extended_asset quantity, time_point_sec expiration, string memo);

public:

    void addreserves(name owner, extended_asset token_x, extended_asset token_y);

    void outreserves(name owner, extended_symbol x, extended_symbol y, double rate);

public:

    ACTION bill(name owner, extended_asset asset, double price, time_point_sec expire_on, uint64_t deposit_ratio, string memo);

    ACTION unbill(name owner, uint64_t bill_id, string memo);

    ACTION getincentive(name owner, uint64_t bill_id);

    ACTION setabostats(uint64_t stage, double user_rate, double foundation_rate, extended_asset total_release, extended_asset remaining_release, time_point_sec start_at, time_point_sec end_at, time_point_sec last_released_at);

    ACTION order(name owner, uint64_t bill_id, uint64_t benchmark_price, PriceRangeType price_range, uint64_t epoch, extended_asset asset, extended_asset reserve, string memo);
    
    ACTION setreserve(name owner, extended_asset dmc_quantity, extended_asset rsi_quantity);

public:

    ACTION allocation(string memo);

    ACTION increase(name owner, extended_asset asset, name miner);

    ACTION redemption(name owner, double rate, name miner);

    ACTION mint(name owner, extended_asset asset);

    ACTION setmakerrate(name owner, double rate);

    ACTION setmakerbstr(name owner, uint64_t self_benchmark_stake_rate);

    ACTION liquidation(string memo);

    ACTION addmerkle(name sender, uint64_t order_id, checksum256 merkle_root, uint64_t data_block_count);

    ACTION reqchallenge(name sender, uint64_t order_id, uint64_t data_id, checksum256 hash_data, std::string nonce);

    ACTION anschallenge(name sender, uint64_t order_id, checksum256 reply_hash);

    ACTION arbitration(name sender, uint64_t order_id, const std::vector<char>& data, std::vector<checksum256> cut_merkle);

    ACTION paychallenge(name sender, uint64_t order_id);

    ACTION setdmcconfig(name key, uint64_t value);

    ACTION claimorder(name payer, uint64_t order_id);

    ACTION claimdeposit(name payer, uint64_t order_id);

    ACTION addordasset(name sender, uint64_t order_id, extended_asset quantity);

    ACTION subordasset(name sender, uint64_t order_id, extended_asset quantity);

    ACTION updateorder(name payer, uint64_t order_id);

    ACTION cancelorder(name sender, uint64_t order_id);

    ACTION nftcreatesym(extended_symbol nft_symbol, std::string symbol_uri, nft_type type);

    ACTION nftcreate(name to, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity);

    ACTION nftissue(name to, uint64_t nft_id, extended_asset quantity);

    ACTION nfttransfer(name from, name to, uint64_t nft_id, extended_asset quantity, std::string memo);

    ACTION nfttransferb(name from, name to, std::vector<nft_batch_args> batch_args, std::string memo);

    ACTION nftburn(name from, uint64_t nft_id, extended_asset quantity);

    ACTION burnbatch(name from, std::vector<nft_batch_args> batch_args);

private:
    void uniswaporder(name owner, extended_asset quantity, extended_asset to, double price, name id, name rampay);
    double get_real_asset(extended_asset quantity);

    template <typename T, T (*wipe_function)(T)>
    extended_asset get_asset_by_amount(T amount, extended_symbol symbol);

    void uniswapdeal(name owner, extended_asset& market_from, extended_asset& market_to, extended_asset from, extended_asset to_sym, uint64_t primary, double price, name rampay);

    extended_asset exchange_from_uniswap(extended_asset add_balance);

    /*! 
     * 1. release the DMC from abo_stats, exchange a part of DMC to RSI, then exretire RSI.
     * 2. release the DMC from DMC penalty pool, and ditto.
     * 3. exchange RSI to DMC, and return the amount of DMC.
    */
    extended_asset get_dmc_by_vrsi(extended_asset rsi_quantity);

    extended_asset allocation_abo(time_point_sec now_time);

    extended_asset allocation_penalty(time_point_sec now_time);

    void increase_penalty(extended_asset quantity);

public:
    ACTION outreceipt(name owner, extended_asset x, extended_asset y);
    ACTION traderecord(name owner, name oppo, extended_asset from, extended_asset to, extended_asset fee, uint64_t bid_id);
    ACTION pricerec(uint64_t old_price, uint64_t new_price);
    ACTION uniswapsnap(name owner, extended_asset quantity);
    
public:
    ACTION incentiverec(name owner, extended_asset inc, uint64_t bill_id);
    ACTION redeemrec(name owner, name miner, extended_asset asset);
    ACTION liqrec(name miner, extended_asset pst_asset, extended_asset dmc_asset);
    ACTION billliqrec(name miner, uint64_t bill_id, extended_asset sub_pst);
    ACTION currliqrec(name miner, extended_asset sub_pst);

public:
    ACTION nftsymrec(uint64_t symbol_id, extended_symbol nft_symbol, std::string symbol_uri, nft_type type);
    ACTION nftrec(uint64_t symbol_id, uint64_t nft_id, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity);
    ACTION nftaccrec(uint64_t symbol_id, uint64_t nft_id, name owner, extended_asset quantity);
    ACTION allocrec(extended_asset quantity, AllocationType type);
    ACTION innerswaprec(extended_asset vrsi, extended_asset dmc);
public:
    inline asset get_supply(symbol_code sym) const;

private:
    void change_pst(name owner, extended_asset value, bool lock = false);
    /**
     * when issue add it
     **/
    void add_stats(extended_asset quantity);
    /**
     * when retrie, sub it
     */
    void sub_stats(extended_asset quantity);

    void trace_price_history(double price, uint64_t bill_id, uint64_t order_id);

    ChallengeState get_challenge_state(uint64_t order_id);
    bool is_challenge_end(ChallengeState state);

private:
    uint64_t get_dmc_config(name key, uint64_t default_value);
    void set_dmc_config(name key, uint64_t value);
    double get_dmc_rate(uint64_t rate_value);
    double get_benchmark_price();

public:
    TABLE nft_symbol_info {
        uint64_t symbol_id;
        extended_symbol nft_symbol;
        std::string symbol_uri;
        nft_type type;

        uint64_t primary_key() const { return symbol_id; }

        static uint128_t get_extended_symbol(extended_symbol symbol)
        {
            return ((uint128_t(symbol.get_symbol().code().raw()) << 64) + symbol.get_contract().value);
        }
        uint128_t by_symbol() const { return get_extended_symbol(nft_symbol); }
    };

    typedef eosio::multi_index<"nftsymbols"_n, nft_symbol_info,
        indexed_by<"extsymbol"_n, const_mem_fun<nft_symbol_info, uint128_t, &nft_symbol_info::by_symbol>>>
        nft_symbols;

    TABLE nft_info {
        uint64_t nft_id;
        extended_asset supply;
        std::string nft_uri;
        std::string nft_name;
        std::string extra_data;

        uint64_t primary_key() const { return nft_id; }
    };
    typedef eosio::multi_index<"nftinfo"_n, nft_info> nft_infos;

    TABLE nft_balance {
        uint64_t primary;
        name owner;
        uint64_t nft_id;
        extended_asset quantity;

        uint64_t primary_key() const { return primary; }
        uint64_t by_owner() const { return owner.value; }
        uint64_t by_nft_id() const { return nft_id; }
        static uint128_t get_owner_id(name owner, uint64_t nft_id)
        {
            return ((uint128_t(owner.value) << 64) + nft_id);
        }
        uint128_t by_owner_id() const { return get_owner_id(owner, nft_id); }
    };
    typedef eosio::multi_index<"nftbalance"_n, nft_balance,
        indexed_by<"owner"_n, const_mem_fun<nft_balance, uint64_t, &nft_balance::by_owner>>,
        indexed_by<"nftid"_n, const_mem_fun<nft_balance, uint64_t, &nft_balance::by_nft_id>>,
        indexed_by<"ownerid"_n, const_mem_fun<nft_balance, uint128_t, &nft_balance::by_owner_id>>>
        nft_balances;

    TABLE account {
        uint64_t primary;
        extended_asset balance;

        uint64_t primary_key() const { return primary; }
        static uint128_t key(extended_symbol symbol)
        {
            return ((uint128_t(symbol.get_symbol().code().raw()) << 64) + symbol.get_contract().value);
        }
        uint128_t get_key() const { return key(balance.get_extended_symbol()); }
    };

    typedef eosio::multi_index<"accounts"_n, account,
        indexed_by<"byextendedas"_n, const_mem_fun<account, uint128_t, &account::get_key>>>
        accounts;

    TABLE lock_account {
        uint64_t primary;
        extended_asset balance;
        time_point_sec lock_timestamp;

        uint64_t primary_key() const { return primary; }

        static checksum256 key(extended_symbol symbol, time_point_sec lock_timestamp)
        {
            return checksum256::make_from_word_sequence<uint64_t>(symbol.get_symbol().code().raw(), symbol.get_contract().value, uint64_t(lock_timestamp.sec_since_epoch()));
        }
        checksum256 get_key() const { return key(balance.get_extended_symbol(), lock_timestamp); }
    };

    typedef eosio::multi_index<"lockaccounts"_n, lock_account,
        indexed_by<"byextendedas"_n, const_mem_fun<lock_account, checksum256, &lock_account::get_key>>>
        lock_accounts;

    TABLE currency_stats {
        asset supply;
        asset max_supply;
        name issuer;
        asset reserve_supply;

        uint64_t primary_key() const { return supply.symbol.code().raw(); }
        asset get_supply() const { return supply + reserve_supply; }
    };

    typedef eosio::multi_index<"stats"_n, currency_stats> stats;

    TABLE uniswap_market {
        uint64_t primary;
        extended_asset tokenx;
        extended_asset tokeny;
        double total_weights;

        uint64_t primary_key() const { return primary; }
        static checksum256 key(extended_symbol symbolx, extended_symbol symboly)
        {
            if (symbolx < symboly) {
                auto swap = symbolx;
                symbolx = symboly;
                symboly = swap;
            }
            return checksum256::make_from_word_sequence<uint64_t>(symbolx.get_symbol().code().raw(), symbolx.get_contract().value, symboly.get_symbol().code().raw(), symboly.get_contract().value);
        }
        checksum256 get_key() const { return key(tokenx.get_extended_symbol(), tokeny.get_extended_symbol()); }
    };
    typedef eosio::multi_index<"swapmarket"_n, uniswap_market,
        indexed_by<"bysymbol"_n, const_mem_fun<uniswap_market, checksum256, &uniswap_market::get_key>>>
        swap_market;

    TABLE inner_uniswap_market {
        uint64_t primary;
        extended_asset tokenx;
        extended_asset tokeny;
        uint64_t primary_key() const { return primary; }
        
        static checksum256 key(extended_symbol symbolx, extended_symbol symboly)
        {
            if (symbolx < symboly) {
                auto swap = symbolx;
                symbolx = symboly;
                symboly = swap;
            }
            return checksum256::make_from_word_sequence<uint64_t>(symbolx.get_symbol().code().raw(), symbolx.get_contract().value, symboly.get_symbol().code().raw(), symboly.get_contract().value);
        }
        checksum256 get_key() const { return key(tokenx.get_extended_symbol(), tokeny.get_extended_symbol()); }
    };
    typedef eosio::multi_index<"innermarker"_n, inner_uniswap_market,
        indexed_by<"bysymbol"_n, const_mem_fun<inner_uniswap_market, checksum256, &inner_uniswap_market::get_key>>>
        inner_market;

    TABLE market_pool {
        name owner;
        double weights;

        uint64_t primary_key() const { return owner.value; }
        EOSLIB_SERIALIZE(market_pool, (owner)(weights))
    };
    typedef eosio::multi_index<"swappool"_n, market_pool> swap_pool;

    TABLE bill_record {
        uint64_t bill_id;
        name owner;
        extended_asset matched;
        extended_asset unmatched;
        uint64_t price;
        time_point_sec created_at;
        time_point_sec updated_at;
        time_point_sec expire_on;
        uint64_t deposit_ratio;

        uint64_t primary_key() const { return bill_id; }
        uint64_t get_lower() const { return price; }
        uint64_t get_owner() const { return owner.value; }
        uint64_t get_ratio() const { return deposit_ratio; }
        uint64_t get_unmatched() const { return unmatched.quantity.amount; }
        uint64_t get_time() const { return uint64_t(updated_at.sec_since_epoch()); }
        uint64_t by_expire() const { return uint64_t(expire_on.sec_since_epoch()); }
    };
    typedef eosio::multi_index<"billrec"_n, bill_record,
        indexed_by<"bylowerprice"_n, const_mem_fun<bill_record, uint64_t, &bill_record::get_lower>>,
        indexed_by<"byowner"_n, const_mem_fun<bill_record, uint64_t, &bill_record::get_owner>>,
        indexed_by<"byratio"_n, const_mem_fun<bill_record, uint64_t, &bill_record::get_ratio>>,
        indexed_by<"bycunmatched"_n, const_mem_fun<bill_record, uint64_t, &bill_record::get_unmatched>>,
        indexed_by<"bytime"_n, const_mem_fun<bill_record, uint64_t, &bill_record::get_time>>,
        indexed_by<"byexpire"_n, const_mem_fun<bill_record, uint64_t, &bill_record::by_expire>>>
        bill_stats;

    TABLE pst_stats {
        name owner;
        extended_asset amount;

        uint64_t primary_key() const { return owner.value; }
        uint64_t by_amount() const { return -amount.quantity.amount; }
    };
    typedef eosio::multi_index<"pststats"_n, pst_stats> pststats;

    TABLE abo_stats {
        uint64_t stage;
        double user_rate;
        double foundation_rate;
        extended_asset total_release;
        extended_asset remaining_release;
        time_point_sec start_at;
        time_point_sec end_at;
        time_point_sec last_user_released_at;
        time_point_sec last_foundation_released_at;

        uint64_t primary_key() const { return stage; }
        EOSLIB_SERIALIZE(abo_stats, (stage)(user_rate)(foundation_rate)(total_release)(remaining_release)(start_at)(end_at)(last_user_released_at)(last_foundation_released_at))
    };
    typedef eosio::multi_index<"abostats"_n, abo_stats> abostats;

    TABLE penalty_stats {
        time_point_sec start_at;
        time_point_sec end_at;
        extended_asset remaining_release;

        uint64_t primary_key() const { return uint64_t(end_at.sec_since_epoch()); }
        EOSLIB_SERIALIZE(penalty_stats, (start_at)(end_at)(remaining_release))
    };
    typedef eosio::multi_index<"penaltystats"_n, penalty_stats> penaltystats;

    TABLE dmc_config {
        name key;
        uint64_t value;

        uint64_t primary_key() const { return key.value; }
    };
    typedef eosio::multi_index<"dmcconfig"_n, dmc_config> dmc_global;

    TABLE dmc_order {
        uint64_t order_id;
        name user;
        name miner;
        uint64_t bill_id;
        extended_asset user_pledge; // dmc
        extended_asset miner_lock_pst; // pst
        extended_asset miner_lock_dmc;
        extended_asset price; // dmc per price
        extended_asset settlement_pledge;
        extended_asset lock_pledge;
        OrderState state;
        time_point_sec deliver_start_date;
        time_point_sec latest_settlement_date;
        extended_asset miner_lock_rsi;
        extended_asset miner_rsi;
        extended_asset user_rsi;
        extended_asset deposit;
        uint64_t epoch;
        time_point_sec deposit_valid;
        time_point_sec cancel_date;

        uint64_t primary_key() const { return order_id; }
        static uint128_t get_state_id(OrderState state, uint64_t order_id)
        {
            return ((uint128_t(state) << 64) + order_id);
        }
        uint128_t by_state_id() const { return get_state_id(state, order_id); }
        uint64_t by_user() const { return user.value; }
        uint64_t by_miner() const { return miner.value; }
        uint64_t by_settlement_date() const { 
            if (state == OrderStateWaiting) {
                return uint64_max;
            } else if (state == OrderStateCancel || state == OrderStateEnd) {
                if (user_pledge.quantity.amount || lock_pledge.quantity.amount || deposit.quantity.amount) {
                    return uint64_max;
                }
            }
            return latest_settlement_date.sec_since_epoch(); 
        }
    };
    typedef eosio::multi_index<"dmcorder"_n, dmc_order,
        indexed_by<"user"_n, const_mem_fun<dmc_order, uint64_t, &dmc_order::by_user>>,
        indexed_by<"miner"_n, const_mem_fun<dmc_order, uint64_t, &dmc_order::by_miner>>,
        indexed_by<"stateid"_n, const_mem_fun<dmc_order, uint128_t, &dmc_order::by_state_id>>,
        indexed_by<"settlement"_n, const_mem_fun<dmc_order, uint64_t, &dmc_order::by_settlement_date>>>
        dmc_orders;

    TABLE dmc_challenge {
        uint64_t order_id;
        checksum256 pre_merkle_root;
        uint64_t pre_data_block_count;
        checksum256 merkle_root;
        uint64_t data_block_count;
        name merkle_submitter;
        uint64_t data_id;
        checksum256 hash_data;
        uint64_t challenge_times;
        std::string nonce;
        ChallengeState state;
        extended_asset user_lock;
        extended_asset miner_pay;
        time_point_sec challenge_date;
        name challenger;

        uint64_t primary_key() const { return order_id; }
    };
    typedef eosio::multi_index<"dmchallenge"_n, dmc_challenge> dmc_challenges;

    TABLE dmc_maker {
        name miner;
        double current_rate; // r
        double miner_rate;
        double total_weight;
        extended_asset total_staked;
        // m' = benchmark_stake_rate / 100.0
        // n' = m' * 0.6
        uint64_t benchmark_stake_rate;
        time_point_sec rate_updated_at;

        uint64_t primary_key() const { return miner.value; }
        uint64_t by_m() const { return benchmark_stake_rate; }
        uint64_t get_n() const { return benchmark_stake_rate * 0.6; }
        double get_real_m() const { return benchmark_stake_rate / 100.0; }
        double by_rate() const { return current_rate / get_n(); }
    };
    typedef eosio::multi_index<"dmcmaker"_n, dmc_maker,
        indexed_by<"byrate"_n, const_mem_fun<dmc_maker, double, &dmc_maker::by_rate>>,
        indexed_by<"bym"_n, const_mem_fun<dmc_maker, uint64_t, &dmc_maker::by_m>>>
        dmc_makers;

    TABLE maker_pool {
        name owner;
        double weight;

        uint64_t primary_key() const { return owner.value; };
    };
    typedef eosio::multi_index<"makerpool"_n, maker_pool> dmc_maker_pool;

    TABLE price_history {
        uint64_t primary;
        uint64_t bill_id;
        uint64_t order_id;
        double price;
        time_point_sec created_at;

        uint64_t primary_key() const { return primary; }
        double by_prices() const { return -price; }
        uint64_t get_time() const { return uint64_max - uint64_t(created_at.sec_since_epoch()); };
        uint64_t by_bill_id() const { return bill_id; }
        uint64_t by_order_id() const { return order_id; }
    };
    typedef eosio::multi_index<"dmcprice"_n, price_history,
        indexed_by<"byprice"_n, const_mem_fun<price_history, double, &price_history::by_prices>>,
        indexed_by<"bytime"_n, const_mem_fun<price_history, uint64_t, &price_history::get_time>>,
        indexed_by<"bybill"_n, const_mem_fun<price_history, uint64_t, &price_history::by_bill_id>>,
        indexed_by<"byorder"_n, const_mem_fun<price_history, uint64_t, &price_history::by_order_id>>>
        price_table;

    TABLE bc_price {
        std::vector<double> prices;
        double benchmark_price;
        uint64_t primary_key() const { return 0; }
    };
    typedef eosio::multi_index<"bcprice"_n, bc_price> bc_price_table;

    struct maker_lp_pool {
        name owner;
        double ratio;
    };

    TABLE maker_snapshot {
        uint64_t order_id;
        name miner;
        uint64_t bill_id;
        uint64_t rate;
        std::vector<maker_lp_pool> lps;
        uint64_t primary_key() const { return order_id; }
    };
    typedef eosio::multi_index<"makesnapshot"_n, maker_snapshot> maker_snapshot_table;

    struct distribute_maker_snapshot {
        name lp;
        extended_asset quantity;
        MakerDistributeType type;
    };

public:
    // 1: create 2: update 3: destory
    ACTION orderrec(dmc_order order_info, uint8_t type);
    ACTION challengerec(dmc_challenge challenge_info);
    ACTION billsnap(bill_record bill_info);
    ACTION makerecord(dmc_maker maker_info);
    ACTION makerpoolrec(name miner, std::vector<maker_pool> pool_info);
    ACTION makersnaprec(maker_snapshot maker_snapshot);
    ACTION dismakerec(uint64_t order_id, std::vector<asset_type_args> rewards, extended_asset total_sub, std::vector<distribute_maker_snapshot> distribute_info);
    ACTION assetrec(uint64_t order_id, std::vector<extended_asset> changed, name owner, AssetReceiptType rec_type);
    ACTION orderassrec(uint64_t order_id, std::vector<asset_type_args> changed, name owner, AccountType acc_type, time_point_sec exec_date);

private:
    inline static name get_foundation(name issuer)
    {
        return issuer == system_account ? dmc_account : issuer;
    }

    void sub_balance(name owner, extended_asset value);
    void add_balance(name owner, extended_asset value, name ram_payer);

    void lock_sub_balance(name owner, extended_asset value, time_point_sec lock_timestamp);
    void lock_sub_balance(name foundation, extended_asset quantity, bool recur = false);
    void lock_add_balance(name owner, extended_asset value, time_point_sec lock_timestamp, name ram_payer);
    // convert balance to lock_balance
    void exchange_balance_to_lockbalance(name owner, extended_asset value, time_point_sec lock_timestamp, name ram_payer);
    extended_asset get_balance(extended_asset quantity, name name);

private:
    uint64_t calbonus(name owner, uint64_t primary, name ram_payer);
    double cal_current_rate(extended_asset dmc_asset, name owner, double real_m);

private:
    void generate_maker_snapshot(uint64_t order_id, uint64_t bill_id, name miner, name payer, uint64_t r, bool reset = false);
    void update_order_asset(dmc_order& order, OrderState new_state, uint64_t claims_interval);
    void change_order(dmc_order& order, const dmc_challenge& challenge, time_point_sec current, uint64_t claims_interval, name payer);
    void update_order(dmc_order& order, const dmc_challenge& challenge, name payer);
    extended_asset distribute_lp_pool(uint64_t order_id, std::vector<asset_type_args> rewards, extended_asset challenge_pledge, name payer);
    void phishing_challenge();
    void delete_maker_snapshot(uint64_t order_id);
    void delete_order_pst(const dmc_order& order);
    void send_totalvote_to_system(name owner);
};

asset token::get_supply(symbol_code sym) const
{
    stats statstable(get_self(), system_account.value);
    const auto& st = statstable.get(dmc_sym.get_symbol().code().raw());
    return st.get_supply();
}

template <typename T, T (*wipe_function)(T)>
extended_asset token::get_asset_by_amount(T amount, extended_symbol symbol)
{
    uint64_t asset_amount = wipe_function(amount * std::pow(10, symbol.get_symbol().precision()));
    extended_asset asset = extended_asset(asset_amount, symbol);
    check(asset.quantity.is_valid(), "Invalid asset, please change symbol!");
    return asset;
}

template <typename T>
checksum256 sha256(const T& value)
{
    auto digest = pack(value);
    checksum256 hash = sha256(digest.data(), uint32_t(digest.size()));
    return hash;
}
} // namespace eosio