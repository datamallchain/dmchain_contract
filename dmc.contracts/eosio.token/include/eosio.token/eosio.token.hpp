/**
 *  @file
 *  @copyright defined in fibos/LICENSE.txt
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/crypto.h>

#include <string>
#include <cmath>

namespace eosio {

using std::string;

static const account_name eos_account = N(eosio);
constexpr double static_weights = 10000.0;
constexpr double uniswap_fee = 0.003;
constexpr uint64_t uint64_max = ~uint64_t(0);
constexpr uint64_t minimum_token_precision = 0;
constexpr double miner_scale = 0.8;

// for DMC
static const account_name system_account = N(datamall);
static const account_name empty_account = name { 0 };
static const extended_symbol pst_sym = extended_symbol(S(0, PST), system_account);
static const extended_symbol rsi_sym = extended_symbol(S(8, RSI), system_account);
static const extended_symbol dmc_sym = extended_symbol(S(4, DMC), system_account);
constexpr uint64_t default_dmc_claims_interval = 7 * 24 * 3600;
constexpr uint32_t identifying_code_mask = 0x3FFFFFF;
constexpr uint64_t default_dmc_challenge_interval = 24 * 60 * 60;

constexpr uint64_t default_bill_dmc_claims_interval = 7 * 24 * 3600;
constexpr uint64_t price_fluncuation_interval = 7 * 24 * 3600;
constexpr uint64_t seconds_three_days = 3 * 24 * 3600;

// 2
constexpr uint64_t default_benchmark_stake_rate = 200;
// 1.25
constexpr uint64_t default_liquidation_stake_rate = 125;
// 0.3
constexpr uint64_t default_penalty_rate = 30;

// for abo
static const account_name abo_account = N(dmfoundation);

// 业务类型
enum e_order_state {
    OrderStateWaiting = 0,
    OrderStateDeliver = 1,
    OrderStatePreEnd = 2,
    OrderStatePreCont = 3,
    OrderStateEnd = 4,
    OrderStateAbort = 5,
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

enum e_order_receipt_type {
    OrderReceiptUpdate = 1,
    OrderReceiptClaim = 2,
    OrderReceiptUser = 3,
    OrderReceiptChallengeReq = 4,
    OrderReceiptChallengeAns = 5,
    OrderReceiptChallengeArb = 6,
    OrderReceiptChallengePayRet = 7,
    OrderReceiptChallengePayReward = 8,
    OrderReceiptLockRet = 9,
    OrderReceiptEnd = 10,
};

enum e_maker_receipt_type {
    MakerReceiptClaim = 1,
    MakerReceiptChallengePay = 2,
    MakerReceiptIncrease = 3,
    MakerReceiptRedemption = 4,
    MakerReceiptLiquidation = 5,
};

typedef uint8_t OrderState;
typedef uint8_t ChallengeState;

typedef uint8_t nft_type;
typedef uint8_t OrderReceiptType;
typedef uint8_t MakerReceiptType;

class token : public contract {

public:
    token(account_name self)
        : contract(self)
    {
        //set_order_migration(0, self);
    }

    struct nft_batch_args {
        uint64_t nft_id;
        extended_asset quantity;
    };

public:
    /*! @brief ClassicToken 创建函数
     @param issuer 通证发行账号
     @param max_supply 通证最大发行量
     */
    void create(account_name issuer, asset max_supply);

    /*! @brief ClassicToken 增发函数
     @param to 接收新通证的账号
     @param quantity 新增通证数量
     @param memo 备注
     */
    void issue(account_name to, asset quantity, string memo);

    /*! @brief ClassicToken 回收函数
     @param quantity 回收通证数量
     @param memo 备注
     */
    void retire(asset quantity, string memo);

    /*! @brief ClassicToken 转账函数
     @param from 发送账号
     @param to 接收账号
     @param quantity 转账通证数量
     @param memo 备注
     */
    void transfer(account_name from, account_name to, asset quantity, string memo);

    /*! @brief ClassicToken 资源回收函数
     * 当通证数量为0时，可以回收RAM

     @param owner 通证发行账号
     @param symbol 回收通证类型
     */
    void close(account_name owner, symbol_type symbol);

