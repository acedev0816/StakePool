#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

namespace atomicassets {

    static constexpr name ATOMICASSETS_ACCOUNT = name("atomicassets");

    struct TOKEN{
        name token_contract;
        symbol token_symbol;
    };

    struct FORMAT{
        string name;
        string type;
    };

    struct collections_s{
        name                collection_name;
        name                author;
        bool                allow_notify;
        vector<name>        authorized_accounts;
        vector<name>        notify_accounts;
        double              market_fee;
        vector<uint8_t>     serialized_data;

        uint64_t primary_key() const { return collection_name.value; };
    };
    typedef multi_index<name("collections"), collections_s> collections_t;


    //Scope: collection_name
    struct schemes_s{
        name                scheme_name;
        vector<FORMAT>      format;

        uint64_t primary_key() const { return scheme_name.value; }
    };
    typedef multi_index<name("schemes"), schemes_s> schemes_t;


    //Scope: collection_name
    struct presets_s{
        uint32_t            preset_id;
        name                scheme_name;
        bool                transferable;
        bool                burnable;
        uint32_t            max_supply;
        uint32_t            issued_supply;
        vector<uint8_t>     immustruct_serialized_data;

        uint64_t primary_key() const { return uint64_t{preset_id}; }
    };
    typedef multi_index<name("presets"), presets_s> presets_t;


    //Scope: owner
    struct assets_s{
        uint64_t            asset_id;
        name                collection_name;
        name                scheme_name;
        int32_t             preset_id;
        name                ram_payer;
        vector<asset>       backed_tokens;
        vector<uint8_t>     immustruct_serialized_data;
        vector<uint8_t>     mustruct_serialized_data;

        uint64_t primary_key() const { return asset_id; };
    };
    typedef multi_index<name("assets"), assets_s> assets_t;


    struct offers_s{
        uint64_t            offer_id;
        name                sender;
        name                recipient;
        vector<uint64_t>    sender_asset_ids;
        vector<uint64_t>    recipient_asset_ids;
        string              memo;

        uint64_t primary_key() const { return offer_id; };
        uint64_t by_sender() const { return sender.value; };
        uint64_t by_recipient() const { return recipient.value; };
    };
    typedef multi_index<name("offers"), offers_s,
        indexed_by < name("sender"), const_mem_fun < offers_s, uint64_t, &offers_s::by_sender>>,
    indexed_by<name("recipient"), const_mem_fun < offers_s, uint64_t, &offers_s::by_recipient>>>
    offers_t;

    struct balances_s{
        name                owner;
        vector<asset>       quantities;

        uint64_t primary_key() const { return owner.value; };
    };
    typedef multi_index<name("balances"), balances_s> balances_t;


    struct config_s{
        uint64_t            asset_counter = 1099511627780; //2^40
        int32_t             preset_counter = 1;
        uint64_t            offer_counter = 1;
        vector<FORMAT>      collection_format = {};
        vector<TOKEN>       supported_tokens = {};
    };
    typedef singleton<name("config"), config_s> config_t;

    struct tokenconfigs_s{
        name                standard = name("atomicassets");
        std::string         version = string("0.2.0");
    };
    typedef singleton<name("tokenconfigs"), tokenconfigs_s> tokenconfigs_t;


    collections_t collections = collections_t(ATOMICASSETS_ACCOUNT, ATOMICASSETS_ACCOUNT.value);
    offers_t offers = offers_t(ATOMICASSETS_ACCOUNT, ATOMICASSETS_ACCOUNT.value);
    balances_t balances = balances_t(ATOMICASSETS_ACCOUNT, ATOMICASSETS_ACCOUNT.value);
    config_t config = config_t(ATOMICASSETS_ACCOUNT, ATOMICASSETS_ACCOUNT.value);
    tokenconfigs_t tokenconfigs = tokenconfigs_t(ATOMICASSETS_ACCOUNT, ATOMICASSETS_ACCOUNT.value);

    assets_t get_assets(name acc) {
        return assets_t(ATOMICASSETS_ACCOUNT, acc.value);
    }

    schemes_t get_schemes(name collection_name) {
        return schemes_t(ATOMICASSETS_ACCOUNT, collection_name.value);
    }

    presets_t get_presets(name collection_name) {
        return presets_t(ATOMICASSETS_ACCOUNT, collection_name.value);
    }
};