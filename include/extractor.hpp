#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/crypto.hpp>

#include <atomicassets-interface.hpp>
#include <delphioracle-interface.hpp>

using namespace std;
using namespace eosio;

static constexpr name DEFAULT_MARKETPLACE_CREATOR = name("fees.atomic");


/**
* This function takes a vector of asset ids, sorts them and then returns the sha256 hash
* It should therefore return the same hash for two vectors if and only if both vectors include
* exactly the same asset ids in any order
*/
checksum256 hash_asset_ids(const vector <uint64_t> &asset_ids) {
    uint64_t asset_ids_array[asset_ids.size()];
    std::copy(asset_ids.begin(), asset_ids.end(), asset_ids_array);
    std::sort(asset_ids_array, asset_ids_array + asset_ids.size());

    return eosio::sha256((char *) asset_ids_array, sizeof(asset_ids_array));
};


CONTRACT extractor : public contract {
public:
    using contract::contract;
    //utility
    ACTION init();
    ACTION convcounters();

    //set version
    ACTION setversion(
        string new_version
    );

    // set address of apoc token
    ACTION setapocaddr(
        name token_contract
    );

    // claim token
    ACTION claim(
        name owner,
        asset token_to_withdraw
    );

    //consume counter : unique
    uint64_t consume_counter(name counter_name);

    // stake apoc items
    ACTION stake(
        name seller,
        vector <uint64_t> asset_ids,
    );

    // unstake apoc token
    ACTION unstake(
        uint64_t stake_id
    );



    void receive_token_transfer(
        name from,
        name to,
        asset quantity,
        string memo
    );

    ACTION lognewstake(
        uint64_t stake_id,
        name owner,
        vector <uint64_t> asset_ids,
        name collection_name,
    );

    ACTION lognewclaim(
        name owner,
        vector <uint64_t> asset_ids,
        double amount
    );


private:
    struct COUNTER_RANGE {
        name counter_name;
        uint64_t start_id;
        uint64_t end_id;
    };

    struct TOKEN {
        name   token_contract;
        symbol token_symbol;
    };

    TABLE balances_s {
        name           owner;
        vector <asset> quantities;

        uint64_t primary_key() const { return owner.value; };
    };

    typedef multi_index <name("balances"), balances_s> balances_t;


    TABLE stake_s { // table for staking pool
        uint64_t          stake_id;
        name              owner;
        vector <uint64_t> asset_ids;
        int64_t           offer_id; //-1 if no offer has been created yet, else the offer id
        name              collection_name;

        uint64_t primary_key() const { return stake_id; };

        checksum256 asset_ids_hash() const { return hash_asset_ids(asset_ids); };
    };

    typedef multi_index <name("stakes"), stake_s,
        indexed_by < name("assetidshash"), const_mem_fun < stake_s, checksum256, &stake_s::asset_ids_hash>>>
    stake_t;




    TABLE config_s {
        string              version                  = "1.3.2";
        uint64_t            stake_counter             = 0; 
        uint32_t            minimum_claim_duration =  1440; // 1 day
        uint32_t            minimum_calc_duaration = 720; //12 hours
        TOKEN               apoc_token               = {
            .token_symbol = symbol("APOC"),
            .token_contract = name("apocalyptics")};
        name                atomicassets_account     = atomicassets::ATOMICASSETS_ACCOUNT;
    };
    typedef singleton <name("config"), config_s>               config_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index <name("config"), config_s>             config_t_for_abi;


    stake_t        pool         = stake_t(get_self(), get_self().value);
    balances_t     balances     = balances_t(get_self(), get_self().value);
    counters_t     counters     = counters_t(get_self(), get_self().value);
    config_t       config       = config_t(get_self(), get_self().value);


    name get_collection_and_check_assets(name owner, vector <uint64_t> asset_ids);

    name get_collection_author(name collection_name);

    double get_collection_fee(name collection_name);




    name require_get_supported_token_contract(symbol token_symbol);

    SYMBOLPAIR require_get_symbol_pair(symbol listing_symbol, symbol settlement_symbol);


    bool is_token_supported(name token_contract, symbol token_symbol);

    bool is_symbol_supported(symbol token_symbol);

    void internal_withdraw_tokens(
        name withdrawer,
        asset quantity,
        string memo
    );

    void internal_add_balance(name owner, asset quantity);

    void internal_decrease_balance(name owner, asset quantity);

    void internal_transfer_assets(name to, vector <uint64_t> asset_ids, string memo);

};