public:
    /*! @brief SmartToken 创建函数
     @param issuer 通证发行账号
     @param maximum_supply 最大可发行通证数量
     @param reserve_supply 未流通通证数量
     @param expiration 项目方预设的项目锁仓期
     */
    void excreate(account_name issuer, asset maximum_supply, asset reserve_supply, time_point_sec expiration);

    /*! @brief SmartToken 增发函数
     @param to 接收新通证的账号
     @param quantity 新增通证数量
     @param memo 备注
     */
    void exissue(account_name to, extended_asset quantity, string memo);

    /*! @brief SmartToken 销毁函数
    @param from 销毁通证的账号
     @param quantity 销毁通证数量
     @param memo 备注
     */
    void exretire(account_name from, extended_asset value, string memo);

    /*! @brief SmartToken 转账函数
     @param from 发送账号
     @param to 接收账号
     @param quantity 转账通证数量
     @param memo 备注
     */
    void extransfer(account_name from, account_name to, extended_asset quantity, string memo);

    /*! @brief SmartToken 资源回收函数
     * 当通证数量为0时，可以回收RAM

     @param owner 通证发行账号
     @param symbol 回收通证类型
     */
    void exclose(account_name owner, extended_symbol symbol);

public:
    /*! @brief SmartToken 兑换函数
     @param owner 兑换账号
     @param quantity 兑换通证数量
     @param tosymbol 兑换通证目标类型
     @param memo 备注
     */
    void exchange(account_name owner, extended_asset quantity, extended_asset to, double price, account_name id, string memo);

    /*! @brief SmartToken 删除函数
     * 删除通证满足满足如下条件并回收RAM。
     * 1. 通证发行者
     * 2. 通证发行者拥有所有通证流通数量
     * 3. 没有设置锁仓期的通证

     @param sym 所需删除通证类型
     @param memo 备注
     */
    void exdestroy(extended_symbol sym);

public:
    /*! @brief LockToken 转账函数
     @param from 发送账号
     @param to 接收账号
     @param quantity 转账通证数量
     @param expiration 待转出锁仓时间
     @param expiration_to 待转入锁仓时间
     @param memo 备注
     */
    void exlocktrans(account_name from, account_name to, extended_asset quantity, time_point_sec expiration, time_point_sec expiration_to, string memo);

    /*! @brief LockToken 解锁函数
     @param owner 解锁账号
     @param quantity 解锁通证数量
     @param expiration 锁仓时间
     @param memo 备注
     */
    void exunlock(account_name owner, extended_asset quantity, time_point_sec expiration, string memo);

    /*! @brief LockToken 加锁函数
     @param owner 加锁账号
     @param quantity 加锁通证数量
     @param expiration 加锁时间
     @param memo 备注
     */
    void exlock(account_name from, extended_asset quantity, time_point_sec expiration, string memo);

public:
    /*! @brief uniswap中充值
     @param owner 加仓账户
     @param token_x  加仓的 x 通证数
     @param token_y  加仓的 y 通证数
    */
    void addreserves(account_name owner, extended_asset token_x, extended_asset token_y);

    /*! @brief uniswap中提取
     @param owner 提取账户
     @param x  提取 x 的通证类型
     @param y  提取 y 的通证类型
     @param rate 提取比例
    */
    void outreserves(account_name owner, extended_symbol x, extended_symbol y, double rate);

public:
    /*! @brief 质押PST获得积分
     @param owner 质押账户
     @param asset 质押数量
     @param memo 备注
     */
    void bill(account_name owner, extended_asset asset, double price, string memo);

    /*! @brief 取消质押PST
     @param owner 取消质押账户
     @param primary 取消的 id
     @param memo 备注
    */
    void unbill(account_name owner, uint64_t bill_id, string memo);

    /*! @brief 领取激励
     @param owner 领取激励账户
     @param primary 领取的id
    */
    void getincentive(account_name owner, uint64_t bill_id);

    /*! @brief 设置 DMC 释放
     @param stage 释放阶段，目前1-11
     @param user_rate 释放时用户占比
     @param foundation_rate 释放时基金会占比
     @param total_release 该阶段释放总额
     @param start_at 该阶段开始时间
     @param end_at 该阶段结束时间
    */
    void setabostats(uint64_t stage, double user_rate, double foundation_rate, extended_asset total_release, time_point_sec start_at, time_point_sec end_at);

    /** @brief 释放 DMC
     @param memo 附言
     */
    void allocation(string memo);

    /*! @brief 撮合挂单订单
     @param owner 交易者
     @param miner 挂单者
     @param bill_id 挂单 id
     @param asset 交易数量
     @param reserve 预存数量
     @param memo 附言
     */
    void order(account_name owner, account_name miner, uint64_t bill_id, extended_asset asset, extended_asset reserve, string memo);

public:
    /*! @brief 增加准备金
    @param owner 矿工 / lp
    @param asset 抵押的 DMC 量
    @param miner 矿工
    */
    void increase(account_name owner, extended_asset asset, account_name miner);

    /*! @brief 赎回准备金
    @param owner 矿工 / lp
    @param double 赎回的比例
    @param miner 矿工
    */
    void redemption(account_name owner, double rate, account_name miner);

    /*! @brief 铸造 PST
    @param owner 矿工
    @param asset 铸造的 PST 数量
    */
    void mint(account_name owner, extended_asset asset);

    /*! @brief 修改比例
    @param owner 矿工
    @param rate 矿工最低质押比例
    */
    void setmakerrate(account_name owner, double rate);

    /*! @brief 清算
    @param memo 附言
    由 onblock 方法调用，定期清算
    */
    void liquidation(string memo);

    void cleanpst(string memo);

    /*! @brief 矿工和用户提供一致的默克尔树根信息
    @param sender 提交者
    @param order_id 订单id
    @param merkle_root 默克尔树根
    */
    void addmerkle(name sender, uint64_t order_id, checksum256 merkle_root, uint64_t data_block_count);

    /*! @brief 用户请求挑战
    @param sender 用户账户名
    @param order_id 订单id
    @param data_id 数据块id
    @param hash_data 数据哈希两次后的结果
    @param nonce 生成hash时的混淆值
    */
    void reqchallenge(name sender, uint64_t order_id, uint64_t data_id, checksum256 hash_data, std::string nonce);

    /*! @brief 矿工响应挑战
    @param sender 矿工账户名
    @param order_id 订单id
    @param reply_hash 矿工的数据响应哈希
    */
    void anschallenge(name sender, uint64_t order_id, checksum256 reply_hash);

    /*! @brief 矿工完成仲裁
    @param sender 矿工账户名
    @param order_id 订单id
    @param data 数据信息
    @param cut_merkle 默克尔剪枝树信息
    */
    void arbitration(name sender, uint64_t order_id, const std::vector<char>& data, std::vector<checksum256> cut_merkle);

    /*! @brief 挑战超时赔付
    @param sender 账户名
    @param order_id 订单id
    */
    void paychallenge(name sender, uint64_t order_id);

    /*! @brief 设置dmc配置项
    @param payer 配置的键
    @param order_id 配置的值
    */
    void setdmcconfig(account_name key, uint64_t value);

    /*! @brief 领取交付奖励
    @param payer 账户名
    @param order_id 订单id
    */
    void claimorder(name payer, uint64_t order_id);

    /*! @brief 用户增加订单预存额
    @param sender 账户名
    @param order_id 订单id
    @param order_id 增加数量
    */
    void addordasset(name sender, uint64_t order_id, extended_asset quantity);

    /*! @brief 用户减少订单预存额
    @param sender 账户名
    @param order_id 订单id
    @param order_id 减少数量
    */
    void subordasset(name sender, uint64_t order_id, extended_asset quantity);

    /*! @brief 更新订单状态
    @param payer 账户名
    @param order_id 订单id
    */
    void updateorder(name payer, uint64_t order_id);

    /*! @brief 创建nft族别
    @param nft_symbol nft族的符号
    @param symbol_uri nft族的描述
    @param type nft族类别(0: 721, 1: 1155)
    */
    void nftcreatesym(extended_symbol nft_symbol, std::string symbol_uri, nft_type type);

    /*! @brief 创建 nft
    @param to 创建目标账户
    @param nft_uri nft的描述
    @param nft_name nft名称
    @param extra_data 额外数据
    @param quantity 创建数量
    */
    void nftcreate(name to, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity);

    /*! @brief 增发nft
    @param to 增发目标账户
    @param nft_id 要增发nft的id
    @param quantity 增发数量
    */
    void nftissue(name to, uint64_t nft_id, extended_asset quantity);

    /*! @brief 转账nft
    @param from 转出账户
    @param to 转入账户
    @param nft_id 要转账nft的id
    @param quantity 转账数量
    @param memo 备注
    */
    void nfttransfer(name from, name to, uint64_t nft_id, extended_asset quantity, std::string memo);

    /*! @brief 批量转账nft
    @param from 转出账户
    @param to 转入账户
    @param batch_args 要转账nft的id和数量的数组
    @param memo 备注
    */
    void nfttransferb(name from, name to, std::vector<nft_batch_args> batch_args, std::string memo);

    /*! @brief 销毁nft
    @param from 销毁操作账户
    @param nft_id 要销毁nft的id
    @param quantity 销毁数量
    */
    void nftburn(name from, uint64_t nft_id, extended_asset quantity);

    /*! @brief 批量销毁nft
    @param from 销毁操作账户
    @param batch_args 要销毁nft的id和数量的数组
    */
    void burnbatch(name from, std::vector<nft_batch_args> batch_args);

private:
    void uniswaporder(account_name owner, extended_asset quantity, extended_asset to, double price, account_name id, account_name rampay);
    double get_real_asset(extended_asset quantity);

    template <typename T, T (*wipe_function)(T)>
    extended_asset get_asset_by_amount(T amount, extended_symbol symbol);

    void uniswapdeal(account_name owner, extended_asset& market_from, extended_asset& market_to, extended_asset from, extended_asset to_sym, uint64_t primary, double price, account_name rampay);

public:
    void receipt(extended_asset in, extended_asset out, extended_asset fee);
    void outreceipt(account_name owner, extended_asset x, extended_asset y);
    void traderecord(account_name owner, account_name oppo, extended_asset from, extended_asset to, extended_asset fee, uint64_t bid_id);
    void orderchange(uint64_t bid_id, uint8_t state);
    void bidrec(uint64_t price, extended_asset quantity, extended_asset filled, uint64_t bid_id);
    void pricerec(uint64_t old_price, uint64_t new_price);
    void uniswapsnap(account_name owner, extended_asset quantity);

public:
    void billrec(account_name owner, extended_asset asset, uint64_t bill_id, uint8_t state);
    void orderrec(account_name owner, account_name oppo, extended_asset sell, extended_asset buy, extended_asset reserve, uint64_t bill_id, uint64_t order_id);
    void incentiverec(account_name owner, extended_asset inc, uint64_t bill_id, uint64_t order_id, uint8_t type);
    void orderclarec(account_name owner, extended_asset quantity, uint64_t bill_id, uint64_t order_id);
    void redeemrec(account_name owner, account_name miner, extended_asset asset);
    void liqrec(account_name miner, extended_asset pst_asset, extended_asset dmc_asset);
    void makerliqrec(account_name miner, uint64_t bill_id, extended_asset sub_pst);
    void makercharec(account_name sender, account_name miner, extended_asset changed, MakerReceiptType type);
    void ordercharec(uint64_t order_id, extended_asset storage, extended_asset lock, extended_asset settlement, extended_asset challenge, time_point_sec exec_date, OrderReceiptType type);
    void assetcharec(account_name owner, extended_asset changed, uint8_t type, uint64_t id);

public:
    void nftsymrec(uint64_t symbol_id, extended_symbol nft_symbol, std::string symbol_uri, nft_type type);
    void nftrec(uint64_t symbol_id, uint64_t nft_id, std::string nft_uri, std::string nft_name, std::string extra_data, extended_asset quantity);
    void nftaccrec(uint64_t symbol_id, uint64_t nft_id, name owner, extended_asset quantity);

public:
    inline asset get_supply(symbol_type sym) const;

private:
    void change_pst(account_name owner, extended_asset value);
    /**
     * when issue add it
     **/
    void add_stats(extended_asset quantity);
    /**
     * when retrie, sub it
     */
    void sub_stats(extended_asset quantity);
    // void changestake(account_name owner, extended_asset asset, uint64_t primary);
    string uint32_to_string(uint32_t value);
    void trace_price_history(double price, uint64_t bill_id);

    ChallengeState get_challenge_state(uint64_t order_id);
    bool is_challenge_end(ChallengeState state);

private:
    uint64_t get_dmc_config(name key, uint64_t default_value);
    double get_dmc_rate(name key, uint64_t default_value);

private:
    struct nft_symbol_info {
        uint64_t symbol_id;
        extended_symbol nft_symbol;
        std::string symbol_uri;
        nft_type type;

        uint64_t primary_key() const { return symbol_id; }

        static uint128_t get_extended_symbol(extended_symbol symbol)
        {
            return ((uint128_t(symbol.name()) << 64) + symbol.contract);
        }
        uint128_t by_symbol() const { return get_extended_symbol(nft_symbol); }

        EOSLIB_SERIALIZE(nft_symbol_info, (symbol_id)(nft_symbol)(symbol_uri)(type))
    };

    typedef eosio::multi_index<N(nftsymbols), nft_symbol_info,
        indexed_by<N(extsymbol), const_mem_fun<nft_symbol_info, uint128_t, &nft_symbol_info::by_symbol>>>
        nft_symbols;

    struct nft_info {
        uint64_t nft_id;
        extended_asset supply;
        std::string nft_uri;
        std::string nft_name;
        std::string extra_data;

        uint64_t primary_key() const { return nft_id; }

        EOSLIB_SERIALIZE(nft_info, (nft_id)(supply)(nft_uri)(nft_name)(extra_data))
    };
    typedef eosio::multi_index<N(nftinfo), nft_info> nft_infos;

    struct nft_balance {
        uint64_t primary;
        name owner;
        uint64_t nft_id;
        extended_asset quantity;

        uint64_t primary_key() const { return primary; }
        uint64_t by_owner() const { return owner; }
        uint64_t by_nft_id() const { return nft_id; }
        static uint128_t get_owner_id(uint64_t owner, uint64_t nft_id)
        {
            return ((uint128_t(owner) << 64) + nft_id);
        }
        uint128_t by_owner_id() const { return get_owner_id(owner, nft_id); }

        EOSLIB_SERIALIZE(nft_balance, (primary)(owner)(nft_id)(quantity))
    };
    typedef eosio::multi_index<N(nftbalance), nft_balance,
        indexed_by<N(owner), const_mem_fun<nft_balance, uint64_t, &nft_balance::by_owner>>,
        indexed_by<N(nftid), const_mem_fun<nft_balance, uint64_t, &nft_balance::by_nft_id>>,
        indexed_by<N(ownerid), const_mem_fun<nft_balance, uint128_t, &nft_balance::by_owner_id>>>
        nft_balances;

    struct account {
        uint64_t primary;
        extended_asset balance;

        uint64_t primary_key() const { return primary; }
        static uint128_t key(extended_symbol symbol)
        {
            return ((uint128_t(symbol.name()) << 64) + symbol.contract);
        }
        uint128_t get_key() const { return key(balance.get_extended_symbol()); }

        EOSLIB_SERIALIZE(account, (primary)(balance))
    };

    typedef eosio::multi_index<N(accounts), account,
        indexed_by<N(byextendedasset), const_mem_fun<account, uint128_t, &account::get_key>>>
        accounts;

    struct lock_account : public account {
        time_point_sec lock_timestamp;

        static key256 key(extended_symbol symbol, time_point_sec lock_timestamp)
        {
            return key256::make_from_word_sequence<uint64_t>(symbol.name(), symbol.contract, uint64_t(lock_timestamp.sec_since_epoch()));
        }
        key256 get_key() const { return key(balance.get_extended_symbol(), lock_timestamp); }

        EOSLIB_SERIALIZE(lock_account, (primary)(balance)(lock_timestamp))
    };

    typedef eosio::multi_index<N(lockaccounts), lock_account,
        indexed_by<N(byextendedasset), const_mem_fun<lock_account, key256, &lock_account::get_key>>>
        lock_accounts;

    struct currency_stats {
        account_name issuer; // 发行者
        asset max_supply; // 最大可发行通证数量

        asset supply; // 当前流通通证数量
        asset reserve_supply; // 未流通通证数量

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

    struct market_pool {
        account_name owner;
        double weights;

        uint64_t primary_key() const { return owner; }
        EOSLIB_SERIALIZE(market_pool, (owner)(weights))
    };
    typedef eosio::multi_index<N(swappool), market_pool> swap_pool;

    struct bill_record {
        uint64_t primary;
        uint64_t bill_id;
        account_name owner;
        extended_asset matched;
        extended_asset unmatched;
        uint64_t price;
        time_point_sec created_at;
        time_point_sec updated_at;

        uint64_t primary_key() const { return primary; }
        uint64_t get_lower() const { return price; }
        uint64_t get_time() const { return uint64_t(updated_at.sec_since_epoch()); };
        uint64_t get_stake_id() const { return bill_id; }
        EOSLIB_SERIALIZE(bill_record, (primary)(bill_id)(owner)(matched)(unmatched)(price)(created_at)(updated_at))
    };
    typedef eosio::multi_index<N(stakerec), bill_record,
        indexed_by<N(bylowerprice), const_mem_fun<bill_record, uint64_t, &bill_record::get_lower>>,
        indexed_by<N(bytime), const_mem_fun<bill_record, uint64_t, &bill_record::get_time>>,
        indexed_by<N(byid), const_mem_fun<bill_record, uint64_t, &bill_record::get_stake_id>>>
        bill_stats;

    struct pst_stats {
        account_name owner;
        extended_asset amount;

        uint64_t primary_key() const { return owner; }
        uint64_t by_amount() const { return -amount.amount; }
        EOSLIB_SERIALIZE(pst_stats, (owner)(amount))
    };
    typedef eosio::multi_index<N(pststats), pst_stats> pststats;

    struct abo_stats {
        uint64_t stage;
        double user_rate;
        double foundation_rate;
        extended_asset total_release;
        extended_asset remaining_release;
        time_point_sec start_at;
        time_point_sec end_at;
        time_point_sec last_released_at;

        uint64_t primary_key() const { return stage; }
        EOSLIB_SERIALIZE(abo_stats, (stage)(user_rate)(foundation_rate)(total_release)(remaining_release)(start_at)(end_at)(last_released_at))
    };
    typedef eosio::multi_index<N(abostats), abo_stats> abostats;

    struct stake_id_args {
        account_name owner;
        extended_asset quantity;
        uint64_t price;
        time now;
        string memo;
    };

    struct order_id_args {
        account_name owner;
        account_name miner;
        uint64_t bill_id;
        extended_asset asset;
        extended_asset reserve;
        string memo;
        time_point_sec now;
    };

    struct dmc_config {
        account_name key;
        uint64_t value;

        uint64_t primary_key() const { return key; }

        EOSLIB_SERIALIZE(dmc_config, (key)(value))
    };
    typedef eosio::multi_index<N(dmcconfig), dmc_config> dmc_global;

    // dmc订单表
    struct dmc_order {
        uint64_t order_id; // 唯一性主键
        account_name user; // 购买者
        account_name miner; // 挂单者
        uint64_t bill_id;
        extended_asset user_pledge; // dmc
        extended_asset miner_pledge; // pst
        extended_asset price; // dmc per price
        extended_asset settlement_pledge;
        extended_asset lock_pledge;
        OrderState state;
        time_point_sec deliver_start_date;
        time_point_sec latest_settlement_date;

        uint64_t primary_key() const { return order_id; }
        uint64_t get_user() const { return user; }
        uint64_t get_miner() const { return miner; }
        EOSLIB_SERIALIZE(dmc_order, (order_id)(user)(miner)(bill_id)(user_pledge)(miner_pledge)(price)(settlement_pledge)(lock_pledge)(state)(deliver_start_date)(latest_settlement_date))
    };
    typedef eosio::multi_index<N(dmcorder), dmc_order,
        indexed_by<N(user), const_mem_fun<dmc_order, uint64_t, &dmc_order::get_user>>,
        indexed_by<N(miner), const_mem_fun<dmc_order, uint64_t, &dmc_order::get_miner>>>
        dmc_orders;

    // dmc 挑战表
    struct dmc_challenge {
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

        uint64_t primary_key() const { return order_id; }
        EOSLIB_SERIALIZE(dmc_challenge, (order_id)(pre_merkle_root)(pre_data_block_count)(merkle_root)(data_block_count)(merkle_submitter)(data_id)(hash_data)(challenge_times)(nonce)(state)(user_lock)(miner_pay)(challenge_date))
    };
    typedef eosio::multi_index<N(dmchallenge), dmc_challenge> dmc_challenges;

    struct limited_partner {
        account_name owner;
        extended_asset staked;
        time_point_sec updated_at;
    };

    // pst 铸造表
    struct dmc_maker {
        account_name miner;
        double current_rate; // r
        double miner_rate;
        double total_weight;
        extended_asset total_staked;

        uint64_t primary_key() const { return miner; }
        double by_rate() const { return current_rate; }
        EOSLIB_SERIALIZE(dmc_maker, (miner)(current_rate)(miner_rate)(total_weight)(total_staked))
    };
    typedef eosio::multi_index<N(dmcmaker), dmc_maker,
        indexed_by<N(byrate), const_mem_fun<dmc_maker, double, &dmc_maker::by_rate>>>
        dmc_makers;

    struct maker_pool {
        account_name owner;
        double weight;

        uint64_t primary_key() const { return owner; };
        EOSLIB_SERIALIZE(maker_pool, (owner)(weight))
    };
    typedef eosio::multi_index<N(makerpool), maker_pool> dmc_maker_pool;
    struct price_history {
        uint64_t primary;
        uint64_t bill_id;
        double price;
        time_point_sec created_at;

        uint64_t primary_key() const { return primary; }
        double by_prices() const { return -price; }
        uint64_t get_time() const { return uint64_t(created_at.sec_since_epoch()); };
        EOSLIB_SERIALIZE(price_history, (primary)(bill_id)(price)(created_at))
    };
    typedef eosio::multi_index<N(dmcprice), price_history,
        indexed_by<N(byprice), const_mem_fun<price_history, double, &price_history::by_prices>>,
        indexed_by<N(bytime), const_mem_fun<price_history, uint64_t, &price_history::get_time>>>
        price_table;

    struct price_avg {
        uint64_t primary = 0;
        double total = 0;
        uint64_t count = 0;
        double avg = 0;
        uint64_t primary_key() const { return primary; }
        EOSLIB_SERIALIZE(price_avg, (primary)(total)(count)(avg))
    };
    typedef eosio::multi_index<N(priceavg), price_avg> avg_table;

    struct order_migration {
        uint64_t current_id;
        time_point_sec begin_date;
        uint64_t primary_key() const { return 1; }
        EOSLIB_SERIALIZE(order_migration, (current_id)(begin_date))
    };
    typedef eosio::multi_index<N(ordermiginfo), order_migration> order_migration_table;

public:
    void set_order_migration(uint64_t order_id, account_name payer)
    {
        order_migration_table order_migration_tbl(_self, _self);
        if (order_migration_tbl.begin() == order_migration_tbl.end()) {
            order_migration_tbl.emplace(payer, [&](auto& o) {
                o.current_id = order_id;
                o.begin_date = time_point_sec(now());
            });
        } else {
            auto order_iter = order_migration_tbl.begin();
            order_migration_tbl.modify(order_iter, payer, [&](auto& o) {
                o.current_id = order_id;
            });
        }
    }

public:
    void ordermig(account_name payer, uint32_t limit);

private:
    inline static account_name get_foundation(account_name issuer)
    {
        return issuer == system_account ? eos_account : issuer;
    }

    void sub_balance(account_name owner, extended_asset value);
    void add_balance(account_name owner, extended_asset value, account_name ram_payer);

    void lock_sub_balance(account_name owner, extended_asset value, time_point_sec lock_timestamp);
    void lock_sub_balance(account_name foundation, extended_asset quantity, bool recur = false);
    void lock_add_balance(account_name owner, extended_asset value, time_point_sec lock_timestamp, account_name ram_payer);

    extended_asset get_balance(extended_asset quantity, account_name name);

private:
    uint64_t calbonus(account_name owner, uint64_t primary, account_name ram_payer);
    double cal_makerd_pst(extended_asset dmc_asset);
    double cal_current_rate(extended_asset dmc_asset, account_name owner);

private:
    void change_order(dmc_order& order, const dmc_challenge& challenge, time_point_sec current, uint64_t claims_interval, name payer);
    void update_order(dmc_order& order, const dmc_challenge& challenge, name payer);
    void destory_pst(const dmc_order& info);
    void claim_dmc_reward(const dmc_order& info, dmc_challenge& challenge, account_name payer);
};

asset token::get_supply(symbol_type sym) const
{
    stats statstable(_self, system_account);
    const auto& st = statstable.get(symbol_type(CORE_SYMBOL).name());
    return st.get_supply();
}

std::string token::uint32_to_string(uint32_t value)
{
    std::string result;
    do {
        result += "0123456789"[value % 10];
        value /= 10;
    } while (value);
    std::reverse(result.begin(), result.end());
    return result;
}

template <typename T>
checksum256 sha256(const T& value)
{
    auto digest = pack(value);
    checksum256 hash;
    ::sha256(digest.data(), uint32_t(digest.size()), &hash);
    return hash;
}
} // namespace eosio